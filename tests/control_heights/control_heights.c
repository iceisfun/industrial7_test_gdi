#include "t.h"
#include <commctrl.h>
/* control_heights conformance: invariants of the text-metric and control-geometry calls that the
 * probes/control_heights recorder measures. Every hard CHECK below is backed by the Win10 22H2 decompile
 * (corpus API, build 19041.2965); the source function is named next to each group. Win10-specific numbers
 * (row heights, header heights per font and DPI) are NOT checked here: the golden generator adds those from
 * the recorded data (data/win10/control_heights/).
 *
 * comctl32 checks run in an activation context created from the v6 manifest (the exe itself has no manifest),
 * against the comctl32 that LoadLibrary returns inside it; the decompile is of comctl32 v6. */

/* ---------------------------------------------------------------- helpers */
#define SND(h, m, w, l) U(SendMessageW)((h), (m), (WPARAM)(w), (LPARAM)(l))
/* EXPECT_EQ (t.h) evaluates its arguments twice, which repeats a SendMessage with an already-overwritten in/out
 * buffer; EQ evaluates each side exactly once. */
#define EQ(a, b) do { long long a_ = (long long)(a), b_ = (long long)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got %lld want %lld", a_, b_); } while (0)

static HFONT mkfont(const WCHAR *face, int h){
  LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
  return U(CreateFontIndirectW)(&lf);
}

static const char V6_MANIFEST[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
  "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
  "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
  "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
  "</dependentAssembly></dependency></assembly>\r\n";

typedef struct { HANDLE ctx; ULONG_PTR cookie; HMODULE cc; HWND parent; } v6env;
static LRESULT CALLBACK parent_proc(HWND h, UINT m, WPARAM w, LPARAM l){ return U(DefWindowProcW)(h, m, w, l); }

/* activate comctl32 v6 and create a hidden parent window; 0 = skip (reason printed) */
static int v6_begin(v6env *e){
  memset(e, 0, sizeof *e);
  char path[MAX_PATH]; t_tmpfile(path, sizeof path, "v6.manifest");
  HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (f == INVALID_HANDLE_VALUE) { t_skip("cannot write manifest"); return 0; }
  DWORD wr; WriteFile(f, V6_MANIFEST, sizeof V6_MANIFEST - 1, &wr, NULL); CloseHandle(f);
  ACTCTXA ac; memset(&ac, 0, sizeof ac); ac.cbSize = sizeof ac; ac.lpSource = path;
  e->ctx = CreateActCtxA(&ac);
  if (e->ctx == INVALID_HANDLE_VALUE) { t_skip("CreateActCtx failed %lu", GetLastError()); return 0; }
  if (!ActivateActCtx(e->ctx, &e->cookie)) { ReleaseActCtx(e->ctx); t_skip("ActivateActCtx failed"); return 0; }
  e->cc = LoadLibraryW(L"comctl32.dll");
  typedef BOOL (WINAPI *icc_t)(const INITCOMMONCONTROLSEX *);
  icc_t icc = e->cc ? (icc_t)GetProcAddress(e->cc, "InitCommonControlsEx") : NULL;
  if (!icc) { t_skip("comctl32 v6 not loadable"); return 0; }
  INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES};
  icc(&ic);
  WNDCLASSW wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = parent_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"CHTestParent";
  U(RegisterClassW)(&wc);
  e->parent = U(CreateWindowExW)(0, L"CHTestParent", L"", WS_OVERLAPPEDWINDOW, 0, 0, 500, 400, NULL, NULL, GetModuleHandleW(NULL), NULL);
  if (!e->parent) { t_skip("parent window creation failed %lu (no display driver?)", GetLastError()); return 0; }
  return 1;
}
static void v6_end(v6env *e){
  if (e->parent) U(DestroyWindow)(e->parent);
  if (e->cookie) DeactivateActCtx(0, e->cookie);
  if (e->ctx && e->ctx != INVALID_HANDLE_VALUE) ReleaseActCtx(e->ctx);
}
static HWND child(v6env *e, const WCHAR *cls, DWORD style, int w, int h){
  return U(CreateWindowExW)(0, cls, NULL, WS_CHILD | WS_VISIBLE | style, 0, 0, w, h, e->parent, (HMENU)1, GetModuleHandleW(NULL), NULL);
}

/* ---------------------------------------------------------------- GetObjectW on fonts
 * gdi32full!GetObjectW: object type 0x0a (font) with pv == NULL returns 0x5c = sizeof(LOGFONTW) before entering the
 * kernel. win32kfull!NtGdiExtGetObjectW / GreExtGetObjectW: a font copies min(stored size, c) bytes and returns that
 * count. Type 0x01 (DC): GdiSetLastError(6) (= TEB LastErrorValue) and return 0. */
TEST(getobject_font_null_buffer_returns_logfontw_size){
  HFONT f = mkfont(L"Tahoma", -11);
  EQ(U(GetObjectW)(f, 0, NULL), sizeof(LOGFONTW));
  EQ(U(GetObjectW)(f, 1000, NULL), sizeof(LOGFONTW));
  EQ(U(GetObjectW)(U(GetStockObject)(DEFAULT_GUI_FONT), 0, NULL), sizeof(LOGFONTW));
  EQ(U(GetObjectW)(U(GetStockObject)(SYSTEM_FONT), 0, NULL), sizeof(LOGFONTW));
  U(DeleteObject)(f);
}
TEST(getobject_font_roundtrip){
  static const struct { const WCHAR *face; int h; } F[] = {
    {L"Segoe UI", -12}, {L"MS Shell Dlg", -11}, {L"MS Shell Dlg 2", -11}, {L"Tahoma", -11}, {L"Microsoft Sans Serif", -11},
    {L"Arial", 13}, {L"Courier", -13}, {L"NoSuchFace_xyz", -12} };
  for (unsigned i = 0; i < sizeof F / sizeof *F; i++) {
    HFONT f = mkfont(F[i].face, F[i].h);
    LOGFONTW lf; memset(&lf, 0x55, sizeof lf);
    EQ(U(GetObjectW)(f, sizeof lf, &lf), sizeof(LOGFONTW));
    EQ(lf.lfHeight, F[i].h);
    EQ(lf.lfWeight, FW_NORMAL);
    EXPECT_WSTR(lf.lfFaceName, F[i].face);   /* the LOGFONT is stored as given: no substitution in GetObject */
    U(DeleteObject)(f);
  }
}
TEST(getobject_font_partial_buffer_copies_c_bytes){
  HFONT f = mkfont(L"Segoe UI", -12);
  unsigned char b[sizeof(LOGFONTW)]; memset(b, 0xAB, sizeof b);
  EQ(U(GetObjectW)(f, 8, b), 8);
  LONG h; memcpy(&h, b, 4); EQ(h, -12);
  int untouched = 1; for (unsigned i = 8; i < sizeof b; i++) if (b[i] != 0xAB) untouched = 0;
  CHECKF(untouched, "bytes past c were written");
  U(DeleteObject)(f);
}
TEST(getobject_on_dc_fails_invalid_handle){
  HDC dc = U(CreateCompatibleDC)(NULL);
  LOGFONTW lf; SET_ERR(0);
  EXPECT_FAIL(U(GetObjectW)(dc, sizeof lf, &lf) == 0, ERROR_INVALID_HANDLE);   /* GdiSetLastError(6) */
  U(DeleteDC)(dc);
}

/* ---------------------------------------------------------------- TEXTMETRIC
 * win32kfull!vIFIMetricsToTextMetricWStrict (called by vIFIMetricsToTextMetricW): tmDescent = tmHeight - tmAscent. */
TEST(textmetric_height_is_ascent_plus_descent){
  static const WCHAR *FACES[] = {L"Segoe UI", L"MS Shell Dlg", L"MS Shell Dlg 2", L"Tahoma", L"Microsoft Sans Serif", L"Arial", L"Courier New", L"Courier", L"System", L"Terminal"};
  static const int H[] = {-8, -11, -12, -13, -15, -16, -24, 13, 16};
  HDC dc = U(GetDC)(NULL);
  for (unsigned i = 0; i < sizeof FACES / sizeof *FACES; i++) for (unsigned j = 0; j < sizeof H / sizeof *H; j++) {
    HFONT f = mkfont(FACES[i], H[j]); HGDIOBJ o = U(SelectObject)(dc, f);
    TEXTMETRICW tm; CHECK(U(GetTextMetricsW)(dc, &tm));
    CHECKF(tm.tmHeight == tm.tmAscent + tm.tmDescent, "face #%u h=%d: %ld != %ld + %ld", i, H[j], tm.tmHeight, tm.tmAscent, tm.tmDescent);
    U(SelectObject)(dc, o); U(DeleteObject)(f);
  }
  static const int STOCK[] = {SYSTEM_FONT, DEFAULT_GUI_FONT, ANSI_VAR_FONT, ANSI_FIXED_FONT, OEM_FIXED_FONT, SYSTEM_FIXED_FONT, DEVICE_DEFAULT_FONT};
  for (unsigned i = 0; i < sizeof STOCK / sizeof *STOCK; i++) {
    HGDIOBJ o = U(SelectObject)(dc, U(GetStockObject)(STOCK[i]));
    TEXTMETRICW tm; CHECK(U(GetTextMetricsW)(dc, &tm));
    CHECKF(tm.tmHeight == tm.tmAscent + tm.tmDescent, "stock %d", STOCK[i]);
    U(SelectObject)(dc, o);
  }
  U(ReleaseDC)(NULL, dc);
}

/* ---------------------------------------------------------------- SysHeader32 HDM_LAYOUT
 * comctl32!_Header_WndProc 0x1205 -> Header_OnLayout, whose int result is the message result.
 * Header_OnLayout: returns 0 if the HDLAYOUT, prc or pwpos is NULL; else hwndInsertAfter = 0, flags = 0x14,
 * x = prc->left, y = prc->top, cx = prc->right - prc->left, cy = (HDS_HIDDEN ? 0 : height), prc->top += cy, returns 1.
 * It never writes pwpos->hwnd. Header_OnGetItemRect: rect = GetClientRect with left/right from the item; 0 for a bad index. */
TEST(header_layout_contract){
  v6env e; if (!v6_begin(&e)) { v6_end(&e); return; }
  static const DWORD ST[] = {HDS_HORZ, HDS_BUTTONS, HDS_BUTTONS | HDS_HIDDEN, HDS_BUTTONS | HDS_FILTERBAR, HDS_FLAT};
  HFONT fonts[3] = {NULL, mkfont(L"Segoe UI", -12), mkfont(L"MS Shell Dlg", -11)};
  for (unsigned s = 0; s < sizeof ST / sizeof *ST; s++) for (int fi = 0; fi < 3; fi++) {
    HWND h = child(&e, WC_HEADERW, ST[s], 300, 30);
    if (!h) { CHECKF(0, "header create failed"); continue; }
    if (fonts[fi]) SND(h, WM_SETFONT, fonts[fi], TRUE);
    HDITEMW it; memset(&it, 0, sizeof it); it.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT; it.fmt = HDF_STRING; it.cxy = 100; it.pszText = (LPWSTR)L"Name";
    SND(h, HDM_INSERTITEMW, 0, &it);
    RECT rc = {7, 11, 307, 211}; WINDOWPOS wp; memset(&wp, 0x77, sizeof wp); HWND sentinel = wp.hwnd;
    HDLAYOUT hl = {&rc, &wp};
    EQ(SND(h, HDM_LAYOUT, 0, &hl), 1);
    EQ(wp.hwnd, sentinel);
    EQ(wp.hwndInsertAfter, NULL);
    EQ(wp.flags, SWP_NOZORDER | SWP_NOACTIVATE);
    EQ(wp.x, 7); EQ(wp.y, 11); EQ(wp.cx, 300);
    EQ(rc.top, 11 + wp.cy);
    EQ(rc.left, 7); EQ(rc.right, 307); EQ(rc.bottom, 211);
    if (ST[s] & HDS_HIDDEN) EQ(wp.cy, 0); else CHECKF(wp.cy > 0, "cy %d", wp.cy);
    /* item rect spans the client height */
    U(SetWindowPos)(h, NULL, 0, 0, 300, wp.cy ? wp.cy : 20, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT cr, ir; U(GetClientRect)(h, &cr);
    EQ(SND(h, HDM_GETITEMRECT, 0, &ir), 1);
    EQ(ir.top, cr.top); EQ(ir.bottom, cr.bottom);
    EQ(SND(h, HDM_GETITEMRECT, 5, &ir), 0);
    if (ir.right - ir.left != 100) t_note("item width %ld (set 100)", ir.right - ir.left);
    U(DestroyWindow)(h);
  }
  for (int fi = 1; fi < 3; fi++) U(DeleteObject)(fonts[fi]);
  v6_end(&e);
}

/* Header_OnLayout NULL checks (same decompile): a NULL HDLAYOUT, prc or pwpos returns 0 without writing. */
TEST(header_layout_null_args){
  v6env e; if (!v6_begin(&e)) { v6_end(&e); return; }
  HWND h = child(&e, WC_HEADERW, HDS_BUTTONS, 300, 30);
  if (!h) { CHECKF(0, "header create failed"); v6_end(&e); return; }
  EQ(SND(h, HDM_LAYOUT, 0, NULL), 0);
  RECT rc = {7, 11, 307, 211}; WINDOWPOS wp; memset(&wp, 0x77, sizeof wp);
  HDLAYOUT hl = {&rc, NULL};
  EQ(SND(h, HDM_LAYOUT, 0, &hl), 0);
  EQ(rc.top, 11);
  hl.prc = NULL; hl.pwpos = &wp;
  EQ(SND(h, HDM_LAYOUT, 0, &hl), 0);
  EQ(wp.x, 0x77777777);
  U(DestroyWindow)(h);
  v6_end(&e);
}

/* ---------------------------------------------------------------- SysTreeView32 item height
 * comctl32!TV_WndProc: TVM_GETITEMHEIGHT returns cyItem (+0x166). TVM_SETITEMHEIGHT returns the previous cyItem,
 * stores wParam, marks it user-set unless wParam == -1, then TV_SetItemHeight. TV_SetItemHeight: not user-set ->
 * cyItem = max(text, image) + 1; then cyItem < 2 -> 1, else rounded down to even unless TVS_NONEVENHEIGHT.
 * TV_GetItemRect: top = TopMargin + cyItem * (visible index - first visible); height = cyItem * integral;
 * fItemRect FALSE -> left = 0. */
static HTREEITEM tv_add(HWND h, HTREEITEM parent, const WCHAR *t){
  TVINSERTSTRUCTW is; memset(&is, 0, sizeof is); is.hParent = parent; is.hInsertAfter = TVI_LAST; is.item.mask = TVIF_TEXT; is.item.pszText = (LPWSTR)t;
  return (HTREEITEM)SND(h, TVM_INSERTITEMW, 0, &is);
}
TEST(treeview_item_height_contract){
  v6env e; if (!v6_begin(&e)) { v6_end(&e); return; }
  HFONT fonts[4] = {NULL, mkfont(L"Segoe UI", -12), mkfont(L"MS Shell Dlg", -11), mkfont(L"Tahoma", -13)};
  for (int nonEven = 0; nonEven < 2; nonEven++) for (int fi = 0; fi < 4; fi++) {
    HWND h = child(&e, WC_TREEVIEWW, TVS_HASLINES | TVS_HASBUTTONS | (nonEven ? TVS_NONEVENHEIGHT : 0), 300, 300);
    if (!h) { CHECKF(0, "treeview create failed"); continue; }
    if (fonts[fi]) SND(h, WM_SETFONT, fonts[fi], TRUE);
    HTREEITEM a = tv_add(h, TVI_ROOT, L"Alpha"), b = tv_add(h, TVI_ROOT, L"Beta"), c = tv_add(h, TVI_ROOT, L"Gamma");
    int ih = (int)SND(h, TVM_GETITEMHEIGHT, 0, 0);
    CHECKF(ih >= 1, "ih %d", ih);
    if (!nonEven && ih >= 2) CHECKF(ih % 2 == 0, "default height %d not even without TVS_NONEVENHEIGHT", ih);
    RECT ra, rb, rc, rt;
    memcpy(&ra, &a, sizeof a); EQ(SND(h, TVM_GETITEMRECT, FALSE, &ra), 1);
    memcpy(&rb, &b, sizeof b); EQ(SND(h, TVM_GETITEMRECT, FALSE, &rb), 1);
    memcpy(&rc, &c, sizeof c); EQ(SND(h, TVM_GETITEMRECT, FALSE, &rc), 1);
    memcpy(&rt, &a, sizeof a); EQ(SND(h, TVM_GETITEMRECT, TRUE, &rt), 1);
    EQ(ra.left, 0); EQ(rb.left, 0);
    EQ(ra.bottom - ra.top, ih); EQ(rb.bottom - rb.top, ih);
    EQ(rb.top, ra.top + ih); EQ(rc.top, ra.top + 2 * ih);
    EQ(rt.top, ra.top); EQ(rt.bottom, ra.bottom);
    /* set / reset */
    EQ(SND(h, TVM_SETITEMHEIGHT, 21, 0), ih);
    EQ(SND(h, TVM_GETITEMHEIGHT, 0, 0), nonEven ? 21 : 20);
    memcpy(&rb, &b, sizeof b); SND(h, TVM_GETITEMRECT, FALSE, &rb);
    EQ(rb.bottom - rb.top, nonEven ? 21 : 20);
    EQ(SND(h, TVM_SETITEMHEIGHT, 1, 0), nonEven ? 21 : 20);
    EQ(SND(h, TVM_GETITEMHEIGHT, 0, 0), 1);
    SND(h, TVM_SETITEMHEIGHT, 0, 0);
    EQ(SND(h, TVM_GETITEMHEIGHT, 0, 0), 1);
    SND(h, TVM_SETITEMHEIGHT, 30, 0);
    EQ(SND(h, TVM_GETITEMHEIGHT, 0, 0), 30);
    EQ(SND(h, TVM_SETITEMHEIGHT, (WPARAM)-1, 0), 30);
    EQ(SND(h, TVM_GETITEMHEIGHT, 0, 0), ih);           /* -1 restores the font-derived default */
    U(DestroyWindow)(h);
  }
  for (int fi = 1; fi < 4; fi++) U(DeleteObject)(fonts[fi]);
  v6_end(&e);
}

/* ---------------------------------------------------------------- observations (never fail)
 * List view report rows: comctl32!CLVView::ComputeCYItemSize gives cy = max(text cy, image cy) + SM_CYBORDER, raised
 * to text cy + 2 * EditYPadding in report/list; the mapping of "text cy" to TEXTMETRIC is not decomp-verified, so the
 * relation to tmHeight is logged only. */
TEST(listview_report_rows_observed){
  v6env e; if (!v6_begin(&e)) { v6_end(&e); return; }
  HFONT fonts[3] = {NULL, mkfont(L"Segoe UI", -12), mkfont(L"MS Shell Dlg", -11)};
  HDC dc = U(GetDC)(NULL);
  for (int fi = 0; fi < 3; fi++) {
    HWND h = child(&e, WC_LISTVIEWW, LVS_REPORT, 400, 300);
    if (!h) { t_note("listview create failed"); continue; }
    if (fonts[fi]) SND(h, WM_SETFONT, fonts[fi], TRUE);
    LVCOLUMNW col; memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH; col.cx = 100; col.pszText = (LPWSTR)L"Name";
    SND(h, LVM_INSERTCOLUMNW, 0, &col);
    for (int i = 0; i < 2; i++) { LVITEMW it; memset(&it, 0, sizeof it); it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPWSTR)L"Item"; SND(h, LVM_INSERTITEMW, 0, &it); }
    RECT r0, r1; r0.left = r1.left = LVIR_BOUNDS;
    SND(h, LVM_GETITEMRECT, 0, &r0); SND(h, LVM_GETITEMRECT, 1, &r1);
    HFONT used = (HFONT)SND(h, WM_GETFONT, 0, 0); HGDIOBJ o = U(SelectObject)(dc, used ? (HGDIOBJ)used : U(GetStockObject)(SYSTEM_FONT));
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm); U(SelectObject)(dc, o);
    t_note("font #%d: tmHeight %ld row %ld..%ld (h %ld) next top %ld SM_CYBORDER %d", fi, tm.tmHeight, r0.top, r0.bottom, r0.bottom - r0.top, r1.top, U(GetSystemMetrics)(SM_CYBORDER));
    U(DestroyWindow)(h);
  }
  U(ReleaseDC)(NULL, dc);
  for (int fi = 1; fi < 3; fi++) U(DeleteObject)(fonts[fi]);
  v6_end(&e);
}
