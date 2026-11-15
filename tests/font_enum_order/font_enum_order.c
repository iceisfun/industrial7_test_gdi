#include "t.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
/* font_enum_order: enumeration (EnumFontFamilies*, EnumFonts*), font creation round trips, the mapper flags and the
 * undocumented font-link / face-alias exports. Sources for every expectation are in EVIDENCE.md. */

#define MAXE 4096
typedef struct {
  char  a[LF_FACESIZE];      /* lfFaceName as ANSI (A variants) or "" */
  WCHAR w[LF_FACESIZE];      /* lfFaceName as wide (W variants) or converted */
  WCHAR full[LF_FULLFACESIZE], style[LF_FACESIZE];
  DWORD type; BYTE cs, pf; LONG h, tmh; DWORD ntmf; int ex; BYTE tmcs;
} Ent;
static Ent g_e[MAXE]; static int g_n, g_stopat, g_ret, g_ex_mode, g_lpbad; static LPARAM g_lp;

static void a2w(const char *s, WCHAR *w, int cap){ MultiByteToWideChar(CP_ACP,0,s,-1,w,cap); }
static void w2a(const WCHAR *w, char *s, int cap){ WideCharToMultiByte(CP_ACP,0,w,-1,s,cap,NULL,NULL); }

/* g_ex_mode: 0 = ENUMLOGFONT (non-Ex), 1 = ENUMLOGFONTEX */
static int CALLBACK cbW(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp){
  if(lp!=g_lp) g_lpbad++;
  if(g_n<MAXE){ Ent *e=&g_e[g_n]; memset(e,0,sizeof *e); lstrcpynW(e->w,lf->lfFaceName,LF_FACESIZE); w2a(e->w,e->a,LF_FACESIZE);
    e->type=type; e->cs=lf->lfCharSet; e->pf=lf->lfPitchAndFamily; e->h=lf->lfHeight; e->tmh=tm->tmHeight; e->tmcs=tm->tmCharSet; e->ex=g_ex_mode;
    if(g_ex_mode){ const ENUMLOGFONTEXW *x=(const ENUMLOGFONTEXW*)lf; lstrcpynW(e->full,x->elfFullName,LF_FULLFACESIZE); lstrcpynW(e->style,x->elfStyle,LF_FACESIZE); }
    else { const ENUMLOGFONTW *x=(const ENUMLOGFONTW*)lf; lstrcpynW(e->full,x->elfFullName,LF_FULLFACESIZE); lstrcpynW(e->style,x->elfStyle,LF_FACESIZE); }
    if(type & TRUETYPE_FONTTYPE) e->ntmf=((const NEWTEXTMETRICW*)tm)->ntmFlags; else e->ntmf=0xffffffff;
  }
  g_n++;
  if(g_stopat && g_n>=g_stopat) return 0;
  return g_ret;
}
static int CALLBACK cbA(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM lp){
  if(lp!=g_lp) g_lpbad++;
  if(g_n<MAXE){ Ent *e=&g_e[g_n]; memset(e,0,sizeof *e); lstrcpynA(e->a,lf->lfFaceName,LF_FACESIZE); a2w(e->a,e->w,LF_FACESIZE);
    e->type=type; e->cs=lf->lfCharSet; e->pf=lf->lfPitchAndFamily; e->h=lf->lfHeight; e->tmh=tm->tmHeight; e->tmcs=tm->tmCharSet; e->ex=g_ex_mode;
    if(g_ex_mode){ const ENUMLOGFONTEXA *x=(const ENUMLOGFONTEXA*)lf; a2w((const char*)x->elfFullName,e->full,LF_FULLFACESIZE); a2w((const char*)x->elfStyle,e->style,LF_FACESIZE); }
    else { const ENUMLOGFONTA *x=(const ENUMLOGFONTA*)lf; a2w((const char*)x->elfFullName,e->full,LF_FULLFACESIZE); a2w((const char*)x->elfStyle,e->style,LF_FACESIZE); }
    if(type & TRUETYPE_FONTTYPE) e->ntmf=((const NEWTEXTMETRICA*)tm)->ntmFlags; else e->ntmf=0xffffffff;
  }
  g_n++;
  if(g_stopat && g_n>=g_stopat) return 0;
  return g_ret;
}
static void reset(int stopat, int ret, int ex){ g_lp=0x5a5a1234; g_lpbad=0; g_n=0; g_stopat=stopat; g_ret=ret; g_ex_mode=ex; }
static HDC mkdc(void){ return U(CreateCompatibleDC)(NULL); }
static char *n8(const WCHAR *w){ static char b[8][200]; static int i; char *r=b[i++&7]; WideCharToMultiByte(CP_UTF8,0,w,-1,r,200,NULL,NULL); return r; }

static int find_w(const WCHAR *name){ for(int i=0;i<g_n&&i<MAXE;i++) if(!lstrcmpiW(g_e[i].w,name)) return i; return -1; }
static int exists_face(HDC dc, const WCHAR *face){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfCharSet=DEFAULT_CHARSET; lstrcpyW(lf.lfFaceName,face);
  reset(0,1,1); U(EnumFontFamiliesExW)(dc,&lf,(FONTENUMPROCW)cbW,g_lp,0); return g_n>0;
}


/* ------------------------------------------------------------------ helpers */
static int run_efw(HDC dc, const WCHAR *face){ reset(0,1,0); return U(EnumFontFamiliesW)(dc,face,(FONTENUMPROCW)cbW,g_lp); }
static int run_efa(HDC dc, const WCHAR *face){ char a[LF_FACESIZE]; reset(0,1,0); if(face) w2a(face,a,sizeof a);
  return U(EnumFontFamiliesA)(dc,face?a:NULL,(FONTENUMPROCA)cbA,g_lp); }
static int run_efontsw(HDC dc, const WCHAR *face){ reset(0,1,0); return U(EnumFontsW)(dc,face,(FONTENUMPROCW)cbW,g_lp); }
static int run_efontsa(HDC dc, const WCHAR *face){ char a[LF_FACESIZE]; reset(0,1,0); if(face) w2a(face,a,sizeof a);
  return U(EnumFontsA)(dc,face?a:NULL,(FONTENUMPROCA)cbA,g_lp); }
static int run_exw(HDC dc, const WCHAR *face, int cs){ LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfCharSet=(BYTE)cs; lstrcpyW(lf.lfFaceName,face);
  reset(0,1,1); return U(EnumFontFamiliesExW)(dc,&lf,(FONTENUMPROCW)cbW,g_lp,0); }
static int run_exa(HDC dc, const WCHAR *face, int cs){ LOGFONTA lf; memset(&lf,0,sizeof lf); lf.lfCharSet=(BYTE)cs; w2a(face,lf.lfFaceName,LF_FACESIZE);
  reset(0,1,1); return U(EnumFontFamiliesExA)(dc,&lf,(FONTENUMPROCA)cbA,g_lp,0); }
static Ent *snap(int *n){ static Ent s[MAXE]; *n=g_n<MAXE?g_n:MAXE; memcpy(s,g_e,sizeof(Ent)*(size_t)*n); return s; }
static int save(Ent *dst){ int n=g_n<MAXE?g_n:MAXE; memcpy(dst,g_e,sizeof(Ent)*(size_t)n); return n; }
static int is_tt(const Ent *e){ return (e->type&TRUETYPE_FONTTYPE)!=0; }
/* index of the first entry with this name in a snapshot, -1 if absent */
static int idx(const Ent *s,int n,const WCHAR *nm){ for(int i=0;i<n;i++) if(!lstrcmpiW(s[i].w,nm)) return i; return -1; }
/* true if names appear in this relative order (all must be present) ; -1 when one is absent */
static int in_order(const Ent *s,int n,const WCHAR **nm,int k){ int last=-1; for(int i=0;i<k;i++){ int j=idx(s,n,nm[i]); if(j<0) return -1; if(j<=last) return 0; last=j; } return 1; }
static void sigof(const Ent *s,int n,char *out,int cap){
  out[0]=0;
  for(int i=0;i<n;i++){ char st[LF_FACESIZE],t[160]; strncpy(st,n8(s[i].style),sizeof st-1); st[sizeof st-1]=0; for(char *c=st;*c;c++){ if(*c==' ') *c='_'; }
    snprintf(t,sizeof t,"%s%s/%d",i?" ":"",st,s[i].cs); if(strlen(out)+strlen(t)+1<(size_t)cap) strcat(out,t); }
}
static int cmp_str(const void *a,const void *b){ return strcmp(*(char*const*)a,*(char*const*)b); }
/* multiset equality of space separated tokens */
static int same_multiset(const char *a,const char *b){
  char ta[4096],tb[4096]; strncpy(ta,a,sizeof ta-1); ta[sizeof ta-1]=0; strncpy(tb,b,sizeof tb-1); tb[sizeof tb-1]=0;
  /* style names use '_' for spaces inside a token */
  char *pa[256],*pb[256]; int na=0,nb=0; char *p;
  for(p=strtok(ta," ");p;p=strtok(NULL," ")) { if(strchr(p,'/') ) { pa[na++]=p; } }
  for(p=strtok(tb," ");p;p=strtok(NULL," ")) { if(strchr(p,'/') ) { pb[nb++]=p; } }
  if(na!=nb){ return 0; }
  qsort(pa,na,sizeof*pa,cmp_str); qsort(pb,nb,sizeof*pb,cmp_str);
  for(int i=0;i<na;i++){ if(strcmp(pa[i],pb[i])) return 0; }
  return 1;
}
static void regsub(const char *n,char *out,int cap){ out[0]=0; DWORD sz=cap; RegGetValueA(HKEY_LOCAL_MACHINE,"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",n,RRF_RT_REG_SZ,NULL,out,&sz); }

/* Anchors: relative order of well-known families in the NULL-face listing, and the raster block before the TrueType block. */
static const WCHAR *ANCH_RASTER[]={L"System",L"Terminal",L"Fixedsys",L"Modern",L"Roman",L"Script",L"Courier",L"MS Serif",L"MS Sans Serif",L"Small Fonts"};
static const WCHAR *ANCH_TT[]={L"Marlett",L"Arial",L"Arial Black",L"Calibri",L"Cambria",L"Candara",L"Comic Sans MS",L"Consolas",L"Courier New",L"Georgia",L"Impact",
  L"Lucida Console",L"Microsoft Sans Serif",L"Segoe UI",L"Symbol",L"Tahoma",L"Times New Roman",L"Trebuchet MS",L"Verdana",L"Webdings",L"Wingdings"};

/* ------------------------------------------------------------------ EnumFontFamiliesW */
TEST(feo_efw_null_lists_each_family_once){
  HDC dc=mkdc(); REQUIRE(dc);
  int r=run_efw(dc,NULL); int n; Ent *s=snap(&n);
  EXPECT_EQ(r,1);                       /* callback returned 1 every time: return value is the last callback value */
  CHECK(n>0); EXPECT_EQ(g_lpbad,0);
  int tt=0,ras=0,other=0,dup=0,noflags=0,badtype=0,first_tt=-1,late_raster=0;
  for(int i=0;i<n;i++){
    if(s[i].type & ~(DWORD)(RASTER_FONTTYPE|TRUETYPE_FONTTYPE)) badtype++;     /* no DEVICE_FONTTYPE on a screen DC */
    if(is_tt(&s[i])){ tt++; if(first_tt<0) first_tt=i; if(!s[i].ntmf||s[i].ntmf==0xffffffff) noflags++; }
    else if(s[i].type&RASTER_FONTTYPE){ ras++; if(first_tt>=0) late_raster++; }
    else other++;
    for(int j=0;j<i;j++) if(!lstrcmpiW(s[i].w,s[j].w)) dup++;
  }
  EXPECT_EQ(dup,0); EXPECT_EQ(badtype,0); EXPECT_EQ(noflags,0);
  CHECK(tt>0); CHECK(ras>0);
  t_note("families=%d truetype=%d raster=%d vector(type 0)=%d",n,tt,ras,other);
  /* Oracle: stroked "vector" families (Modern, Roman, Script) arrive with FontType 0: neither RASTER nor TRUETYPE nor DEVICE. */
  for(int i=0;i<n;i++) if(!is_tt(&s[i]) && !(s[i].type&RASTER_FONTTYPE)) t_note("FontType 0 family: %s",n8(s[i].w));
  EXPECT_EQ(late_raster,0);             /* observed: every non-TrueType family precedes the first TrueType family (every boot) */
  U(DeleteDC)(dc);
}
TEST(feo_efw_null_order_anchors){
  HDC dc=mkdc(); REQUIRE(dc);
  run_efw(dc,NULL); int n; Ent *s=snap(&n);
  int r1=in_order(s,n,ANCH_RASTER,sizeof ANCH_RASTER/sizeof*ANCH_RASTER), r2=in_order(s,n,ANCH_TT,sizeof ANCH_TT/sizeof*ANCH_TT);
  if(r1<0||r2<0){ t_skip("standard font inventory not installed (anchor family missing)"); }
  else { CHECKF(r1==1,"raster anchors out of order"); CHECKF(r2==1,"TrueType anchors out of order"); }
  U(DeleteDC)(dc);
}
TEST(feo_efw_first_face_of_family){
  HDC dc=mkdc(); REQUIRE(dc);
  run_efw(dc,NULL); int n; Ent *s=snap(&n);
  static const WCHAR *F[]={L"Arial",L"Courier New",L"Segoe UI",L"Tahoma",L"Times New Roman",L"Courier",L"MS Sans Serif"};
  for(unsigned k=0;k<sizeof F/sizeof*F;k++){
    int i=idx(s,n,F[k]); if(i<0){ t_skip("%s not installed",n8(F[k])); continue; }
    run_efw(dc,F[k]); CHECKF(g_n>0,"%s",n8(F[k])); if(!g_n) continue;
    CHECKF(g_e[0].type==s[i].type && g_e[0].cs==s[i].cs && g_e[0].pf==s[i].pf && !lstrcmpW(g_e[0].full,s[i].full) && !lstrcmpW(g_e[0].style,s[i].style),
           "%s: first face of the per-family listing differs from the NULL listing entry",n8(F[k]));
    if(is_tt(&s[i])) CHECKF(!lstrcmpW(s[i].style,L"Regular"),"%s style %s",n8(F[k]),n8(s[i].style));
  }
  U(DeleteDC)(dc);
}
TEST(feo_efw_callback_stop_and_return){
  HDC dc=mkdc(); REQUIRE(dc);
  run_efw(dc,NULL); int full=g_n; CHECK(full>3);
  int r; reset(1,1,0); r=U(EnumFontFamiliesW)(dc,NULL,(FONTENUMPROCW)cbW,g_lp); EXPECT_EQ(g_n,1); EXPECT_EQ(r,0);
  reset(3,1,0); r=U(EnumFontFamiliesW)(dc,NULL,(FONTENUMPROCW)cbW,g_lp); EXPECT_EQ(g_n,3); EXPECT_EQ(r,0);
  reset(0,5,0); r=U(EnumFontFamiliesW)(dc,NULL,(FONTENUMPROCW)cbW,g_lp); EXPECT_EQ(g_n,full); EXPECT_EQ(r,5);   /* any nonzero continues; last value returned */
  reset(0,-1,0); r=U(EnumFontFamiliesW)(dc,NULL,(FONTENUMPROCW)cbW,g_lp); EXPECT_EQ(g_n,full); EXPECT_EQ(r,-1);
  reset(0,1,0); r=U(EnumFontFamiliesA)(dc,NULL,(FONTENUMPROCA)cbA,g_lp); EXPECT_EQ(g_n,full); EXPECT_EQ(r,1);
  reset(2,1,0); r=U(EnumFontFamiliesA)(dc,NULL,(FONTENUMPROCA)cbA,g_lp); EXPECT_EQ(g_n,2); EXPECT_EQ(r,0);
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ A versus W */
TEST(feo_efa_vs_efw_same_dc){
  HDC dc=mkdc(); REQUIRE(dc);
  static Ent wl[MAXE]; int nw,na;
  run_efw(dc,NULL); nw=save(wl);
  run_efa(dc,NULL); Ent *a=snap(&na);
  EXPECT_EQ(na,nw);
  int bad=0,badnarrow=0;
  for(int i=0;i<na&&i<nw;i++){
    char nar[LF_FACESIZE]; WideCharToMultiByte(CP_ACP,0,wl[i].w,-1,nar,sizeof nar,NULL,NULL);
    if(strcmp(nar,a[i].a)) badnarrow++;
    if(lstrcmpW(wl[i].w,a[i].w) || wl[i].type!=a[i].type || wl[i].cs!=a[i].cs || wl[i].ntmf!=a[i].ntmf || lstrcmpW(wl[i].full,a[i].full) || lstrcmpW(wl[i].style,a[i].style)) bad++;
  }
  EXPECT_EQ(badnarrow,0);     /* A name == W name narrowed by the ACP */
  EXPECT_EQ(bad,0);           /* same families, same relative order, same flags */
  EXPECT_EQ(g_lpbad,0);
  /* EnumFonts with a NULL face enumerates the same family list as EnumFontFamilies */
  static Ent fl[MAXE]; int nf;
  run_efontsw(dc,NULL); nf=save(fl);
  EXPECT_EQ(nf,nw); int bad2=0; for(int i=0;i<nf&&i<nw;i++) if(lstrcmpW(fl[i].w,wl[i].w)||fl[i].type!=wl[i].type) bad2++; EXPECT_EQ(bad2,0);
  run_efontsa(dc,NULL); EXPECT_EQ(g_n,nw);
  U(DeleteDC)(dc);
}
TEST(feo_efa_vs_efw_per_family){
  HDC dc=mkdc(); REQUIRE(dc);
  static const WCHAR *F[]={L"Arial",L"Courier",L"Terminal",L"Segoe UI",L"MS Sans Serif"};
  for(unsigned k=0;k<sizeof F/sizeof*F;k++){
    static Ent wl[MAXE]; int nw,na; run_efw(dc,F[k]); nw=save(wl);
    run_efa(dc,F[k]); Ent *a=snap(&na);
    CHECKF(na==nw,"%s: %d vs %d",n8(F[k]),na,nw); if(na!=nw) continue;
    int bad=0; for(int i=0;i<na;i++) if(lstrcmpW(wl[i].w,a[i].w)||wl[i].type!=a[i].type||wl[i].cs!=a[i].cs||wl[i].h!=a[i].h||lstrcmpW(wl[i].full,a[i].full)) bad++;
    CHECKF(bad==0,"%s: %d entries differ between A and W",n8(F[k]),bad);
  }
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ EnumFontFamiliesEx */
TEST(feo_exa_ansi_charset_empty_name){
  HDC dc=mkdc(); REQUIRE(dc);
  int r=run_exa(dc,L"",ANSI_CHARSET); int n; Ent *s=snap(&n); static Ent sa[MAXE]; memcpy(sa,s,sizeof(Ent)*n);
  EXPECT_EQ(r,1); CHECK(n>0); EXPECT_EQ(g_lpbad,0);
  int badcs=0,dup=0; for(int i=0;i<n;i++){ if(sa[i].cs!=ANSI_CHARSET||sa[i].tmcs!=ANSI_CHARSET) badcs++; for(int j=0;j<i;j++) if(!lstrcmpiW(sa[i].w,sa[j].w)&&!lstrcmpW(sa[i].style,sa[j].style)) dup++; }
  EXPECT_EQ(badcs,0); EXPECT_EQ(dup,0);
  /* only faces with that charset: OEM (Terminal), SYMBOL and the stroked families are absent, Arial/System present */
  CHECK(idx(sa,n,L"Arial")>=0); CHECK(idx(sa,n,L"System")>=0);
  CHECK(idx(sa,n,L"Terminal")<0); CHECK(idx(sa,n,L"Symbol")<0); CHECK(idx(sa,n,L"Wingdings")<0); CHECK(idx(sa,n,L"Marlett")<0); CHECK(idx(sa,n,L"Modern")<0);
  /* the ANSI listing is exactly the cs==ANSI entries of the DEFAULT_CHARSET listing, in the same order (styles collapse to one per family) */
  run_exw(dc,L"",DEFAULT_CHARSET); int nd; Ent *d=snap(&nd);
  int k=0,mism=0; for(int i=0;i<nd;i++) if(d[i].cs==ANSI_CHARSET){ if(k>=n||lstrcmpW(d[i].w,sa[k].w)) mism++; k++; }
  CHECKF(k==n && mism==0,"ANSI listing (%d) vs ANSI entries of DEFAULT listing (%d), %d mismatches",n,k,mism);
  /* every name is also in the plain family listing */
  run_efw(dc,NULL); int nf; Ent *f=snap(&nf); int miss=0; for(int i=0;i<n;i++) if(idx(f,nf,sa[i].w)<0) miss++; EXPECT_EQ(miss,0);
  /* SYMBOL_CHARSET lists only symbol faces */
  run_exw(dc,L"",SYMBOL_CHARSET); n=g_n; int bs=0; for(int i=0;i<n;i++) if(g_e[i].cs!=SYMBOL_CHARSET) bs++; CHECK(n>0); EXPECT_EQ(bs,0);
  U(DeleteDC)(dc);
}
TEST(feo_exa_face_name_styles){
  HDC dc=mkdc(); REQUIRE(dc);
  static const WCHAR *F[]={L"Arial",L"Courier New",L"Segoe UI",L"Times New Roman"};
  static const WCHAR *ST[]={L"Regular",L"Bold",L"Bold Italic",L"Italic"};
  static const DWORD NF[]={NTM_REGULAR,NTM_BOLD,NTM_BOLD|NTM_ITALIC,NTM_ITALIC};
  for(unsigned k=0;k<4;k++){
    int r=run_exa(dc,F[k],ANSI_CHARSET); if(!g_n){ t_skip("%s not installed",n8(F[k])); continue; }
    EXPECT_EQ(r,1); EXPECT_EQ(g_lpbad,0);
    CHECKF(g_n==4,"%s: %d styles",n8(F[k]),g_n); if(g_n!=4) continue;
    for(int i=0;i<4;i++){
      CHECKF(!lstrcmpiW(g_e[i].w,F[k]),"%s lfFaceName %s",n8(F[k]),n8(g_e[i].w));
      CHECKF(!lstrcmpW(g_e[i].style,ST[i]),"%s style[%d]=%s want %s",n8(F[k]),i,n8(g_e[i].style),n8(ST[i]));
      CHECK(g_e[i].type==TRUETYPE_FONTTYPE); CHECK(g_e[i].cs==ANSI_CHARSET);
      CHECKF((g_e[i].ntmf&(NTM_REGULAR|NTM_BOLD|NTM_ITALIC))==NF[i],"%s ntmFlags %lx for %s",n8(F[k]),g_e[i].ntmf,n8(ST[i]));
      CHECK(g_e[i].full[0]!=0);
      CHECKF(wcsstr(g_e[i].full,F[k])!=NULL,"full name %s",n8(g_e[i].full));
    }
  }
  /* a charset the family does not cover: nothing is enumerated */
  run_exa(dc,L"Arial",SYMBOL_CHARSET); EXPECT_EQ(g_n,0);
  /* GREEK_CHARSET: the same four styles, carrying lfCharSet GREEK */
  run_exa(dc,L"Arial",GREEK_CHARSET); CHECKF(g_n==4,"Arial greek n=%d",g_n); if(g_n==4){ for(int i=0;i<4;i++){ EXPECT_EQ(g_e[i].cs,GREEK_CHARSET); EXPECT_EQ(g_e[i].tmcs,GREEK_CHARSET); } }
  /* unknown family: callback never called */
  int r=run_exa(dc,L"NoSuchFaceX",DEFAULT_CHARSET); EXPECT_EQ(g_n,0); EXPECT_EQ(r,1);
  r=run_exw(dc,L"NoSuchFaceX",DEFAULT_CHARSET); EXPECT_EQ(g_n,0); EXPECT_EQ(r,1);
  /* ExA and ExW agree */
  static Ent w[MAXE]; int nw; run_exw(dc,L"Arial",DEFAULT_CHARSET); nw=save(w); int na; run_exa(dc,L"Arial",DEFAULT_CHARSET); Ent *a=snap(&na);
  EXPECT_EQ(na,nw); int bad=0; for(int i=0;i<na&&i<nw;i++) if(lstrcmpW(a[i].w,w[i].w)||a[i].cs!=w[i].cs||lstrcmpW(a[i].style,w[i].style)||a[i].ntmf!=w[i].ntmf) bad++; EXPECT_EQ(bad,0);
  /* raster family through the Ex entry point: no elfStyle, no ntmFlags, FontType RASTER */
  run_exa(dc,L"Courier",DEFAULT_CHARSET); CHECK(g_n>0); for(int i=0;i<g_n;i++){ CHECK(g_e[i].type==RASTER_FONTTYPE); CHECK(g_e[i].style[0]==0); CHECK(g_e[i].full[0]!=0); }
  U(DeleteDC)(dc);
}
/* Recorded enumeration order (observed in every boot on Win10 22H2 19045.2965, en-US, standard inventory): style-major, then charset in
 * ANSI, HEBREW, ARABIC, GREEK, TURKISH, BALTIC, EASTEUROPE, RUSSIAN, VIETNAMESE order; styles Regular, Bold, Bold Italic, Italic. */
static const char SIG_FULL[]="Regular/0 Regular/177 Regular/178 Regular/161 Regular/162 Regular/186 Regular/238 Regular/204 Regular/163 Bold/0 Bold/177 Bold/178 Bold/161 Bold/162 Bold/186 Bold/238 Bold/204 Bold/163 "
  "Bold_Italic/0 Bold_Italic/177 Bold_Italic/161 Bold_Italic/162 Bold_Italic/186 Bold_Italic/238 Bold_Italic/204 Bold_Italic/163 Italic/0 Italic/177 Italic/161 Italic/162 Italic/186 Italic/238 Italic/204 Italic/163";
static const char SIG_SEGOE[]="Regular/0 Regular/177 Regular/178 Regular/161 Regular/162 Regular/186 Regular/238 Regular/204 Regular/163 Bold/0 Bold/177 Bold/178 Bold/161 Bold/162 Bold/186 Bold/238 Bold/204 Bold/163 "
  "Bold_Italic/0 Bold_Italic/161 Bold_Italic/162 Bold_Italic/186 Bold_Italic/238 Bold_Italic/204 Bold_Italic/163 Italic/0 Italic/161 Italic/162 Italic/186 Italic/238 Italic/204 Italic/163";
TEST(feo_exa_default_charset_order_recorded){
  HDC dc=mkdc(); REQUIRE(dc);
  static const struct { const WCHAR *f; const char *sig; } G[]={{L"Arial",SIG_FULL},{L"Courier New",SIG_FULL},{L"Segoe UI",SIG_SEGOE}};
  for(int k=0;k<3;k++){
    run_exa(dc,G[k].f,DEFAULT_CHARSET); int n; Ent *s=snap(&n); char sg[4096]; sigof(s,n,sg,sizeof sg);
    if(!strcmp(sg,G[k].sig)) { CHECK(1); continue; }
    if(!same_multiset(sg,G[k].sig)) t_skip("%s: installed styles/charsets differ from the recorded inventory",n8(G[k].f));
    else CHECKF(0,"%s: same faces but different order: %s",n8(G[k].f),sg);
  }
  U(DeleteDC)(dc);
}
TEST(feo_exa_substituted_face_listing){
  HDC dc=mkdc(); REQUIRE(dc);
  char sub[128]; regsub("MS Shell Dlg",sub,sizeof sub); WCHAR subw[128]; a2w(sub,subw,128);
  int r=run_exa(dc,L"MS Shell Dlg",DEFAULT_CHARSET); EXPECT_EQ(r,1);
  if(!sub[0]||!g_n){ t_skip("no FontSubstitutes entry for MS Shell Dlg or not enumerated (n=%d)",g_n); U(DeleteDC)(dc); return; }
  int bad=0; for(int i=0;i<g_n;i++){ if(lstrcmpiW(g_e[i].w,L"MS Shell Dlg")) bad++; if(lstrcmpiW(g_e[i].full,subw)) bad++; }
  CHECKF(bad==0,"entries must carry the alias as lfFaceName and the substitute target (%s) as elfFullName",sub);
  t_note("MS Shell Dlg lists %d entries via %s",g_n,sub);
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ EnumFonts */
TEST(feo_enumfonts_bitmap_sizes){
  HDC dc=mkdc(); REQUIRE(dc);
  static const WCHAR *F[]={L"Courier",L"MS Sans Serif",L"Terminal"};
  for(unsigned k=0;k<3;k++){
    int rw=run_efontsw(dc,F[k]); int nw; static Ent w[MAXE]; nw=save(w);
    if(!nw){ t_skip("%s not installed",n8(F[k])); continue; }
    EXPECT_EQ(rw,1); EXPECT_EQ(g_lpbad,0);
    int ra=run_efontsa(dc,F[k]); EXPECT_EQ(ra,1); EXPECT_EQ(g_n,nw);
    CHECKF(nw>=2,"%s: %d sizes",n8(F[k]),nw);
    int distinct=0,notraster=0; for(int i=0;i<nw;i++){ int d=1; for(int j=0;j<i;j++) if(w[j].h==w[i].h) d=0; distinct+=d; if(w[i].type!=RASTER_FONTTYPE||lstrcmpiW(w[i].w,F[k])) notraster++; }
    CHECKF(distinct>=2,"%s: only %d distinct heights",n8(F[k]),distinct); EXPECT_EQ(notraster,0);
    t_note("%s: %d entries, %d distinct heights",n8(F[k]),nw,distinct);
    /* EnumFontFamilies lists the same entries */
    run_efw(dc,F[k]); EXPECT_EQ(g_n,nw);
    /* return value = last callback return */
    int r; reset(0,7,0); r=U(EnumFontsW)(dc,F[k],(FONTENUMPROCW)cbW,g_lp); EXPECT_EQ(g_n,nw); EXPECT_EQ(r,7);
    reset(2,1,0); r=U(EnumFontsW)(dc,F[k],(FONTENUMPROCW)cbW,g_lp); EXPECT_EQ(g_n,2); EXPECT_EQ(r,0);
    reset(1,1,0); r=U(EnumFontsA)(dc,"Courier",(FONTENUMPROCA)cbA,g_lp); EXPECT_EQ(g_n,1); EXPECT_EQ(r,0);
  }
  U(DeleteDC)(dc);
}
TEST(feo_enumfonts_truetype_family){
  HDC dc=mkdc(); REQUIRE(dc);
  /* Oracle: EnumFonts on a TrueType family lists every style and every charset-alias family ("Arial Greek" ...) as separate entries,
   * not a single entry. Entries come grouped by style: Regular, Bold, Bold Italic, Italic. */
  int r=run_efontsw(dc,L"Arial"); int n; static Ent w[MAXE]; n=save(w);
  if(!n){ t_skip("Arial not installed"); U(DeleteDC)(dc); return; }
  EXPECT_EQ(r,1); CHECK(n>1);
  int first_ok=!lstrcmpW(w[0].w,L"Arial")&&!lstrcmpW(w[0].style,L"Regular"); CHECK(first_ok);
  int bad=0,ncs0=0; for(int i=0;i<n;i++){ if(!is_tt(&w[i])) bad++; if(w[i].ntmf==0||w[i].ntmf==0xffffffff) bad++; if(wcsncmp(w[i].w,L"Arial",5)&&wcscmp(w[i].w,L"Arabic Transparent")) bad++; if(!lstrcmpW(w[i].w,L"Arial")) ncs0++; }
  EXPECT_EQ(bad,0); EXPECT_EQ(ncs0,4);     /* the base family name once per style */
  static const WCHAR *ST[]={L"Regular",L"Bold",L"Bold Italic",L"Italic"}; int si=0,back=0;
  for(int i=0;i<n;i++){ while(si<4&&lstrcmpW(w[i].style,ST[si])) si++; if(si>=4){ back++; break; } }
  EXPECT_EQ(back,0);
  int na; run_efontsa(dc,L"Arial"); na=g_n; EXPECT_EQ(na,n);
  U(DeleteDC)(dc);
}
TEST(feo_enumfonts_unknown_face){
  HDC dc=mkdc(); REQUIRE(dc);
  int r=run_efontsw(dc,L"NoSuchFaceX"); EXPECT_EQ(g_n,0); EXPECT_EQ(r,1);
  r=run_efontsa(dc,L"NoSuchFaceX"); EXPECT_EQ(g_n,0); EXPECT_EQ(r,1);
  r=run_efw(dc,L"NoSuchFaceX"); EXPECT_EQ(g_n,0); EXPECT_EQ(r,1);
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ ENUMLOGFONT / NEWTEXTMETRIC content */
TEST(feo_callback_structures){
  HDC dc=mkdc(); REQUIRE(dc);
  run_exw(dc,L"Arial",ANSI_CHARSET); if(g_n!=4){ t_skip("Arial styles not as expected (n=%d)",g_n); U(DeleteDC)(dc); return; }
  /* TrueType faces report a NEWTEXTMETRIC with nonzero ntmFlags; ntmSizeEM/ntmCellHeight/ntmAvgWidth are filled */
  struct { NEWTEXTMETRICEXW x; int ok; } nm; memset(&nm,0,sizeof nm);
  static NEWTEXTMETRICEXW seen[8]; static int ns, bad;
  ns=0; bad=0;
  int CALLBACK cb(const LOGFONTW *lf,const TEXTMETRICW *tm,DWORD type,LPARAM lp){
    if(ns<8 && (type&TRUETYPE_FONTTYPE)) memcpy(&seen[ns++],tm,sizeof(NEWTEXTMETRICW));
    const ENUMLOGFONTEXW *x=(const ENUMLOGFONTEXW*)lf;
    if(!x->elfFullName[0]||!x->elfStyle[0]) bad++;
    if(x->elfScript[0]==0) bad+=100;        /* script name ("Western") */
    if(tm->tmHeight<=0||tm->tmAveCharWidth<=0||tm->tmMaxCharWidth<=0) bad+=1000;
    return 1; };
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfCharSet=ANSI_CHARSET; lstrcpyW(lf.lfFaceName,L"Arial");
  U(EnumFontFamiliesExW)(dc,&lf,cb,0,0);
  EXPECT_EQ(ns,4); EXPECT_EQ(bad,0);
  for(int i=0;i<ns;i++){ const NEWTEXTMETRICW *t=(const NEWTEXTMETRICW*)&seen[i]; CHECK(t->ntmFlags!=0); CHECK(t->ntmSizeEM>0); CHECK(t->ntmCellHeight>0); CHECK(t->ntmAvgWidth>0); }
  /* the lfWeight/lfItalic of each enumerated LOGFONT agree with the style name */
  static const int WT[]={FW_NORMAL,FW_BOLD,FW_BOLD,FW_NORMAL}; static const int IT[]={0,0,1,1}; static int wbad; wbad=0; static int ii;
  ii=0;
  int CALLBACK cb2(const LOGFONTW *l,const TEXTMETRICW *tm,DWORD type,LPARAM lp){ if(ii<4){ if(l->lfWeight!=WT[ii]||(!!l->lfItalic)!=IT[ii]) wbad++; ii++; } return 1; };
  U(EnumFontFamiliesExW)(dc,&lf,cb2,0,0); EXPECT_EQ(ii,4); EXPECT_EQ(wbad,0);
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ CreateFont* round trips */
static HFONT cf_a(const char *face,int q,int cs,int wt){ return U(CreateFontA)(-13,5,0,0,wt,1,1,0,cs,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,q,VARIABLE_PITCH|FF_SWISS,face); }
TEST(feo_createfonta_round_trip){
  HFONT f=U(CreateFontA)(-13,5,30,40,FW_BOLD,1,1,1,ANSI_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,VARIABLE_PITCH|FF_SWISS,"Tahoma");
  CHECK(f!=NULL); if(!f) return;
  LOGFONTA a; memset(&a,0xcc,sizeof a); EXPECT_EQ(U(GetObjectA)(f,sizeof a,&a),sizeof a);
  EXPECT_EQ(a.lfHeight,-13); EXPECT_EQ(a.lfWidth,5); EXPECT_EQ(a.lfEscapement,30); EXPECT_EQ(a.lfOrientation,40); EXPECT_EQ(a.lfWeight,FW_BOLD);
  EXPECT_EQ(a.lfItalic,1); EXPECT_EQ(a.lfUnderline,1); EXPECT_EQ(a.lfStrikeOut,1); EXPECT_EQ(a.lfCharSet,ANSI_CHARSET); EXPECT_EQ(a.lfOutPrecision,OUT_TT_PRECIS);
  EXPECT_EQ(a.lfClipPrecision,CLIP_DEFAULT_PRECIS); EXPECT_EQ(a.lfQuality,ANTIALIASED_QUALITY); EXPECT_EQ(a.lfPitchAndFamily,VARIABLE_PITCH|FF_SWISS);
  EXPECT_STR(a.lfFaceName,"Tahoma");
  /* the same request through CreateFontIndirectA / W / CreateFontW yields the same stored LOGFONT */
  LOGFONTA ia=a; HFONT g=U(CreateFontIndirectA)(&ia); LOGFONTA b; memset(&b,0xcc,sizeof b); U(GetObjectA)(g,sizeof b,&b); CHECK(!memcmp(&a,&b,offsetof(LOGFONTA,lfFaceName)) && !strcmp(a.lfFaceName,b.lfFaceName));
  HFONT h=U(CreateFontW)(-13,5,30,40,FW_BOLD,1,1,1,ANSI_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,VARIABLE_PITCH|FF_SWISS,L"Tahoma");
  LOGFONTW w,w2; memset(&w,0xcc,sizeof w); memset(&w2,0xdd,sizeof w2); U(GetObjectW)(h,sizeof w,&w);
  LOGFONTW iw; memset(&iw,0,sizeof iw); iw.lfHeight=-13; iw.lfWidth=5; iw.lfEscapement=30; iw.lfOrientation=40; iw.lfWeight=FW_BOLD; iw.lfItalic=1; iw.lfUnderline=1; iw.lfStrikeOut=1;
  iw.lfCharSet=ANSI_CHARSET; iw.lfOutPrecision=OUT_TT_PRECIS; iw.lfClipPrecision=CLIP_DEFAULT_PRECIS; iw.lfQuality=ANTIALIASED_QUALITY; iw.lfPitchAndFamily=VARIABLE_PITCH|FF_SWISS; lstrcpyW(iw.lfFaceName,L"Tahoma");
  HFONT k=U(CreateFontIndirectW)(&iw); U(GetObjectW)(k,sizeof w2,&w2); CHECK(w.lfHeight==w2.lfHeight&&w.lfWidth==w2.lfWidth&&w.lfEscapement==w2.lfEscapement&&w.lfOrientation==w2.lfOrientation&&w.lfWeight==w2.lfWeight&&w.lfItalic==w2.lfItalic&&w.lfUnderline==w2.lfUnderline&&w.lfStrikeOut==w2.lfStrikeOut&&w.lfCharSet==w2.lfCharSet&&w.lfOutPrecision==w2.lfOutPrecision&&w.lfClipPrecision==w2.lfClipPrecision&&w.lfQuality==w2.lfQuality&&w.lfPitchAndFamily==w2.lfPitchAndFamily&&!lstrcmpW(w.lfFaceName,w2.lfFaceName));
  EXPECT_WSTR(w.lfFaceName,L"Tahoma"); EXPECT_EQ(w.lfEscapement,30); EXPECT_EQ(w.lfOrientation,40); EXPECT_EQ(w.lfStrikeOut,1);
  /* selecting it: GetTextFaceA names the face that matched, GetTextMetrics reflects the request */
  HDC dc=mkdc(); HFONT o=(HFONT)U(SelectObject)(dc,f); char face[64]; memset(face,0,sizeof face);
  int n=U(GetTextFaceA)(dc,sizeof face,face); EXPECT_STR(face,"Tahoma"); EXPECT_EQ(n,6);
  TEXTMETRICA tm; CHECK(U(GetTextMetricsA)(dc,&tm)); EXPECT_EQ(tm.tmWeight,FW_BOLD); CHECK(tm.tmItalic); CHECK(tm.tmUnderlined); CHECK(tm.tmStruckOut);
  U(SelectObject)(dc,o); U(DeleteDC)(dc); U(DeleteObject)(f); U(DeleteObject)(g); U(DeleteObject)(h); U(DeleteObject)(k);
}
TEST(feo_createfonta_quality_charset_weight_passthrough){
  static const int Q[]={DEFAULT_QUALITY,DRAFT_QUALITY,PROOF_QUALITY,NONANTIALIASED_QUALITY,ANTIALIASED_QUALITY,CLEARTYPE_QUALITY,5+1};
  for(unsigned i=0;i<sizeof Q/sizeof*Q;i++){ HFONT f=cf_a("Arial",Q[i],DEFAULT_CHARSET,FW_DONTCARE); LOGFONTA a; memset(&a,0xcc,sizeof a); U(GetObjectA)(f,sizeof a,&a);
    CHECKF(a.lfQuality==Q[i],"quality %d stored as %d",Q[i],a.lfQuality); CHECKF(a.lfCharSet==DEFAULT_CHARSET,"charset stored as %d",a.lfCharSet); CHECKF(a.lfWeight==FW_DONTCARE,"weight stored as %d",a.lfWeight);
    U(DeleteObject)(f); }
  /* the stored LOGFONT is not rewritten to the charset of the matched face */
  HFONT f=cf_a("Symbol",DEFAULT_QUALITY,ANSI_CHARSET,FW_NORMAL); LOGFONTA a; U(GetObjectA)(f,sizeof a,&a); EXPECT_EQ(a.lfCharSet,ANSI_CHARSET);
  HDC dc=mkdc(); HFONT o=(HFONT)U(SelectObject)(dc,f); char face[64]; U(GetTextFaceA)(dc,64,face); TEXTMETRICA tm; U(GetTextMetricsA)(dc,&tm);
  t_note("Symbol requested with ANSI_CHARSET: face=%s tmCharSet=%d",face,tm.tmCharSet);
  U(SelectObject)(dc,o); U(DeleteDC)(dc); U(DeleteObject)(f);
  /* over-long face name is truncated to LF_FACESIZE-1 characters with a terminator */
  char longn[64]; memset(longn,'A',63); longn[63]=0; f=U(CreateFontA)(-12,0,0,0,0,0,0,0,0,0,0,0,0,longn); memset(&a,0xcc,sizeof a); U(GetObjectA)(f,sizeof a,&a);
  EXPECT_EQ(strlen(a.lfFaceName),LF_FACESIZE-1); CHECK(!strncmp(a.lfFaceName,longn,LF_FACESIZE-1)); U(DeleteObject)(f);
  /* NULL face name: accepted, stored empty */
  f=U(CreateFontA)(-12,0,0,0,0,0,0,0,0,0,0,0,0,NULL); CHECK(f!=NULL); if(f){ memset(&a,0xcc,sizeof a); U(GetObjectA)(f,sizeof a,&a); EXPECT_EQ(a.lfFaceName[0],0); U(DeleteObject)(f); }
}
TEST(feo_createfont_unknown_face_fallback){
  HDC dc=mkdc(); REQUIRE(dc);
  HFONT f=cf_a("NoSuchFaceX",DEFAULT_QUALITY,DEFAULT_CHARSET,FW_NORMAL); CHECK(f!=NULL);
  HFONT o=(HFONT)U(SelectObject)(dc,f); char face[64]=""; U(GetTextFaceA)(dc,64,face);
  CHECKF(strcmp(face,"NoSuchFaceX")!=0 && face[0],"matched face %s",face);
  WCHAR fw[64]; a2w(face,fw,64); CHECKF(exists_face(dc,fw),"matched face %s is an installed family",face);
  t_note("unknown face NoSuchFaceX (-13, DEFAULT_CHARSET, VARIABLE_PITCH|FF_SWISS) mapped to %s",face);
  { LOGFONTA l2; memset(&l2,0,sizeof l2); l2.lfHeight=-16; lstrcpyA(l2.lfFaceName,"NoSuchFaceX"); HFONT f2=U(CreateFontIndirectA)(&l2); HFONT o2=(HFONT)U(SelectObject)(dc,f2); char fc2[64]=""; U(GetTextFaceA)(dc,64,fc2);
    t_note("unknown face NoSuchFaceX (-16, DEFAULT_CHARSET, no pitch/family) mapped to %s",fc2); U(SelectObject)(dc,o2); U(DeleteObject)(f2); }
  LOGFONTA a; U(GetObjectA)(f,sizeof a,&a); EXPECT_STR(a.lfFaceName,"NoSuchFaceX");     /* the request is kept, not the match */
  U(SelectObject)(dc,o); U(DeleteObject)(f); U(DeleteDC)(dc);
}
TEST(feo_createfontindirectex){
  typedef HFONT (WINAPI *CFIEA)(const ENUMLOGFONTEXDVA*); typedef HFONT (WINAPI *CFIEW)(const ENUMLOGFONTEXDVW*);
  CFIEA ea=(CFIEA)t_proc("CreateFontIndirectExA"); CFIEW ew=(CFIEW)t_proc("CreateFontIndirectExW"); REQUIRE(ea); REQUIRE(ew);
  ENUMLOGFONTEXDVA x; memset(&x,0,sizeof x);
  x.elfEnumLogfontEx.elfLogFont.lfHeight=-16; x.elfEnumLogfontEx.elfLogFont.lfWeight=FW_BOLD; x.elfEnumLogfontEx.elfLogFont.lfItalic=1; x.elfEnumLogfontEx.elfLogFont.lfCharSet=ANSI_CHARSET;
  lstrcpyA((char*)x.elfEnumLogfontEx.elfLogFont.lfFaceName,"Arial"); lstrcpyA((char*)x.elfEnumLogfontEx.elfFullName,"Arial Bold Italic"); lstrcpyA((char*)x.elfEnumLogfontEx.elfStyle,"Bold Italic"); lstrcpyA((char*)x.elfEnumLogfontEx.elfScript,"Western");
  x.elfDesignVector.dvReserved=STAMP_DESIGNVECTOR; x.elfDesignVector.dvNumAxes=0;
  HFONT f=ea(&x); CHECK(f!=NULL); if(!f) return;
  LOGFONTA a; memset(&a,0xcc,sizeof a); EXPECT_EQ(U(GetObjectA)(f,sizeof a,&a),sizeof a);
  EXPECT_STR(a.lfFaceName,"Arial"); EXPECT_EQ(a.lfHeight,-16); EXPECT_EQ(a.lfWeight,FW_BOLD); EXPECT_EQ(a.lfItalic,1); EXPECT_EQ(a.lfCharSet,ANSI_CHARSET);
  ENUMLOGFONTEXA e; memset(&e,0xcc,sizeof e); EXPECT_EQ(U(GetObjectA)(f,sizeof e,&e),sizeof e); EXPECT_STR((char*)e.elfLogFont.lfFaceName,"Arial");
  t_note("GetObjectA(ENUMLOGFONTEXA) after CreateFontIndirectExA: full=\"%s\" style=\"%s\" script=\"%s\"",(char*)e.elfFullName,(char*)e.elfStyle,(char*)e.elfScript);
  HDC dc=mkdc(); HFONT o=(HFONT)U(SelectObject)(dc,f); char face[64]; U(GetTextFaceA)(dc,64,face); EXPECT_STR(face,"Arial");
  TEXTMETRICA tm; U(GetTextMetricsA)(dc,&tm); EXPECT_EQ(tm.tmWeight,FW_BOLD); CHECK(tm.tmItalic); EXPECT_EQ(tm.tmHeight-tm.tmInternalLeading,16);
  U(SelectObject)(dc,o);
  /* the wide entry point stores the same LOGFONT */
  ENUMLOGFONTEXDVW wx; memset(&wx,0,sizeof wx); LOGFONTW *lw=&wx.elfEnumLogfontEx.elfLogFont; lw->lfHeight=-16; lw->lfWeight=FW_BOLD; lw->lfItalic=1; lw->lfCharSet=ANSI_CHARSET; lstrcpyW(lw->lfFaceName,L"Arial");
  wx.elfDesignVector.dvReserved=STAMP_DESIGNVECTOR; HFONT g=ew(&wx); CHECK(g!=NULL);
  if(g){ LOGFONTW w; memset(&w,0xcc,sizeof w); EXPECT_EQ(U(GetObjectW)(g,sizeof w,&w),sizeof w); EXPECT_WSTR(w.lfFaceName,L"Arial"); EXPECT_EQ(w.lfHeight,-16); EXPECT_EQ(w.lfWeight,FW_BOLD);
    o=(HFONT)U(SelectObject)(dc,g); WCHAR fw[64]; U(GetTextFaceW)(dc,64,fw); EXPECT_WSTR(fw,L"Arial"); U(SelectObject)(dc,o); U(DeleteObject)(g); }
  /* a font selected through this path is deleted like any other */
  CHECK(U(DeleteObject)(f)); U(DeleteDC)(dc);
  SetLastError(0); HFONT nf=ea(NULL); CHECK(nf==NULL);
  t_note("CreateFontIndirectExA(NULL)=%p err=%lu",nf,GetLastError());
}
TEST(feo_createfontindirectex_variable_axis_NOTE){
  typedef HFONT (WINAPI *CFIEW)(const ENUMLOGFONTEXDVW*); CFIEW ew=(CFIEW)t_proc("CreateFontIndirectExW"); REQUIRE(ew);
  HDC dc=mkdc(); REQUIRE(dc);
  if(!exists_face(dc,L"Bahnschrift")){ t_skip("Bahnschrift (variable font) not installed"); U(DeleteDC)(dc); return; }
  for(int pass=0;pass<2;pass++){
    ENUMLOGFONTEXDVW wx; memset(&wx,0,sizeof wx); LOGFONTW *lw=&wx.elfEnumLogfontEx.elfLogFont; lw->lfHeight=-32; lw->lfWeight=FW_NORMAL; lw->lfCharSet=ANSI_CHARSET; lstrcpyW(lw->lfFaceName,L"Bahnschrift");
    wx.elfDesignVector.dvReserved=STAMP_DESIGNVECTOR; if(pass){ wx.elfDesignVector.dvNumAxes=1; wx.elfDesignVector.dvValues[0]=700; }
    HFONT f=ew(&wx); HFONT o=(HFONT)U(SelectObject)(dc,f); TEXTMETRICW tm; U(GetTextMetricsW)(dc,&tm); ABC abc; SIZE sz; U(GetTextExtentPoint32W)(dc,L"Hamburgefonstiv",15,&sz);
    t_note("Bahnschrift design vector axes=%d: tmWeight=%ld extent=%ld",pass,tm.tmWeight,sz.cx); U(SelectObject)(dc,o); U(DeleteObject)(f); }
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ SetMapperFlags */
#define SMF_EQ(dc_,fl_,want_) do{ DWORD r_=U(SetMapperFlags)(dc_,fl_); t_check(r_==(DWORD)(want_),__FILE__,__LINE__,"SetMapperFlags(" #dc_ "," #fl_ ")==" #want_,"got 0x%lx want 0x%lx",r_,(DWORD)(want_)); }while(0)
TEST(feo_setmapperflags_state){
  HDC dc=mkdc(); REQUIRE(dc);
  SMF_EQ(dc,0,0);                           /* initial state */
  SMF_EQ(dc,ASPECT_FILTERING,0);            /* returns the previous flags */
  SMF_EQ(dc,ASPECT_FILTERING,ASPECT_FILTERING);
  SetLastError(0); DWORD r=U(SetMapperFlags)(dc,2); EXPECT_EQ(r,GDI_ERROR); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  SMF_EQ(dc,0xffffffff,GDI_ERROR);
  SMF_EQ(dc,3,GDI_ERROR);
  SMF_EQ(dc,ASPECT_FILTERING,ASPECT_FILTERING);   /* rejected values did not change the state */
  SMF_EQ(dc,0,ASPECT_FILTERING);
  SMF_EQ(dc,0,0);
  SetLastError(0); SMF_EQ(NULL,ASPECT_FILTERING,GDI_ERROR);
  t_note("SetMapperFlags(NULL,1): GetLastError=%lu",GetLastError());
  /* the flag belongs to the DC */
  HDC d2=mkdc(); SMF_EQ(dc,ASPECT_FILTERING,0); SMF_EQ(d2,0,0); SMF_EQ(dc,0,ASPECT_FILTERING);
  SMF_EQ(dc,0,0);
  U(DeleteDC)(d2); U(DeleteDC)(dc);
}
TEST(feo_setmapperflags_aspect_filtering){
  HDC dc=mkdc(); REQUIRE(dc);
  int ax=U(GetDeviceCaps)(dc,ASPECTX), ay=U(GetDeviceCaps)(dc,ASPECTY);
  struct { const WCHAR *face; int h; } C[]={{L"MS Sans Serif",-20},{L"Courier",-20},{L"Terminal",-20},{L"Small Fonts",-14}};
  int unfiltered_off_aspect=0;
  for(int k=0;k<4;k++){
    int asp[2],tmh[2];
    for(int fl=0;fl<2;fl++){
      U(SetMapperFlags)(dc,fl?ASPECT_FILTERING:0);
      LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=C[k].h; lf.lfCharSet=DEFAULT_CHARSET; lf.lfOutPrecision=OUT_RASTER_PRECIS; lstrcpyW(lf.lfFaceName,C[k].face);
      HFONT f=U(CreateFontIndirectW)(&lf); HFONT o=(HFONT)U(SelectObject)(dc,f); TEXTMETRICW tm; U(GetTextMetricsW)(dc,&tm);
      asp[fl]=tm.tmDigitizedAspectX; tmh[fl]=tm.tmHeight;
      t_note("flags=%d %s h=%d -> tmHeight=%ld digitized aspect %ld/%ld",fl,n8(C[k].face),C[k].h,tm.tmHeight,tm.tmDigitizedAspectX,tm.tmDigitizedAspectY);
      if(fl) CHECKF((long)tm.tmDigitizedAspectX*ay==(long)tm.tmDigitizedAspectY*ax,"%s: filtered aspect %ld/%ld does not match device %d/%d",n8(C[k].face),tm.tmDigitizedAspectX,tm.tmDigitizedAspectY,ax,ay);
      if(!fl && (long)tm.tmDigitizedAspectX*ay!=(long)tm.tmDigitizedAspectY*ax) unfiltered_off_aspect++;
      U(SelectObject)(dc,o); U(DeleteObject)(f);
    }
    U(SetMapperFlags)(dc,0);
  }
  /* MS Sans Serif -20 re-selected with and without the flag: NOTE only (the strike chosen depends on mapper history, see EVIDENCE.md) */
  { LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=-20; lf.lfCharSet=DEFAULT_CHARSET; lf.lfOutPrecision=OUT_RASTER_PRECIS; lstrcpyW(lf.lfFaceName,L"MS Sans Serif");
    TEXTMETRICW t0,t1; HFONT f=U(CreateFontIndirectW)(&lf); HFONT o=(HFONT)U(SelectObject)(dc,f);
    U(SetMapperFlags)(dc,0); U(GetTextMetricsW)(dc,&t0); U(SelectObject)(dc,o); o=(HFONT)U(SelectObject)(dc,f);
    U(SetMapperFlags)(dc,ASPECT_FILTERING); U(SelectObject)(dc,o); o=(HFONT)U(SelectObject)(dc,f); U(GetTextMetricsW)(dc,&t1);
    t_note("reselect: flags=0 -> %ld/%ld h=%ld, flags=1 -> %ld/%ld h=%ld",t0.tmDigitizedAspectX,t0.tmDigitizedAspectY,t0.tmHeight,t1.tmDigitizedAspectX,t1.tmDigitizedAspectY,t1.tmHeight);
    U(SetMapperFlags)(dc,0); U(SelectObject)(dc,o); U(DeleteObject)(f); }
  CHECK(U(SetMapperFlags)(dc,0)==0);   /* state reset */
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ GetTextFaceAliasW and the font-link exports (undocumented) */
typedef int   (WINAPI *AliasW_t)(HDC,int,LPWSTR);
typedef BOOL  (WINAPI *AnyLinked_t)(void);
typedef BOOL  (WINAPI *IsLinked_t)(HDC);
typedef DWORD (WINAPI *Assoc_t)(HDC);
typedef DWORD (WINAPI *QAssoc_t)(void);
static HFONT sel_face(HDC dc,const char *face,HFONT *old){ LOGFONTA lf; memset(&lf,0,sizeof lf); lf.lfHeight=-16; lf.lfCharSet=DEFAULT_CHARSET; lstrcpyA(lf.lfFaceName,face);
  HFONT f=U(CreateFontIndirectA)(&lf); *old=(HFONT)U(SelectObject)(dc,f); return f; }
TEST(feo_gettextfacealiasw){
  AliasW_t al=(AliasW_t)t_proc("GetTextFaceAliasW"); REQUIRE(al);
  HDC dc=mkdc(); REQUIRE(dc);
  static const char *F[]={"MS Shell Dlg","MS Shell Dlg 2","Helv","Tms Rmn","MS Sans Serif","Arial","Segoe UI","Courier","Symbol","NoSuchFaceX"};
  for(unsigned k=0;k<sizeof F/sizeof*F;k++){
    HFONT o,f=sel_face(dc,F[k],&o); WCHAR a[64],g[64]; memset(a,0xcc,sizeof a); memset(g,0xdd,sizeof g);
    int ra=al(dc,64,a), rg=U(GetTextFaceW)(dc,64,g);
    CHECKF(ra==rg && !lstrcmpW(a,g),"%s: alias \"%s\"(%d) vs GetTextFaceW \"%s\"(%d)",F[k],n8(a),ra,n8(g),rg);
    EXPECT_EQ(ra,lstrlenW(a)+1);
    char sub[128]; regsub(F[k],sub,sizeof sub);
    t_note("%s: GetTextFaceAliasW=\"%s\" GetTextFaceW=\"%s\" FontSubstitutes=\"%s\"",F[k],n8(a),n8(g),sub);
    U(SelectObject)(dc,o); U(DeleteObject)(f);
  }
  /* an explicitly requested alias is reported under the requested name, not the substitution target (observed; not the registry target) */
  { HFONT o,f=sel_face(dc,"MS Shell Dlg",&o); WCHAR a[64]; al(dc,64,a); char sub[128]; regsub("MS Shell Dlg",sub,sizeof sub); WCHAR sw[128]; a2w(sub,sw,128);
    CHECK(!lstrcmpW(a,L"MS Shell Dlg")); if(sub[0]) CHECK(lstrcmpiW(a,sw)!=0);
    U(SelectObject)(dc,o); U(DeleteObject)(f); }
  /* argument handling (shared with GetTextFaceW) */
  HFONT o,f=sel_face(dc,"Arial",&o); WCHAR b[64]; SetLastError(0); EXPECT_EQ(al(dc,0,b),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  EXPECT_EQ(al(dc,0,NULL),U(GetTextFaceW)(dc,0,NULL)); EXPECT_EQ(al(dc,64,NULL),6);       /* "Arial" + NUL */
  memset(b,0xcc,sizeof b); EXPECT_EQ(al(dc,3,b),U(GetTextFaceW)(dc,3,NULL)>=3?3:0); CHECK(b[2]==0); CHECK(b[0]==L'A'&&b[1]==L'r');
  U(SelectObject)(dc,o); U(DeleteObject)(f);
  SetLastError(0); EXPECT_EQ(al(NULL,64,b),U(GetTextFaceW)(NULL,64,b)); 
  U(DeleteDC)(dc);
}
TEST(feo_font_link_and_assoc_exports){
  AnyLinked_t any=(AnyLinked_t)t_proc("AnyLinkedFonts"); IsLinked_t fil=(IsLinked_t)t_proc("FontIsLinked"); Assoc_t gfa=(Assoc_t)t_proc("GetFontAssocStatus"); QAssoc_t qfa=(QAssoc_t)t_proc("QueryFontAssocStatus");
  REQUIRE(any); REQUIRE(fil); REQUIRE(gfa); REQUIRE(qfa);
  HDC dc=mkdc(); REQUIRE(dc);
  /* SystemLink is present in the registry of the standard image (recorded regsub FontLink\SystemLink=1) */
  HKEY k; int haskey=RegOpenKeyExA(HKEY_LOCAL_MACHINE,"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink",0,KEY_READ,&k)==ERROR_SUCCESS; if(haskey) RegCloseKey(k);
  BOOL a1=any(), a2=any(); CHECK(a1==a2);
  t_note("AnyLinkedFonts=%d (SystemLink key %s)",(int)a1,haskey?"present":"absent");
  if(haskey) CHECK(a1!=0);
  /* decompiled: GetFontAssocStatus(NULL) returns 0 without a kernel call */
  EXPECT_EQ(gfa(NULL),0);
  DWORD q1=qfa(), q2=qfa(); CHECK(q1==q2); t_note("QueryFontAssocStatus=0x%lx",q1);
  static const struct { const char *face; int want; } F[]={{"Arial",0},{"Courier",0},{"Symbol",0},{"Segoe UI",1},{"MS Gothic",1},{"SimSun",1},{"Microsoft YaHei",1},{"Malgun Gothic",1}};
  for(unsigned i=0;i<sizeof F/sizeof*F;i++){
    HFONT o,f=sel_face(dc,F[i].face,&o); WCHAR got[64]; U(GetTextFaceW)(dc,64,got); WCHAR req[64]; a2w(F[i].face,req,64);
    if(lstrcmpiW(got,req)){ t_skip("%s not installed (matched %s)",F[i].face,n8(got)); U(SelectObject)(dc,o); U(DeleteObject)(f); continue; }
    BOOL l1=fil(dc), l2=fil(dc); DWORD as=gfa(dc);
    t_note("%s: FontIsLinked=%d GetFontAssocStatus=0x%lx",F[i].face,(int)l1,as);
    CHECK(l1==l2); CHECKF((l1!=0)==F[i].want,"%s: FontIsLinked=%d want %d",F[i].face,(int)l1,F[i].want);
    EXPECT_EQ(as,gfa(dc));
    U(SelectObject)(dc,o); U(DeleteObject)(f);
  }
  /* the association status is repeatable on the same DC */
  CHECK(gfa(dc)==gfa(dc));
  SetLastError(0); BOOL bad=fil(NULL); t_note("FontIsLinked(NULL)=%d err=%lu",(int)bad,GetLastError());
  U(DeleteDC)(dc);
}

/* ------------------------------------------------------------------ SetFontEnumeration (undocumented): isolated in a child process, NOTE only */
CHILD(feo_setenum){
  typedef DWORD (WINAPI *SFE)(DWORD); SFE sfe=(SFE)t_proc("SetFontEnumeration");
  HDC dc=mkdc(); DWORD arg=(DWORD)strtoul(argv[1],NULL,0);
  reset(0,1,0); U(EnumFontFamiliesW)(dc,NULL,(FONTENUMPROCW)cbW,g_lp); int before=g_n;
  SetLastError(0); DWORD r=sfe?sfe(arg):0xdeadbeef; DWORD err=GetLastError();
  reset(0,1,0); U(EnumFontFamiliesW)(dc,NULL,(FONTENUMPROCW)cbW,g_lp); int after=g_n;
  FILE *f=fopen(argv[2],"w"); if(f){ fprintf(f,"before=%d ret=0x%lx err=%lu after=%d",before,r,err,after); fclose(f);} return sfe?0:3;
}
TEST(feo_setfontenumeration_NOTE){
  REQUIRE(t_proc("SetFontEnumeration"));
  static const char *args[]={"0","1"};
  for(int i=0;i<2;i++){ char p[MAX_PATH],ex[MAX_PATH+16]; t_tmpfile(p,sizeof p,"se.txt"); snprintf(ex,sizeof ex,"%s \"%s\"",args[i],p);
    DWORD c=t_run_child("feo_setenum",ex,60000); char b[200]="(no result)"; FILE *f=fopen(p,"r"); if(f){ if(!fgets(b,sizeof b,f)) b[0]=0; fclose(f);} remove(p);
    CHECKF(c==0,"child exit %lx",c); t_note("SetFontEnumeration(%s) in a child process: %s",args[i],b); }
  /* the parent's enumeration is untouched */
  HDC dc=mkdc(); int before; run_efw(dc,NULL); before=g_n; run_efw(dc,NULL); EXPECT_EQ(g_n,before); U(DeleteDC)(dc);
}
