#include "oa_common.h"
/* GetCharABCWidthsA/W/I, GetCharABCWidthsFloatA/W/I, GetCharWidthA/32A/FloatA (+W siblings).
 * Expectations and sources: EVIDENCE.md in this directory. */

static const WCHAR *k_tt[] = { L"Arial", L"Times New Roman" };

static int fabs_i(float f, int i){ float d = f - (float)i; return (d < 0 ? -d : d) <= 1.0f; }
static float fdiff(float a, float b){ float d = a - b; return d < 0 ? -d : d; }

TEST(abc_a_vs_w_vs_width){
  int i;
  for(i = 0; i < 2; i++){
    oa_font d; UINT ch; ABC aa[26], aw[26], one; INT wd[26]; BOOL ra, rw;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    memset(aa, 0xcc, sizeof aa); memset(aw, 0xdd, sizeof aw);
    ra = U(GetCharABCWidthsA)(d.dc, 'A', 'Z', aa);
    rw = U(GetCharABCWidthsW)(d.dc, 'A', 'Z', aw);
    CHECK(ra); CHECK(rw);
    CHECK(U(GetCharWidth32W)(d.dc, 'A', 'Z', wd));
    CHECK(!memcmp(aa, aw, sizeof aa));
    for(ch = 'A'; ch <= 'Z'; ch++){
      int k = ch - 'A';
      CHECKF(aw[k].abcA + (int)aw[k].abcB + aw[k].abcC == wd[k], "%ls '%c' A+B+C = %d+%u+%d vs GetCharWidth32 %d", k_tt[i], ch, aw[k].abcA, aw[k].abcB, aw[k].abcC, wd[k]);
      CHECKF(aw[k].abcB > 0, "%ls '%c' abcB %u", k_tt[i], ch, aw[k].abcB);
      /* single-character calls agree with the range call */
      CHECK(U(GetCharABCWidthsW)(d.dc, ch, ch, &one));
      CHECKF(!memcmp(&one, &aw[k], sizeof one), "%ls '%c' single vs range ABC differ", k_tt[i], ch);
    }
    t_note("%ls 'A' ABC %d %u %d, 'W' ABC %d %u %d", k_tt[i], aw[0].abcA, aw[0].abcB, aw[0].abcC, aw['W'-'A'].abcA, aw['W'-'A'].abcB, aw['W'-'A'].abcC);
    /* GetCharWidth32W/A and GetCharWidthW/A all produce the same advances for a TrueType face (overhang is 0) */
    {
      INT w32a[26], wa[26], ww[26]; CHECK(d.tm.tmOverhang == 0);
      CHECK(U(GetCharWidth32A)(d.dc, 'A', 'Z', w32a)); CHECK(U(GetCharWidthA)(d.dc, 'A', 'Z', wa)); CHECK(U(GetCharWidthW)(d.dc, 'A', 'Z', ww));
      CHECK(!memcmp(w32a, wd, sizeof wd)); CHECKF(!memcmp(wa, wd, sizeof wd), "%ls GetCharWidthA != GetCharWidth32W", k_tt[i]); CHECK(!memcmp(ww, wd, sizeof wd));
    }
    /* high ANSI range (ACP 1252): GetCharABCWidthsA(0x80..) matches W for the mapped character */
    if(GetACP() == 1252){
      static const UCHAR hi[] = { 0x80, 0x85, 0x91, 0x93, 0x99, 0x9F, 0xA9, 0xE9 };
      unsigned j;
      for(j = 0; j < sizeof hi; j++){
        char c = (char)hi[j]; WCHAR wc; ABC x, y; INT xa, yw;
        MultiByteToWideChar(1252, 0, &c, 1, &wc, 1);
        CHECK(U(GetCharABCWidthsA)(d.dc, hi[j], hi[j], &x));
        CHECK(U(GetCharABCWidthsW)(d.dc, wc, wc, &y));
        CHECKF(!memcmp(&x, &y, sizeof x), "%ls ANSI 0x%02x vs U+%04x ABC differ", k_tt[i], hi[j], wc);
        CHECK(U(GetCharWidth32A)(d.dc, hi[j], hi[j], &xa)); CHECK(U(GetCharWidth32W)(d.dc, wc, wc, &yw));
        CHECKF(xa == yw, "%ls ANSI 0x%02x width %d vs U+%04x %d", k_tt[i], hi[j], xa, wc, yw);
      }
    }
    oa_close(&d);
  }
}

TEST(abc_index_variant){
  int i;
  for(i = 0; i < 2; i++){
    oa_font d; WCHAR s[26]; WORD gi[26]; ABC ai[26], aw[26]; int k; INT wi[26], ww[26];
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    for(k = 0; k < 26; k++) s[k] = 'A' + k;
    CHECK(U(GetGlyphIndicesW)(d.dc, s, 26, gi, GGI_MARK_NONEXISTING_GLYPHS) == 26);
    CHECK(U(GetCharABCWidthsW)(d.dc, 'A', 'Z', aw));
    memset(ai, 0xcc, sizeof ai);
    CHECK(U(GetCharABCWidthsI)(d.dc, 0, 26, gi, ai));
    CHECKF(!memcmp(ai, aw, sizeof ai), "%ls GetCharABCWidthsI(gi) != GetCharABCWidthsW", k_tt[i]);
    CHECK(U(GetCharWidthI)(d.dc, 0, 26, gi, wi)); CHECK(U(GetCharWidth32W)(d.dc, 'A', 'Z', ww));
    CHECK(!memcmp(wi, ww, sizeof wi));
    /* consecutive-index form (pgi NULL): glyphs gi0..gi0+n-1 */
    { ABC c3[3], t3[3]; WORD g3[3]; g3[0] = gi[0]; g3[1] = gi[0] + 1; g3[2] = gi[0] + 2;
      CHECK(U(GetCharABCWidthsI)(d.dc, gi[0], 3, NULL, c3)); CHECK(U(GetCharABCWidthsI)(d.dc, 0, 3, g3, t3));
      CHECK(!memcmp(c3, t3, sizeof c3)); }
    oa_close(&d);
  }
}

TEST(abc_float_variants){
  int i; REQUIRE(UN(GetCharABCWidthsFloatI));
  for(i = 0; i < 2; i++){
    oa_font d; ABCFLOAT fa[26], fw[26], fi[26]; ABC aw[26]; WORD gi[26]; WCHAR s[26]; int k; float maxdev = 0, maxexact = 0; FLOAT fwd[26], fwdA[26];
    BOOL r;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    for(k = 0; k < 26; k++) s[k] = 'A' + k;
    CHECK(U(GetGlyphIndicesW)(d.dc, s, 26, gi, 0) == 26);
    memset(fa, 0xcc, sizeof fa); memset(fw, 0xdd, sizeof fw); memset(fi, 0xee, sizeof fi);
    CHECK(U(GetCharABCWidthsFloatA)(d.dc, 'A', 'Z', fa));
    CHECK(U(GetCharABCWidthsFloatW)(d.dc, 'A', 'Z', fw));
    r = UN(GetCharABCWidthsFloatI)(d.dc, 0, 26, gi, fi);
    t_note("%ls GetCharABCWidthsFloatI returns %d", k_tt[i], r);
    CHECK(r);
    CHECK(U(GetCharABCWidthsW)(d.dc, 'A', 'Z', aw));
    CHECK(U(GetCharWidthFloatW)(d.dc, 'A', 'Z', fwd)); CHECK(U(GetCharWidthFloatA)(d.dc, 'A', 'Z', fwdA));
    for(k = 0; k < 26; k++){
      float d1, d2, d3, dw;
      CHECKF(!memcmp(&fa[k], &fw[k], sizeof fa[k]), "%ls '%c' FloatA != FloatW", k_tt[i], 'A' + k);
      if(r) CHECKF(!memcmp(&fi[k], &fw[k], sizeof fi[k]), "%ls '%c' FloatI != FloatW", k_tt[i], 'A' + k);
      CHECKF(fabs_i(fw[k].abcfA, aw[k].abcA) && fabs_i(fw[k].abcfB, (int)aw[k].abcB) && fabs_i(fw[k].abcfC, aw[k].abcC),
             "%ls '%c' float ABC %g %g %g vs int %d %u %d", k_tt[i], 'A' + k, fw[k].abcfA, fw[k].abcfB, fw[k].abcfC, aw[k].abcA, aw[k].abcB, aw[k].abcC);
      d1 = fdiff(fw[k].abcfA, (float)aw[k].abcA); d2 = fdiff(fw[k].abcfB, (float)aw[k].abcB); d3 = fdiff(fw[k].abcfC, (float)aw[k].abcC);
      if(d1 > maxdev) maxdev = d1;
      if(d2 > maxdev) maxdev = d2;
      if(d3 > maxdev) maxdev = d3;
      dw = fdiff(fw[k].abcfA + fw[k].abcfB + fw[k].abcfC, fwd[k] * 16.0f); if(dw > maxexact) maxexact = dw;
      CHECKF(fdiff(fwd[k], fwdA[k]) == 0, "%ls '%c' GetCharWidthFloatA %g != W %g", k_tt[i], 'A' + k, fwdA[k], fwd[k]);
      /* observed: GetCharWidthFloat returns the integer advance divided by 16 (not the width in logical units) */
      CHECKF(fwd[k] * 16.0f == (float)(aw[k].abcA + (int)aw[k].abcB + aw[k].abcC), "%ls '%c' float width %g x16 vs int advance %d", k_tt[i], 'A' + k, fwd[k], aw[k].abcA + (int)aw[k].abcB + aw[k].abcC);
      CHECKF(fdiff(fw[k].abcfA + fw[k].abcfB + fw[k].abcfC, fwd[k] * 16.0f) <= 0.01f, "%ls '%c' float A+B+C %g vs float width x16 %g", k_tt[i], 'A' + k, fw[k].abcfA + fw[k].abcfB + fw[k].abcfC, fwd[k] * 16.0f);
    }
    t_note("%ls float vs int ABC: max |diff| %g; float A+B+C vs 16*float width: max |diff| %g; 'A' float ABC %g %g %g width %g", k_tt[i], maxdev, maxexact, fw[0].abcfA, fw[0].abcfB, fw[0].abcfC, fwd[0]);
    oa_close(&d);
  }
}

TEST(width_proportional_vs_fixed){
  static const struct { const WCHAR *f; int h; int prop; } F[] = {
    { L"Arial", -20, 1 }, { L"Times New Roman", -20, 1 }, { L"Courier New", -20, 0 }, { L"Lucida Console", -20, 0 } };
  int i;
  for(i = 0; i < 4; i++){
    oa_font d; INT wi, wM, wA, wA2, wsp; FLOAT fi, fM;
    if(!oa_open(&d, F[i].f, F[i].h, 1)){ t_skip("%ls not available as TrueType", F[i].f); continue; }
    CHECK(U(GetCharWidth32W)(d.dc, 'i', 'i', &wi)); CHECK(U(GetCharWidth32W)(d.dc, 'M', 'M', &wM));
    CHECK(U(GetCharWidthA)(d.dc, 'i', 'i', &wA)); CHECK(U(GetCharWidthA)(d.dc, 'M', 'M', &wA2));
    CHECK(U(GetCharWidthFloatA)(d.dc, 'i', 'i', &fi)); CHECK(U(GetCharWidthFloatA)(d.dc, 'M', 'M', &fM));
    CHECK(U(GetCharWidthA)(d.dc, ' ', ' ', &wsp));
    EXPECT_EQ(wA, wi); EXPECT_EQ(wA2, wM);
    CHECKF(fi * 16.0f == (float)wi && fM * 16.0f == (float)wM, "%ls float widths %g %g (x16) vs %d %d", F[i].f, fi, fM, wi, wM);
    t_note("%ls: 'i' %d 'M' %d space %d (float %g %g)", F[i].f, wi, wM, wsp, fi, fM);
    if(F[i].prop){ CHECKF(wi < wM, "%ls 'i' %d !< 'M' %d", F[i].f, wi, wM); CHECK(fi < fM); CHECK(d.tm.tmPitchAndFamily & TMPF_FIXED_PITCH); }
    else { CHECKF(wi == wM, "%ls fixed face 'i' %d != 'M' %d", F[i].f, wi, wM); CHECKF(!(d.tm.tmPitchAndFamily & TMPF_FIXED_PITCH), "%ls fixed face has TMPF_FIXED_PITCH (variable pitch) set", F[i].f); EXPECT_EQ(wsp, wM); }
    oa_close(&d);
  }
}

/* Observed on Win10 22H2 (the docs say GetCharABCWidths works only with TrueType): raster faces succeed and report
 * A = 0, B = advance, C = 0. */
TEST(abc_bitmap_font){
  static const WCHAR *k_bmp[] = { L"Terminal", L"MS Sans Serif", L"Small Fonts" };
  int i, tried = 0;
  for(i = 0; i < 3; i++){
    oa_font d; ABC a[3], b[3]; ABCFLOAT f[3]; INT w[3], wa[3]; FLOAT fw[3]; WORD gi[3] = { 36, 37, 38 }; int k;
    if(!oa_open(&d, k_bmp[i], -13, 0)){ oa_close(&d); continue; }
    tried++;
    CHECK(U(GetCharABCWidthsW)(d.dc, 'A', 'C', a));
    CHECK(U(GetCharABCWidthsA)(d.dc, 'A', 'C', b));
    CHECK(U(GetCharABCWidthsFloatW)(d.dc, 'A', 'C', f));
    CHECK(U(GetCharWidth32W)(d.dc, 'A', 'C', w)); CHECK(U(GetCharWidthA)(d.dc, 'A', 'C', wa)); CHECK(U(GetCharWidthFloatA)(d.dc, 'A', 'C', fw));
    CHECK(!memcmp(a, b, sizeof a));
    CHECK(U(GetCharABCWidthsI)(d.dc, 0, 3, gi, b)); t_note("%ls GetCharABCWidthsI returns TRUE; glyph 36..38 abcB %u %u %u", k_bmp[i], b[0].abcB, b[1].abcB, b[2].abcB);
    CHECK(d.tm.tmOverhang == 0);
    for(k = 0; k < 3; k++){
      CHECKF(a[k].abcA == 0 && a[k].abcC == 0 && (INT)a[k].abcB == w[k], "%ls '%c' ABC %d %u %d vs width %d", k_bmp[i], 'A' + k, a[k].abcA, a[k].abcB, a[k].abcC, w[k]);
      CHECKF(f[k].abcfA == 0 && f[k].abcfC == 0 && f[k].abcfB == (float)w[k], "%ls '%c' float ABC %g %g %g vs %d", k_bmp[i], 'A' + k, f[k].abcfA, f[k].abcfB, f[k].abcfC, w[k]);
      CHECKF(wa[k] == w[k] && w[k] > 0, "%ls '%c' width A %d W %d", k_bmp[i], 'A' + k, wa[k], w[k]);
      CHECKF(fw[k] * 16.0f == (float)w[k], "%ls '%c' float width %g x16 vs %d", k_bmp[i], 'A' + k, fw[k], w[k]);
    }
    if(i == 0){   /* Terminal is a fixed-pitch raster face: every character advances alike, equal to tmAveCharWidth */
      INT wi, wM; CHECK(U(GetCharWidthA)(d.dc, 'i', 'i', &wi)); CHECK(U(GetCharWidthA)(d.dc, 'M', 'M', &wM));
      EXPECT_EQ(wi, wM); EXPECT_EQ(wi, d.tm.tmAveCharWidth);
    }
    oa_close(&d);
  }
  if(!tried) t_skip("no raster face realised as non-TrueType");
}
