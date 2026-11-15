#include "t.h"
#include "gir.h"
/* Undocumented exports, declared locally. CHWIDTHINFO (12 bytes) is the structure the kernel entry point
 * NtGdiGetCharWidthInfo fills. */
typedef struct { LONG lMaxNegA, lMaxNegC, lMinWidthD; } CHWIDTHINFO_;
UDECL(BOOL, GetCharWidthInfo, (HDC, CHWIDTHINFO_*));
UDECL(UINT, GdiGetCodePage, (HDC));
UDECL(UINT, GetCodePage, (HDC));
UDECL(LONG, RtlGetVersion, (OSVERSIONINFOW*));

/* TranslateCharsetInfo's table and the font-signature bits depend on the build and the ANSI code page. */
static int env_ok(char *why, size_t cap){
  OSVERSIONINFOW vi; memset(&vi,0,sizeof vi); vi.dwOSVersionInfoSize=sizeof vi;
  if(!UN(RtlGetVersion)||UN(RtlGetVersion)(&vi)) { snprintf(why,cap,"cannot read the OS build"); return 0; }
  if(vi.dwBuildNumber<19041||vi.dwBuildNumber>19045){ snprintf(why,cap,"build %lu is not 19041..19045",vi.dwBuildNumber); return 0; }
  if(GetACP()!=1252){ snprintf(why,cap,"ANSI code page %u is not 1252",GetACP()); return 0; }
  return 1;
}

/* ---------------------------------------------------------------- GetTextFaceW / GetTextFaceA */
TEST(text_face_counts){
  gir_dc d; char why[160];
  if(!gir_open(&d,L"Arial",-16,why,sizeof why)){ t_skip("%s",why); gir_close(&d); return; }
  WCHAR w[32]; char a[32];
  /* size query: characters including the terminating NUL (docs), same for A */
  EXPECT_EQ(U(GetTextFaceW)(d.dc,0,NULL),6);
  EXPECT_EQ(U(GetTextFaceA)(d.dc,0,NULL),6);
  /* W: a short buffer gets a truncated, always NUL-terminated name; the return value is the count copied including the NUL (observed) */
  for(int n=1;n<=8;n++){
    for(int i=0;i<32;i++) w[i]=0xAAAA;
    int r=U(GetTextFaceW)(d.dc,n,w); int want=n<6?n:6;
    CHECKF(r==want, "GetTextFaceW(n=%d) returned %d want %d", n, r, want);
    CHECKF(!memcmp(w,L"Arial",2*(want-1)) && w[want-1]==0, "GetTextFaceW(n=%d) buffer", n);
  }
  /* A: same copy, but the return value excludes the NUL (observed; differs from the W variant and from the size query) */
  for(int n=1;n<=8;n++){
    memset(a,0x55,sizeof a);
    int r=U(GetTextFaceA)(d.dc,n,a); int want=(n<6?n:6)-1;
    CHECKF(r==want, "GetTextFaceA(n=%d) returned %d want %d", n, r, want);
    CHECKF(!memcmp(a,"Arial",want) && a[want]==0, "GetTextFaceA(n=%d) buffer", n);
  }
  t_note("GetTextFaceA returns the length without the NUL for a buffer but 6 (with the NUL) for the size query");
  EXPECT_EQ(U(GetTextFaceW)(d.dc,32,w),6); EXPECT_WSTR(w,L"Arial");
  EXPECT_EQ(U(GetTextFaceA)(d.dc,32,a),5); EXPECT_STR(a,"Arial");
  SetLastError(0); EXPECT_EQ(U(GetTextFaceW)(d.dc,-1,w),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  EXPECT_EQ(U(GetTextFaceW)(NULL,32,w),0);
  EXPECT_EQ(U(GetTextFaceW)(d.dc,32,NULL),6);
  gir_close(&d);
}

static const WCHAR *k_tt_[]={L"Arial",L"Segoe UI"};
/* ---------------------------------------------------------------- GetFontLanguageInfo */
TEST(font_language_info){
  gir_dc d; char why[160];
  if(!gir_open(&d,L"Arial",-16,why,sizeof why)){ t_skip("%s",why); gir_close(&d); return; }
  const DWORD allowed=FLI_MASK|FLI_GLYPHS|GCP_ERROR;
  for(unsigned fi=0;fi<2;fi++){
    HFONT f=gir_mkfont(k_tt_[fi],-16,400,DEFAULT_CHARSET), o=(HFONT)U(SelectObject)(d.dc,f);
    DWORD fli=U(GetFontLanguageInfo)(d.dc);
    DWORD kp=U(GetKerningPairsW)(d.dc,0,NULL);
    CHECKF(fli&FLI_GLYPHS, "%ls: FLI_GLYPHS missing (0x%lx)", k_tt_[fi], fli);
    CHECKF(!(fli&~allowed), "%ls: undefined bits in 0x%lx", k_tt_[fi], fli);
    CHECKF(!!(fli&GCP_USEKERNING)==(kp>0&&kp!=GDI_ERROR), "%ls: GCP_USEKERNING %d vs %lu kerning pairs", k_tt_[fi], !!(fli&GCP_USEKERNING), kp);
    EXPECT_EQ(fli,0x40008);
    U(SelectObject)(d.dc,o); U(DeleteObject)(f);
  }
  HFONT f=gir_mkfont(L"Symbol",-16,400,SYMBOL_CHARSET), o=(HFONT)U(SelectObject)(d.dc,f);
  WCHAR sn[LF_FACESIZE]=L""; U(GetTextFaceW)(d.dc,LF_FACESIZE,sn);
  DWORD fli=U(GetFontLanguageInfo)(d.dc); U(SelectObject)(d.dc,o); U(DeleteObject)(f);
  if(lstrcmpiW(sn,L"Symbol")) t_skip("Symbol not realised (%ls)", sn);
  else { t_note("Symbol: GetFontLanguageInfo 0x%lx", fli); EXPECT_EQ(fli,FLI_GLYPHS); }
  f=gir_mkfont(L"Terminal",-12,400,OEM_CHARSET); o=(HFONT)U(SelectObject)(d.dc,f);
  WCHAR n[32]; U(GetTextFaceW)(d.dc,32,n);
  if(!lstrcmpiW(n,L"Terminal")){ fli=U(GetFontLanguageInfo)(d.dc); EXPECT_EQ(fli,0); } else t_note("Terminal not realised (%ls)", n);
  U(SelectObject)(d.dc,o); U(DeleteObject)(f);
  EXPECT_EQ(U(GetFontLanguageInfo)(NULL),GCP_ERROR);   /* docs: GCP_ERROR on failure */
  gir_close(&d);
}

/* ---------------------------------------------------------------- GetCharWidthInfo */
TEST(char_width_info){
  gir_dc d; char why[160];
  if(!UN(GetCharWidthInfo)){ t_skip("GetCharWidthInfo not exported"); return; }
  static const WCHAR *faces[]={L"Arial",L"Segoe UI",L"Times New Roman"};
  int done=0;
  for(unsigned fi=0;fi<3;fi++){
    int first[3]={0,0,0}; 
    for(int hi=0;hi<2;hi++){
      int h=hi?-96:-48;
      if(!gir_open(&d,faces[fi],h,why,sizeof why)){ t_note("%s",why); gir_close(&d); break; }
      DWORD len; BYTE *mp=gir_fontdata(d.dc,GIR_TAG('m','a','x','p'),&len); int ng=(mp&&len>=6)?(mp[4]<<8)|mp[5]:0; free(mp);
      ABC *abc=(ABC*)malloc(sizeof(ABC)*(ng+1));
      CHWIDTHINFO_ w={0x7777,0x7777,0x7777};
      BOOL ok=UN(GetCharWidthInfo)(d.dc,&w);
      if(ng && U(GetCharABCWidthsI)(d.dc,0,ng,NULL,abc)){
        int minA=1<<30,minC=1<<30,minAdv=1<<30;
        for(int i=0;i<ng;i++){ int adv=abc[i].abcA+(int)abc[i].abcB+abc[i].abcC; if(abc[i].abcA<minA)minA=abc[i].abcA; if(abc[i].abcC<minC)minC=abc[i].abcC; if(adv>0&&adv<minAdv)minAdv=adv; }
        EXPECT_EQ(ok,TRUE);
        t_note("%ls h %d: CHWIDTHINFO %ld %ld %ld; over %d glyphs min A %d min C %d min nonzero advance %d", faces[fi],h,w.lMaxNegA,w.lMaxNegC,w.lMinWidthD,ng,minA,minC,minAdv);
        CHECKF(abs(w.lMaxNegA-minA)<=1, "%ls h %d lMaxNegA %ld vs min abcA %d", faces[fi],h,w.lMaxNegA,minA);
        CHECKF(abs(w.lMaxNegC-minC)<=1, "%ls h %d lMaxNegC %ld vs min abcC %d", faces[fi],h,w.lMaxNegC,minC);
        CHECKF(abs(w.lMinWidthD-minAdv)<=1, "%ls h %d lMinWidthD %ld vs min nonzero advance %d", faces[fi],h,w.lMinWidthD,minAdv);
        CHECK(w.lMaxNegA<=0 && w.lMaxNegC<=0 && w.lMinWidthD>0);
        done++;
      } else CHECKF(0,"%ls: GetCharABCWidthsI failed (glyphs %d)", faces[fi], ng);
      if(hi==0){ first[0]=w.lMaxNegA; first[1]=w.lMaxNegC; first[2]=w.lMinWidthD; }
      else { CHECK(abs(w.lMaxNegA-2*first[0])<=2); CHECK(abs(w.lMaxNegC-2*first[1])<=2); CHECK(abs(w.lMinWidthD-2*first[2])<=2); }
      free(abc); gir_close(&d);
    }
  }
  if(!done) { t_skip("no TrueType face verified"); return; }
  HDC dc=U(GetDC)(NULL); CHWIDTHINFO_ w;
  SetLastError(0); EXPECT_EQ(UN(GetCharWidthInfo)(dc,NULL),FALSE);
  EXPECT_EQ(UN(GetCharWidthInfo)(NULL,&w),FALSE);
  HFONT f=gir_mkfont(L"Terminal",-12,400,OEM_CHARSET), o=(HFONT)U(SelectObject)(dc,f); WCHAR n[32]; U(GetTextFaceW)(dc,32,n);
  if(!lstrcmpiW(n,L"Terminal")){ w.lMaxNegA=w.lMaxNegC=w.lMinWidthD=0x7777; EXPECT_EQ(UN(GetCharWidthInfo)(dc,&w),TRUE); EXPECT_EQ(w.lMaxNegA,0); EXPECT_EQ(w.lMaxNegC,0); EXPECT_EQ(w.lMinWidthD,0); }
  U(SelectObject)(dc,o); U(DeleteObject)(f); U(ReleaseDC)(NULL,dc);
}

/* ---------------------------------------------------------------- GetRasterizerCaps */
/* The kernel side (decompiled NtGdiGetRasterizerCaps) copies min(cb,6) bytes of {nSize=6, wFlags, nLanguageID}
 * to the caller and probes the buffer for 4-byte alignment: a misaligned pointer makes the call FALSE. A
 * RASTERIZER_STATUS on the stack has only 2-byte alignment, so the tests use an explicitly aligned buffer. */
TEST(rasterizer_caps){
  const char *mods[]={"gdi32.dll","gdi32full.dll"};
  EXPECT_EQ(sizeof(RASTERIZER_STATUS),6);
  for(int m=0;m<2;m++){
    BOOL (WINAPI *p)(LPRASTERIZER_STATUS,UINT)=(BOOL (WINAPI*)(LPRASTERIZER_STATUS,UINT))t_proc_in(mods[m],"GetRasterizerCaps");
    if(!p){ t_note("%s does not export GetRasterizerCaps", mods[m]); continue; }
    union { DWORD d[4]; BYTE b[16]; RASTERIZER_STATUS rs; } u;
    memset(&u,0xAA,sizeof u); SetLastError(0);
    BOOL r=p(&u.rs,sizeof u.rs);
    CHECKF(r, "%s: GetRasterizerCaps(aligned, 6) returned FALSE", mods[m]);
    EXPECT_EQ(u.rs.nSize,sizeof(RASTERIZER_STATUS));
    CHECKF((u.rs.wFlags&(TT_ENABLED|TT_AVAILABLE))==(TT_ENABLED|TT_AVAILABLE), "%s: wFlags 0x%x", mods[m], u.rs.wFlags);
    CHECKF((u.rs.wFlags&~(RASTER_FONTTYPE|TT_ENABLED|TT_AVAILABLE|1|2|4))==0, "%s: undefined wFlags bits 0x%x", mods[m], u.rs.wFlags);
    t_note("%s: nSize %d wFlags 0x%x nLanguageID 0x%x (system default lang 0x%x)", mods[m], u.rs.nSize, u.rs.wFlags, u.rs.nLanguageID, GetSystemDefaultLangID());
    EXPECT_EQ(u.rs.nLanguageID,GetSystemDefaultLangID());
    CHECK(u.b[6]==0xAA && u.b[7]==0xAA);          /* nothing written past 6 bytes */
    /* smaller byte counts copy a prefix and still succeed */
    RASTERIZER_STATUS full=u.rs;
    for(unsigned cb=1;cb<=5;cb++){
      memset(&u,0xAA,sizeof u); r=p(&u.rs,cb);
      CHECKF(r, "%s: cb=%u returned FALSE", mods[m], cb);
      CHECKF(!memcmp(&u,&full,cb) && u.b[cb]==0xAA, "%s: cb=%u did not copy exactly %u prefix bytes", mods[m], cb, cb);
    }
    /* larger than the structure: 6 bytes are copied */
    memset(&u,0xAA,sizeof u); r=p(&u.rs,12); CHECK(r); CHECK(!memcmp(&u,&full,6) && u.b[6]==0xAA);
    /* zero bytes, NULL buffer, misaligned buffer: FALSE */
    memset(&u,0xAA,sizeof u);
    EXPECT_EQ(p(&u.rs,0),FALSE); CHECK(u.b[0]==0xAA);
    EXPECT_EQ(p(NULL,6),FALSE);
    memset(&u,0xAA,sizeof u); BOOL rm=p((RASTERIZER_STATUS*)(u.b+2),6);
    t_note("%s: 2-byte aligned buffer returns %d", mods[m], rm);
    EXPECT_EQ(rm,FALSE);
  }
}

/* ---------------------------------------------------------------- GetTextCharset / GetTextCharsetInfo / GetCodePage */
static HFONT g_old;
static int sel(HDC dc, const WCHAR *face, int cs, WCHAR *got){
  HFONT f=gir_mkfont(face,-16,400,cs); g_old=(HFONT)U(SelectObject)(dc,f);
  U(GetTextFaceW)(dc,LF_FACESIZE,got); return 1;
}
static void unsel(HDC dc){ HFONT f=(HFONT)U(SelectObject)(dc,g_old); U(DeleteObject)(f); }

TEST(text_charset_and_info){
  HDC dc=U(GetDC)(NULL); WCHAR got[LF_FACESIZE]; char why[160];
  if(!env_ok(why,sizeof why)){ U(ReleaseDC)(NULL,dc); t_skip("%s",why); return; }
  FONTSIGNATURE af; memset(&af,0,sizeof af);
  /* Arial, ANSI and DEFAULT (ACP 1252 -> ANSI) */
  int cses[]={ANSI_CHARSET,DEFAULT_CHARSET};
  for(int i=0;i<2;i++){
    sel(dc,L"Arial",cses[i],got);
    if(lstrcmpiW(got,L"Arial")){ t_note("Arial not realised (%ls)",got); unsel(dc); continue; }
    FONTSIGNATURE fs; memset(&fs,0xAA,sizeof fs);
    EXPECT_EQ(U(GetTextCharset)(dc),ANSI_CHARSET);
    EXPECT_EQ(U(GetTextCharsetInfo)(dc,&fs,0),ANSI_CHARSET);
    CHECK(fs.fsCsb[0]&1);                 /* Latin 1 bit of the code page signature (docs: FONTSIGNATURE) */
    CHECK(!(fs.fsCsb[0]&0x80000000u));   /* not symbol */
    TEXTMETRICW tm; U(GetTextMetricsW)(dc,&tm); EXPECT_EQ(tm.tmCharSet,ANSI_CHARSET);
    if(i==0) af=fs; else CHECK(!memcmp(&af,&fs,sizeof fs));   /* the signature belongs to the face: same for both requests */
    EXPECT_EQ(U(GetTextCharsetInfo)(dc,NULL,0),ANSI_CHARSET);
    unsel(dc);
  }
  /* Greek: the charset is the requested one, the signature stays the face's own */
  sel(dc,L"Arial",GREEK_CHARSET,got);
  if(!lstrcmpiW(got,L"Arial")){ FONTSIGNATURE fs; EXPECT_EQ(U(GetTextCharset)(dc),GREEK_CHARSET); EXPECT_EQ(U(GetTextCharsetInfo)(dc,&fs,0),GREEK_CHARSET); CHECK(!memcmp(&af,&fs,sizeof fs)); }
  unsel(dc);
  /* SYMBOL_CHARSET: realised as a symbol face, signature has only the symbol bit */
  sel(dc,L"Symbol",SYMBOL_CHARSET,got);
  if(!lstrcmpiW(got,L"Symbol")){ FONTSIGNATURE fs; memset(&fs,0xAA,sizeof fs);
    EXPECT_EQ(U(GetTextCharset)(dc),SYMBOL_CHARSET); EXPECT_EQ(U(GetTextCharsetInfo)(dc,&fs,0),SYMBOL_CHARSET);
    EXPECT_EQ(fs.fsCsb[0],0x80000000u); EXPECT_EQ(fs.fsCsb[1],0); EXPECT_EQ(fs.fsUsb[0]|fs.fsUsb[1]|fs.fsUsb[2]|fs.fsUsb[3],0); }
  else t_skip("Symbol not realised (%ls)",got);
  unsel(dc);
  /* a different face with SYMBOL_CHARSET is replaced by a symbol face */
  sel(dc,L"Arial",SYMBOL_CHARSET,got); t_note("Arial + SYMBOL_CHARSET realised as %ls", got);
  EXPECT_EQ(U(GetTextCharset)(dc),SYMBOL_CHARSET); EXPECT_NE(lstrcmpiW(got,L"Arial"),0);
  unsel(dc);
  /* bitmap face: Terminal, OEM charset, no signature */
  sel(dc,L"Terminal",OEM_CHARSET,got);
  if(!lstrcmpiW(got,L"Terminal")){ FONTSIGNATURE fs; memset(&fs,0xAA,sizeof fs);
    EXPECT_EQ(U(GetTextCharset)(dc),OEM_CHARSET); EXPECT_EQ(U(GetTextCharsetInfo)(dc,&fs,0),OEM_CHARSET);
    DWORD z=0; for(unsigned i=0;i<4;i++) z|=fs.fsUsb[i]; z|=fs.fsCsb[0]|fs.fsCsb[1]; EXPECT_EQ(z,0); }
  else t_note("Terminal not realised (%ls)",got);
  unsel(dc);
  /* invalid DC: DEFAULT_CHARSET, ERROR_INVALID_HANDLE (observed) */
  SetLastError(0); EXPECT_EQ(U(GetTextCharset)(NULL),DEFAULT_CHARSET); EXPECT_ERR(ERROR_INVALID_HANDLE);
  SetLastError(0); EXPECT_EQ(U(GetTextCharsetInfo)(NULL,NULL,0),DEFAULT_CHARSET); EXPECT_ERR(ERROR_INVALID_HANDLE);
  U(ReleaseDC)(NULL,dc);
}

TEST(gdi_code_page){
  HDC dc=U(GetDC)(NULL); WCHAR got[LF_FACESIZE]; char why[160];
  if(!UN(GdiGetCodePage)||!UN(GetCodePage)){ U(ReleaseDC)(NULL,dc); t_skip("export missing"); return; }
  if(!env_ok(why,sizeof why)){ U(ReleaseDC)(NULL,dc); t_skip("%s",why); return; }
  /* GdiGetCodePage (gdi32) and GetCodePage (gdi32full) are one function; the value is the ACP-derived code page of the DC's font charset. */
  static const struct { const WCHAR *face; int cs; } cases[]={{L"Arial",ANSI_CHARSET},{L"Arial",DEFAULT_CHARSET},{L"Arial",GREEK_CHARSET},{L"Arial",EASTEUROPE_CHARSET},{L"Arial",RUSSIAN_CHARSET},{L"Arial",TURKISH_CHARSET},{L"Arial",HEBREW_CHARSET},{L"Arial",ARABIC_CHARSET},{L"Arial",BALTIC_CHARSET},{L"Symbol",SYMBOL_CHARSET}};
  for(unsigned i=0;i<sizeof cases/sizeof*cases;i++){
    sel(dc,cases[i].face,cases[i].cs,got);
    UINT cs=U(GetTextCharset)(dc); CHARSETINFO ci; memset(&ci,0,sizeof ci);
    UINT a=UN(GdiGetCodePage)(dc), b=UN(GetCodePage)(dc);
    if(!U(TranslateCharsetInfo)((DWORD*)(ULONG_PTR)cs,&ci,TCI_SRCCHARSET)){ t_note("%ls cs %d: no ACP for charset %u (GdiGetCodePage %u)",got,cases[i].cs,cs,a); unsel(dc); continue; }
    CHECKF(a==b, "%ls cs %d: GdiGetCodePage %u != GetCodePage %u", got, cases[i].cs, a, b);
    CHECKF(a==ci.ciACP, "%ls cs %d (charset %u): GdiGetCodePage %u, TranslateCharsetInfo ACP %lu", got, cases[i].cs, cs, a, ci.ciACP);
    unsel(dc);
  }
  /* invalid handle: the system ANSI code page and ERROR_INVALID_PARAMETER (decompiled gdi32full!GetCodePage, observed) */
  SetLastError(0); EXPECT_EQ(UN(GdiGetCodePage)((HDC)(ULONG_PTR)0x12345),GetACP()); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  SetLastError(0); EXPECT_EQ(UN(GetCodePage)(NULL),GetACP()); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  /* bitmap OEM font has no ANSI code page */
  sel(dc,L"Terminal",OEM_CHARSET,got);
  if(!lstrcmpiW(got,L"Terminal")) EXPECT_EQ(UN(GdiGetCodePage)(dc),0);
  unsel(dc);
  U(ReleaseDC)(NULL,dc);
}

/* ---------------------------------------------------------------- TranslateCharsetInfo */
static const struct { DWORD cs, acp, csb; } k_tci[]={
  {0,1252,0x00000001},{2,42,0x80000000},{128,932,0x00020000},{129,949,0x00080000},{130,1361,0x00200000},
  {134,936,0x00040000},{136,950,0x00100000},{161,1253,0x00000008},{162,1254,0x00000010},{163,1258,0x00000100},
  {177,1255,0x00000020},{178,1256,0x00000040},{186,1257,0x00000080},{204,1251,0x00000004},{222,874,0x00010000},
  {238,1250,0x00000002},{254,65001,0x04000000}};
#define NTCI ((int)(sizeof k_tci/sizeof*k_tci))
static BOOL tci(DWORD src, DWORD flags, CHARSETINFO *ci){ memset(ci,0xAA,sizeof *ci); return U(TranslateCharsetInfo)((DWORD*)(ULONG_PTR)src,ci,flags); }
static int ci_untouched(const CHARSETINFO *ci){ const BYTE *b=(const BYTE*)ci; for(size_t i=0;i<sizeof *ci;i++) if(b[i]!=0xAA) return 0; return 1; }

TEST(translate_charset_info){
  char why[160]; if(!env_ok(why,sizeof why)){ t_skip("%s",why); return; }
  CHARSETINFO ci;
  for(int i=0;i<NTCI;i++){
    /* by charset (value passed in the pointer argument) */
    BOOL ok=tci(k_tci[i].cs,TCI_SRCCHARSET,&ci);
    CHECKF(ok, "SRCCHARSET %lu failed", k_tci[i].cs);
    if(ok){ EXPECT_EQ(ci.ciCharset,k_tci[i].cs); EXPECT_EQ(ci.ciACP,k_tci[i].acp); EXPECT_EQ(ci.fs.fsCsb[0],k_tci[i].csb); EXPECT_EQ(ci.fs.fsCsb[1],0);
      EXPECT_EQ(ci.fs.fsUsb[0]|ci.fs.fsUsb[1]|ci.fs.fsUsb[2]|ci.fs.fsUsb[3],0); }
    /* by code page */
    ok=tci(k_tci[i].acp,TCI_SRCCODEPAGE,&ci);
    CHECKF(ok, "SRCCODEPAGE %lu failed", k_tci[i].acp);
    if(ok){ EXPECT_EQ(ci.ciCharset,k_tci[i].cs); EXPECT_EQ(ci.ciACP,k_tci[i].acp); EXPECT_EQ(ci.fs.fsCsb[0],k_tci[i].csb); }
    /* by font signature: pointer to two DWORDs, the second zero, the first exactly one table bit */
    DWORD sig[2]={k_tci[i].csb,0}; memset(&ci,0xAA,sizeof ci);
    ok=U(TranslateCharsetInfo)(sig,&ci,TCI_SRCFONTSIG);
    CHECKF(ok, "SRCFONTSIG %08lx failed", k_tci[i].csb);
    if(ok){ EXPECT_EQ(ci.ciCharset,k_tci[i].cs); EXPECT_EQ(ci.ciACP,k_tci[i].acp); EXPECT_EQ(ci.fs.fsCsb[0],k_tci[i].csb); }
  }
  /* documented anchors */
  CHECK(tci(ANSI_CHARSET,TCI_SRCCHARSET,&ci)); EXPECT_EQ(ci.ciACP,1252);
  CHECK(tci(SHIFTJIS_CHARSET,TCI_SRCCHARSET,&ci)); EXPECT_EQ(ci.ciACP,932);
  CHECK(tci(1252,TCI_SRCCODEPAGE,&ci)); EXPECT_EQ(ci.ciCharset,ANSI_CHARSET);
  CHECK(tci(932,TCI_SRCCODEPAGE,&ci)); EXPECT_EQ(ci.ciCharset,SHIFTJIS_CHARSET);
  /* locale source */
  CHECK(tci(0x409,TCI_SRCLOCALE,&ci)); EXPECT_EQ(ci.ciCharset,ANSI_CHARSET); EXPECT_EQ(ci.ciACP,1252);
  CHECK(tci(0x411,TCI_SRCLOCALE,&ci)); EXPECT_EQ(ci.ciCharset,SHIFTJIS_CHARSET); EXPECT_EQ(ci.ciACP,932);
}

TEST(translate_charset_info_failures){
  char why[160]; if(!env_ok(why,sizeof why)){ t_skip("%s",why); return; }
  CHARSETINFO ci;
  /* not in the table: FALSE and the output is not written */
  static const DWORD badcs[]={DEFAULT_CHARSET,OEM_CHARSET,0x7F,0x55,3,0x180};
  for(unsigned i=0;i<sizeof badcs/sizeof*badcs;i++){ BOOL ok=tci(badcs[i],TCI_SRCCHARSET,&ci); CHECKF(!ok, "SRCCHARSET %lu succeeded", badcs[i]); if(!ok) CHECK(ci_untouched(&ci)); }
  static const DWORD badcp[]={437,0,12345,1,65000};
  for(unsigned i=0;i<sizeof badcp/sizeof*badcp;i++){ BOOL ok=tci(badcp[i],TCI_SRCCODEPAGE,&ci); CHECKF(!ok, "SRCCODEPAGE %lu succeeded", badcp[i]); CHECK(ci_untouched(&ci)); }
  /* font signature: must match one table entry exactly */
  DWORD two[2]={3,0}; memset(&ci,0xAA,sizeof ci); EXPECT_EQ(U(TranslateCharsetInfo)(two,&ci,TCI_SRCFONTSIG),FALSE); CHECK(ci_untouched(&ci));
  DWORD hi[2]={1,1}; EXPECT_EQ(U(TranslateCharsetInfo)(hi,&ci,TCI_SRCFONTSIG),FALSE);
  DWORD zero[2]={0,0}; EXPECT_EQ(U(TranslateCharsetInfo)(zero,&ci,TCI_SRCFONTSIG),FALSE);
  /* a real face's signature has many bits: no match */
  HDC dc=U(GetDC)(NULL); WCHAR got[LF_FACESIZE]; sel(dc,L"Arial",DEFAULT_CHARSET,got);
  if(!lstrcmpiW(got,L"Arial")){ FONTSIGNATURE fs; U(GetTextCharsetInfo)(dc,&fs,0); DWORD s[2]={fs.fsCsb[0],0}; EXPECT_EQ(U(TranslateCharsetInfo)(s,&ci,TCI_SRCFONTSIG),FALSE); }
  unsel(dc); U(ReleaseDC)(NULL,dc);
  /* bad flags: FALSE, ERROR_INVALID_PARAMETER (decompiled, observed) */
  SetLastError(0); EXPECT_EQ(tci(ANSI_CHARSET,0x10,&ci),FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER); CHECK(ci_untouched(&ci));
  SetLastError(0); EXPECT_EQ(tci(ANSI_CHARSET,0,&ci),FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  /* NULL output */
  EXPECT_EQ(U(TranslateCharsetInfo)((DWORD*)(ULONG_PTR)ANSI_CHARSET,NULL,TCI_SRCCHARSET),FALSE);
  /* locale that does not exist */
  EXPECT_EQ(tci(0xFFFF,TCI_SRCLOCALE,&ci),FALSE);
}
