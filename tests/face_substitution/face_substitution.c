#include "t.h"
/* face_substitution: invariants of CreateFont(Indirect)(A/W/ExW), GetObject/GetTextFace/GetFontData/GetTextMetrics that hold on ANY
 * Windows (no font-specific numbers; those come from the recorded goldens, probes/face_substitution).
 * Every hard CHECK cites the Win10 22H2 code it was verified against (corpus API, build 19041.2965):
 *   gdi32full!CreateFontIndirectW @0x6140, CreateFontIndirectA @0x1f00, CreateFontIndirectExW @0x61e0, CreateFontW @0x5490,
 *   gdi32full!GetObjectW @0x19f60, GetObjectA @0xbfb0, win32kfull!NtGdiExtGetObjectW @0x99e80, GreExtGetObjectW @0x99f88, hfontCreate @0x8fee0 (stores the ELFDV before lNormAngle),
 *   gdi32full!GetObjectType @0xa0000-type branch (font -> 6),
 *   gdi32full!GetTextFaceW @0x66e0, GetTextFaceA @0x51da0, win32kfull!NtGdiGetTextFaceW @0x5ae40, GreGetTextFaceW @0x5af50,
 *   gdi32full!GetFontDataImpl @0x20df0, win32kfull!NtGdiGetFontUnicodeRanges @0x150d30,
 *   gdi32full!EnumFontFamiliesExW @0x3b00 -> EnumFontsInternalW @0x3b58, win32kfull!GreSelectFontInternal @0x15ea20,
 *   gdi32full!GetTextMetricsAImpl @0x4ce0, and the TEXTMETRIC / extent / ABC rules in docs/research/02 sections 6.2 and 6.5
 *   (vIFIMetricsToTextMetricWStrict @0x5d88c, RFONTOBJ::bTextExtent @0x25044, GreGetCharABCWidthsW @0xf1738,
 *   RFONTOBJ::lOverhang @0x5e8e8). Font-dependent observations are t_note only. */

static HDC screen_dc(void){ return U(GetDC)(NULL); }
static void release_dc(HDC dc){ U(ReleaseDC)(NULL,dc); }
static LOGFONTW mk(const WCHAR *face, LONG h, BYTE cs){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=FW_NORMAL; lf.lfCharSet=cs; if(face) lstrcpynW(lf.lfFaceName,face,LF_FACESIZE); return lf; }
static const WCHAR *FACES[]={L"Segoe UI",L"MS Shell Dlg",L"MS Shell Dlg 2",L"Tahoma",L"Arial",L"Courier New",L"Courier",L"MS Sans Serif",
  L"Microsoft Sans Serif",L"System",L"Fixedsys",L"Terminal",L"",L"NoSuchFace_xyz",L"@SimSun",L"helv",L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcde"};
#define NF ((int)(sizeof FACES/sizeof*FACES))

/* ---------------------------------------------------------------- CreateFont* and GetObject */
TEST(createfont_null_logfont_returns_null){
  /* CreateFontIndirectW/A: `if (lplf == NULL) return NULL` before anything else */
  CHECK(U(CreateFontIndirectW)(NULL)==NULL);
  CHECK(U(CreateFontIndirectA)(NULL)==NULL);
}

TEST(getobjectw_returns_logfont_unchanged){
  /* CreateFontIndirectW copies the LOGFONT (32 WCHAR face included) unchanged; GreExtGetObjectW memcpy's the stored LOGFONT */
  static const BYTE CS[]={DEFAULT_CHARSET,ANSI_CHARSET,SYMBOL_CHARSET,SHIFTJIS_CHARSET,RUSSIAN_CHARSET,OEM_CHARSET,3,254};
  static const LONG H[]={-11,-12,0,13,-1,1000,-1000};
  for(int i=0;i<NF;i++) for(int c=0;c<(int)sizeof CS;c++) for(int h=0;h<(int)(sizeof H/sizeof*H);h++){
    LOGFONTW in=mk(FACES[i],H[h],CS[c]); in.lfPitchAndFamily=(BYTE)(c*0x11); in.lfOutPrecision=(BYTE)(h%11); in.lfQuality=(BYTE)(c%7); in.lfClipPrecision=(BYTE)(h*16);
    in.lfEscapement=h*450; in.lfOrientation=c*900; in.lfWeight=h*100; in.lfItalic=(BYTE)(c&1);
    HFONT f=U(CreateFontIndirectW)(&in); CHECKF(f!=NULL,"face %d cs %d h %ld",i,CS[c],H[h]); if(!f) continue;
    LOGFONTW out; memset(&out,0xcc,sizeof out); int n=U(GetObjectW)(f,sizeof out,&out);
    CHECKF(n==(int)sizeof(LOGFONTW),"GetObjectW returned %d",n);
    CHECKF(!memcmp(&in,&out,sizeof in),"read-back differs: face %d cs %d h %ld",i,CS[c],H[h]);
    CHECK(U(GetObjectType)(f)==OBJ_FONT);
    U(DeleteObject)(f); }
}

TEST(getobject_font_size_protocol){
  LOGFONTW in=mk(L"Arial",-12,DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&in); REQUIRE(f);
  /* GetObjectW: font handle and pv == NULL -> 0x5c (sizeof LOGFONTW) in user mode */
  EXPECT_EQ(U(GetObjectW)(f,0,NULL),sizeof(LOGFONTW));
  EXPECT_EQ(U(GetObjectW)(f,1000,NULL),sizeof(LOGFONTW));
  /* GreExtGetObjectW copies min(c, stored size); NtGdiExtGetObjectW copies that many bytes back: c=10 -> 10, rest untouched */
  BYTE b[256]; memset(b,0xcc,sizeof b); int n=U(GetObjectW)(f,10,b); EXPECT_EQ(n,10);
  CHECK(!memcmp(b,&in,10)); int untouched=1; for(int i=10;i<(int)sizeof b;i++) if(b[i]!=0xcc) untouched=0; CHECK(untouched);
  /* GetObjectA: pv == NULL -> 0x3c; c == 0x3c or c == 0x5c -> LOGFONTA, returns 0x3c; other c (< 0x104, != 0xbc) -> c */
  EXPECT_EQ(U(GetObjectA)(f,0,NULL),sizeof(LOGFONTA));
  LOGFONTA a; memset(&a,0,sizeof a); EXPECT_EQ(U(GetObjectA)(f,sizeof a,&a),sizeof(LOGFONTA));
  CHECK(a.lfHeight==in.lfHeight && a.lfWeight==in.lfWeight && a.lfCharSet==in.lfCharSet); EXPECT_STR(a.lfFaceName,"Arial");
  BYTE big[sizeof(LOGFONTW)+8]; memset(big,0xcc,sizeof big); EXPECT_EQ(U(GetObjectA)(f,sizeof(LOGFONTW),big),sizeof(LOGFONTA));
  memset(b,0xcc,sizeof b); EXPECT_EQ(U(GetObjectA)(f,10,b),10);
  U(DeleteObject)(f);
}

TEST(createfontw_builds_logfont){
  /* CreateFontW stores each DWORD argument into its LOGFONT BYTE field (truncation) and copies the face up to its NUL */
  HFONT f=U(CreateFontW)(-12,3,450,900,700,0x1ff,0x102,0x103,0x1cc,0x107,0x1f2,0x105,0x122,L"Tahoma"); REQUIRE(f);
  LOGFONTW o; memset(&o,0,sizeof o); EXPECT_EQ(U(GetObjectW)(f,sizeof o,&o),sizeof o);
  CHECK(o.lfHeight==-12 && o.lfWidth==3 && o.lfEscapement==450 && o.lfOrientation==900 && o.lfWeight==700);
  EXPECT_EQ(o.lfItalic,0xff); EXPECT_EQ(o.lfUnderline,0x02); EXPECT_EQ(o.lfStrikeOut,0x03); EXPECT_EQ(o.lfCharSet,0xcc);
  EXPECT_EQ(o.lfOutPrecision,0x07); EXPECT_EQ(o.lfClipPrecision,0xf2); EXPECT_EQ(o.lfQuality,0x05); EXPECT_EQ(o.lfPitchAndFamily,0x22);
  EXPECT_WSTR(o.lfFaceName,L"Tahoma"); U(DeleteObject)(f);
  /* NULL face -> lfFaceName[0] = 0 */
  f=U(CreateFontW)(-12,0,0,0,0,0,0,0,DEFAULT_CHARSET,0,0,0,0,NULL); REQUIRE(f);
  memset(&o,0xcc,sizeof o); EXPECT_EQ(U(GetObjectW)(f,sizeof o,&o),sizeof o); EXPECT_EQ(o.lfFaceName[0],0); U(DeleteObject)(f);
  /* a 40-char face: at most 32 WCHAR are copied; the first 31 survive in the read-back */
  const WCHAR *lng=L"Segoe UIABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  f=U(CreateFontW)(-12,0,0,0,0,0,0,0,DEFAULT_CHARSET,0,0,0,0,lng); REQUIRE(f);
  memset(&o,0,sizeof o); EXPECT_EQ(U(GetObjectW)(f,sizeof o,&o),sizeof o); CHECK(!memcmp(o.lfFaceName,lng,31*sizeof(WCHAR)));
  t_note("CreateFontW 40-char face: read-back WCHAR[31]=0x%x",o.lfFaceName[31]);
  U(DeleteObject)(f);
}

TEST(createfontindirectexw_design_axes_limit){
  /* CreateFontIndirectExW: dvNumAxes >= 17 -> NULL (checked before NtGdiHfontCreate) */
  ENUMLOGFONTEXDVW e; memset(&e,0,sizeof e); e.elfEnumLogfontEx.elfLogFont=mk(L"Arial",-12,DEFAULT_CHARSET); e.elfDesignVector.dvReserved=STAMP_DESIGNVECTOR;
  e.elfDesignVector.dvNumAxes=17; CHECK(U(CreateFontIndirectExW)(&e)==NULL);
  e.elfDesignVector.dvNumAxes=0x80000000u; CHECK(U(CreateFontIndirectExW)(&e)==NULL);
  e.elfDesignVector.dvNumAxes=0; HFONT f=U(CreateFontIndirectExW)(&e); CHECK(f!=NULL);
  if(f){ LOGFONTW o; memset(&o,0xcc,sizeof o); EXPECT_EQ(U(GetObjectW)(f,sizeof o,&o),sizeof o); CHECK(!memcmp(&o,&e.elfEnumLogfontEx.elfLogFont,sizeof o)); U(DeleteObject)(f); }
}

TEST(createfontindirecta_matches_w){
  /* CreateFontIndirectA converts and calls the same ExW path: the W read-back of an A font equals the converted LOGFONT */
  static const char *AF[]={"Segoe UI","MS Shell Dlg","Courier","","NoSuchFace_xyz","ABCDEFGHIJKLMNOPQRSTUVWXYZabcde"};
  for(int i=0;i<6;i++){ LOGFONTA a; memset(&a,0,sizeof a); a.lfHeight=-11; a.lfWeight=400; a.lfCharSet=DEFAULT_CHARSET; a.lfPitchAndFamily=0x22; lstrcpynA(a.lfFaceName,AF[i],LF_FACESIZE);
    HFONT f=U(CreateFontIndirectA)(&a); CHECK(f!=NULL); if(!f) continue;
    LOGFONTW w; memset(&w,0,sizeof w); EXPECT_EQ(U(GetObjectW)(f,sizeof w,&w),sizeof w);
    CHECK(!memcmp(&w,&a,offsetof(LOGFONTW,lfFaceName)));
    WCHAR want[LF_FACESIZE]; MultiByteToWideChar(CP_ACP,0,AF[i],-1,want,LF_FACESIZE); EXPECT_WSTR(w.lfFaceName,want);
    LOGFONTA b; memset(&b,0,sizeof b); EXPECT_EQ(U(GetObjectA)(f,sizeof b,&b),sizeof b); CHECK(!memcmp(&a,&b,offsetof(LOGFONTA,lfFaceName))); EXPECT_STR(b.lfFaceName,a.lfFaceName);
    U(DeleteObject)(f); }
}

/* ---------------------------------------------------------------- SelectObject / GetTextFace */
TEST(selectobject_font_roundtrip){
  HDC dc=screen_dc(); REQUIRE(dc);
  LOGFONTW lf=mk(L"Tahoma",-11,DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&lf); REQUIRE(f);
  /* GreSelectFontInternal returns the previously selected LFONT handle */
  HFONT prev=(HFONT)U(SelectObject)(dc,f); CHECK(prev!=NULL);
  CHECK((HFONT)U(GetCurrentObject)(dc,OBJ_FONT)==f);
  CHECK((HFONT)U(SelectObject)(dc,f)==f);            /* same font again: returns it */
  CHECK((HFONT)U(SelectObject)(dc,prev)==f);
  CHECK((HFONT)U(GetCurrentObject)(dc,OBJ_FONT)==prev);
  U(DeleteObject)(f); release_dc(dc);
}

TEST(gettextface_buffer_protocol){
  HDC dc=screen_dc(); REQUIRE(dc);
  for(int i=0;i<NF;i++){ LOGFONTW lf=mk(FACES[i],-11,DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&lf); if(!f) continue; HFONT o=(HFONT)U(SelectObject)(dc,f);
    /* NULL buffer: count including the NUL */
    int n=U(GetTextFaceW)(dc,0,NULL); CHECKF(n>=2,"face %d: n=%d",i,n);
    WCHAR full[LF_FACESIZE*2]; for(int j=0;j<LF_FACESIZE*2;j++) full[j]=L'#';
    EXPECT_EQ(U(GetTextFaceW)(dc,LF_FACESIZE*2,full),n); EXPECT_EQ(lstrlenW(full),n-1);
    /* truncation: returns c, copies c-1 chars and a NUL (GreGetTextFaceW) */
    if(n>3){ WCHAR t[8]; for(int j=0;j<8;j++) t[j]=L'#'; EXPECT_EQ(U(GetTextFaceW)(dc,3,t),3); CHECK(t[0]==full[0] && t[1]==full[1] && t[2]==0 && t[3]==L'#'); }
    { WCHAR t[4]={L'#',L'#',L'#',L'#'}; EXPECT_EQ(U(GetTextFaceW)(dc,1,t),1); CHECK(t[0]==0 && t[1]==L'#'); }
    /* buffer with c < 1 -> 0, ERROR_INVALID_PARAMETER (user-mode check) */
    { WCHAR t[4]; SET_ERR(0); EXPECT_EQ(U(GetTextFaceW)(dc,0,t),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
      SET_ERR(0); EXPECT_EQ(U(GetTextFaceW)(dc,-5,t),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
      char a[4]; SET_ERR(0); EXPECT_EQ(U(GetTextFaceA)(dc,0,a),0); EXPECT_ERR(ERROR_INVALID_PARAMETER); }
    /* A/W parity for ASCII names: A returns the length without the NUL (WideCharToMultiByte count - 1) */
    int ascii=1; for(int j=0;full[j];j++) if(full[j]>=0x80) ascii=0;
    if(ascii){ char a[LF_FACESIZE*2]; int na=U(GetTextFaceA)(dc,sizeof a,a); EXPECT_EQ(na,n-1); char w2a[LF_FACESIZE*2]; WideCharToMultiByte(CP_ACP,0,full,-1,w2a,sizeof w2a,NULL,NULL); EXPECT_STR(a,w2a);
      EXPECT_EQ(U(GetTextFaceA)(dc,0,NULL),n); }
    U(SelectObject)(dc,o); U(DeleteObject)(f); }
  release_dc(dc);
}

/* ---------------------------------------------------------------- GetFontData / GetFontUnicodeRanges */
TEST(getfontdata_buffer_protocol){
  HDC dc=screen_dc(); REQUIRE(dc); int tested=0;
  for(int i=0;i<NF;i++){ LOGFONTW lf=mk(FACES[i],-12,DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&lf); if(!f) continue; HFONT o=(HFONT)U(SelectObject)(dc,f);
    DWORD sz=U(GetFontData)(dc,0,0,NULL,0);
    if(sz!=GDI_ERROR && sz>12){ tested++;
      /* GetFontDataImpl: buffer NULL with cb != 0 -> treated as a size query */
      EXPECT_EQ(U(GetFontData)(dc,0,0,NULL,100),sz);
      /* cb == 0xffffffff -> GDI_ERROR before any read */
      /* (the buffer is big enough for the whole font so that an implementation which copies anyway fails the check instead of smashing the stack) */
      BYTE *room=(BYTE*)malloc((size_t)sz+65536); if(room){ EXPECT_EQ(U(GetFontData)(dc,0,0,room,0xffffffff),GDI_ERROR); free(room); }
      BYTE tiny[16];
      /* offset + cb overflow -> GDI_ERROR */
      EXPECT_EQ(U(GetFontData)(dc,0,0xfffffff0u,tiny,0x20),GDI_ERROR);
      BYTE *b=(BYTE*)malloc(sz); if(b){ EXPECT_EQ(U(GetFontData)(dc,0,0,b,sz),sz);
        BYTE h[12]; EXPECT_EQ(U(GetFontData)(dc,0,0,h,12),12); CHECK(!memcmp(h,b,12)); free(b); }
      TEXTMETRICW tm; if(U(GetTextMetricsW)(dc,&tm)) t_note("face %d: GetFontData ok, tmPitchAndFamily 0x%x",i,tm.tmPitchAndFamily); }
    else t_note("face %d: GetFontData(0) = 0x%lx (not an sfnt font)",i,sz);
    U(SelectObject)(dc,o); U(DeleteObject)(f); }
  CHECKF(tested>0,"no face gave sfnt data"); release_dc(dc);
}

TEST(getfontunicoderanges_protocol){
  HDC dc=screen_dc(); REQUIRE(dc);
  LOGFONTW lf=mk(L"Arial",-12,DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&lf); REQUIRE(f); HFONT o=(HFONT)U(SelectObject)(dc,f);
  DWORD n=U(GetFontUnicodeRanges)(dc,NULL);
  if(n){ GLYPHSET *g=(GLYPHSET*)calloc(1,n+64); REQUIRE(g);
    /* NtGdiGetFontUnicodeRanges: same size for the query and the fill */
    EXPECT_EQ(U(GetFontUnicodeRanges)(dc,g),n);
    EXPECT_EQ(g->cbThis,n);
    CHECK(offsetof(GLYPHSET,ranges)+(size_t)g->cRanges*sizeof(WCRANGE)<=n);
    DWORD sum=0; for(DWORD i=0;i<g->cRanges;i++) sum+=g->ranges[i].cGlyphs;
    if(sum!=g->cGlyphsSupported) t_note("sum of cGlyphs %lu != cGlyphsSupported %lu",sum,g->cGlyphsSupported);
    free(g); }
  else t_note("GetFontUnicodeRanges(Arial) = 0");
  U(SelectObject)(dc,o); U(DeleteObject)(f); release_dc(dc);
}

/* ---------------------------------------------------------------- TEXTMETRIC / extents / widths */
TEST(textmetric_extent_width_invariants){
  HDC dc=screen_dc(); REQUIRE(dc);
  static const LONG H[]={-8,-9,-10,-11,-12,-13,-16,0,13,16,20};
  static const WCHAR S[]=L"The quick brown fox jumps";
  for(int i=0;i<NF;i++) for(int h=0;h<(int)(sizeof H/sizeof*H);h++){
    LOGFONTW lf=mk(FACES[i],H[h],DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&lf); if(!f) continue; HFONT o=(HFONT)U(SelectObject)(dc,f);
    TEXTMETRICW tm; TEXTMETRICA ta;
    if(U(GetTextMetricsW)(dc,&tm)){
      /* tmDescent = tmHeight - tmAscent (vIFIMetricsToTextMetricWStrict) */
      CHECKF(tm.tmHeight==tm.tmAscent+tm.tmDescent,"face %d h %ld: %ld != %ld+%ld",i,H[h],tm.tmHeight,tm.tmAscent,tm.tmDescent);
      /* GetTextMetricsA = the W metrics converted (GetTextMetricsAImpl) */
      if(U(GetTextMetricsA)(dc,&ta)){
        CHECK(ta.tmHeight==tm.tmHeight && ta.tmAscent==tm.tmAscent && ta.tmDescent==tm.tmDescent && ta.tmInternalLeading==tm.tmInternalLeading &&
              ta.tmExternalLeading==tm.tmExternalLeading && ta.tmAveCharWidth==tm.tmAveCharWidth && ta.tmMaxCharWidth==tm.tmMaxCharWidth &&
              ta.tmWeight==tm.tmWeight && ta.tmOverhang==tm.tmOverhang && ta.tmPitchAndFamily==tm.tmPitchAndFamily && ta.tmCharSet==tm.tmCharSet && ta.tmItalic==tm.tmItalic); }
      else CHECK(0);
      /* extent: cy = tmHeight (bComputeTextExtent / RFONTOBJ::bTextExtent, MM_TEXT) */
      SIZE sz; if(U(GetTextExtentPoint32W)(dc,S,25,&sz)){ CHECKF(sz.cy==tm.tmHeight,"face %d h %ld: cy %ld tmHeight %ld",i,H[h],sz.cy,tm.tmHeight);
        /* cx = (sum of 28.4 advances + 8) >> 4; GetCharWidth32 = round(advance + overhang) per glyph.
           So when every advance is a whole pixel (GetCharWidthFloat integral) and there is no overhang, cx == sum of widths. */
        INT w[25]; FLOAT fw[25]; int okw=1, integral=1; long sum=0;
        for(int c=0;c<25;c++){ if(!U(GetCharWidth32W)(dc,S[c],S[c],&w[c]) || !U(GetCharWidthFloatW)(dc,S[c],S[c],&fw[c])){ okw=0; break; } sum+=w[c]; if(fw[c]!=(FLOAT)(LONG)fw[c]) integral=0; }
        if(okw && integral && tm.tmOverhang==0) CHECKF(sz.cx==sum,"face %d h %ld: cx %ld sum %ld",i,H[h],sz.cx,sum);
        else if(okw && sz.cx!=sum) t_note("face %d h %ld: extent %ld != sum of widths %ld (fractional advances or overhang)",i,H[h],sz.cx,sum); }
      if(tm.tmPitchAndFamily&TMPF_TRUETYPE){
        /* TrueType: tmOverhang = 0 (lOverhang is non-zero only for bitmap/stroke fonts) and A+B+C = round(advance) = GetCharWidth32 */
        CHECKF(tm.tmOverhang==0,"face %d: TT overhang %ld",i,tm.tmOverhang);
        ABC abc[58]; INT cw[58];
        if(U(GetCharABCWidthsW)(dc,'A','z',abc) && U(GetCharWidth32W)(dc,'A','z',cw)){
          int bad=0; for(int c=0;c<58;c++) if(abc[c].abcA+(int)abc[c].abcB+abc[c].abcC!=cw[c]) bad++; CHECKF(bad==0,"face %d h %ld: %d chars with A+B+C != width",i,H[h],bad); }
        else CHECKF(0,"GetCharABCWidthsW/GetCharWidth32W failed on a TrueType font"); }
    }
    U(SelectObject)(dc,o); U(DeleteObject)(f); }
  release_dc(dc);
}

/* ---------------------------------------------------------------- enumeration */
typedef struct { int calls, ret; } ecb;
static int CALLBACK enum_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp){ ecb *e=(ecb*)lp; e->calls++; return e->ret; }
TEST(enumfontfamiliesex_returns_last_callback_value){
  HDC dc=screen_dc(); REQUIRE(dc);
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfCharSet=DEFAULT_CHARSET;
  ecb all={0,7}; int r=U(EnumFontFamiliesExW)(dc,&lf,enum_cb,(LPARAM)&all,0);
  REQUIRE(all.calls>0);
  /* EnumFontsInternalW: the result is the last callback value; a 0 stops the walk */
  EXPECT_EQ(r,7);
  ecb stop={0,0}; r=U(EnumFontFamiliesExW)(dc,&lf,enum_cb,(LPARAM)&stop,0); EXPECT_EQ(r,0); EXPECT_EQ(stop.calls,1);
  /* a NULL LOGFONT enumerates as DEFAULT_CHARSET with no face */
  ecb nul={0,7}; r=U(EnumFontFamiliesExW)(dc,NULL,enum_cb,(LPARAM)&nul,0); EXPECT_EQ(r,7); EXPECT_EQ(nul.calls,all.calls);
  ecb none={0,7}; lstrcpyW(lf.lfFaceName,L"NoSuchFace_xyz_123"); r=U(EnumFontFamiliesExW)(dc,&lf,enum_cb,(LPARAM)&none,0);
  t_note("EnumFontFamiliesExW(no such face) -> %d, %d callbacks",r,none.calls);
  release_dc(dc);
}

/* ---------------------------------------------------------------- observations only */
TEST(glyph_indices_observations){
  HDC dc=screen_dc(); REQUIRE(dc);
  LOGFONTW lf=mk(L"Tahoma",-12,DEFAULT_CHARSET); HFONT f=U(CreateFontIndirectW)(&lf); REQUIRE(f); HFONT o=(HFONT)U(SelectObject)(dc,f);
  WORD a[4]={0}, b[4]={0}; DWORD r1=U(GetGlyphIndicesW)(dc,L"A\x4E2D\xFFFF",3,a,GGI_MARK_NONEXISTING_GLYPHS), r2=U(GetGlyphIndicesW)(dc,L"A\x4E2D\xFFFF",3,b,0);
  CHECK(r1!=GDI_ERROR); CHECK(r2!=GDI_ERROR);
  t_note("Tahoma GetGlyphIndicesW mark: ret %lu %04x %04x %04x; plain: ret %lu %04x %04x %04x",r1,a[0],a[1],a[2],r2,b[0],b[1],b[2]);
  U(SelectObject)(dc,o); U(DeleteObject)(f); release_dc(dc);
}
