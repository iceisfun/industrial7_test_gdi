#include "t.h"
#include <stdlib.h>
/* DPI-scaling conformance tests: only invariants that hold at ANY system DPI, in any DPI-awareness context.
 * Each hard CHECK cites the Win10 22H2 (19041.2965) decomp it was verified against (corpus explorer API,
 * /api/function/<module>/<name>/decomp). Recorded Win10 numbers (per DPI) live in probes/dpi_scaling and are turned
 * into golden checks elsewhere; nothing here depends on the system DPI value.
 *
 * Sources:
 *  [VDAC]  user32!ValidateDpiAwarenessContextEx @0x16d20: -1 -> 0x6010, -2 -> (procdpi<<8)|0x11, -3 -> 0x12, -4 -> 0x22,
 *          -5 -> 0x40006010; any other value must be one of those packed forms (0x...11 with any 9-bit DPI is valid when
 *          not setting); else ERROR_INVALID_PARAMETER (87) and 0. With "set" semantics a system-aware value whose DPI
 *          != the process DPI is rejected too.
 *  [AWARE] user32!GetAwarenessFromDpiAwarenessContext @0x27e50 = packed & 0xf, or -1 when invalid;
 *          user32!GetDpiFromDpiAwarenessContext @0x86550 = (packed >> 8) & 0x1ff, or 0 when invalid;
 *          user32!AreDpiAwarenessContextsEqual @0x29310 = both valid && ((a ^ b) & 0x7fffffff) == 0.
 *  [STDAC] user32!SetThreadDpiAwarenessContext @0x16c80: validates with set semantics; invalid -> returns NULL.
 *  [GDFS]  user32!GetDpiForSystem @0x11650: thread awareness 0 (unaware) on a composited desktop -> 96.
 *  [GSMD]  user32!RealGetSystemMetricsForDpi @0x20520: dpi <= 0 or index < 0 -> 0 + error 87; an index that is not
 *          DPI-dependent -> RealGetSystemMetrics(index); DPI-dependent ones read the shared gpsi table (96: gpsi+0x964).
 *  [SPID]  win32kfull!NtUserSystemParametersInfoForDpi @0x38410 / _SystemParametersInfoForDpi @0x3858c: dpi != 0 and
 *          pv != NULL, action only SPI_GETNONCLIENTMETRICS (uiParam == cbSize == 0x1f8), SPI_GETICONTITLELOGFONT
 *          (uiParam == 0x5c), SPI_GETICONMETRICS (uiParam == cbSize == 0x6c); anything else -> FALSE + error 87.
 *  [NCMD]  win32kfull!GetWindowNCMetricsForDpi @0x5e988: iCaptionHeight/iMenuHeight/iScrollWidth/... are
 *          GetDpiDependentMetric(slot, dpi) - the same gpsi table user32!GetDpiDependentMetric @0x1334c reads.
 *  [AWRD]  user32!AdjustWindowRectExForDpi @0x103c0: dpi 0 -> FALSE + 87; the frame (n) is added to all four sides,
 *          WS_EX_CLIENTEDGE adds 2 to n, WS_CAPTION adds SM_CYCAPTION (SM_CYSMCAPTION with WS_EX_TOOLWINDOW) and a menu
 *          SM_CYMENU, both ForDpi, to the top only.
 *  [TM]    win32kfull!vIFIMetricsToTextMetricWStrict @0x5d88c: tmDescent = tmHeight - tmAscent; tmOverhang = 0 for
 *          TrueType (docs/research/02 section 6.2).
 *  [EXT]   gdi32full!bComputeTextExtent @0x10ba0 / win32kfull!RFONTOBJ::bTextExtent @0x25044: cx = round(sum of 28.4
 *          advances), cy = tmHeight; GreGetCharWidthW @0xf70d4 rounds each advance; GreGetCharABCWidthsW @0xf1738:
 *          A+B+C == round(advance) == CharWidth32 - overhang (docs/research/02 section 6.5).
 *  [GOBJ]  gdi32full!GetObjectW @0x19f60: font handle, pv NULL -> 0x5c; DC-type handle -> 0 + ERROR_INVALID_HANDLE;
 *          win32kfull!GreExtGetObjectW @0x99f88: copies min(c, stored) bytes of the stored LOGFONT (unchanged).
 *  [TMA]   gdi32full!GetTextMetricsAImpl @0x4ce0: the same internal W metrics, converted by vTextMetricWToTextMetricStrict.
 */

/* every USER/GDI call goes through the harness resolver (the DLLs under test are never linked) */
#define GetDC U(GetDC)
#define ReleaseDC U(ReleaseDC)
#define GetDeviceCaps U(GetDeviceCaps)
#define SelectObject U(SelectObject)
#define DeleteObject U(DeleteObject)
#define CreateFontIndirectW U(CreateFontIndirectW)
#define CreateFontW U(CreateFontW)
#define GetObjectW U(GetObjectW)
#define GetTextMetricsW U(GetTextMetricsW)
#define GetTextMetricsA U(GetTextMetricsA)
#define GetTextExtentPoint32W U(GetTextExtentPoint32W)
#define GetTextExtentPoint32A U(GetTextExtentPoint32A)
#define GetCharWidth32W U(GetCharWidth32W)
#define GetCharABCWidthsW U(GetCharABCWidthsW)
#define GetStockObject U(GetStockObject)
#define GetSystemMetrics U(GetSystemMetrics)
#define GetDesktopWindow U(GetDesktopWindow)
#define C(v) ((DPI_AWARENESS_CONTEXT)(LONG_PTR)(v))
static const LONG_PTR CTX_ALL[] = { -1, -2, -3, -4, -5 };

/* run fn once in each thread DPI context that can be entered (plus the process default) */
typedef void (*ctx_fn)(const char *ctxname);
static void each_ctx(ctx_fn fn) {
    static const struct { const char *n; LONG_PTR v; } L[] = { {"proc",0}, {"unaware",-1}, {"system",-2}, {"pm2",-4} };
    __typeof__(&SetThreadDpiAwarenessContext) set = U(SetThreadDpiAwarenessContext);
    for (unsigned i = 0; i < sizeof L / sizeof *L; i++) {
        DPI_AWARENESS_CONTEXT old = NULL;
        if (L[i].v) { if (!set) continue; old = set(C(L[i].v)); if (!old) { t_note("cannot enter ctx %s", L[i].n); continue; } }
        fn(L[i].n);
        if (old) set(old);
    }
}

/* ---------------------------------------------------------------- awareness contexts */
TEST(dpictx_pseudo_handles_valid) {
    REQUIRE(U(IsValidDpiAwarenessContext)); REQUIRE(U(GetAwarenessFromDpiAwarenessContext)); REQUIRE(U(GetDpiFromDpiAwarenessContext));
    static const int aw[] = { 0, 1, 2, 2, 0 };          /* [VDAC] + [AWARE] */
    for (int i = 0; i < 5; i++) {
        CHECKF(U(IsValidDpiAwarenessContext)(C(CTX_ALL[i])), "ctx %d", (int)CTX_ALL[i]);
        int a = U(GetAwarenessFromDpiAwarenessContext)(C(CTX_ALL[i]));
        CHECKF(a == aw[i], "ctx %d awareness %d want %d", (int)CTX_ALL[i], a, aw[i]);
    }
    EXPECT_EQ(U(GetDpiFromDpiAwarenessContext)(C(-1)), 96);   /* 0x6010 */
    EXPECT_EQ(U(GetDpiFromDpiAwarenessContext)(C(-5)), 96);   /* 0x40006010 */
    EXPECT_EQ(U(GetDpiFromDpiAwarenessContext)(C(-3)), 0);    /* 0x12 */
    EXPECT_EQ(U(GetDpiFromDpiAwarenessContext)(C(-4)), 0);    /* 0x22 */
    UINT sys = U(GetDpiFromDpiAwarenessContext)(C(-2));
    CHECKF(sys >= 96 && sys <= 511, "system-aware ctx dpi %u", sys);
}

TEST(dpictx_invalid_values) {
    REQUIRE(U(IsValidDpiAwarenessContext));
    SET_ERR(0); CHECK(!U(IsValidDpiAwarenessContext)(NULL)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!U(IsValidDpiAwarenessContext)(C(-6))); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!U(IsValidDpiAwarenessContext)(C(0x6012))); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* unaware DPI bits + pm nibble */
    EXPECT_EQ(U(GetAwarenessFromDpiAwarenessContext)(NULL), -1);   /* DPI_AWARENESS_INVALID */
    EXPECT_EQ(U(GetDpiFromDpiAwarenessContext)(NULL), 0);
    CHECK(!U(AreDpiAwarenessContextsEqual)(NULL, NULL));
    CHECK(!U(AreDpiAwarenessContextsEqual)(NULL, C(-1)));
}

TEST(dpictx_packed_system_value) {
    /* [VDAC] (v & 0x5ffe00ff) == 0x11 is a valid system-aware value carrying any 9-bit DPI when only validated ... */
    REQUIRE(U(IsValidDpiAwarenessContext));
    DPI_AWARENESS_CONTEXT v = C((0x1ff << 8) | 0x11);              /* "system aware at 511 DPI" */
    CHECK(U(IsValidDpiAwarenessContext)(v));
    EXPECT_EQ(U(GetAwarenessFromDpiAwarenessContext)(v), 1);
    EXPECT_EQ(U(GetDpiFromDpiAwarenessContext)(v), 511);
    /* ... but SetThreadDpiAwarenessContext validates with set semantics: its DPI must equal the process DPI [VDAC][STDAC] */
    REQUIRE(U(SetThreadDpiAwarenessContext));
    SET_ERR(0);
    DPI_AWARENESS_CONTEXT r = U(SetThreadDpiAwarenessContext)(v);
    CHECK(r == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    if (r) U(SetThreadDpiAwarenessContext)(r);
}

TEST(dpictx_equality) {
    REQUIRE(U(AreDpiAwarenessContextsEqual));
    for (int i = 0; i < 5; i++) CHECKF(U(AreDpiAwarenessContextsEqual)(C(CTX_ALL[i]), C(CTX_ALL[i])), "ctx %d", (int)CTX_ALL[i]);
    CHECK(!U(AreDpiAwarenessContextsEqual)(C(-3), C(-4)));   /* 0x12 vs 0x22 */
    CHECK(!U(AreDpiAwarenessContextsEqual)(C(-1), C(-5)));   /* 0x6010 vs 0x40006010: bit 30 counts */
    CHECK(!U(AreDpiAwarenessContextsEqual)(C(-1), C(-4)));
    CHECK(U(AreDpiAwarenessContextsEqual)(C(-1), C(0x6010))); /* a packed value equals its pseudo handle */
    CHECK(U(AreDpiAwarenessContextsEqual)(C(-4), C(0x22)));
}

TEST(dpictx_thread_roundtrip) {
    REQUIRE(U(SetThreadDpiAwarenessContext)); REQUIRE(U(GetThreadDpiAwarenessContext));
    GetDesktopWindow();                                       /* make this a GUI thread */
    DPI_AWARENESS_CONTEXT orig = U(GetThreadDpiAwarenessContext)();
    CHECK(U(IsValidDpiAwarenessContext)(orig));
    for (int i = 0; i < 5; i++) {
        DPI_AWARENESS_CONTEXT prev = U(SetThreadDpiAwarenessContext)(C(CTX_ALL[i]));
        CHECKF(prev != NULL, "set ctx %d", (int)CTX_ALL[i]);
        if (!prev) continue;
        DPI_AWARENESS_CONTEXT cur = U(GetThreadDpiAwarenessContext)();
        CHECKF(U(AreDpiAwarenessContextsEqual)(cur, C(CTX_ALL[i])), "now ctx %d", (int)CTX_ALL[i]);
        DPI_AWARENESS_CONTEXT back = U(SetThreadDpiAwarenessContext)(prev);
        CHECKF(U(AreDpiAwarenessContextsEqual)(back, C(CTX_ALL[i])), "set returns previous for ctx %d", (int)CTX_ALL[i]);
        CHECK(U(AreDpiAwarenessContextsEqual)(U(GetThreadDpiAwarenessContext)(), orig));
    }
    SET_ERR(0);
    CHECK(U(SetThreadDpiAwarenessContext)(NULL) == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(U(AreDpiAwarenessContextsEqual)(U(GetThreadDpiAwarenessContext)(), orig));   /* failed set changes nothing */
}

TEST(dpiforsystem_unaware_thread_is_96) {
    REQUIRE(U(GetDpiForSystem)); REQUIRE(U(SetThreadDpiAwarenessContext));
    DPI_AWARENESS_CONTEXT old = U(SetThreadDpiAwarenessContext)(C(-1));
    REQUIRE(old);
    EXPECT_EQ(U(GetDpiForSystem)(), 96);                      /* [GDFS], desktop is always composited on Win8+ */
    DPI_AWARENESS_CONTEXT o2 = U(SetThreadDpiAwarenessContext)(C(-5));
    if (o2) EXPECT_EQ(U(GetDpiForSystem)(), 96);              /* gdiscaled: awareness nibble 0 too */
    U(SetThreadDpiAwarenessContext)(old);
}

/* ---------------------------------------------------------------- GetSystemMetricsForDpi */
TEST(smfordpi_bad_params) {
    REQUIRE(U(GetSystemMetricsForDpi));
    SET_ERR(0); EXPECT_EQ(U(GetSystemMetricsForDpi)(SM_CYCAPTION, 0), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); EXPECT_EQ(U(GetSystemMetricsForDpi)(-1, 96), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); EXPECT_EQ(U(GetSystemMetricsForDpi)(SM_CXVSCROLL, (UINT)-96), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER); /* (int)dpi <= 0 */
}

static void smfordpi_passthrough(const char *ctx) {
    static const int idx[] = { SM_CXSCREEN, SM_CYSCREEN, SM_SWAPBUTTON, SM_CMOUSEBUTTONS, SM_CXVIRTUALSCREEN, SM_CMONITORS };
    static const UINT dpis[] = { 96, 144, 192 };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) for (unsigned j = 0; j < 3; j++) {
        int a = U(GetSystemMetricsForDpi)(idx[i], dpis[j]), b = GetSystemMetrics(idx[i]);
        CHECKF(a == b, "ctx %s index %d dpi %u: ForDpi %d GetSystemMetrics %d", ctx, idx[i], dpis[j], a, b);   /* [GSMD] */
    }
}
TEST(smfordpi_unscaled_index_passthrough) { REQUIRE(U(GetSystemMetricsForDpi)); each_ctx(smfordpi_passthrough); }

static int g_sm96[8];
static const int SCALED[8] = { SM_CXVSCROLL, SM_CYHSCROLL, SM_CYCAPTION, SM_CYMENU, SM_CXSMICON, SM_CYSMICON, SM_CXICON, SM_CXFRAME };
static void smfordpi_96(const char *ctx) {
    for (int i = 0; i < 8; i++) {
        int v = U(GetSystemMetricsForDpi)(SCALED[i], 96);
        CHECKF(v == g_sm96[i], "ctx %s index %d at 96: %d vs %d in the process context", ctx, SCALED[i], v, g_sm96[i]);
    }
}
TEST(smfordpi_96_is_thread_independent) {
    /* [GSMD]: dpi 96 reads gpsi+0x964, a global table: same value whatever the thread's awareness */
    REQUIRE(U(GetSystemMetricsForDpi));
    for (int i = 0; i < 8; i++) { g_sm96[i] = U(GetSystemMetricsForDpi)(SCALED[i], 96); CHECKF(g_sm96[i] > 0, "index %d", SCALED[i]); }
    each_ctx(smfordpi_96);
}

/* ---------------------------------------------------------------- SystemParametersInfoForDpi */
TEST(spifordpi_param_protocol) {
    REQUIRE(U(SystemParametersInfoForDpi));
    __typeof__(&SystemParametersInfoForDpi) spi = U(SystemParametersInfoForDpi);
    NONCLIENTMETRICSW n; ICONMETRICSW im; LOGFONTW lf; RECT rc;
    CHECK(sizeof n == 0x1f8 && sizeof im == 0x6c && sizeof lf == 0x5c);
    memset(&n, 0, sizeof n); n.cbSize = sizeof n;
    SET_ERR(0); CHECK(!spi(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 0)); EXPECT_ERR(ERROR_INVALID_PARAMETER);       /* dpi 0 */
    SET_ERR(0); CHECK(!spi(SPI_GETNONCLIENTMETRICS, sizeof n, NULL, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER);    /* pv NULL */
    SET_ERR(0); CHECK(!spi(SPI_GETNONCLIENTMETRICS, sizeof n - 4, &n, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER);  /* uiParam != 0x1f8 */
    n.cbSize = sizeof n - 4;                                                                                        /* pre-Vista size */
    SET_ERR(0); CHECK(!spi(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER);      /* cbSize != 0x1f8 */
    memset(&im, 0, sizeof im); im.cbSize = sizeof im - 4;
    SET_ERR(0); CHECK(!spi(SPI_GETICONMETRICS, sizeof im, &im, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!spi(SPI_GETICONTITLELOGFONT, sizeof(LOGFONTA), &lf, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER); /* uiParam != 0x5c */
    SET_ERR(0); CHECK(!spi(SPI_GETWORKAREA, sizeof rc, &rc, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER);              /* unsupported action */
    SET_ERR(0); CHECK(!spi(SPI_GETFONTSMOOTHING, 0, &rc, 0, 96)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(spifordpi_ncm_matches_smfordpi) {
    /* [NCMD] + [GSMD]: both sides read GetDpiDependentMetric(slot, dpi) from the same gpsi table */
    REQUIRE(U(SystemParametersInfoForDpi)); REQUIRE(U(GetSystemMetricsForDpi));
    static const UINT dpis[] = { 96, 120, 144, 168, 192 };
    int border96 = -1;
    for (unsigned j = 0; j < 5; j++) {
        UINT d = dpis[j]; NONCLIENTMETRICSW n; memset(&n, 0, sizeof n); n.cbSize = sizeof n;
        if (!U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, d)) { CHECKF(0, "SPI ForDpi NCM failed at %u gle %lu", d, GetLastError()); continue; }
#define SMD(i) U(GetSystemMetricsForDpi)(i, d)
        CHECKF(n.iScrollWidth == SMD(SM_CXVSCROLL), "dpi %u iScrollWidth %d", d, n.iScrollWidth);
        CHECKF(n.iScrollHeight == SMD(SM_CYVSCROLL), "dpi %u iScrollHeight %d", d, n.iScrollHeight);
        CHECKF(n.iCaptionWidth == SMD(SM_CXSIZE), "dpi %u iCaptionWidth %d", d, n.iCaptionWidth);
        CHECKF(n.iCaptionHeight == SMD(SM_CYSIZE), "dpi %u iCaptionHeight %d", d, n.iCaptionHeight);
        CHECKF(n.iSmCaptionWidth == SMD(SM_CXSMSIZE), "dpi %u iSmCaptionWidth %d", d, n.iSmCaptionWidth);
        CHECKF(n.iSmCaptionHeight == SMD(SM_CYSMSIZE), "dpi %u iSmCaptionHeight %d", d, n.iSmCaptionHeight);
        CHECKF(n.iMenuWidth == SMD(SM_CXMENUSIZE), "dpi %u iMenuWidth %d", d, n.iMenuWidth);
        CHECKF(n.iMenuHeight == SMD(SM_CYMENUSIZE), "dpi %u iMenuHeight %d", d, n.iMenuHeight);
        CHECKF(n.iPaddedBorderWidth == SMD(SM_CXPADDEDBORDER), "dpi %u iPaddedBorderWidth %d", d, n.iPaddedBorderWidth);
#undef SMD
        if (d == 96) border96 = n.iBorderWidth;
        if (d == 192 && border96 >= 0) CHECKF(n.iBorderWidth == 2 * border96, "iBorderWidth 192 %d = EngMulDiv(%d,192,96)", n.iBorderWidth, border96);
    }
}

/* ---------------------------------------------------------------- AdjustWindowRectExForDpi */
TEST(awrfordpi_bad_dpi) {
    REQUIRE(U(AdjustWindowRectExForDpi));
    RECT r = { 0, 0, 100, 100 };
    SET_ERR(0); CHECK(!U(AdjustWindowRectExForDpi)(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, 0)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(r.left == 0 && r.top == 0 && r.right == 100 && r.bottom == 100);    /* untouched */
}

TEST(awrfordpi_frame_composition) {
    REQUIRE(U(AdjustWindowRectExForDpi)); REQUIRE(U(GetSystemMetricsForDpi));
    static const struct { DWORD st, ex; } S[] = {
        { WS_OVERLAPPEDWINDOW, 0 }, { WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, WS_EX_DLGMODALFRAME },
        { WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW }, { WS_POPUP | WS_BORDER, 0 }, { WS_POPUP | WS_THICKFRAME, 0 },
        { WS_CHILD | WS_BORDER, 0 }, { WS_POPUP, 0 }, { WS_CAPTION, WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE } };
    static const UINT dpis[] = { 96, 120, 144, 168, 192 };
    for (unsigned s = 0; s < sizeof S / sizeof *S; s++) for (unsigned j = 0; j < 5; j++) for (int menu = 0; menu < 2; menu++) {
        UINT d = dpis[j]; RECT r = { 0, 0, 100, 100 };
        if (!U(AdjustWindowRectExForDpi)(&r, S[s].st, menu, S[s].ex, d)) { CHECKF(0, "style %u dpi %u failed", s, d); continue; }
        int n = -r.left;
        CHECKF(r.right - 100 == n && r.bottom - 100 == n, "style %u dpi %u menu %d: sides %ld %ld %ld", s, d, menu, -r.left, r.right - 100, r.bottom - 100);
        int cap = 0;
        if ((S[s].st & WS_CAPTION) == WS_CAPTION) cap = U(GetSystemMetricsForDpi)((S[s].ex & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION, d);
        if (menu) cap += U(GetSystemMetricsForDpi)(SM_CYMENU, d);
        CHECKF(-r.top - n == cap, "style %u dpi %u menu %d: top %ld frame %d caption+menu %d", s, d, menu, -r.top, n, cap);
        RECT r2 = { 0, 0, 100, 100 };
        if (U(AdjustWindowRectExForDpi)(&r2, S[s].st, menu, S[s].ex | WS_EX_CLIENTEDGE, d))
            CHECKF(-r2.left == n + 2 && -r2.top == -r.top + 2, "style %u dpi %u: clientedge adds 2 (%ld vs %d)", s, d, -r2.left, n);
    }
}

/* ---------------------------------------------------------------- GDI font invariants at pt-derived heights */
static const char *TT_FACES[] = { "Segoe UI", "Tahoma", "Arial", "MS Shell Dlg", "MS Shell Dlg 2", "Microsoft Sans Serif", "Courier New", "System" };
static const char STR[] = "The quick brown fox jumps over the lazy dog 0123456789";

static void font_invariants(const char *ctx) {
    HDC dc = GetDC(NULL); int y = GetDeviceCaps(dc, LOGPIXELSY);
    static const int pts[] = { 8, 9, 12 };
    for (unsigned f = 0; f < sizeof TT_FACES / sizeof *TT_FACES; f++) for (unsigned p = 0; p < 3; p++) {
        LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -MulDiv(pts[p], y, 72); lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
        MultiByteToWideChar(CP_ACP, 0, TT_FACES[f], -1, lf.lfFaceName, LF_FACESIZE);
        HFONT h = CreateFontIndirectW(&lf); if (!h) { CHECKF(0, "CreateFontIndirectW %s", TT_FACES[f]); continue; }
        LOGFONTW back; memset(&back, 0xcc, sizeof back);
        EXPECT_EQ(GetObjectW(h, sizeof back, &back), sizeof(LOGFONTW));                         /* [GOBJ] */
        CHECKF(back.lfHeight == lf.lfHeight && back.lfWeight == lf.lfWeight && !lstrcmpW(back.lfFaceName, lf.lfFaceName),
               "ctx %s %s: GetObject returns the stored LOGFONT", ctx, TT_FACES[f]);
        HFONT old = (HFONT)SelectObject(dc, h);
        CHECK(old != NULL);
        TEXTMETRICW tm; TEXTMETRICA tma;
        if (!GetTextMetricsW(dc, &tm) || !GetTextMetricsA(dc, &tma)) { CHECKF(0, "GetTextMetrics %s", TT_FACES[f]); SelectObject(dc, old); DeleteObject(h); continue; }
        int tt = (tm.tmPitchAndFamily & TMPF_TRUETYPE) != 0;
        CHECKF(tm.tmHeight == tm.tmAscent + tm.tmDescent, "ctx %s %s %dpt: h %ld a %ld d %ld", ctx, TT_FACES[f], pts[p], tm.tmHeight, tm.tmAscent, tm.tmDescent); /* [TM] */
        if (tt) CHECKF(tm.tmOverhang == 0, "ctx %s %s: TT overhang %ld", ctx, TT_FACES[f], tm.tmOverhang);
        /* [TMA] A and W carry the same numbers */
        CHECKF(tma.tmHeight == tm.tmHeight && tma.tmAscent == tm.tmAscent && tma.tmDescent == tm.tmDescent &&
               tma.tmInternalLeading == tm.tmInternalLeading && tma.tmExternalLeading == tm.tmExternalLeading &&
               tma.tmAveCharWidth == tm.tmAveCharWidth && tma.tmMaxCharWidth == tm.tmMaxCharWidth && tma.tmWeight == tm.tmWeight &&
               tma.tmOverhang == tm.tmOverhang && tma.tmDigitizedAspectX == tm.tmDigitizedAspectX &&
               tma.tmDigitizedAspectY == tm.tmDigitizedAspectY && tma.tmItalic == tm.tmItalic && tma.tmUnderlined == tm.tmUnderlined &&
               tma.tmStruckOut == tm.tmStruckOut && tma.tmPitchAndFamily == tm.tmPitchAndFamily && tma.tmCharSet == tm.tmCharSet,
               "ctx %s %s %dpt: TEXTMETRICA != TEXTMETRICW", ctx, TT_FACES[f], pts[p]);
        /* [EXT] extent vs widths */
        int n = (int)strlen(STR); WCHAR ws[64]; MultiByteToWideChar(CP_ACP, 0, STR, -1, ws, 64);
        SIZE sz; CHECK(GetTextExtentPoint32W(dc, ws, n, &sz));
        CHECKF(sz.cy == tm.tmHeight, "ctx %s %s %dpt: extent cy %ld tmHeight %ld", ctx, TT_FACES[f], pts[p], sz.cy, tm.tmHeight);
        INT w[95]; long sum = 0;
        if (GetCharWidth32W(dc, 0x20, 0x7e, w)) {
            for (int i = 0; i < n; i++) sum += w[(unsigned char)STR[i] - 0x20];
            long diff = labs(sz.cx - (sum - (long)n * tm.tmOverhang + tm.tmOverhang));
            CHECKF(2 * diff <= n + 1, "ctx %s %s %dpt: extent %ld vs sum of widths %ld (n %d)", ctx, TT_FACES[f], pts[p], sz.cx, sum, n);
            if (diff) t_note("ctx %s %s %dpt: extent %ld != sum of widths %ld (fractional advances)", ctx, TT_FACES[f], pts[p], sz.cx, sum);
            ABC abc[95];
            if (tt && GetCharABCWidthsW(dc, 0x20, 0x7e, abc)) {
                int bad = 0; for (int i = 0; i < 95; i++) if (abc[i].abcA + (int)abc[i].abcB + abc[i].abcC != w[i] - tm.tmOverhang) bad++;
                CHECKF(bad == 0, "ctx %s %s %dpt: %d chars with A+B+C != CharWidth32", ctx, TT_FACES[f], pts[p], bad);
            }
        } else CHECKF(0, "GetCharWidth32W %s", TT_FACES[f]);
        SIZE sa;
        if (GetTextExtentPoint32A(dc, STR, n, &sa) && (sa.cx != sz.cx || sa.cy != sz.cy))
            t_note("ctx %s %s %dpt: A extent %ldx%ld W extent %ldx%ld", ctx, TT_FACES[f], pts[p], sa.cx, sa.cy, sz.cx, sz.cy);
        CHECK(SelectObject(dc, old) == h);                                                   /* SelectObject round trip */
        DeleteObject(h);
    }
    ReleaseDC(NULL, dc);
}
TEST(font_invariants_every_ctx) { each_ctx(font_invariants); }

TEST(getobject_font_size_protocol) {
    HFONT h = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
    REQUIRE(h);
    EXPECT_EQ(GetObjectW(h, 0, NULL), 0x5c);                                                 /* [GOBJ] */
    EXPECT_EQ(GetObjectW(h, 1000, NULL), 0x5c);
    LOGFONTW lf; EXPECT_EQ(GetObjectW(h, sizeof lf, &lf), sizeof lf);
    EXPECT_EQ(lf.lfHeight, -12);
    HDC dc = GetDC(NULL);
    BYTE buf[sizeof(LOGFONTW)];
    SET_ERR(0); EXPECT_EQ(GetObjectW(dc, sizeof buf, buf), 0); EXPECT_ERR(ERROR_INVALID_HANDLE);   /* DC-type handle */
    ReleaseDC(NULL, dc); DeleteObject(h);
    /* the stock DEFAULT_GUI_FONT rule (docs/research/02 section 10): observed only - a registry GUIFont value can override it */
    LOGFONTW g; HDC d2 = GetDC(NULL); int y = GetDeviceCaps(d2, LOGPIXELSY); ReleaseDC(NULL, d2);
    if (GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof g, &g) && g.lfHeight != -((8 * y + 36) / 72))
        t_note("DEFAULT_GUI_FONT lfHeight %ld, rule -((8*%d+36)/72) = %d", g.lfHeight, y, -((8 * y + 36) / 72));
}

/* ---------------------------------------------------------------- dialog font: template kind x DS_SHELLFONT
 * [DLGF] user32!InternalCreateDialog @0x6100 -> user32!CreateDlgFont @0x3ca4: lfHeight = -((pt*dpi+36)/72) with dpi =
 *        GetDpiForSystem() in the creating thread (or the monitor DPI for a per-monitor thread; one monitor here, so it
 *        equals GetDpiForWindow of the dialog); "MS Shell Dlg" becomes "MS Shell Dlg 2" only when the template is a
 *        DLGTEMPLATEEX (signature 0xFFFF) and style & (DS_SETFONT|DS_FIXEDSYS) == both (DS_SHELLFONT);
 *        user32!FixupDlgFaceName @0x3f58 otherwise copies the template face. user32!FixupDlgLogFont @0x4000: a DLGTEMPLATEEX
 *        (dlgVer 1) keeps the template weight. */
static INT_PTR CALLBACK t_dlgproc(HWND h, UINT m, WPARAM w, LPARAM l) { (void)h; (void)w; (void)l; return m == WM_INITDIALOG; }
static HWND t_dialog(int ex, DWORD extra, int pt, const WCHAR *face) {
    static DWORD buf[128]; memset(buf, 0, sizeof buf);
    DWORD st = WS_POPUP | WS_CAPTION | DS_SETFONT | extra; WORD *p = (WORD *)buf;
    if (!ex) { DLGTEMPLATE *t = (DLGTEMPLATE *)buf; t->style = st; t->cx = 100; t->cy = 50; p = (WORD *)(t + 1); }
    else { *p++ = 1; *p++ = 0xFFFF; p += 4; *p++ = LOWORD(st); *p++ = HIWORD(st); p++; p += 2; *p++ = 100; *p++ = 50; }
    *p++ = 0; *p++ = 0; *p++ = 0;
    *p++ = (WORD)pt; if (ex) { *p++ = FW_NORMAL; *p++ = 0x0100; }
    while (*face) *p++ = *face++;
    *p++ = 0;
    return U(CreateDialogIndirectParamW)(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)buf, NULL, t_dlgproc, 0);
}
static int g_dlg_made;
static void dlgfont_ctx(const char *ctx) {
    static const struct { int ex; DWORD extra; int pt; const WCHAR *face, *want; } D[] = {
        { 1, DS_FIXEDSYS, 8, L"MS Shell Dlg", L"MS Shell Dlg 2" },      /* DIALOGEX + DS_SHELLFONT */
        { 1, 0,           8, L"MS Shell Dlg", L"MS Shell Dlg" },        /* DIALOGEX, DS_SETFONT only */
        { 0, DS_FIXEDSYS, 8, L"MS Shell Dlg", L"MS Shell Dlg" },        /* DIALOG: no shell-font mapping */
        { 1, DS_FIXEDSYS, 9, L"Segoe UI",     L"Segoe UI" },
        { 1, 0,           8, L"Tahoma",       L"Tahoma" } };
    for (unsigned i = 0; i < sizeof D / sizeof *D; i++) {
        HWND h = t_dialog(D[i].ex, D[i].extra, D[i].pt, D[i].face);
        if (!h) { t_note("ctx %s: dialog %u not created (gle %lu)", ctx, i, GetLastError()); continue; }
        g_dlg_made++;
        HFONT f = (HFONT)U(SendMessageW)(h, WM_GETFONT, 0, 0); LOGFONTW lf;
        UINT wdpi = U(GetDpiForWindow) ? U(GetDpiForWindow)(h) : 96;
        int okf = f && GetObjectW(f, sizeof lf, &lf) == sizeof lf;
        CHECKF(okf, "ctx %s dialog %u font", ctx, i);
        if (okf) {
            CHECKF(!lstrcmpW(lf.lfFaceName, D[i].want), "ctx %s dialog %u: face %ls want %ls", ctx, i, lf.lfFaceName, D[i].want);
            int want = -((D[i].pt * (int)wdpi + 36) / 72);
            CHECKF(lf.lfHeight == want, "ctx %s dialog %u: lfHeight %ld want -((%d*%u+36)/72) = %d", ctx, i, lf.lfHeight, D[i].pt, wdpi, want);
            if (D[i].ex) CHECKF(lf.lfWeight == FW_NORMAL, "ctx %s dialog %u: weight %ld", ctx, i, lf.lfWeight);
        }
        U(DestroyWindow)(h);
    }
}
TEST(dlgfont_template_kind_and_dpi_rule) {
    REQUIRE(U(CreateDialogIndirectParamW));
    g_dlg_made = 0; each_ctx(dlgfont_ctx);
    if (!g_dlg_made) t_skip("no dialog could be created (no usable desktop)");
}
