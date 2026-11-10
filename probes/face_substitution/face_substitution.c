/* face_substitution: CreateFontIndirectW/A/ExW face substitution and font-mapper recorder (REC v1).
 *
 * For a long list of requested LOGFONTs it records WHAT THE MAPPER CHOSE, measured on a DC:
 *   GetTextFaceW (+ return count), GetTextFaceAliasW, the full TEXTMETRIC, GetObject LOGFONT read-back
 *   (byte-equal to the request?), font type (TrueType / raster / vector, TMPF_DEVICE), the realised font's
 *   identity (GetFontData(table 0) size + SHA-256, 'ttcf' size + SHA-256, matched against the SHA-256 of every
 *   file in %WINDIR%\Fonts -> file name), the sfnt 'name' table strings, GetOutlineTextMetricsW face names,
 *   GetFontUnicodeRanges (glyph count / range count / hash), GetTextCharsetInfo, a hash of GetCharWidth32
 *   0x20..0x7E and two text extents.
 * Plus: registry dumps (FontSubstitutes, Fonts, FontLink\SystemLink, FontMapper*, GRE_Initialize, FontDPI,
 *   FontAssoc, LanguagePack, Windows\*Font*, Hardware Profiles fonts, HKCU desktop font/DPI values),
 *   the %WINDIR%\Fonts listing with size + SHA-256, EnumFontFamiliesExW per charset and per family (all styles),
 *   EnumFontsW / EnumFontFamiliesW, stock fonts, font fallback for missing glyphs (GetGlyphIndicesW with and
 *   without GGI_MARK_NONEXISTING_GLYPHS, extents and a 1bpp render of CJK / emoji / RTL / Thai strings in the
 *   UI faces), buffer-protocol / bad-parameter behaviour, and the `answer` table: which face, file, strike and
 *   metrics MS Shell Dlg / MS Shell Dlg 2 / MS Sans Serif / Microsoft Sans Serif give at -8..-13 (GM_COMPATIBLE
 *   and GM_ADVANCED, weight 400 and 700; plus Tahoma, Arial, Segoe UI, Courier, Courier New), with 1bpp renders. Doc: docs/probes/face_substitution.md
 *
 * Units: req_h is lfHeight as passed (req_h<0 = character/em height in logical units, req_h>0 = cell height).
 *   All DCs here are MM_TEXT with an identity transform, so logical = device px. TEXTMETRIC columns are px:
 *   h = tmHeight (cell = ascent + descent), il/el = internal/external leading. Request fields are keyed req_*
 *   (v2) so they never clash with the same-named result columns (cs = tmCharSet, wt = tmWeight, ...).
 * ok column of the measure block: 1 = font created and selected, 0 = CreateFont* returned NULL,
 *   -1 = created but SelectObject failed (all other columns `!`, nothing is measured on the DC's old font).
 * rz_* columns (v2): GetFontRealizationInfo (struct size accepted: 0x32c on Win10 - NtGdiGetRealizationInfo
 *   accepts only 0x10 / 0x32c; 24 on Wine) -> flags, file count, face index in the file (TTC), simulations
 *   (bit0 = emboldened, bit1 = obliqued; RFONTOBJ::GetRealizationInfo @0xaa044), then GetFontFileInfo(file 0)
 *   -> rz_file = lowercase basename of the file that backs the realised font (also raster .fon strikes and
 *   TTC members, which the hash match cannot name) and rz_fsize = its size in bytes (NtGdiGetFontFileInfo).
 * Bitmaps (answer_bmp, fallback_bmp) are 1bpp black-on-white renders, NONANTIALIASED_QUALITY.
 *
 * usage: face_substitution.exe [recorder options, rec.h] [--quick | --full (default)] [--no-filehash]
 *   --quick   smaller matrix (UI faces only, fewer heights/charsets, no per-family enumeration) ~1/5 of the rows
 *   --full    everything (the VM runs this)
 *   --no-filehash  do not hash %WINDIR%\Fonts (sizes only; file matching then fails) - for debugging only
 */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TAG4(a,b,c,d) ((DWORD)(a)|((DWORD)(b)<<8)|((DWORD)(c)<<16)|((DWORD)(d)<<24))
#define T_HEAD TAG4('h','e','a','d')
#define T_NAME TAG4('n','a','m','e')
#define T_TTCF TAG4('t','t','c','f')
#define T_MAXP TAG4('m','a','x','p')

static int g_full=1, g_filehash=1;
static HDC g_scr, g_mem;
typedef int (WINAPI *pfnFaceAlias)(HDC,int,LPWSTR);
static pfnFaceAlias pGetTextFaceAliasW;
typedef BOOL (WINAPI *pfnFontResInfo)(LPCWSTR,DWORD*,LPVOID,DWORD);
static pfnFontResInfo pGetFontResourceInfoW;
typedef BOOL (WINAPI *pfnRzInfo)(HDC,void*);
static pfnRzInfo pGetFontRealizationInfo;
typedef BOOL (WINAPI *pfnFontFileInfo)(DWORD,DWORD,void*,SIZE_T,SIZE_T*);
static pfnFontFileInfo pGetFontFileInfo;

/* ------------------------------------------------------------------ small utils */
/* text value/key: "!" (and NULL) -> the not-available token, "~" or "" -> the empty token; anything else escaped */
static void rv_t(rv *v, const char *s){ if(!s || !strcmp(s,"!")) rv_na(v); else rv_s(v,strcmp(s,"~")?s:""); }
static void rk_t(rk *k, const char *n, const char *s){ rk_s(k,n,(s && strcmp(s,"~"))?s:""); }
static void sha16(char out[17], const void *p, size_t n){ uint8_t d[32]; rec_sha256(p,n,d); rec_hex(out,d,8); }
static uint16_t be16(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void w2u(char *o, int cap, const WCHAR *w){ if(!WideCharToMultiByte(CP_UTF8,0,w,-1,o,cap,NULL,NULL)) o[0]=0; }
static void u2w(WCHAR *o, int cap, const char *u){ if(!MultiByteToWideChar(CP_UTF8,0,u,-1,o,cap)) o[0]=0; }
static int is_ascii(const char *s){ for(;*s;s++) if((unsigned char)*s>=0x80) return 0; return 1; }

/* string set (dedupe of row keys, family names) */
typedef struct { char **v; int n, cap; } sset;
static unsigned long fnv(const char *s){ unsigned long h=2166136261u; for(;*s;s++){ h^=(unsigned char)*s; h*=16777619u; } return h; }
static int sset_add(sset *s, const char *k){ /* 1 if new */
  if(s->n*2>=s->cap){ int nc=s->cap?s->cap*2:1024; char **nv=(char**)calloc(nc,sizeof(char*));
    for(int i=0;i<s->cap;i++) if(s->v[i]){ unsigned long h=fnv(s->v[i])%nc; while(nv[h]) h=(h+1)%nc; nv[h]=s->v[i]; }
    free(s->v); s->v=nv; s->cap=nc; }
  unsigned long h=fnv(k)%s->cap; while(s->v[h]){ if(!strcmp(s->v[h],k)) return 0; h=(h+1)%s->cap; }
  s->v[h]=_strdup(k); s->n++; return 1; }
static void sset_free(sset *s){ for(int i=0;i<s->cap;i++) free(s->v[i]); free(s->v); memset(s,0,sizeof *s); }
/* emit a row whose key may repeat: append ;dup=N */
static sset g_keys;
static void row_u(const char *table, rk *k, const rv *v){
  char probe[RK_MAX+80]; snprintf(probe,sizeof probe,"%s|%s",table,k->b);
  if(!sset_add(&g_keys,probe)){ for(int d=2;;d++){ char p2[RK_MAX+100]; snprintf(p2,sizeof p2,"%s;dup=%d",probe,d); if(sset_add(&g_keys,p2)){ rk_i(k,"dup",d); break; } } }
  rec_row(table,k,v); }

/* ------------------------------------------------------------------ %WINDIR%\Fonts */
typedef struct { char name[260]; unsigned long long size; uint8_t sha[32]; int hashed; } ffile;
static ffile *g_ff; static int g_nff;
static int ffcmp(const void *a, const void *b){ return strcmp(((const ffile*)a)->name,((const ffile*)b)->name); }
static void list_dir(const char *dirkey, const char *dir, int add_global){
  char pat[MAX_PATH]; snprintf(pat,sizeof pat,"%s\\*",dir);
  WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(pat,&fd);
  int cap=512,n=0; ffile *a=(ffile*)calloc(cap,sizeof(ffile));
  if(h!=INVALID_HANDLE_VALUE){ do{ if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue; if(n==cap){ cap*=2; a=(ffile*)realloc(a,cap*sizeof(ffile)); }
      memset(&a[n],0,sizeof a[n]); snprintf(a[n].name,sizeof a[n].name,"%s",fd.cFileName); CharLowerA(a[n].name);
      a[n].size=((unsigned long long)fd.nFileSizeHigh<<32)|fd.nFileSizeLow; n++; } while(FindNextFileA(h,&fd)); FindClose(h); }
  qsort(a,n,sizeof(ffile),ffcmp);
  static int sch=0; if(!sch){ sch=1; rec_schema("fontfile","dir file","size:B sha256 ext"); }
  unsigned long long tot=0;
  for(int i=0;i<n;i++){ char p[MAX_PATH*2]; snprintf(p,sizeof p,"%s\\%s",dir,a[i].name);
    if(g_filehash){ unsigned long long sz; if(rec_file_sha256(p,a[i].sha,&sz)) a[i].hashed=1; }
    rk k; rk_reset(&k); rk_t(&k,"dir",dirkey); rk_t(&k,"file",a[i].name);
    rv v; rv_reset(&v); rv_u(&v,a[i].size); if(a[i].hashed){ char hx[65]; rec_hex(hx,a[i].sha,32); rv_t(&v,hx); } else rv_na(&v);
    const char *e=strrchr(a[i].name,'.'); rv_t(&v,e?e+1:"~"); rec_row("fontfile",&k,&v); tot+=a[i].size; }
  rec_note("fontfile dir=%s files=%d bytes=%llu",dirkey,n,tot);
  if(add_global){ g_ff=a; g_nff=n; } else free(a);
}
static const char *ff_match(const uint8_t sha[32]){ for(int i=0;i<g_nff;i++) if(g_ff[i].hashed && !memcmp(g_ff[i].sha,sha,32)) return g_ff[i].name; return NULL; }
static const ffile *ff_find(const char *name){ for(int i=0;i<g_nff;i++) if(!_stricmp(g_ff[i].name,name)) return &g_ff[i]; return NULL; }

/* ------------------------------------------------------------------ sfnt helpers */
static uint8_t *font_table(HDC dc, DWORD tag, DWORD *len){
  DWORD n=GetFontData(dc,tag,0,NULL,0); *len=0; if(n==GDI_ERROR||n==0) return NULL;
  uint8_t *b=(uint8_t*)malloc(n); if(!b) return NULL;
  if(GetFontData(dc,tag,0,b,n)!=n){ free(b); return NULL; } *len=n; return b; }
/* best string for nameID: Windows/en-US > Windows any > Unicode > Mac Roman */
static void name_str(const uint8_t *t, DWORD len, int nameid, char *out, int cap){
  out[0]=0; if(!t||len<6) return; int cnt=be16(t+2), so=be16(t+4), best=-1, bs=0;
  for(int i=0;i<cnt && 6+12*(i+1)<=(int)len;i++){ const uint8_t *r=t+6+12*i; int pl=be16(r), lang=be16(r+4), id=be16(r+6);
    if(id!=nameid) continue; int s= pl==3 ? (lang==0x409?4:3) : pl==0 ? 2 : pl==1 ? 1 : 0; if(s>bs){ bs=s; best=i; } }
  if(best<0) return; const uint8_t *r=t+6+12*best; int pl=be16(r), l=be16(r+8), off=be16(r+10);
  if(so+off+l>(int)len) return; const uint8_t *p=t+so+off;
  if(pl==1){ int n=l<cap-1?l:cap-1; for(int i=0;i<n;i++) out[i]=(p[i]<0x80&&p[i]>=0x20)?(char)p[i]:'?'; out[n]=0; return; }
  WCHAR w[512]; int n=l/2; if(n>511) n=511; for(int i=0;i<n;i++) w[i]=(WCHAR)be16(p+2*i); w[n]=0; w2u(out,cap,w); }

typedef struct { uint8_t key[32]; char id[17]; char file[260]; } fident;
static fident *g_fid; static int g_nfid, g_cfid;
static int g_font_schema=0;
/* identity of the font realised in dc: id (16 hex) or "!" (GetFontData fails: raster/vector); emits a `font` row once per id */
static void font_ident(HDC dc, char id[17], char file[260]){
  strcpy(id,"!"); strcpy(file,"!");
  DWORD size0=GetFontData(dc,0,0,NULL,0); if(size0==GDI_ERROR||size0==0) return;
  DWORD hl, nl; uint8_t *head=font_table(dc,T_HEAD,&hl), *name=font_table(dc,T_NAME,&nl);
  rec_sha256_ctx c; rec_sha256_init(&c); rec_sha256_update(&c,&size0,4); if(head) rec_sha256_update(&c,head,hl); if(name) rec_sha256_update(&c,name,nl);
  uint8_t key[32]; rec_sha256_final(&c,key);
  for(int i=0;i<g_nfid;i++) if(!memcmp(g_fid[i].key,key,32)){ strcpy(id,g_fid[i].id); strcpy(file,g_fid[i].file); free(head); free(name); return; }
  /* first time: hash the whole data */
  if(g_nfid==g_cfid){ g_cfid=g_cfid?g_cfid*2:64; g_fid=(fident*)realloc(g_fid,g_cfid*sizeof(fident)); }
  fident *f=&g_fid[g_nfid++]; memcpy(f->key,key,32); rec_hex(f->id,key,8);
  uint8_t s0[32], st[32]; char h0[65]="!", ht[65]="!"; DWORD sizet=GDI_ERROR; const char *m=NULL, *mby="none";
  uint8_t *b=(uint8_t*)malloc(size0); int ok0=0; int ntab=0; char tags[64*6]=""; int ttc0=0;
  if(b && GetFontData(dc,0,0,b,size0)==size0){ ok0=1; rec_sha256(b,size0,s0); rec_hex(h0,s0,32);
    if(size0>=12){ if(be32(b)==0x74746366u) ttc0=1; else { ntab=be16(b+4); int nt=ntab>64?64:ntab;
      for(int i=0;i<nt && 12+16*(i+1)<=(int)size0;i++){ char tg[6]; memcpy(tg,b+12+16*i,4); tg[4]=0; if(i) strcat(tags,","); strcat(tags,tg); } } } }
  free(b);
  if(ok0){ m=ff_match(s0); if(m) mby="table0"; }
  sizet=GetFontData(dc,T_TTCF,0,NULL,0);
  if(sizet!=GDI_ERROR && sizet>0){ uint8_t *t=(uint8_t*)malloc(sizet);
    if(t && GetFontData(dc,T_TTCF,0,t,sizet)==sizet){ rec_sha256(t,sizet,st); rec_hex(ht,st,32); if(!m){ m=ff_match(st); if(m) mby="ttcf"; } }
    free(t); }
  snprintf(f->file,sizeof f->file,"%s",m?m:"!"); strcpy(id,f->id); strcpy(file,f->file);
  if(!g_font_schema){ g_font_schema=1;
    rec_schema("font","id","file match_by size0:B sha0 ttcf_size:B ttcf_sha ttc0 ntables tags upem num_glyphs head_rev name_family name_sub name_full name_version name_typo_family name_typo_sub name_ps"); }
  rk k; rk_reset(&k); rk_t(&k,"id",f->id);
  rv v; rv_reset(&v); rv_t(&v,f->file); rv_t(&v,mby); rv_u(&v,size0); rv_t(&v,h0);
  if(sizet!=GDI_ERROR) rv_u(&v,sizet); else rv_na(&v); rv_t(&v,ht); rv_i(&v,ttc0); rv_i(&v,ntab); rv_t(&v,tags[0]?tags:"~");
  if(head && hl>=54){ rv_i(&v,be16(head+18)); } else rv_na(&v);
  { DWORD ml; uint8_t *mx=font_table(dc,T_MAXP,&ml); if(mx&&ml>=6) rv_i(&v,be16(mx+4)); else rv_na(&v); free(mx); }
  if(head && hl>=8){ rv_x(&v,be32(head+4)); } else rv_na(&v);
  static const int IDS[]={1,2,4,5,16,17,6};
  for(unsigned i=0;i<sizeof IDS/sizeof*IDS;i++){ char s[600]; name_str(name,nl,IDS[i],s,sizeof s); if(s[0]) rv_t(&v,s); else rv_na(&v); }
  rec_row("font",&k,&v);
  free(head); free(name);
}

/* ------------------------------------------------------------------ realisation info: which file backs the font */
typedef struct { int ver; DWORD flags, nfiles; int face, sim; char file[260]; unsigned long long fsize; int fok; } rzinfo;
static void rz_get(HDC dc, rzinfo *r){
  memset(r,0,sizeof *r); strcpy(r->file,"!");
  if(!pGetFontRealizationInfo) return;
  static DWORD b[0x400/4]; static const DWORD SZ[]={0x32c,24};
  for(int i=0;i<2 && !r->ver;i++){ memset(b,0,sizeof b); b[0]=SZ[i]; if(pGetFontRealizationInfo(dc,b)) r->ver=(int)SZ[i]; }
  if(!r->ver) return;
  r->flags=b[1]; r->nfiles=b[4]; r->face=((const WORD*)b)[10]; r->sim=((const WORD*)b)[11];
  if(!pGetFontFileInfo || !r->nfiles) return;
  static BYTE fb[16+2*1100]; SIZE_T need=0; memset(fb,0,sizeof fb);
  if(!pGetFontFileInfo(b[3],0,fb,sizeof fb,&need)) return;
  WCHAR *p=(WCHAR*)(fb+16); p[1099]=0; const WCHAR *bs=p;
  for(const WCHAR *q=p;*q;q++) if(*q==L'\\'||*q==L'/') bs=q+1;
  char u[260]; w2u(u,sizeof u,bs); CharLowerA(u); if(u[0]){ snprintf(r->file,sizeof r->file,"%s",u); r->fok=1; }
  memcpy(&r->fsize,fb+8,8);
}
static void rv_rz(rv *v, const rzinfo *r){
  if(!r->ver){ for(int i=0;i<7;i++) rv_na(v); return; }
  rv_x(v,r->ver); rv_x(v,r->flags); rv_u(v,r->nfiles); rv_i(v,r->face); rv_x(v,r->sim);
  if(r->fok){ rv_t(v,r->file); rv_u(v,r->fsize); } else { rv_na(v); rv_na(v); } }
#define RZ_COLS "rz rz_flags rz_nfiles rz_face rz_sim rz_file rz_fsize:B"

/* ------------------------------------------------------------------ the measurement row */
static const WCHAR EXT19[]=L"The quick brown fox";
static const WCHAR EXT52[]=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
#define MEAS_COLS "ok got got_n alias h:px a:px d:px il:px el:px ave:px max:px wt ovh:px dax day first last dflt brk it ul so pf cs " \
  "lf_eq lf_face lf_h lf_cs type font file otm_family otm_face otm_style otm_full otm_em ur_glyphs ur_ranges ur_sha tci_cs csb0 csb1 cw_sha ext19:px ext52:px ext_cy:px " RZ_COLS
#define MEAS_N 53 /* columns of MEAS_COLS */
static const char *ftype(BYTE pf){ static char b[16]; const char *t=(pf&TMPF_TRUETYPE)?"tt":(pf&TMPF_VECTOR)?"vector":"raster"; snprintf(b,sizeof b,"%s%s",t,(pf&TMPF_DEVICE)?"+dev":""); return b; }

/* measures font f in dc; ansi: use the A functions for face/metrics/readback. lfin: the LOGFONTW (or LOGFONTA when ansi) passed. */
static void measure(rv *v, HDC dc, HFONT f, int ansi, const void *lfin, TEXTMETRICW *tmout, char fileout[260]){
  if(tmout) memset(tmout,0,sizeof *tmout); if(fileout) strcpy(fileout,"!");
  if(!f){ rv_i(v,0); for(int i=1;i<MEAS_N;i++) rv_na(v); return; }
  HFONT o=(HFONT)SelectObject(dc,f);
  if(!o || o==(HFONT)HGDI_ERROR){ rv_i(v,-1); for(int i=1;i<MEAS_N;i++) rv_na(v); return; }
  rv_i(v,1);
  if(ansi){ char got[LF_FACESIZE*2]=""; int n=GetTextFaceA(dc,sizeof got,got); if(n>0) rv_t(v,got); else rv_na(v); rv_i(v,n); }
  else { WCHAR got[LF_FACESIZE*2]=L""; int n=GetTextFaceW(dc,LF_FACESIZE*2,got); if(n>0) rv_w(v,got); else rv_na(v); rv_i(v,n); }
  if(pGetTextFaceAliasW){ WCHAR al[LF_FACESIZE*2]=L""; int n=pGetTextFaceAliasW(dc,LF_FACESIZE*2,al); if(n>0) rv_w(v,al); else rv_na(v); } else rv_na(v);
  TEXTMETRICW tm; memset(&tm,0,sizeof tm); int okt;
  if(ansi){ TEXTMETRICA ta; okt=GetTextMetricsA(dc,&ta); if(okt){ tm.tmHeight=ta.tmHeight; tm.tmAscent=ta.tmAscent; tm.tmDescent=ta.tmDescent; tm.tmInternalLeading=ta.tmInternalLeading;
      tm.tmExternalLeading=ta.tmExternalLeading; tm.tmAveCharWidth=ta.tmAveCharWidth; tm.tmMaxCharWidth=ta.tmMaxCharWidth; tm.tmWeight=ta.tmWeight; tm.tmOverhang=ta.tmOverhang;
      tm.tmDigitizedAspectX=ta.tmDigitizedAspectX; tm.tmDigitizedAspectY=ta.tmDigitizedAspectY; tm.tmFirstChar=(BYTE)ta.tmFirstChar; tm.tmLastChar=(BYTE)ta.tmLastChar;
      tm.tmDefaultChar=(BYTE)ta.tmDefaultChar; tm.tmBreakChar=(BYTE)ta.tmBreakChar; tm.tmItalic=ta.tmItalic; tm.tmUnderlined=ta.tmUnderlined; tm.tmStruckOut=ta.tmStruckOut;
      tm.tmPitchAndFamily=ta.tmPitchAndFamily; tm.tmCharSet=ta.tmCharSet; } }
  else okt=GetTextMetricsW(dc,&tm);
  if(okt){ rv_i(v,tm.tmHeight); rv_i(v,tm.tmAscent); rv_i(v,tm.tmDescent); rv_i(v,tm.tmInternalLeading); rv_i(v,tm.tmExternalLeading); rv_i(v,tm.tmAveCharWidth);
    rv_i(v,tm.tmMaxCharWidth); rv_i(v,tm.tmWeight); rv_i(v,tm.tmOverhang); rv_i(v,tm.tmDigitizedAspectX); rv_i(v,tm.tmDigitizedAspectY);
    rv_x(v,tm.tmFirstChar); rv_x(v,tm.tmLastChar); rv_x(v,tm.tmDefaultChar); rv_x(v,tm.tmBreakChar); rv_i(v,tm.tmItalic); rv_i(v,tm.tmUnderlined); rv_i(v,tm.tmStruckOut);
    rv_x(v,tm.tmPitchAndFamily); rv_i(v,tm.tmCharSet); if(tmout) *tmout=tm; }
  else for(int i=0;i<20;i++) rv_na(v);
  /* GetObject read-back */
  if(ansi){ LOGFONTA a; memset(&a,0,sizeof a); int n=GetObjectA(f,sizeof a,&a); if(n>0){ const LOGFONTA *li=(const LOGFONTA*)lfin; char an[LF_FACESIZE+1]; memcpy(an,a.lfFaceName,LF_FACESIZE); an[LF_FACESIZE]=0; rv_i(v,!memcmp(&a,li,offsetof(LOGFONTA,lfFaceName)) && !strncmp(a.lfFaceName,li->lfFaceName,LF_FACESIZE)); rv_t(v,an[0]?an:"~"); rv_i(v,a.lfHeight); rv_i(v,a.lfCharSet); } else { rv_na(v); rv_na(v); rv_na(v); rv_na(v); } }
  else { LOGFONTW a; memset(&a,0,sizeof a); int n=GetObjectW(f,sizeof a,&a); if(n>0){ WCHAR fn[LF_FACESIZE+1]; memcpy(fn,a.lfFaceName,sizeof a.lfFaceName); fn[LF_FACESIZE]=0;
      const LOGFONTW *li=(const LOGFONTW*)lfin; rv_i(v,!memcmp(&a,li,offsetof(LOGFONTW,lfFaceName)) && !wcsncmp(a.lfFaceName,li->lfFaceName,LF_FACESIZE)); if(fn[0]) rv_w(v,fn); else rv_t(v,"~"); rv_i(v,a.lfHeight); rv_i(v,a.lfCharSet); } else { rv_na(v); rv_na(v); rv_na(v); rv_na(v); } }
  rv_t(v,okt?ftype(tm.tmPitchAndFamily):"!");
  char id[17], file[260]; font_ident(dc,id,file); rv_t(v,id); rv_t(v,file); if(fileout) strcpy(fileout,file);
  UINT on=GetOutlineTextMetricsW(dc,0,NULL);
  if(on>0 && on<1000000){ OUTLINETEXTMETRICW *ot=(OUTLINETEXTMETRICW*)calloc(1,on);
    if(ot && GetOutlineTextMetricsW(dc,on,ot)){ const char *ofs[4]={(const char*)ot->otmpFamilyName,(const char*)ot->otmpFaceName,(const char*)ot->otmpStyleName,(const char*)ot->otmpFullName};
      for(int i=0;i<4;i++){ size_t off=(size_t)ofs[i]; if(off && off<on) rv_w(v,(const WCHAR*)((const char*)ot+off)); else rv_na(v); } rv_u(v,ot->otmEMSquare); }
    else for(int i=0;i<5;i++) rv_na(v); free(ot); }
  else for(int i=0;i<5;i++) rv_na(v);
  DWORD un=GetFontUnicodeRanges(dc,NULL);
  if(un>0 && un<4000000){ GLYPHSET *gs=(GLYPHSET*)calloc(1,un); if(gs && GetFontUnicodeRanges(dc,gs)==un){ char h[17]; size_t rb=gs->cRanges*sizeof(WCRANGE); if(offsetof(GLYPHSET,ranges)+rb>un) rb=un-offsetof(GLYPHSET,ranges);
      sha16(h,gs->ranges,rb); rv_u(v,gs->cGlyphsSupported); rv_u(v,gs->cRanges); rv_t(v,h); } else { rv_na(v); rv_na(v); rv_na(v); } free(gs); }
  else { rv_na(v); rv_na(v); rv_na(v); }
  FONTSIGNATURE fs; memset(&fs,0,sizeof fs); int tci=GetTextCharsetInfo(dc,&fs,0);
  rv_i(v,tci); rv_x(v,fs.fsCsb[0]); rv_x(v,fs.fsCsb[1]);
  INT w[95]; if(GetCharWidth32W(dc,0x20,0x7e,w)){ char h[17]; sha16(h,w,sizeof w); rv_t(v,h); } else rv_na(v);
  SIZE s1={0,0}, s2={0,0}; int e1=GetTextExtentPoint32W(dc,EXT19,19,&s1), e2=GetTextExtentPoint32W(dc,EXT52,52,&s2);
  if(e1) rv_i(v,s1.cx); else rv_na(v); if(e2){ rv_i(v,s2.cx); rv_i(v,s2.cy); } else { rv_na(v); rv_na(v); }
  { rzinfo rz; rz_get(dc,&rz); rv_rz(v,&rz); }
  SelectObject(dc,o);
}

/* request description and key */
typedef struct { const char *src, *api; LOGFONTW lf; const char *dcn; int gm; } req;
static void req_key(rk *k, const req *r, const char *facekey){
  rk_reset(k); rk_t(k,"src",r->src); rk_t(k,"api",r->api);
  if(facekey) rk_t(k,"face",facekey); else { WCHAR fn[LF_FACESIZE+1]; memcpy(fn,r->lf.lfFaceName,sizeof r->lf.lfFaceName); fn[LF_FACESIZE]=0; if(fn[0]) rk_w(k,"face",fn); else rk_t(k,"face","~"); }
  rk_i(k,"req_h",r->lf.lfHeight); rk_i(k,"req_w",r->lf.lfWidth); rk_i(k,"req_wt",r->lf.lfWeight); rk_i(k,"req_it",r->lf.lfItalic); rk_i(k,"req_cs",r->lf.lfCharSet);
  rk_x(k,"req_pf",r->lf.lfPitchAndFamily); rk_i(k,"req_op",r->lf.lfOutPrecision); rk_x(k,"req_cp",r->lf.lfClipPrecision); rk_i(k,"req_q",r->lf.lfQuality);
  rk_i(k,"req_esc",r->lf.lfEscapement); rk_i(k,"req_ori",r->lf.lfOrientation); rk_t(k,"dc",r->dcn); rk_i(k,"gm",r->gm);
}
static int g_map_schema=0;
static void map_req(const req *r, const char *facekey){
  if(!g_map_schema){ g_map_schema=1; rec_schema("map","src api face req_h req_w req_wt req_it req_cs req_pf req_op req_cp req_q req_esc req_ori dc gm",MEAS_COLS); }
  HDC dc=!strcmp(r->dcn,"mem")?g_mem:g_scr; int oldgm=SetGraphicsMode(dc,r->gm);
  HFONT f=NULL; int ansi=0; LOGFONTA la;
  if(!strcmp(r->api,"W")) f=CreateFontIndirectW(&r->lf);
  else if(!strcmp(r->api,"A")){ ansi=1; memset(&la,0,sizeof la); memcpy(&la,&r->lf,offsetof(LOGFONTA,lfFaceName));
    WideCharToMultiByte(CP_ACP,0,r->lf.lfFaceName,-1,la.lfFaceName,LF_FACESIZE,NULL,NULL); la.lfFaceName[LF_FACESIZE-1]=0; f=CreateFontIndirectA(&la); }
  else if(!strcmp(r->api,"ExW")){ ENUMLOGFONTEXDVW e; memset(&e,0,sizeof e); e.elfEnumLogfontEx.elfLogFont=r->lf; e.elfDesignVector.dvReserved=STAMP_DESIGNVECTOR; e.elfDesignVector.dvNumAxes=0; f=CreateFontIndirectExW(&e); }
  else if(!strcmp(r->api,"CreateFontW")){ const LOGFONTW *l=&r->lf; f=CreateFontW(l->lfHeight,l->lfWidth,l->lfEscapement,l->lfOrientation,l->lfWeight,l->lfItalic,l->lfUnderline,l->lfStrikeOut,l->lfCharSet,l->lfOutPrecision,l->lfClipPrecision,l->lfQuality,l->lfPitchAndFamily,l->lfFaceName); }
  rk k; req_key(&k,r,facekey); rv v; rv_reset(&v);
  measure(&v,dc,f,ansi,ansi?(const void*)&la:(const void*)&r->lf,NULL,NULL);
  row_u("map",&k,&v);
  if(f) DeleteObject(f);
  SetGraphicsMode(dc,oldgm?oldgm:GM_COMPATIBLE);
}
static LOGFONTW lf_make(const char *face_utf8, int h, int cs){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=FW_NORMAL; lf.lfCharSet=(BYTE)cs; u2w(lf.lfFaceName,LF_FACESIZE,face_utf8); lf.lfFaceName[LF_FACESIZE-1]=0; return lf; }
static void map_simple(const char *src, const char *api, const LOGFONTW *lf, const char *dcn, int gm){ req r; r.src=src; r.api=api; r.lf=*lf; r.dcn=dcn; r.gm=gm; map_req(&r,NULL); }

/* ------------------------------------------------------------------ face lists */
/* core = in every sub-matrix; others only in the base list */
static const char *CORE[]={"Segoe UI","MS Shell Dlg","MS Shell Dlg 2","MS Sans Serif","Microsoft Sans Serif","Tahoma","Arial","Courier","Courier New",
  "System","Fixedsys","Terminal","Times New Roman","Helv","Tms Rmn","Times","Helvetica","Symbol","SimSun","MS Gothic","","NoSuchFace","@SimSun","Marlett",
  "Wingdings","Small Fonts","Modern","Roman","Script","MS Serif","Lucida Console","Consolas"};
static const char *SMALL[]={"Segoe UI","MS Shell Dlg","MS Shell Dlg 2","MS Sans Serif","Microsoft Sans Serif","Tahoma","Arial","System","Courier","Courier New","NoSuchFace",""};
static const char *EXTRA[]={
  /* real TrueType families on stock Win10 (absence is a datum) */
  "Verdana","Calibri","Cambria","Cambria Math","Candara","Constantia","Corbel","Georgia","Trebuchet MS","Impact","Comic Sans MS","Palatino Linotype",
  "Sylfaen","Lucida Sans Unicode","Franklin Gothic Medium","Arial Black","Arial Narrow","Segoe UI Light","Segoe UI Semibold","Segoe UI Semilight","Segoe UI Black",
  "Segoe UI Symbol","Segoe UI Emoji","Segoe UI Historic","Segoe MDL2 Assets","Segoe Print","Segoe Script","Gabriola","Ebrima","Gadugi","Javanese Text",
  "Leelawadee UI","Myanmar Text","Nirmala UI","Mongolian Baiti","MV Boli","Microsoft Himalaya","Microsoft New Tai Lue","Microsoft PhagsPa","Microsoft Tai Le",
  "Microsoft Yi Baiti","Sitka Text","Webdings","Wingdings 2","Wingdings 3","Bahnschrift","Ink Free","HoloLens MDL2 Assets",
  "SimHei","NSimSun","Microsoft YaHei","Microsoft YaHei UI","Microsoft JhengHei","Microsoft JhengHei UI","MingLiU","PMingLiU","MingLiU_HKSCS",
  "MS PGothic","MS UI Gothic","MS Mincho","MS PMincho","Meiryo","Meiryo UI","Yu Gothic","Yu Gothic UI","Yu Mincho","Malgun Gothic","Gulim","Batang","Dotum","Gungsuh",
  "Arial Unicode MS","Lucida Sans","Book Antiqua","Garamond","Century Gothic","Estrangelo Edessa","Mangal","Gautami","Latha","Raavi","Shruti","Tunga","Vrinda","Kartika",
  /* raster / vector / legacy names */
  "Courier 10,12,15","MS Sans Serif 8,10,12,14,18,24","Roman","Modern","Script","WST_Czec","WST_Engl","Fixedsys Excelsior","Terminal Bold",
  /* FontSubstitutes classics and aliases (case / variants) */
  "Arial CE","Arial CYR","Arial Greek","Arial TUR","Arial Baltic","Courier New CE","Courier New CYR","Times New Roman CE","Times New Roman Greek","Tahoma Armenian",
  "MS Shell Dlg 3","MS Shell Dlg2","MS Shell","Shell Dlg","Default Sans Serif","Microsoft Sans","MS Sans","Sans Serif","Serif","Monospace","Lucida Grande",
  "Tms Rmn ","Helv ","Helvetica Neue","Times New Roman Baltic","Courier New Baltic","Arial Unicode","Lucida Console Bold","OCR-A","Symbol Set SWA",
  /* case variants */
  "segoe ui","SEGOE UI","ms shell dlg","MS SHELL DLG","ms shell dlg 2","MS SHELL DLG 2","tahoma","TAHOMA","aRiAl","helv","HELV","system","SYSTEM","fixedsys",
  "microsoft sans serif","MICROSOFT SANS SERIF","ms sans serif","courier new","COURIER",
  /* whitespace / punctuation edge cases */
  " Arial","Arial ","Arial  ","Ari al","Arial\t","Segoe UI,0","Arial,204","Tahoma;","MS Shell Dlg\x7f","-","@","@@SimSun","*","?",
  /* @ = vertical */
  "@Segoe UI","@MS Shell Dlg","@MS Shell Dlg 2","@Arial","@MS Gothic","@SimSun","@Microsoft YaHei","@Malgun Gothic","@NoSuchFace","@System","@Tahoma","@MS UI Gothic",
  /* localized (FM_INFO_FAMILY_EQUIV) aliases */
  "\xe5\xae\x8b\xe4\xbd\x93" /* SimSun */, "\xe9\xbb\x91\xe4\xbd\x93" /* SimHei */, "\xe5\xbe\xae\xe8\xbd\xaf\xe9\x9b\x85\xe9\xbb\x91" /* Microsoft YaHei */,
  "\xef\xbc\xad\xef\xbc\xb3 \xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf" /* MS Gothic, fullwidth MS */, "\xea\xb5\xb4\xeb\xa6\xbc" /* Gulim */,
  "\xeb\xa7\x91\xec\x9d\x80 \xea\xb3\xa0\xeb\x94\x95" /* Malgun Gothic */, "\xe6\x96\xb0\xe7\xb4\xb0\xe6\x98\x8e\xe9\xab\x94" /* PMingLiU */, "\xe3\x83\xa1\xe3\x82\xa4\xe3\x83\xaa\xe3\x82\xaa" /* Meiryo */,
  /* nonexistent */
  "NoSuchFace2","zzzz","Arial Bogus","Segoe UI Bogus","Tahoma Bogus","x",
  /* 31-char boundary: exactly 31 chars (fits with NUL), and 30 */
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcde","ABCDEFGHIJKLMNOPQRSTUVWXYZabcd","Segoe UIxxxxxxxxxxxxxxxxxxxxxxx","Microsoft Sans Serifxxxxxxxxxxx"};
static const int CS[]={0,1,2,77,128,129,130,134,136,161,162,163,177,178,186,204,222,238,255,3,100,254};
static const int CS_QUICK[]={0,1,2,128,204,255};
#define N(a) ((int)(sizeof(a)/sizeof*(a)))

/* ------------------------------------------------------------------ registry */
static int g_reg_schema=0, g_reg_ord=0;
static void reg_row(const char *path, const char *name, int idx, const char *type, const char *val){
  if(!g_reg_schema){ g_reg_schema=1; rec_schema("reg","k n i","ord type value"); }
  rk k; rk_reset(&k); rk_t(&k,"k",path); rk_t(&k,"n",name[0]?name:"~"); rk_i(&k,"i",idx);
  rv v; rv_reset(&v); rv_i(&v,g_reg_ord++); rv_t(&v,type); rv_t(&v,val[0]?val:"~"); row_u("reg",&k,&v); }
static int has_font(const char *s){ char b[400]; snprintf(b,sizeof b,"%s",s); CharLowerA(b); return strstr(b,"font")||strstr(b,"dpi")||strstr(b,"logpixels")||strstr(b,"smooth"); }
static void reg_dump(HKEY root, const char *rootname, const WCHAR *path, REGSAM view, int depth, int filter){
  char upath[1200], fullp[1400]; w2u(upath,sizeof upath,path); snprintf(fullp,sizeof fullp,"%s\\%s%s",rootname,upath,view==KEY_WOW64_32KEY?"(wow32)":"");
  HKEY k; if(RegOpenKeyExW(root,path,0,KEY_READ|view,&k)){ reg_row(fullp,"~",-1,"missing","!"); return; }
  static BYTE data[131072];
  for(DWORD i=0;;i++){ WCHAR nm[1024]; DWORD nl=1024, dl=sizeof data-4, t=0; LONG r=RegEnumValueW(k,i,nm,&nl,NULL,&t,data,&dl);
    if(r==ERROR_NO_MORE_ITEMS) break; if(r) { reg_row(fullp,"~",-2,"enumerr","!"); continue; }
    char unm[2100]; w2u(unm,sizeof unm,nm); if(filter && !has_font(unm)) continue;
    data[dl]=0; data[dl+1]=0;
    if(t==REG_SZ||t==REG_EXPAND_SZ){ static char u[140000]; w2u(u,sizeof u,(WCHAR*)data); if(strlen(u)>6000) u[6000]=0; reg_row(fullp,unm,0,t==REG_SZ?"sz":"expand_sz",u); }
    else if(t==REG_MULTI_SZ){ WCHAR *p=(WCHAR*)data; int j=0; while((BYTE*)p<data+dl && *p){ static char u[8000]; w2u(u,sizeof u,p); reg_row(fullp,unm,j++,"multi_sz",u); p+=wcslen(p)+1; } if(!j) reg_row(fullp,unm,0,"multi_sz","~"); }
    else if(t==REG_DWORD && dl==4){ char b[40]; snprintf(b,sizeof b,"%lu",*(DWORD*)data); reg_row(fullp,unm,0,"dword",b); }
    else if(t==REG_QWORD && dl==8){ char b[40]; snprintf(b,sizeof b,"%llu",*(unsigned long long*)data); reg_row(fullp,unm,0,"qword",b); }
    else { static char hx[2*1024+1]; DWORD n=dl>1024?1024:dl; rec_hex(hx,data,n); char ty[40]; snprintf(ty,sizeof ty,"type%lu_len%lu",t,dl); reg_row(fullp,unm,0,ty,n?hx:"~"); } }
  if(depth>0){ WCHAR sub[300]; for(DWORD i=0;;i++){ DWORD sl=300; if(RegEnumKeyExW(k,i,sub,&sl,NULL,NULL,NULL,NULL)) break;
      WCHAR np[1200]; _snwprintf(np,1200,L"%s\\%s",path,sub); np[1199]=0; reg_dump(root,rootname,np,view,depth-1,filter); } }
  RegCloseKey(k);
}
static void do_registry(void){
  static const struct { const WCHAR *p; int depth, filter; } NT[]={
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",0,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",0,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontLink",2,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontMapper",1,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontMapperFamilyFallback",1,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontMapperFamilyFallbackDeprecated",1,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\GRE_Initialize",2,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontDPI",1,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontAssoc",2,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontDrivers",1,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Font Drivers",1,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontIntensityCorrection",1,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontCache",1,0},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\LanguagePack",3,0},
    {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows",0,1},{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutesDeprecated",0,0},
    {L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts",1,0},{L"SYSTEM\\CurrentControlSet\\Control\\FontAssoc",2,0},
    {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Fonts",1,0}};
  for(int i=0;i<N(NT);i++) reg_dump(HKEY_LOCAL_MACHINE,"HKLM",NT[i].p,KEY_WOW64_64KEY,NT[i].depth,NT[i].filter);
  reg_dump(HKEY_LOCAL_MACHINE,"HKLM",L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",KEY_WOW64_32KEY,0,0);
  reg_dump(HKEY_CURRENT_USER,"HKCU",L"Control Panel\\Desktop",KEY_WOW64_64KEY,1,1);
  reg_dump(HKEY_CURRENT_USER,"HKCU",L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",KEY_WOW64_64KEY,0,0);
  reg_dump(HKEY_CURRENT_USER,"HKCU",L"Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",KEY_WOW64_64KEY,0,0);
}

/* ------------------------------------------------------------------ enumeration */
typedef struct { int qcs; const char *mode; sset *fams; int count; } enum_ctx;
static int CALLBACK ef_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp){
  enum_ctx *c=(enum_ctx*)lp; const ENUMLOGFONTEXW *e=(const ENUMLOGFONTEXW*)lf; c->count++;
  if(c->fams){ char u[200]; w2u(u,sizeof u,lf->lfFaceName); sset_add(c->fams,u); }
  rk k; rk_reset(&k); rk_i(&k,"q_cs",c->qcs); rk_w(&k,"face",lf->lfFaceName); rk_i(&k,"cs",lf->lfCharSet); rk_x(&k,"type",type);
  rv v; rv_reset(&v); rv_w(&v,e->elfFullName); rv_w(&v,e->elfStyle); rv_w(&v,e->elfScript); rv_x(&v,lf->lfPitchAndFamily); rv_i(&v,lf->lfOutPrecision); rv_i(&v,lf->lfHeight);
  rv_i(&v,tm->tmHeight); rv_i(&v,tm->tmAveCharWidth);
  if(type&TRUETYPE_FONTTYPE){ const NEWTEXTMETRICEXW *n=(const NEWTEXTMETRICEXW*)tm; rv_x(&v,n->ntmTm.ntmFlags); rv_x(&v,n->ntmFontSig.fsCsb[0]); } else { rv_na(&v); rv_na(&v); }
  row_u("enum_fam",&k,&v); return 1; }
static int CALLBACK es_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp){
  enum_ctx *c=(enum_ctx*)lp; const ENUMLOGFONTEXW *e=(const ENUMLOGFONTEXW*)lf; c->count++;
  rk k; rk_reset(&k); rk_t(&k,"q_face",c->mode); rk_w(&k,"face",lf->lfFaceName); rk_w(&k,"full",e->elfFullName); rk_w(&k,"style",e->elfStyle); rk_i(&k,"cs",lf->lfCharSet); rk_x(&k,"type",type); rk_i(&k,"ent_h",lf->lfHeight); rk_i(&k,"ent_w",lf->lfWidth);
  rv v; rv_reset(&v); rv_w(&v,e->elfScript);
  rv_i(&v,lf->lfHeight); rv_i(&v,lf->lfWidth); rv_i(&v,lf->lfWeight); rv_i(&v,lf->lfItalic); rv_x(&v,lf->lfPitchAndFamily); rv_i(&v,lf->lfOutPrecision); rv_i(&v,lf->lfClipPrecision); rv_i(&v,lf->lfQuality);
  rv_i(&v,tm->tmHeight); rv_i(&v,tm->tmAscent); rv_i(&v,tm->tmDescent); rv_i(&v,tm->tmInternalLeading); rv_i(&v,tm->tmExternalLeading); rv_i(&v,tm->tmAveCharWidth); rv_i(&v,tm->tmMaxCharWidth);
  rv_i(&v,tm->tmWeight); rv_i(&v,tm->tmOverhang); rv_i(&v,tm->tmDigitizedAspectX); rv_i(&v,tm->tmDigitizedAspectY); rv_x(&v,tm->tmFirstChar); rv_x(&v,tm->tmLastChar); rv_x(&v,tm->tmDefaultChar); rv_x(&v,tm->tmBreakChar); rv_x(&v,tm->tmPitchAndFamily); rv_i(&v,tm->tmCharSet);
  if(type&TRUETYPE_FONTTYPE){ const NEWTEXTMETRICEXW *n=(const NEWTEXTMETRICEXW*)tm; rv_x(&v,n->ntmTm.ntmFlags); rv_u(&v,n->ntmTm.ntmSizeEM); rv_u(&v,n->ntmTm.ntmCellHeight); rv_u(&v,n->ntmTm.ntmAvgWidth);
    char b[80]; snprintf(b,sizeof b,"%08lx.%08lx.%08lx.%08lx",n->ntmFontSig.fsUsb[0],n->ntmFontSig.fsUsb[1],n->ntmFontSig.fsUsb[2],n->ntmFontSig.fsUsb[3]); rv_t(&v,b);
    snprintf(b,sizeof b,"%08lx.%08lx",n->ntmFontSig.fsCsb[0],n->ntmFontSig.fsCsb[1]); rv_t(&v,b); }
  else for(int i=0;i<6;i++) rv_na(&v);
  row_u("enum_style",&k,&v); return 1; }
static int CALLBACK eo_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp){
  enum_ctx *c=(enum_ctx*)lp; c->count++;
  rk k; rk_reset(&k); rk_t(&k,"api",c->mode); rk_i(&k,"q_cs",c->qcs); rk_w(&k,"face",lf->lfFaceName); rk_i(&k,"cs",lf->lfCharSet); rk_x(&k,"type",type); rk_i(&k,"h",lf->lfHeight); rk_i(&k,"wt",lf->lfWeight); rk_i(&k,"it",lf->lfItalic);
  rv v; rv_reset(&v); rv_w(&v,((const ENUMLOGFONTW*)lf)->elfFullName); rv_x(&v,lf->lfPitchAndFamily); rv_i(&v,lf->lfOutPrecision); rv_i(&v,tm->tmHeight); rv_i(&v,tm->tmAveCharWidth); rv_x(&v,tm->tmPitchAndFamily);
  row_u("enum_old",&k,&v); return 1; }

static sset g_fams;
static void do_enum(void){
  rec_schema("enum_fam","q_cs face cs type","full style script lf_pf lf_op lf_h tm_h:px tm_ave:px ntm_flags csb0");
  rec_schema("enum_count","api q_face q_cs","n");
  for(int i=0;i<(g_full?N(CS):N(CS_QUICK));i++){ int cs=g_full?CS[i]:CS_QUICK[i]; LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfCharSet=(BYTE)cs;
    enum_ctx c={cs,"",cs==DEFAULT_CHARSET?&g_fams:NULL,0}; int r=EnumFontFamiliesExW(g_scr,&lf,ef_proc,(LPARAM)&c,0);
    rk k; rk_reset(&k); rk_t(&k,"api","EnumFontFamiliesExW"); rk_t(&k,"q_face","~"); rk_i(&k,"q_cs",cs); rv v; rv_reset(&v); rv_i(&v,c.count); rec_row("enum_count",&k,&v); (void)r; }
  if(!g_fams.n){ /* still collect the DEFAULT list when quick list lacked it */ }
  rec_schema("enum_style","q_face face full style cs type ent_h ent_w","script lf_h lf_w lf_wt lf_it lf_pf lf_op lf_cp lf_q h:px a:px d:px il:px el:px ave:px max:px wt ovh:px dax day first last dflt brk pf tcs ntm_flags ntm_em ntm_cell ntm_avgw usb csb");
  /* per family: all styles x charsets */
  char **fam=NULL; int nf=0;
  if(g_full){ fam=(char**)calloc(g_fams.n+1,sizeof(char*)); for(int i=0;i<g_fams.cap;i++) if(g_fams.v[i]) fam[nf++]=g_fams.v[i];
  }
  if(g_full){ /* sort properly */ for(int i=1;i<nf;i++){ char *x=fam[i]; int j=i-1; while(j>=0 && strcmp(fam[j],x)>0){ fam[j+1]=fam[j]; j--; } fam[j+1]=x; } }
  static const char *QF[]={"Segoe UI","Tahoma","Arial","Microsoft Sans Serif","MS Sans Serif","MS Shell Dlg","MS Shell Dlg 2","Courier","Courier New","System","Fixedsys","Terminal","MS Serif","Small Fonts","Helv","NoSuchFace"};
  int nq=g_full?nf:N(QF); sset extra={0}; for(int i=0;i<nf;i++) sset_add(&extra,fam[i]);
  for(int i=0;i<nq + (g_full?N(QF):0);i++){
    const char *face = i<nq ? (g_full?fam[i]:QF[i]) : QF[i-nq];
    if(g_full && i>=nq){ /* QF names that are not enumerated family names (aliases, NoSuchFace) */ if(!sset_add(&extra,face)) continue; }
    LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfCharSet=DEFAULT_CHARSET; u2w(lf.lfFaceName,LF_FACESIZE,face);
    enum_ctx c={1,face,NULL,0}; EnumFontFamiliesExW(g_scr,&lf,es_proc,(LPARAM)&c,0);
    rk k; rk_reset(&k); rk_t(&k,"api","EnumFontFamiliesExW"); rk_t(&k,"q_face",face); rk_i(&k,"q_cs",1); rv v; rv_reset(&v); rv_i(&v,c.count); row_u("enum_count",&k,&v); }
  free(fam); sset_free(&extra);
  /* legacy enumerators */
  rec_schema("enum_old","api q_cs face cs type h wt it","full lf_pf lf_op tm_h:px tm_ave:px tm_pf");
  { enum_ctx c={-1,"EnumFontsW",NULL,0}; EnumFontsW(g_scr,NULL,eo_proc,(LPARAM)&c); rk k; rk_reset(&k); rk_t(&k,"api","EnumFontsW"); rk_t(&k,"q_face","~"); rk_i(&k,"q_cs",-1); rv v; rv_reset(&v); rv_i(&v,c.count); row_u("enum_count",&k,&v); }
  { enum_ctx c={-1,"EnumFontFamiliesW",NULL,0}; EnumFontFamiliesW(g_scr,NULL,eo_proc,(LPARAM)&c); rk k; rk_reset(&k); rk_t(&k,"api","EnumFontFamiliesW"); rk_t(&k,"q_face","~"); rk_i(&k,"q_cs",-1); rv v; rv_reset(&v); rv_i(&v,c.count); row_u("enum_count",&k,&v); }
}

/* ------------------------------------------------------------------ stock fonts */
static void do_stock(void){
  static const struct { int id; const char *n; } ST[]={{OEM_FIXED_FONT,"OEM_FIXED_FONT"},{ANSI_FIXED_FONT,"ANSI_FIXED_FONT"},{ANSI_VAR_FONT,"ANSI_VAR_FONT"},{SYSTEM_FONT,"SYSTEM_FONT"},
    {DEVICE_DEFAULT_FONT,"DEVICE_DEFAULT_FONT"},{SYSTEM_FIXED_FONT,"SYSTEM_FIXED_FONT"},{DEFAULT_GUI_FONT,"DEFAULT_GUI_FONT"}};
  /* obj_*: the stock object's own LOGFONT (GetObjectW); the measure block follows (its lf_* = the same read-back, compared) */
  rec_schema("stock","obj dc","obj_n obj_h obj_w obj_wt obj_it obj_cs obj_op obj_cp obj_q obj_pf obj_face " MEAS_COLS " is_system_font");
  sset seen={0};
  for(int d=0;d<2;d++){ HDC dc=d?g_mem:g_scr;
    for(int i=0;i<N(ST);i++){ HFONT f=(HFONT)GetStockObject(ST[i].id); LOGFONTW lf; memset(&lf,0,sizeof lf); int n=GetObjectW(f,sizeof lf,&lf);
      rk k; rk_reset(&k); rk_t(&k,"obj",ST[i].n); rk_t(&k,"dc",d?"mem":"screen");
      rv v; rv_reset(&v); rv_i(&v,n); rv_i(&v,lf.lfHeight); rv_i(&v,lf.lfWidth); rv_i(&v,lf.lfWeight); rv_i(&v,lf.lfItalic); rv_i(&v,lf.lfCharSet); rv_i(&v,lf.lfOutPrecision); rv_i(&v,lf.lfClipPrecision); rv_i(&v,lf.lfQuality); rv_x(&v,lf.lfPitchAndFamily);
      if(lf.lfFaceName[0]) rv_w(&v,lf.lfFaceName); else rv_t(&v,"~");
      measure(&v,dc,f,0,&lf,NULL,NULL); rv_i(&v,f==(HFONT)GetStockObject(SYSTEM_FONT)); row_u("stock",&k,&v);
      /* EnumFonts on the stock face name */
      if(!d && lf.lfFaceName[0]){ char u[100]; w2u(u,sizeof u,lf.lfFaceName); if(sset_add(&seen,u)){ enum_ctx c={-2,"EnumFontsW.stock",NULL,0}; EnumFontsW(g_scr,lf.lfFaceName,eo_proc,(LPARAM)&c); } } }
    /* the DC's initial font */
    HDC tmp=CreateCompatibleDC(d?g_mem:g_scr); HFONT cur=(HFONT)GetCurrentObject(tmp,OBJ_FONT); LOGFONTW lf; memset(&lf,0,sizeof lf); int n=GetObjectW(cur,sizeof lf,&lf);
    rk k; rk_reset(&k); rk_t(&k,"obj","dc_initial"); rk_t(&k,"dc",d?"mem.compat":"screen.compat");
    rv v; rv_reset(&v); rv_i(&v,n); rv_i(&v,lf.lfHeight); rv_i(&v,lf.lfWidth); rv_i(&v,lf.lfWeight); rv_i(&v,lf.lfItalic); rv_i(&v,lf.lfCharSet); rv_i(&v,lf.lfOutPrecision); rv_i(&v,lf.lfClipPrecision); rv_i(&v,lf.lfQuality); rv_x(&v,lf.lfPitchAndFamily);
    if(lf.lfFaceName[0]) rv_w(&v,lf.lfFaceName); else rv_t(&v,"~");
    measure(&v,tmp,cur,0,&lf,NULL,NULL); rv_i(&v,cur==(HFONT)GetStockObject(SYSTEM_FONT)); /* v1 put the identity into `ok` and left the block NA */
    row_u("stock",&k,&v); DeleteDC(tmp); }
  sset_free(&seen);
}

/* ------------------------------------------------------------------ fallback for missing glyphs */
static void do_fallback(void){
  static const struct { const char *n; const WCHAR *s; } TX[]={
    {"latin",L"Ag"},{"cyr",L"\x0416\x0436\x042F"},{"greek",L"\x03A9\x03BB\x03B2"},{"hebrew",L"\x05E9\x05DC\x05D5\x05DD"},{"arabic",L"\x0633\x0644\x0627\x0645"},
    {"thai",L"\x0E2A\x0E27\x0E31\x0E2A"},{"devanagari",L"\x0928\x092E\x0938\x094D"},{"cjk",L"\x4E2D\x6587\x5B57"},{"kana",L"\x65E5\x672C\x304B\x306A\x30AB"},
    {"hangul",L"\xD55C\xAD6D\xC5B4"},{"emoji",L"\xD83D\xDE00\xD83D\xDC4D"},{"sym",L"\x263A\x2605\x2665\x20AC"},{"box",L"\x2500\x2502\x250C\x2588"},
    {"pua",L"\xE000\xF020"},{"nonchar",L"\xFFFF\xFFFE"},{"ctrl",L"\x0001\x0009\x000A"},{"nbsp_shy",L"\x00A0\x00AD\x2011"},{"latinext",L"\x0151\x0171\x1EA0"},{"armenian",L"\x0531\x0561"},{"georgian",L"\x10D0\x10D1"}};
  static const char *FF[]={"Tahoma","Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Microsoft Sans Serif","MS Sans Serif","Arial","Courier New","System","SimSun","Microsoft YaHei","MS Gothic","Malgun Gothic","Segoe UI Emoji","Segoe UI Symbol","Times New Roman","NoSuchFace"};
  int nff=g_full?N(FF):8;
  rec_schema("fallback","face req_h q text","got n ret_mark idx_mark ret_plain idx_plain missing ext_cx:px ext_cy:px abc_ok font file rz_file rz_face");
  for(int fi=0;fi<nff;fi++) for(int hq=0;hq<2;hq++){
    int h=hq?-16:-12; LOGFONTW lf=lf_make(FF[fi],h,DEFAULT_CHARSET); lf.lfQuality=NONANTIALIASED_QUALITY;
    HFONT f=CreateFontIndirectW(&lf); HFONT o=(HFONT)SelectObject(g_scr,f);
    WCHAR got[64]=L""; GetTextFaceW(g_scr,64,got); char id[17], file[260]; font_ident(g_scr,id,file); rzinfo rz; rz_get(g_scr,&rz);
    for(int ti=0;ti<N(TX);ti++){ const WCHAR *s=TX[ti].s; int n=(int)wcslen(s); WORD a[16], b[16]; memset(a,0,sizeof a); memset(b,0,sizeof b);
      DWORD r1=GetGlyphIndicesW(g_scr,s,n,a,GGI_MARK_NONEXISTING_GLYPHS), r2=GetGlyphIndicesW(g_scr,s,n,b,0);
      char ia[16*6+1]="", ib[16*6+1]=""; int miss=0; for(int i=0;i<n;i++){ char t[8]; snprintf(t,sizeof t,"%s%x",i?",":"",a[i]); strcat(ia,t); snprintf(t,sizeof t,"%s%x",i?",":"",b[i]); strcat(ib,t); if(a[i]==0xffff) miss++; }
      SIZE sz={0,0}; int okx=GetTextExtentPoint32W(g_scr,s,n,&sz);
      ABC abc[4]; int okabc=GetCharABCWidthsW(g_scr,s[0],s[0],abc);
      rk k; rk_reset(&k); rk_t(&k,"face",FF[fi]); rk_i(&k,"req_h",h); rk_t(&k,"q","nonaa"); rk_t(&k,"text",TX[ti].n);
      rv v; rv_reset(&v); rv_w(&v,got); rv_i(&v,n); rv_i(&v,(int)r1); rv_t(&v,r1==GDI_ERROR?"!":ia); rv_i(&v,(int)r2); rv_t(&v,r2==GDI_ERROR?"!":ib); rv_i(&v,miss);
      if(okx){ rv_i(&v,sz.cx); rv_i(&v,sz.cy); } else { rv_na(&v); rv_na(&v); } rv_i(&v,okabc); rv_t(&v,id); rv_t(&v,file); if(rz.fok) rv_t(&v,rz.file); else rv_na(&v); if(rz.ver) rv_i(&v,rz.face); else rv_na(&v); row_u("fallback",&k,&v);
      /* render: does TextOut fall back / link? 4bpp, black on white, non-antialiased */
      if(h==-12){ int W=8+n*16, H=20; if(W>96) W=96; rec_dib d; if(rec_dib_create(&d,W,H)){
          HFONT f2=CreateFontIndirectW(&lf); HFONT o2=(HFONT)SelectObject(d.dc,f2); SetBkMode(d.dc,TRANSPARENT); SetTextColor(d.dc,RGB(0,0,0));
          TextOutW(d.dc,2,1,s,n); GdiFlush(); size_t cap=(size_t)((W+7)/8)*H; uint8_t *g=(uint8_t*)malloc(cap); size_t gn=rec_gray_from_dib(d.bits,W,H,1,g,cap);
          rec_bitmap("fallback_bmp",&k,W,H,1,g,gn); free(g); SelectObject(d.dc,o2); DeleteObject(f2); rec_dib_free(&d); } }
    }
    SelectObject(g_scr,o); DeleteObject(f); }
}

/* ------------------------------------------------------------------ protocol / bad parameters */
static void proto(const char *what, long long ret, DWORD gle, const char *detail){
  rk k; rk_reset(&k); rk_t(&k,"case",what); rv v; rv_reset(&v); rv_i(&v,ret); rv_u(&v,gle); rv_t(&v,detail&&detail[0]?detail:"~"); row_u("proto",&k,&v); }
static void do_proto(void){
  rec_schema("proto","case","ret gle detail");
  char d[600]; HFONT f; LOGFONTW lf; DWORD r;
  SetLastError(0xdead); f=CreateFontIndirectW(NULL); proto("CreateFontIndirectW.null",f!=NULL,GetLastError(),NULL);
  SetLastError(0xdead); f=CreateFontIndirectA(NULL); proto("CreateFontIndirectA.null",f!=NULL,GetLastError(),NULL);
  /* CreateFontIndirectExW(NULL) is not called: gdi32full!CreateFontIndirectExW reads the struct before any check (decomp @0x61e0) */
  { ENUMLOGFONTEXDVW e; memset(&e,0,sizeof e); e.elfEnumLogfontEx.elfLogFont=lf_make("Arial",-12,1); e.elfDesignVector.dvReserved=STAMP_DESIGNVECTOR;
    static const int AX[]={0,1,16,17,100}; for(int i=0;i<N(AX);i++){ e.elfDesignVector.dvNumAxes=AX[i]; SetLastError(0xdead); f=CreateFontIndirectExW(&e); snprintf(d,sizeof d,"axes=%d",AX[i]); char c[60]; snprintf(c,sizeof c,"CreateFontIndirectExW.axes%d",AX[i]); proto(c,f!=NULL,GetLastError(),d); if(f) DeleteObject(f); }
    e.elfDesignVector.dvNumAxes=0; e.elfDesignVector.dvReserved=0; SetLastError(0xdead); f=CreateFontIndirectExW(&e); proto("CreateFontIndirectExW.nostamp",f!=NULL,GetLastError(),NULL); if(f) DeleteObject(f); }
  /* GetObject sizes */
  lf=lf_make("Segoe UI",-12,1); f=CreateFontIndirectW(&lf);
  static const int SZ[]={0,1,4,28,59,60,91,92,93,0x15c,0x1a4,0x1a5,0x250,0x251,1000};
  for(int i=0;i<N(SZ);i++){ static BYTE buf[2048]; memset(buf,0xcc,sizeof buf); SetLastError(0xdead); int n=GetObjectW(f,SZ[i],SZ[i]?buf:NULL); DWORD e=GetLastError();
    int last=-1; for(int j=0;j<(int)sizeof buf;j++) if(buf[j]!=0xcc) last=j; snprintf(d,sizeof d,"written_upto=%d",last+1); char c[60]; snprintf(c,sizeof c,"GetObjectW.font.c%d",SZ[i]); proto(c,n,e,d);
    memset(buf,0xcc,sizeof buf); SetLastError(0xdead); n=GetObjectA(f,SZ[i],SZ[i]?buf:NULL); e=GetLastError(); last=-1; for(int j=0;j<(int)sizeof buf;j++) if(buf[j]!=0xcc) last=j; snprintf(d,sizeof d,"written_upto=%d",last+1); snprintf(c,sizeof c,"GetObjectA.font.c%d",SZ[i]); proto(c,n,e,d); }
  { int n=GetObjectW(f,0,NULL); proto("GetObjectW.font.null0",n,0,NULL); }
  /* GetTextFace buffer protocol */
  HFONT o=(HFONT)SelectObject(g_scr,f);
  static const int TC[]={-1,0,1,2,3,8,9,10,32};
  for(int i=0;i<N(TC);i++){ WCHAR w[64]; for(int j=0;j<64;j++) w[j]=L'#'; SetLastError(0xdead); int n=GetTextFaceW(g_scr,TC[i],w); DWORD e=GetLastError(); w[63]=0; char u[200]; w2u(u,sizeof u,w); char c[60]; snprintf(c,sizeof c,"GetTextFaceW.c%d",TC[i]); proto(c,n,e,u);
    char a[64]; memset(a,'#',sizeof a); SetLastError(0xdead); n=GetTextFaceA(g_scr,TC[i],a); e=GetLastError(); a[63]=0; for(int j=0;j<63;j++) if(!a[j]) a[j]='0'; snprintf(c,sizeof c,"GetTextFaceA.c%d",TC[i]); proto(c,n,e,a); }
  { SetLastError(0xdead); int n=GetTextFaceW(g_scr,0,NULL); proto("GetTextFaceW.null0",n,GetLastError(),NULL); n=GetTextFaceW(g_scr,5,NULL); proto("GetTextFaceW.null5",n,GetLastError(),NULL);
    SetLastError(0xdead); n=GetTextFaceA(g_scr,0,NULL); proto("GetTextFaceA.null0",n,GetLastError(),NULL);
    SetLastError(0xdead); n=GetTextFaceW(NULL,32,(WCHAR[32]){0}); proto("GetTextFaceW.nulldc",n,GetLastError(),NULL); }
  /* GetFontData */
  SelectObject(g_scr,o); DeleteObject(f); lf=lf_make("Arial",-12,1); f=CreateFontIndirectW(&lf); o=(HFONT)SelectObject(g_scr,f);
  { DWORD sz=GetFontData(g_scr,0,0,NULL,0); proto("GetFontData.size0",(int)sz,0,NULL);
    SetLastError(0xdead); r=GetFontData(g_scr,0,0,NULL,100); proto("GetFontData.nullbuf_cb100",(int)r,GetLastError(),NULL);
    BYTE b[64]; /* gdi32full!GetFontDataImpl rejects cb=0xffffffff before reading; Wine copies the whole font, so give it room */
    { BYTE *big=(BYTE*)malloc((size_t)sz+65536); SetLastError(0xdead); r=big?GetFontData(g_scr,0,0,big,0xffffffff):GDI_ERROR; proto("GetFontData.cb_ffffffff",(int)r,GetLastError(),NULL); free(big); }
    SetLastError(0xdead); r=GetFontData(g_scr,TAG4('x','y','z','w'),0,NULL,0); proto("GetFontData.badtag",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontData(g_scr,0,sz,b,4); proto("GetFontData.off_eq_size",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontData(g_scr,0,sz-2,b,4); proto("GetFontData.off_size_m2_cb4",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontData(g_scr,0,sz+100,NULL,0); proto("GetFontData.off_past_null",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontData(g_scr,T_HEAD,0,NULL,0); proto("GetFontData.head",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontData(g_scr,T_TTCF,0,NULL,0); proto("GetFontData.ttcf_on_ttf",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontData(NULL,0,0,NULL,0); proto("GetFontData.nulldc",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontUnicodeRanges(g_scr,NULL); proto("GetFontUnicodeRanges.null",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetFontUnicodeRanges(NULL,NULL); proto("GetFontUnicodeRanges.nulldc",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetOutlineTextMetricsW(g_scr,0,NULL); proto("GetOutlineTextMetricsW.size",(int)r,GetLastError(),NULL);
    SetLastError(0xdead); r=GetOutlineTextMetricsW(g_scr,4,(OUTLINETEXTMETRICW*)(BYTE[64]){0}); proto("GetOutlineTextMetricsW.c4",(int)r,GetLastError(),NULL);
    WORD gi[4]; SetLastError(0xdead); r=GetGlyphIndicesW(g_scr,L"A\xFFFF",2,gi,GGI_MARK_NONEXISTING_GLYPHS); snprintf(d,sizeof d,"%x,%x",gi[0],gi[1]); proto("GetGlyphIndicesW.mark",(int)r,GetLastError(),d);
    SetLastError(0xdead); r=GetGlyphIndicesW(g_scr,L"A\xFFFF",2,gi,0); snprintf(d,sizeof d,"%x,%x",gi[0],gi[1]); proto("GetGlyphIndicesW.plain",(int)r,GetLastError(),d);
    SetLastError(0xdead); r=GetGlyphIndicesW(g_scr,L"A",1,gi,0xff); snprintf(d,sizeof d,"%x",gi[0]); proto("GetGlyphIndicesW.flags_ff",(int)r,GetLastError(),d);
    SetLastError(0xdead); r=GetGlyphIndicesW(g_scr,NULL,0,NULL,0); proto("GetGlyphIndicesW.null0",(int)r,GetLastError(),NULL); }
  SelectObject(g_scr,o); DeleteObject(f);
  /* raster font: GetFontData / OTM / ranges */
  lf=lf_make("MS Sans Serif",-11,1); f=CreateFontIndirectW(&lf); o=(HFONT)SelectObject(g_scr,f);
  SetLastError(0xdead); r=GetFontData(g_scr,0,0,NULL,0); proto("raster.GetFontData",(int)r,GetLastError(),NULL);
  SetLastError(0xdead); r=GetOutlineTextMetricsW(g_scr,0,NULL); proto("raster.GetOutlineTextMetricsW",(int)r,GetLastError(),NULL);
  SetLastError(0xdead); r=GetFontUnicodeRanges(g_scr,NULL); proto("raster.GetFontUnicodeRanges",(int)r,GetLastError(),NULL);
  { ABC abc; SetLastError(0xdead); BOOL b=GetCharABCWidthsW(g_scr,'A','A',&abc); proto("raster.GetCharABCWidthsW",b,GetLastError(),NULL); }
  SelectObject(g_scr,o); DeleteObject(f);
  /* readback of odd LOGFONT fields (stored unchanged?) */
  { LOGFONTW in=lf_make("Arial",-12,1); in.lfWeight=-5; in.lfItalic=0x7f; in.lfUnderline=2; in.lfStrikeOut=0xff; in.lfQuality=0xee; in.lfOutPrecision=0xdd; in.lfClipPrecision=0xcc; in.lfPitchAndFamily=0xff; in.lfEscapement=-3601; in.lfOrientation=12345;
    f=CreateFontIndirectW(&in); LOGFONTW out; memset(&out,0,sizeof out); int n=GetObjectW(f,sizeof out,&out); snprintf(d,sizeof d,"eq=%d wt=%ld it=%d ul=%d so=%d q=%d op=%d cp=%d pf=%d esc=%ld ori=%ld",memcmp(&in,&out,sizeof in)==0,out.lfWeight,out.lfItalic,out.lfUnderline,out.lfStrikeOut,out.lfQuality,out.lfOutPrecision,out.lfClipPrecision,out.lfPitchAndFamily,out.lfEscapement,out.lfOrientation);
    proto("readback.odd_fields",n,0,d); if(f) DeleteObject(f); }
  { HFONT g=CreateFontW(-12,0,0,0,400,0x1ff,0x102,0x103,0x1cc,0x107,0x1ff,0x105,0x122,L"Arial"); LOGFONTW out; memset(&out,0,sizeof out); int n=GetObjectW(g,sizeof out,&out);
    snprintf(d,sizeof d,"it=%d ul=%d so=%d cs=%d op=%d cp=%d q=%d pf=%d",out.lfItalic,out.lfUnderline,out.lfStrikeOut,out.lfCharSet,out.lfOutPrecision,out.lfClipPrecision,out.lfQuality,out.lfPitchAndFamily); proto("CreateFontW.byte_truncation",n,0,d); if(g) DeleteObject(g); }
  { HFONT g=CreateFontW(-12,0,0,0,400,0,0,0,1,0,0,0,0,NULL); LOGFONTW out; memset(&out,0xcc,sizeof out); int n=GetObjectW(g,sizeof out,&out); snprintf(d,sizeof d,"face0=%x",out.lfFaceName[0]); proto("CreateFontW.nullface",n,0,d); if(g) DeleteObject(g); }
  /* 40-char face through CreateFontW (copies at most 32 WCHAR), and a 32-WCHAR unterminated lfFaceName */
  { const WCHAR *lng=L"Segoe UIABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; HFONT g=CreateFontW(-12,0,0,0,400,0,0,0,1,0,0,0,0,lng); LOGFONTW out; memset(&out,0,sizeof out); int n=GetObjectW(g,sizeof out,&out);
    WCHAR fn[LF_FACESIZE+1]; memcpy(fn,out.lfFaceName,sizeof out.lfFaceName); fn[LF_FACESIZE]=0; char u[200]; w2u(u,sizeof u,fn); snprintf(d,sizeof d,"len=%d face=%s",(int)wcslen(fn),u); proto("CreateFontW.face40",n,0,d);
    HFONT o2=(HFONT)SelectObject(g_scr,g); WCHAR got[80]=L""; int gn=GetTextFaceW(g_scr,80,got); w2u(u,sizeof u,got); proto("CreateFontW.face40.textface",gn,0,u); SelectObject(g_scr,o2); if(g) DeleteObject(g); }
}

/* ------------------------------------------------------------------ the answer: MS Shell Dlg family at -8..-13 */
static void do_answer(void){
  /* key: requested face, req_h (lfHeight; <0 = em height px), req_wt (400 / 700: bold dialog text), gm, dc. DEFAULT_CHARSET, other fields 0. */
  static const char *AF[]={"MS Shell Dlg","MS Shell Dlg 2","MS Sans Serif","Microsoft Sans Serif","Tahoma","Arial","Segoe UI","Courier","Courier New"};
  rec_schema("answer","face req_h req_wt gm dc","got otm_family file type h:px a:px d:px il:px el:px ave:px max:px ext52:px cw_sha font " RZ_COLS " cs pf");
  for(int fi=0;fi<N(AF);fi++) for(int wi=0;wi<2;wi++) for(int h=-8;h>=-13;h--) for(int gm=1;gm<=2;gm++){
    LOGFONTW lf=lf_make(AF[fi],h,DEFAULT_CHARSET); lf.lfWeight=wi?FW_BOLD:FW_NORMAL; int og=SetGraphicsMode(g_scr,gm);
    HFONT f=CreateFontIndirectW(&lf); HFONT o=f?(HFONT)SelectObject(g_scr,f):NULL;
    rk k; rk_reset(&k); rk_t(&k,"face",AF[fi]); rk_i(&k,"req_h",h); rk_i(&k,"req_wt",lf.lfWeight); rk_i(&k,"gm",gm); rk_t(&k,"dc","screen");
    rv v; rv_reset(&v);
    if(!o || o==(HFONT)HGDI_ERROR){ for(int i=0;i<14+7+2;i++) rv_na(&v); rec_row("answer",&k,&v); SetGraphicsMode(g_scr,og?og:GM_COMPATIBLE); if(f) DeleteObject(f); continue; }
    WCHAR got[64]=L""; int gn=GetTextFaceW(g_scr,64,got); TEXTMETRICW tm; memset(&tm,0,sizeof tm); int okt=GetTextMetricsW(g_scr,&tm);
    char id[17], file[260]; font_ident(g_scr,id,file); rzinfo rz; rz_get(g_scr,&rz);
    WCHAR fam[64]=L"!"; UINT on=GetOutlineTextMetricsW(g_scr,0,NULL); if(on>0&&on<1000000){ OUTLINETEXTMETRICW *ot=(OUTLINETEXTMETRICW*)calloc(1,on); if(ot&&GetOutlineTextMetricsW(g_scr,on,ot)&&(size_t)ot->otmpFamilyName<on){ wcsncpy(fam,(WCHAR*)((char*)ot+(size_t)ot->otmpFamilyName),63); fam[63]=0; } free(ot); }
    SIZE s={0,0}; int okx=GetTextExtentPoint32W(g_scr,EXT52,52,&s); INT w[95]; char cw[17]="!"; if(GetCharWidth32W(g_scr,0x20,0x7e,w)) sha16(cw,w,sizeof w);
    if(gn>0) rv_w(&v,got); else rv_na(&v); if(wcscmp(fam,L"!")) rv_w(&v,fam); else rv_na(&v); rv_t(&v,file); rv_t(&v,okt?ftype(tm.tmPitchAndFamily):"!");
    if(okt){ rv_i(&v,tm.tmHeight); rv_i(&v,tm.tmAscent); rv_i(&v,tm.tmDescent); rv_i(&v,tm.tmInternalLeading); rv_i(&v,tm.tmExternalLeading); rv_i(&v,tm.tmAveCharWidth); rv_i(&v,tm.tmMaxCharWidth); }
    else for(int i=0;i<7;i++) rv_na(&v);
    if(okx) rv_i(&v,s.cx); else rv_na(&v); rv_t(&v,cw); rv_t(&v,id); rv_rz(&v,&rz);
    if(okt){ rv_i(&v,tm.tmCharSet); rv_x(&v,tm.tmPitchAndFamily); } else { rv_na(&v); rv_na(&v); }
    rec_row("answer",&k,&v);
    SelectObject(g_scr,o); SetGraphicsMode(g_scr,og?og:GM_COMPATIBLE);
    /* render "Shell Dlg 0123" in this font (GM as requested), 1bpp, non-antialiased; regular weight of the four Shell/Sans faces */
    if(fi<4 && !wi){ LOGFONTW lq=lf; lq.lfQuality=NONANTIALIASED_QUALITY; rec_dib d; if(rec_dib_create(&d,96,18)){ SetGraphicsMode(d.dc,gm); HFONT f2=CreateFontIndirectW(&lq); HFONT o2=(HFONT)SelectObject(d.dc,f2);
        SetBkMode(d.dc,TRANSPARENT); SetTextColor(d.dc,RGB(0,0,0)); TextOutW(d.dc,1,1,L"Shell Dlg 0123",14); GdiFlush(); uint8_t g[12*18]; size_t gn2=rec_gray_from_dib(d.bits,96,18,1,g,sizeof g);
        rec_bitmap("answer_bmp",&k,96,18,1,g,gn2); SelectObject(d.dc,o2); DeleteObject(f2); rec_dib_free(&d); } }
    DeleteObject(f); }
  /* micross.ttf presence (directory listing) */
  const ffile *m=ff_find("micross.ttf"); if(m){ char hx[65]="!"; if(m->hashed) rec_hex(hx,m->sha,32); rec_note("answer micross.ttf present size=%llu sha256=%s",m->size,hx); } else rec_note("answer micross.ttf absent from the %%WINDIR%%\\Fonts listing");
}

/* ------------------------------------------------------------------ font resource info (file -> faces) */
static void do_fontres(void){
  if(!pGetFontResourceInfoW){ rec_note("GetFontResourceInfoW not exported"); return; }
  rec_schema("fontres","file","ok_desc desc ok_lf n_lf lf0_face lf0_h lf0_cs lf0_pf");
  char dir[MAX_PATH]; GetWindowsDirectoryA(dir,sizeof dir); strncat(dir,"\\Fonts",sizeof dir-strlen(dir)-1);
  for(int i=0;i<g_nff;i++){ const char *e=strrchr(g_ff[i].name,'.'); if(!e) continue; int fon=!_stricmp(e,".fon"); if(!g_full && !fon && _stricmp(g_ff[i].name,"micross.ttf")) continue;
    if(!fon && _stricmp(e,".ttf") && _stricmp(e,".ttc") && _stricmp(e,".otf")) continue;
    char p[MAX_PATH*2]; snprintf(p,sizeof p,"%s\\%s",dir,g_ff[i].name); WCHAR wp[MAX_PATH*2]; u2w(wp,MAX_PATH*2,p);
    static BYTE buf[65536]; DWORD sz=sizeof buf; memset(buf,0,sizeof buf); BOOL ok1=pGetFontResourceInfoW(wp,&sz,buf,1);
    char desc[2000]=""; if(ok1){ WCHAR *w=(WCHAR*)buf; w[sizeof buf/2-1]=0; w2u(desc,sizeof desc,w); }
    DWORD sz2=sizeof buf; memset(buf,0,sizeof buf); BOOL ok2=pGetFontResourceInfoW(wp,&sz2,buf,2);
    rk k; rk_reset(&k); rk_t(&k,"file",g_ff[i].name); rv v; rv_reset(&v); rv_i(&v,ok1); rv_t(&v,desc[0]?desc:"~"); rv_i(&v,ok2);
    if(ok2 && sz2>=sizeof(LOGFONTW)){ LOGFONTW *l=(LOGFONTW*)buf; rv_i(&v,(int)(sz2/sizeof(LOGFONTW))); rv_w(&v,l->lfFaceName); rv_i(&v,l->lfHeight); rv_i(&v,l->lfCharSet); rv_x(&v,l->lfPitchAndFamily); }
    else for(int j=0;j<5;j++) rv_na(&v);
    rec_row("fontres",&k,&v); }
}

/* ------------------------------------------------------------------ main matrix */
static void do_map(void){
  /* A: base config, every face, three APIs (A only for ASCII names) */
  sset seen={0};
  for(int i=0;i<N(CORE)+N(EXTRA);i++){ const char *f=i<N(CORE)?CORE[i]:EXTRA[i-N(CORE)]; if(!g_full && i>=N(CORE)) break; if(!sset_add(&seen,f)) continue;
    LOGFONTW lf=lf_make(f,-11,DEFAULT_CHARSET); map_simple("base","W",&lf,"screen",GM_COMPATIBLE);
    lf.lfHeight=-12; map_simple("base","W",&lf,"screen",GM_COMPATIBLE);
    if(g_full || i<N(CORE)){ lf.lfHeight=-11; if(is_ascii(f)) map_simple("base","A",&lf,"screen",GM_COMPATIBLE); map_simple("base","ExW",&lf,"screen",GM_COMPATIBLE); map_simple("base","CreateFontW",&lf,"screen",GM_COMPATIBLE); } }
  sset_free(&seen); rec_flush();
  /* 32 WCHAR unterminated lfFaceName */
  { req r; r.src="edge"; r.api="W"; r.dcn="screen"; r.gm=GM_COMPATIBLE; r.lf=lf_make("",-11,1); const WCHAR *s=L"Segoe UIABCDEFGHIJKLMNOPQRSTUVWX"; memcpy(r.lf.lfFaceName,s,32*sizeof(WCHAR)); map_req(&r,"Segoe UIABCDEFGHIJKLMNOPQRSTUVWX[32,noNUL]"); }
  /* FontSubstitutes entries (dynamic): the value name as face; ",cs" parsed into lfCharSet; both with DEFAULT and with the entry's charset */
  { HKEY k; if(!RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",0,KEY_READ|KEY_WOW64_64KEY,&k)){
      for(DWORD i=0;;i++){ WCHAR nm[300]; DWORD nl=300; if(RegEnumValueW(k,i,nm,&nl,NULL,NULL,NULL,NULL)) break; WCHAR face[300]; wcscpy(face,nm); int cs=-1; WCHAR *c=wcschr(face,L','); if(c){ *c=0; cs=_wtoi(c+1); }
        LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=-11; lf.lfWeight=400; lf.lfCharSet=DEFAULT_CHARSET; wcsncpy(lf.lfFaceName,face,LF_FACESIZE-1);
        map_simple("subst","W",&lf,"screen",GM_COMPATIBLE); if(cs>=0 && cs<256){ lf.lfCharSet=(BYTE)cs; map_simple("subst","W",&lf,"screen",GM_COMPATIBLE); } }
      RegCloseKey(k); } }
  rec_flush();
  /* every enumerated family (full) */
  if(g_full){ char **fam=(char**)calloc(g_fams.n+1,sizeof(char*)); int nf=0; for(int i=0;i<g_fams.cap;i++) if(g_fams.v[i]) fam[nf++]=g_fams.v[i];
    for(int i=1;i<nf;i++){ char *x=fam[i]; int j=i-1; while(j>=0 && strcmp(fam[j],x)>0){ fam[j+1]=fam[j]; j--; } fam[j+1]=x; }
    for(int i=0;i<nf;i++){ LOGFONTW lf=lf_make(fam[i],-11,DEFAULT_CHARSET); map_simple("family","W",&lf,"screen",GM_COMPATIBLE); }
    free(fam); rec_flush(); }
  /* B: heights x graphics mode, memory DC */
  static const char *HF[]={"MS Shell Dlg","MS Shell Dlg 2","MS Sans Serif","Microsoft Sans Serif","Tahoma","Segoe UI","Arial","Helv","MS Serif","Courier","Courier New","System","Fixedsys","Terminal","Small Fonts"};
  static const int HQ[]={-8,-9,-10,-11,-12,-13,0,13,16};
  int hl[80], nh=0; if(g_full){ for(int h=-1;h>=-24;h--) hl[nh++]=h; hl[nh++]=-26; hl[nh++]=-28; hl[nh++]=-32; hl[nh++]=-36; hl[nh++]=-48; hl[nh++]=-72; hl[nh++]=0; for(int h=1;h<=24;h++) hl[nh++]=h; hl[nh++]=26; hl[nh++]=28; hl[nh++]=32; hl[nh++]=36; hl[nh++]=48; }
  else for(int i=0;i<N(HQ);i++) hl[nh++]=HQ[i];
  for(int fi=0;fi<N(HF);fi++){ for(int hi=0;hi<nh;hi++) for(int gm=1;gm<=(g_full?2:1);gm++){ LOGFONTW lf=lf_make(HF[fi],hl[hi],DEFAULT_CHARSET); map_simple("height","W",&lf,"mem",gm); } rec_flush(); }
  /* C: charsets */
  for(int fi=0;fi<N(CORE);fi++) for(int ci=0;ci<(g_full?N(CS):N(CS_QUICK));ci++){ LOGFONTW lf=lf_make(CORE[fi],-11,g_full?CS[ci]:CS_QUICK[ci]); map_simple("charset","W",&lf,"screen",GM_COMPATIBLE); }
  rec_flush();
  const char **SF=g_full?CORE:SMALL; int nsf=g_full?N(CORE):N(SMALL);
  /* D: pitch x family */
  static const int PITCH[]={DEFAULT_PITCH,FIXED_PITCH,VARIABLE_PITCH,MONO_FONT};
  for(int fi=0;fi<nsf;fi++) for(int pi=0;pi<N(PITCH);pi++) for(int fam=0;fam<=0x50;fam+=0x10){ int p=PITCH[pi]; if(!g_full && fam && p) continue; LOGFONTW lf=lf_make(SF[fi],-11,DEFAULT_CHARSET); lf.lfPitchAndFamily=(BYTE)(p|fam); map_simple("pitchfamily","W",&lf,"screen",GM_COMPATIBLE); }
  rec_flush();
  /* E: out precision 0..10 */
  for(int fi=0;fi<nsf;fi++) for(int op=0;op<=10;op++){ LOGFONTW lf=lf_make(SF[fi],-11,DEFAULT_CHARSET); lf.lfOutPrecision=(BYTE)op; map_simple("outprec","W",&lf,"screen",GM_COMPATIBLE); }
  rec_flush();
  /* F..J on SMALL */
  static const int CP[]={0,1,2,4,0x10,0x20,0x40,0x80,0xff};
  for(int fi=0;fi<N(SMALL);fi++) for(int i=0;i<N(CP);i++){ LOGFONTW lf=lf_make(SMALL[fi],-11,DEFAULT_CHARSET); lf.lfClipPrecision=(BYTE)CP[i]; map_simple("clipprec","W",&lf,"screen",GM_COMPATIBLE); }
  for(int fi=0;fi<N(SMALL);fi++) for(int q=0;q<=6;q++){ LOGFONTW lf=lf_make(SMALL[fi],-11,DEFAULT_CHARSET); lf.lfQuality=(BYTE)q; map_simple("quality","W",&lf,"screen",GM_COMPATIBLE); }
  { static const char *OF[]={"Segoe UI","MS Shell Dlg","MS Sans Serif","Tahoma","Arial","System","Courier","@SimSun","@MS Gothic","@Arial","@Segoe UI","SimSun"};
    static const int EO[][2]={{0,0},{900,900},{900,0},{0,900},{1800,1800},{2700,2700},{450,450},{3600,3600}};
    for(int fi=0;fi<N(OF);fi++) for(int i=0;i<(g_full?N(EO):3);i++) for(int gm=1;gm<=2;gm++){ LOGFONTW lf=lf_make(OF[fi],-12,DEFAULT_CHARSET); lf.lfEscapement=EO[i][0]; lf.lfOrientation=EO[i][1]; map_simple("orient","W",&lf,"mem",gm); } }
  rec_flush();
  { static const int WT[]={0,1,100,200,300,350,400,500,550,600,700,800,900,1000,1001};
    for(int fi=0;fi<N(SMALL);fi++) for(int i=0;i<(g_full?N(WT):4);i++) for(int it=0;it<2;it++){ LOGFONTW lf=lf_make(SMALL[fi],-11,DEFAULT_CHARSET); lf.lfWeight=WT[i]; lf.lfItalic=(BYTE)it; map_simple("weight","W",&lf,"screen",GM_COMPATIBLE); } }
  { static const int WD[]={0,1,3,4,5,6,7,8,10,16,-6};
    for(int fi=0;fi<N(SMALL);fi++) for(int i=0;i<(g_full?N(WD):3);i++){ LOGFONTW lf=lf_make(SMALL[fi],-11,DEFAULT_CHARSET); lf.lfWidth=WD[i]; map_simple("width","W",&lf,"screen",GM_COMPATIBLE); } }
  /* charset x pitch for the empty face and NoSuchFace: the FontMapper signature path (02 section 2.2) */
  if(g_full){ static const char *EF[]={"","NoSuchFace"}; static const int CSS[]={0,1,2,128,129,134,136,161,177,178,204,222,238,255};
    for(int fi=0;fi<2;fi++) for(int ci=0;ci<N(CSS);ci++) for(int p=0;p<3;p++) for(int fam=0;fam<=0x50;fam+=0x10){ LOGFONTW lf=lf_make(EF[fi],-11,CSS[ci]); lf.lfPitchAndFamily=(BYTE)(p|fam); map_simple("signature","W",&lf,"screen",GM_COMPATIBLE); } }
  rec_flush();
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"face_substitution","2");
  for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"--quick")) g_full=0; else if(!strcmp(argv[i],"--full")) g_full=1; else if(!strcmp(argv[i],"--no-filehash")) g_filehash=0; }
  rec_env("fs.mode","%s",g_full?"full":"quick");
  HMODULE gdi=GetModuleHandleA("gdi32.dll");
  pGetTextFaceAliasW=(pfnFaceAlias)(void*)GetProcAddress(gdi,"GetTextFaceAliasW");
  pGetFontResourceInfoW=(pfnFontResInfo)(void*)GetProcAddress(gdi,"GetFontResourceInfoW");
  pGetFontRealizationInfo=(pfnRzInfo)(void*)GetProcAddress(gdi,"GetFontRealizationInfo");
  pGetFontFileInfo=(pfnFontFileInfo)(void*)GetProcAddress(gdi,"GetFontFileInfo");
  rec_env("fs.have_GetTextFaceAliasW","%d",pGetTextFaceAliasW!=NULL); rec_env("fs.have_GetFontResourceInfoW","%d",pGetFontResourceInfoW!=NULL);
  rec_env("fs.have_GetFontRealizationInfo","%d",pGetFontRealizationInfo!=NULL); rec_env("fs.have_GetFontFileInfo","%d",pGetFontFileInfo!=NULL);
  g_scr=GetDC(NULL); g_mem=CreateCompatibleDC(g_scr);
  DWORD t0=GetTickCount(), t;
  do_registry(); rec_flush(); t=GetTickCount(); rec_env("fs.ms.registry","%lu",t-t0); t0=t;
  { char dir[MAX_PATH]; GetWindowsDirectoryA(dir,sizeof dir); strncat(dir,"\\Fonts",sizeof dir-strlen(dir)-1); list_dir("windir",dir,1);
    char la[MAX_PATH]; DWORD n=GetEnvironmentVariableA("LOCALAPPDATA",la,sizeof la); if(n&&n<sizeof la-40){ strcat(la,"\\Microsoft\\Windows\\Fonts"); list_dir("user",la,0); } }
  rec_flush(); t=GetTickCount(); rec_env("fs.ms.fontfiles","%lu",t-t0); t0=t;
  do_enum(); rec_flush(); t=GetTickCount(); rec_env("fs.ms.enum","%lu",t-t0); t0=t;
  do_answer(); rec_flush(); t=GetTickCount(); rec_env("fs.ms.answer","%lu",t-t0); t0=t;
  do_map(); t=GetTickCount(); rec_env("fs.ms.map","%lu",t-t0); t0=t;
  do_stock(); do_fallback(); rec_flush(); t=GetTickCount(); rec_env("fs.ms.stock_fallback","%lu",t-t0); t0=t;
  do_proto(); do_fontres(); t=GetTickCount(); rec_env("fs.ms.proto_fontres","%lu",t-t0);
  rec_note("face_substitution done fonts_identified=%d keys=%d",g_nfid,g_keys.n);
  DeleteDC(g_mem); ReleaseDC(NULL,g_scr); sset_free(&g_keys); sset_free(&g_fams);
  return rec_finish(0);
}
