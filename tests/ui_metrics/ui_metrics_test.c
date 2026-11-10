#include "t.h"
/* ui_metrics conformance: invariants of the layout metric APIs that hold on ANY Windows (no recorded Win10 numbers;
 * those come from probes/ui_metrics through the golden generator). Every hard CHECK below is backed by a reading of
 * the Win10 22H2 (19041.2965) decompilation in the corpus explorer; the function named in each comment is the one
 * that was read. Anything not verified there is t_note()d, never checked. */

#define DECOMP(...) /* documentation only: what was verified in the decompilation */

/* ---------------------------------------------------------------- helpers */
static HFONT make_font(const WCHAR *face, int h){
  LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
  return U(CreateFontIndirectW)(&lf);
}

/* in-memory DLGTEMPLATE, no controls; font only when face != NULL (DS_SETFONT) */
static HWND make_dialog(const WCHAR *face, WORD pt, DWORD extra_style){
  static DWORD buf[256]; BYTE *p = (BYTE *)buf; int n = 0;
  #define PW(w) do { WORD w_ = (WORD)(w); memcpy(p + n, &w_, 2); n += 2; } while (0)
  #define PD(d) do { DWORD d_ = (DWORD)(d); memcpy(p + n, &d_, 4); n += 4; } while (0)
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | extra_style | (face ? DS_SETFONT : 0);
  PD(style); PD(0); PW(0); PW(0); PW(0); PW(200); PW(100); PW(0); PW(0); PW(0);   /* no menu, class, empty title */
  if (face) { PW(pt); const WCHAR *s = face; do PW(*s); while (*s++); }
  #undef PW
  #undef PD
  return U(CreateDialogIndirectParamW)(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)buf, NULL, NULL, 0);
}

/* ---------------------------------------------------------------- TEXTMETRIC */
TEST(tm_descent_is_height_minus_ascent){
  DECOMP(win32kfull!vIFIMetricsToTextMetricWStrict: tmDescent = tmHeight - tmAscent; vGetTextMetrics copies the cached result)
  static const WCHAR *faces[] = {L"Segoe UI", L"MS Shell Dlg", L"MS Shell Dlg 2", L"Tahoma", L"Arial", L"Courier New", L"Courier",
                                 L"System", L"Fixedsys", L"Terminal", L"Microsoft Sans Serif", L"NoSuchFaceXyz"};
  static const int hs[] = {-8, -11, -12, -13, -15, -16, -18, -24, 0, 12, 16, 20};
  HDC dc = U(GetDC)(NULL); CHECK(dc != NULL); if (!dc) return;
  int n = 0;
  for (unsigned i = 0; i < sizeof faces / sizeof *faces; i++) for (unsigned j = 0; j < sizeof hs / sizeof *hs; j++) {
    HFONT f = make_font(faces[i], hs[j]); HGDIOBJ o = U(SelectObject)(dc, f);
    TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
    if (U(GetTextMetricsW)(dc, &tm)) { n++; CHECKF(tm.tmDescent == tm.tmHeight - tm.tmAscent, "face %d h %d: h %ld a %ld d %ld", i, hs[j], tm.tmHeight, tm.tmAscent, tm.tmDescent); }
    U(SelectObject)(dc, o); U(DeleteObject)(f);
  }
  /* stock fonts and the NONCLIENTMETRICS fonts */
  for (int s = OEM_FIXED_FONT; s <= DEFAULT_GUI_FONT; s++) {
    if (s == 15) continue;  /* not a font (DEFAULT_PALETTE) */
    HGDIOBJ o = U(SelectObject)(dc, U(GetStockObject)(s)); TEXTMETRICW tm;
    if (U(GetTextMetricsW)(dc, &tm)) { n++; CHECKF(tm.tmDescent == tm.tmHeight - tm.tmAscent, "stock %d", s); }
    U(SelectObject)(dc, o);
  }
  NONCLIENTMETRICSW ncm; memset(&ncm, 0, sizeof ncm); ncm.cbSize = sizeof ncm;
  if (U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) {
    LOGFONTW *l[] = {&ncm.lfCaptionFont, &ncm.lfSmCaptionFont, &ncm.lfMenuFont, &ncm.lfStatusFont, &ncm.lfMessageFont};
    for (int i = 0; i < 5; i++) {
      HFONT f = U(CreateFontIndirectW)(l[i]); HGDIOBJ o = U(SelectObject)(dc, f); TEXTMETRICW tm;
      if (U(GetTextMetricsW)(dc, &tm)) { n++; CHECKF(tm.tmDescent == tm.tmHeight - tm.tmAscent, "ncm font %d", i); }
      U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
  }
  CHECK(n > 100);
  U(ReleaseDC)(NULL, dc);
}

TEST(tm_extent_vs_char_widths_observed){
  /* NOT verified in the decomp: extent of a string = sum of GetCharWidth32 for these faces (no kerning, MM_TEXT).
   * Reported as a note only. */
  HDC dc = U(GetDC)(NULL); if (!dc) return;
  static const WCHAR *faces[] = {L"Segoe UI", L"MS Shell Dlg", L"Tahoma", L"Arial", L"Courier New"};
  static const WCHAR s[] = L"The quick brown fox jumps";
  for (unsigned i = 0; i < 5; i++) for (int h = -11; h >= -13; h--) {
    HFONT f = make_font(faces[i], h); HGDIOBJ o = U(SelectObject)(dc, f);
    SIZE sz; INT w; long sum = 0;
    for (const WCHAR *c = s; *c; c++) { U(GetCharWidth32W)(dc, *c, *c, &w); sum += w; }
    if (U(GetTextExtentPoint32W)(dc, s, lstrlenW(s), &sz) && sz.cx != sum) t_note("face %d h %d: extent %ld != sum of widths %ld", i, h, sz.cx, sum);
    U(SelectObject)(dc, o); U(DeleteObject)(f);
  }
  U(ReleaseDC)(NULL, dc);
}

/* ---------------------------------------------------------------- GetObject on fonts */
TEST(getobject_font_buffer_protocol){
  DECOMP(gdi32full!GetObjectW: font handle type 0x0a0000, pv NULL -> 0x5c)
  DECOMP(gdi32full!GetObjectA: font, pv NULL -> 0x3c; c in {0x3c, 0x5c} -> convert LOGFONTW to LOGFONTA, return 0x3c)
  HGDIOBJ fs[] = {U(GetStockObject)(DEFAULT_GUI_FONT), U(GetStockObject)(SYSTEM_FONT), U(GetStockObject)(ANSI_VAR_FONT), make_font(L"Segoe UI", -12)};
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(U(GetObjectW)(fs[i], 0, NULL), sizeof(LOGFONTW));
    EXPECT_EQ(U(GetObjectA)(fs[i], 0, NULL), sizeof(LOGFONTA));
    BYTE big[sizeof(LOGFONTW)]; memset(big, 0xCC, sizeof big);
    EXPECT_EQ(U(GetObjectA)(fs[i], sizeof(LOGFONTW), big), sizeof(LOGFONTA));
    CHECK(big[sizeof(LOGFONTA)] == 0xCC);   /* only 60 bytes written */
    LOGFONTA la; EXPECT_EQ(U(GetObjectA)(fs[i], sizeof la, &la), sizeof(LOGFONTA));
    LOGFONTW lw; EXPECT_EQ(U(GetObjectW)(fs[i], sizeof lw, &lw), sizeof(LOGFONTW));
    /* A/W parity of the numeric fields */
    CHECK(la.lfHeight == lw.lfHeight && la.lfWidth == lw.lfWidth && la.lfWeight == lw.lfWeight && la.lfItalic == lw.lfItalic &&
          la.lfCharSet == lw.lfCharSet && la.lfPitchAndFamily == lw.lfPitchAndFamily && la.lfQuality == lw.lfQuality);
    char fw[LF_FACESIZE * 2]; WideCharToMultiByte(CP_ACP, 0, lw.lfFaceName, -1, fw, sizeof fw, NULL, NULL);
    if (strcmp(fw, la.lfFaceName)) t_note("font %d: A face \"%s\" vs W face \"%s\"", i, la.lfFaceName, fw);
  }
  U(DeleteObject)(fs[3]);
}

TEST(stock_font_identity){
  DECOMP(gdi32full!GetStockObject: (uint)i > 0x15 -> NULL; otherwise a read from the GDI shared-memory stock table. For
         OEM_FIXED_FONT / SYSTEM_FONT / SYSTEM_FIXED_FONT the slot depends on the thread DPI awareness and for DEFAULT_GUI_FONT
         on the DPI of the thread, so the handle is stable only within one thread DPI context)
  static const int ids[] = {OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT};
  for (int i = 0; i < 7; i++) {
    HGDIOBJ a = U(GetStockObject)(ids[i]), b = U(GetStockObject)(ids[i]);
    CHECKF(a != NULL && a == b, "stock %d", ids[i]);
    DWORD t = U(GetObjectType)(a); if (t != OBJ_FONT) t_note("stock %d: GetObjectType %lu", ids[i], t);
  }
  EXPECT_EQ(U(GetStockObject)(0x16), NULL);
  EXPECT_EQ(U(GetStockObject)(-1), NULL);
  EXPECT_EQ(U(GetStockObject)(1000), NULL);
}

/* ---------------------------------------------------------------- dialogs */
TEST(mapdialogrect_rounding){
  DECOMP(user32!MapDialogRect: x2 = (cxBase*x + 2) / 4, y2 = (cyBase*y + 4) / 8, ints; FALSE when not a dialog)
  static const struct { const WCHAR *face; WORD pt; } F[] = {{L"MS Shell Dlg", 8}, {L"Segoe UI", 9}, {L"Tahoma", 8}, {L"Courier New", 10}, {NULL, 0}};
  for (unsigned i = 0; i < sizeof F / sizeof *F; i++) {
    HWND d = make_dialog(F[i].face, F[i].pt, 0);
    CHECKF(d != NULL, "dialog %u", i); if (!d) continue;
    RECT b = {0, 0, 4, 8}; CHECK(U(MapDialogRect)(d, &b));
    int bx = b.right, by = b.bottom; CHECK(bx > 0 && by > 0);
    CHECK(b.left == 0 && b.top == 0);
    static const int v[] = {0, 1, 2, 3, 5, 7, 13, 14, 50, 99, 100, 101, 255, 1000, -1, -2, -3, -7, -14, -100};
    for (unsigned j = 0; j < sizeof v / sizeof *v; j++) {
      RECT r = {v[j], v[j], v[j], v[j]}; CHECK(U(MapDialogRect)(d, &r));
      int ex = (bx * v[j] + 2) / 4, ey = (by * v[j] + 4) / 8;
      CHECKF(r.left == ex && r.right == ex && r.top == ey && r.bottom == ey, "font %u v %d base %dx%d got %ld,%ld want %d,%d", i, v[j], bx, by, r.left, r.top, ex, ey);
    }
    U(DestroyWindow)(d);
  }
}

TEST(mapdialogrect_not_a_dialog){
  DECOMP(user32!MapDialogRect: ValidateHwnd + ValidateDialogPwnd, else return 0 and the rect is untouched)
  HWND w = U(CreateWindowExW)(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
  CHECK(w != NULL);
  RECT r = {1, 2, 3, 4};
  CHECK(!U(MapDialogRect)(w, &r)); CHECK(r.left == 1 && r.top == 2 && r.right == 3 && r.bottom == 4);
  CHECK(!U(MapDialogRect)(NULL, &r)); CHECK(r.left == 1 && r.bottom == 4);
  if (w) U(DestroyWindow)(w);
}

TEST(dialog_base_units_stable){
  DECOMP(user32!GetDialogBaseUnits: reads two WORDs from the per-DPI server info; no side effects)
  LONG a = U(GetDialogBaseUnits)(), b = U(GetDialogBaseUnits)();
  EXPECT_EQ(a, b);
  t_note("GetDialogBaseUnits = %d x %d", LOWORD(a), HIWORD(a));
}

/* ---------------------------------------------------------------- AdjustWindowRectEx(ForDpi) */
static const DWORD STY[] = {WS_OVERLAPPEDWINDOW, WS_CAPTION, WS_CAPTION | WS_SYSMENU, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
  WS_POPUP, WS_POPUP | WS_BORDER, WS_POPUP | WS_DLGFRAME, WS_POPUP | WS_THICKFRAME, WS_CAPTION | WS_THICKFRAME, WS_CHILD,
  WS_CHILD | WS_BORDER, WS_CHILD | WS_THICKFRAME, WS_CHILD | WS_CAPTION, WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL};
static const DWORD EXS[] = {0, WS_EX_WINDOWEDGE, WS_EX_CLIENTEDGE, WS_EX_STATICEDGE, WS_EX_DLGMODALFRAME, WS_EX_TOOLWINDOW,
  WS_EX_OVERLAPPEDWINDOW, WS_EX_PALETTEWINDOW, WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE};
#define NS (sizeof STY / sizeof *STY)
#define NE (sizeof EXS / sizeof *EXS)

/* top = menu (SM_CYMENU) + caption (SM_CYCAPTION, or SM_CYSMCAPTION with WS_EX_TOOLWINDOW) + border; the border is
 * the same on all four sides */
static void check_decomposition(RECT r, DWORD s, DWORD x, int menu, UINT dpi, const char *what){
  int dl = -r.left, dt = -r.top, dr = r.right - 100, db = r.bottom - 100;
  CHECKF(dl == dr && dl == db, "%s style 0x%lx ex 0x%lx menu %d dpi %u: l %d r %d b %d", what, s, x, menu, dpi, dl, dr, db);
  int want = dl;
  if (menu) want += U(GetSystemMetricsForDpi)(SM_CYMENU, dpi);
  if ((s & WS_CAPTION) == WS_CAPTION) want += U(GetSystemMetricsForDpi)((x & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION, dpi);
  CHECKF(dt == want, "%s style 0x%lx ex 0x%lx menu %d dpi %u: top %d want %d", what, s, x, menu, dpi, dt, want);
}

TEST(adjustwindowrectexfordpi_decomposition){
  DECOMP(user32!AdjustWindowRectExForDpi: top -= GetSystemMetricsForDpi(SM_CYMENU) if bMenu; top -= SM_CYCAPTION or SM_CYSMCAPTION
         (WS_EX_TOOLWINDOW) if WS_CAPTION; then the same border n is subtracted/added on all four sides; WS_EX_CLIENTEDGE adds 2)
  REQUIRE(U(AdjustWindowRectExForDpi)); REQUIRE(U(GetSystemMetricsForDpi));
  static const UINT D[] = {96, 120, 144, 192};
  for (unsigned di = 0; di < 4; di++) for (unsigned i = 0; i < NS; i++) for (unsigned j = 0; j < NE; j++) for (int m = 0; m < 2; m++) {
    RECT r = {0, 0, 100, 100};
    CHECK(U(AdjustWindowRectExForDpi)(&r, STY[i], m, EXS[j], D[di]));
    check_decomposition(r, STY[i], EXS[j], m, D[di], "ForDpi");
    if (!(EXS[j] & WS_EX_CLIENTEDGE)) {
      RECT r2 = {0, 0, 100, 100};
      CHECK(U(AdjustWindowRectExForDpi)(&r2, STY[i], m, EXS[j] | WS_EX_CLIENTEDGE, D[di]));
      CHECKF(r2.left == r.left - 2 && r2.right == r.right + 2 && r2.bottom == r.bottom + 2 && r2.top == r.top - 2,
             "clientedge +2: style 0x%lx ex 0x%lx dpi %u", STY[i], EXS[j], D[di]);
    }
  }
}

TEST(adjustwindowrectexfordpi_borderless_unchanged){
  DECOMP(user32!AdjustWindowRectExForDpi: no caption/border/thickframe bits, no edge ex bits, no menu -> nothing added)
  REQUIRE(U(AdjustWindowRectExForDpi));
  static const DWORD s[] = {WS_CHILD, WS_POPUP, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, WS_POPUP | WS_VSCROLL | WS_HSCROLL};
  for (int i = 0; i < 4; i++) for (UINT d = 96; d <= 192; d += 48) {
    RECT r = {10, 20, 110, 220}; CHECK(U(AdjustWindowRectExForDpi)(&r, s[i], FALSE, 0, d));
    CHECKF(r.left == 10 && r.top == 20 && r.right == 110 && r.bottom == 220, "style 0x%lx dpi %u", s[i], d);
  }
}

TEST(adjustwindowrectexfordpi_dpi0){
  DECOMP(user32!AdjustWindowRectExForDpi: dpi == 0 -> SetLastError(ERROR_INVALID_PARAMETER), return FALSE)
  REQUIRE(U(AdjustWindowRectExForDpi));
  RECT r = {0, 0, 100, 100}; SET_ERR(0);
  EXPECT_FAIL(!U(AdjustWindowRectExForDpi)(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, 0), ERROR_INVALID_PARAMETER);
  CHECK(r.left == 0 && r.top == 0 && r.right == 100 && r.bottom == 100);
}

TEST(adjustwindowrectex_uses_system_dpi){
  DECOMP(user32!RealAdjustWindowRectEx -> _AdjustWindowRectExForDpi(..., GetDpiForSystem()): menu/caption via GetSystemMetricsForDpi,
         then InflateRect(n, n))
  REQUIRE(U(GetDpiForSystem)); REQUIRE(U(GetSystemMetricsForDpi));
  UINT d = U(GetDpiForSystem)();
  for (unsigned i = 0; i < NS; i++) for (unsigned j = 0; j < NE; j++) for (int m = 0; m < 2; m++) {
    RECT r = {0, 0, 100, 100};
    CHECK(U(AdjustWindowRectEx)(&r, STY[i], m, EXS[j]));
    check_decomposition(r, STY[i], EXS[j], m, d, "AdjustWindowRectEx");
  }
}

/* ---------------------------------------------------------------- GetSystemMetricsForDpi */
TEST(getsystemmetricsfordpi_bad_params){
  DECOMP(user32!RealGetSystemMetricsForDpi: (int)dpi <= 0 or index < 0 -> SetLastError(ERROR_INVALID_PARAMETER), return 0)
  REQUIRE(U(GetSystemMetricsForDpi));
  SET_ERR(0); EXPECT_EQ(U(GetSystemMetricsForDpi)(SM_CYCAPTION, 0), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  SET_ERR(0); EXPECT_EQ(U(GetSystemMetricsForDpi)(-1, 96), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  SET_ERR(0); EXPECT_EQ(U(GetSystemMetricsForDpi)(SM_CXICON, (UINT)-96), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(getsystemmetricsfordpi_dpi_independent_indices){
  DECOMP(user32!RealGetSystemMetricsForDpi: indices outside the DPI-scaled set {2,3,4,9..15,20,21,28..33,34,35,38,39,49..55,71,72,92}
         are answered by RealGetSystemMetrics(index), whatever the dpi)
  REQUIRE(U(GetSystemMetricsForDpi));
  static const int idx[] = {SM_CXSCREEN, SM_CYSCREEN, SM_MOUSEPRESENT, SM_SWAPBUTTON, SM_CMOUSEBUTTONS, SM_CMONITORS, SM_DBCSENABLED,
                            SM_IMMENABLED, SM_MIDEASTENABLED, SM_SAMEDISPLAYFORMAT};
  static const UINT d[] = {72, 96, 120, 144, 192, 480};
  for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
    int g = U(GetSystemMetrics)(idx[i]);
    for (unsigned j = 0; j < sizeof d / sizeof *d; j++) CHECKF(U(GetSystemMetricsForDpi)(idx[i], d[j]) == g, "index %d dpi %u", idx[i], d[j]);
  }
}

TEST(getsystemmetrics_scaled_observed){
  /* NOT verified: how the scaled metrics grow with dpi. Monotonic growth is only noted. */
  REQUIRE(U(GetSystemMetricsForDpi));
  static const int idx[] = {SM_CYCAPTION, SM_CYMENU, SM_CXVSCROLL, SM_CXICON, SM_CXSMICON, SM_CXFRAME, SM_CXPADDEDBORDER, SM_CYSMCAPTION};
  for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
    int a = U(GetSystemMetricsForDpi)(idx[i], 96), b = U(GetSystemMetricsForDpi)(idx[i], 144), c = U(GetSystemMetricsForDpi)(idx[i], 192);
    if (!(a <= b && b <= c)) t_note("index %d: 96 %d 144 %d 192 %d not monotonic", idx[i], a, b, c);
  }
}

/* ---------------------------------------------------------------- SystemParametersInfo(ForDpi) buffer protocols */
TEST(spifordpi_protocol){
  DECOMP(win32kfull!NtUserSystemParametersInfoForDpi: dpi == 0, pv == NULL, an action other than SPI_GETNONCLIENTMETRICS /
         SPI_GETICONTITLELOGFONT / SPI_GETICONMETRICS, or uiParam != 504 / 92 / 108 -> ERROR_INVALID_PARAMETER, FALSE)
  DECOMP(win32kfull!_SystemParametersInfoForDpi: NONCLIENTMETRICS cbSize must be 0x1f8, ICONMETRICS cbSize 0x6c, else ERROR_INVALID_PARAMETER)
  REQUIRE(U(SystemParametersInfoForDpi));
  NONCLIENTMETRICSW n; ICONMETRICSW im; LOGFONTW lf; RECT wa;
  memset(&n, 0, sizeof n); n.cbSize = sizeof n;
  SET_ERR(0); CHECK(U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 96));
  EXPECT_EQ(n.cbSize, sizeof n);
  CHECK(n.lfMessageFont.lfFaceName[0] != 0);
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 0), ERROR_INVALID_PARAMETER);
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof n, NULL, 0, 96), ERROR_INVALID_PARAMETER);
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, 500, &n, 0, 96), ERROR_INVALID_PARAMETER);
  n.cbSize = 500;
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 96), ERROR_INVALID_PARAMETER);
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETWORKAREA, sizeof wa, &wa, 0, 96), ERROR_INVALID_PARAMETER);
  memset(&lf, 0, sizeof lf);
  SET_ERR(0); CHECK(U(SystemParametersInfoForDpi)(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0, 96));
  CHECK(lf.lfFaceName[0] != 0 && lf.lfHeight != 0);
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETICONTITLELOGFONT, sizeof(LOGFONTA), &lf, 0, 96), ERROR_INVALID_PARAMETER);
  memset(&im, 0, sizeof im); im.cbSize = sizeof im;
  SET_ERR(0); CHECK(U(SystemParametersInfoForDpi)(SPI_GETICONMETRICS, sizeof im, &im, 0, 96));
  EXPECT_EQ(im.cbSize, sizeof im);
  im.cbSize = sizeof im - 4;
  SET_ERR(0); EXPECT_FAIL(!U(SystemParametersInfoForDpi)(SPI_GETICONMETRICS, sizeof im, &im, 0, 96), ERROR_INVALID_PARAMETER);
}

TEST(spi_ncm_size_protocol){
  DECOMP(user32!RealSystemParametersInfoW SPI_GETNONCLIENTMETRICS: pv NULL -> FALSE; cbSize must be 500 or 504; uiParam is replaced by cbSize)
  DECOMP(user32!RealSystemParametersInfoA SPI_GETNONCLIENTMETRICS: cbSize must be 340 or 344, then the W call is made with 504)
  NONCLIENTMETRICSW n; NONCLIENTMETRICSA a;
  memset(&n, 0, sizeof n); n.cbSize = sizeof n; CHECK(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0));
  NONCLIENTMETRICSW n0; memset(&n0, 0, sizeof n0); n0.cbSize = sizeof n0; CHECK(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, 0, &n0, 0));
  CHECK(!memcmp(&n, &n0, sizeof n));
  NONCLIENTMETRICSW n5; memset(&n5, 0, sizeof n5); n5.cbSize = 500; CHECK(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, 500, &n5, 0));
  CHECK(n5.iCaptionHeight == n.iCaptionHeight && n5.iMenuHeight == n.iMenuHeight && n5.iScrollWidth == n.iScrollWidth);
  CHECK(n5.lfMessageFont.lfHeight == n.lfMessageFont.lfHeight);
  memset(&n5, 0, sizeof n5); n5.cbSize = 340; CHECK(!U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, 340, &n5, 0));
  CHECK(!U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof n, NULL, 0));
  /* A/W parity */
  memset(&a, 0, sizeof a); a.cbSize = sizeof a; CHECK(U(SystemParametersInfoA)(SPI_GETNONCLIENTMETRICS, sizeof a, &a, 0));
  CHECK(a.iBorderWidth == n.iBorderWidth && a.iScrollWidth == n.iScrollWidth && a.iScrollHeight == n.iScrollHeight &&
        a.iCaptionWidth == n.iCaptionWidth && a.iCaptionHeight == n.iCaptionHeight && a.iSmCaptionWidth == n.iSmCaptionWidth &&
        a.iSmCaptionHeight == n.iSmCaptionHeight && a.iMenuWidth == n.iMenuWidth && a.iMenuHeight == n.iMenuHeight);
  CHECK(a.lfMessageFont.lfHeight == n.lfMessageFont.lfHeight && a.lfMessageFont.lfWeight == n.lfMessageFont.lfWeight &&
        a.lfCaptionFont.lfHeight == n.lfCaptionFont.lfHeight && a.lfMenuFont.lfHeight == n.lfMenuFont.lfHeight);
  char fw[64]; WideCharToMultiByte(CP_ACP, 0, n.lfMessageFont.lfFaceName, -1, fw, sizeof fw, NULL, NULL);
  if (strcmp(fw, a.lfMessageFont.lfFaceName)) t_note("A message face \"%s\" vs W \"%s\"", a.lfMessageFont.lfFaceName, fw);
  NONCLIENTMETRICSA a4; memset(&a4, 0, sizeof a4); a4.cbSize = 340; CHECK(U(SystemParametersInfoA)(SPI_GETNONCLIENTMETRICS, 340, &a4, 0));
  CHECK(a4.iCaptionHeight == a.iCaptionHeight);
  memset(&a4, 0, sizeof a4); a4.cbSize = 500; CHECK(!U(SystemParametersInfoA)(SPI_GETNONCLIENTMETRICS, 500, &a4, 0));
}

TEST(spi_iconmetrics_size_protocol){
  DECOMP(user32!RealSystemParametersInfoW SPI_GETICONMETRICS (0x2d): pv NULL -> FALSE; cbSize must be 0x6c)
  ICONMETRICSW im; memset(&im, 0, sizeof im); im.cbSize = sizeof im;
  CHECK(U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, &im, 0));
  CHECK(im.lfFont.lfFaceName[0] != 0);
  im.cbSize = sizeof im - 4; CHECK(!U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, &im, 0));
  CHECK(!U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, NULL, 0));
}

TEST(spifordpi_matches_spi_at_system_dpi_observed){
  /* NOT verified: SPI_GETNONCLIENTMETRICS for GetDpiForSystem() == SystemParametersInfoW in a system-aware process.
   * Only a note: it depends on the process awareness (an unaware process at 144 DPI sees virtualised 96 values). */
  if (!U(SystemParametersInfoForDpi) || !U(GetDpiForSystem)) return;
  NONCLIENTMETRICSW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
  if (U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof a, &a, 0) && U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof b, &b, 0, U(GetDpiForSystem)()))
    if (a.iCaptionHeight != b.iCaptionHeight || a.lfMessageFont.lfHeight != b.lfMessageFont.lfHeight)
      t_note("SPI vs ForDpi(%u): caption %d/%d message lfHeight %ld/%ld", U(GetDpiForSystem)(), a.iCaptionHeight, b.iCaptionHeight,
             a.lfMessageFont.lfHeight, b.lfMessageFont.lfHeight);
}
