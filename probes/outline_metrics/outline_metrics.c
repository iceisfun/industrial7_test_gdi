/* outline_metrics: GetOutlineTextMetricsW / GetOutlineTextMetricsA recorder.
 *
 * For each standard UI / reference face x height x style (regular, bold, italic, bold italic), on a screen DC
 * (GetDC(NULL), MM_TEXT, identity transform, current boot DPI), every row records the realised face (GetTextFaceW)
 * and every field of OUTLINETEXTMETRICW: size, the embedded TEXTMETRICW (all 20 fields), the 10 PANOSE bytes, fsSelection,
 * fsType, slope rise/run, italic angle, EM square, ascent/descent/line gap, cap/x height, font box, Mac metrics, minimum PPEM,
 * sub/superscript size+offset, strikeout and underscore size+position, and the four name-offset strings.
 *
 * usage: outline_metrics.exe [recorder options, see rec.h] [--quick]
 *   default grid: 16 faces (TrueType UI/reference faces, three bitmap faces and one absent name, which exercise the
 *                 failure path) x heights {-8,-9,-10,-11,-12,-13,-14,-16,-20,-24,-32, 8/9/10/12 pt at this DPI} x 4 styles
 *   --quick       5 faces x 5 heights x regular/bold
 *
 * Units: lengths in device pixels (lfHeight < 0 = character (em) height; the identity transform makes logical units equal
 * pixels) except where the column unit says otherwise (em:fu = font design units, ital:0.1deg).
 *
 * Tables (S lines carry the column names):
 *   ptmap  pt                    point size -> lfHeight = -MulDiv(pt, LOGPIXELSY, 72)
 *   otm    face;req_h;wt;it      got (realised face), need (GetOutlineTextMetricsW(dc,0,NULL)), ret (full call), then all fields
 *   otmname face;req_h;wt;it     the four strings reached through otmpFamilyName/otmpFaceName/otmpStyleName/otmpFullName,
 *                                 their offsets (bytes from the start of the structure), and the FONTSIGNATURE of the font
 *   otmcall face;req_h;wt;it     call contract: return for cb=0/NULL, cb=need-1, cb=sizeof(OUTLINETEXTMETRICW) with a real
 *                                 buffer, and the byte-wise ANSI variant (need, size, the four strings as bytes)
 * A face that cannot be created or selected gets a row with got=! and nothing else. A face for which the call fails
 * (none in the default grid: the System and Terminal raster faces also return a structure) gets need=0 ret=0 and `!` in every field. otmFiller is padding and is not recorded; the TEXTMETRICW is
 * recorded in full inside the otm row (prefix tm_). */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FACES[] = {
  "Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma","Arial","Courier New","Microsoft Sans Serif","Times New Roman",
  "Verdana","Consolas","Calibri","Lucida Console","Segoe UI Symbol","System","Terminal","NoSuchFace I7"
};
static const char *FACES_QUICK[] = {"Segoe UI","Tahoma","Arial","Courier New","System"};
static const int PX[] = {-8,-9,-10,-11,-12,-13,-14,-16,-20,-24,-32};
static const int PX_QUICK[] = {-9,-11,-12,-16,-24};
static const int PT[] = {8,9,10,12};

#define TM_COLS "tm_h:px tm_asc:px tm_desc:px tm_il:px tm_el:px tm_avgw:px tm_maxw:px tm_wt tm_ovh:px tm_dax:px tm_day:px tm_first tm_last tm_def tm_brk tm_it tm_ul tm_so tm_pf tm_cs"
#define OTM_COLS "got need ret size " TM_COLS " panose fssel fstype rise run ital:0.1deg em:fu asc:px desc:px lgap:px capem:px xh:px " \
  "box_l:px box_t:px box_r:px box_b:px mac_asc:px mac_desc:px mac_lgap:px minppem:px sub_sx:px sub_sy:px sub_ox:px sub_oy:px " \
  "sup_sx:px sup_sy:px sup_ox:px sup_oy:px so_size:px so_pos:px us_size:px us_pos:px"
#define OTM_NCOLS 56

static void wide(WCHAR *d, const char *s){ int i=0; for(;s[i]&&i<LF_FACESIZE-1;i++) d[i]=(WCHAR)(unsigned char)s[i]; d[i]=0; }
static void nas(rv *v, int n){ for(int i=0;i<n;i++) rv_na(v); }

static HFONT mk(const char *face, int h, int wt, int it){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=wt; lf.lfItalic=(BYTE)it; lf.lfCharSet=DEFAULT_CHARSET;
  wide(lf.lfFaceName,face); return CreateFontIndirectW(&lf);
}

/* a string at byte offset off of o (valid only if the whole string, including its terminator, lies inside the buffer) */
static void put_wstr(rv *v, const BYTE *b, UINT n, size_t off){
  if(!off || off>=n){ rv_na(v); return; }
  const WCHAR *s=(const WCHAR*)(b+off); WCHAR t[256]; size_t m=0;
  while(m<255 && off+2*(m+1)<=n && s[m]){ t[m]=s[m]; m++; }
  t[m]=0; if(m) rv_w(v,t); else rv_s(v,"");
}
static void put_astr(rv *v, const BYTE *b, UINT n, size_t off){
  if(!off || off>=n){ rv_na(v); return; }
  const char *s=(const char*)(b+off); char t[256]; size_t m=0;
  while(m<255 && off+m<n && s[m]){ t[m]=s[m]; m++; }
  t[m]=0; if(m) rv_s(v,t); else rv_s(v,"");
}

static void one(HDC dc, const char *face, int h, int wt, int it){
  rk k; rk_reset(&k); rk_s(&k,"face",face); rk_i(&k,"req_h",h); rk_i(&k,"wt",wt); rk_i(&k,"it",it);
  rv v; rv_reset(&v);
  HFONT hf=mk(face,h,wt,it); HGDIOBJ of=hf?SelectObject(dc,hf):NULL;
  if(!hf || !of || of==HGDI_ERROR){
    nas(&v,OTM_NCOLS); rec_row("otm",&k,&v); if(hf) DeleteObject(hf); return; }
  WCHAR got[LF_FACESIZE+1]; memset(got,0,sizeof got); if(GetTextFaceW(dc,LF_FACESIZE,got)) rv_w(&v,got); else rv_na(&v);
  UINT need=GetOutlineTextMetricsW(dc,0,NULL); rv_i(&v,need);
  OUTLINETEXTMETRICW *o=need?(OUTLINETEXTMETRICW*)calloc(1,need+64):NULL;
  UINT ret=o?GetOutlineTextMetricsW(dc,need,o):0; rv_i(&v,ret);
  if(!ret){ nas(&v,OTM_NCOLS-3); rec_row("otm",&k,&v);
    /* still record the call contract for faces without outlines */
    rv c; rv_reset(&c); BYTE tiny[8]; rv_i(&c,need); rv_i(&c,GetOutlineTextMetricsW(dc,1,(OUTLINETEXTMETRICW*)tiny)); rv_i(&c,GetOutlineTextMetricsW(dc,sizeof(OUTLINETEXTMETRICW),NULL));
    { UINT na=GetOutlineTextMetricsA(dc,0,NULL); rv_i(&c,na); } nas(&c,6);
    rec_row("otmcall",&k,&c);
    free(o); SelectObject(dc,of); DeleteObject(hf); return; }
  const TEXTMETRICW *t=&o->otmTextMetrics;
  rv_i(&v,o->otmSize);
  rv_i(&v,t->tmHeight); rv_i(&v,t->tmAscent); rv_i(&v,t->tmDescent); rv_i(&v,t->tmInternalLeading); rv_i(&v,t->tmExternalLeading);
  rv_i(&v,t->tmAveCharWidth); rv_i(&v,t->tmMaxCharWidth); rv_i(&v,t->tmWeight); rv_i(&v,t->tmOverhang);
  rv_i(&v,t->tmDigitizedAspectX); rv_i(&v,t->tmDigitizedAspectY);
  rv_x(&v,t->tmFirstChar); rv_x(&v,t->tmLastChar); rv_x(&v,t->tmDefaultChar); rv_x(&v,t->tmBreakChar);
  rv_i(&v,t->tmItalic); rv_i(&v,t->tmUnderlined); rv_i(&v,t->tmStruckOut); rv_x(&v,t->tmPitchAndFamily); rv_i(&v,t->tmCharSet);
  char pan[32]; rec_hex(pan,&o->otmPanoseNumber,10); rv_s(&v,pan);
  rv_x(&v,o->otmfsSelection); rv_x(&v,o->otmfsType); rv_i(&v,o->otmsCharSlopeRise); rv_i(&v,o->otmsCharSlopeRun);
  rv_i(&v,o->otmItalicAngle); rv_i(&v,o->otmEMSquare); rv_i(&v,o->otmAscent); rv_i(&v,o->otmDescent); rv_i(&v,o->otmLineGap);
  rv_i(&v,o->otmsCapEmHeight); rv_i(&v,o->otmsXHeight);
  rv_i(&v,o->otmrcFontBox.left); rv_i(&v,o->otmrcFontBox.top); rv_i(&v,o->otmrcFontBox.right); rv_i(&v,o->otmrcFontBox.bottom);
  rv_i(&v,o->otmMacAscent); rv_i(&v,o->otmMacDescent); rv_i(&v,o->otmMacLineGap); rv_i(&v,o->otmusMinimumPPEM);
  rv_i(&v,o->otmptSubscriptSize.x); rv_i(&v,o->otmptSubscriptSize.y); rv_i(&v,o->otmptSubscriptOffset.x); rv_i(&v,o->otmptSubscriptOffset.y);
  rv_i(&v,o->otmptSuperscriptSize.x); rv_i(&v,o->otmptSuperscriptSize.y); rv_i(&v,o->otmptSuperscriptOffset.x); rv_i(&v,o->otmptSuperscriptOffset.y);
  rv_i(&v,o->otmsStrikeoutSize); rv_i(&v,o->otmsStrikeoutPosition); rv_i(&v,o->otmsUnderscoreSize); rv_i(&v,o->otmsUnderscorePosition);
  rec_row("otm",&k,&v);

  /* names */
  { rv n; rv_reset(&n); const BYTE *b=(const BYTE*)o;
    size_t offs[4]={(size_t)o->otmpFamilyName,(size_t)o->otmpFaceName,(size_t)o->otmpStyleName,(size_t)o->otmpFullName};
    for(int i=0;i<4;i++) put_wstr(&n,b,ret,offs[i]);
    for(int i=0;i<4;i++) rv_i(&n,(long long)offs[i]);
    FONTSIGNATURE fs; memset(&fs,0,sizeof fs); GetTextCharsetInfo(dc,&fs,0);
    for(int i=0;i<4;i++) rv_x(&n,fs.fsUsb[i]);
    rv_x(&n,fs.fsCsb[0]); rv_x(&n,fs.fsCsb[1]);
    rec_row("otmname",&k,&n); }

  /* call contract + ANSI variant */
  { rv c; rv_reset(&c);
    rv_i(&c,need);
    rv_i(&c,GetOutlineTextMetricsW(dc,need-1,o));                          /* buffer one byte too small */
    rv_i(&c,GetOutlineTextMetricsW(dc,sizeof(OUTLINETEXTMETRICW),o));      /* header-sized buffer, no room for the strings */
    UINT na=GetOutlineTextMetricsA(dc,0,NULL); rv_i(&c,na);
    OUTLINETEXTMETRICA *a=na?(OUTLINETEXTMETRICA*)calloc(1,na+64):NULL; UINT ra=a?GetOutlineTextMetricsA(dc,na,a):0;
    rv_i(&c,ra);
    if(ra){ const BYTE *b=(const BYTE*)a; rv_i(&c,a->otmSize);
      size_t offs[4]={(size_t)a->otmpFamilyName,(size_t)a->otmpFaceName,(size_t)a->otmpStyleName,(size_t)a->otmpFullName};
      for(int i=0;i<4;i++) put_astr(&c,b,ra,offs[i]); }
    else nas(&c,5);
    rec_row("otmcall",&k,&c); free(a); }
  free(o); SelectObject(dc,of); DeleteObject(hf);
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"outline_metrics","1");
  int quick=0;
  for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--quick")) quick=1;
  HDC dc=GetDC(NULL); int dpi=GetDeviceCaps(dc,LOGPIXELSY);
  rec_env("outline_metrics.mode","%s",quick?"quick":"full");
  rec_note("units: lengths are device pixels (screen DC, MM_TEXT, identity transform); req_h is lfHeight as passed (negative = em height)");
  rec_note("signs: every field is exactly as GetOutlineTextMetricsW returned it (descent, mac_desc, box_b, offsets are not normalised); otmFiller is padding and is not recorded");
  rec_schema("ptmap","pt","req_h");
  rec_schema("otm","face req_h wt it",OTM_COLS);
  rec_schema("otmname","face req_h wt it","family face style full off_family off_face off_style off_full usb0 usb1 usb2 usb3 csb0 csb1");
  rec_schema("otmcall","face req_h wt it","need ret_short ret_hdr need_a ret_a size_a fam_a face_a style_a full_a");
  int hs[32], nh=0;
  if(quick){ for(unsigned i=0;i<sizeof PX_QUICK/sizeof*PX_QUICK;i++) hs[nh++]=PX_QUICK[i]; }
  else { for(unsigned i=0;i<sizeof PX/sizeof*PX;i++) hs[nh++]=PX[i];
    for(unsigned i=0;i<sizeof PT/sizeof*PT;i++){ int h=-MulDiv(PT[i],dpi,72), dup=0;
      for(int j=0;j<nh;j++) if(hs[j]==h) dup=1;
      rk k; rk_reset(&k); rk_i(&k,"pt",PT[i]); rv v; rv_reset(&v); rv_i(&v,h); rec_row("ptmap",&k,&v);
      if(!dup) hs[nh++]=h; } }
  const char **fc=quick?FACES_QUICK:FACES; int nf=quick?(int)(sizeof FACES_QUICK/sizeof*FACES_QUICK):(int)(sizeof FACES/sizeof*FACES);
  for(int f=0;f<nf;f++) for(int i=0;i<nh;i++)
    for(int wt=400;wt<=700;wt+=300) for(int it=0;it<=1;it++){
      if(quick && it) continue;
      one(dc,fc[f],hs[i],wt,it); }
  ReleaseDC(NULL,dc);
  return rec_finish(0);
}
