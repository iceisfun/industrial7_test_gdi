/* char_widths: per-character widths, ABC widths, kerning, text extents and DrawText(DT_CALCRECT) rectangles
 * for the UI faces (D12 data; docs/research/01_d12_context.md R4/R5, Q1/Q5/Q8).
 *
 * usage: char_widths.exe [recorder options, see rec.h] [--quick | --full] [--heights px|pt|all]
 *   --full (default)  faces: 16 (UI + bitmap + reference + one absent name "NoSuchFace I7"), heights -8..-32 (13)
 *                     + 8..12 pt at this process's LOGPIXELSY, + cell heights 13/15/16/19 (core faces only),
 *                     weights 400/700, italic for Segoe UI/Tahoma/Arial (weight 400)
 *   --quick           5 faces, -11/-12/-16 + 8/9 pt, weight 400 only (plumbing / smoke)
 *   --heights pt      only the point-size heights (for DPI sweeps: pixel-height rows do not depend on DPI)
 *
 * All widths are integers in device pixels (MM_TEXT, screen DC from GetDC(NULL), DEFAULT_QUALITY,
 * DEFAULT_CHARSET) unless the table says :lu (logical units, fx table: mapping mode / world transform).
 * Key of a font configuration: face (requested name), req_h (lfHeight exactly as passed; negative = character (em)
 * height, positive = cell height; docs/PROBE_FORMAT.md 3), wt (lfWeight), it (lfItalic). The value column `h` of the
 * font table is the RESULT tmHeight. A font that cannot be created/selected gets a font row with got=! and nothing
 * else (never the previous font's numbers). Core grid (exdx, abcf, fx, DrawText, tabs) = core faces at -11/-12/-13/-16
 * plus the 8 pt and 9 pt heights of this DPI (so `--heights pt` DPI sweeps still get the core tables).
 *
 * Tables (S lines carry the column names):
 *   chars   blk                          the 16 code points of each character block (hex)
 *   str     s                            the test strings (length in UTF-16 units, UTF-8 text)
 *   ptmap   pt                           point size -> lfHeight = -MulDiv(pt, LOGPIXELSY, 72)
 *   font    face;req_h;wt;it                 GetTextFaceW + TEXTMETRICW of the realised font
 *   cw32    face;req_h;wt;it;blk             GetCharWidth32W per char (16 per row)
 *   abc     face;req_h;wt;it;blk             GetCharABCWidthsW A B C per char (only where the call succeeds)
 *   abcf    face;req_h;wt;it;blk             GetCharABCWidthsFloatW (core grid only; elsewhere H h_abcf)
 *   gi      face;req_h;wt;it;blk         GetGlyphIndicesW(GGI_MARK_NONEXISTING_GLYPHS), once per face/wt/it (first height)
 *   cons    face;req_h;wt;it                 cross-function consistency counts (see S line)
 *   kern    face;req_h;wt;it                 GetKerningPairsW count + first 16 pairs
 *   ext     face;req_h;wt;it;s               GetTextExtentPoint32W / GetTextExtentExPointW(max 100) / ...ExPointI
 *   exdx    face;req_h;wt;it;s;i             GetTextExtentExPointW partial extents, 16 per row (core grid only)
 *   tab     face;req_h;wt;it;s;tabs          GetTabbedTextExtentW
 *   fx      face;req_h;wt;it;s;fx            extents under SetTextCharacterExtra / SetTextJustification / MM_TWIPS /
 *                                        GM_ADVANCED world transforms / lfEscapement 900 (logical units)
 *   qual    face;req_h;q                     lfQuality effect on widths (ASCII sum, pangram extent)
 *   dt      face;req_h;wt;it;s;fl;w          DrawTextW(DT_CALCRECT|...) on rect (0,0,w,1000): return + rect
 *   H lines h_cw32 h_cw h_cwf h_cwi h_abc h_abci h_abcf h_kern h_exdx h_exdxi per font configuration (sha256/16 of the
 *           raw arrays, 21 blocks x 16 in block order; a failed block hashes as 0xFF bytes), h_qual_cw32 per qual row
 */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NB 16
typedef struct { const char *name; WCHAR c[NB]; int contig; } block;
static block g_blk[32]; static int g_nblk;

static void blk_range(const char *name, int first){ block *b=&g_blk[g_nblk++]; b->name=name; b->contig=1; for(int i=0;i<NB;i++) b->c[i]=(WCHAR)(first+i); }
static void blk_list(const char *name, const unsigned short *l){ block *b=&g_blk[g_nblk++]; b->name=name; b->contig=0; for(int i=0;i<NB;i++) b->c[i]=l[i]; }

static void init_blocks(void){
  static const char *asc[]={"asc2","asc3","asc4","asc5","asc6","asc7"};
  static const char *l1[]={"lat1a","lat1b","lat1c","lat1d","lat1e","lat1f"};
  static const char *cy[]={"cyr0","cyr1","cyr2","cyr3"};
  for(int i=0;i<6;i++) blk_range(asc[i],0x20+16*i);          /* U+0020..U+007F (U+007F extra) */
  for(int i=0;i<6;i++) blk_range(l1[i],0xA0+16*i);           /* U+00A0..U+00FF */
  static const unsigned short la0[NB]={0x100,0x101,0x102,0x103,0x104,0x105,0x106,0x107,0x10C,0x10D,0x10E,0x10F,0x118,0x119,0x11A,0x11B};
  static const unsigned short la1[NB]={0x141,0x142,0x143,0x144,0x150,0x151,0x152,0x153,0x158,0x159,0x15A,0x15B,0x160,0x161,0x17D,0x17E};
  static const unsigned short gr0[NB]={0x391,0x392,0x393,0x394,0x395,0x396,0x397,0x398,0x399,0x39A,0x39B,0x39C,0x39D,0x39E,0x39F,0x3A0};
  static const unsigned short gr1[NB]={0x3A1,0x3A3,0x3A4,0x3A5,0x3A6,0x3A7,0x3A8,0x3A9,0x3B1,0x3B2,0x3B3,0x3B4,0x3BB,0x3C0,0x3C3,0x3C9};
  static const unsigned short misc[NB]={0x2013,0x2014,0x2018,0x2019,0x201A,0x201C,0x201D,0x201E,0x2020,0x2022,0x2026,0x2030,0x20AC,0x2122,0xFFFD,0xE000};
  blk_list("latA0",la0); blk_list("latA1",la1); blk_list("grk0",gr0); blk_list("grk1",gr1);
  for(int i=0;i<4;i++) blk_range(cy[i],0x410+16*i);          /* U+0410..U+044F */
  blk_list("misc",misc);
}

/* ---------------------------------------------------------------- strings */
typedef struct { const char *id; const WCHAR *s; } tstr;
static const tstr g_str[]={
  {"fox",    L"The quick brown fox jumps over the lazy dog"},
  {"digits", L"0123456789"},
  {"W10",    L"WWWWWWWWWW"},
  {"i10",    L"iiiiiiiiii"},
  {"Hg",     L"Hg"},
  {"accap",  L"\x00C0\x00C1\x00C2\x00C3\x00C4\x00C5\x00C7\x00C8\x00C9\x00CA\x00CB\x00CC\x00CD\x00CE\x00CF\x00D1\x00D2\x00D3\x00D4\x00D5\x00D6\x00D9\x00DA\x00DB\x00DC\x00DD"},
  {"space",  L" "},
  {"fev",    L"File Edit View"},
  {"fevh",   L"File  Edit  View  Favorites  Tools  Help"},
  {"name",   L"Name"},
  {"size",   L"Size"},
  {"type",   L"Type"},
  {"datemod",L"Date modified"},
  {"modified",L"Modified"},
  {"alpha",  L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"},
  {"hello",  L"Hello world"},
  {"ell",    L"..."},
  {"ellc",   L"\x2026"},
  {"ampf",   L"&File"},
  {"ok",     L"OK"},
  {"cancel", L"Cancel"},
  {"tabs",   L"Name\tSize\tType"},
  {"uni",    L"\x0391\x03B2\x03B3 \x0416\x0436 \x2013\x2014\x2018\x2019\x201C\x201D\x2026\x20AC"},
  {"pua",    L"A\xE000\xFFFD" L"B"},
};
#define NSTR ((int)(sizeof g_str/sizeof g_str[0]))
static int str_len(int i){ return lstrlenW(g_str[i].s); }
static int str_find(const char *id){ for(int i=0;i<NSTR;i++) if(!strcmp(g_str[i].id,id)) return i; return -1; }

/* ---------------------------------------------------------------- configuration */
typedef struct { const char *face; WCHAR facew[LF_FACESIZE]; int core; int italic_ok; } tface;
static tface g_face[24]; static int g_nface;
static int g_h[40], g_nh; static int g_full=1; static int g_heights_mode=0; /* 0 all, 1 px, 2 pt */
static int g_dpi=96; static int g_pth[8], g_npt;

static void add_face(const char *f, int core, int itok){ tface *t=&g_face[g_nface++]; t->face=f; t->core=core; t->italic_ok=itok; MultiByteToWideChar(CP_ACP,0,f,-1,t->facew,LF_FACESIZE); }
static void add_h(int h){ for(int i=0;i<g_nh;i++) if(g_h[i]==h) return; g_h[g_nh++]=h; }
static int is_pt89(int h){ for(int i=0;i<g_npt && i<2;i++) if(g_pth[i]==h) return 1; return 0; }   /* 8 pt / 9 pt at this DPI */
static int is_core_h(int h){ return h==-11||h==-12||h==-13||h==-16||is_pt89(h); }
/* select hf; 0 if it could not be created or selected (the DC keeps its previous font, so nothing may be measured) */
static int sel_font(HDC dc, HFONT hf, HFONT *old){ *old=NULL; if(!hf) return 0; HGDIOBJ o=SelectObject(dc,hf); if(!o||o==HGDI_ERROR) return 0; *old=(HFONT)o; return 1; }

static HFONT mkfont(const WCHAR *face, int h, int wt, int it, int esc, int q){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=wt; lf.lfItalic=(BYTE)it; lf.lfCharSet=DEFAULT_CHARSET;
  lf.lfEscapement=esc; lf.lfOrientation=esc; lf.lfQuality=(BYTE)q; lstrcpynW(lf.lfFaceName,face,LF_FACESIZE);
  return CreateFontIndirectW(&lf);
}
static void fkey(rk *k, const tface *f, int h, int wt, int it){ rk_reset(k); rk_s(k,"face",f->face); rk_i(k,"req_h",h); rk_i(k,"wt",wt); rk_i(k,"it",it); }

/* ---------------------------------------------------------------- per-char data */
typedef struct { INT cw32[32][NB], cw[32][NB], cwi[32][NB], cwa[NB*6]; FLOAT cwf[32][NB]; ABC abc[32][NB], abci[32][NB]; ABCFLOAT abcf[32][NB];
                 WORD gi[32][NB], gim[32][NB]; int ok_cw32[32], ok_cw[32], ok_cwf[32], ok_cwi[32], ok_abc[32], ok_abci[32], ok_abcf[32], ok_gi[32], ok_cwa; } cdata;

static int get_cw(HDC dc, const block *b, INT *out, BOOL (WINAPI *fn)(HDC,UINT,UINT,LPINT)){
  if(b->contig) return fn(dc,b->c[0],b->c[NB-1],out)!=0;
  for(int i=0;i<NB;i++) if(!fn(dc,b->c[i],b->c[i],&out[i])) return 0;
  return 1;
}
static int get_cwf(HDC dc, const block *b, FLOAT *out){
  if(b->contig) return GetCharWidthFloatW(dc,b->c[0],b->c[NB-1],out)!=0;
  for(int i=0;i<NB;i++) if(!GetCharWidthFloatW(dc,b->c[i],b->c[i],&out[i])) return 0;
  return 1;
}
static int get_abc(HDC dc, const block *b, ABC *out){
  if(b->contig) return GetCharABCWidthsW(dc,b->c[0],b->c[NB-1],out)!=0;
  for(int i=0;i<NB;i++) if(!GetCharABCWidthsW(dc,b->c[i],b->c[i],&out[i])) return 0;
  return 1;
}
static int get_abcf(HDC dc, const block *b, ABCFLOAT *out){
  if(b->contig) return GetCharABCWidthsFloatW(dc,b->c[0],b->c[NB-1],out)!=0;
  for(int i=0;i<NB;i++) if(!GetCharABCWidthsFloatW(dc,b->c[i],b->c[i],&out[i])) return 0;
  return 1;
}

static void hash_rows(const char *table, const rk *k, const void *base, size_t rowbytes, const int *ok){
  /* hash of all blocks; a failed block contributes 16 x 0xFFFFFFFF bytes-worth of 0xFF so it stays distinguishable */
  size_t n=rowbytes*g_nblk; uint8_t *buf=(uint8_t*)malloc(n);
  for(int b=0;b<g_nblk;b++){ if(ok[b]) memcpy(buf+b*rowbytes,(const uint8_t*)base+b*rowbytes,rowbytes); else memset(buf+b*rowbytes,0xFF,rowbytes); }
  rec_hash(table,k,buf,n); free(buf);
}

static void schemas(void){
  char cols[2048]; int n;
  n=0; for(int i=0;i<NB;i++) n+=sprintf(cols+n,"%sc%d",i?" ":"",i); rec_schema("chars","blk",cols);
  rec_schema("str","s","len text");
  rec_schema("ptmap","pt","h dpi");
  rec_schema("font","face req_h wt it","got h:px a:px d:px il:px el:px ave:px max:px oh:px weight it pf cs first last default break tt");
  n=0; for(int i=0;i<NB;i++) n+=sprintf(cols+n,"%sw%d:px",i?" ":"",i); rec_schema("cw32","face req_h wt it blk",cols);
  n=0; for(int i=0;i<NB;i++) n+=sprintf(cols+n,"%sa%d:px b%d:px c%d:px",i?" ":"",i,i,i); rec_schema("abc","face req_h wt it blk",cols);
  rec_schema("abcf","face req_h wt it blk",cols);
  n=0; for(int i=0;i<NB;i++) n+=sprintf(cols+n,"%sg%d",i?" ":"",i); rec_schema("gi","face req_h wt it blk",cols);
  rec_schema("cons","face req_h wt it","nchars cw_ok cw_ne cwA_ne cwi_ok cwi_ne abc_ok abc_err abc_sum_ne abci_ok abci_ne abcf_ok abcf_sum_ne missing sum_ascii:px ext_ascii:px ext_ascii_cy:px cwf_ok cwf_ne");
  n=sprintf(cols,"npairs"); for(int i=0;i<16;i++) n+=sprintf(cols+n," f%d s%d k%d:px",i,i,i); rec_schema("kern","face req_h wt it",cols);
  rec_schema("ext","face req_h wt it s","cx:px cy:px exfit excx:px excy:px exlast:px icx:px ifit sumw:px idx_ne");
  n=0; for(int i=0;i<NB;i++) n+=sprintf(cols+n,"%sd%d:px",i?" ":"",i); rec_schema("exdx","face req_h wt it s i",cols);
  rec_schema("tab","face req_h wt it s tabs","cx:px cy:px");
  rec_schema("fx","face req_h wt it s fx","cx:lu cy:lu exfit excx:lu cwW:lu tmh:lu");
  rec_schema("qual","face req_h q","sum_ascii:px fox_cx:px fox_cy:px tmh:px ave:px");
  rec_schema("dt","face req_h wt it s fl w","ret:px l:px t:px r:px b:px");
}

/* ---------------------------------------------------------------- one font configuration */
static void do_ext(HDC dc, const tface *f, int h, int wt, int it, int core, uint8_t *dxhash_buf, size_t *dxhash_n, uint8_t *dxihash_buf, size_t *dxihash_n){
  for(int si=0;si<NSTR;si++){
    const WCHAR *s=g_str[si].s; int n=str_len(si);
    rk k; fkey(&k,f,h,wt,it); rk_s(&k,"s",g_str[si].id);
    rv v; rv_reset(&v);
    SIZE sz; if(GetTextExtentPoint32W(dc,s,n,&sz)){ rv_i(&v,sz.cx); rv_i(&v,sz.cy); } else { rv_na(&v); rv_na(&v); }
    INT dx[128]; int fit=-1; SIZE es; int okdx=0;
    if(GetTextExtentExPointW(dc,s,n,100,&fit,dx,&es)){ okdx=1; rv_i(&v,fit); rv_i(&v,es.cx); rv_i(&v,es.cy); rv_i(&v,n>0?dx[n-1]:0);
      memcpy(dxhash_buf+*dxhash_n,dx,n*sizeof(INT)); *dxhash_n+=n*sizeof(INT); }
    else { rv_na(&v); rv_na(&v); rv_na(&v); rv_na(&v); }
    WORD gi[128]; INT dxi[128]; int ifit=-1; SIZE is; int okdxi=0;
    if(GetGlyphIndicesW(dc,s,n,gi,0)!=GDI_ERROR && GetTextExtentExPointI(dc,gi,n,100,&ifit,dxi,&is)){ okdxi=1; rv_i(&v,is.cx); rv_i(&v,ifit);
      memcpy(dxihash_buf+*dxihash_n,dxi,n*sizeof(INT)); *dxihash_n+=n*sizeof(INT); }
    else { rv_na(&v); rv_na(&v); }
    long long sum=0; int okw=1; for(int i=0;i<n;i++){ INT w; if(!GetCharWidth32W(dc,s[i],s[i],&w)){ okw=0; break; } sum+=w; }
    if(okw) rv_i(&v,sum); else rv_na(&v);
    if(okdx&&okdxi){ int ne=0; for(int i=0;i<n;i++) if(dx[i]!=dxi[i]) ne++; rv_i(&v,ne); } else rv_na(&v);
    rec_row("ext",&k,&v);
    if(core){
      for(int i0=0;i0<n;i0+=NB){ rk k2=k; rk_i(&k2,"i",i0); rv v2; rv_reset(&v2); for(int i=i0;i<i0+NB;i++){ if(i<n&&okdx) rv_i(&v2,dx[i]); else rv_na(&v2); } rec_row("exdx",&k2,&v2); }
    }
  }
}

static void do_tab(HDC dc, const tface *f, int h, int wt, int it){
  static const char *ids[]={"tabs","tabs2","tabs3","tabs4"};
  static const WCHAR *ss[]={L"Name\tSize\tType", L"a\tb\tc\td", L"\tX", L"&Open...\tCtrl+O"};
  INT t2[2]={40,100}, t1[1]={32};
  for(int i=0;i<4;i++){
    int n=lstrlenW(ss[i]);
    for(int m=0;m<3;m++){
      rk k; fkey(&k,f,h,wt,it); rk_s(&k,"s",ids[i]); rk_s(&k,"tabs",m==0?"default":m==1?"40,100":"32");
      DWORD r= m==0? GetTabbedTextExtentW(dc,ss[i],n,0,NULL) : m==1? GetTabbedTextExtentW(dc,ss[i],n,2,t2) : GetTabbedTextExtentW(dc,ss[i],n,1,t1);
      rv v; rv_reset(&v); if(r){ rv_i(&v,LOWORD(r)); rv_i(&v,HIWORD(r)); } else { rv_na(&v); rv_na(&v); }
      rec_row("tab",&k,&v);
    }
  }
}

/* effects on extents: logical units */
static void fx_row(HDC dc, const tface *f, int h, int wt, int it, int si, const char *fxn){
  const WCHAR *s=g_str[si].s; int n=str_len(si);
  rk k; fkey(&k,f,h,wt,it); rk_s(&k,"s",g_str[si].id); rk_s(&k,"fx",fxn);
  rv v; rv_reset(&v);
  SIZE sz; if(GetTextExtentPoint32W(dc,s,n,&sz)){ rv_i(&v,sz.cx); rv_i(&v,sz.cy); } else { rv_na(&v); rv_na(&v); }
  INT dx[128]; int fit=-1; SIZE es;
  if(GetTextExtentExPointW(dc,s,n,100,&fit,dx,&es)){ rv_i(&v,fit); rv_i(&v,es.cx); } else { rv_na(&v); rv_na(&v); }
  INT w; if(GetCharWidth32W(dc,'W','W',&w)) rv_i(&v,w); else rv_na(&v);
  TEXTMETRICW tm; if(GetTextMetricsW(dc,&tm)) rv_i(&v,tm.tmHeight); else rv_na(&v);
  rec_row("fx",&k,&v);
}

static void do_fx(HDC dc, const tface *f, int h, int wt, int it){
  static const char *ids[]={"fox","hello","fev","digits","W10","space","tabs"};
  for(unsigned j=0;j<sizeof ids/sizeof*ids;j++){
    int si=str_find(ids[j]); if(si<0) continue;
    const WCHAR *s=g_str[si].s; int n=str_len(si); int nb=0; for(int i=0;i<n;i++) if(s[i]==' ') nb++;
    HFONT hf=mkfont(f->facew,h,wt,it,0,DEFAULT_QUALITY), of;
    int sv=SaveDC(dc);
    if(!sel_font(dc,hf,&of)){ RestoreDC(dc,sv); if(hf) DeleteObject(hf); continue; }
    fx_row(dc,f,h,wt,it,si,"base");
    SetTextCharacterExtra(dc,1); fx_row(dc,f,h,wt,it,si,"ce1");
    SetTextCharacterExtra(dc,2); fx_row(dc,f,h,wt,it,si,"ce2");
    SetTextCharacterExtra(dc,-1); fx_row(dc,f,h,wt,it,si,"cem1");
    SetTextCharacterExtra(dc,0);
    SetTextJustification(dc,10,nb>0?nb:1); fx_row(dc,f,h,wt,it,si,"tj10");
    SetTextJustification(dc,0,0);
    SetTextJustification(dc,10,nb>0?nb:1); SetTextCharacterExtra(dc,1); fx_row(dc,f,h,wt,it,si,"tj10ce1");
    SetTextJustification(dc,0,0); SetTextCharacterExtra(dc,0);
    /* MM_TWIPS: same LOGFONT (lfHeight now in twips) */
    SetMapMode(dc,MM_TWIPS);
    { HFONT h2=mkfont(f->facew,h,wt,it,0,DEFAULT_QUALITY); HFONT o2; if(sel_font(dc,h2,&o2)){ fx_row(dc,f,h,wt,it,si,"twips_same"); SelectObject(dc,o2); } if(h2) DeleteObject(h2); }
    /* MM_TWIPS: lfHeight scaled to the same pixel size (1440 twips per logical inch) */
    { HFONT h2=mkfont(f->facew,MulDiv(h,1440,g_dpi),wt,it,0,DEFAULT_QUALITY); HFONT o2; if(sel_font(dc,h2,&o2)){ fx_row(dc,f,h,wt,it,si,"twips_scaled"); SelectObject(dc,o2); } if(h2) DeleteObject(h2); }
    SetMapMode(dc,MM_TEXT);
    /* world transforms */
    SetGraphicsMode(dc,GM_ADVANCED);
    struct { const char *n; XFORM x; } xf[]={
      {"wt_id",  {1,0,0,1,0,0}},
      {"wt_s2",  {2,0,0,2,0,0}},
      {"wt_s15", {1.5f,0,0,1.5f,0,0}},
      {"wt_sx2", {2,0,0,1,0,0}},
      {"wt_r30", {0.8660254f,0.5f,-0.5f,0.8660254f,0,0}},
      {"wt_r90", {0,1,-1,0,0,0}},
    };
    for(unsigned i=0;i<sizeof xf/sizeof*xf;i++){ SetWorldTransform(dc,&xf[i].x); fx_row(dc,f,h,wt,it,si,xf[i].n); }
    ModifyWorldTransform(dc,NULL,MWT_IDENTITY);
    { HFONT h2=mkfont(f->facew,h,wt,it,900,DEFAULT_QUALITY); HFONT o2; if(sel_font(dc,h2,&o2)){ fx_row(dc,f,h,wt,it,si,"esc900adv"); SelectObject(dc,o2); } if(h2) DeleteObject(h2); }
    SetGraphicsMode(dc,GM_COMPATIBLE);
    { HFONT h2=mkfont(f->facew,h,wt,it,900,DEFAULT_QUALITY); HFONT o2; if(sel_font(dc,h2,&o2)){ fx_row(dc,f,h,wt,it,si,"esc900"); SelectObject(dc,o2); } if(h2) DeleteObject(h2); }
    SelectObject(dc,of); RestoreDC(dc,sv); DeleteObject(hf);
  }
}

static void do_dt(HDC dc, const tface *f, int h, int wt, int it){
  static const struct { const char *id; const WCHAR *s; } ds[]={
    {"fox",L"The quick brown fox jumps over the lazy dog"}, {"fev",L"File Edit View"}, {"ampf",L"&File"},
    {"amp2",L"Save &As...  &&  Close"}, {"tabs",L"Name\tSize\tType"}, {"datemod",L"Date modified"},
    {"path",L"C:\\Program Files\\Common Files\\Microsoft Shared\\Office\\readme.txt"},
    {"lines",L"Line one\r\nLine two\r\nLine three"}, {"hello",L"Hello world"}, {"empty",L""},
    {"longword",L"Supercalifragilisticexpialidocious"},
  };
  static const struct { const char *n; UINT fl; } fls[]={
    {"sl",DT_SINGLELINE}, {"sl_np",DT_SINGLELINE|DT_NOPREFIX}, {"sl_el",DT_SINGLELINE|DT_EXTERNALLEADING},
    {"sl_et",DT_SINGLELINE|DT_EXPANDTABS}, {"sl_ee",DT_SINGLELINE|DT_END_ELLIPSIS}, {"sl_pe",DT_SINGLELINE|DT_PATH_ELLIPSIS},
    {"sl_we",DT_SINGLELINE|DT_WORD_ELLIPSIS}, {"sl_ctr",DT_SINGLELINE|DT_CENTER|DT_VCENTER},
    {"ml",0}, {"wb",DT_WORDBREAK}, {"wb_np",DT_WORDBREAK|DT_NOPREFIX}, {"wb_ec",DT_WORDBREAK|DT_EDITCONTROL},
    {"wb_et",DT_WORDBREAK|DT_EXPANDTABS}, {"wb_ee",DT_WORDBREAK|DT_END_ELLIPSIS}, {"wb_el",DT_WORDBREAK|DT_EXTERNALLEADING},
    {"et",DT_EXPANDTABS}, {"et_ts",DT_EXPANDTABS|DT_TABSTOP|(4<<8)},
  };
  /* rect widths: single-line flags 60 (ellipsis/clipping) and 1000; multi-line flags 60 and 200 (word wrap) */
  static const int wsl[]={60,1000}, wml[]={60,200};
  for(unsigned si=0;si<sizeof ds/sizeof*ds;si++) for(unsigned fi=0;fi<sizeof fls/sizeof*fls;fi++) for(unsigned wi=0;wi<2;wi++){
    const int *ws=(fls[fi].fl&DT_SINGLELINE)?wsl:wml;
    RECT r={0,0,ws[wi],1000};
    int ret=DrawTextW(dc,ds[si].s,-1,&r,fls[fi].fl|DT_CALCRECT);
    rk k; fkey(&k,f,h,wt,it); rk_s(&k,"s",ds[si].id); rk_s(&k,"fl",fls[fi].n); rk_i(&k,"w",ws[wi]);
    rv v; rv_reset(&v); rv_i(&v,ret); rv_i(&v,r.left); rv_i(&v,r.top); rv_i(&v,r.right); rv_i(&v,r.bottom);
    rec_row("dt",&k,&v);
  }
}

static cdata g_cd;
static int g_gi_done[24][2][2];

static void do_config(HDC dc, int fi, int h, int wt, int it){
  tface *f=&g_face[fi];
  int core = f->core && wt==400 && !it && is_core_h(h);
  int pth=0; for(int i=0;i<g_npt;i++) if(g_pth[i]==h) pth=1;
  int dth = pth||h==-9||h==-11||h==-12||h==-13||h==-15||h==-16||h==-20||h==-24;
  int dtgrid = f->core && !it && dth && (wt==400 || h==-11 || h==-12 || is_pt89(h));  /* DrawText + tabs grid */
  int fxgrid = f->core && wt==400 && !it && (h==-11||h==-12||h==-16||h==-24||is_pt89(h));
  int abcfgrid = core && (h==-11||h==-12||h==-16||is_pt89(h));
  HFONT hf=mkfont(f->facew,h,wt,it,0,DEFAULT_QUALITY), of;
  rk k; fkey(&k,f,h,wt,it); rv v;
  if(!sel_font(dc,hf,&of)){
    rec_note("char_widths font not created/selected face=%s req_h=%d wt=%d it=%d hf=%d", f->face, h, wt, it, hf!=NULL);
    rv_reset(&v); for(int i=0;i<18;i++) rv_na(&v); rec_row("font",&k,&v);
    if(hf) DeleteObject(hf);
    return;
  }
  TEXTMETRICW tm; memset(&tm,0,sizeof tm); WCHAR got[LF_FACESIZE]=L"";
  int okf=GetTextFaceW(dc,LF_FACESIZE,got)>0, okt=GetTextMetricsW(dc,&tm);
  rv_reset(&v); if(okf) rv_w(&v,got); else rv_na(&v);
  if(okt){ rv_i(&v,tm.tmHeight); rv_i(&v,tm.tmAscent); rv_i(&v,tm.tmDescent); rv_i(&v,tm.tmInternalLeading); rv_i(&v,tm.tmExternalLeading);
    rv_i(&v,tm.tmAveCharWidth); rv_i(&v,tm.tmMaxCharWidth); rv_i(&v,tm.tmOverhang); rv_i(&v,tm.tmWeight); rv_i(&v,tm.tmItalic); rv_x(&v,tm.tmPitchAndFamily); rv_i(&v,tm.tmCharSet);
    rv_x(&v,tm.tmFirstChar); rv_x(&v,tm.tmLastChar); rv_x(&v,tm.tmDefaultChar); rv_x(&v,tm.tmBreakChar); rv_i(&v,(tm.tmPitchAndFamily&TMPF_TRUETYPE)?1:0); }
  else for(int i=0;i<17;i++) rv_na(&v);
  rec_row("font",&k,&v);

  cdata *c=&g_cd; memset(c,0,sizeof *c);
  DWORD abc_err=0;
  for(int b=0;b<g_nblk;b++){
    block *bl=&g_blk[b];
    c->ok_cw32[b]=get_cw(dc,bl,c->cw32[b],GetCharWidth32W);
    c->ok_cw[b]=get_cw(dc,bl,c->cw[b],GetCharWidthW);
    c->ok_cwf[b]=get_cwf(dc,bl,c->cwf[b]);
    SetLastError(0); c->ok_abc[b]=get_abc(dc,bl,c->abc[b]); if(!c->ok_abc[b] && !abc_err) abc_err=GetLastError();
    c->ok_abcf[b]=get_abcf(dc,bl,c->abcf[b]);
    WORD gi0[NB]; int okg=GetGlyphIndicesW(dc,bl->c,NB,gi0,0)!=GDI_ERROR;
    c->ok_gi[b]=GetGlyphIndicesW(dc,bl->c,NB,c->gim[b],GGI_MARK_NONEXISTING_GLYPHS)!=GDI_ERROR;
    memcpy(c->gi[b],gi0,sizeof gi0);
    c->ok_cwi[b]=okg && GetCharWidthI(dc,0,NB,gi0,c->cwi[b]);
    c->ok_abci[b]=okg && GetCharABCWidthsI(dc,0,NB,gi0,c->abci[b]);
  }
  c->ok_cwa=GetCharWidth32A(dc,0x20,0x7F,c->cwa)!=0;
  /* rows */
  for(int b=0;b<g_nblk;b++){
    rk kb=k; rk_s(&kb,"blk",g_blk[b].name);
    rv_reset(&v); for(int i=0;i<NB;i++){ if(c->ok_cw32[b]) rv_i(&v,c->cw32[b][i]); else rv_na(&v); } rec_row("cw32",&kb,&v);
    if(c->ok_abc[b]){ rv_reset(&v); for(int i=0;i<NB;i++){ rv_i(&v,c->abc[b][i].abcA); rv_i(&v,c->abc[b][i].abcB); rv_i(&v,c->abc[b][i].abcC); } rec_row("abc",&kb,&v); }
    if(abcfgrid && c->ok_abcf[b]){ rv_reset(&v); for(int i=0;i<NB;i++){ rv_f(&v,c->abcf[b][i].abcfA); rv_f(&v,c->abcf[b][i].abcfB); rv_f(&v,c->abcf[b][i].abcfC); } rec_row("abcf",&kb,&v); }
  }
  if(!g_gi_done[fi][wt==700][it]){
    g_gi_done[fi][wt==700][it]=1;
    for(int b=0;b<g_nblk;b++){
      rk kg; rk_reset(&kg); rk_s(&kg,"face",f->face); rk_i(&kg,"req_h",h); rk_i(&kg,"wt",wt); rk_i(&kg,"it",it); rk_s(&kg,"blk",g_blk[b].name);
      rv_reset(&v); for(int i=0;i<NB;i++){ if(c->ok_gi[b]) rv_i(&v,c->gim[b][i]); else rv_na(&v); } rec_row("gi",&kg,&v);
    }
  }
  /* consistency */
  int nch=g_nblk*NB, ok_cw=1, cw_ne=0, cwa_ne=0, cwi_ok=1, cwi_ne=0, abc_ok=1, abc_ne=0, abci_ok=1, abci_ne=0, abcf_ok=1, abcf_ne=0, missing=0, cwf_ok=1, cwf_ne=0;
  for(int b=0;b<g_nblk;b++){
    if(!c->ok_cw32[b]||!c->ok_cw[b]) ok_cw=0;
    if(!c->ok_cwi[b]) cwi_ok=0;
    if(!c->ok_abc[b]) abc_ok=0;
    if(!c->ok_abci[b]) abci_ok=0;
    if(!c->ok_abcf[b]) abcf_ok=0;
    if(!c->ok_cwf[b]) cwf_ok=0;
    for(int i=0;i<NB;i++){
      if(c->ok_cw32[b]&&c->ok_cw[b]&&c->cw[b][i]!=c->cw32[b][i]) cw_ne++;
      if(c->ok_cw32[b]&&c->ok_cwi[b]&&c->cwi[b][i]!=c->cw32[b][i]) cwi_ne++;
      if(c->ok_cw32[b]&&c->ok_abc[b]&&c->abc[b][i].abcA+(int)c->abc[b][i].abcB+c->abc[b][i].abcC!=c->cw32[b][i]-tm.tmOverhang) abc_ne++;
      if(c->ok_abc[b]&&c->ok_abci[b]&&memcmp(&c->abc[b][i],&c->abci[b][i],sizeof(ABC))) abci_ne++;
      if(c->ok_cw32[b]&&c->ok_abcf[b]){ float s=c->abcf[b][i].abcfA+c->abcf[b][i].abcfB+c->abcf[b][i].abcfC; if((int)floorf(s+0.5f)!=c->cw32[b][i]-tm.tmOverhang) abcf_ne++; }
      if(c->ok_gi[b]&&c->gim[b][i]==0xFFFF) missing++;
      if(c->ok_cw32[b]&&c->ok_cwf[b]&&(int)floorf(c->cwf[b][i]+0.5f)!=c->cw32[b][i]-tm.tmOverhang) cwf_ne++;
    }
  }
  if(c->ok_cwa){ for(int i=0;i<96;i++) if(c->cwa[i]!=c->cw32[i/NB][i%NB]) cwa_ne++; }
  WCHAR asc[96]; for(int i=0;i<95;i++) asc[i]=(WCHAR)(0x20+i); long long sasc=0; int sasc_ok=1;
  for(int i=0;i<95;i++){ if(!c->ok_cw32[i/NB]) sasc_ok=0; sasc+=c->cw32[i/NB][i%NB]; }
  SIZE ea; int oke=GetTextExtentPoint32W(dc,asc,95,&ea);
  rv_reset(&v); rv_i(&v,nch); rv_i(&v,ok_cw); rv_i(&v,cw_ne); if(c->ok_cwa) rv_i(&v,cwa_ne); else rv_na(&v);
  rv_i(&v,cwi_ok); rv_i(&v,cwi_ne); rv_i(&v,abc_ok); rv_i(&v,(long long)abc_err); rv_i(&v,abc_ne); rv_i(&v,abci_ok); rv_i(&v,abci_ne); rv_i(&v,abcf_ok); rv_i(&v,abcf_ne); rv_i(&v,missing);
  if(sasc_ok) rv_i(&v,sasc); else rv_na(&v); if(oke){ rv_i(&v,ea.cx); rv_i(&v,ea.cy); } else { rv_na(&v); rv_na(&v); }
  rv_i(&v,cwf_ok); rv_i(&v,cwf_ne);
  rec_row("cons",&k,&v);
  /* hashes per configuration */
  hash_rows("h_cw32",&k,c->cw32,sizeof c->cw32[0],c->ok_cw32);
  hash_rows("h_cw",&k,c->cw,sizeof c->cw[0],c->ok_cw);
  hash_rows("h_cwf",&k,c->cwf,sizeof c->cwf[0],c->ok_cwf);
  hash_rows("h_cwi",&k,c->cwi,sizeof c->cwi[0],c->ok_cwi);
  hash_rows("h_abc",&k,c->abc,sizeof c->abc[0],c->ok_abc);
  hash_rows("h_abci",&k,c->abci,sizeof c->abci[0],c->ok_abci);
  hash_rows("h_abcf",&k,c->abcf,sizeof c->abcf[0],c->ok_abcf);
  /* kerning */
  {
    DWORD np=GetKerningPairsW(dc,0,NULL); KERNINGPAIR *kp=NULL; DWORD got2=0;
    if(np>0 && np<1000000){ kp=(KERNINGPAIR*)calloc(np,sizeof *kp); got2=GetKerningPairsW(dc,np,kp); }
    rv_reset(&v); rv_i(&v,np);
    for(int i=0;i<16;i++){ if(kp && (DWORD)i<got2){ rv_x(&v,kp[i].wFirst); rv_x(&v,kp[i].wSecond); rv_i(&v,kp[i].iKernAmount); } else { rv_na(&v); rv_na(&v); rv_na(&v); } }
    rec_row("kern",&k,&v);
    if(kp){ rec_hash("h_kern",&k,kp,got2*sizeof *kp); free(kp); } else rec_hash("h_kern",&k,"",0);
  }
  /* extents */
  { static uint8_t dxb[NSTR*128*sizeof(INT)], dxib[NSTR*128*sizeof(INT)]; size_t dxn=0, dxin=0;
    do_ext(dc,f,h,wt,it,core,dxb,&dxn,dxib,&dxin); rec_hash("h_exdx",&k,dxb,dxn); rec_hash("h_exdxi",&k,dxib,dxin); }
  if(dtgrid){ do_tab(dc,f,h,wt,it); do_dt(dc,f,h,wt,it); }
  SelectObject(dc,of); DeleteObject(hf);
  if(fxgrid) do_fx(dc,f,h,wt,it);
}

static void do_quality(HDC dc){
  static const struct { const char *n; int q; } qs[]={{"default",DEFAULT_QUALITY},{"draft",DRAFT_QUALITY},{"proof",PROOF_QUALITY},{"nonaa",NONANTIALIASED_QUALITY},{"aa",ANTIALIASED_QUALITY},{"cleartype",CLEARTYPE_QUALITY},{"ctnatural",CLEARTYPE_NATURAL_QUALITY}};
  int fox=str_find("fox");
  for(int fi=0;fi<g_nface;fi++){ if(!g_face[fi].core) continue;
    static const int hs[]={-11,-12,-16};
    for(int hi=0;hi<3;hi++) for(unsigned qi=0;qi<sizeof qs/sizeof*qs;qi++){
      if(!g_full && qi>3) continue;
      HFONT hf=mkfont(g_face[fi].facew,hs[hi],400,0,0,qs[qi].q), of;
      rk k; rk_reset(&k); rk_s(&k,"face",g_face[fi].face); rk_i(&k,"req_h",hs[hi]); rk_s(&k,"q",qs[qi].n);
      INT w[95]; rv v; rv_reset(&v);
      if(!sel_font(dc,hf,&of)){ for(int i=0;i<5;i++) rv_na(&v); rec_row("qual",&k,&v); if(hf) DeleteObject(hf); continue; }
      if(GetCharWidth32W(dc,0x20,0x7E,w)){ long long s=0; for(int i=0;i<95;i++) s+=w[i]; rv_i(&v,s); rec_hash("h_qual_cw32",&k,w,sizeof w); } else rv_na(&v);
      SIZE sz; if(GetTextExtentPoint32W(dc,g_str[fox].s,str_len(fox),&sz)){ rv_i(&v,sz.cx); rv_i(&v,sz.cy); } else { rv_na(&v); rv_na(&v); }
      TEXTMETRICW tm; if(GetTextMetricsW(dc,&tm)){ rv_i(&v,tm.tmHeight); rv_i(&v,tm.tmAveCharWidth); } else { rv_na(&v); rv_na(&v); }
      rec_row("qual",&k,&v);
      SelectObject(dc,of); DeleteObject(hf);
    }
  }
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"char_widths","2");
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--quick")) g_full=0;
    else if(!strcmp(argv[i],"--full")) g_full=1;
    else if(!strcmp(argv[i],"--heights") && i+1<argc){ i++; g_heights_mode=!strcmp(argv[i],"px")?1:!strcmp(argv[i],"pt")?2:0; }
  }
  rec_env("char_widths.mode","%s",g_full?"full":"quick");
  rec_env("char_widths.heights","%s",g_heights_mode==1?"px":g_heights_mode==2?"pt":"all");
  HDC dc=GetDC(NULL);
  g_dpi=GetDeviceCaps(dc,LOGPIXELSY);
  init_blocks();
  /* faces: core = the UI faces + Arial + Courier New (DrawText, tabs, fx, abcf, exdx grids) */
  add_face("Segoe UI",1,1); add_face("MS Shell Dlg",1,0); add_face("MS Shell Dlg 2",1,0); add_face("Tahoma",1,1);
  add_face("Arial",1,1); add_face("Courier New",1,0);
  if(g_full){
    add_face("Microsoft Sans Serif",1,0); add_face("MS Sans Serif",0,0); add_face("Courier",0,0); add_face("System",0,0);
    add_face("Fixedsys",0,0); add_face("Terminal",0,0); add_face("Times New Roman",0,0); add_face("Verdana",0,0); add_face("Consolas",0,0);
    add_face("NoSuchFace I7",0,0);   /* absent name: records what the mapper falls back to (font.got) */
  } else { g_nface=0; add_face("Segoe UI",1,1); add_face("MS Shell Dlg",1,0); add_face("Tahoma",1,0); add_face("Arial",1,0); add_face("Courier New",1,0); }
  static const int pxf[]={-8,-9,-10,-11,-12,-13,-14,-15,-16,-18,-20,-24,-32}, pxq[]={-11,-12,-16};
  static const int ptf[]={8,9,10,11,12}, ptq[]={8,9};
  const int *px=g_full?pxf:pxq; int npx=g_full?13:3; const int *pt=g_full?ptf:ptq; int npt=g_full?5:2;
  if(g_heights_mode!=2) for(int i=0;i<npx;i++) add_h(px[i]);
  for(int i=0;i<npt;i++) g_pth[g_npt++]=-MulDiv(pt[i],g_dpi,72);
  if(g_heights_mode!=1) for(int i=0;i<npt;i++) add_h(-MulDiv(pt[i],g_dpi,72));
  /* positive lfHeight = cell height (tmHeight requested); core faces only, pixel heights (DPI-independent) */
  static const int cellf[]={13,15,16,19};
  if(g_full && g_heights_mode!=2) for(int i=0;i<4;i++) add_h(cellf[i]);
  schemas();
  for(int b=0;b<g_nblk;b++){ rk k; rk_reset(&k); rk_s(&k,"blk",g_blk[b].name); rv v; rv_reset(&v); for(int i=0;i<NB;i++) rv_x(&v,g_blk[b].c[i]); rec_row("chars",&k,&v); }
  for(int i=0;i<NSTR;i++){ rk k; rk_reset(&k); rk_s(&k,"s",g_str[i].id); rv v; rv_reset(&v); rv_i(&v,str_len(i)); rv_w(&v,g_str[i].s); rec_row("str",&k,&v); }
  for(int i=0;i<npt;i++){ rk k; rk_reset(&k); rk_i(&k,"pt",pt[i]); rv v; rv_reset(&v); rv_i(&v,-MulDiv(pt[i],g_dpi,72)); rv_i(&v,g_dpi); rec_row("ptmap",&k,&v); }
  rec_note("units: px = device pixels (screen DC, MM_TEXT, identity transform); fx table lu = logical units of that row's mapping mode / world transform. req_h = lfHeight as passed (<0 em height, >0 cell height); font.h = tmHeight (cell)");
  int ncfg=0;
  for(int fi=0;fi<g_nface;fi++){
    for(int hi=0;hi<g_nh;hi++){
      if(g_h[hi]>0 && !g_face[fi].core) continue;
      do_config(dc,fi,g_h[hi],400,0); ncfg++;
      if(g_full){ do_config(dc,fi,g_h[hi],700,0); ncfg++; }
      if(g_full && g_face[fi].italic_ok){ do_config(dc,fi,g_h[hi],400,1); ncfg++; }
    }
    rec_flush();
  }
  do_quality(dc);
  ReleaseDC(NULL,dc);
  rec_note("char_widths done configs=%d faces=%d heights=%d blocks=%d strings=%d", ncfg, g_nface, g_nh, g_nblk, NSTR);
  return rec_finish(0);
}
