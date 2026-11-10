#include "t.h"
/* Glyph rasterisation / font data conformance: invariants that hold on any Windows, not recorded Win10 numbers.
 * Every GDI/USER call goes through U(Name). Faces used: Arial, Tahoma, Segoe UI, Courier New, MS Shell Dlg, System.
 * A face that is not installed (GetTextFaceW reports another name) skips the test that needs it.
 * Decomp references are Win10 22H2 19041.2965 gdi32full.dll / win32kfull.sys. */

static const MAT2 IDENT = { {0,1}, {0,0}, {0,0}, {0,1} };

typedef struct { HDC dc; HFONT f, old; } fdc;

static HFONT mkfont(const WCHAR *face, int h, int wt, int it, int ul, int so, int q){
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = h; lf.lfWeight = wt; lf.lfItalic = (BYTE)it; lf.lfUnderline = (BYTE)ul; lf.lfStrikeOut = (BYTE)so;
    lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = (BYTE)q;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return U(CreateFontIndirectW)(&lf);
}
/* memory DC with a font selected; returns 0 (and skips) if the face is not realised as asked */
static int fdc_open(fdc *d, const WCHAR *face, int h, int q){
    memset(d, 0, sizeof *d);
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) { t_skip("CreateCompatibleDC failed"); return 0; }
    d->f = mkfont(face, h, FW_NORMAL, 0, 0, 0, q);
    d->old = (HFONT)U(SelectObject)(d->dc, d->f);
    WCHAR got[LF_FACESIZE] = L"";
    U(GetTextFaceW)(d->dc, LF_FACESIZE, got);
    /* MS Shell Dlg is an alias: accept whatever it maps to */
    if (lstrcmpiW(face, L"MS Shell Dlg") && lstrcmpiW(got, face)) {
        char a[64]; WideCharToMultiByte(CP_ACP, 0, face, -1, a, sizeof a, 0, 0);
        t_skip("face %s not installed", a);
        return 0;
    }
    return 1;
}
static void fdc_close(fdc *d){
    if (d->dc) { U(SelectObject)(d->dc, d->old); U(DeleteDC)(d->dc); }
    if (d->f) U(DeleteObject)(d->f);
    memset(d, 0, sizeof *d);
}

static const WCHAR *TT_FACES[] = { L"Arial", L"Tahoma", L"Segoe UI", L"Courier New" };
static const int HEIGHTS[] = { -8, -11, -12, -13, -16, -20, -32 };

/* ================================================================ GetGlyphOutlineW parameter checks */
TEST(glyph_ggo_null_metrics_ptr_fails) {
    REQUIRE(U(GetGlyphOutlineW));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    /* gdi32full!GetGlyphOutlineInternalW: lpgm == NULL or lpmat2 == NULL -> 0xFFFFFFFF before the syscall */
    for (UINT fmt = GGO_METRICS; fmt <= GGO_GRAY8_BITMAP; fmt++) {
        if (fmt == GGO_NATIVE || fmt == 3) continue;
        DWORD r = U(GetGlyphOutlineW)(d.dc, 'A', fmt, NULL, 0, NULL, &IDENT);
        CHECKF(r == GDI_ERROR, "fmt %u r=%lu", fmt, r);
    }
    fdc_close(&d);
}
TEST(glyph_ggo_null_matrix_fails) {
    REQUIRE(U(GetGlyphOutlineW));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    GLYPHMETRICS gm;
    for (UINT fmt = GGO_METRICS; fmt <= GGO_GRAY8_BITMAP; fmt++) {
        if (fmt == GGO_NATIVE || fmt == 3) continue;
        memset(&gm, 0x5A, sizeof gm);
        DWORD r = U(GetGlyphOutlineW)(d.dc, 'A', fmt, &gm, 0, NULL, NULL);
        CHECKF(r == GDI_ERROR, "fmt %u r=%lu", fmt, r);
        /* the early return happens before the kernel writes the metrics */
        CHECK(gm.gmBlackBoxX == 0x5A5A5A5A);
    }
    fdc_close(&d);
}
TEST(glyph_ggo_null_hdc_invalid_handle) {
    REQUIRE(U(GetGlyphOutlineW));
    GLYPHMETRICS gm; memset(&gm, 0, sizeof gm);
    /* win32kfull!GreGetGlyphOutlineInternal: DCOBJ lock fails -> EngSetLastError(ERROR_INVALID_HANDLE), -1 */
    SET_ERR(0);
    DWORD r = U(GetGlyphOutlineW)(NULL, 'A', GGO_METRICS, &gm, 0, NULL, &IDENT);
    EXPECT_FAIL(r == GDI_ERROR, ERROR_INVALID_HANDLE);
    SET_ERR(0);
    r = U(GetGlyphOutlineW)(NULL, 'A', GGO_GRAY8_BITMAP, &gm, 0, NULL, &IDENT);
    EXPECT_FAIL(r == GDI_ERROR, ERROR_INVALID_HANDLE);
}

/* ================================================================ GetGlyphOutlineW buffer protocol */
/* Documented layout: GGO_BITMAP rows are 1bpp DWORD-aligned, GGO_GRAYn_BITMAP rows are 1 byte per pixel DWORD-aligned,
 * levels 0..4 / 0..16 / 0..64; the size query (cjBuffer 0) returns the size the fetch fills. */
static void ggo_protocol(const WCHAR *face, int h, WCHAR ch){
    fdc d; if (!fdc_open(&d, face, h, DEFAULT_QUALITY)) return;
    static const UINT F[4] = { GGO_BITMAP, GGO_GRAY2_BITMAP, GGO_GRAY4_BITMAP, GGO_GRAY8_BITMAP };
    static const int MAXV[4] = { 255, 4, 16, 64 };
    GLYPHMETRICS m; memset(&m, 0, sizeof m);
    DWORD rm = U(GetGlyphOutlineW)(d.dc, ch, GGO_METRICS, &m, 0, NULL, &IDENT);
    CHECKF(rm != GDI_ERROR, "GGO_METRICS h=%d ch=%x", h, ch);
    for (int i = 0; i < 4; i++) {
        GLYPHMETRICS g1, g2; memset(&g1, 0, sizeof g1); memset(&g2, 0, sizeof g2);
        DWORD n = U(GetGlyphOutlineW)(d.dc, ch, F[i], &g1, 0, NULL, &IDENT);
        CHECKF(n != GDI_ERROR && n > 0, "fmt %u size h=%d ch=%x", F[i], h, ch);
        if (n == GDI_ERROR || n == 0) continue;
        DWORD stride = i == 0 ? ((g1.gmBlackBoxX + 31) / 32) * 4 : (g1.gmBlackBoxX + 3) & ~3u;
        CHECKF(n == stride * g1.gmBlackBoxY, "fmt %u h=%d ch=%x n=%lu stride=%lu bby=%u", F[i], h, ch, n, stride, g1.gmBlackBoxY);
        BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n + 16);
        memset(buf + n, 0xEE, 16);
        DWORD r = U(GetGlyphOutlineW)(d.dc, ch, F[i], &g2, n, buf, &IDENT);
        CHECKF(r == n, "fmt %u fetch r=%lu n=%lu", F[i], r, n);
        CHECK(!memcmp(&g1, &g2, sizeof g1));          /* query and fetch report the same metrics */
        int over = 0; for (int j = 0; j < 16; j++) over |= buf[n + j] != 0xEE;
        CHECKF(!over, "fmt %u wrote past cjBuffer", F[i]);
        if (i > 0) { int bad = 0; for (DWORD j = 0; j < n; j++) bad |= buf[j] > MAXV[i]; CHECKF(!bad, "fmt %u level > %d", F[i], MAXV[i]); }
        /* a glyph with a non-empty bitmap has ink */
        int any = 0; for (DWORD j = 0; j < n; j++) any |= buf[j];
        CHECKF(any, "fmt %u all zero h=%d ch=%x", F[i], h, ch);
        HeapFree(GetProcessHeap(), 0, buf);
        /* the advance does not depend on the format */
        CHECKF(g1.gmCellIncX == m.gmCellIncX && g1.gmCellIncY == m.gmCellIncY, "fmt %u inc %d vs %d", F[i], g1.gmCellIncX, m.gmCellIncX);
    }
    fdc_close(&d);
}
TEST(glyph_ggo_buffer_protocol) {
    REQUIRE(U(GetGlyphOutlineW));
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++)
        for (unsigned h = 0; h < sizeof HEIGHTS / sizeof *HEIGHTS; h++) {
            ggo_protocol(TT_FACES[f], HEIGHTS[h], 'A');
            ggo_protocol(TT_FACES[f], HEIGHTS[h], 'g');
            ggo_protocol(TT_FACES[f], HEIGHTS[h], 0xE9);
        }
}
TEST(glyph_ggo_space_has_no_bitmap) {
    REQUIRE(U(GetGlyphOutlineW));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    GLYPHMETRICS gm; memset(&gm, 0, sizeof gm);
    DWORD n = U(GetGlyphOutlineW)(d.dc, ' ', GGO_GRAY8_BITMAP, &gm, 0, NULL, &IDENT);
    /* an empty glyph needs no buffer; the advance is still reported */
    CHECKF(n == 0, "n=%lu", n);
    CHECK(gm.gmCellIncX > 0);
    fdc_close(&d);
}
TEST(glyph_ggo_glyph_index_matches_char) {
    REQUIRE(U(GetGlyphOutlineW)); REQUIRE(U(GetGlyphIndicesW));
    /* GreGetGlyphOutlineInternal: GGO_GLYPH_INDEX (0x80) skips vXlatGlyphArray and uses the index as is, so the index
     * from GetGlyphIndicesW must give the same glyph */
    static const WCHAR chars[] = L"AgW0.";
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        fdc d; if (!fdc_open(&d, TT_FACES[f], -13, DEFAULT_QUALITY)) continue;
        for (int i = 0; chars[i]; i++) {
            WORD gi = 0; CHECK(U(GetGlyphIndicesW)(d.dc, &chars[i], 1, &gi, 0) == 1);
            for (UINT fmt = GGO_METRICS; fmt <= GGO_GRAY8_BITMAP; fmt++) {
                if (fmt == GGO_NATIVE || fmt == 3) continue;
                GLYPHMETRICS a, b; BYTE ba[4096], bb[4096]; memset(ba, 0, sizeof ba); memset(bb, 0, sizeof bb);
                DWORD na = U(GetGlyphOutlineW)(d.dc, chars[i], fmt, &a, sizeof ba, fmt ? ba : NULL, &IDENT);
                DWORD nb = U(GetGlyphOutlineW)(d.dc, gi, fmt | GGO_GLYPH_INDEX, &b, sizeof bb, fmt ? bb : NULL, &IDENT);
                CHECKF(na == nb && !memcmp(&a, &b, sizeof a), "ch %x fmt %u", chars[i], fmt);
                if (fmt && na != GDI_ERROR && na <= sizeof ba) CHECKF(!memcmp(ba, bb, na), "bytes ch %x fmt %u", chars[i], fmt);
            }
        }
        fdc_close(&d);
    }
}
TEST(glyph_ggo_a_w_parity) {
    REQUIRE(U(GetGlyphOutlineW)); REQUIRE(U(GetGlyphOutlineA));
    fdc d; if (!fdc_open(&d, L"Tahoma", -11, DEFAULT_QUALITY)) return;
    for (int c = 0x21; c < 0x7F; c++) {
        GLYPHMETRICS a, w; BYTE ba[2048], bw[2048]; memset(ba, 0, sizeof ba); memset(bw, 0, sizeof bw);
        DWORD na = U(GetGlyphOutlineA)(d.dc, c, GGO_GRAY8_BITMAP, &a, sizeof ba, ba, &IDENT);
        DWORD nw = U(GetGlyphOutlineW)(d.dc, c, GGO_GRAY8_BITMAP, &w, sizeof bw, bw, &IDENT);
        CHECKF(na == nw && !memcmp(&a, &w, sizeof a) && (na == GDI_ERROR || !memcmp(ba, bw, na)), "char %x", c);
    }
    fdc_close(&d);
}
TEST(glyph_ggo_metrics_advance_equals_charwidth) {
    REQUIRE(U(GetGlyphOutlineW)); REQUIRE(U(GetCharWidth32W));
    /* GGO_METRICS gmCellIncX and GetCharWidth32W both come from the same rounded fxD advance (no overhang on a
     * regular TrueType face) */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++)
        for (unsigned h = 0; h < sizeof HEIGHTS / sizeof *HEIGHTS; h++) {
            fdc d; if (!fdc_open(&d, TT_FACES[f], HEIGHTS[h], NONANTIALIASED_QUALITY)) continue;
            int bad = 0;
            for (int c = 0x20; c < 0x7F; c++) {
                GLYPHMETRICS gm; INT cw = -1;
                U(GetGlyphOutlineW)(d.dc, c, GGO_METRICS, &gm, 0, NULL, &IDENT);
                U(GetCharWidth32W)(d.dc, c, c, &cw);
                if (gm.gmCellIncX != cw) { if (!bad) t_note("face %d h %d char %x inc %d cw %d", f, HEIGHTS[h], c, gm.gmCellIncX, cw); bad++; }
            }
            CHECKF(bad == 0, "face %u h %d: %d chars differ", f, HEIGHTS[h], bad);
            fdc_close(&d);
        }
}

/* ================================================================ TEXTMETRIC invariants */
TEST(glyph_tm_height_is_ascent_plus_descent) {
    REQUIRE(U(GetTextMetricsW));
    static const WCHAR *faces[] = { L"Arial", L"Tahoma", L"Segoe UI", L"Courier New", L"MS Shell Dlg", L"System", L"Courier", L"MS Sans Serif" };
    for (unsigned f = 0; f < sizeof faces / sizeof *faces; f++)
        for (unsigned h = 0; h < sizeof HEIGHTS / sizeof *HEIGHTS; h++) {
            HDC dc = U(CreateCompatibleDC)(NULL); HFONT fo = mkfont(faces[f], HEIGHTS[h], FW_NORMAL, 0, 0, 0, DEFAULT_QUALITY);
            HFONT o = (HFONT)U(SelectObject)(dc, fo);
            TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
            CHECK(U(GetTextMetricsW)(dc, &tm));
            CHECKF(tm.tmHeight == tm.tmAscent + tm.tmDescent, "face %u h %d: %ld != %ld + %ld", f, HEIGHTS[h], tm.tmHeight, tm.tmAscent, tm.tmDescent);
            CHECK(tm.tmInternalLeading >= 0 && tm.tmInternalLeading < tm.tmHeight);
            CHECK(tm.tmAveCharWidth > 0 && tm.tmMaxCharWidth >= tm.tmAveCharWidth);
            U(SelectObject)(dc, o); U(DeleteObject)(fo); U(DeleteDC)(dc);
        }
}
TEST(glyph_tm_truetype_em_height) {
    REQUIRE(U(GetTextMetricsW));
    /* lfHeight < 0 asks for a character (em) height: tmHeight - tmInternalLeading == -lfHeight for a TrueType face */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++)
        for (unsigned h = 0; h < sizeof HEIGHTS / sizeof *HEIGHTS; h++) {
            fdc d; if (!fdc_open(&d, TT_FACES[f], HEIGHTS[h], DEFAULT_QUALITY)) continue;
            TEXTMETRICW tm; memset(&tm, 0, sizeof tm); U(GetTextMetricsW)(d.dc, &tm);
            CHECKF(tm.tmHeight - tm.tmInternalLeading == -HEIGHTS[h], "face %u h %d: tmHeight %ld il %ld", f, HEIGHTS[h], tm.tmHeight, tm.tmInternalLeading);
            CHECK(tm.tmPitchAndFamily & TMPF_TRUETYPE);
            fdc_close(&d);
        }
}
TEST(glyph_tm_null_buffer_and_null_dc) {
    REQUIRE(U(GetTextMetricsW));
    /* gdi32full!GetTextMetricsW: the result stays 0 when lptm == NULL; an invalid DC fails the handle-table lookup */
    HDC dc = U(CreateCompatibleDC)(NULL);
    /* Wine dereferences lptm under its GDI lock; the harness's crash handler then leaves the lock held and the next
     * test deadlocks. The NULL-lptm call is therefore made on Windows only. */
    if (!t_is_wine()) CHECK(U(GetTextMetricsW)(dc, NULL) == 0);
    else t_note("wine: GetTextMetricsW(dc, NULL) not called");
    TEXTMETRICW tm;
    CHECK(U(GetTextMetricsW)(NULL, &tm) == 0);
    U(DeleteDC)(dc);
}
TEST(glyph_tm_a_w_parity) {
    REQUIRE(U(GetTextMetricsA)); REQUIRE(U(GetTextMetricsW));
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        fdc d; if (!fdc_open(&d, TT_FACES[f], -12, DEFAULT_QUALITY)) continue;
        TEXTMETRICA a; TEXTMETRICW w; memset(&a, 0, sizeof a); memset(&w, 0, sizeof w);
        CHECK(U(GetTextMetricsA)(d.dc, &a)); CHECK(U(GetTextMetricsW)(d.dc, &w));
        CHECK(a.tmHeight == w.tmHeight && a.tmAscent == w.tmAscent && a.tmDescent == w.tmDescent && a.tmInternalLeading == w.tmInternalLeading
              && a.tmExternalLeading == w.tmExternalLeading && a.tmAveCharWidth == w.tmAveCharWidth && a.tmMaxCharWidth == w.tmMaxCharWidth
              && a.tmWeight == w.tmWeight && a.tmOverhang == w.tmOverhang && a.tmItalic == w.tmItalic && a.tmPitchAndFamily == w.tmPitchAndFamily
              && a.tmCharSet == w.tmCharSet);
        fdc_close(&d);
    }
}

/* ================================================================ widths / extents */
TEST(glyph_abc_bad_params) {
    REQUIRE(U(GetCharABCWidthsW));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    ABC abc[4];
    /* gdi32full!bGetCharABCWidthsW: lpABC == NULL or wLast < wFirst -> SetLastError(87), FALSE */
    SET_ERR(0); EXPECT_FAIL(!U(GetCharABCWidthsW)(d.dc, 'B', 'A', abc), ERROR_INVALID_PARAMETER);
    SET_ERR(0); EXPECT_FAIL(!U(GetCharABCWidthsW)(d.dc, 'A', 'B', NULL), ERROR_INVALID_PARAMETER);
    fdc_close(&d);
}
TEST(glyph_abc_sum_is_width_minus_overhang) {
    REQUIRE(U(GetCharABCWidthsW)); REQUIRE(U(GetCharWidth32W));
    /* GreGetCharABCWidthsW: A = round(fxA), B = round(fxAB) - A, C = round(fxD) - round(fxAB), so A+B+C = round(fxD);
     * GreGetCharWidthW returns round(fxD + overhang) */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++)
        for (unsigned h = 0; h < sizeof HEIGHTS / sizeof *HEIGHTS; h++)
            for (int bold = 0; bold < 2; bold++) {
                HDC dc = U(CreateCompatibleDC)(NULL); HFONT fo = mkfont(TT_FACES[f], HEIGHTS[h], bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, NONANTIALIASED_QUALITY);
                HFONT o = (HFONT)U(SelectObject)(dc, fo);
                TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
                ABC abc[95]; INT cw[95];
                CHECK(U(GetCharABCWidthsW)(dc, 0x20, 0x7E, abc)); CHECK(U(GetCharWidth32W)(dc, 0x20, 0x7E, cw));
                int bad = 0; for (int i = 0; i < 95; i++) bad += abc[i].abcA + (int)abc[i].abcB + abc[i].abcC != cw[i] - tm.tmOverhang;
                CHECKF(bad == 0, "face %u h %d bold %d: %d chars", f, HEIGHTS[h], bold, bad);
                U(SelectObject)(dc, o); U(DeleteObject)(fo); U(DeleteDC)(dc);
            }
}
TEST(glyph_extent_zero_and_negative_count) {
    REQUIRE(U(GetTextExtentPoint32W));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    /* gdi32full!GetTextExtentPointWInternal: c == 0 -> TRUE with {0,0}; c < 0 -> SetLastError(87), FALSE */
    SIZE sz = { 77, 77 };
    CHECK(U(GetTextExtentPoint32W)(d.dc, L"abc", 0, &sz)); CHECK(sz.cx == 0 && sz.cy == 0);
    SET_ERR(0); EXPECT_FAIL(!U(GetTextExtentPoint32W)(d.dc, L"abc", -1, &sz), ERROR_INVALID_PARAMETER);
    fdc_close(&d);
}
TEST(glyph_extent_is_sum_of_widths) {
    REQUIRE(U(GetTextExtentPoint32W)); REQUIRE(U(GetCharWidth32W));
    /* RFONTOBJ::bTextExtent / bComputeTextExtent: cx = sum of the advances (+ overhang), cy = tmHeight */
    static const WCHAR *strs[] = { L"Name", L"OK", L"Cancel", L"Size", L"Hg", L"The quick brown fox jumps over the lazy dog" };
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++)
        for (unsigned h = 0; h < sizeof HEIGHTS / sizeof *HEIGHTS; h++) {
            fdc d; if (!fdc_open(&d, TT_FACES[f], HEIGHTS[h], NONANTIALIASED_QUALITY)) continue;
            TEXTMETRICW tm; U(GetTextMetricsW)(d.dc, &tm);
            for (unsigned s = 0; s < sizeof strs / sizeof *strs; s++) {
                int n = lstrlenW(strs[s]), sum = 0;
                for (int i = 0; i < n; i++) { INT cw = 0; U(GetCharWidth32W)(d.dc, strs[s][i], strs[s][i], &cw); sum += cw; }
                SIZE sz = { 0, 0 }; CHECK(U(GetTextExtentPoint32W)(d.dc, strs[s], n, &sz));
                CHECKF(sz.cy == tm.tmHeight, "cy %ld tmHeight %ld", sz.cy, tm.tmHeight);
                CHECKF(sz.cx == sum, "face %u h %d s %u: cx %ld sum %d", f, HEIGHTS[h], s, sz.cx, sum);
            }
            fdc_close(&d);
        }
}
TEST(glyph_extent_a_w_parity) {
    REQUIRE(U(GetTextExtentPoint32A)); REQUIRE(U(GetTextExtentPoint32W));
    fdc d; if (!fdc_open(&d, L"Segoe UI", -12, DEFAULT_QUALITY)) return;
    SIZE a = {0,0}, w = {0,0};
    CHECK(U(GetTextExtentPoint32A)(d.dc, "Cancel Apply OK", 15, &a));
    CHECK(U(GetTextExtentPoint32W)(d.dc, L"Cancel Apply OK", 15, &w));
    CHECK(a.cx == w.cx && a.cy == w.cy);
    fdc_close(&d);
}

/* ================================================================ GetGlyphIndicesW */
TEST(glyph_indices_count_and_missing_mark) {
    REQUIRE(U(GetGlyphIndicesW));
    /* GreGetGlyphIndicesW returns the count on success; GGI_MARK_NONEXISTING_GLYPHS reports 0xFFFF for an unmapped char */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        fdc d; if (!fdc_open(&d, TT_FACES[f], -12, DEFAULT_QUALITY)) continue;
        WCHAR s[] = { 'A', 'B', 0xFFFF, 'z' }; WORD gi[4] = { 1, 1, 1, 1 };
        CHECK(U(GetGlyphIndicesW)(d.dc, s, 4, gi, GGI_MARK_NONEXISTING_GLYPHS) == 4);
        CHECK(gi[2] == 0xFFFF);
        CHECK(gi[0] != 0xFFFF && gi[0] != 0 && gi[1] != gi[0]);
        WORD gj[4] = { 1, 1, 1, 1 };
        CHECK(U(GetGlyphIndicesW)(d.dc, s, 4, gj, 0) == 4);
        CHECK(gj[2] != 0xFFFF);                       /* without the flag the default glyph is reported */
        CHECK(gj[0] == gi[0] && gj[3] == gi[3]);
        fdc_close(&d);
    }
}
TEST(glyph_indices_null_dc) {
    REQUIRE(U(GetGlyphIndicesW));
    WORD gi[2]; /* GreGetGlyphIndicesW: XDCOBJ lock fails -> 0xFFFFFFFF */
    CHECK(U(GetGlyphIndicesW)(NULL, L"AB", 2, gi, 0) == GDI_ERROR);
}
TEST(glyph_indices_a_w_parity) {
    REQUIRE(U(GetGlyphIndicesA)); REQUIRE(U(GetGlyphIndicesW));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    char a[95]; WCHAR w[95]; for (int i = 0; i < 95; i++) { a[i] = (char)(0x20 + i); w[i] = (WCHAR)(0x20 + i); }
    WORD ga[95], gw[95];
    CHECK(U(GetGlyphIndicesA)(d.dc, a, 95, ga, 0) == 95);
    CHECK(U(GetGlyphIndicesW)(d.dc, w, 95, gw, 0) == 95);
    CHECK(!memcmp(ga, gw, sizeof ga));
    fdc_close(&d);
}

/* ================================================================ GetFontData */
#define TAG4(a,b,c,d) ((DWORD)(a) | ((DWORD)(b) << 8) | ((DWORD)(c) << 16) | ((DWORD)(d) << 24))
TEST(glyph_fontdata_null_dc_invalid_handle) {
    REQUIRE(U(GetFontData));
    /* win32kfull!ulGetFontData: XDCOBJ lock fails -> EngSetLastError(ERROR_INVALID_HANDLE), GDI_ERROR */
    SET_ERR(0);
    DWORD r = U(GetFontData)(NULL, 0, 0, NULL, 0);
    EXPECT_FAIL(r == GDI_ERROR, ERROR_INVALID_HANDLE);
}
TEST(glyph_fontdata_buffer_protocol) {
    REQUIRE(U(GetFontData));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    DWORD total = U(GetFontData)(d.dc, 0, 0, NULL, 0);
    CHECK(total != GDI_ERROR && total > 12);
    /* gdi32full!GetFontDataImpl: cjBuffer 0xFFFFFFFF -> GDI_ERROR; a NULL buffer turns the call into a size query */
    CHECK(U(GetFontData)(d.dc, 0, 0, NULL, 0xFFFFFFFF) == GDI_ERROR);
    CHECK(U(GetFontData)(d.dc, 0, 0, NULL, 100) == total);
    DWORD hl = U(GetFontData)(d.dc, TAG4('h','e','a','d'), 0, NULL, 0);
    CHECKF(hl == 54, "head length %lu", hl);         /* the OpenType 'head' table is 54 bytes */
    BYTE head[64]; memset(head, 0, sizeof head);
    CHECK(U(GetFontData)(d.dc, TAG4('h','e','a','d'), 0, head, hl) == hl);
    /* magicNumber 0x5F0F3CF5 at offset 12, big-endian */
    CHECK(head[12] == 0x5F && head[13] == 0x0F && head[14] == 0x3C && head[15] == 0xF5);
    BYTE part[4] = {0};
    CHECK(U(GetFontData)(d.dc, TAG4('h','e','a','d'), 12, part, 4) == 4);   /* dwOffset into the table */
    CHECK(!memcmp(part, head + 12, 4));
    BYTE *all = (BYTE *)HeapAlloc(GetProcessHeap(), 0, total);
    CHECK(U(GetFontData)(d.dc, 0, 0, all, total) == total);
    /* sfnt version 0x00010000 (TrueType outlines) */
    CHECK(all[0] == 0 && all[1] == 1 && all[2] == 0 && all[3] == 0);
    HeapFree(GetProcessHeap(), 0, all);
    fdc_close(&d);
}
TEST(glyph_fontdata_missing_table) {
    REQUIRE(U(GetFontData));
    fdc d; if (!fdc_open(&d, L"Arial", -12, DEFAULT_QUALITY)) return;
    DWORD r = U(GetFontData)(d.dc, TAG4('z','q','x','j'), 0, NULL, 0);
    CHECKF(r == GDI_ERROR, "r=%lu", r);
    fdc_close(&d);
}

/* ================================================================ font object round trips */
TEST(glyph_font_getobject_roundtrip) {
    REQUIRE(U(GetObjectW)); REQUIRE(U(CreateFontIndirectW));
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = -11; lf.lfWeight = FW_BOLD; lf.lfItalic = 1; lf.lfUnderline = 1; lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY; lstrcpyW(lf.lfFaceName, L"MS Shell Dlg 2");
    HFONT f = U(CreateFontIndirectW)(&lf); CHECK(f != NULL);
    CHECK(U(GetObjectW)(f, 0, NULL) == sizeof(LOGFONTW));
    LOGFONTW back; memset(&back, 0x33, sizeof back);
    CHECK(U(GetObjectW)(f, sizeof back, &back) == sizeof(LOGFONTW));
    /* the logical font keeps the request verbatim; substitution happens at realisation */
    CHECK(!memcmp(&back, &lf, offsetof(LOGFONTW, lfFaceName)));
    CHECK(!lstrcmpW(back.lfFaceName, L"MS Shell Dlg 2"));
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT o = (HFONT)U(SelectObject)(dc, f);
    CHECK(o != NULL);
    CHECK((HFONT)U(GetCurrentObject)(dc, OBJ_FONT) == f);
    CHECK((HFONT)U(SelectObject)(dc, o) == f);
    U(DeleteDC)(dc); U(DeleteObject)(f);
}
TEST(glyph_textface_buffer_protocol) {
    REQUIRE(U(GetTextFaceW));
    fdc d; if (!fdc_open(&d, L"Tahoma", -12, DEFAULT_QUALITY)) return;
    int n = U(GetTextFaceW)(d.dc, 0, NULL);          /* size query: characters including the terminating NUL */
    CHECKF(n == 7, "n=%d", n);
    WCHAR b[LF_FACESIZE]; int r = U(GetTextFaceW)(d.dc, LF_FACESIZE, b);
    t_note("GetTextFaceW fetch returned %d", r);
    EXPECT_WSTR(b, L"Tahoma");
    fdc_close(&d);
}
TEST(glyph_otm_size_query) {
    REQUIRE(U(GetOutlineTextMetricsW));
    fdc d; if (!fdc_open(&d, L"Segoe UI", -12, DEFAULT_QUALITY)) return;
    /* gdi32full!GetOutlineTextMetricsW passes cjCopy 0 when potm is NULL: a size query */
    UINT n = U(GetOutlineTextMetricsW)(d.dc, 0, NULL);
    CHECK(n >= sizeof(OUTLINETEXTMETRICW));
    CHECK(U(GetOutlineTextMetricsW)(d.dc, 1234, NULL) == n);
    OUTLINETEXTMETRICW *o = (OUTLINETEXTMETRICW *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
    CHECK(U(GetOutlineTextMetricsW)(d.dc, n, o) != 0);
    CHECK(o->otmSize == n);
    TEXTMETRICW tm; U(GetTextMetricsW)(d.dc, &tm);
    CHECK(o->otmTextMetrics.tmHeight == tm.tmHeight && o->otmTextMetrics.tmAscent == tm.tmAscent);
    CHECK(o->otmEMSquare > 0);
    HeapFree(GetProcessHeap(), 0, o);
    fdc_close(&d);
}

/* ================================================================ rendering invariants */
typedef struct { HDC dc; HBITMAP bmp, old; BYTE *bits; int w, h, stride; } dib32;
static int dib_new(dib32 *d, int w, int h){
    memset(d, 0, sizeof *d);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    d->dc = U(CreateCompatibleDC)(NULL); void *bits = NULL;
    d->bmp = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d->bmp) { U(DeleteDC)(d->dc); return 0; }
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bmp); d->bits = (BYTE *)bits; d->w = w; d->h = h; d->stride = w * 4;
    memset(d->bits, 0xFF, (size_t)d->stride * h);
    return 1;
}
static void dib_free(dib32 *d){ U(SelectObject)(d->dc, d->old); U(DeleteObject)(d->bmp); U(DeleteDC)(d->dc); }
static int px_white(const dib32 *d, int x, int y){ const BYTE *p = d->bits + (size_t)d->stride * y + 4 * x; return p[0] == 255 && p[1] == 255 && p[2] == 255; }

static void render(dib32 *d, HFONT f, const WCHAR *s, int method, const RECT *rc){
    HFONT o = (HFONT)U(SelectObject)(d->dc, f);
    U(SetBkMode)(d->dc, TRANSPARENT); U(SetTextColor)(d->dc, RGB(0,0,0)); U(SetBkColor)(d->dc, RGB(255,255,255));
    int n = lstrlenW(s);
    if (method == 0) U(TextOutW)(d->dc, 4, 2, s, n);
    else if (method == 1) U(ExtTextOutW)(d->dc, 4, 2, 0, NULL, s, n, NULL);
    else if (method == 2) { WORD gi[64]; U(GetGlyphIndicesW)(d->dc, s, n, gi, 0); U(ExtTextOutW)(d->dc, 4, 2, ETO_GLYPH_INDEX, NULL, (LPCWSTR)gi, n, NULL); }
    else if (method == 3) U(ExtTextOutW)(d->dc, 4, 2, ETO_CLIPPED, rc, s, n, NULL);
    else if (method == 4) U(ExtTextOutW)(d->dc, 4, 2, ETO_OPAQUE, rc, s, n, NULL);
    U(GdiFlush)();
    U(SelectObject)(d->dc, o);
}
TEST(glyph_render_textout_equals_exttextout) {
    REQUIRE(U(ExtTextOutW)); REQUIRE(U(CreateDIBSection));
    /* TextOutW is ExtTextOutW with no options; ETO_GLYPH_INDEX with the indices of the same string draws the same glyphs */
    static const int Q[] = { NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY };
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++)
        for (int q = 0; q < 3; q++) {
            HFONT fo = mkfont(TT_FACES[f], -12, FW_NORMAL, 0, 0, 0, Q[q]);
            dib32 a, b, c; if (!dib_new(&a, 120, 24) || !dib_new(&b, 120, 24) || !dib_new(&c, 120, 24)) { U(DeleteObject)(fo); t_skip("dib"); return; }
            render(&a, fo, L"Cancel Hg", 0, NULL); render(&b, fo, L"Cancel Hg", 1, NULL); render(&c, fo, L"Cancel Hg", 2, NULL);
            CHECKF(!memcmp(a.bits, b.bits, (size_t)a.stride * a.h), "face %u q %d TextOutW vs ExtTextOutW", f, Q[q]);
            CHECKF(!memcmp(a.bits, c.bits, (size_t)a.stride * a.h), "face %u q %d ETO_GLYPH_INDEX", f, Q[q]);
            int ink = 0; for (int y = 0; y < a.h; y++) for (int x = 0; x < a.w; x++) ink += !px_white(&a, x, y);
            CHECK(ink > 0);
            dib_free(&a); dib_free(&b); dib_free(&c); U(DeleteObject)(fo);
        }
}
TEST(glyph_render_clipped_stays_inside) {
    REQUIRE(U(ExtTextOutW)); REQUIRE(U(CreateDIBSection));
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        HFONT fo = mkfont(TT_FACES[f], -16, FW_NORMAL, 0, 0, 0, ANTIALIASED_QUALITY);
        dib32 a; if (!dib_new(&a, 160, 30)) { U(DeleteObject)(fo); t_skip("dib"); return; }
        RECT rc = { 10, 4, 40, 14 };
        render(&a, fo, L"WWWWWWWWWW", 3, &rc);
        int outside = 0, inside = 0;
        for (int y = 0; y < a.h; y++) for (int x = 0; x < a.w; x++) {
            int in = x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
            if (!px_white(&a, x, y)) { if (in) inside++; else outside++; }
        }
        CHECKF(outside == 0, "face %u: %d pixels outside the ETO_CLIPPED rectangle", f, outside);
        CHECK(inside > 0);
        dib_free(&a); U(DeleteObject)(fo);
    }
}
TEST(glyph_render_opaque_fills_rect) {
    REQUIRE(U(ExtTextOutW)); REQUIRE(U(CreateDIBSection));
    HFONT fo = mkfont(L"Arial", -12, FW_NORMAL, 0, 0, 0, NONANTIALIASED_QUALITY);
    dib32 a; if (!dib_new(&a, 100, 24)) { U(DeleteObject)(fo); t_skip("dib"); return; }
    HFONT o = (HFONT)U(SelectObject)(a.dc, fo);
    U(SetBkColor)(a.dc, RGB(0, 0, 255)); U(SetTextColor)(a.dc, RGB(255, 255, 255));
    RECT rc = { 50, 0, 100, 24 };   /* opaque rect away from the text: fills it with the background colour */
    U(ExtTextOutW)(a.dc, 4, 2, ETO_OPAQUE, &rc, L"", 0, NULL); U(GdiFlush)();
    const BYTE *p = a.bits + 4 * 60 + (size_t)a.stride * 10;
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0);     /* BGRA: blue */
    CHECK(px_white(&a, 10, 10));
    U(SelectObject)(a.dc, o); dib_free(&a); U(DeleteObject)(fo);
}
TEST(glyph_render_nonaa_is_bilevel) {
    REQUIRE(U(TextOutW)); REQUIRE(U(CreateDIBSection));
    /* NONANTIALIASED_QUALITY: black text on white has only black and white pixels */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        HFONT fo = mkfont(TT_FACES[f], -13, FW_NORMAL, 0, 0, 0, NONANTIALIASED_QUALITY);
        dib32 a; if (!dib_new(&a, 140, 24)) { U(DeleteObject)(fo); t_skip("dib"); return; }
        render(&a, fo, L"Name OK Cancel Size", 0, NULL);
        int grey = 0;
        for (int y = 0; y < a.h; y++) for (int x = 0; x < a.w; x++) {
            const BYTE *p = a.bits + (size_t)a.stride * y + 4 * x;
            if (!((p[0] == 0 && p[1] == 0 && p[2] == 0) || (p[0] == 255 && p[1] == 255 && p[2] == 255))) grey++;
        }
        CHECKF(grey == 0, "face %u: %d grey pixels", f, grey);
        dib_free(&a); U(DeleteObject)(fo);
    }
}
TEST(glyph_render_aa_is_grey) {
    REQUIRE(U(TextOutW)); REQUIRE(U(CreateDIBSection));
    /* ANTIALIASED_QUALITY never produces colour fringes (R == G == B on black-on-white); ClearType and smoothing
     * availability are system settings, so only the colour rule is checked */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        HFONT fo = mkfont(TT_FACES[f], -16, FW_NORMAL, 0, 0, 0, ANTIALIASED_QUALITY);
        dib32 a; if (!dib_new(&a, 160, 28)) { U(DeleteObject)(fo); t_skip("dib"); return; }
        render(&a, fo, L"Name OK Cancel Size", 0, NULL);
        int colour = 0, grey = 0;
        for (int y = 0; y < a.h; y++) for (int x = 0; x < a.w; x++) {
            const BYTE *p = a.bits + (size_t)a.stride * y + 4 * x;
            if (p[0] != p[1] || p[1] != p[2]) colour++; else if (p[0] != 0 && p[0] != 255) grey++;
        }
        CHECKF(colour == 0, "face %u: %d colour pixels", f, colour);
        t_note("face %u AA -16: %d intermediate grey pixels", f, grey);
        dib_free(&a); U(DeleteObject)(fo);
    }
}
TEST(glyph_drawtext_height_and_singleline) {
    REQUIRE(U(DrawTextW)); REQUIRE(U(CreateDIBSection));
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        fdc d; if (!fdc_open(&d, TT_FACES[f], -12, DEFAULT_QUALITY)) continue;
        TEXTMETRICW tm; U(GetTextMetricsW)(d.dc, &tm);
        RECT rc = { 0, 0, 0, 0 };
        /* DT_CALCRECT|DT_SINGLELINE: one line, the rectangle becomes the text extent; the return value is its height */
        int h = U(DrawTextW)(d.dc, L"Cancel", -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SIZE sz; U(GetTextExtentPoint32W)(d.dc, L"Cancel", 6, &sz);
        CHECKF(h == tm.tmHeight, "h=%d tmHeight=%ld", h, tm.tmHeight);
        CHECKF(rc.right - rc.left == sz.cx && rc.bottom - rc.top == tm.tmHeight, "rc %ld,%ld,%ld,%ld ext %ld", rc.left, rc.top, rc.right, rc.bottom, sz.cx);
        /* "&&" is a literal ampersand, "&C" is a prefix: neither counts as an extra char in the measured width */
        RECT r2 = { 0, 0, 0, 0 }; U(DrawTextW)(d.dc, L"&Cancel", -1, &r2, DT_CALCRECT | DT_SINGLELINE);
        CHECK(r2.right - r2.left == sz.cx);
        fdc_close(&d);
    }
}
TEST(glyph_synth_bold_italic_flags) {
    REQUIRE(U(GetTextMetricsW));
    /* the realised TEXTMETRIC reports the requested emphasis whether the face is real or synthesized */
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) {
        HDC dc = U(CreateCompatibleDC)(NULL);
        HFONT fo = mkfont(TT_FACES[f], -12, FW_BOLD, 1, 1, 1, DEFAULT_QUALITY);
        HFONT o = (HFONT)U(SelectObject)(dc, fo);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm); CHECK(U(GetTextMetricsW)(dc, &tm));
        CHECK(tm.tmItalic && tm.tmUnderlined && tm.tmStruckOut);
        CHECKF(tm.tmWeight >= FW_SEMIBOLD, "weight %ld", tm.tmWeight);
        U(SelectObject)(dc, o); U(DeleteObject)(fo); U(DeleteDC)(dc);
    }
}
