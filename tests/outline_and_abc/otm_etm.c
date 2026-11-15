#include "oa_common.h"
/* GetOutlineTextMetricsA/W and GetETM. Expectations and sources: EVIDENCE.md in this directory. */

static const WCHAR *k_tt[] = { L"Arial", L"Times New Roman" };

static DWORD ttfont_unitsPerEm(HDC dc){
  BYTE b[2]; DWORD n = U(GetFontData)(dc, 0x64616568 /* 'head' read as LE dword of "head" */, 18, b, 2);
  if(n != 2) return 0;
  return (DWORD)b[0] << 8 | b[1];
}

static int ansi_eq_wide(const char *a, const WCHAR *w){
  WCHAR t[256]; if(!MultiByteToWideChar(CP_ACP, 0, a, -1, t, 256)) return 0;
  return !lstrcmpW(t, w);
}

#define SAME(f) CHECKF(ta->f == tw->f, "TEXTMETRIC field " #f ": A %ld W %ld", (long)ta->f, (long)tw->f)

static void otm_face(const WCHAR *face){
  oa_font d; DWORD sa, sw; OUTLINETEXTMETRICA *a = NULL; OUTLINETEXTMETRICW *w = NULL;
  if(!oa_open(&d, face, -20, 1)){ t_skip("%ls not available as TrueType", face); return; }
  sa = U(GetOutlineTextMetricsA)(d.dc, 0, NULL);
  sw = U(GetOutlineTextMetricsW)(d.dc, 0, NULL);
  CHECKF(sa >= sizeof(OUTLINETEXTMETRICA), "A size %lu", sa);
  CHECKF(sw >= sizeof(OUTLINETEXTMETRICW), "W size %lu", sw);
  t_note("%ls: size query A=%lu W=%lu (sizeof A=%u W=%u)", face, sa, sw, (unsigned)sizeof *a, (unsigned)sizeof *w);
  a = (OUTLINETEXTMETRICA *)calloc(1, sa + 16); w = (OUTLINETEXTMETRICW *)calloc(1, sw + 16);
  if(!a || !w){ t_skip("out of memory"); free(a); free(w); oa_close(&d); return; }
  { DWORD ra = U(GetOutlineTextMetricsA)(d.dc, sa, a), rw = U(GetOutlineTextMetricsW)(d.dc, sw, w);
    CHECK(ra != 0); CHECK(rw != 0);      /* documented: nonzero on success */
    t_note("%ls: fill returns A=%lu W=%lu (sizes %lu/%lu)", face, ra, rw, sa, sw); }
  {
    TEXTMETRICA *ta = &a->otmTextMetrics; TEXTMETRICW tmw = w->otmTextMetrics; TEXTMETRICW *tw = &tmw;
    SAME(tmHeight); SAME(tmAscent); SAME(tmDescent); SAME(tmInternalLeading); SAME(tmExternalLeading);
    SAME(tmAveCharWidth); SAME(tmMaxCharWidth); SAME(tmWeight); SAME(tmOverhang);
    SAME(tmDigitizedAspectX); SAME(tmDigitizedAspectY); SAME(tmItalic); SAME(tmUnderlined); SAME(tmStruckOut);
    SAME(tmPitchAndFamily); SAME(tmCharSet);
    EXPECT_EQ(ta->tmHeight, d.tm.tmHeight);
    EXPECT_EQ(ta->tmAscent, d.tm.tmAscent);
    EXPECT_EQ(ta->tmDescent, d.tm.tmDescent);
    EXPECT_EQ(ta->tmHeight, ta->tmAscent + ta->tmDescent);
    EXPECT_EQ(tw->tmHeight, d.tm.tmHeight);
    EXPECT_EQ(tw->tmAveCharWidth, d.tm.tmAveCharWidth);
    CHECKF(ta->tmPitchAndFamily & TMPF_TRUETYPE, "otm pitchandfamily 0x%x lacks TMPF_TRUETYPE", ta->tmPitchAndFamily);
  }
  t_note("%ls: otmSize A=%u W=%u", face, a->otmSize, w->otmSize);
  /* design-unit and style data are identical between A and W */
  EXPECT_EQ(a->otmEMSquare, w->otmEMSquare);
  EXPECT_EQ(a->otmAscent, w->otmAscent);
  EXPECT_EQ(a->otmDescent, w->otmDescent);
  EXPECT_EQ(a->otmLineGap, w->otmLineGap);
  EXPECT_EQ(a->otmsCapEmHeight, w->otmsCapEmHeight);
  EXPECT_EQ(a->otmsXHeight, w->otmsXHeight);
  EXPECT_EQ(a->otmfsSelection, w->otmfsSelection);
  EXPECT_EQ(a->otmfsType, w->otmfsType);
  EXPECT_EQ(a->otmsCharSlopeRise, w->otmsCharSlopeRise);
  EXPECT_EQ(a->otmsCharSlopeRun, w->otmsCharSlopeRun);
  EXPECT_EQ(a->otmItalicAngle, w->otmItalicAngle);
  EXPECT_EQ(a->otmEMSquare, ttfont_unitsPerEm(d.dc));       /* head.unitsPerEm via GetFontData */
  CHECK(a->otmEMSquare >= 16);
  CHECKF(a->otmAscent > 0 && a->otmDescent < 0, "otmAscent %d otmDescent %d", a->otmAscent, a->otmDescent);
  EXPECT_EQ(memcmp(&a->otmPanoseNumber, &w->otmPanoseNumber, sizeof a->otmPanoseNumber), 0);
  EXPECT_EQ(memcmp(&a->otmrcFontBox, &w->otmrcFontBox, sizeof a->otmrcFontBox), 0);
  t_note("%ls: EM %u asc %d desc %d gap %u cap %u xh %u box %ld,%ld,%ld,%ld", face, a->otmEMSquare, a->otmAscent, a->otmDescent,
         a->otmLineGap, a->otmsCapEmHeight, a->otmsXHeight, a->otmrcFontBox.left, a->otmrcFontBox.top, a->otmrcFontBox.right, a->otmrcFontBox.bottom);
  /* string offsets: byte offsets from the start of the structure, pointing at the same text in A and W */
  {
    const char *fa[4]; const WCHAR *fw[4]; const char *an[4] = { "Family", "Face", "Style", "FullName" };
    DWORD oa[4] = { (DWORD)(ULONG_PTR)a->otmpFamilyName, (DWORD)(ULONG_PTR)a->otmpFaceName, (DWORD)(ULONG_PTR)a->otmpStyleName, (DWORD)(ULONG_PTR)a->otmpFullName };
    DWORD ow[4] = { (DWORD)(ULONG_PTR)w->otmpFamilyName, (DWORD)(ULONG_PTR)w->otmpFaceName, (DWORD)(ULONG_PTR)w->otmpStyleName, (DWORD)(ULONG_PTR)w->otmpFullName };
    int i;
    for(i = 0; i < 4; i++){
      CHECKF(oa[i] >= sizeof *a && oa[i] < sa, "%s A offset %lu outside [%u,%lu)", an[i], oa[i], (unsigned)sizeof *a, sa);
      CHECKF(ow[i] >= sizeof *w && ow[i] < sw, "%s W offset %lu outside [%u,%lu)", an[i], ow[i], (unsigned)sizeof *w, sw);
      if(oa[i] < sa && ow[i] < sw){
        fa[i] = (const char *)a + oa[i]; fw[i] = (const WCHAR *)((const char *)w + ow[i]);
        CHECKF(ansi_eq_wide(fa[i], fw[i]), "%s: A text \"%s\" differs from W text", an[i], fa[i]);
      }
    }
    if(oa[1] < sa && ow[1] < sw){
      /* the face name text is the face name that was selected (GetTextFaceW) */
      CHECKF(!lstrcmpiW((const WCHAR *)((const char *)w + ow[1]), face), "otmpFaceName text is not %ls", face);
      t_note("%ls: family \"%s\" face \"%s\" style \"%s\" full \"%s\"", face, fa[0], fa[1], fa[2], fa[3]);
    }
    /* the strings are contained in the buffer and zero-terminated */
    {
      DWORD i2; int nulA = 0; for(i2 = sizeof *a; i2 < sa; i2++) if(((char *)a)[i2] == 0) nulA++;
      CHECK(nulA >= 4);
    }
  }
  free(a); free(w);
  oa_close(&d);
}

TEST(otm_arial_ab){ otm_face(k_tt[0]); }
TEST(otm_times_ab){ otm_face(k_tt[1]); }

TEST(otm_size_contract){
  oa_font d; DWORD sa, sw, r, cb; BYTE *buf; OUTLINETEXTMETRICW *bw; OUTLINETEXTMETRICA *ba;
  if(!oa_open(&d, L"Arial", -20, 1)){ t_skip("Arial not available as TrueType"); return; }
  sa = U(GetOutlineTextMetricsA)(d.dc, 0, NULL);
  sw = U(GetOutlineTextMetricsW)(d.dc, 0, NULL);
  CHECK(sa > 0 && sw > 0);
  /* size query is repeatable */
  EXPECT_EQ(U(GetOutlineTextMetricsA)(d.dc, 0, NULL), sa);
  EXPECT_EQ(U(GetOutlineTextMetricsW)(d.dc, 0, NULL), sw);
  buf = (BYTE *)calloc(1, sw + 64);
  if(!buf){ t_skip("oom"); oa_close(&d); return; }
  bw = (OUTLINETEXTMETRICW *)buf; ba = (OUTLINETEXTMETRICA *)buf;
  /* observed on Win10 22H2: cbData == 0 with a non-NULL buffer is a size query for W and returns 0 for A (asymmetric) */
  r = U(GetOutlineTextMetricsW)(d.dc, 0, bw); t_note("W cbData=0, non-NULL buffer returns %lu (size %lu)", r, sw); EXPECT_EQ(r, sw);
  r = U(GetOutlineTextMetricsA)(d.dc, 0, ba); t_note("A cbData=0, non-NULL buffer returns %lu (size %lu)", r, sa); EXPECT_EQ(r, 0);
  /* observed: a buffer smaller than the structure itself is filled partially and the call returns cbData */
  { static const DWORD part[] = { 1, 4, 100, 231 }; unsigned i;
    for(i = 0; i < sizeof part / sizeof *part; i++){
      memset(buf, 0xa5, sw + 64);
      r = U(GetOutlineTextMetricsW)(d.dc, part[i], bw);
      CHECKF(r == part[i], "W cbData=%lu returns %lu", part[i], r);
      CHECKF(buf[part[i]] == 0xa5 && buf[part[i] + 1] == 0xa5, "W cbData=%lu wrote past the buffer", part[i]);
      memset(buf, 0xa5, sa + 64);
      r = U(GetOutlineTextMetricsA)(d.dc, part[i], ba);
      CHECKF(r == part[i], "A cbData=%lu returns %lu", part[i], r);
      CHECKF(buf[part[i]] == 0xa5, "A cbData=%lu wrote past the buffer", part[i]);
    } }
  cb = sizeof(OUTLINETEXTMETRICW);
  CHECK(sw > cb);
  r = U(GetOutlineTextMetricsW)(d.dc, cb, bw); t_note("W cbData=sizeof(struct)=%lu returns %lu (size %lu)", cb, r, sw); EXPECT_EQ(r, cb);
  /* beyond the fixed part but short of the strings: fails */
  r = U(GetOutlineTextMetricsW)(d.dc, cb + 1, bw); EXPECT_EQ(r, 0);
  r = U(GetOutlineTextMetricsW)(d.dc, sw - 1, bw); t_note("W cbData=size-1 returns %lu", r); EXPECT_EQ(r, 0);
  cb = sizeof(OUTLINETEXTMETRICA);
  r = U(GetOutlineTextMetricsA)(d.dc, cb + 1, ba); EXPECT_EQ(r, 0);
  r = U(GetOutlineTextMetricsA)(d.dc, sa - 1, ba); t_note("A cbData=size-1 returns %lu", r); EXPECT_EQ(r, 0);
  /* exactly the size, and larger, succeed; the return value is the required size */
  r = U(GetOutlineTextMetricsW)(d.dc, sw, bw); EXPECT_EQ(r, sw);
  r = U(GetOutlineTextMetricsA)(d.dc, sa, ba); EXPECT_EQ(r, sa);
  r = U(GetOutlineTextMetricsW)(d.dc, sw + 64, bw); EXPECT_EQ(r, sw);
  r = U(GetOutlineTextMetricsA)(d.dc, sa + 64, ba); EXPECT_EQ(r, sa);
  /* invalid DC */
  r = U(GetOutlineTextMetricsW)(NULL, 0, NULL); EXPECT_EQ(r, 0);
  r = U(GetOutlineTextMetricsA)(NULL, 0, NULL); EXPECT_EQ(r, 0);
  free(buf);
  oa_close(&d);
}

/* Observed on Win10 22H2 (contradicts the common reading of the docs, which suggest a failure): raster faces also
 * return an OUTLINETEXTMETRIC, synthesised from the text metrics. */
TEST(otm_bitmap_font){
  static const WCHAR *k_bmp[] = { L"Terminal", L"MS Sans Serif", L"Small Fonts", L"Courier" };
  int i, tried = 0;
  for(i = 0; i < 4; i++){
    oa_font d; int h = (i == 0) ? -12 : -13; DWORD sw, sa; OUTLINETEXTMETRICW *w; OUTLINETEXTMETRICA *a;
    if(!oa_open(&d, k_bmp[i], h, 0)){ oa_close(&d); continue; }
    tried++;
    sw = U(GetOutlineTextMetricsW)(d.dc, 0, NULL); sa = U(GetOutlineTextMetricsA)(d.dc, 0, NULL);
    t_note("raster %ls: pf 0x%x h %ld; GetOutlineTextMetrics size W=%lu A=%lu", k_bmp[i], d.tm.tmPitchAndFamily, d.tm.tmHeight, sw, sa);
    CHECK(sw > sizeof *w); CHECK(sa > sizeof *a);
    w = (OUTLINETEXTMETRICW *)calloc(1, sw + 8); a = (OUTLINETEXTMETRICA *)calloc(1, sa + 8);
    if(w && a && U(GetOutlineTextMetricsW)(d.dc, sw, w) == sw && U(GetOutlineTextMetricsA)(d.dc, sa, a) == sa){
      DWORD fo = (DWORD)(ULONG_PTR)w->otmpFaceName, fam = (DWORD)(ULONG_PTR)w->otmpFamilyName;
      EXPECT_EQ(w->otmSize, sw); EXPECT_EQ(a->otmSize, sa);
      EXPECT_EQ(w->otmTextMetrics.tmHeight, d.tm.tmHeight); EXPECT_EQ(w->otmTextMetrics.tmAscent, d.tm.tmAscent);
      EXPECT_EQ(a->otmTextMetrics.tmHeight, d.tm.tmHeight);
      EXPECT_EQ(w->otmAscent, d.tm.tmAscent); EXPECT_EQ(w->otmDescent, -d.tm.tmDescent);
      EXPECT_EQ(w->otmEMSquare, d.tm.tmHeight - d.tm.tmInternalLeading);
      EXPECT_EQ(a->otmEMSquare, w->otmEMSquare);
      CHECK(!(w->otmTextMetrics.tmPitchAndFamily & TMPF_TRUETYPE));
      CHECK(fo >= sizeof *w && fo < sw && fam >= sizeof *w && fam < sw);
      if(fo < sw && fam < sw){
        CHECKF(!lstrcmpiW((WCHAR *)((char *)w + fo), k_bmp[i]), "raster face name text mismatch");
        CHECKF(!lstrcmpiW((WCHAR *)((char *)w + fam), k_bmp[i]), "raster family name text mismatch");
      }
    } else CHECKF(0, "%ls: could not fill OUTLINETEXTMETRIC", k_bmp[i]);
    free(w); free(a);
    oa_close(&d);
  }
  if(!tried) t_skip("no raster face (Terminal, MS Sans Serif, Small Fonts, Courier) realised as non-TrueType");
}

/* ---- GetETM (undocumented; decompiled gdi32full!GetETM forwards the buffer to the kernel, then stores
 * GetKerningPairsA(dc,0,NULL) at byte offset 0x30 of the output, i.e. the Win16 EXTTEXTMETRIC etmKernPairs) --- */
typedef struct { short v[26]; } etm_t;   /* 52 bytes: etmSize..etmKernTracks, as the 16-bit EXTTEXTMETRIC */

TEST(etm_vs_otm){
  oa_font d; int r; etm_t e; BYTE canary[8 + 52 + 8];
  int i;
  REQUIRE(UN(GetETM));
  for(i = 0; i < 2; i++){
    OUTLINETEXTMETRICW *w; DWORD sw; DWORD kp;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    memset(canary, 0x77, sizeof canary); memset(&e, 0, sizeof e);
    r = UN(GetETM)(d.dc, canary + 8);
    t_note("%ls: GetETM returns %d, etmSize %d", k_tt[i], r, ((etm_t *)(canary + 8))->v[0]);
    { int j, n = 0; for(j = 0; j < 8; j++) if(canary[j] != 0x77 || canary[8 + 52 + j] != 0x77) n++;
      CHECKF(n == 0, "GetETM wrote outside 52 bytes"); }
    CHECKF(r != 0, "%ls: GetETM returns 0", k_tt[i]);
    EXPECT_EQ(((etm_t *)(canary + 8))->v[0], 52);                          /* etmSize */
    if(r){
      etm_t *p = (etm_t *)(canary + 8);
      sw = U(GetOutlineTextMetricsW)(d.dc, 0, NULL);
      w = (OUTLINETEXTMETRICW *)calloc(1, sw + 8);
      if(w && U(GetOutlineTextMetricsW)(d.dc, sw, w)){
        t_note("%ls: etm master units %d cap %d xh %d  / otm EM %u cap %u xh %u", k_tt[i], p->v[6], p->v[7], p->v[8], w->otmEMSquare, w->otmsCapEmHeight, w->otmsXHeight);
        EXPECT_EQ((unsigned short)p->v[6], w->otmEMSquare);                 /* etmMasterUnits == EM square */
      }
      free(w);
      kp = U(GetKerningPairsA)(d.dc, 0, NULL);
      EXPECT_EQ((unsigned short)p->v[24], kp);                             /* etmKernPairs at offset 0x30 */
      t_note("%ls: etmKernPairs %u (GetKerningPairsA count %lu) etmKernTracks %u", k_tt[i], (unsigned short)p->v[24], kp, (unsigned short)p->v[25]);
    }
    oa_close(&d);
  }
  /* bitmap font: record */
  if(oa_open(&d, L"Terminal", -12, 0)){
    memset(canary, 0, sizeof canary);
    r = UN(GetETM)(d.dc, canary + 8);
    t_note("Terminal: GetETM returns %d", r);
    oa_close(&d);
  } else { oa_close(&d); }
  /* NULL buffer / NULL DC */
  r = UN(GetETM)(NULL, NULL); t_note("GetETM(NULL,NULL) returns %d", r); EXPECT_EQ(r, 0);
}
