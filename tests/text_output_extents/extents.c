#include "toe_common.h"
/* GetTextExtentPoint{A,W,32A,32W,I}, GetTextExtentExPoint{A,W,I}, GdiGetCharDimensions on Arial 18 px (no window).
 * Sources: see EVIDENCE.md in this directory. */

static const char *k_str[]={"Hello","The quick brown fox","AVAWAY To","iiiiiiii","WWWW","MM  ii  jj","0123456789"," a b "};
#define NSTR ((int)(sizeof k_str/sizeof*k_str))

/* the extent of a string is the sum of the per-character advances; A, W, 32A, 32W and I agree for ASCII text */
TEST(extent_variants_agree){
  REQUIRE(U(GetTextExtentPointA)); REQUIRE(U(GetTextExtentPointW)); REQUIRE(U(GetTextExtentPoint32A));
  REQUIRE(U(GetTextExtentPoint32W)); REQUIRE(U(GetTextExtentPointI)); REQUIRE(U(GetGlyphIndicesW));
  E e; E_OPEN(e,64,64,DEFAULT_QUALITY,18);
  for(int i=0;i<NSTR;i++){
    const char *s=k_str[i]; WCHAR w[64]; widen(s,w,64); int n=(int)strlen(s);
    SIZE a={-1,-1},ww={-2,-2},a32={-3,-3},w32={-4,-4},gi={-5,-5};
    CHECK(U(GetTextExtentPointA)(e.dc,s,n,&a));
    CHECK(U(GetTextExtentPointW)(e.dc,w,n,&ww));
    CHECK(U(GetTextExtentPoint32A)(e.dc,s,n,&a32));
    CHECK(U(GetTextExtentPoint32W)(e.dc,w,n,&w32));
    WORD g[64]; DWORD r=U(GetGlyphIndicesW)(e.dc,w,n,g,GGI_MARK_NONEXISTING_GLYPHS);
    int allg=(r!=GDI_ERROR); for(int k=0;allg&&k<n;k++) if(g[k]==0xFFFF) allg=0;
    CHECKF(allg,"%s: glyph indices missing",s);
    CHECK(U(GetTextExtentPointI)(e.dc,g,n,&gi));
    CHECKF(a.cx==w32.cx&&ww.cx==w32.cx&&a32.cx==w32.cx&&gi.cx==w32.cx,"%s: cx A %ld W %ld 32A %ld 32W %ld I %ld",s,a.cx,ww.cx,a32.cx,w32.cx,gi.cx);
    CHECKF(a.cy==w32.cy&&ww.cy==w32.cy&&a32.cy==w32.cy&&gi.cy==w32.cy,"%s: cy A %ld W %ld 32A %ld 32W %ld I %ld",s,a.cy,ww.cy,a32.cy,w32.cy,gi.cy);
    CHECKF(w32.cy==e.tm.tmHeight,"%s: cy %ld tmHeight %ld",s,w32.cy,e.tm.tmHeight);
    long sum=sumw(e.dc,w,n);
    CHECKF(w32.cx==sum,"%s: extent %ld != sum of GetCharWidth32W %ld",s,w32.cx,sum);
    /* glyph-index advances */
    INT gw[64]; long gs=0; for(int k=0;k<n;k++){ gw[k]=0; U(GetCharWidthI)(e.dc,g[k],1,NULL,&gw[k]); gs+=gw[k]; }
    CHECKF(gs==w32.cx,"%s: sum of GetCharWidthI %ld != extent %ld",s,gs,w32.cx);
  }
  e_close(&e);
}

TEST(extent_empty_and_concat){
  E e; E_OPEN(e,64,64,DEFAULT_QUALITY,18);
  SIZE z={9,9}; CHECK(U(GetTextExtentPointA)(e.dc,"x",0,&z)); EQV(z.cx,0); EQV(z.cy,0);
  z.cx=z.cy=9; WORD g=0; CHECK(U(GetTextExtentPointI)(e.dc,&g,0,&z)); EQV(z.cx,0); EQV(z.cy,0);
  /* without kerning the extent of a concatenation is the sum of the extents of its parts */
  SIZE a,b,ab; U(GetTextExtentPoint32A)(e.dc,"AV",2,&a); U(GetTextExtentPoint32A)(e.dc,"AW",2,&b);
  U(GetTextExtentPoint32A)(e.dc,"AVAW",4,&ab);
  CHECKF(ab.cx==a.cx+b.cx,"AV %ld + AW %ld != AVAW %ld",a.cx,b.cx,ab.cx);
  e_close(&e);
}

static void ex_one(E *e, const char *s, int maxext){
  WCHAR w[64]; widen(s,w,64); int n=(int)strlen(s);
  INT dx[64]; INT dxa[64]; INT dxi[64]; SIZE sz, szp; int fit=-99, fita=-99, fiti=-99; SIZE sza, szi;
  memset(dx,0x55,sizeof dx);
  BOOL r=U(GetTextExtentExPointW)(e->dc,w,n,maxext,&fit,dx,&sz);
  CHECKF(r,"%s max %d: GetTextExtentExPointW failed",s,maxext);
  U(GetTextExtentPoint32W)(e->dc,w,n,&szp);
  CHECKF(sz.cx==szp.cx&&sz.cy==szp.cy,"%s max %d: ExPoint size %ldx%ld vs %ldx%ld",s,maxext,sz.cx,sz.cy,szp.cx,szp.cy);
  long run=0; int want=0, mono=1, pref=1;
  for(int i=0;i<n;i++){ INT cw=0; U(GetCharWidth32W)(e->dc,w[i],w[i],&cw); run+=cw; if(run<=maxext) want=i+1; if(i<fit){ if(dx[i]!=run) pref=0; if(i&&dx[i]<dx[i-1]) mono=0; } }
  CHECKF(fit==want,"%s max %d: nFit %d want %d",s,maxext,fit,want);
  /* only the first nFit elements of alpDx are defined (documented: one per character that fits) */
  CHECKF(mono,"%s max %d: alpDx not monotonic",s,maxext);
  CHECKF(pref,"%s max %d: alpDx[0..nFit) is not the prefix sum of GetCharWidth32W",s,maxext);
  if(fit==n&&n) CHECKF(dx[n-1]==sz.cx,"%s: last alpDx %d != cx %ld",s,dx[n-1],sz.cx);
  if(fit<n&&fit<64&&dx[fit]!=0x55555555) t_note("%s max %d: alpDx[nFit] was written (%d)",s,maxext,dx[fit]);
  /* A variant agrees with W */
  memset(dxa,0x55,sizeof dxa);
  CHECK(U(GetTextExtentExPointA)(e->dc,s,n,maxext,&fita,dxa,&sza));
  CHECKF(fita==fit&&sza.cx==sz.cx&&sza.cy==sz.cy&&!memcmp(dxa,dx,fit*sizeof(INT)),"%s max %d: A variant differs (nFit %d vs %d)",s,maxext,fita,fit);
  /* glyph index variant agrees */
  WORD g[64]; U(GetGlyphIndicesW)(e->dc,w,n,g,0);
  memset(dxi,0x55,sizeof dxi);
  CHECK(U(GetTextExtentExPointI)(e->dc,g,n,maxext,&fiti,dxi,&szi));
  CHECKF(fiti==fit&&szi.cx==sz.cx&&szi.cy==sz.cy&&!memcmp(dxi,dx,fit*sizeof(INT)),"%s max %d: I variant differs (nFit %d vs %d)",s,maxext,fiti,fit);
  /* NULL lpnFit / NULL alpDx */
  SIZE s1={-1,-1}, s2={-1,-1}, s3={-1,-1};
  CHECK(U(GetTextExtentExPointW)(e->dc,w,n,maxext,NULL,NULL,&s1));
  CHECKF(s1.cx==sz.cx&&s1.cy==sz.cy,"%s: NULL/NULL size %ldx%ld",s,s1.cx,s1.cy);
  int f2=-9; CHECK(U(GetTextExtentExPointW)(e->dc,w,n,maxext,&f2,NULL,&s2)); CHECKF(f2==fit&&s2.cx==sz.cx,"%s: nFit with NULL alpDx %d",s,f2);
  memset(dxa,0x55,sizeof dxa); CHECK(U(GetTextExtentExPointW)(e->dc,w,n,maxext,NULL,dxa,&s3));
  CHECKF(!memcmp(dxa,dx,fit*sizeof(INT))&&s3.cx==sz.cx,"%s: alpDx with NULL lpnFit differs",s);
}

TEST(extent_ex_fit_and_prefix_sums){
  REQUIRE(U(GetTextExtentExPointW)); REQUIRE(U(GetTextExtentExPointA)); REQUIRE(U(GetTextExtentExPointI)); REQUIRE(U(GetGlyphIndicesW));
  E e; E_OPEN(e,64,64,DEFAULT_QUALITY,18);
  for(int i=0;i<NSTR;i++){
    const char *s=k_str[i]; WCHAR w[64]; widen(s,w,64); int n=(int)strlen(s);
    SIZE full; U(GetTextExtentPoint32W)(e.dc,w,n,&full);
    int cand[]={1,10,50,100,(int)full.cx-1,(int)full.cx,(int)full.cx+1,100000};
    for(unsigned c=0;c<sizeof cand/sizeof*cand;c++) ex_one(&e,s,cand[c]);
    /* a limit exactly equal to a partial sum includes that character (partial extent <= limit) */
    INT dx[64]; SIZE sz; int fit; U(GetTextExtentExPointW)(e.dc,w,n,100000,&fit,dx,&sz);
    for(int k=0;k<n;k++) ex_one(&e,s,dx[k]);
    CHECKF(fit==n,"%s: huge limit nFit %d want %d",s,fit,n);
  }
  e_close(&e);
}

TEST(extent_ex_zero_and_negative_limit){
  E e; E_OPEN(e,64,64,DEFAULT_QUALITY,18);
  INT dx[8]; int fit=-9; SIZE sz;
  BOOL r=U(GetTextExtentExPointA)(e.dc,"Hello",5,0,&fit,dx,&sz);
  t_note("GetTextExtentExPointA(max 0): ret %d nFit %d cx %ld",r,fit,sz.cx);
  CHECK(r); CHECKF(fit==0,"max 0: nFit %d",fit);
  e_close(&e);
}

/* GdiGetCharDimensions(hdc, TEXTMETRICW*, LONG *height) -> average character width used for dialog base units */
UDECL(LONG,GdiGetCharDimensions,(HDC,LPTEXTMETRICW,LONG*));
TEST(gdi_get_char_dimensions){
  REQUIRE(UN(GdiGetCharDimensions));
  static const WCHAR ALPHA[]=L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const WCHAR *faces[]={L"Arial",L"Times New Roman",L"Courier New",L"Tahoma"};
  static const int hs[]={-12,-18,-24};
  int done=0;
  for(unsigned fi=0;fi<4;fi++) for(unsigned hi=0;hi<3;hi++){
    HDC dc=U(CreateCompatibleDC)(NULL);
    LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=hs[hi]; lf.lfWeight=400; lf.lfCharSet=ANSI_CHARSET; lstrcpyW(lf.lfFaceName,faces[fi]);
    HFONT f=U(CreateFontIndirectW)(&lf); HFONT of=(HFONT)U(SelectObject)(dc,f);
    WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(dc,LF_FACESIZE,got);
    TEXTMETRICW tm0; U(GetTextMetricsW)(dc,&tm0);
    if(!lstrcmpiW(got,faces[fi])){
      TEXTMETRICW tm; memset(&tm,0x7f,sizeof tm); LONG h=-77;
      LONG avg=UN(GdiGetCharDimensions)(dc,&tm,&h);
      SIZE sz; U(GetTextExtentPoint32W)(dc,ALPHA,52,&sz);
      LONG want=(tm0.tmPitchAndFamily&TMPF_FIXED_PITCH)?((sz.cx/26+1)/2):tm0.tmAveCharWidth;
      CHECKF(avg==want,"%ls %d: GdiGetCharDimensions %ld want %ld (extent52 %ld, tmAveCharWidth %ld, pf 0x%x)",faces[fi],hs[hi],avg,want,sz.cx,tm0.tmAveCharWidth,tm0.tmPitchAndFamily);
      CHECKF(h==tm0.tmHeight,"%ls %d: height out %ld tmHeight %ld",faces[fi],hs[hi],h,tm0.tmHeight);
      CHECKF(!memcmp(&tm,&tm0,sizeof tm),"%ls %d: returned TEXTMETRIC differs from GetTextMetricsW",faces[fi],hs[hi]);
      /* NULL TEXTMETRIC and NULL height are accepted */
      LONG h2=-1; LONG a2=UN(GdiGetCharDimensions)(dc,NULL,&h2); CHECKF(a2==avg&&h2==h,"%ls %d: NULL tm gives %ld/%ld",faces[fi],hs[hi],a2,h2);
      LONG a3=UN(GdiGetCharDimensions)(dc,NULL,NULL); CHECKF(a3==avg,"%ls %d: NULL/NULL gives %ld",faces[fi],hs[hi],a3);
      if(hi==1) t_note("%ls %d: GdiGetCharDimensions %ld extent52/52 %ld.%02ld tmAve %ld",faces[fi],hs[hi],avg,sz.cx/52,(sz.cx%52)*100/52,tm0.tmAveCharWidth);
      done++;
    }
    U(SelectObject)(dc,of); U(DeleteObject)(f); U(DeleteDC)(dc);
  }
  /* invalid DC: 0 */
  LONG h=-5; LONG r=UN(GdiGetCharDimensions)(NULL,NULL,&h); CHECKF(r==0,"NULL DC: %ld",r);
  if(!done) t_skip("none of the test faces was realised by name");
}
