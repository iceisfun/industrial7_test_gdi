/* font_inventory: static inventory of the Windows font installation (no rendering).
 *
 * Records, for %WINDIR%\Fonts:
 *   dir       every file: size, full SHA-256, kind (by magic), face count, VS_FIXEDFILEINFO versions
 *   face      per sfnt face: identity (name IDs 1/2/4/6/5/16/17), upem, glyph count, presence flags of the
 *             tables GDI cares about (hdmx VDMX LTSH EBLC gasp kern ...), EBLC strike list, gasp ranges
 *   name      name IDs 0..6,16,17,21,22 (en-US Windows record preferred) + `name_loc` every non-0x409
 *             Windows-platform family/full name (localized names GDI also matches on)
 *   head hhea os2 post maxp   the design metrics that feed TEXTMETRIC/OUTLINETEXTMETRIC
 *   cmap      chosen subtable, mapped-codepoint count per key Unicode block; H lines of the coverage
 *             bitset and of the codepoint->glyph map
 *   eblc gasp vdmx vdmx_ratio hdmx ltsh kern   detail rows (vdmx = per-ppem yMax/yMin GDI uses for ascent/descent)
 *   tabdir    (--full) every table: stored checksum, length, SHA-256/16 of its bytes
 *   fon       every RT_FONT resource of every .fon (NE or PE): FNT header fields + face name + resource hash
 *   reg/regkey  Fonts, FontSubstitutes, FontLink\SystemLink, FontMapper, FontMapperFamilyFallback, GRE_Initialize,
 *             FontDPI, Font Drivers, FontAssoc, LanguagePack\SurrogateFallback, per-user Fonts, WindowMetrics
 *   keyfile   existence/size/sha256/version of the files the D12 question names (micross, tahoma, segoeui, ...)
 *   gdi_file  which file GDI actually maps a requested face to (GetFontData of the selected font, hashed,
 *             matched against the dir hashes)
 *   summary   totals
 *
 * usage: font_inventory.exe [recorder options] [--quick|--full]
 *   --full  (default) hash and parse every file
 *   --quick only the key files are hashed/parsed (others listed with size only); no tabdir rows
 *   --dir D parse directory D instead of %WINDIR%\Fonts (parser plumbing tests under Wine only; noted in an N line)
 * Units: :B bytes, :fu font design units (see upem in `face`), :px pixels (ppem / EBLC / VDMX / hdmx values).
 * Every multi-byte font field is read big-endian per the OpenType spec; FNT (.fon) fields little-endian. */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PROBE_VER "1"
static int g_full = 1;

/* ------------------------------------------------------------------ bounded big-endian reads */
static const uint8_t *G_P; static size_t G_N; static int g_oob;
static unsigned U8(size_t o){ if(o+1>G_N){ g_oob=1; return 0; } return G_P[o]; }
static unsigned U16(size_t o){ if(o+2>G_N||o+2<o){ g_oob=1; return 0; } return (G_P[o]<<8)|G_P[o+1]; }
static int S16(size_t o){ return (int)(int16_t)U16(o); }
static uint32_t U32(size_t o){ if(o+4>G_N||o+4<o){ g_oob=1; return 0; } return ((uint32_t)G_P[o]<<24)|((uint32_t)G_P[o+1]<<16)|((uint32_t)G_P[o+2]<<8)|G_P[o+3]; }
static int S8(size_t o){ return (int)(int8_t)U8(o); }
static unsigned L16(size_t o){ if(o+2>G_N||o+2<o){ g_oob=1; return 0; } return G_P[o]|(G_P[o+1]<<8); }
static uint32_t L32(size_t o){ if(o+4>G_N||o+4<o){ g_oob=1; return 0; } return G_P[o]|(G_P[o+1]<<8)|((uint32_t)G_P[o+2]<<16)|((uint32_t)G_P[o+3]<<24); }

static void sha16(char out[17], const void *p, size_t n){ uint8_t h[32]; rec_sha256(p,n,h); rec_hex(out,h,8); out[16]=0; }
static void tagstr(char t[5], uint32_t tag){ for(int i=0;i<4;i++){ unsigned c=(tag>>(24-8*i))&0xff; t[i]=(c>=0x21&&c<0x7f)?(char)c:'_'; } t[4]=0; }

/* ------------------------------------------------------------------ file list */
typedef struct {
  WCHAR wname[MAX_PATH]; char name[MAX_PATH*3]; /* lowercase UTF-8 */
  unsigned long long size; int isdir; int key; int hashed; uint8_t sha[32];
} fent;
static fent *F; static int NF;

static const char *KEYFILES[] = {
  "micross.ttf","tahoma.ttf","tahomabd.ttf","segoeui.ttf","segoeuib.ttf","segoeuii.ttf","segoeuiz.ttf","segoeuil.ttf",
  "segoeuisl.ttf","seguisb.ttf","seguisym.ttf","seguiemj.ttf","arial.ttf","arialbd.ttf","ariali.ttf","arialbi.ttf",
  "cour.ttf","courbd.ttf","couri.ttf","courbi.ttf","times.ttf","timesbd.ttf","verdana.ttf","lucon.ttf","consola.ttf",
  "marlett.ttf","symbol.ttf","wingding.ttf","webdings.ttf","msgothic.ttc","msyh.ttc","msjh.ttc","simsun.ttc","mingliub.ttc",
  "gulim.ttc","malgun.ttf","meiryo.ttc","mangal.ttf","yugothm.ttc","calibri.ttf",
  "vgasys.fon","vgaoem.fon","vgafix.fon","vga850.fon","vgas1255.fon","8514sys.fon","8514fix.fon","8514oem.fon",
  "sserife.fon","sseriff.fon","serife.fon","seriff.fon","coure.fon","couref.fon","smalle.fon","smallf.fon",
  "modern.fon","roman.fon","script.fon","app850.fon","dosapp.fon","cga40woa.fon","cga80woa.fon","ega40woa.fon","ega80woa.fon",
  "ssee0874.fon","marlett.ttf", NULL };
static char g_extra_key[64][MAX_PATH]; static int g_nextra; /* GRE_Initialize / SystemLink files */
static int is_key(const char *lname){
  for(int i=0;KEYFILES[i];i++) if(!strcmp(KEYFILES[i],lname)) return 1;
  for(int i=0;i<g_nextra;i++) if(!strcmp(g_extra_key[i],lname)) return 1;
  return 0;
}
static void add_extra_key(const WCHAR *w){
  char u[MAX_PATH*3]; WideCharToMultiByte(CP_UTF8,0,w,-1,u,sizeof u,NULL,NULL); u[sizeof u-1]=0;
  const char *b=strrchr(u,'\\'); b=b?b+1:u; char l[MAX_PATH]; snprintf(l,sizeof l,"%s",b);
  for(char *p=l;*p;p++) if(*p>='A'&&*p<='Z') *p+=32;
  const char *dot=strrchr(l,'.');
  if(!dot || (strcmp(dot,".fon")&&strcmp(dot,".ttf")&&strcmp(dot,".ttc")&&strcmp(dot,".otf")&&strcmp(dot,".fnt"))) return;
  if(!*l || is_key(l) || g_nextra>=64) return;
  snprintf(g_extra_key[g_nextra++],MAX_PATH,"%s",l);
}
static int fcmp(const void *a, const void *b){ return strcmp(((const fent*)a)->name,((const fent*)b)->name); }
static WCHAR g_fontdir[MAX_PATH];

static const char *g_dir_override;
static void list_dir(void){
  WCHAR pat[MAX_PATH];
  if(g_dir_override){ MultiByteToWideChar(CP_UTF8,0,g_dir_override,-1,g_fontdir,MAX_PATH); g_fontdir[MAX_PATH-1]=0; rec_note("font directory override (plumbing test, not the Windows inventory): %s",g_dir_override); }
  else { GetWindowsDirectoryW(g_fontdir,MAX_PATH); wcscat(g_fontdir,L"\\Fonts"); }
  swprintf(pat,MAX_PATH,L"%ls\\*",g_fontdir);
  WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(pat,&fd); int cap=0;
  if(h==INVALID_HANDLE_VALUE){ rec_note("FindFirstFile %%WINDIR%%\\Fonts failed gle=%lu",GetLastError()); return; }
  do{
    if(!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
    if(NF==cap){ cap=cap?cap*2:512; F=(fent*)realloc(F,cap*sizeof *F); }
    fent *e=&F[NF++]; memset(e,0,sizeof *e);
    wcsncpy(e->wname,fd.cFileName,MAX_PATH-1);
    WCHAR lw[MAX_PATH]; wcsncpy(lw,fd.cFileName,MAX_PATH-1); lw[MAX_PATH-1]=0;
    for(WCHAR *p=lw;*p;p++) if(*p>='A'&&*p<='Z') *p+=32;
    WideCharToMultiByte(CP_UTF8,0,lw,-1,e->name,sizeof e->name,NULL,NULL);
    e->size=((unsigned long long)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;
    e->isdir=!!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
  }while(FindNextFileW(h,&fd));
  FindClose(h);
  qsort(F,NF,sizeof *F,fcmp);
}

static uint8_t *read_file(const WCHAR *path, size_t *n){
  HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);
  if(h==INVALID_HANDLE_VALUE) return NULL;
  LARGE_INTEGER sz; if(!GetFileSizeEx(h,&sz)||sz.QuadPart>(1LL<<30)){ CloseHandle(h); return NULL; }
  uint8_t *b=(uint8_t*)malloc((size_t)sz.QuadPart+1); size_t o=0;
  while(b && o<(size_t)sz.QuadPart){ DWORD r=0; DWORD want=(DWORD)(((size_t)sz.QuadPart-o)>(1u<<24)?(1u<<24):((size_t)sz.QuadPart-o));
    if(!ReadFile(h,b+o,want,&r,NULL)||!r) break;
    o+=r; }
  CloseHandle(h);
  if(!b || o!=(size_t)sz.QuadPart){ free(b); return NULL; }
  *n=o; return b;
}

/* ------------------------------------------------------------------ version resource */
static void rv_version(rv *v, const WCHAR *path){
  DWORD dummy=0, n=GetFileVersionInfoSizeW(path,&dummy);
  if(!n){ rv_na(v); rv_na(v); return; }
  void *b=malloc(n); VS_FIXEDFILEINFO *fi=NULL; UINT fl=0;
  if(b && GetFileVersionInfoW(path,0,n,b) && VerQueryValueW(b,L"\\",(void**)&fi,&fl) && fi && fl>=sizeof *fi){
    char t[64];
    snprintf(t,sizeof t,"%u.%u.%u.%u",HIWORD(fi->dwFileVersionMS),LOWORD(fi->dwFileVersionMS),HIWORD(fi->dwFileVersionLS),LOWORD(fi->dwFileVersionLS)); rv_s(v,t);
    snprintf(t,sizeof t,"%u.%u.%u.%u",HIWORD(fi->dwProductVersionMS),LOWORD(fi->dwProductVersionMS),HIWORD(fi->dwProductVersionLS),LOWORD(fi->dwProductVersionLS)); rv_s(v,t);
  } else { rv_na(v); rv_na(v); }
  free(b);
}

/* ------------------------------------------------------------------ sfnt helpers */
static int find_table(size_t fo, uint32_t tag, size_t *off, size_t *len){
  unsigned nt=U16(fo+4);
  for(unsigned i=0;i<nt;i++){ size_t r=fo+12+16*(size_t)i; if(U32(r)==tag){ size_t o=U32(r+8), l=U32(r+12);
      if(o>G_N || l>G_N-o){ return 0; } *off=o; *len=l; return 1; } }
  return 0;
}
#define TAG(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))

/* decode a name record into UTF-16 (platform 0/3: UTF-16BE; 1/0 MacRoman as Latin-1 approximation; else hex) */
static void name_decode(WCHAR *out, int cap, unsigned plat, unsigned enc, size_t o, unsigned len){
  int n=0;
  if(plat==0||plat==3){ for(unsigned i=0;i+1<len && n<cap-1;i+=2) out[n++]=(WCHAR)U16(o+i); }
  else if(plat==1 && enc==0){ for(unsigned i=0;i<len && n<cap-1;i++) out[n++]=(WCHAR)U8(o+i); }
  else { static const char hx[]="0123456789abcdef"; for(unsigned i=0;i<len && n<cap-3;i++){ unsigned c=U8(o+i); out[n++]=hx[c>>4]; out[n++]=hx[c&15]; } }
  out[n]=0;
}

/* pick the best record for nameID: (3,*,0x409) > (3,*,any) > (0,*,*) > (1,0,0). Returns record offset or 0. */
static size_t name_pick(size_t no, unsigned id){
  unsigned cnt=U16(no+2); size_t best=0; int bs=0;
  for(unsigned i=0;i<cnt;i++){ size_t r=no+6+12*(size_t)i; if(U16(r+6)!=id) continue;
    unsigned p=U16(r), lang=U16(r+4); int s=0;
    if(p==3 && lang==0x409) s=4; else if(p==3) s=3; else if(p==0) s=2; else if(p==1 && U16(r+2)==0 && lang==0) s=1;
    if(s>bs){ bs=s; best=r; } }
  return best;
}
static void rv_name(rv *v, size_t no, size_t nlen, unsigned id, char *plain, int pcap){
  if(plain&&pcap) plain[0]=0;
  if(!no){ rv_na(v); return; }
  size_t r=name_pick(no,id); if(!r){ rv_na(v); return; }
  size_t so=no+U16(no+4); WCHAR w[512]; name_decode(w,512,U16(r),U16(r+2),so+U16(r+10),U16(r+8));
  rv_w(v,w);
  if(plain&&pcap) WideCharToMultiByte(CP_UTF8,0,w,-1,plain,pcap,NULL,NULL);
}

/* ------------------------------------------------------------------ cmap coverage */
static uint8_t g_cov[0x110000/8]; static uint16_t *g_gid; /* 0x110000 */
static const struct { const char *n; uint32_t a,b; } BLK[] = {
  {"c0_ctl",0x00,0x1f},{"basic_latin",0x20,0x7e},{"latin1",0xa0,0xff},{"latin_ext_a",0x100,0x17f},{"latin_ext_b",0x180,0x24f},
  {"ipa",0x250,0x2af},{"spacing_mod",0x2b0,0x2ff},{"comb_diac",0x300,0x36f},{"greek",0x370,0x3ff},{"cyrillic",0x400,0x4ff},
  {"armenian",0x530,0x58f},{"hebrew",0x590,0x5ff},{"arabic",0x600,0x6ff},{"devanagari",0x900,0x97f},{"thai",0xe00,0xe7f},
  {"georgian",0x10a0,0x10ff},{"hangul_jamo",0x1100,0x11ff},{"latin_ext_add",0x1e00,0x1eff},{"greek_ext",0x1f00,0x1fff},
  {"gen_punct",0x2000,0x206f},{"currency",0x20a0,0x20cf},{"letterlike",0x2100,0x214f},{"number_forms",0x2150,0x218f},
  {"arrows",0x2190,0x21ff},{"math_ops",0x2200,0x22ff},{"misc_tech",0x2300,0x23ff},{"box_drawing",0x2500,0x257f},
  {"block_elem",0x2580,0x259f},{"geom_shapes",0x25a0,0x25ff},{"misc_symbols",0x2600,0x26ff},{"dingbats",0x2700,0x27bf},
  {"cjk_sym_punct",0x3000,0x303f},{"hiragana",0x3040,0x309f},{"katakana",0x30a0,0x30ff},{"bopomofo",0x3100,0x312f},
  {"cjk_unified",0x4e00,0x9fff},{"hangul_syll",0xac00,0xd7af},{"pua",0xe000,0xf8ff},{"pua_symbol_f0",0xf000,0xf0ff},
  {"cjk_compat_ideo",0xf900,0xfaff},{"alpha_pres",0xfb00,0xfb4f},{"arabic_pres_a",0xfb50,0xfdff},{"arabic_pres_b",0xfe70,0xfeff},
  {"half_full",0xff00,0xffef},{"specials",0xfff0,0xffff},{"smp",0x10000,0x1ffff},{"sip",0x20000,0x2ffff},{"supp_pua",0xf0000,0x10ffff},
};
#define NBLK (int)(sizeof BLK/sizeof*BLK)
static void cov_set(uint32_t c, unsigned gid){ if(c<0x110000 && gid){ g_cov[c>>3]|=(uint8_t)(1u<<(c&7)); g_gid[c]=(uint16_t)gid; } }

static int cmap_fill(size_t so, size_t slen, unsigned fmt){
  size_t end=so+slen; if(end>G_N) end=G_N;
  if(fmt==0){ for(unsigned c=0;c<256;c++) cov_set(c,U8(so+6+c)); return 1; }
  if(fmt==6){ unsigned first=U16(so+6), cnt=U16(so+8); for(unsigned i=0;i<cnt;i++) cov_set(first+i,U16(so+10+2*i)); return 1; }
  if(fmt==4){ unsigned segx2=U16(so+6), seg=segx2/2; size_t eo=so+14, sto=eo+segx2+2, dlo=sto+segx2, ro=dlo+segx2;
    for(unsigned s=0;s<seg;s++){ unsigned e=U16(eo+2*s), st=U16(sto+2*s); int d=S16(dlo+2*s); unsigned r=U16(ro+2*s);
      if(st>e) continue;
      for(unsigned c=st;c<=e;c++){ if(c==0xffff) break; unsigned g;
        if(!r) g=(c+d)&0xffff; else { size_t go=ro+2*s+r+2*(c-st); if(go+2>end) { g=0; } else { g=U16(go); if(g) g=(g+d)&0xffff; } }
        cov_set(c,g); } }
    return 1; }
  if(fmt==12||fmt==13){ uint32_t n=U32(so+12); if((size_t)n*12>slen) n=(uint32_t)(slen/12);
    for(uint32_t i=0;i<n;i++){ size_t go=so+16+12*(size_t)i; uint32_t a=U32(go), b=U32(go+4), g=U32(go+8);
      if(b>=0x110000) b=0x10ffff;
      if(a>b) continue;
      for(uint32_t c=a;c<=b;c++) cov_set(c, fmt==12 ? (unsigned)(g+(c-a)) : (unsigned)g); }
    return 1; }
  return 0;
}

static void do_cmap(const char *fname, int fi, size_t fo, rk *kf){
  size_t co,cl; rv v; rv_reset(&v);
  if(!find_table(fo,TAG('c','m','a','p'),&co,&cl)){ rv_na(&v); for(int i=0;i<3+NBLK;i++) rv_na(&v); rec_row("cmap",kf,&v); return; }
  unsigned n=U16(co+2); char subs[1024]=""; int sl=0;
  size_t best=0; unsigned bfmt=0; int bscore=0; char btag[32]="!";
  for(unsigned i=0;i<n && i<64;i++){ size_t r=co+4+8*(size_t)i; unsigned p=U16(r), e=U16(r+2); size_t so=co+U32(r+4); unsigned f=U16(so);
    sl+=snprintf(subs+sl,sizeof subs-sl,"%s%u.%u.%u",sl?",":"",p,e,f); if(sl>=(int)sizeof subs) sl=sizeof subs-1;
    int s=0;
    if(p==3&&e==10&&f==12) s=9; else if(p==0&&(e==4||e==6)&&f==12) s=8; else if(p==3&&e==1&&f==4) s=7; else if(p==0&&e==3&&f==4) s=6;
    else if(p==0&&f==4) s=5; else if(p==3&&e==0&&f==4) s=4; else if(p==1&&e==0&&(f==0||f==6||f==4)) s=2;
    if(s>bscore){ bscore=s; best=so; bfmt=f; snprintf(btag,sizeof btag,"%u.%u.%u",p,e,f); } }
  memset(g_cov,0,sizeof g_cov); memset(g_gid,0,0x110000*sizeof *g_gid);
  size_t blen=0;
  if(best){ if(bfmt==12||bfmt==13) blen=U32(best+4); else blen=U16(best+2); if(best+blen>G_N) blen=G_N-best; cmap_fill(best,blen,bfmt); }
  rv_s(&v,subs[0]?subs:"~"); rv_s(&v,btag);
  unsigned long tot=0, supp=0;
  for(uint32_t c=0;c<0x110000;c++) if(g_cov[c>>3]&(1u<<(c&7))){ tot++; if(c>=0x10000) supp++; }
  rv_u(&v,tot); rv_u(&v,supp);
  for(int b=0;b<NBLK;b++){ unsigned long k=0; for(uint32_t c=BLK[b].a;c<=BLK[b].b;c++) if(g_cov[c>>3]&(1u<<(c&7))) k++; rv_u(&v,k); }
  rec_row("cmap",kf,&v);
  if(best && tot){
    rec_hash("cmap_cov",kf,g_cov,sizeof g_cov);
    /* codepoint->glyph map, as (cp u32 LE, gid u16 LE) for mapped cps in ascending order */
    uint8_t *m=(uint8_t*)malloc((size_t)tot*6); size_t o=0;
    for(uint32_t c=0;c<0x110000;c++) if(g_cov[c>>3]&(1u<<(c&7))){ uint32_t g=g_gid[c]; memcpy(m+o,&c,4); m[o+4]=(uint8_t)g; m[o+5]=(uint8_t)(g>>8); o+=6; }
    rec_hash("cmap_map",kf,m,o); free(m);
  }
  (void)fname; (void)fi;
}

/* ------------------------------------------------------------------ one sfnt face */
static int g_nfaces;
static void do_face(const char *fname, int fi, size_t fo, int detail){
  rk kf; rk_reset(&kf); rk_s(&kf,"file",fname); rk_i(&kf,"face",fi);
  uint32_t sv=U32(fo); unsigned nt=U16(fo+4);
  size_t o,l, no=0,nlen=0;
  int has_name=find_table(fo,TAG('n','a','m','e'),&no,&nlen);
  if(!has_name) no=0;
  g_nfaces++;
  /* ---- face summary */
  static const uint32_t PT[]={TAG('h','d','m','x'),TAG('V','D','M','X'),TAG('L','T','S','H'),TAG('E','B','L','C'),TAG('E','B','D','T'),TAG('E','B','S','C'),
    TAG('g','a','s','p'),TAG('k','e','r','n'),TAG('G','P','O','S'),TAG('G','S','U','B'),TAG('G','D','E','F'),TAG('C','F','F',' '),TAG('g','l','y','f'),
    TAG('D','S','I','G'),TAG('v','h','e','a'),TAG('f','p','g','m'),TAG('p','r','e','p'),TAG('c','v','t',' '),TAG('C','B','L','C'),TAG('C','O','L','R'),
    TAG('S','V','G',' '),TAG('f','v','a','r'),TAG('m','o','r','x'),TAG('P','C','L','T'),TAG('B','A','S','E'),TAG('J','S','T','F'),TAG('M','A','T','H'),TAG('b','l','o','c')};
  char flags[64]; int nf=0; for(unsigned i=0;i<sizeof PT/sizeof*PT;i++) flags[nf++]=find_table(fo,PT[i],&o,&l)?'1':'0'; flags[nf]=0;
  rv v; rv_reset(&v);
  rv_x(&v,sv); rv_i(&v,nt);
  char fam[256]="", sub[256]="";
  rv_name(&v,no,nlen,1,fam,sizeof fam); rv_name(&v,no,nlen,2,sub,sizeof sub); rv_name(&v,no,nlen,4,NULL,0); rv_name(&v,no,nlen,6,NULL,0);
  unsigned upem=0, ng=0;
  if(find_table(fo,TAG('h','e','a','d'),&o,&l)) { upem=U16(o+18); rv_i(&v,upem); } else rv_na(&v);
  if(find_table(fo,TAG('m','a','x','p'),&o,&l)) { ng=U16(o+4); rv_i(&v,ng); } else rv_na(&v);
  rv_s(&v,flags);
  /* EBLC/bloc strike list "ppemXxppemYbBitDepth" */
  char sl[1024]=""; int sn=0; size_t eo,el;
  if(find_table(fo,TAG('E','B','L','C'),&eo,&el)||find_table(fo,TAG('b','l','o','c'),&eo,&el)){
    uint32_t ns=U32(eo+4); for(uint32_t i=0;i<ns && i<256;i++){ size_t b=eo+8+48*(size_t)i;
      sn+=snprintf(sl+sn,sizeof sl-sn,"%s%ux%ub%u",sn?",":"",U8(b+44),U8(b+45),U8(b+46)); if(sn>=(int)sizeof sl){ sn=sizeof sl-1; break; } } }
  rv_s(&v,sn?sl:"~");
  char gs[1024]=""; int gn=0; size_t go,gl;
  if(find_table(fo,TAG('g','a','s','p'),&go,&gl)){ unsigned nr=U16(go+2); for(unsigned i=0;i<nr && i<128;i++){
      gn+=snprintf(gs+gn,sizeof gs-gn,"%s%u:0x%x",gn?",":"",U16(go+4+4*i),U16(go+6+4*i)); if(gn>=(int)sizeof gs){ gn=sizeof gs-1; break; } } }
  rv_s(&v,gn?gs:"~");
  char tl[1024]=""; int tn=0; for(unsigned i=0;i<nt && i<128;i++){ char t[5]; tagstr(t,U32(fo+12+16*(size_t)i)); tn+=snprintf(tl+tn,sizeof tl-tn,"%s%s",tn?",":"",t); if(tn>=(int)sizeof tl){ tn=sizeof tl-1; break; } }
  rv_s(&v,tn?tl:"~");
  rec_row("face",&kf,&v);

  /* ---- name */
  if(no){
    static const unsigned IDS[]={0,1,2,3,4,5,6,16,17,21,22};
    for(unsigned i=0;i<sizeof IDS/sizeof*IDS;i++){ size_t r=name_pick(no,IDS[i]); if(!r) continue;
      rk k=kf; rk_i(&k,"id",IDS[i]); rv_reset(&v); WCHAR w[512]; size_t so=no+U16(no+4);
      name_decode(w,IDS[i]==0?160:512,U16(r),U16(r+2),so+U16(r+10),U16(r+8)); /* copyright truncated to 159 chars */
      rv_w(&v,w); rv_i(&v,U16(r)); rv_i(&v,U16(r+2)); rv_x(&v,U16(r+4)); rec_row("name",&k,&v); }
    /* every Windows-platform family / full name in a non-0x409 language (localized names) */
    unsigned cnt=U16(no+2); size_t so=no+U16(no+4);
    for(unsigned i=0;i<cnt;i++){ size_t r=no+6+12*(size_t)i; unsigned p=U16(r), id=U16(r+6), lang=U16(r+4);
      if(p!=3 || lang==0x409 || (id!=1&&id!=4&&id!=16)) continue;
      rk k=kf; rk_i(&k,"id",id); rk_i(&k,"enc",U16(r+2)); rk_x(&k,"lang",lang); rv_reset(&v); WCHAR w[512];
      name_decode(w,512,p,U16(r+2),so+U16(r+10),U16(r+8)); rv_w(&v,w); rec_row("name_loc",&k,&v); }
    rk k=kf; rv_reset(&v); rv_i(&v,U16(no)); rv_i(&v,cnt); rec_row("name_n",&k,&v);
  }
  /* ---- head */
  if(find_table(fo,TAG('h','e','a','d'),&o,&l)){ rv_reset(&v);
    rv_x(&v,U32(o)); rv_x(&v,U32(o+4)); rv_x(&v,U32(o+12)); rv_x(&v,U16(o+16)); rv_i(&v,U16(o+18));
    rv_i(&v,S16(o+36)); rv_i(&v,S16(o+38)); rv_i(&v,S16(o+40)); rv_i(&v,S16(o+42)); rv_x(&v,U16(o+44)); rv_i(&v,U16(o+46));
    rv_i(&v,S16(o+48)); rv_i(&v,S16(o+50)); rec_row("head",&kf,&v); }
  /* ---- hhea */
  if(find_table(fo,TAG('h','h','e','a'),&o,&l)){ rv_reset(&v);
    rv_i(&v,S16(o+4)); rv_i(&v,S16(o+6)); rv_i(&v,S16(o+8)); rv_i(&v,U16(o+10)); rv_i(&v,S16(o+12)); rv_i(&v,S16(o+14)); rv_i(&v,S16(o+16));
    rv_i(&v,S16(o+18)); rv_i(&v,S16(o+20)); rv_i(&v,S16(o+22)); rv_i(&v,U16(o+34)); rec_row("hhea",&kf,&v); }
  /* ---- OS/2 */
  if(find_table(fo,TAG('O','S','/','2'),&o,&l)){ rv_reset(&v); unsigned ver=U16(o);
    rv_i(&v,ver); rv_i(&v,S16(o+2)); rv_i(&v,U16(o+4)); rv_i(&v,U16(o+6)); rv_x(&v,U16(o+8)); rv_x(&v,U16(o+62));
    rv_i(&v,S16(o+68)); rv_i(&v,S16(o+70)); rv_i(&v,S16(o+72)); rv_i(&v,U16(o+74)); rv_i(&v,U16(o+76));
    char pan[21]; rec_hex(pan,G_P+(o+42<=G_N?o+32:0),(o+42<=G_N)?10:0); rv_s(&v,(o+42<=G_N)?pan:"!");
    char vend[5]; for(int i=0;i<4;i++){ unsigned c=U8(o+58+i); vend[i]=(c>=0x20&&c<0x7f)?(char)c:'?'; } vend[4]=0; rv_s(&v,vend);
    rv_x(&v,U32(o+42)); rv_x(&v,U32(o+46)); rv_x(&v,U32(o+50)); rv_x(&v,U32(o+54));
    if(ver>=1 && l>=86){ rv_x(&v,U32(o+78)); rv_x(&v,U32(o+82)); } else { rv_na(&v); rv_na(&v); }
    if(ver>=2 && l>=96){ rv_i(&v,S16(o+86)); rv_i(&v,S16(o+88)); rv_x(&v,U16(o+90)); rv_x(&v,U16(o+92)); rv_i(&v,U16(o+94)); }
    else { for(int i=0;i<5;i++) rv_na(&v); }
    rv_x(&v,U16(o+64)); rv_x(&v,U16(o+66)); rv_x(&v,(unsigned)S16(o+30)&0xffff);
    rv_i(&v,S16(o+26)); rv_i(&v,S16(o+28));
    rv_i(&v,S16(o+10)); rv_i(&v,S16(o+12)); rv_i(&v,S16(o+14)); rv_i(&v,S16(o+16));
    rv_i(&v,S16(o+18)); rv_i(&v,S16(o+20)); rv_i(&v,S16(o+22)); rv_i(&v,S16(o+24));
    rv_i(&v,(int)l);
    rec_row("os2",&kf,&v); }
  /* ---- post */
  if(find_table(fo,TAG('p','o','s','t'),&o,&l)){ rv_reset(&v);
    rv_x(&v,U32(o)); rv_x(&v,U32(o+4)); rv_i(&v,S16(o+8)); rv_i(&v,S16(o+10)); rv_i(&v,U32(o+12)); rec_row("post",&kf,&v); }
  /* ---- maxp */
  if(find_table(fo,TAG('m','a','x','p'),&o,&l)){ rv_reset(&v); uint32_t mv=U32(o);
    rv_x(&v,mv); rv_i(&v,U16(o+4));
    if(mv==0x00010000 && l>=32){ for(int i=6;i<=30;i+=2) rv_i(&v,U16(o+i)); } else for(int i=6;i<=30;i+=2) rv_na(&v);
    rec_row("maxp",&kf,&v); }
  /* ---- cmap */
  do_cmap(fname,fi,fo,&kf);
  /* ---- hmtx advance hash */
  { size_t ho,hl,mo,ml; if(find_table(fo,TAG('h','h','e','a'),&ho,&hl) && find_table(fo,TAG('h','m','t','x'),&mo,&ml)){
      unsigned nh=U16(ho+34); size_t need=(size_t)nh*4; if(need>ml) need=ml; rec_hash("hmtx",&kf,G_P+mo,need); } }
  /* ---- EBLC strikes */
  if(find_table(fo,TAG('E','B','L','C'),&eo,&el)||find_table(fo,TAG('b','l','o','c'),&eo,&el)){
    uint32_t ns=U32(eo+4); for(uint32_t i=0;i<ns && i<256;i++){ size_t b=eo+8+48*(size_t)i; rk k=kf; rk_i(&k,"strike",i); rv_reset(&v);
      rv_i(&v,U8(b+44)); rv_i(&v,U8(b+45)); rv_i(&v,U8(b+46)); rv_x(&v,U8(b+47)); rv_i(&v,U16(b+40)); rv_i(&v,U16(b+42)); rv_u(&v,U32(b+8));
      rv_i(&v,S8(b+16)); rv_i(&v,S8(b+17)); rv_i(&v,U8(b+18)); rv_i(&v,S8(b+28)); rv_i(&v,S8(b+29));
      rec_row("eblc",&k,&v); } }
  /* ---- gasp */
  if(find_table(fo,TAG('g','a','s','p'),&go,&gl)){ unsigned nr=U16(go+2); for(unsigned i=0;i<nr && i<256;i++){ rk k=kf; rk_i(&k,"i",i); rv_reset(&v);
      rv_i(&v,U16(go)); rv_i(&v,U16(go+4+4*i)); rv_x(&v,U16(go+6+4*i)); rec_row("gasp",&k,&v); } }
  /* ---- kern (format-0 pair counts) */
  if(find_table(fo,TAG('k','e','r','n'),&o,&l)){ unsigned ver=U16(o); rv_reset(&v);
    if(ver==0){ unsigned n=U16(o+2); size_t p=o+4; unsigned long pairs=0; char fm[128]=""; int fn=0;
      for(unsigned i=0;i<n && i<64 && p+6<=o+l;i++){ unsigned len=U16(p+2), cov=U16(p+4), f=cov>>8; if(f==0) pairs+=U16(p+6);
        fn+=snprintf(fm+fn,sizeof fm-fn,"%s%u/0x%x",fn?",":"",f,cov&0xff); if(fn>=(int)sizeof fm) fn=sizeof fm-1; if(!len) break; p+=len; }
      rv_i(&v,ver); rv_i(&v,n); rv_u(&v,pairs); rv_s(&v,fn?fm:"~"); }
    else { rv_i(&v,ver); rv_na(&v); rv_na(&v); rv_na(&v); }
    char h[17]; sha16(h,G_P+o,l); rv_s(&v,h); rec_row("kern",&kf,&v); }

  if(!detail) return;
  /* ---- VDMX */
  if(find_table(fo,TAG('V','D','M','X'),&o,&l)){
    unsigned ver=U16(o), nrec=U16(o+2), nrat=U16(o+4); size_t offs=o+6+4*(size_t)nrat;
    unsigned seen[64]; int ns=0;
    for(unsigned r=0;r<nrat && r<64;r++){ unsigned go2=U16(offs+2*r); int gi=-1; for(int j=0;j<ns;j++) if(seen[j]==go2){ gi=j; break; }
      if(gi<0 && ns<64){ seen[ns]=go2; gi=ns++; }
      rk k=kf; rk_i(&k,"r",r); rv_reset(&v); rv_i(&v,ver); rv_i(&v,U8(o+6+4*r)); rv_i(&v,U8(o+7+4*r)); rv_i(&v,U8(o+8+4*r)); rv_i(&v,U8(o+9+4*r)); rv_i(&v,gi);
      rec_row("vdmx_ratio",&k,&v); }
    for(int g=0;g<ns;g++){ size_t gp=o+seen[g]; unsigned recs=U16(gp); for(unsigned i=0;i<recs && i<512;i++){ size_t e=gp+4+6*(size_t)i;
        rk k=kf; rk_i(&k,"group",g); rk_i(&k,"ypel",U16(e)); rv_reset(&v); rv_i(&v,S16(e+2)); rv_i(&v,S16(e+4)); rec_row("vdmx",&k,&v); } }
    (void)nrec;
  }
  /* ---- hdmx */
  if(find_table(fo,TAG('h','d','m','x'),&o,&l)){
    unsigned n=U16(o+2); uint32_t rs=U32(o+4);
    for(unsigned i=0;i<n && i<256;i++){ size_t r=o+8+(size_t)rs*i; if(r+2>o+l) break; size_t wl=ng; if(r+2+wl>o+l) wl=(o+l>r+2)?o+l-r-2:0;
      rk k=kf; rk_i(&k,"ppem",U8(r)); rv_reset(&v); rv_i(&v,U8(r+1)); rv_u(&v,rs); char h[17]; sha16(h,G_P+r+2,wl); rv_s(&v,h); rec_row("hdmx",&k,&v); } }
  /* ---- LTSH */
  if(find_table(fo,TAG('L','T','S','H'),&o,&l)){ unsigned n=U16(o+2); size_t m=n; if(4+m>l) m=l>4?l-4:0; unsigned lin=0, mx=0;
    for(size_t i=0;i<m;i++){ unsigned y=U8(o+4+i); if(y==1) lin++; if(y>mx) mx=y; }
    rv_reset(&v); rv_i(&v,U16(o)); rv_i(&v,n); rv_i(&v,lin); rv_i(&v,mx); char h[17]; sha16(h,G_P+o+4,m); rv_s(&v,h); rec_row("ltsh",&kf,&v); }
  /* ---- table directory (full only) */
  if(g_full){ for(unsigned i=0;i<nt && i<128;i++){ size_t r=fo+12+16*(size_t)i; char t[5]; tagstr(t,U32(r));
      rk k=kf; rk_s(&k,"tag",t); rv_reset(&v); uint32_t to=U32(r+8), tl=U32(r+12); rv_x(&v,U32(r+4)); rv_u(&v,tl); rv_u(&v,to);
      if(to<=G_N && tl<=G_N-to){ char h[17]; sha16(h,G_P+to,tl); rv_s(&v,h); } else rv_na(&v);
      rec_row("tabdir",&k,&v); } }
}

/* ------------------------------------------------------------------ .fon (FNT resources) */
static void fnt_row(const char *fname, const char *rid, size_t ro, size_t rl){
  rk k; rk_reset(&k); rk_s(&k,"file",fname); rk_s(&k,"res",rid); rv v; rv_reset(&v);
  /* bounded LE reads relative to the resource */
  const uint8_t *sp=G_P; size_t sn=G_N; G_P=sp+ro; G_N=rl;
  unsigned ver=L16(0);
  rv_x(&v,ver); rv_u(&v,L32(2)); rv_x(&v,L16(66)); rv_i(&v,L16(68)); rv_i(&v,L16(70)); rv_i(&v,L16(72)); rv_i(&v,L16(74)); rv_i(&v,L16(76)); rv_i(&v,L16(78));
  rv_i(&v,U8(80)); rv_i(&v,U8(81)); rv_i(&v,U8(82)); rv_i(&v,L16(83)); rv_i(&v,U8(85)); rv_i(&v,L16(86)); rv_i(&v,L16(88)); rv_x(&v,U8(90));
  rv_i(&v,L16(91)); rv_i(&v,L16(93)); rv_i(&v,U8(95)); rv_i(&v,U8(96)); rv_i(&v,U8(97)); rv_i(&v,U8(98));
  uint32_t fo=L32(105); char face[64]=""; if(fo && fo<rl){ int i=0; while(i<63 && fo+i<rl && G_P[fo+i]){ face[i]=(char)G_P[fo+i]; i++; } face[i]=0; }
  rv_s(&v,face[0]?face:"~");
  char cr[61]; int i=0; for(;i<60 && 6+i<(int)rl && G_P[6+i];i++) cr[i]=(char)G_P[6+i]; cr[i]=0; while(i>0&&cr[i-1]==' ') cr[--i]=0; rv_s(&v,cr[0]?cr:"~");
  char h[17]; sha16(h,G_P,rl); rv_s(&v,h); rv_u(&v,rl);
  /* character width table (v2: 4 bytes/char, v3: 6 bytes/char) for first..last+1 */
  unsigned first=U8(95), last=U8(96); size_t ct=(ver>=0x300)?148:118, es=(ver>=0x300)?6:4; unsigned nch=(last>=first)?last-first+2:0;
  if(ct+es*nch<=rl){ uint8_t w[257]; for(unsigned c=0;c<nch && c<257;c++) w[c]=(uint8_t)L16(ct+es*c); char hw[17]; sha16(hw,w,nch<257?nch:257); rv_s(&v,hw); } else rv_na(&v);
  G_P=sp; G_N=sn;
  rec_row("fon",&k,&v);
}
static int g_fonres;
static void do_ne(const char *fname, size_t ne){
  size_t rt=ne+L16(ne+0x24); unsigned shift=L16(rt); size_t p=rt+2; int nres=0;
  for(int guard=0;guard<64;guard++){ unsigned type=L16(p); if(!type) break; unsigned cnt=L16(p+2); p+=8;
    for(unsigned i=0;i<cnt && i<512;i++){ size_t ni=p+12*(size_t)i; size_t off=(size_t)L16(ni)<<shift, len=(size_t)L16(ni+2)<<shift; unsigned id=L16(ni+6);
      if(type==0x8008){ char rid[32]; if(id&0x8000) snprintf(rid,sizeof rid,"%u",id&0x7fff); else snprintf(rid,sizeof rid,"name@%u",id);
        if(off<G_N){ if(len>G_N-off) len=G_N-off; fnt_row(fname,rid,off,len); nres++; g_fonres++; } } }
    p+=12*(size_t)cnt; }
  /* module description = first entry of the non-resident name table */
  uint32_t nro=L32(ne+0x2c); char desc[256]=""; if(nro<G_N){ unsigned n=U8(nro); unsigned i=0; for(;i<n && i<255 && nro+1+i<G_N;i++) desc[i]=(char)G_P[nro+1+i]; desc[i]=0; }
  rk k; rk_reset(&k); rk_s(&k,"file",fname); rv v; rv_reset(&v); rv_s(&v,"ne"); rv_i(&v,nres); rv_s(&v,desc[0]?desc:"~"); rec_row("fon_file",&k,&v);
}
typedef struct { const char *fname; int n; } pe_ctx;
static BOOL CALLBACK pe_res_cb(HMODULE m, LPCWSTR type, LPWSTR name, LONG_PTR lp){
  pe_ctx *c=(pe_ctx*)lp; HRSRC r=FindResourceW(m,name,type); if(!r) return TRUE; HGLOBAL g=LoadResource(m,r); DWORD sz=SizeofResource(m,r);
  const uint8_t *d=(const uint8_t*)LockResource(g); if(!d) return TRUE;
  char rid[64]; if(IS_INTRESOURCE(name)) snprintf(rid,sizeof rid,"%u",(unsigned)(ULONG_PTR)name); else { char u[48]; WideCharToMultiByte(CP_UTF8,0,name,-1,u,sizeof u,NULL,NULL); u[47]=0; snprintf(rid,sizeof rid,"%s",u); }
  const uint8_t *sp=G_P; size_t sn=G_N; G_P=d; G_N=sz; fnt_row(c->fname,rid,0,sz); G_P=sp; G_N=sn; c->n++; g_fonres++;
  return TRUE;
}
static void do_pe_fon(const char *fname, const WCHAR *path){
  HMODULE m=LoadLibraryExW(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  pe_ctx c={fname,0};
  if(m){ EnumResourceNamesW(m,MAKEINTRESOURCEW(8),pe_res_cb,(LONG_PTR)&c); FreeLibrary(m); }
  rk k; rk_reset(&k); rk_s(&k,"file",fname); rv v; rv_reset(&v); rv_s(&v,"pe"); if(m) rv_i(&v,c.n); else rv_na(&v); rv_s(&v,"~"); rec_row("fon_file",&k,&v);
}

/* ------------------------------------------------------------------ per file */
static unsigned long g_kind_n[8]; static const char *KIND[]={"ttf","ttc","otf","fon_ne","fon_pe","dir","other","unread"};
static void do_file(fent *e){
  WCHAR path[MAX_PATH*2]; swprintf(path,MAX_PATH*2,L"%ls\\%ls",g_fontdir,e->wname);
  rk k; rk_reset(&k); rk_s(&k,"file",e->name); rv v; rv_reset(&v);
  if(e->isdir){ rv_u(&v,0); rv_na(&v); rv_s(&v,"dir"); rv_na(&v); rv_na(&v); rv_na(&v); rec_row("dir",&k,&v); g_kind_n[5]++; return; }
  int deep = g_full || e->key;
  size_t n=0; uint8_t *b=NULL;
  if(deep) b=read_file(path,&n);
  rv_u(&v,e->size);
  if(b){ rec_sha256(b,n,e->sha); e->hashed=1; char hx[65]; rec_hex(hx,e->sha,32); rv_s(&v,hx); } else rv_na(&v);
  int kind=7, nfaces=0;
  if(b){
    G_P=b; G_N=n; g_oob=0; uint32_t m=U32(0);
    if(m==TAG('t','t','c','f')){ kind=1; nfaces=(int)U32(8); if(nfaces<0||nfaces>1024) nfaces=0; }
    else if(m==0x00010000 || m==TAG('t','r','u','e')){ kind=0; nfaces=1; }
    else if(m==TAG('O','T','T','O')){ kind=2; nfaces=1; }
    else if(n>0x40 && b[0]=='M' && b[1]=='Z'){ uint32_t lf=L32(0x3c); if(lf+2<n && b[lf]=='N' && b[lf+1]=='E') kind=3; else if(lf+4<n && b[lf]=='P' && b[lf+1]=='E') kind=4; else kind=6; }
    else kind=6;
  } else if(!deep) kind=7;
  g_kind_n[kind]++;
  rv_s(&v,KIND[kind]); rv_i(&v,nfaces);
  rv_version(&v,path);
  rec_row("dir",&k,&v);
  if(e->key){ /* keyfile table is written separately; nothing here */ }
  if(b){
    if(kind==1){ for(int i=0;i<nfaces;i++){ size_t fo=U32(12+4*(size_t)i); if(fo+12<=n) do_face(e->name,i,fo,1); } }
    else if(kind==0||kind==2) do_face(e->name,0,0,1);
    else if(kind==3) do_ne(e->name,L32(0x3c));
    else if(kind==4) do_pe_fon(e->name,path);
    if(g_oob) rec_note("out-of-bounds read while parsing %s (fields shown as 0)",e->name);
    free(b);
  }
  rec_flush();
}

/* ------------------------------------------------------------------ registry */
typedef struct { WCHAR name[400]; DWORD type; BYTE *data; DWORD len; } regv;
static int regv_cmp(const void *a, const void *b){ return _wcsicmp(((const regv*)a)->name,((const regv*)b)->name); }
static void dump_key(const char *shortname, HKEY root, const WCHAR *path, int collect_files){
  HKEY h; rk k; rk_reset(&k); rk_s(&k,"key",shortname); rv v; rv_reset(&v);
  LONG r=RegOpenKeyExW(root,path,0,KEY_READ,&h);
  char up[600]; WideCharToMultiByte(CP_UTF8,0,path,-1,up,sizeof up,NULL,NULL); up[sizeof up-1]=0;
  rv_s(&v,root==HKEY_CURRENT_USER?"HKCU":"HKLM"); rv_s(&v,up);
  if(r!=ERROR_SUCCESS){ rv_i(&v,0); rv_na(&v); rv_na(&v); rec_row("regkey",&k,&v); return; }
  DWORD nv=0,ns=0,mvn=0,mvd=0; RegQueryInfoKeyW(h,NULL,NULL,NULL,&ns,NULL,NULL,&nv,&mvn,&mvd,NULL,NULL);
  rv_i(&v,1); rv_i(&v,nv); rv_i(&v,ns); rec_row("regkey",&k,&v);
  regv *a=(regv*)calloc(nv+1,sizeof *a); int na=0;
  for(DWORD i=0;i<nv;i++){ regv *x=&a[na]; DWORD nl=400, dl=mvd+4; x->data=(BYTE*)calloc(1,dl+4);
    if(RegEnumValueW(h,i,x->name,&nl,NULL,&x->type,x->data,&dl)==ERROR_SUCCESS){ x->len=dl; na++; } else free(x->data); }
  qsort(a,na,sizeof *a,regv_cmp);
  for(int i=0;i<na;i++){ regv *x=&a[i];
    rk kk=k; rk_w(&kk,"name",x->name);
    if(x->type==REG_MULTI_SZ){ const WCHAR *p=(const WCHAR*)x->data; const WCHAR *e=(const WCHAR*)(x->data+x->len); int j=0;
      while(p<e && *p){ rk k2=kk; rk_i(&k2,"i",j++); rv_reset(&v); rv_s(&v,"multi_sz"); rv_w(&v,p); rec_row("reg",&k2,&v);
        if(collect_files){ const WCHAR *c=wcschr(p,L','); WCHAR f[MAX_PATH]; size_t l=c?(size_t)(c-p):wcslen(p); if(l<MAX_PATH){ wcsncpy(f,p,l); f[l]=0; add_extra_key(f); } }
        p+=wcslen(p)+1; }
      if(!j){ rk k2=kk; rk_i(&k2,"i",0); rv_reset(&v); rv_s(&v,"multi_sz"); rv_s(&v,"~"); rec_row("reg",&k2,&v); }
      continue; }
    rk_i(&kk,"i",0); rv_reset(&v);
    if(x->type==REG_SZ||x->type==REG_EXPAND_SZ){ rv_s(&v,x->type==REG_SZ?"sz":"expand_sz"); rv_w(&v,(const WCHAR*)x->data); if(collect_files==2) add_extra_key((const WCHAR*)x->data); }
    else if(x->type==REG_DWORD && x->len>=4){ DWORD d; memcpy(&d,x->data,4); rv_s(&v,"dword"); rv_x(&v,d); }
    else if(x->type==REG_QWORD && x->len>=8){ unsigned long long q; memcpy(&q,x->data,8); rv_s(&v,"qword"); rv_x(&v,q); }
    else { char t[24]; snprintf(t,sizeof t,"type%lu",x->type); rv_s(&v,x->type==REG_BINARY?"binary":t);
      if(x->len<=256){ char hx[520]; rec_hex(hx,x->data,x->len); rv_s(&v,x->len?hx:"~"); }
      else { char h[17]; sha16(h,x->data,x->len); char t2[64]; snprintf(t2,sizeof t2,"sha256/16=%s,len=%lu",h,x->len); rv_s(&v,t2); } }
    rec_row("reg",&kk,&v); }
  for(int i=0;i<na;i++) free(a[i].data);
  free(a);
  /* subkey names (sorted) */
  if(ns){ WCHAR (*sk)[256]=calloc(ns,sizeof *sk); DWORD m=0; for(DWORD i=0;i<ns;i++){ DWORD l=256; if(RegEnumKeyExW(h,i,sk[m],&l,NULL,NULL,NULL,NULL)==ERROR_SUCCESS) m++; }
    for(DWORD i=0;i<m;i++) for(DWORD j=i+1;j<m;j++) if(_wcsicmp(sk[i],sk[j])>0){ WCHAR t[256]; wcscpy(t,sk[i]); wcscpy(sk[i],sk[j]); wcscpy(sk[j],t); }
    for(DWORD i=0;i<m;i++){ rk k2=k; rk_w(&k2,"subkey",sk[i]); rv_reset(&v); rv_i(&v,1); rec_row("regsub",&k2,&v); }
    free(sk); }
  RegCloseKey(h);
}
static void do_registry(void){
  rec_schema("regkey","key","root path exists values subkeys");
  rec_schema("reg","key name i","type value");
  rec_schema("regsub","key subkey","exists");
  const WCHAR *NT=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\";
  struct { const char *s; HKEY r; const WCHAR *p; int abs; int collect; } K[]={
    {"Fonts",HKEY_LOCAL_MACHINE,L"Fonts",0,0},
    {"FontSubstitutes",HKEY_LOCAL_MACHINE,L"FontSubstitutes",0,0},
    {"FontLink.SystemLink",HKEY_LOCAL_MACHINE,L"FontLink\\SystemLink",0,1},
    {"FontLink",HKEY_LOCAL_MACHINE,L"FontLink",0,0},
    {"FontMapper",HKEY_LOCAL_MACHINE,L"FontMapper",0,0},
    {"FontMapperFamilyFallback",HKEY_LOCAL_MACHINE,L"FontMapperFamilyFallback",0,0},
    {"FontDPI",HKEY_LOCAL_MACHINE,L"FontDPI",0,0},
    {"GRE_Initialize",HKEY_LOCAL_MACHINE,L"GRE_Initialize",0,2},
    {"Font Drivers",HKEY_LOCAL_MACHINE,L"Font Drivers",0,0},
    {"FontIntensityCorrection",HKEY_LOCAL_MACHINE,L"FontIntensityCorrection",0,0},
    {"FontCache",HKEY_LOCAL_MACHINE,L"FontCache",0,0},
    {"FontAssoc.AssocCharset",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\FontAssoc\\Associated Charset",1,0},
    {"FontAssoc.AssocDefaultFonts",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\FontAssoc\\Associated DefaultFonts",1,0},
    {"HwProfile.Fonts",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts",1,0},
    {"SurrogateFallback",HKEY_LOCAL_MACHINE,L"LanguagePack\\SurrogateFallback",0,0},
    {"Windows",HKEY_LOCAL_MACHINE,L"Windows",0,0},
    {"HKCU.Fonts",HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",1,0},
    {"HKCU.WindowMetrics",HKEY_CURRENT_USER,L"Control Panel\\Desktop\\WindowMetrics",1,0},
    {"HKCU.Desktop",HKEY_CURRENT_USER,L"Control Panel\\Desktop",1,0},
  };
  for(unsigned i=0;i<sizeof K/sizeof*K;i++){ WCHAR p[400]; if(K[i].abs) wcscpy(p,K[i].p); else swprintf(p,400,L"%ls%ls",NT,K[i].p);
    dump_key(K[i].s,K[i].r,p,K[i].collect); rec_flush(); }
}

/* ------------------------------------------------------------------ GDI: which file backs a face */
static void gdi_file(HDC dc, const char *face, int weight, int charset){
  LOGFONTA lf; memset(&lf,0,sizeof lf); lf.lfHeight=-11; lf.lfWeight=weight; lf.lfCharSet=(BYTE)charset; snprintf(lf.lfFaceName,LF_FACESIZE,"%s",face);
  HFONT f=CreateFontIndirectA(&lf), o=(HFONT)SelectObject(dc,f);
  rk k; rk_reset(&k); rk_s(&k,"face",face); rk_i(&k,"weight",weight); rk_i(&k,"cs",charset); rv v; rv_reset(&v);
  WCHAR got[LF_FACESIZE]=L""; if(GetTextFaceW(dc,LF_FACESIZE,got)>0) rv_w(&v,got); else rv_na(&v);
  TEXTMETRICW tm; if(GetTextMetricsW(dc,&tm)){ rv_x(&v,tm.tmPitchAndFamily); rv_i(&v,tm.tmCharSet); rv_i(&v,tm.tmWeight); } else { rv_na(&v); rv_na(&v); rv_na(&v); }
  DWORD sc=GetFontData(dc,0x66637474 /* 'ttcf' */,0,NULL,0), s0=GetFontData(dc,0,0,NULL,0);
  if(sc==GDI_ERROR) rv_na(&v); else rv_u(&v,sc);
  if(s0==GDI_ERROR) rv_na(&v); else rv_u(&v,s0);
  DWORD use=(sc!=GDI_ERROR)?sc:s0; DWORD tag=(sc!=GDI_ERROR)?0x66637474:0;
  int matched=0;
  if(use!=GDI_ERROR && use>0 && use<(1u<<30)){
    uint8_t *b=(uint8_t*)malloc(use);
    if(b && GetFontData(dc,tag,0,b,use)==use){ uint8_t h[32]; rec_sha256(b,use,h); char hx[65]; rec_hex(hx,h,32); rv_s(&v,hx);
      char mf[1024]=""; int ml=0; for(int i=0;i<NF;i++) if(F[i].hashed && !memcmp(F[i].sha,h,32)){ ml+=snprintf(mf+ml,sizeof mf-ml,"%s%s",ml?",":"",F[i].name); matched++; if(ml>=(int)sizeof mf){ ml=sizeof mf-1; break; } }
      rv_s(&v,ml?mf:"~"); }
    else { rv_na(&v); rv_na(&v); }
    free(b);
  } else { rv_na(&v); rv_na(&v); }
  rv_i(&v,matched);
  rec_row("gdi_file",&k,&v);
  SelectObject(dc,o); DeleteObject(f);
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"font_inventory",PROBE_VER);
  for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"--quick")) g_full=0; else if(!strcmp(argv[i],"--full")) g_full=1;
    else if(!strcmp(argv[i],"--dir") && i+1<argc) g_dir_override=argv[++i]; }
  rec_note(g_full?"mode full":"mode quick");
  g_gid=(uint16_t*)malloc(0x110000*sizeof *g_gid);
  DWORD t0=GetTickCount();

  do_registry();   /* first: GRE_Initialize / SystemLink files join the key-file set */

  rec_schema("dir","file","size:B sha256 kind faces filever prodver");
  rec_schema("face","file face","sfnt_ver tables family subfamily full postscript upem:fu glyphs has_flags eblc_strikes gasp_ranges table_tags");
  rec_schema("name","file face id","value platform encoding lang");
  rec_schema("name_loc","file face id enc lang","value");
  rec_schema("name_n","file face","format count");
  rec_schema("head","file face","version revision magic flags upem:fu xmin:fu ymin:fu xmax:fu ymax:fu macstyle lowest_rec_ppem:px direction_hint index_to_loc");
  rec_schema("hhea","file face","ascender:fu descender:fu linegap:fu adv_max:fu min_lsb:fu min_rsb:fu xmax_extent:fu caret_rise caret_run caret_offset n_hmetrics");
  rec_schema("os2","file face","version xavg:fu weight width fstype fsselection typo_asc:fu typo_desc:fu typo_gap:fu win_asc:fu win_desc:fu panose vendor ur1 ur2 ur3 ur4 cp1 cp2 xheight:fu capheight:fu default_char break_char max_context first_char last_char family_class strikeout_size:fu strikeout_pos:fu sub_xsize:fu sub_ysize:fu sub_xoff:fu sub_yoff:fu sup_xsize:fu sup_ysize:fu sup_xoff:fu sup_yoff:fu table_len:B");
  rec_schema("post","file face","version italic_angle_fixed underline_pos:fu underline_thick:fu is_fixed_pitch");
  rec_schema("maxp","file face","version glyphs points contours comp_points comp_contours zones twilight storage fdefs idefs stack instr_size comp_elems comp_depth");
  { char cols[2048]; int n=snprintf(cols,sizeof cols,"subtables chosen mapped mapped_supp"); for(int b=0;b<NBLK;b++) n+=snprintf(cols+n,sizeof cols-n," %s",BLK[b].n);
    rec_schema("cmap","file face",cols); }
  rec_schema("eblc","file face strike","ppem_x:px ppem_y:px bitdepth flags start_glyph end_glyph n_index_sub hori_asc:px hori_desc:px hori_wmax:px vert_asc:px vert_desc:px");
  rec_schema("gasp","file face i","version max_ppem:px behavior");
  rec_schema("kern","file face","version ntables pairs_f0 formats sha256_16");
  rec_schema("vdmx_ratio","file face r","version charset xratio ystart yend group");
  rec_schema("vdmx","file face group ypel","ymax:px ymin:px");
  rec_schema("hdmx","file face ppem","max_width:px record_size:B widths_sha256_16");
  rec_schema("ltsh","file face","version glyphs linear_count max_ypel:px sha256_16");
  rec_schema("tabdir","file face tag","checksum length:B offset:B sha256_16");
  rec_schema("fon","file res","version size:B type points:pt vres hres ascent:px int_lead:px ext_lead:px italic underline strikeout weight charset pix_width:px pix_height:px pitch_family avg_width:px max_width:px first_char last_char default_char break_char face copyright res_sha256_16 res_len:B widths_sha256_16");
  rec_schema("fon_file","file","format font_res description");

  rec_note("face.has_flags = one 0/1 digit per table, in order: hdmx VDMX LTSH EBLC EBDT EBSC gasp kern GPOS GSUB GDEF CFF glyf DSIG vhea fpgm prep cvt CBLC COLR SVG fvar morx PCLT BASE JSTF MATH bloc");
  list_dir();
  for(int i=0;i<NF;i++) F[i].key=is_key(F[i].name);
  unsigned long long tot=0; for(int i=0;i<NF;i++){ tot+=F[i].size; do_file(&F[i]); }

  /* key files: existence even when absent */
  rec_schema("keyfile","file","exists size:B sha256 filever");
  int nk=0; const char *klist[160]; for(int i=0;KEYFILES[i];i++){ int dup=0; for(int j=0;j<nk;j++) if(!strcmp(klist[j],KEYFILES[i])) dup=1; if(!dup && nk<160) klist[nk++]=KEYFILES[i]; }
  for(int i=0;i<g_nextra;i++){ int dup=0; for(int j=0;j<nk;j++) if(!strcmp(klist[j],g_extra_key[i])) dup=1; if(!dup && nk<160) klist[nk++]=g_extra_key[i]; }
  for(int i=0;i<nk;i++){ rk k; rk_reset(&k); rk_s(&k,"file",klist[i]); rv v; rv_reset(&v); fent *e=NULL;
    for(int j=0;j<NF;j++) if(!strcmp(F[j].name,klist[i])){ e=&F[j]; break; }
    if(!e){ rv_i(&v,0); rv_na(&v); rv_na(&v); rv_na(&v); }
    else { rv_i(&v,1); rv_u(&v,e->size); if(e->hashed){ char hx[65]; rec_hex(hx,e->sha,32); rv_s(&v,hx); } else rv_na(&v);
      WCHAR path[MAX_PATH*2]; swprintf(path,MAX_PATH*2,L"%ls\\%ls",g_fontdir,e->wname); rv vv; rv_reset(&vv); rv_version(&vv,path);
      char *sp=strchr(vv.b,' '); if(sp) *sp=0; rv_raw(&v,vv.b); }
    rec_row("keyfile",&k,&v); }

  /* which file GDI maps each face to */
  rec_schema("gdi_file","face weight cs","got_face pitch_family charset weight size_ttcf:B size_table0:B sha256 matched_files n_matched");
  HDC dc=GetDC(NULL);
  static const char *GF[]={"Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Microsoft Sans Serif","Tahoma","Arial","Courier New","Courier","MS Sans Serif",
    "MS Serif","System","Terminal","Fixedsys","Small Fonts","Marlett","Segoe UI Symbol","Segoe UI Emoji","Times New Roman","Lucida Console","Consolas","Calibri","Verdana",
    "Symbol","Wingdings","Webdings","Helv","Tms Rmn","Helvetica","Times","Modern","Roman","Script","MS UI Gothic","MS Gothic","SimSun","PMingLiU","Gulim",
    "Microsoft YaHei","Microsoft JhengHei","Malgun Gothic","Meiryo","Yu Gothic","Mangal","NoSuchFace12345",""};
  for(unsigned i=0;i<sizeof GF/sizeof*GF;i++){ gdi_file(dc,GF[i],FW_NORMAL,DEFAULT_CHARSET); }
  static const char *GB[]={"Segoe UI","Tahoma","MS Shell Dlg","MS Shell Dlg 2","Microsoft Sans Serif","Arial","Courier New"};
  for(unsigned i=0;i<sizeof GB/sizeof*GB;i++) gdi_file(dc,GB[i],FW_BOLD,DEFAULT_CHARSET);
  static const char *GC[]={"MS Shell Dlg","MS Shell Dlg 2","Segoe UI","Tahoma","Arial"};
  static const int CS[]={ANSI_CHARSET,SHIFTJIS_CHARSET,GB2312_CHARSET,HANGUL_CHARSET,CHINESEBIG5_CHARSET,SYMBOL_CHARSET,OEM_CHARSET,GREEK_CHARSET,RUSSIAN_CHARSET,ARABIC_CHARSET,HEBREW_CHARSET,THAI_CHARSET};
  for(unsigned i=0;i<sizeof GC/sizeof*GC;i++) for(unsigned j=0;j<sizeof CS/sizeof*CS;j++) gdi_file(dc,GC[i],FW_NORMAL,CS[j]);
  ReleaseDC(NULL,dc);

  rec_schema("summary","~","files bytes:B ttf ttc otf fon_ne fon_pe dirs other unread sfnt_faces fon_resources hashed");
  { rv v; rv_reset(&v); rv_i(&v,NF); rv_u(&v,tot); for(int i=0;i<8;i++) rv_u(&v,g_kind_n[i]); rv_i(&v,g_nfaces); rv_i(&v,g_fonres);
    int hs=0; for(int i=0;i<NF;i++) hs+=F[i].hashed; rv_i(&v,hs); rec_rowf("summary","~","%s",v.b); }
  rec_note("elapsed_ms %lu",(unsigned long)(GetTickCount()-t0)); /* N line: timing is not data */
  return rec_finish(0);
}
