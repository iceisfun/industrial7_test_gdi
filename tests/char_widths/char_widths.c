#include "t.h"
/* char_widths: invariants of the GDI width / extent / DrawText APIs that hold on ANY Windows.
 *
 * Hard CHECKs are limited to behaviour read in the Win10 22H2 decomp (corpus API, gdi32full / user32 19041.2965):
 *   gdi32full!bGetCharWidthW @0x52508   (GetCharWidth32W / GetCharWidthW): NULL buffer, iFirst > iLast or
 *                                       iLast > 0xFFFF -> FALSE + ERROR_INVALID_PARAMETER (87); an invalid DC with
 *                                       iLast < 0x80 -> FALSE + 87.
 *   gdi32full!bGetCharWidthA @0x52174   (GetCharWidth32A): invalid DC, NULL buffer, iFirst > iLast, iLast > 0xFF
 *                                       (SBCS charset) -> FALSE + 87; chars < 0x80 come from the same CFONT cache
 *                                       (bComputeCharWidths) as the W call -> A/W parity on ASCII.
 *   gdi32full!bGetCharABCWidthsW @0x22070 (GetCharABCWidthsW / GetCharABCWidthsFloatW): NULL buffer or
 *                                       iLast < iFirst -> FALSE + 87.
 *   gdi32full!GetCharWidthI @0x503f0    NULL piWidths, or NULL pgi with giFirst > 0xFFFF -> FALSE + 87;
 *                                       cgi == 0 -> TRUE (before the DC is looked at).
 *   gdi32full!GetKerningPairsW @0x51140 nPairs == 0 with a buffer -> 0 + 87.
 *   gdi32full!GetTextExtentPointWInternal @0xe8dc (GetTextExtentPoint32W): c == 0 -> TRUE, size {0,0} (before
 *                                       the DC is looked at); c < 0 -> FALSE + 87; cy = the cached tmHeight
 *                                       (also RFONTOBJ::bTextExtent: cy = RFONT+0x164 = tmHeight, docs/research/02 6.5).
 *   win32kfull vIFIMetricsToTextMetricWStrict (docs/research/02 6.2): tmDescent = tmHeight - tmAscent;
 *                                       tmOverhang = 0 for TrueType (no bitmap/stroke flag).
 *   win32kfull GreGetCharABCWidthsW / GreGetCharWidthW (docs/research/02 6.5): A + B + C = round(fxD) and
 *                                       GetCharWidth32 = round(fxD) + overhang (MM_TEXT) -> A+B+C = width - tmOverhang.
 *   win32kfull GreExtGetObjectW (docs/research/02 1): GetObjectW(hfont, 0, NULL) = sizeof(LOGFONTW); the stored
 *                                       LOGFONT comes back unchanged.
 *   user32!DrawTextExWorker @0x1e810 + DT_InitDrawTextInfo @0x1ed80: NULL string -> 0; line height =
 *                                       tmHeight (+ tmExternalLeading with DT_EXTERNALLEADING); DT_CALCRECT|DT_SINGLELINE
 *                                       sets bottom = top + line height and returns that height.
 * Everything else (extent == sum of widths, ExPoint partial extents, GetCharWidthW == GetCharWidth32W, I == W,
 * tabbed extents, raster-font ABC failure) is reported with t_note only. Win10 numbers are NOT checked here
 * (data/win10/char_widths + the golden generator own them). */

static HFONT mkfont(const WCHAR *face, int h, int wt){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=wt; lf.lfCharSet=DEFAULT_CHARSET; lstrcpynW(lf.lfFaceName,face,LF_FACESIZE);
  return U(CreateFontIndirectW)(&lf);
}
/* screen DC with a font of the given face selected; returns 0 (with a NOTE) if the DC/metrics fail or, with need_tt,
 * if the face is not realised as that TrueType face. Single-font tests turn the 0 into t_skip themselves. */
typedef struct { HDC dc; HFONT f, old; TEXTMETRICW tm; } fdc;
static int fdc_open(fdc *d, const WCHAR *face, int h, int wt, int need_tt){
  memset(d,0,sizeof *d);
  d->dc=U(GetDC)(NULL); if(!d->dc){ t_note("GetDC(NULL) failed"); return 0; }
  d->f=mkfont(face,h,wt); d->old=(HFONT)U(SelectObject)(d->dc,d->f);
  WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(d->dc,LF_FACESIZE,got);
  if(!U(GetTextMetricsW)(d->dc,&d->tm)){ t_note("%ls %d: GetTextMetricsW failed", face, h); return 0; }
  if(need_tt && (lstrcmpiW(got,face) || !(d->tm.tmPitchAndFamily & TMPF_TRUETYPE))){ t_note("%ls %d: realised as %ls pf 0x%x, not that TrueType face", face, h, got, d->tm.tmPitchAndFamily); return 0; }
  return 1;
}
static void fdc_close(fdc *d){
  if(d->dc){ if(d->old) U(SelectObject)(d->dc,d->old); U(ReleaseDC)(NULL,d->dc); }
  if(d->f) U(DeleteObject)(d->f);
}

/* ---------------------------------------------------------------- parameter validation (decomp-verified)
 * The bad-parameter calls run in a child process (exe --child badparam <first> <logfile>) so that an implementation
 * which writes through the NULL buffer or loops over an inverted range crashes/hangs only the child. The child runs
 * cases first..N-1 and appends "<case> <code> <ret> <lasterror>" per case to the log (flushed); code 0 = expected
 * return and last error, 1 = wrong return, 2 = wrong last error. If the child dies, the parent records the child's
 * exit code for the case after the last logged one and restarts at the next case. */
typedef struct { const char *name; int expect_ok; DWORD err; } badcase;
static const badcase k_bad[]={
  {"GetCharWidth32W(dc,A,A,NULL)",0,ERROR_INVALID_PARAMETER},      /* 0 */
  {"GetCharWidth32W(dc,B,A,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidth32W(dc,A,0x10000,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidthW(dc,A,A,NULL)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidthW(dc,B,A,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidth32A(dc,A,A,NULL)",0,ERROR_INVALID_PARAMETER},      /* 5 */
  {"GetCharWidth32A(dc,B,A,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidth32W(NULL,A,A,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidth32A(NULL,A,A,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharABCWidthsW(dc,A,A,NULL)",0,ERROR_INVALID_PARAMETER},
  {"GetCharABCWidthsW(dc,B,A,buf)",0,ERROR_INVALID_PARAMETER},     /* 10 */
  {"GetCharABCWidthsFloatW(dc,A,A,NULL)",0,ERROR_INVALID_PARAMETER},
  {"GetCharABCWidthsFloatW(dc,B,A,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidthI(dc,0,2,gi,NULL)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidthI(dc,0x10000,2,NULL,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetCharWidthI(NULL,0,0,gi,buf) cgi=0",1,0},                     /* 15 */
  {"GetKerningPairsW(dc,0,buf)",0,ERROR_INVALID_PARAMETER},
  {"GetTextExtentPoint32W(dc,abc,-1)",0,ERROR_INVALID_PARAMETER},
  {"GetTextExtentPoint32W(NULL,abc,0) -> TRUE 0x0",1,0},
};
#define NBAD ((int)(sizeof k_bad/sizeof k_bad[0]))
static int bad_case(HDC dc, int n, BOOL *ret, DWORD *err){
  INT w[8]; ABC a[4]; ABCFLOAT af[4]; WORD gi[2]={3,4}; KERNINGPAIR kp[2]; SIZE sz={77,77};
  SetLastError(0xDEADBEEF);
  BOOL r=FALSE; int sizeok=1;
  switch(n){
    case 0: r=U(GetCharWidth32W)(dc,'A','A',NULL); break;
    case 1: r=U(GetCharWidth32W)(dc,'B','A',w); break;
    case 2: r=U(GetCharWidth32W)(dc,'A',0x10000,w); break;
    case 3: r=U(GetCharWidthW)(dc,'A','A',NULL); break;
    case 4: r=U(GetCharWidthW)(dc,'B','A',w); break;
    case 5: r=U(GetCharWidth32A)(dc,'A','A',NULL); break;
    case 6: r=U(GetCharWidth32A)(dc,'B','A',w); break;
    case 7: r=U(GetCharWidth32W)(NULL,'A','A',w); break;
    case 8: r=U(GetCharWidth32A)(NULL,'A','A',w); break;
    case 9: r=U(GetCharABCWidthsW)(dc,'A','A',NULL); break;
    case 10: r=U(GetCharABCWidthsW)(dc,'B','A',a); break;
    case 11: r=U(GetCharABCWidthsFloatW)(dc,'A','A',NULL); break;
    case 12: r=U(GetCharABCWidthsFloatW)(dc,'B','A',af); break;
    case 13: r=U(GetCharWidthI)(dc,0,2,gi,NULL); break;
    case 14: r=U(GetCharWidthI)(dc,0x10000,2,NULL,w); break;
    case 15: r=U(GetCharWidthI)(NULL,0,0,gi,w); break;
    case 16: r=U(GetKerningPairsW)(dc,0,kp)!=0; break;
    case 17: r=U(GetTextExtentPoint32W)(dc,L"abc",-1,&sz); break;
    case 18: r=U(GetTextExtentPoint32W)(NULL,L"abc",0,&sz); sizeok=(sz.cx==0&&sz.cy==0); break;
  }
  *err=GetLastError(); *ret=r;
  if(!!r!=k_bad[n].expect_ok || !sizeok) return 1;
  if(!k_bad[n].expect_ok && *err!=k_bad[n].err) return 2;
  return 0;
}
CHILD(badparam){
  if(argc<3) return 3;
  int first=atoi(argv[1]); FILE *lf=fopen(argv[2],"a"); if(!lf) return 3;
  HDC dc=U(CreateCompatibleDC)(NULL); if(!dc) return 3;   /* memory DC: no user32/desktop in the child */
  HFONT f=mkfont(L"Arial",-12,400); U(SelectObject)(dc,f);
  for(int n=first;n<NBAD;n++){ BOOL r; DWORD e; int c=bad_case(dc,n,&r,&e); fprintf(lf,"%d %d %d %lu\n",n,c,(int)r,e); fflush(lf); }
  fclose(lf); return 0;
}
TEST(bad_params){
  char log[MAX_PATH]; t_tmpfile(log,sizeof log,"badparam.log"); DeleteFileA(log);
  DWORD code[64], rv[64], le[64]; int have[64]={0};
  int next=0;
  while(next<NBAD){
    char a[MAX_PATH+32]; snprintf(a,sizeof a,"%d \"%s\"",next,log);
    DWORD rc=t_run_child("badparam",a,20000);
    if(rc!=0) t_note("badparam child starting at case %d exited 0x%lx", next, rc);
    FILE *f=fopen(log,"r"); int n,c,r; unsigned long e; int last=next-1;
    if(f){ while(fscanf(f,"%d %d %d %lu",&n,&c,&r,&e)==4) if(n>=0&&n<NBAD){ have[n]=1; code[n]=c; rv[n]=r; le[n]=e; if(n>last) last=n; } fclose(f); }
    if(last>=NBAD-1) break;
    /* the child died (crash / timeout) in case last+1 */
    have[last+1]=1; code[last+1]=rc; rv[last+1]=0; le[last+1]=0;
    next=last+2;
  }
  for(int i=0;i<NBAD;i++){
    if(!have[i]){ CHECKF(0,"%s: no result",k_bad[i].name); continue; }
    CHECKF(code[i]==0, "%s: %s (ret %lu, last error %lu; want ret %d, error %lu)", k_bad[i].name,
           code[i]==1?"wrong return":code[i]==2?"wrong last error":"child died", rv[i], le[i], k_bad[i].expect_ok, k_bad[i].err);
    if(code[i]>2) t_note("%s: child exit 0x%lx (0xDEAD0001 = timeout)", k_bad[i].name, code[i]);
  }
}
TEST(cw32_valid_single){
  fdc d; if(!fdc_open(&d,L"Arial",-12,400,0)) { fdc_close(&d); t_skip("font setup failed"); return; }
  INT w[2]; CHECK(U(GetCharWidth32W)(d.dc,'A','A',w));   /* iFirst == iLast is a valid one-element request */
  WORD gi[2]={3,4}; CHECK(U(GetCharWidthI)(d.dc,0,0,gi,w));
  SIZE sz={77,77}; CHECK(U(GetTextExtentPoint32W)(d.dc,L"abc",0,&sz)); EXPECT_EQ(sz.cx,0); EXPECT_EQ(sz.cy,0);
  fdc_close(&d);
}
TEST(kerning_pairs_protocol){
  fdc d; if(!fdc_open(&d,L"Arial",-12,400,0)) { fdc_close(&d); t_skip("font setup failed"); return; }
  KERNINGPAIR kp[2];
  DWORD n=U(GetKerningPairsW)(d.dc,0,NULL);
  t_note("Arial -12 GetKerningPairsW count = %lu", n);
  if(n>0 && n<100000){
    KERNINGPAIR *all=(KERNINGPAIR*)calloc(n,sizeof *all); DWORD g=U(GetKerningPairsW)(d.dc,n,all);
    CHECKF(g<=n, "returned %lu > requested %lu", g, n);
    DWORD g1=U(GetKerningPairsW)(d.dc,1,kp);
    if(g1!=1) t_note("GetKerningPairsW(nPairs=1) returned %lu", g1);
    else if(g>=1) CHECK(kp[0].wFirst==all[0].wFirst && kp[0].wSecond==all[0].wSecond && kp[0].iKernAmount==all[0].iKernAmount);
    free(all);
  }
  fdc_close(&d);
}
TEST(drawtext_null_string){
  HDC dc=U(GetDC)(NULL); RECT r={0,0,100,100};
  int ret=U(DrawTextW)(dc,NULL,-1,&r,DT_CALCRECT|DT_SINGLELINE); EXPECT_EQ(ret,0);
  U(ReleaseDC)(NULL,dc);
}

/* ---------------------------------------------------------------- metric invariants */
static const WCHAR *k_faces[]={L"Arial",L"Tahoma",L"Segoe UI",L"Courier New",L"Times New Roman",L"MS Shell Dlg",L"MS Shell Dlg 2",L"Microsoft Sans Serif",L"System",L"Fixedsys",L"Terminal",L"Courier",L"MS Sans Serif"};
static const int k_h[]={-8,-11,-12,-13,-16,-24,13,16};

TEST(tm_height_is_ascent_plus_descent){
  for(unsigned fi=0;fi<sizeof k_faces/sizeof*k_faces;fi++) for(unsigned hi=0;hi<sizeof k_h/sizeof*k_h;hi++) for(int wt=400;wt<=700;wt+=300){
    fdc d; if(!fdc_open(&d,k_faces[fi],k_h[hi],wt,0)){ fdc_close(&d); continue; }
    CHECKF(d.tm.tmHeight==d.tm.tmAscent+d.tm.tmDescent, "face %ls h %d wt %d: %ld != %ld + %ld", k_faces[fi],k_h[hi],wt,d.tm.tmHeight,d.tm.tmAscent,d.tm.tmDescent);
    if(d.tm.tmPitchAndFamily & TMPF_TRUETYPE) CHECKF(d.tm.tmOverhang==0, "TrueType %ls h %d wt %d overhang %ld", k_faces[fi],k_h[hi],wt,d.tm.tmOverhang);
    fdc_close(&d);
  }
}

TEST(abc_sum_equals_width_truetype){
  static const WCHAR *tt[]={L"Arial",L"Tahoma",L"Segoe UI",L"Courier New",L"Times New Roman"};
  int done=0;
  for(unsigned fi=0;fi<sizeof tt/sizeof*tt;fi++) for(unsigned hi=0;hi<6;hi++) for(int wt=400;wt<=700;wt+=300){
    fdc d; if(!fdc_open(&d,tt[fi],k_h[hi],wt,1)){ fdc_close(&d); continue; }
    INT w[95]; ABC a[95];
    CHECK(U(GetCharWidth32W)(d.dc,0x20,0x7E,w));
    CHECK(U(GetCharABCWidthsW)(d.dc,0x20,0x7E,a));
    int bad=0, first=-1;
    for(int i=0;i<95;i++) if(a[i].abcA+(int)a[i].abcB+a[i].abcC!=w[i]-d.tm.tmOverhang){ bad++; if(first<0) first=i; }
    CHECKF(bad==0, "%ls h %d wt %d: %d chars with A+B+C != width (first U+%04X: %d+%u+%d vs %d)", tt[fi],k_h[hi],wt,bad,
           first<0?0:0x20+first, first<0?0:a[first].abcA, first<0?0:a[first].abcB, first<0?0:a[first].abcC, first<0?0:w[first]);
    done++;
    fdc_close(&d);
  }
  if(!done) t_skip("no TrueType face realised");
}

TEST(width_a_w_parity_ascii){
  for(unsigned fi=0;fi<sizeof k_faces/sizeof*k_faces;fi++){
    fdc d; if(!fdc_open(&d,k_faces[fi],-12,400,0)){ fdc_close(&d); continue; }
    INT wa[95], ww[95];
    CHECK(U(GetCharWidth32A)(d.dc,0x20,0x7E,wa)); CHECK(U(GetCharWidth32W)(d.dc,0x20,0x7E,ww));
    int bad=0; for(int i=0;i<95;i++) if(wa[i]!=ww[i]) bad++;
    CHECKF(bad==0, "%ls -12: %d ASCII widths differ between GetCharWidth32A and W", k_faces[fi], bad);
    INT w16[95]; if(U(GetCharWidthW)(d.dc,0x20,0x7E,w16)){ int b2=0; for(int i=0;i<95;i++) if(w16[i]!=ww[i]) b2++; if(b2) t_note("%ls -12: GetCharWidthW differs from GetCharWidth32W in %d chars", k_faces[fi], b2); }
    fdc_close(&d);
  }
}

TEST(extent_cy_is_tmheight){
  static const WCHAR *ss[]={L"The quick brown fox jumps over the lazy dog", L"Hg", L" ", L"\x00C0\x00C9\x00D6", L"\x0391\x0416\x2026\x20AC"};
  for(unsigned fi=0;fi<sizeof k_faces/sizeof*k_faces;fi++) for(unsigned hi=0;hi<sizeof k_h/sizeof*k_h;hi++){
    fdc d; if(!fdc_open(&d,k_faces[fi],k_h[hi],400,0)){ fdc_close(&d); continue; }
    for(unsigned si=0;si<sizeof ss/sizeof*ss;si++){
      SIZE sz; int n=lstrlenW(ss[si]);
      CHECK(U(GetTextExtentPoint32W)(d.dc,ss[si],n,&sz));
      /* hard check only where no font linking / LPK fallback can be involved: ASCII strings (the client CFONT path in
       * GetTextExtentPointWInternal), or a TrueType face that has every glyph of the string. A non-ASCII string on a
       * raster face or with a missing glyph goes through the kernel/LPK path with linked fonts: observation only. */
      int ascii=1; for(int i=0;i<n;i++) if(ss[si][i]>=0x80) ascii=0;
      int allglyphs=0;
      if(!ascii && (d.tm.tmPitchAndFamily & TMPF_TRUETYPE)){ WORD g[16]; allglyphs = n<=16 && U(GetGlyphIndicesW)(d.dc,ss[si],n,g,GGI_MARK_NONEXISTING_GLYPHS)!=GDI_ERROR;
        for(int i=0;allglyphs && i<n;i++) if(g[i]==0xFFFF) allglyphs=0; }
      if(ascii || allglyphs) CHECKF(sz.cy==d.tm.tmHeight, "%ls h %d str %u: cy %ld tmHeight %ld", k_faces[fi],k_h[hi],si,sz.cy,d.tm.tmHeight);
      else if(sz.cy!=d.tm.tmHeight) t_note("%ls h %d str %u (non-ASCII, linked/missing glyphs): cy %ld tmHeight %ld", k_faces[fi],k_h[hi],si,sz.cy,d.tm.tmHeight);
      /* observations only: sum of widths, ExPoint partial extents */
      long sum=0; for(int i=0;i<n;i++){ INT w=0; U(GetCharWidth32W)(d.dc,ss[si][i],ss[si][i],&w); sum+=w; }
      if(sum!=sz.cx) t_note("%ls h %d str %u: extent %ld != sum of GetCharWidth32W %ld", k_faces[fi],k_h[hi],si,sz.cx,sum);
      INT dx[64]; int fit=-1; SIZE es;
      if(U(GetTextExtentExPointW)(d.dc,ss[si],n,100,&fit,dx,&es)){
        if(es.cx!=sz.cx || es.cy!=sz.cy) t_note("%ls h %d str %u: ExPoint size %ldx%ld vs %ldx%ld", k_faces[fi],k_h[hi],si,es.cx,es.cy,sz.cx,sz.cy);
        if(n>0 && dx[n-1]!=sz.cx) t_note("%ls h %d str %u: last partial extent %d != cx %ld", k_faces[fi],k_h[hi],si,dx[n-1],sz.cx);
        int f2=0; while(f2<n && dx[f2]<=100) f2++;
        if(f2!=fit) t_note("%ls h %d str %u: nFit %d, partial extents say %d", k_faces[fi],k_h[hi],si,fit,f2);
      }
    }
    fdc_close(&d);
  }
}

TEST(drawtext_calcrect_single_line_height){
  static const WCHAR *ss[]={L"The quick brown fox", L"&File", L"Name\tSize"};
  for(unsigned fi=0;fi<sizeof k_faces/sizeof*k_faces;fi++) for(unsigned hi=0;hi<sizeof k_h/sizeof*k_h;hi++){
    fdc d; if(!fdc_open(&d,k_faces[fi],k_h[hi],400,0)){ fdc_close(&d); continue; }
    for(unsigned si=0;si<sizeof ss/sizeof*ss;si++){
      RECT r={5,7,500,1000};
      int ret=U(DrawTextW)(d.dc,ss[si],-1,&r,DT_CALCRECT|DT_SINGLELINE);
      CHECKF(ret==d.tm.tmHeight && r.top==7 && r.bottom==7+d.tm.tmHeight, "%ls h %d str %u: ret %d rect t %ld b %ld tmHeight %ld",
             k_faces[fi],k_h[hi],si,ret,r.top,r.bottom,d.tm.tmHeight);
      RECT r2={5,7,500,1000};
      int ret2=U(DrawTextW)(d.dc,ss[si],-1,&r2,DT_CALCRECT|DT_SINGLELINE|DT_EXTERNALLEADING);
      CHECKF(ret2==d.tm.tmHeight+d.tm.tmExternalLeading && r2.bottom==7+ret2, "%ls h %d str %u DT_EXTERNALLEADING: ret %d b %ld h %ld el %ld",
             k_faces[fi],k_h[hi],si,ret2,r2.bottom,d.tm.tmHeight,d.tm.tmExternalLeading);
      /* observation: the single-line width vs the extent (prefix '&' removed, tabs not expanded) */
      SIZE sz; U(GetTextExtentPoint32W)(d.dc,ss[si],lstrlenW(ss[si]),&sz);
      if(si==0 && r.right-r.left!=sz.cx) t_note("%ls h %d: DT_CALCRECT width %ld vs extent %ld", k_faces[fi],k_h[hi],r.right-r.left,sz.cx);
    }
    fdc_close(&d);
  }
}

TEST(width_i_matches_w_observed){
  fdc d; if(!fdc_open(&d,L"Arial",-12,400,1)) { fdc_close(&d); t_skip("font setup failed"); return; }
  WCHAR s[95]; for(int i=0;i<95;i++) s[i]=(WCHAR)(0x20+i);
  WORD gi[95]; INT wi[95], ww[95]; ABC ai[95], aw[95];
  CHECK(U(GetGlyphIndicesW)(d.dc,s,95,gi,0)!=GDI_ERROR);
  CHECK(U(GetCharWidthI)(d.dc,0,95,gi,wi)); CHECK(U(GetCharWidth32W)(d.dc,0x20,0x7E,ww));
  CHECK(U(GetCharABCWidthsI)(d.dc,0,95,gi,ai)); CHECK(U(GetCharABCWidthsW)(d.dc,0x20,0x7E,aw));
  int b1=0,b2=0; for(int i=0;i<95;i++){ if(wi[i]!=ww[i]) b1++; if(memcmp(&ai[i],&aw[i],sizeof(ABC))) b2++; }
  if(b1||b2) t_note("Arial -12: GetCharWidthI differs in %d, GetCharABCWidthsI in %d of 95", b1, b2);
  /* GetTabbedTextExtentW on a string without tabs vs GetTextExtentPoint32W */
  SIZE sz; U(GetTextExtentPoint32W)(d.dc,L"Hello world",11,&sz);
  DWORD t=U(GetTabbedTextExtentW)(d.dc,L"Hello world",11,0,NULL);
  if(LOWORD(t)!=sz.cx || HIWORD(t)!=sz.cy) t_note("tabbed extent %ux%u vs %ldx%ld", LOWORD(t),HIWORD(t),sz.cx,sz.cy);
  fdc_close(&d);
}

TEST(raster_font_abc_observed){
  HDC dc=U(GetDC)(NULL); HGDIOBJ sys=U(GetStockObject)(SYSTEM_FONT), o=U(SelectObject)(dc,sys);
  TEXTMETRICW tm; CHECK(U(GetTextMetricsW)(dc,&tm));
  ABC a[2]; SET_ERR(0); BOOL r=U(GetCharABCWidthsW)(dc,'A','B',a); DWORD e=GetLastError();
  ABCFLOAT af[2]; BOOL rf=U(GetCharABCWidthsFloatW)(dc,'A','B',af);
  t_note("SYSTEM_FONT pf=0x%x tt=%d: GetCharABCWidthsW=%d err=%lu, GetCharABCWidthsFloatW=%d", tm.tmPitchAndFamily, !!(tm.tmPitchAndFamily&TMPF_TRUETYPE), r, e, rf);
  U(SelectObject)(dc,o); U(ReleaseDC)(NULL,dc);
}

/* ---------------------------------------------------------------- object round trips */
TEST(font_getobject_roundtrip){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=-11; lf.lfWidth=3; lf.lfEscapement=900; lf.lfOrientation=900; lf.lfWeight=700; lf.lfItalic=1;
  lf.lfUnderline=1; lf.lfStrikeOut=0; lf.lfCharSet=DEFAULT_CHARSET; lf.lfOutPrecision=OUT_TT_PRECIS; lf.lfClipPrecision=CLIP_DEFAULT_PRECIS;
  lf.lfQuality=ANTIALIASED_QUALITY; lf.lfPitchAndFamily=VARIABLE_PITCH|FF_SWISS; lstrcpyW(lf.lfFaceName,L"MS Shell Dlg");
  HFONT f=U(CreateFontIndirectW)(&lf); CHECK(f!=NULL); if(!f) return;
  int need=U(GetObjectW)(f,0,NULL); EXPECT_EQ(need,sizeof(LOGFONTW));
  LOGFONTW g; memset(&g,0xCC,sizeof g);
  int gotn=U(GetObjectW)(f,sizeof g,&g); EXPECT_EQ(gotn,sizeof(LOGFONTW));
  CHECK(g.lfHeight==lf.lfHeight && g.lfWidth==lf.lfWidth && g.lfEscapement==lf.lfEscapement && g.lfOrientation==lf.lfOrientation);
  CHECK(g.lfWeight==lf.lfWeight && g.lfItalic==lf.lfItalic && g.lfUnderline==lf.lfUnderline && g.lfStrikeOut==lf.lfStrikeOut);
  CHECK(g.lfCharSet==lf.lfCharSet && g.lfOutPrecision==lf.lfOutPrecision && g.lfClipPrecision==lf.lfClipPrecision);
  CHECK(g.lfQuality==lf.lfQuality && g.lfPitchAndFamily==lf.lfPitchAndFamily);
  EXPECT_WSTR(g.lfFaceName,L"MS Shell Dlg");   /* the requested name, not the substitute */
  /* select / reselect */
  HDC dc=U(GetDC)(NULL); HGDIOBJ o=U(SelectObject)(dc,f);
  CHECK(o!=NULL && o!=HGDI_ERROR);
  HGDIOBJ cur=U(GetCurrentObject)(dc,OBJ_FONT); EXPECT_EQ(cur,f);
  HGDIOBJ back=U(SelectObject)(dc,o); EXPECT_EQ(back,f);
  U(ReleaseDC)(NULL,dc); U(DeleteObject)(f);
}
