/* glyph_bitmaps: rasterisation fingerprints of the Win7 UI faces on Win10 (data for the Industrial7 D12 decision).
 *
 * usage: glyph_bitmaps.exe [recorder options, rec.h] [--full | --quick] [--only SECTION[,SECTION...]]
 *   --full (default; the VM runs this): all faces, heights -8..-16,-18,-20,-24,-32, full char set
 *   --quick: 4 faces, heights -11,-12,-16, core chars only (plumbing / fast check)
 *   sections: spi font gidx ggo fonttab text methods styles (the face table is always written)
 *
 * Everything is measured on a memory DC (CreateCompatibleDC(NULL), MM_TEXT, no world transform), so all lengths are
 * device pixels == logical units. Fonts are created with CreateFontIndirectW (DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
 * CLIP_DEFAULT_PRECIS); lfHeight < 0 is the character (em) height in pixels. Pixel-height results do not depend on the
 * system DPI (docs/VM_RUN.md 5.1), so one run at 96 DPI suffices; ENV records DPI and font-smoothing state anyway.
 *
 * Tables (S lines carry the columns; docs/probes/glyph_bitmaps.md explains each):
 *   face      per face: present?, realised name, TrueType?, file length / head checkSumAdjustment / sha (alias targets)
 *   spi       SystemParametersInfo font smoothing state
 *   font      per face x height: realised face + TEXTMETRICW + GetFontData size + otmEMSquare
 *   gidx      GetGlyphIndicesW over the char set (flags 0 and GGI_MARK_NONEXISTING_GLYPHS)
 *   gm        GetGlyphOutlineW per char: GGO_METRICS GLYPHMETRICS + length/hash of GGO_BITMAP/GRAY2/4/8,
 *             with GGO_GLYPH_INDEX parity bits;  gmf: GLYPHMETRICS of a bitmap format when it differs from GGO_METRICS
 *   gmo       GGO_NATIVE outline hashes hinted + GGO_UNHINTED, and the GGO_UNHINTED parity mask of metrics/bitmaps
 *   ggo_nat   raw GGO_NATIVE bytes (hex) for the core chars at -11 -12 -16 -24, hinted and unhinted
 *   ggo_all   H lines: one hash over the whole char set per face x height x format (aliases included)
 *   ggo_bm/ggo_g2/ggo_g4/ggo_g8  B lines: raw GGO bytes for the core chars (bbx x bby, raw levels)
 *   ffile, ftab, fhead, fhhea, fos2, gasp, eblc, hdmx, vdmx   GetFontData tables (lengths, hashes, parsed fields)
 *   tx, txprof, txb   TextOutW renders into 32bpp / 8bpp-grey DIB sections (ink box, ink sum, profiles, raw crops)
 *   txm       ExtTextOutW (ETO_OPAQUE / ETO_CLIPPED / ETO_GLYPH_INDEX / lpDx), DrawTextW, 1bpp DIB, vs TextOutW
 *   sty, styb bold/italic/underline/strikeout: synthesized vs real faces
 */
#include "gb.h"

int g_full = 1;

const gb_face g_faces[] = {
  {"Segoe UI",0,0}, {"Tahoma",0,0}, {"Microsoft Sans Serif",0,0}, {"Arial",0,0}, {"Courier New",0,0},
  {"MS Shell Dlg",1,0}, {"MS Shell Dlg 2",1,0},
  {"MS Sans Serif",0,1}, {"Courier",0,1}, {"System",0,1},
};
const int g_nfaces = sizeof g_faces / sizeof *g_faces;

/* core: the letters of Name / OK / Cancel / Size / Hg plus A W i 0 . (ascending code order, so the core subset of g_all
 * has the same order for every face and the ggo_all set=core hashes compare across faces) */
const WCHAR g_core[] = L".0ACHKNOSWacegilmnz";
WCHAR g_all[256]; int g_nall;
static const WCHAR g_extra[] = { 0xA0, 0xA9, 0xB0, 0xB5, 0xBF, 0xC5, 0xC9, 0xD6, 0xD7, 0xD8, 0xDF, 0xE6, 0xE9, 0xF1, 0xFC,
                                 0x2026, 0x20AC, 0x2122, 0 };

HFONT gb_font(const char *face, int h, int weight, int italic, int underline, int strike, int quality){
  LOGFONTW lf; memset(&lf,0,sizeof lf);
  lf.lfHeight=h; lf.lfWeight=weight; lf.lfItalic=(BYTE)italic; lf.lfUnderline=(BYTE)underline; lf.lfStrikeOut=(BYTE)strike;
  lf.lfCharSet=DEFAULT_CHARSET; lf.lfOutPrecision=OUT_DEFAULT_PRECIS; lf.lfClipPrecision=CLIP_DEFAULT_PRECIS;
  lf.lfQuality=(BYTE)quality; lf.lfPitchAndFamily=DEFAULT_PITCH|FF_DONTCARE;
  MultiByteToWideChar(CP_ACP,0,face,-1,lf.lfFaceName,LF_FACESIZE);
  return CreateFontIndirectW(&lf);
}
void gb_short_hash(char out[17], const void *p, size_t n){ uint8_t d[32]; rec_sha256(p,n,d); rec_hex(out,d,8); }
const char *gb_qname(int q){
  switch(q){ case DEFAULT_QUALITY: return "default"; case NONANTIALIASED_QUALITY: return "nonaa"; case ANTIALIASED_QUALITY: return "aa";
             case CLEARTYPE_QUALITY: return "cleartype"; case CLEARTYPE_NATURAL_QUALITY: return "ctnatural"; }
  return "other";
}

/* ---------------------------------------------------------------- spi */
static void sec_spi(void){
  rec_schema("spi","name","ret val");
  struct { const char *n; UINT a; } t[] = { {"FONTSMOOTHING",SPI_GETFONTSMOOTHING}, {"FONTSMOOTHINGTYPE",SPI_GETFONTSMOOTHINGTYPE},
    {"FONTSMOOTHINGCONTRAST",SPI_GETFONTSMOOTHINGCONTRAST}, {"FONTSMOOTHINGORIENTATION",SPI_GETFONTSMOOTHINGORIENTATION},
    {"CLEARTYPE",0x1048 /* SPI_GETCLEARTYPE */} };
  for(unsigned i=0;i<sizeof t/sizeof*t;i++){
    UINT v=0; BOOL r=SystemParametersInfoW(t[i].a,0,&v,0);
    rk k; rk_reset(&k); rk_s(&k,"name",t[i].n); rv x; rv_reset(&x); rv_i(&x,r!=0); if(r) rv_u(&x,v); else rv_na(&x);
    rec_row("spi",&k,&x);
  }
}

/* ---------------------------------------------------------------- font: realised face per height */
const int H_FULL[13] = {-8,-9,-10,-11,-12,-13,-14,-15,-16,-18,-20,-24,-32};
const int H_QUICK[3] = {-11,-12,-16};
static void sec_font(void){
  rec_schema("font","face req_h","got h:px asc:px desc:px il:px el:px ave:px max:px weight ovh:px pf cs first last def brk fontdata_len:B em:fu");
  HDC dc=CreateCompatibleDC(NULL);
  const int *hs=g_full?H_FULL:H_QUICK; int nh=g_full?13:3;
  for(int fi=0;fi<g_nfaces;fi++){
    if(!g_full && !(fi==0 || fi==1 || fi==3 || fi==5)) continue;   /* absent faces too: the row shows the substitute */
    for(int hi=0;hi<nh;hi++){
      HFONT f=gb_font(g_faces[fi].face,hs[hi],FW_NORMAL,0,0,0,DEFAULT_QUALITY), o=(HFONT)SelectObject(dc,f);
      WCHAR got[LF_FACESIZE]=L""; TEXTMETRICW tm; memset(&tm,0,sizeof tm);
      int okf=GetTextFaceW(dc,LF_FACESIZE,got)>0, okt=GetTextMetricsW(dc,&tm);
      DWORD fl=GetFontData(dc,0,0,NULL,0); UINT on=GetOutlineTextMetricsW(dc,0,NULL); int em=-1;
      if(on && on!=GDI_ERROR){ OUTLINETEXTMETRICW *otm=(OUTLINETEXTMETRICW*)calloc(1,on); if(GetOutlineTextMetricsW(dc,on,otm)) em=(int)otm->otmEMSquare; free(otm); }
      rk k; rk_reset(&k); rk_s(&k,"face",g_faces[fi].face); rk_i(&k,"req_h",hs[hi]);
      rv v; rv_reset(&v); if(okf) rv_w(&v,got); else rv_na(&v);
      if(okt){ rv_i(&v,tm.tmHeight); rv_i(&v,tm.tmAscent); rv_i(&v,tm.tmDescent); rv_i(&v,tm.tmInternalLeading); rv_i(&v,tm.tmExternalLeading);
        rv_i(&v,tm.tmAveCharWidth); rv_i(&v,tm.tmMaxCharWidth); rv_i(&v,tm.tmWeight); rv_i(&v,tm.tmOverhang); rv_x(&v,tm.tmPitchAndFamily);
        rv_i(&v,tm.tmCharSet); rv_x(&v,tm.tmFirstChar); rv_x(&v,tm.tmLastChar); rv_x(&v,tm.tmDefaultChar); rv_x(&v,tm.tmBreakChar); }
      else for(int i=0;i<15;i++) rv_na(&v);
      if(fl==GDI_ERROR) rv_na(&v); else rv_u(&v,fl);
      if(em<0) rv_na(&v); else rv_i(&v,em);
      rec_row("font",&k,&v);
      SelectObject(dc,o); DeleteObject(f);
    }
  }
  DeleteDC(dc);
}

/* ---------------------------------------------------------------- gidx */
static void sec_gidx(void){
  rec_schema("gidx","face flags","ret n indices");
  static const WCHAR probe_missing[] = { 0x0001, 0x4E00, 0xE000, 0xFFFF, 0 };
  WCHAR s[300]; int n=0; for(int i=0;i<g_nall;i++) s[n++]=g_all[i]; for(int i=0;probe_missing[i];i++) s[n++]=probe_missing[i];
  HDC dc=CreateCompatibleDC(NULL);
  for(int fi=0;fi<g_nfaces;fi++){
    HFONT f=gb_font(g_faces[fi].face,-12,FW_NORMAL,0,0,0,DEFAULT_QUALITY), o=(HFONT)SelectObject(dc,f);
    for(int fl=0;fl<2;fl++){
      WORD gi[300]; memset(gi,0xCD,sizeof gi);
      DWORD r=GetGlyphIndicesW(dc,s,n,gi,fl?GGI_MARK_NONEXISTING_GLYPHS:0);
      rk k; rk_reset(&k); rk_s(&k,"face",g_faces[fi].face); rk_x(&k,"flags",fl?GGI_MARK_NONEXISTING_GLYPHS:0);
      rv v; rv_reset(&v); if(r==GDI_ERROR) rv_na(&v); else rv_u(&v,r); rv_i(&v,n);
      if(r!=GDI_ERROR){ char buf[2400]; int bl=0; for(int i=0;i<n;i++) bl+=snprintf(buf+bl,sizeof buf-bl,"%s%u",i?",":"",gi[i]); rv_raw(&v,buf); }
      else rv_na(&v);
      rec_row("gidx",&k,&v);
    }
    /* key for the list order */
    SelectObject(dc,o); DeleteObject(f);
  }
  { char buf[2400]; int bl=0; for(int i=0;i<n;i++) bl+=snprintf(buf+bl,sizeof buf-bl,"%s%x",i?",":"",s[i]); rec_note("gidx chars (hex, in order): %s",buf); }
  DeleteDC(dc);
}

/* ---------------------------------------------------------------- face: is the requested face really there?
 * A non-alias face is present when GetTextFaceW returns the requested name (case-insensitive); an alias (MS Shell Dlg,
 * MS Shell Dlg 2) is present when it realises at all (GetTextFaceW reports the alias name, so the file it maps to is
 * identified by fontdata_len + the head checkSumAdjustment instead). An absent face gets its face row (present=0) and
 * font/gidx rows (they show the substitute) but no ggo/fonttab/text/methods/styles rows: the probe never records a
 * substitute's glyphs under the requested name. */
static int g_present[16];
#define TAG_HEAD_M 0x64616568u
static void sec_face(void){
  rec_schema("face","face","kind got present tt fontdata_len:B head_csum file_sha");
  rec_note("face: kind outline|alias|raster (what the probe expects); got = GetTextFaceW at lfHeight -12; present = "
           "requested face realised (alias: realised at all); tt = TMPF_TRUETYPE; fontdata_len = GetFontData(0) size; "
           "head_csum = head.checkSumAdjustment; file_sha = first 64 bits of SHA-256 of the whole font data (! for raster)");
  HDC dc=CreateCompatibleDC(NULL);
  for(int fi=0;fi<g_nfaces;fi++){
    const gb_face *F=&g_faces[fi];
    HFONT f=gb_font(F->face,-12,FW_NORMAL,0,0,0,DEFAULT_QUALITY), o=(HFONT)SelectObject(dc,f);
    WCHAR got[LF_FACESIZE]=L"", want[LF_FACESIZE]=L""; MultiByteToWideChar(CP_ACP,0,F->face,-1,want,LF_FACESIZE);
    int okf=f && GetTextFaceW(dc,LF_FACESIZE,got)>0;
    TEXTMETRICW tm; memset(&tm,0,sizeof tm); int okt=GetTextMetricsW(dc,&tm);
    int present = okf && (F->alias || !lstrcmpiW(got,want));
    if(fi<16) g_present[fi]=present;
    rk k; rk_reset(&k); rk_s(&k,"face",F->face);
    rv v; rv_reset(&v); rv_s(&v,F->alias?"alias":F->raster?"raster":"outline"); if(okf) rv_w(&v,got); else rv_na(&v);
    rv_i(&v,present); if(okt) rv_i(&v,(tm.tmPitchAndFamily&TMPF_TRUETYPE)!=0); else rv_na(&v);
    DWORD fl=GetFontData(dc,0,0,NULL,0);
    if(fl==GDI_ERROR || fl==0){ rv_na(&v); rv_na(&v); rv_na(&v); }
    else {
      rv_u(&v,fl); uint8_t cs[4]; DWORD r=GetFontData(dc,TAG_HEAD_M,8,cs,4);
      if(r==4) rv_x(&v,(unsigned long)cs[0]<<24|(unsigned long)cs[1]<<16|(unsigned long)cs[2]<<8|cs[3]); else rv_na(&v);
      uint8_t *b=(uint8_t*)malloc(fl); char h[17];
      if(b && GetFontData(dc,0,0,b,fl)==fl){ gb_short_hash(h,b,fl); rv_raw(&v,h); } else rv_na(&v);
      free(b);
    }
    rec_row("face",&k,&v);
    if(!present) rec_note("absent face=%s (realised as %s): ggo/fonttab/text/methods/styles rows skipped", F->face, okf?"other":"nothing");
    SelectObject(dc,o); DeleteObject(f);
  }
  DeleteDC(dc);
}

/* --quick: Segoe UI, Tahoma, Arial, MS Shell Dlg. Absent faces are off in every mode (see sec_face). */
int gb_face_on(int fi){ return (g_full || fi==0 || fi==1 || fi==3 || fi==5) && (fi>=16 || g_present[fi]); }

int main(int argc, char **argv){
  rec_init(&argc,argv,"glyph_bitmaps","2");
  const char *only=NULL;
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--quick")) g_full=0; else if(!strcmp(argv[i],"--full")) g_full=1;
    else if(!strcmp(argv[i],"--only") && i+1<argc) only=argv[++i];
  }
  g_nall=0; for(WCHAR c=0x20;c<=0x7E;c++) g_all[g_nall++]=c; for(int i=0;g_extra[i];i++) g_all[g_nall++]=g_extra[i];
  rec_note("mode=%s only=%s", g_full?"full":"quick", only?only:"all");
  rec_flush();
#define RUN(name, fn) do{ if(!only || strstr(only,name)){ DWORD t0=GetTickCount(); fn(); rec_note("section %s rows=%d", name, rec_rows()); \
                          fprintf(stderr,"section %s %lu ms\n",name,(unsigned long)(GetTickCount()-t0)); rec_flush(); } }while(0)
  sec_face();               /* always: presence decides what the other sections record */
  RUN("spi",sec_spi);
  RUN("font",sec_font);
  RUN("gidx",sec_gidx);
  RUN("fonttab",sec_fonttab);
  RUN("ggo",sec_ggo);
  RUN("text",sec_text);
  RUN("methods",sec_methods);
  RUN("styles",sec_styles);
  rec_note("glyph_bitmaps done");
  return rec_finish(0);
}
