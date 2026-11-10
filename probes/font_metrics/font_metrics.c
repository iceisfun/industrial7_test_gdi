/* font_metrics: TEXTMETRIC / OUTLINETEXTMETRIC / face-mapping recorder for the D12 decision.
 *
 * For a matrix of faces x lfHeight x weight x italic x quality x charset x graphics mode, on a screen DC
 * (GetDC(NULL), MM_TEXT, current boot DPI), every row records:
 *   GetTextFaceW (+ the NULL-buffer length), GetTextMetricsW (all 20 fields), GetTextMetricsA (char bytes and
 *   whether its other fields equal the W ones), GetObjectW round trip of the LOGFONTW, SelectObject return check,
 *   GetFontLanguageInfo, GetCharacterPlacementW return (flags 0 and the FLI_MASK flags), GetTextCharsetInfo,
 *   52-letter GetTextExtentPoint32W, GetFontRealizationInfo (flags, file count, face index, simulations) and the
 *   realised font FILE (GetFontFileInfo, basename, lowercase).
 * Per (face, req_h): H line = SHA-256 of the concatenated TEXTMETRICW structs of that group (fixed order).
 * GetOutlineTextMetricsW for the main grid (q=default cs=DEFAULT gm=compat), every field, plus names at -11/-12/-16.
 * Stock fonts (GetStockObject 10..17) and the SystemParametersInfo UI fonts (NONCLIENTMETRICS, ICONMETRICS,
 * SPI_GETICONTITLELOGFONT, and SystemParametersInfoForDpi at 96..192 when available): LOGFONT + the same TM row.
 *
 * Units: all TEXTMETRIC / OTM lengths in device pixels (screen DC, MM_TEXT, no world transform, so lu == px).
 * req_h is lfHeight exactly as passed: negative = character (em) height, positive = cell height, 0 = default.
 *
 * Tables: tm (face;req_h;wt;it;q;cs;gm), tmhash (H per face;req_h: SHA-256 of the group's TEXTMETRICW fields packed
 *   as 20 int32 in row order), otm (face;req_h;wt;it), otmname (names via otmp* offsets + FONTSIGNATURE, req_h -11/-12/-16),
 *   stock_lf / stock_tm (obj;via=stock|recreate) / stock_tmhash, ui_lf / ui_tm / ui_tmhash (src;dpi, dpi=sys or the
 *   SystemParametersInfoForDpi value), ncm, icm.
 * Grid per (face, req_h): main = wt{400,700} x it{0,1} at q=default cs=1 gm=compat; quality axis (7 lfQuality values
 *   incl. PROOF) at 400/0/cs=1; charset axis {0,2(symbol faces only),255,161,204,128,177,178} at 400/0/q=default;
 *   one gm=adv row (GM_ADVANCED DC: no MS Shell Dlg height snap). Column `a_ch` = TEXTMETRICA first<<24|last<<16|
 *   default<<8|break; `a_eq` = all other A fields equal W; `go_eq` = GetObjectW returned the passed LOGFONTW byte for
 *   byte; `sel` = SelectObject returned the previous font and gave f back; `gcp0`/`gcpf` = GetCharacterPlacementW
 *   return (cy<<16|cx) for "Hello World" with flags 0 / GetFontLanguageInfo&FLI_MASK; `ri_*` = GetFontRealizationInfo
 *   (flags byte|byte5<<8, file count, face index, simulations bit0 bold bit1 italic); `file` = GetFontFileInfo name.
 *
 * usage: font_metrics.exe [recorder options] [--full | --quick] [--no-otm] [--no-grid]
 *   --full (default): 21 faces x 71 heights, ~27k tm rows + ~6k otm rows (~8-9 MB of text, < 1 min on Win10)
 *   --quick: 8 UI faces x 17 heights, fewer qualities/charsets (~3k rows) */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- matrix */
static const char *FACES_FULL[] = {
  "Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma","Arial","Courier New","Courier","Microsoft Sans Serif",
  "MS Sans Serif","System","Terminal","Fixedsys","Lucida Console","Consolas","Segoe UI Symbol","Verdana",
  "Times New Roman","Calibri","Marlett","","NoSuchFace I7"
};
static const char *FACES_QUICK[] = {
  "Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma","Arial","Courier New","Microsoft Sans Serif","System"
};
static const int H_QUICK[] = {-8,-9,-10,-11,-12,-13,-14,-15,-16,-18,-20,-24,0,13,15,16,19};

typedef struct { int v; const char *name; } qual_t;
static const qual_t QUALS[] = {
  {DEFAULT_QUALITY,"default"},{DRAFT_QUALITY,"draft"},{PROOF_QUALITY,"proof"},{NONANTIALIASED_QUALITY,"nonaa"},
  {ANTIALIASED_QUALITY,"aa"},{CLEARTYPE_QUALITY,"ct"},{CLEARTYPE_NATURAL_QUALITY,"ctn"}
};
static const int QUALS_QUICK[] = {0,3,5}; /* indexes into QUALS */
/* charsets; DEFAULT_CHARSET (1) is the main grid */
static const int CS_FULL[] = {ANSI_CHARSET,SYMBOL_CHARSET,OEM_CHARSET,GREEK_CHARSET,RUSSIAN_CHARSET,SHIFTJIS_CHARSET,HEBREW_CHARSET,ARABIC_CHARSET};
static const int CS_QUICK[] = {ANSI_CHARSET,OEM_CHARSET,RUSSIAN_CHARSET};
static int symbol_meaningful(const char *face){
  return !strcmp(face,"Marlett") || !strcmp(face,"Segoe UI Symbol") || !strcmp(face,"Arial") || !face[0];
}

static const WCHAR ALPHA52[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static const WCHAR HELLO[] = L"Hello World";

/* ---------------------------------------------------------------- optional exports */
typedef BOOL (WINAPI *pGetFontRealizationInfo)(HDC, void *);
typedef BOOL (WINAPI *pGetFontFileInfo)(DWORD, DWORD, void *, SIZE_T, SIZE_T *);
typedef BOOL (WINAPI *pSPIForDpi)(UINT, UINT, PVOID, UINT, UINT);
static pGetFontRealizationInfo fnRI;
static pGetFontFileInfo fnFFI;
static pSPIForDpi fnSPIDpi;
static DWORD ri_size; /* struct size accepted by GetFontRealizationInfo, 0 = unusable */

/* realization info buffer: Win10 kernel accepts 0x10 / 0x32c (NtGdiGetRealizationInfo); Wine 0x10 / 0x18 */
typedef struct { DWORD size; BYTE flags, b5; WORD w6; DWORD cache_num, instance_id, file_count; WORD face_index, simulations; BYTE rest[0x32c-0x18]; } ri_buf;

static void probe_ri_size(HDC dc){
  static const DWORD sizes[] = {0x32c, 0x18, 0x10};
  if(!fnRI) return;
  for(unsigned i=0;i<3;i++){ ri_buf r; memset(&r,0,sizeof r); r.size=sizes[i]; if(fnRI(dc,&r)){ ri_size=sizes[i]; return; } }
}

/* ---------------------------------------------------------------- table schemas */
#define TM_COLS "got facelen h:px a:px d:px il:px el:px ave:px max:px wt oh:px dax day first last dflt brk it ul so pf cs " \
                "a_ch a_eq go_ret go_eq sel fli gcp0 gcpf tci_cs ext52_cx:px ext52_cy:px ri_flags ri_files ri_face ri_sim file"
#define LF_COLS "h w esc ori wt it ul so cs op cp q pf face"

static int ncols(const char *cols){ int n=0, in=0; for(const char *p=cols;*p;p++){ if(*p==' ') in=0; else if(!in){ in=1; n++; } } return n; }
static void pad_na(rv *v, int n){ for(int i=0;i<n;i++) rv_na(v); }

/* Canonical TEXTMETRICW bytes for hashing/comparing: the 20 fields as little-endian int32 (no struct padding). */
typedef struct { int32_t f[20]; } tm_packed;
static void tm_pack(const TEXTMETRICW *t, tm_packed *o){
  int32_t *f=o->f;
  f[0]=t->tmHeight; f[1]=t->tmAscent; f[2]=t->tmDescent; f[3]=t->tmInternalLeading; f[4]=t->tmExternalLeading;
  f[5]=t->tmAveCharWidth; f[6]=t->tmMaxCharWidth; f[7]=t->tmWeight; f[8]=t->tmOverhang; f[9]=t->tmDigitizedAspectX;
  f[10]=t->tmDigitizedAspectY; f[11]=t->tmFirstChar; f[12]=t->tmLastChar; f[13]=t->tmDefaultChar; f[14]=t->tmBreakChar;
  f[15]=t->tmItalic; f[16]=t->tmUnderlined; f[17]=t->tmStruckOut; f[18]=t->tmPitchAndFamily; f[19]=t->tmCharSet;
}
static int tm_equal(const TEXTMETRICW *a, const TEXTMETRICW *b){ tm_packed x,y; tm_pack(a,&x); tm_pack(b,&y); return !memcmp(&x,&y,sizeof x); }
static void hash_tm(const char *table, const rk *k, const TEXTMETRICW *t){ tm_packed p; tm_pack(t,&p); rec_hash(table,k,&p,sizeof p); }

static void lower_base(const WCHAR *path, char *out, size_t cap){
  const WCHAR *b=path; for(const WCHAR *p=path;*p;p++) if(*p=='\\'||*p=='/') b=p+1;
  WCHAR tmp[MAX_PATH]; size_t n=0; for(;b[n]&&n<MAX_PATH-1;n++) tmp[n]=(b[n]>='A'&&b[n]<='Z')?(WCHAR)(b[n]+32):b[n]; tmp[n]=0;
  if(!WideCharToMultiByte(CP_UTF8,0,tmp,-1,out,(int)cap,NULL,NULL)) snprintf(out,cap,"?");
}

/* Selects f into dc, appends the TM columns to v (and writes the TEXTMETRICW to *tmout), restores the DC.
 * lf = what was passed to CreateFontIndirectW (NULL for stock objects: GetObject round trip vs itself). */
static void measure(HDC dc, HFONT f, const LOGFONTW *lf, rv *v, TEXTMETRICW *tmout){
  memset(tmout,0,sizeof *tmout);
  if(!f){ pad_na(v,ncols(TM_COLS)); return; }
  HGDIOBJ cur=GetCurrentObject(dc,OBJ_FONT);
  HGDIOBJ prev=SelectObject(dc,f);
  /* face */
  WCHAR face[LF_FACESIZE*2]; face[0]=0;
  int fl=GetTextFaceW(dc,0,NULL);
  int fr=GetTextFaceW(dc,LF_FACESIZE*2,face);
  if(fr>0) rv_w(v,face); else rv_na(v);
  rv_i(v,fl);
  /* TEXTMETRICW */
  TEXTMETRICW tm; memset(&tm,0,sizeof tm);
  int okw=GetTextMetricsW(dc,&tm);
  if(okw){
    rv_i(v,tm.tmHeight); rv_i(v,tm.tmAscent); rv_i(v,tm.tmDescent); rv_i(v,tm.tmInternalLeading); rv_i(v,tm.tmExternalLeading);
    rv_i(v,tm.tmAveCharWidth); rv_i(v,tm.tmMaxCharWidth); rv_i(v,tm.tmWeight); rv_i(v,tm.tmOverhang);
    rv_i(v,tm.tmDigitizedAspectX); rv_i(v,tm.tmDigitizedAspectY);
    rv_x(v,tm.tmFirstChar); rv_x(v,tm.tmLastChar); rv_x(v,tm.tmDefaultChar); rv_x(v,tm.tmBreakChar);
    rv_i(v,tm.tmItalic); rv_i(v,tm.tmUnderlined); rv_i(v,tm.tmStruckOut); rv_x(v,tm.tmPitchAndFamily); rv_i(v,tm.tmCharSet);
    *tmout=tm;
  } else for(int i=0;i<20;i++) rv_na(v);
  /* TEXTMETRICA: char bytes packed first<<24|last<<16|default<<8|break; a_eq = all other fields equal W */
  TEXTMETRICA ta; memset(&ta,0,sizeof ta);
  if(GetTextMetricsA(dc,&ta)){
    rv_x(v,((unsigned)(BYTE)ta.tmFirstChar<<24)|((unsigned)(BYTE)ta.tmLastChar<<16)|((unsigned)(BYTE)ta.tmDefaultChar<<8)|(BYTE)ta.tmBreakChar);
    int eq = okw && ta.tmHeight==tm.tmHeight && ta.tmAscent==tm.tmAscent && ta.tmDescent==tm.tmDescent &&
      ta.tmInternalLeading==tm.tmInternalLeading && ta.tmExternalLeading==tm.tmExternalLeading &&
      ta.tmAveCharWidth==tm.tmAveCharWidth && ta.tmMaxCharWidth==tm.tmMaxCharWidth && ta.tmWeight==tm.tmWeight &&
      ta.tmOverhang==tm.tmOverhang && ta.tmDigitizedAspectX==tm.tmDigitizedAspectX && ta.tmDigitizedAspectY==tm.tmDigitizedAspectY &&
      ta.tmItalic==tm.tmItalic && ta.tmUnderlined==tm.tmUnderlined && ta.tmStruckOut==tm.tmStruckOut &&
      ta.tmPitchAndFamily==tm.tmPitchAndFamily && ta.tmCharSet==tm.tmCharSet;
    rv_i(v,eq);
  } else { rv_na(v); rv_na(v); }
  /* GetObjectW round trip */
  LOGFONTW back; memset(&back,0,sizeof back);
  int gr=GetObjectW(f,sizeof back,&back);
  rv_i(v,gr);
  if(lf) rv_i(v, gr==(int)sizeof back && !memcmp(&back,lf,sizeof back)); else rv_na(v);
  /* SelectObject: returned the font that was current; selecting back returns f */
  HGDIOBJ back2=SelectObject(dc,prev);
  rv_i(v, prev==cur && back2==(HGDIOBJ)f);
  SelectObject(dc,f);
  /* language info, character placement */
  DWORD fli=GetFontLanguageInfo(dc);
  if(fli==GCP_ERROR) rv_na(v); else rv_x(v,fli);
  GCP_RESULTSW gr0; memset(&gr0,0,sizeof gr0); gr0.lStructSize=sizeof gr0;
  DWORD g0=GetCharacterPlacementW(dc,HELLO,11,0,&gr0,0);
  rv_x(v,g0);
  /* with the font's own GCP flags (GCP_REORDER etc.): give every output array so no implementation writes
   * through a NULL member (Wine crashes on HEBREW_CHARSET otherwise) */
  WCHAR o_str[32]; UINT o_order[32]; int o_dx[32], o_caret[32]; char o_class[32]; WCHAR o_glyphs[32];
  GCP_RESULTSW gr1; memset(&gr1,0,sizeof gr1); gr1.lStructSize=sizeof gr1;
  gr1.lpOutString=o_str; gr1.lpOrder=o_order; gr1.lpDx=o_dx; gr1.lpCaretPos=o_caret; gr1.lpClass=o_class;
  gr1.lpGlyphs=o_glyphs; gr1.nGlyphs=32; memset(o_class,0,sizeof o_class);
  DWORD g1=(fli==GCP_ERROR)?0:GetCharacterPlacementW(dc,HELLO,11,0,&gr1,fli&FLI_MASK);
  rv_x(v,g1);
  int tcs=GetTextCharsetInfo(dc,NULL,0);
  if(tcs==DEFAULT_CHARSET && !okw) rv_na(v); else rv_i(v,tcs);
  SIZE sz={0,0};
  if(GetTextExtentPoint32W(dc,ALPHA52,52,&sz)){ rv_i(v,sz.cx); rv_i(v,sz.cy); } else { rv_na(v); rv_na(v); }
  /* realization info + file */
  ri_buf r; memset(&r,0,sizeof r); r.size=ri_size;
  if(ri_size && fnRI(dc,&r)){
    rv_x(v,r.flags | ((DWORD)r.b5<<8));
    if(ri_size>=0x18){ rv_i(v,r.file_count); rv_i(v,r.face_index); rv_x(v,r.simulations); } else { rv_na(v); rv_na(v); rv_na(v); }
    char fn[MAX_PATH*3]="";
    if(fnFFI){
      static union { struct { FILETIME wt; LARGE_INTEGER size; WCHAR path[1]; } s; BYTE raw[0x1000]; } fi;
      SIZE_T need=0; memset(&fi,0,sizeof fi);
      if(fnFFI(r.instance_id,0,&fi,sizeof fi,&need)) lower_base(fi.s.path,fn,sizeof fn);
    }
    if(fn[0]) rv_s(v,fn); else rv_na(v);
  } else for(int i=0;i<5;i++) rv_na(v);
  SelectObject(dc,prev);
}

static HFONT make_font(LOGFONTW *lf, const char *face, int h, int wt, int it, int q, int cs){
  memset(lf,0,sizeof *lf);
  lf->lfHeight=h; lf->lfWeight=wt; lf->lfItalic=(BYTE)it; lf->lfCharSet=(BYTE)cs; lf->lfQuality=(BYTE)q;
  lf->lfOutPrecision=OUT_DEFAULT_PRECIS; lf->lfClipPrecision=CLIP_DEFAULT_PRECIS; lf->lfPitchAndFamily=DEFAULT_PITCH|FF_DONTCARE;
  MultiByteToWideChar(CP_UTF8,0,face,-1,lf->lfFaceName,LF_FACESIZE);
  return CreateFontIndirectW(lf);
}

/* ---------------------------------------------------------------- OTM */
#define OTM_COLS "need ret size tm_eq panose fssel fstype rise run ital em:fu asc:px desc:px lgap:px capem:px xh:px " \
                 "box_l:px box_t:px box_r:px box_b:px mac_asc:px mac_desc:px mac_lgap:px minppem sub_sx:px sub_sy:px sub_ox:px sub_oy:px " \
                 "sup_sx:px sup_sy:px sup_ox:px sup_oy:px so_size:px so_pos:px us_size:px us_pos:px"
static const char *OTMN_COLS = "family face style full need usb0 usb1 usb2 usb3 csb0 csb1";

static void otm_row(HDC dc, const rk *k, const TEXTMETRICW *tm, int names, const rk *knames){
  rv v; rv_reset(&v);
  UINT need=GetOutlineTextMetricsW(dc,0,NULL);
  rv_i(&v,need);
  if(!need){ pad_na(&v,ncols(OTM_COLS)-1); rec_row("otm",k,&v); return; }
  OUTLINETEXTMETRICW *o=(OUTLINETEXTMETRICW*)calloc(1,need+16);
  UINT ret=GetOutlineTextMetricsW(dc,need,o);
  rv_i(&v,ret);
  if(!ret){ pad_na(&v,ncols(OTM_COLS)-2); rec_row("otm",k,&v); free(o); return; }
  rv_i(&v,o->otmSize); rv_i(&v,tm_equal(&o->otmTextMetrics,tm));
  char pan[32]; rec_hex(pan,&o->otmPanoseNumber,10); rv_s(&v,pan);
  rv_x(&v,o->otmfsSelection); rv_x(&v,o->otmfsType); rv_i(&v,o->otmsCharSlopeRise); rv_i(&v,o->otmsCharSlopeRun);
  rv_i(&v,o->otmItalicAngle); rv_i(&v,o->otmEMSquare); rv_i(&v,o->otmAscent); rv_i(&v,o->otmDescent); rv_i(&v,o->otmLineGap);
  rv_i(&v,o->otmsCapEmHeight); rv_i(&v,o->otmsXHeight);
  rv_i(&v,o->otmrcFontBox.left); rv_i(&v,o->otmrcFontBox.top); rv_i(&v,o->otmrcFontBox.right); rv_i(&v,o->otmrcFontBox.bottom);
  rv_i(&v,o->otmMacAscent); rv_i(&v,o->otmMacDescent); rv_i(&v,o->otmMacLineGap); rv_i(&v,o->otmusMinimumPPEM);
  rv_i(&v,o->otmptSubscriptSize.x); rv_i(&v,o->otmptSubscriptSize.y); rv_i(&v,o->otmptSubscriptOffset.x); rv_i(&v,o->otmptSubscriptOffset.y);
  rv_i(&v,o->otmptSuperscriptSize.x); rv_i(&v,o->otmptSuperscriptSize.y); rv_i(&v,o->otmptSuperscriptOffset.x); rv_i(&v,o->otmptSuperscriptOffset.y);
  rv_i(&v,o->otmsStrikeoutSize); rv_i(&v,o->otmsStrikeoutPosition); rv_i(&v,o->otmsUnderscoreSize); rv_i(&v,o->otmsUnderscorePosition);
  rec_row("otm",k,&v);
  if(names){
    rv n; rv_reset(&n);
    const PSTR offs[4]={o->otmpFamilyName,o->otmpFaceName,o->otmpStyleName,o->otmpFullName};
    for(int i=0;i<4;i++){
      size_t off=(size_t)offs[i];
      if(off && off<ret){ WCHAR tmp[256]; size_t m=0; const WCHAR *s=(const WCHAR*)((BYTE*)o+off);
        while(m<255 && (off+2*(m+1))<=ret && s[m]){ tmp[m]=s[m]; m++; } tmp[m]=0; if(m) rv_w(&n,tmp); else rv_s(&n,""); }
      else rv_na(&n);
    }
    rv_i(&n,need);
    FONTSIGNATURE fs; memset(&fs,0,sizeof fs);
    GetTextCharsetInfo(dc,&fs,0);
    for(int i=0;i<4;i++) rv_x(&n,fs.fsUsb[i]);
    rv_x(&n,fs.fsCsb[0]); rv_x(&n,fs.fsCsb[1]);
    rec_row("otmname",knames,&n);
  }
  free(o);
}

/* ---------------------------------------------------------------- grid */
typedef struct { tm_packed tm[64]; int n; } grp_t;

static void grid_one(HDC dc, const char *face, int h, int wt, int it, int qi, int cs, int adv, grp_t *g, int do_otm){
  LOGFONTW lf; HFONT f=make_font(&lf,face,h,wt,it,QUALS[qi].v,cs);
  rk k; rk_reset(&k); rk_s(&k,"face",face); rk_i(&k,"req_h",h); rk_i(&k,"wt",wt); rk_i(&k,"it",it);
  rk_s(&k,"q",QUALS[qi].name); rk_i(&k,"cs",cs); rk_s(&k,"gm",adv?"adv":"compat");
  rv v; rv_reset(&v); TEXTMETRICW tm;
  measure(dc,f,&lf,&v,&tm);
  rec_row("tm",&k,&v);
  if(g->n<64) tm_pack(&tm,&g->tm[g->n++]);
  if(do_otm && f){
    HGDIOBJ o=SelectObject(dc,f);
    rk ko; rk_reset(&ko); rk_s(&ko,"face",face); rk_i(&ko,"req_h",h); rk_i(&ko,"wt",wt); rk_i(&ko,"it",it);
    int names = (h==-11||h==-12||h==-16);
    otm_row(dc,&ko,&tm,names,&ko);
    SelectObject(dc,o);
  }
  if(f) DeleteObject(f);
}

static void run_grid(HDC dc, HDC dcadv, int quick, int otm){
  rec_schema("tm","face req_h wt it q cs gm",TM_COLS);
  if(otm){ rec_schema("otm","face req_h wt it",OTM_COLS); rec_schema("otmname","face req_h wt it",OTMN_COLS); }
  const char **faces = quick?FACES_QUICK:FACES_FULL;
  int nfaces = quick?(int)(sizeof FACES_QUICK/sizeof*FACES_QUICK):(int)(sizeof FACES_FULL/sizeof*FACES_FULL);
  int hs[128], nh=0;
  if(quick){ for(unsigned i=0;i<sizeof H_QUICK/sizeof*H_QUICK;i++) hs[nh++]=H_QUICK[i]; }
  else { for(int h=-6;h>=-40;h--) hs[nh++]=h; hs[nh++]=0; for(int h=6;h<=40;h++) hs[nh++]=h; }
  const int *css = quick?CS_QUICK:CS_FULL; int ncs = quick?(int)(sizeof CS_QUICK/sizeof*CS_QUICK):(int)(sizeof CS_FULL/sizeof*CS_FULL);
  int nq = quick?(int)(sizeof QUALS_QUICK/sizeof*QUALS_QUICK):(int)(sizeof QUALS/sizeof*QUALS);
  DWORD t0=GetTickCount();
  for(int fi=0;fi<nfaces;fi++){
    const char *face=faces[fi];
    for(int hi=0;hi<nh;hi++){
      int h=hs[hi]; static grp_t g; g.n=0;
      /* main grid: weight x italic at q=default cs=DEFAULT gm=compat */
      for(int wt=400;wt<=700;wt+=300) for(int it=0;it<=1;it++) grid_one(dc,face,h,wt,it,0,DEFAULT_CHARSET,0,&g,otm);
      /* quality axis at 400/0/DEFAULT */
      for(int q=1;q<nq;q++) grid_one(dc,face,h,400,0,quick?QUALS_QUICK[q]:q,DEFAULT_CHARSET,0,&g,0);
      /* charset axis at 400/0/q default */
      for(int c=0;c<ncs;c++){ if(css[c]==SYMBOL_CHARSET && !symbol_meaningful(face)) continue; grid_one(dc,face,h,400,0,0,css[c],0,&g,0); }
      /* GM_ADVANCED (no MS Shell Dlg height snap there, research 02 5.3) */
      grid_one(dcadv,face,h,400,0,0,DEFAULT_CHARSET,1,&g,0);
      rk k; rk_reset(&k); rk_s(&k,"face",face); rk_i(&k,"req_h",h);
      rec_hash("tmhash",&k,g.tm,sizeof(tm_packed)*(size_t)g.n);
    }
    fprintf(stderr,"font_metrics: face %d/%d done, %lu ms, %d rows\n",fi+1,nfaces,GetTickCount()-t0,rec_rows());
    rec_flush();
  }
}

/* ---------------------------------------------------------------- stock + UI fonts */
static void lf_vals(rv *v, const LOGFONTW *lf){
  rv_i(v,lf->lfHeight); rv_i(v,lf->lfWidth); rv_i(v,lf->lfEscapement); rv_i(v,lf->lfOrientation); rv_i(v,lf->lfWeight);
  rv_i(v,lf->lfItalic); rv_i(v,lf->lfUnderline); rv_i(v,lf->lfStrikeOut); rv_i(v,lf->lfCharSet); rv_i(v,lf->lfOutPrecision);
  rv_i(v,lf->lfClipPrecision); rv_i(v,lf->lfQuality); rv_x(v,lf->lfPitchAndFamily); rv_w(v,lf->lfFaceName);
}

static void run_stock(HDC dc){
  static const struct { int id; const char *name; } S[] = {
    {OEM_FIXED_FONT,"OEM_FIXED_FONT"},{ANSI_FIXED_FONT,"ANSI_FIXED_FONT"},{ANSI_VAR_FONT,"ANSI_VAR_FONT"},
    {SYSTEM_FONT,"SYSTEM_FONT"},{DEVICE_DEFAULT_FONT,"DEVICE_DEFAULT_FONT"},{SYSTEM_FIXED_FONT,"SYSTEM_FIXED_FONT"},
    {DEFAULT_GUI_FONT,"DEFAULT_GUI_FONT"} };
  rec_schema("stock_lf","obj","go_ret " LF_COLS);
  rec_schema("stock_tm","obj via",TM_COLS);
  for(unsigned i=0;i<sizeof S/sizeof*S;i++){
    HFONT f=(HFONT)GetStockObject(S[i].id);
    rk k; rk_reset(&k); rk_s(&k,"obj",S[i].name);
    rv v; rv_reset(&v); LOGFONTW lf; memset(&lf,0,sizeof lf);
    int r=f?GetObjectW(f,sizeof lf,&lf):0; rv_i(&v,r);
    if(r) lf_vals(&v,&lf); else pad_na(&v,ncols(LF_COLS));
    rec_row("stock_lf",&k,&v);
    rk ks=k; rk_s(&ks,"via","stock");
    rv_reset(&v); TEXTMETRICW tm; measure(dc,f,r?&lf:NULL,&v,&tm); rec_row("stock_tm",&ks,&v);
    hash_tm("stock_tmhash",&ks,&tm);
    /* the same LOGFONT re-created through CreateFontIndirectW (stock types 4/5/6 return the PDEV LOGFONT) */
    if(r){ HFONT g=CreateFontIndirectW(&lf); rk kr=k; rk_s(&kr,"via","recreate"); rv_reset(&v); measure(dc,g,&lf,&v,&tm); rec_row("stock_tm",&kr,&v); hash_tm("stock_tmhash",&kr,&tm); if(g) DeleteObject(g); }
  }
}

static void ui_font(HDC dc, const char *src, const char *dpi, const LOGFONTW *lf){
  rk k; rk_reset(&k); rk_s(&k,"src",src); rk_s(&k,"dpi",dpi);
  rv v; rv_reset(&v); lf_vals(&v,lf); rec_row("ui_lf",&k,&v);
  HFONT f=CreateFontIndirectW(lf); TEXTMETRICW tm;
  rv_reset(&v); measure(dc,f,lf,&v,&tm); rec_row("ui_tm",&k,&v);
  hash_tm("ui_tmhash",&k,&tm);
  if(f) DeleteObject(f);
}

static void ui_set(HDC dc, const char *dpi, UINT d){
  NONCLIENTMETRICSW ncm; memset(&ncm,0,sizeof ncm); ncm.cbSize=sizeof ncm;
  BOOL ok = d ? fnSPIDpi(SPI_GETNONCLIENTMETRICS,sizeof ncm,&ncm,0,d) : SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof ncm,&ncm,0);
  if(ok){
    rk k; rk_reset(&k); rk_s(&k,"dpi",dpi); rv v; rv_reset(&v);
    rv_i(&v,ncm.iBorderWidth); rv_i(&v,ncm.iScrollWidth); rv_i(&v,ncm.iScrollHeight); rv_i(&v,ncm.iCaptionWidth); rv_i(&v,ncm.iCaptionHeight);
    rv_i(&v,ncm.iSmCaptionWidth); rv_i(&v,ncm.iSmCaptionHeight); rv_i(&v,ncm.iMenuWidth); rv_i(&v,ncm.iMenuHeight); rv_i(&v,ncm.iPaddedBorderWidth);
    rec_row("ncm",&k,&v);
    ui_font(dc,"ncm.caption",dpi,&ncm.lfCaptionFont); ui_font(dc,"ncm.smcaption",dpi,&ncm.lfSmCaptionFont);
    ui_font(dc,"ncm.menu",dpi,&ncm.lfMenuFont); ui_font(dc,"ncm.status",dpi,&ncm.lfStatusFont); ui_font(dc,"ncm.message",dpi,&ncm.lfMessageFont);
  } else rec_note("SPI_GETNONCLIENTMETRICS failed dpi=%s err=%lu",dpi,GetLastError());
  ICONMETRICSW im; memset(&im,0,sizeof im); im.cbSize=sizeof im;
  ok = d ? fnSPIDpi(SPI_GETICONMETRICS,sizeof im,&im,0,d) : SystemParametersInfoW(SPI_GETICONMETRICS,sizeof im,&im,0);
  if(ok){
    rk k; rk_reset(&k); rk_s(&k,"dpi",dpi); rv v; rv_reset(&v);
    rv_i(&v,im.iHorzSpacing); rv_i(&v,im.iVertSpacing); rv_i(&v,im.iTitleWrap); rec_row("icm",&k,&v);
    ui_font(dc,"icm.font",dpi,&im.lfFont);
  } else rec_note("SPI_GETICONMETRICS failed dpi=%s err=%lu",dpi,GetLastError());
  LOGFONTW it; memset(&it,0,sizeof it);
  ok = d ? fnSPIDpi(SPI_GETICONTITLELOGFONT,sizeof it,&it,0,d) : SystemParametersInfoW(SPI_GETICONTITLELOGFONT,sizeof it,&it,0);
  if(ok) ui_font(dc,"icontitle",dpi,&it); else rec_note("SPI_GETICONTITLELOGFONT failed dpi=%s err=%lu",dpi,GetLastError());
}

static void run_ui(HDC dc){
  rec_schema("ui_lf","src dpi",LF_COLS);
  rec_schema("ui_tm","src dpi",TM_COLS);
  rec_schema("ncm","dpi","border:px scrollw:px scrollh:px capw:px caph:px smcapw:px smcaph:px menuw:px menuh:px padborder:px");
  rec_schema("icm","dpi","hspace:px vspace:px wrap");
  ui_set(dc,"sys",0);
  if(fnSPIDpi){ static const UINT D[]={96,120,144,168,192}; char b[8]; for(int i=0;i<5;i++){ snprintf(b,sizeof b,"%u",D[i]); ui_set(dc,b,D[i]); } }
  else rec_note("SystemParametersInfoForDpi not available");
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"font_metrics","1");
  int quick=0, otm=1, grid=1;
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--quick")) quick=1; else if(!strcmp(argv[i],"--full")) quick=0;
    else if(!strcmp(argv[i],"--no-otm")) otm=0; else if(!strcmp(argv[i],"--no-grid")) grid=0;
    else rec_note("unknown argument %s",argv[i]);
  }
  HMODULE g32=GetModuleHandleA("gdi32.dll"), u32=GetModuleHandleA("user32.dll");
  fnRI=(pGetFontRealizationInfo)GetProcAddress(g32,"GetFontRealizationInfo");
  fnFFI=(pGetFontFileInfo)GetProcAddress(g32,"GetFontFileInfo");
  fnSPIDpi=(pSPIForDpi)GetProcAddress(u32,"SystemParametersInfoForDpi");
  HDC dc=GetDC(NULL), dcadv=GetDC(NULL);
  probe_ri_size(dc);
  rec_env("fm.mode","%s",quick?"quick":"full");
  rec_env("fm.dc","screen GetDC(NULL) MM_TEXT");
  rec_env("fm.ri_size","0x%lx",ri_size);
  rec_env("fm.has_fontfileinfo","%d",fnFFI!=NULL);
  rec_env("fm.logpixels","%dx%d",GetDeviceCaps(dc,LOGPIXELSX),GetDeviceCaps(dc,LOGPIXELSY));
  int gm=SetGraphicsMode(dcadv,GM_ADVANCED);
  rec_env("fm.gm_advanced_set","%d",gm!=0);
  run_stock(dc);
  run_ui(dc);
  if(grid) run_grid(dc,dcadv,quick,otm);
  SetGraphicsMode(dcadv,GM_COMPATIBLE);
  ReleaseDC(NULL,dcadv); ReleaseDC(NULL,dc);
  rec_note("font_metrics done");
  return rec_finish(0);
}
