#include "t.h"
/* kerning_placement: GetKerningPairs{A,W}, GetCharacterPlacement{A,W}, Set/GetTextCharacterExtra,
 * SetTextJustification, Get/SetLayout, SetLayoutWidth. Expectations and their sources: EVIDENCE.md.
 * Faces are realised at 24 px (lfHeight -24). Kern-table presence is verified with GetFontData('kern'). */

#define KERN_TAG 0x6e72656bU   /* 'kern' read as a little-endian DWORD */

typedef struct { HDC dc; HFONT f, old; } fd;
static int fd_open(fd *d, const WCHAR *face){
  LOGFONTW lf; memset(&lf,0,sizeof lf); memset(d,0,sizeof *d);
  lf.lfHeight=-24; lf.lfWeight=FW_NORMAL; lf.lfCharSet=DEFAULT_CHARSET; lstrcpynW(lf.lfFaceName,face,LF_FACESIZE);
  d->dc=U(CreateCompatibleDC)(NULL); if(!d->dc){ t_note("CreateCompatibleDC failed"); return 0; }
  d->f=U(CreateFontIndirectW)(&lf); d->old=(HFONT)U(SelectObject)(d->dc,d->f);
  WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(d->dc,LF_FACESIZE,got);
  if(lstrcmpiW(got,face)){ t_note("%ls realised as %ls", face, got); return 0; }
  return 1;
}
static void fd_close(fd *d){
  if(d->dc){ if(d->old) U(SelectObject)(d->dc,d->old); U(DeleteDC)(d->dc); }
  if(d->f) U(DeleteObject)(d->f);
}
static int has_kern(HDC dc){ return U(GetFontData)(dc,KERN_TAG,0,NULL,0)!=GDI_ERROR; }
static int ext_w(HDC dc, const WCHAR *s, SIZE *sz){ return U(GetTextExtentPoint32W)(dc,s,lstrlenW(s),sz); }

/* single-evaluation equality check (the harness macro evaluates its arguments twice) */
#define EQ(a,b) do{ ULONG_PTR a_=(ULONG_PTR)(a), b_=(ULONG_PTR)(b); \
  t_check(a_==b_,__FILE__,__LINE__,#a " == " #b,"got 0x%llx want 0x%llx",(unsigned long long)a_,(unsigned long long)b_); }while(0)

/* sum of kern amounts of adjacent pairs of s according to GetKerningPairsW */
static int kern_sum(HDC dc, const WCHAR *s, int n){
  DWORD np=U(GetKerningPairsW)(dc,0,NULL); if(!np) return 0;
  KERNINGPAIR *k=calloc(np,sizeof *k); U(GetKerningPairsW)(dc,np,k); int sum=0;
  for(int i=0;i+1<n;i++) for(DWORD j=0;j<np;j++) if(k[j].wFirst==s[i]&&k[j].wSecond==s[i+1]){ sum+=k[j].iKernAmount; break; }
  free(k); return sum;
}

static const WCHAR *k_kfaces[]={ L"Arial", L"Times New Roman" };

/* ------------------------------------------------------------ kerning */
TEST(kerning_count_and_pairs){
  REQUIRE(U(GetKerningPairsA)); REQUIRE(U(GetKerningPairsW)); REQUIRE(U(GetFontData));
  int tested=0;
  for(int i=0;i<2;i++){
    fd d; if(!fd_open(&d,k_kfaces[i])){ fd_close(&d); t_note("%ls unavailable", k_kfaces[i]); continue; }
    if(!has_kern(d.dc)){ t_note("%ls: realised font has no kern table", k_kfaces[i]); fd_close(&d); continue; }
    tested++;
    DWORD na=U(GetKerningPairsA)(d.dc,0,NULL), nw=U(GetKerningPairsW)(d.dc,0,NULL);
    t_note("%ls 24px: count A=%lu W=%lu", k_kfaces[i], na, nw);
    CHECKF(na>0 && nw>0, "%ls count 0 although kern table present", k_kfaces[i]);
    CHECKF(na<=nw, "%ls: A count %lu exceeds W count %lu", k_kfaces[i], na, nw);
    if(na>0 && nw>0 && nw<100000){
      KERNINGPAIR *a=calloc(na,sizeof *a), *w=calloc(nw,sizeof *w);
      DWORD ra=U(GetKerningPairsA)(d.dc,na,a), rw=U(GetKerningPairsW)(d.dc,nw,w);
      EQ(ra,na); EQ(rw,nw);
      /* A pairs are 8-bit ANSI codes. Every A pair whose CP_ACP translation is also in the W list must carry the same
       * amount (asserted). Some A pairs have no exact W counterpart, so the A count is not the number of W pairs
       * representable in CP_ACP; both figures are only NOTEd. */
      DWORD missing=0, mismatch=0, repr=0;
      for(DWORD j=0;j<na;j++){
        char c[2]={(char)a[j].wFirst,(char)a[j].wSecond}; WCHAR u[2]; MultiByteToWideChar(CP_ACP,0,c,2,u,2);
        int f=-1; for(DWORD q=0;q<nw;q++) if(w[q].wFirst==u[0]&&w[q].wSecond==u[1]){ f=(int)q; break; }
        if(f<0){ missing++; if(missing<=6) t_note("  A pair %02x,%02x amt %d -> U+%04x,U+%04x not in W", a[j].wFirst,a[j].wSecond,a[j].iKernAmount,u[0],u[1]); } else if(w[f].iKernAmount!=a[j].iKernAmount) mismatch++;
      }
      for(DWORD q=0;q<nw;q++){ WCHAR u[2]={w[q].wFirst,w[q].wSecond}; char c[2]; BOOL def=FALSE; WCHAR back[2];
        int r=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,u,2,c,2,NULL,&def); MultiByteToWideChar(CP_ACP,0,c,2,back,2);
        if(r==2&&!def&&back[0]==u[0]&&back[1]==u[1]) repr++; }
      t_note("%ls: A pairs missing from W=%lu amount mismatches=%lu; W pairs representable in ANSI=%lu", k_kfaces[i], missing, mismatch, repr);
      EQ(mismatch,0);
      int found=-1; for(DWORD j=0;j<nw;j++) if(w[j].wFirst=='A'&&w[j].wSecond=='V') found=(int)j;
      if(found>=0){ CHECKF(w[found].iKernAmount<0, "%ls AV kern %d not negative", k_kfaces[i], w[found].iKernAmount);
        t_note("%ls AV iKernAmount=%d", k_kfaces[i], w[found].iKernAmount); }
      else CHECKF(0, "%ls: pair (A,V) not found", k_kfaces[i]);
      int fa=-1; for(DWORD j=0;j<na;j++) if(a[j].wFirst=='A'&&a[j].wSecond=='V') fa=(int)j;
      CHECKF(fa>=0 && found>=0 && a[fa].iKernAmount==w[found].iKernAmount, "%ls: AV pair differs between A and W forms", k_kfaces[i]);
      /* kern amounts are in logical units of the DC: integer, nonzero entries only */
      int zero=0; for(DWORD j=0;j<nw;j++) if(!w[j].iKernAmount) zero++;
      t_note("%ls zero-amount pairs: %d", k_kfaces[i], zero);
      /* buffer smaller than the count */
      KERNINGPAIR small[1]; SET_ERR(0);
      DWORD rs=U(GetKerningPairsW)(d.dc,1,small); DWORD e=GetLastError();
      t_note("%ls nPairs=1 (count %lu): ret=%lu err=%lu", k_kfaces[i], nw, rs, e);
      EQ(rs,1); EQ(small[0].wFirst==w[0].wFirst&&small[0].wSecond==w[0].wSecond&&small[0].iKernAmount==w[0].iKernAmount,1);
      free(a); free(w);
    }
    fd_close(&d);
  }
  if(!tested) t_skip("neither Arial nor Times New Roman has a kern table in its realised font");
}

TEST(kerning_none){
  REQUIRE(U(GetKerningPairsW)); REQUIRE(U(GetKerningPairsA));
  const WCHAR *faces[]={ L"Courier New", L"Terminal", L"System" }; int tested=0;
  for(int i=0;i<3;i++){
    fd d; int ok=fd_open(&d,faces[i]);
    if(!ok){ fd_close(&d); t_note("%ls unavailable", faces[i]); continue; }
    int kern=has_kern(d.dc); if(!kern) tested++;
    SET_ERR(0x1234); DWORD nw=U(GetKerningPairsW)(d.dc,0,NULL); DWORD ew=GetLastError();
    SET_ERR(0x1234); DWORD na=U(GetKerningPairsA)(d.dc,0,NULL); DWORD ea=GetLastError();
    t_note("%ls: kern table=%d count W=%lu (err %lu) A=%lu (err %lu)", faces[i], kern, nw, ew, na, ea);
    if(!kern){ EQ(nw,0); EQ(na,0); EQ(ew,0x1234); EQ(ea,0x1234); /* observed: last error untouched */ }
    else t_note("%ls has a kern table; count not asserted", faces[i]);
    fd_close(&d);
  }
  if(!tested) t_skip("none of Courier New, Terminal, System realised without a kern table");
}

/* The gdi32 export table maps the undecorated name GetKerningPairs to the same entry point as GetKerningPairsA,
 * so the name must behave exactly like the A form. */
UDECL(DWORD, GetKerningPairs, (HDC, DWORD, LPKERNINGPAIR));
TEST(kerning_generic_export){
  REQUIRE(U(GetKerningPairsA)); REQUIRE(U(GetFontData));
  GetKerningPairs_pfn g=(GetKerningPairs_pfn)t_proc_in("gdi32.dll","GetKerningPairs");
  if(!g){ t_skip("gdi32 does not export GetKerningPairs"); return; }
  fd d; if(!fd_open(&d,L"Arial") || !has_kern(d.dc)){ fd_close(&d); t_skip("Arial with kern table unavailable"); return; }
  DWORD n=g(d.dc,0,NULL), na=U(GetKerningPairsA)(d.dc,0,NULL);
  t_note("GetKerningPairs count %lu, GetKerningPairsA count %lu", n, na);
  CHECKF(n>0, "GetKerningPairs count 0 although kern table present"); EQ(n,na);
  if(n>0 && n==na && n<100000){
    KERNINGPAIR *p=calloc(n,sizeof *p), *q=calloc(n,sizeof *q);
    DWORD rp=g(d.dc,n,p), rq=U(GetKerningPairsA)(d.dc,n,q);
    EQ(rp,n); EQ(rq,n); CHECKF(!memcmp(p,q,n*sizeof *p), "GetKerningPairs and GetKerningPairsA pair lists differ");
    free(p); free(q);
  }
  EQ(g(NULL,0,NULL),0);
  fd_close(&d);
}

TEST(kerning_bad_args){
  REQUIRE(U(GetKerningPairsW)); REQUIRE(U(GetKerningPairsA));
  SET_ERR(0); DWORD r=U(GetKerningPairsW)(NULL,0,NULL); t_note("GetKerningPairsW(NULL dc): ret=%lu err=%lu", r, GetLastError());
  EQ(r,0);
  SET_ERR(0x1234); r=U(GetKerningPairsA)(NULL,0,NULL); t_note("GetKerningPairsA(NULL dc): ret=%lu err=%lu", r, GetLastError());
  EQ(r,0);
}

/* Kerning is not applied by GetTextExtentPoint32 (no kerning in GDI extents): the width of "AV" equals the sum of the widths. */
TEST(kerning_not_in_extent){
  REQUIRE(U(GetCharWidth32W));
  fd d; if(!fd_open(&d,L"Times New Roman") || !has_kern(d.dc)){ fd_close(&d); t_skip("Times New Roman with kern table unavailable"); return; }
  SIZE s; int w[2]; ext_w(d.dc,L"AV",&s);
  U(GetCharWidth32W)(d.dc,'A','A',&w[0]); U(GetCharWidth32W)(d.dc,'V','V',&w[1]);
  t_note("AV extent %ld, sum of widths %d", s.cx, w[0]+w[1]);
  EQ(s.cx,w[0]+w[1]);
  fd_close(&d);
}

/* ------------------------------------------------------------ GetCharacterPlacement */
typedef struct { GCP_RESULTSA r; char out[64]; UINT order[64]; int dx[64]; int caret[64]; char cls[64]; WCHAR glyphs[64]; } gcpa;
typedef struct { GCP_RESULTSW r; WCHAR out[64]; UINT order[64]; int dx[64]; int caret[64]; char cls[64]; WCHAR glyphs[64]; } gcpw;
static void gcpa_init(gcpa *g, int n){
  memset(g,0,sizeof *g); g->r.lStructSize=sizeof g->r; g->r.lpOutString=g->out; g->r.lpOrder=g->order; g->r.lpDx=g->dx;
  g->r.lpCaretPos=g->caret; g->r.lpClass=g->cls; g->r.lpGlyphs=g->glyphs; g->r.nGlyphs=n; g->r.nMaxFit=0;
}
static void gcpw_init(gcpw *g, int n){
  memset(g,0,sizeof *g); g->r.lStructSize=sizeof g->r; g->r.lpOutString=g->out; g->r.lpOrder=g->order; g->r.lpDx=g->dx;
  g->r.lpCaretPos=g->caret; g->r.lpClass=g->cls; g->r.lpGlyphs=g->glyphs; g->r.nGlyphs=n;
}
static int fontlang_ok(HDC dc){ DWORD f=U(GetFontLanguageInfo)(dc); if(f&(GCP_REORDER|GCP_GLYPHSHAPE|GCP_LIGATE|GCP_DIACRITIC|GCP_KASHIDA|GCP_ERROR)){ t_note("GetFontLanguageInfo flags 0x%lx", f); return 0;} return 1; }

static const char k_txt[]="Hello AV To fit";

TEST(gcpa_plain_extent){
  REQUIRE(U(GetCharacterPlacementA)); REQUIRE(U(GetFontLanguageInfo)); REQUIRE(U(GetKerningPairsW));
  const WCHAR *faces[]={ L"Courier New", L"Arial", L"Times New Roman" }; int tested=0;
  for(int fi=0;fi<3;fi++){
    fd d; if(!fd_open(&d,faces[fi])){ fd_close(&d); t_note("%ls unavailable", faces[fi]); continue; }
    if(!fontlang_ok(d.dc)){ fd_close(&d); t_note("%ls: unexpected GetFontLanguageInfo flag set", faces[fi]); continue; }
    tested++;
    int n=(int)strlen(k_txt); WCHAR wt[64]; for(int i=0;i<=n;i++) wt[i]=(unsigned char)k_txt[i];
    int ks=has_kern(d.dc)?kern_sum(d.dc,wt,n):0;
    SIZE s; U(GetTextExtentPoint32A)(d.dc,k_txt,n,&s);
    int flags[2]={0,GCP_USEKERNING};
    for(int q=0;q<2;q++){
      gcpa g; gcpa_init(&g,n);
      DWORD r=U(GetCharacterPlacementA)(d.dc,k_txt,n,0,&g.r,flags[q]);
      int sum=0; for(int i=0;i<n;i++) sum+=g.dx[i];
      t_note("%ls flags 0x%x: ret=0x%08lx extent %ldx%ld kern sum %d sum(dx)=%d nGlyphs=%u", faces[fi], flags[q], r, s.cx, s.cy, ks, sum, g.r.nGlyphs);
      CHECKF(r!=0, "GetCharacterPlacementA returned 0");
      /* observed: pair kerning is applied with or without GCP_USEKERNING: width = extent + sum of kern amounts */
      EQ(LOWORD(r),s.cx+ks); EQ(HIWORD(r),s.cy);
      EQ(sum,s.cx+ks); EQ(g.r.nGlyphs,(UINT)n);
      CHECK(!memcmp(g.out,k_txt,n));
      int acc=0,okc=1; for(int i=0;i<n;i++){ if(g.caret[i]!=acc) okc=0; acc+=g.dx[i]; }
      t_note("caret positions equal running dx sum: %d", okc);
    }
    fd_close(&d);
  }
  if(!tested) t_skip("none of Courier New, Arial, Times New Roman realised without complex-script flags");
}

TEST(gcpa_justify_maxextent){
  REQUIRE(U(GetCharacterPlacementA)); REQUIRE(U(GetFontLanguageInfo));
  fd d; if(!fd_open(&d,L"Arial")){ fd_close(&d); t_skip("Arial unavailable"); return; }
  if(!fontlang_ok(d.dc)){ fd_close(&d); t_skip("unexpected GetFontLanguageInfo flag set"); return; }
  int n=(int)strlen(k_txt); SIZE s; U(GetTextExtentPoint32A)(d.dc,k_txt,n,&s);
  gcpa g; gcpa_init(&g,n);
  DWORD base=U(GetCharacterPlacementA)(d.dc,k_txt,n,0,&g.r,0); int bw=LOWORD(base);
  /* MAXEXTENT alone, wider than the string: no clipping, natural width */
  gcpa_init(&g,n);
  DWORD r=U(GetCharacterPlacementA)(d.dc,k_txt,n,bw+60,&g.r,GCP_MAXEXTENT);
  int sum=0; for(int i=0;i<n;i++) sum+=g.dx[i];
  t_note("MAXEXTENT wide: ret=0x%08lx natural %d nMaxFit=%u sum(dx)=%d", r, bw, g.r.nMaxFit, sum);
  EQ(LOWORD(r),bw); EQ(sum,bw); EQ(g.r.nMaxFit,(UINT)n);
  /* JUSTIFY|MAXEXTENT: the string is stretched to exactly nMaxExtent, wider or equal-to-natural */
  int ext[3]={bw+60,bw,bw+1};
  for(int k=0;k<3;k++){
    gcpa_init(&g,n);
    r=U(GetCharacterPlacementA)(d.dc,k_txt,n,ext[k],&g.r,GCP_JUSTIFY|GCP_MAXEXTENT);
    sum=0; for(int i=0;i<n;i++) sum+=g.dx[i];
    t_note("JUSTIFY|MAXEXTENT %d (natural %d): ret=0x%08lx sum(dx)=%d nMaxFit=%u", ext[k], bw, r, sum, g.r.nMaxFit);
    EQ(LOWORD(r),ext[k]); EQ(sum,ext[k]); EQ(HIWORD(r),s.cy);
  }
  /* MAXEXTENT narrower: nMaxFit < n, the fitting prefix width <= nMaxExtent */
  gcpa_init(&g,n);
  r=U(GetCharacterPlacementA)(d.dc,k_txt,n,bw/2,&g.r,GCP_MAXEXTENT);
  sum=0; for(int i=0;i<(int)g.r.nMaxFit;i++) sum+=g.dx[i];
  t_note("MAXEXTENT half: ret=0x%08lx nMaxFit=%u sum(prefix dx)=%d half=%d", r, g.r.nMaxFit, sum, bw/2);
  CHECKF(g.r.nMaxFit>0 && g.r.nMaxFit<(UINT)n, "nMaxFit %u not within (0,%d)", g.r.nMaxFit, n);
  CHECKF(sum<=bw/2, "prefix width %d exceeds nMaxExtent %d", sum, bw/2);
  fd_close(&d);
}

TEST(gcp_a_vs_w_glyphshape){
  REQUIRE(U(GetCharacterPlacementA)); REQUIRE(U(GetCharacterPlacementW)); REQUIRE(U(GetFontLanguageInfo));
  fd d; if(!fd_open(&d,L"Arial")){ fd_close(&d); t_skip("Arial unavailable"); return; }
  if(!fontlang_ok(d.dc)){ fd_close(&d); t_skip("unexpected GetFontLanguageInfo flag set"); return; }
  int n=(int)strlen(k_txt); WCHAR wt[64]; for(int i=0;i<=n;i++) wt[i]=(unsigned char)k_txt[i];
  int flagsets[2]={ 0, GCP_GLYPHSHAPE };
  for(int k=0;k<2;k++){
    gcpa a; gcpw w; gcpa_init(&a,n); gcpw_init(&w,n);
    a.glyphs[0]=0; w.glyphs[0]=0;
    DWORD ra=U(GetCharacterPlacementA)(d.dc,k_txt,n,0,&a.r,flagsets[k]);
    DWORD rw=U(GetCharacterPlacementW)(d.dc,wt,n,0,&w.r,flagsets[k]);
    t_note("flags 0x%x: ret A=0x%08lx W=0x%08lx nGlyphs A=%u W=%u", flagsets[k], ra, rw, a.r.nGlyphs, w.r.nGlyphs);
    EQ(ra,rw); EQ(a.r.nGlyphs,w.r.nGlyphs);
    CHECKF(!memcmp(a.dx,w.dx,n*sizeof(int)), "lpDx differ (flags 0x%x)", flagsets[k]);
    if(flagsets[k]&GCP_GLYPHSHAPE){
      CHECKF(!memcmp(a.glyphs,w.glyphs,n*sizeof(WCHAR)), "lpGlyphs differ");
      t_note("glyphs[0..3] = %04x %04x %04x %04x", w.glyphs[0],w.glyphs[1],w.glyphs[2],w.glyphs[3]);
    }
    for(int i=0;i<n;i++) if(w.out[i]!=wt[i]){ CHECKF(0,"W out string differs at %d",i); break; }
  }
  fd_close(&d);
}

TEST(gcpw_kerning){
  REQUIRE(U(GetCharacterPlacementW)); REQUIRE(U(GetKerningPairsW)); REQUIRE(U(GetFontLanguageInfo));
  fd d; if(!fd_open(&d,L"Times New Roman") || !has_kern(d.dc)){ fd_close(&d); t_skip("Times New Roman with kern table unavailable"); return; }
  if(!fontlang_ok(d.dc)){ fd_close(&d); t_skip("unexpected GetFontLanguageInfo flag set"); return; }
  const WCHAR *t=L"AVAVAV"; int n=6; SIZE s; ext_w(d.dc,t,&s); int ks=kern_sum(d.dc,t,n);
  int flags[2]={0,GCP_USEKERNING};
  for(int q=0;q<2;q++){
    gcpw g; gcpw_init(&g,n);
    DWORD r=U(GetCharacterPlacementW)(d.dc,t,n,0,&g.r,flags[q]);
    int sum=0; for(int i=0;i<n;i++) sum+=g.dx[i];
    t_note("AVAVAV flags 0x%x: ret x=%u sum=%d extent %ld kern sum %d", flags[q], LOWORD(r), sum, s.cx, ks);
    CHECKF(ks<0, "kern sum %d not negative", ks);
    EQ(LOWORD(r),s.cx+ks); EQ(sum,s.cx+ks);   /* observed: kerned with and without GCP_USEKERNING */
  }
  fd_close(&d);
}

TEST(gcp_empty_and_bad){
  REQUIRE(U(GetCharacterPlacementA));
  fd d; if(!fd_open(&d,L"Arial")){ fd_close(&d); t_skip("Arial unavailable"); return; }
  SET_ERR(0); DWORD r=U(GetCharacterPlacementA)(d.dc,"",0,0,NULL,0);
  t_note("GetCharacterPlacementA(empty, NULL results): ret=0x%lx err=%lu", r, GetLastError());
  SET_ERR(0); r=U(GetCharacterPlacementA)(NULL,"abc",3,0,NULL,0);
  t_note("GetCharacterPlacementA(NULL dc): ret=0x%lx err=%lu", r, GetLastError());
  EQ(r,0);
  fd_close(&d);
}

/* ------------------------------------------------------------ character extra / justification */
TEST(char_extra){
  REQUIRE(U(SetTextCharacterExtra)); REQUIRE(U(GetTextCharacterExtra));
  fd d; if(!fd_open(&d,L"Arial")){ fd_close(&d); t_skip("Arial unavailable"); return; }
  HDC dc=d.dc; const WCHAR *t=L"abcde"; int n=5; SIZE s0,s1,s2;
  EQ(U(GetTextCharacterExtra)(dc),0);
  ext_w(dc,t,&s0);
  EQ(U(SetTextCharacterExtra)(dc,3),0);       /* previous */
  EQ(U(GetTextCharacterExtra)(dc),3);
  EQ(U(SetTextCharacterExtra)(dc,5),3);
  EQ(U(GetTextCharacterExtra)(dc),5);
  ext_w(dc,t,&s1);
  t_note("extent base %ld, extra 5: %ld", s0.cx, s1.cx);
  EQ(s1.cx,s0.cx+n*5); EQ(s1.cy,s0.cy);
  EQ(U(SetTextCharacterExtra)(dc,-1),5);
  EQ(U(GetTextCharacterExtra)(dc),-1);
  ext_w(dc,t,&s2);
  EQ(s2.cx,s0.cx-n);
  /* single char grows by exactly the extra */
  SIZE a0,a1; U(SetTextCharacterExtra)(dc,0); ext_w(dc,L"a",&a0); U(SetTextCharacterExtra)(dc,4); ext_w(dc,L"a",&a1);
  EQ(a1.cx,a0.cx+4);
  /* GetCharWidth32 does not include the extra? observe */
  int cw; U(GetCharWidth32W)(dc,'a','a',&cw); t_note("GetCharWidth32W('a') with extra 4: %d (base %ld)", cw, a0.cx); EQ(cw,a0.cx);
  /* GetTextExtentExPoint */
  int fit=0, dx[8]; SIZE se; U(GetTextExtentExPointW)(dc,t,n,1000,&fit,dx,&se);
  t_note("GetTextExtentExPointW with extra 4: total %ld dx[4]=%d fit=%d", se.cx, dx[4], fit);
  EQ(se.cx,a0.cx*0+ (s0.cx+n*4)); EQ(dx[4],se.cx);
  /* DC-state: independent per DC */
  HDC dc2=U(CreateCompatibleDC)(NULL); EQ(U(GetTextCharacterExtra)(dc2),0); U(DeleteDC)(dc2);
  /* a text-out call does not reset the extra */
  U(TextOutW)(dc,0,0,t,n); t_note("extra after TextOut: %d", U(GetTextCharacterExtra)(dc));
  EQ(U(GetTextCharacterExtra)(dc),4);
  /* invalid DC */
  SET_ERR(0); int r=U(GetTextCharacterExtra)(NULL); t_note("GetTextCharacterExtra(NULL): 0x%x err=%lu", r, GetLastError());
  EQ((unsigned)r,0x80000000u);
  SET_ERR(0); r=U(SetTextCharacterExtra)(NULL,1); t_note("SetTextCharacterExtra(NULL): 0x%x err=%lu", r, GetLastError());
  EQ((unsigned)r,0x80000000u);
  U(SetTextCharacterExtra)(dc,0);
  fd_close(&d);
}

TEST(text_justification){
  REQUIRE(U(SetTextJustification));
  fd d; if(!fd_open(&d,L"Arial")){ fd_close(&d); t_skip("Arial unavailable"); return; }
  HDC scr=U(GetDC)(NULL); HDC dc=d.dc; const WCHAR *t=L"ab cd ef"; int n=8; SIZE s0,s1,s2,s3,s4;
  ext_w(dc,t,&s0);
  EQ(U(SetTextJustification)(dc,12,2),TRUE);
  ext_w(dc,t,&s1); ext_w(dc,t,&s2);
  t_note("base %ld; extra 12 over 2 breaks: first %ld second %ld", s0.cx, s1.cx, s2.cx);
  EQ(s1.cx,s0.cx+12);
  EQ(s2.cx,s1.cx);                       /* persists across extent queries */
  /* a string with no break char is unchanged */
  ext_w(dc,L"abcdef",&s3); SIZE sb; U(SetTextJustification)(dc,0,0); ext_w(dc,L"abcdef",&sb);
  EQ(s3.cx,sb.cx);
  /* extra smaller than the number of breaks (remainder distribution): total still grows by extra */
  U(SetTextJustification)(dc,1,2); ext_w(dc,t,&s4); EQ(s4.cx,s0.cx+1);
  /* cleared by explicit 0 */
  U(SetTextJustification)(dc,0,0); ext_w(dc,t,&s4); EQ(s4.cx,s0.cx);
  /* TextOut: see below */
  U(SetTextJustification)(dc,12,2); ext_w(dc,t,&s1);
  U(TextOutW)(dc,0,0,t,n);
  ext_w(dc,t,&s2);
  t_note("default 1x1 bitmap: before TextOut %ld after TextOut %ld (base %ld)", s1.cx, s2.cx, s0.cx);
  HBITMAP bm=U(CreateCompatibleBitmap)(scr,300,60), obm=(HBITMAP)U(SelectObject)(dc,bm);
  U(SetTextJustification)(dc,12,2); ext_w(dc,t,&s1);
  U(TextOutW)(dc,0,0,t,n);
  ext_w(dc,t,&s2);
  t_note("300x60 bitmap: before TextOut %ld after TextOut %ld (base %ld)", s1.cx, s2.cx, s0.cx);
  EQ(s1.cx,s0.cx+12);
  EQ(s2.cx,s1.cx);   /* observed (differs from the documentation): TextOut does not clear the setting */
  U(SelectObject)(dc,obm); U(DeleteObject)(bm); U(ReleaseDC)(NULL,scr);
  /* with char extra both apply */
  U(SetTextCharacterExtra)(dc,2); U(SetTextJustification)(dc,10,2); ext_w(dc,t,&s3);
  EQ(s3.cx,s0.cx+10+2*n);
  U(SetTextCharacterExtra)(dc,0); U(SetTextJustification)(dc,0,0);
  SET_ERR(0); BOOL b=U(SetTextJustification)(NULL,1,1); t_note("SetTextJustification(NULL): %d err=%lu", b, GetLastError());
  EQ(b,FALSE);
  fd_close(&d);
}

/* ------------------------------------------------------------ layout */
UDECL(DWORD, SetLayoutWidth, (HDC, int, int));
TEST(layout){
  REQUIRE(U(SetLayout)); REQUIRE(U(GetLayout));
  HDC scr=U(GetDC)(NULL); HDC mem=U(CreateCompatibleDC)(scr);
  DWORD def=U(GetLayout)(mem); t_note("memory DC default layout 0x%lx", def);
  EQ(def,0);
  DWORD scrl=U(GetLayout)(scr); t_note("screen DC layout 0x%lx", scrl);
  EQ(scrl,0);
  EQ(U(SetLayout)(mem,LAYOUT_RTL),0);
  EQ(U(GetLayout)(mem),LAYOUT_RTL);
  EQ(U(SetLayout)(mem,LAYOUT_RTL|LAYOUT_BITMAPORIENTATIONPRESERVED),LAYOUT_RTL);
  DWORD g=U(GetLayout)(mem); t_note("layout after RTL|BITMAPORIENTATIONPRESERVED = 0x%lx", g);
  EQ(g,LAYOUT_RTL|LAYOUT_BITMAPORIENTATIONPRESERVED);
  EQ(U(SetLayout)(mem,0),LAYOUT_RTL|LAYOUT_BITMAPORIENTATIONPRESERVED);
  EQ(U(GetLayout)(mem),0);
  /* layout is per DC */
  HDC mem2=U(CreateCompatibleDC)(scr); U(SetLayout)(mem,LAYOUT_RTL);
  EQ(U(GetLayout)(mem2),0);
  /* a DC created compatible with an RTL DC: observe */
  HDC mem3=U(CreateCompatibleDC)(mem); t_note("DC compatible with RTL DC: layout 0x%lx", U(GetLayout)(mem3)); U(DeleteDC)(mem3);
  /* RTL does not change text extents */
  fd d; if(fd_open(&d,L"Arial")){ SIZE a,b; ext_w(d.dc,L"Hello",&a); U(SetLayout)(d.dc,LAYOUT_RTL); ext_w(d.dc,L"Hello",&b);
    t_note("extent LTR %ld RTL %ld", a.cx, b.cx); EQ(a.cx,b.cx); U(SetLayout)(d.dc,0); } fd_close(&d);
  U(SetLayout)(mem,0);
  /* invalid handles */
  SET_ERR(0); DWORD r=U(GetLayout)(NULL); t_note("GetLayout(NULL): 0x%lx err=%lu", r, GetLastError());
  EQ(r,GDI_ERROR); 
  SET_ERR(0); r=U(SetLayout)(NULL,0); t_note("SetLayout(NULL,0): 0x%lx err=%lu", r, GetLastError());
  EQ(r,GDI_ERROR);
  /* enhanced metafile DC */
  HDC mf=U(CreateEnhMetaFileW)(NULL,NULL,NULL,NULL);
  if(mf){
    SET_ERR(0); DWORD ml=U(GetLayout)(mf); DWORD e=GetLastError(); t_note("metafile DC GetLayout: 0x%lx err=%lu", ml, e);
    SET_ERR(0); DWORD ms=U(SetLayout)(mf,LAYOUT_RTL); t_note("metafile DC SetLayout(RTL): 0x%lx err=%lu", ms, GetLastError());
    HENHMETAFILE h=U(CloseEnhMetaFile)(mf); if(h) U(DeleteEnhMetaFile)(h);
  } else t_note("CreateEnhMetaFileW failed");
  U(DeleteDC)(mem2); U(DeleteDC)(mem); U(ReleaseDC)(NULL,scr);
}

/* SetLayoutWidth is undocumented: observation only (decomp: SetLayoutWidth(hdc, a, b) -> NtGdiSetLayout(hdc, a, b);
 * the decompiled code sets ERROR_INVALID_PARAMETER for a handle that is not a DC; not exercised here, and a NULL DC was
 * observed to return GDI_ERROR with the last error left at 0). No assertions. */
TEST(layout_width_observe){
  REQUIRE(U(GetLayout)); REQUIRE(U(SetLayout));
  SetLayoutWidth_pfn f=UN(SetLayoutWidth); if(!f){ t_skip("SetLayoutWidth not exported"); return; }
  HDC mem=U(CreateCompatibleDC)(NULL);
  SET_ERR(0); DWORD r=f(NULL,-1,0); t_note("SetLayoutWidth(NULL,-1,0): 0x%lx err=%lu", r, GetLastError());
  SET_ERR(0); r=f(mem,-1,0); t_note("SetLayoutWidth(mem,-1,0): ret 0x%lx err=%lu layout now 0x%lx", r, GetLastError(), U(GetLayout)(mem));
  SET_ERR(0); r=f(mem,-1,LAYOUT_RTL); t_note("SetLayoutWidth(mem,-1,RTL): ret 0x%lx err=%lu layout now 0x%lx", r, GetLastError(), U(GetLayout)(mem));
  SET_ERR(0); r=f(mem,100,0); t_note("SetLayoutWidth(mem,100,0): ret 0x%lx err=%lu layout now 0x%lx", r, GetLastError(), U(GetLayout)(mem));
  U(SetLayout)(mem,0);
  U(DeleteDC)(mem);
}
