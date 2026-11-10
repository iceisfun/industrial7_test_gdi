/* ui_metrics: everything a dialog / control layout depends on (docs/PROBE_FORMAT.md, REC v1).
 *
 * Records, per thread DPI-awareness context (ctx=proc is the process default given by the manifest variant;
 * --full also switches the thread to unaware / system / pm / pm2 / gdiscaled with SetThreadDpiAwarenessContext):
 *   dpi, devcaps, mon     GetDpiForSystem/Window, awareness, GetDeviceCaps, monitor rects
 *   sm, sm_dpi            GetSystemMetrics 0..99 + 0x1000..0x2004, GetSystemMetricsForDpi at 96..240
 *   spi, spi_n            scalar SystemParametersInfo values, NONCLIENTMETRICS (cbSize 504/500 + bad sizes),
 *                         ICONMETRICS, ICONTITLELOGFONT, MINIMIZEDMETRICS, WORKAREA, SystemParametersInfoForDpi
 *   lf, ftm, ftm_cw       every LOGFONT seen (NCM, icon title, stock, theme, dialog WM_GETFONT, message box) and the
 *                         font realised on the screen DC (TEXTMETRIC, 52-letter extent, GdiGetCharDimensions, cw hash)
 *   stock                 stock font objects (DEFAULT_GUI_FONT identity)
 *   dbu                   GetDialogBaseUnits
 *   dlg, ctl, ctl_*       in-memory DLGTEMPLATE / DLGTEMPLATEEX dialogs (font, DS_SETFONT / DS_SHELLFONT / DS_FIXEDSYS /
 *                         no font / point size 0x7FFF) created hidden with CreateDialogIndirectParamW: window/client
 *                         rects, MapDialogRect base units, control rects (BUTTON EDIT STATIC COMBOBOX LISTBOX at several
 *                         DLU sizes) and per-class extras (edit margins/rect, combo item heights, listbox item height,
 *                         BCM_GETIDEALSIZE)
 *   awr, awr_dpi, win     AdjustWindowRectEx(ForDpi) style x exstyle x menu, and real CreateWindowEx frames
 *   menu, menu_item       menu bar of a real window at 3 widths; popup (TrackPopupMenuEx) item rects (--full)
 *   mb, mb_ch             MessageBox window and child rects for sample texts (captured from a WH_CBT hook)
 *   theme, theme_sz, theme_part   IsThemeActive/IsAppThemed, GetThemeSysSize/Font, a few GetThemePartSize
 *
 * usage: ui_metrics.exe [recorder options] [--quick | --full] [--no-msgbox] [--no-popup]
 *   --full (default): all contexts, all fonts, ~8k rows, a few seconds plus ~0.2 s per message box.
 *   --quick: process context only, 4 dialog fonts, 3 message boxes, no popup menu.
 * All rows are in device pixels (px) of the context they were measured in unless the column says :du.
 */
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include "rec.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef UM_SS6
#define PROBE "ui_metrics_ss6"
#else
#define PROBE "ui_metrics"
#endif
#define PROBE_VER "1"
#ifndef SPI_GETCARETTIMEOUT
#define SPI_GETCARETTIMEOUT 0x2022
#endif
#ifndef SPI_GETHANDEDNESS
#define SPI_GETHANDEDNESS 0x2024
#endif

static int g_full = 1, g_msgbox = 1, g_popup = 1;
static const char *g_ctx = "proc";
static HINSTANCE g_hi;
static HFONT g_guifont;

/* ------------------------------------------------------------------ dynamic APIs (absent on old systems / Wine) */
typedef HANDLE dctx;
#define DYN(ret, name, args) typedef ret (WINAPI *name##_t) args; static name##_t p##name
DYN(dctx, SetThreadDpiAwarenessContext, (dctx));
DYN(dctx, GetThreadDpiAwarenessContext, (void));
DYN(int,  GetAwarenessFromDpiAwarenessContext, (dctx));
DYN(UINT, GetDpiFromDpiAwarenessContext, (dctx));
DYN(UINT, GetDpiForSystem, (void));
DYN(UINT, GetDpiForWindow, (HWND));
DYN(UINT, GetSystemDpiForProcess, (HANDLE));
DYN(int,  GetSystemMetricsForDpi, (int, UINT));
DYN(BOOL, SystemParametersInfoForDpi, (UINT, UINT, PVOID, UINT, UINT));
DYN(BOOL, AdjustWindowRectExForDpi, (LPRECT, DWORD, BOOL, DWORD, UINT));
DYN(HRESULT, GetProcessDpiAwareness, (HANDLE, int *));
DYN(HRESULT, GetDpiForMonitor, (HMONITOR, int, UINT *, UINT *));
DYN(LONG, GdiGetCharDimensions, (HDC, TEXTMETRICW *, LONG *));
DYN(BOOL, IsThemeActive, (void));
DYN(BOOL, IsAppThemed, (void));
DYN(DWORD, GetThemeAppProperties, (void));
DYN(int,  GetThemeSysSize, (HANDLE, int));
DYN(HRESULT, GetThemeSysFont, (HANDLE, int, LOGFONTW *));
DYN(HANDLE, OpenThemeData, (HWND, LPCWSTR));
DYN(HRESULT, CloseThemeData, (HANDLE));
DYN(HRESULT, GetThemePartSize, (HANDLE, HDC, int, int, LPCRECT, int, SIZE *));
DYN(HRESULT, DwmIsCompositionEnabled, (BOOL *));
DYN(int,  MessageBoxTimeoutW, (HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD));
#define LOAD(mod, name) p##name = (name##_t)(void *)GetProcAddress(mod, #name)

static void load_apis(void){
  HMODULE u = GetModuleHandleA("user32.dll"), g = GetModuleHandleA("gdi32.dll");
  HMODULE sh = LoadLibraryA("shcore.dll"), ux = LoadLibraryA("uxtheme.dll"), dw = LoadLibraryA("dwmapi.dll");
  LOAD(u, SetThreadDpiAwarenessContext); LOAD(u, GetThreadDpiAwarenessContext); LOAD(u, GetAwarenessFromDpiAwarenessContext);
  LOAD(u, GetDpiFromDpiAwarenessContext); LOAD(u, GetDpiForSystem); LOAD(u, GetDpiForWindow); LOAD(u, GetSystemDpiForProcess);
  LOAD(u, GetSystemMetricsForDpi); LOAD(u, SystemParametersInfoForDpi); LOAD(u, AdjustWindowRectExForDpi); LOAD(u, MessageBoxTimeoutW);
  if (g) LOAD(g, GdiGetCharDimensions);
  if (sh) { LOAD(sh, GetProcessDpiAwareness); LOAD(sh, GetDpiForMonitor); }
  if (ux) { LOAD(ux, IsThemeActive); LOAD(ux, IsAppThemed); LOAD(ux, GetThemeAppProperties); LOAD(ux, GetThemeSysSize);
            LOAD(ux, GetThemeSysFont); LOAD(ux, OpenThemeData); LOAD(ux, CloseThemeData); LOAD(ux, GetThemePartSize); }
  if (dw) LOAD(dw, DwmIsCompositionEnabled);
}

/* ------------------------------------------------------------------ small helpers */
static void kctx(rk *k){ rk_reset(k); rk_s(k, "ctx", g_ctx); }
static void rv_b(rv *v, int ok, long long x){ if (ok) rv_i(v, x); else rv_na(v); }
static void rv_rect(rv *v, const RECT *r){ rv_i(v, r->left); rv_i(v, r->top); rv_i(v, r->right); rv_i(v, r->bottom); }
static void rv_rect_na(rv *v){ for (int i = 0; i < 4; i++) rv_na(v); }
static void rv_wh(rv *v, const RECT *r){ rv_i(v, r->right - r->left); rv_i(v, r->bottom - r->top); }

static void schemas(void){
  rec_schema("dpi", "ctx", "thread_aw thread_ctx_dpi sys_dpi proc_aw is_proc_aware sys_dpi_proc win_dpi desk_dpi "
             "mon_eff_x mon_eff_y mon_ang_x mon_raw_x dc_logpx dc_logpy windc_logpx");
  rec_schema("devcaps", "ctx cap", "v");
  rec_schema("mon", "ctx", "mon_l:px mon_t:px mon_r:px mon_b:px work_l:px work_t:px work_r:px work_b:px flags");
  rec_schema("sm", "ctx idx name", "v");
  rec_schema("sm_dpi", "dpi idx name", "v");
  rec_schema("spi", "ctx name", "ret v0 v1 v2");
  rec_schema("spi_n", "ctx src cb", "ret gle border:px scroll_w:px scroll_h:px cap_w:px cap_h:px smcap_w:px smcap_h:px menu_w:px menu_h:px padded:px");
  rec_schema("icm", "ctx src", "ret gle hspacing:px vspacing:px titlewrap");
  rec_schema("spi_err", "ctx case", "ret gle");
  rec_schema("lf", "ctx src", "h:lu w:lu esc orient weight italic underline strike charset outprec clipprec quality pitchfam face");
  rec_schema("ftm", "ctx src", "got_face h:px a:px d:px il:px el:px ave:px max:px weight overhang:px italic charset pf first last "
             "default break dax:px day:px ext52_cx:px ext52_cy:px gcd_cx:px gcd_cy:px ext_hw_cx:px");
  rec_schema("stock", "ctx obj", "objtype size_null same_handle eq_guifont");
  rec_schema("dbu", "ctx", "cx:px cy:px raw");
  rec_schema("dlg", "ctx tmpl style face pt wt it frame", "ok gle style exstyle win_w:px win_h:px cli_w:px cli_h:px "
             "bu_x:px bu_y:px map100_w:px map100_h:px map_7_7_x:px map_7_7_y:px win_dpi fontnull font_eq_guifont");
  rec_schema("ctl", "ctx tmpl style face pt wt it frame cid", "ok style exstyle x:px y:px w:px h:px cli_w:px cli_h:px font du_x:du du_y:du du_w:du du_h:du");
  rec_schema("ctl_edit", "ctx tmpl style face pt wt it frame cid", "margin_l:px margin_r:px rect_l:px rect_t:px rect_r:px rect_b:px");
  rec_schema("ctl_cb", "ctx tmpl style face pt wt it frame cid", "ih_sel:px ih_item:px drop_w:px drop_h:px "
             "item_l:px item_t:px item_r:px item_b:px btn_l:px btn_t:px btn_r:px btn_b:px");
  rec_schema("ctl_lb", "ctx tmpl style face pt wt it frame cid", "ih:px");
  rec_schema("ctl_btn", "ctx tmpl style face pt wt it frame cid", "ideal_cx:px ideal_cy:px tm_l:px tm_t:px tm_r:px tm_b:px");
  rec_schema("awr", "ctx style ex menu", "ret dl:px dt:px dr:px db:px");
  rec_schema("awr_dpi", "dpi style ex menu", "ret dl:px dt:px dr:px db:px");
  rec_schema("win", "ctx style ex menu", "ok style exstyle win_w:px win_h:px cli_w:px cli_h:px cli_x:px cli_y:px");
  rec_schema("menu", "ctx width shown", "ok win_w:px win_h:px cli_w:px cli_h:px cli_y:px bar_l:px bar_t:px bar_r:px bar_b:px nitems rows");
  rec_schema("menu_item", "ctx width shown item", "l:px t:px r:px b:px");
  rec_schema("popup", "ctx", "ok win_w:px win_h:px nitems checkmark_w:px checkmark_h:px win_dx:px win_dy:px");
  rec_schema("popup_item", "ctx item", "x:px y:px w:px h:px");
  rec_schema("mb", "ctx case", "ok x:px y:px win_w:px win_h:px cli_w:px cli_h:px nchild style exstyle");
  rec_schema("mb_ret", "ctx case", "ret seen");
  rec_schema("mb_ch", "ctx case i", "cls id x:px y:px w:px h:px text");
  rec_schema("theme", "ctx", "is_theme_active is_app_themed app_props dwm_composition");
  rec_schema("theme_sz", "ctx src id", "v");
  rec_schema("theme_part", "ctx cls part state ts", "hr cx:px cy:px");
}

/* ------------------------------------------------------------------ fonts */
static void lf_row(const char *src, const LOGFONTW *lf){
  rk k; kctx(&k); rk_s(&k, "src", src);
  rv v; rv_reset(&v);
  if (!lf) { for (int i = 0; i < 14; i++) rv_na(&v); rec_row("lf", &k, &v); return; }
  rv_i(&v, lf->lfHeight); rv_i(&v, lf->lfWidth); rv_i(&v, lf->lfEscapement); rv_i(&v, lf->lfOrientation); rv_i(&v, lf->lfWeight);
  rv_i(&v, lf->lfItalic); rv_i(&v, lf->lfUnderline); rv_i(&v, lf->lfStrikeOut); rv_i(&v, lf->lfCharSet); rv_i(&v, lf->lfOutPrecision);
  rv_i(&v, lf->lfClipPrecision); rv_i(&v, lf->lfQuality); rv_x(&v, lf->lfPitchAndFamily); rv_w(&v, lf->lfFaceName);
  rec_row("lf", &k, &v);
}

/* realise f (or the DC's default font when f == NULL) on the screen DC */
static void ftm_row(const char *src, HFONT f){
  static const WCHAR A52[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  HDC dc = GetDC(NULL); HGDIOBJ o = f ? SelectObject(dc, f) : NULL;
  WCHAR face[LF_FACESIZE + 8] = L""; TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
  int okf = GetTextFaceW(dc, LF_FACESIZE + 8, face) > 0, okt = GetTextMetricsW(dc, &tm);
  SIZE e52 = {0, 0}, ehw = {0, 0};
  int oke = GetTextExtentPoint32W(dc, A52, 52, &e52), okh = GetTextExtentPoint32W(dc, L"Hello world", 11, &ehw);
  LONG gcy = 0, gcx = pGdiGetCharDimensions ? pGdiGetCharDimensions(dc, NULL, &gcy) : 0;
  rk k; kctx(&k); rk_s(&k, "src", src);
  rv v; rv_reset(&v);
  if (okf) rv_w(&v, face); else rv_na(&v);
  if (okt) {
    rv_i(&v, tm.tmHeight); rv_i(&v, tm.tmAscent); rv_i(&v, tm.tmDescent); rv_i(&v, tm.tmInternalLeading); rv_i(&v, tm.tmExternalLeading);
    rv_i(&v, tm.tmAveCharWidth); rv_i(&v, tm.tmMaxCharWidth); rv_i(&v, tm.tmWeight); rv_i(&v, tm.tmOverhang); rv_i(&v, tm.tmItalic);
    rv_i(&v, tm.tmCharSet); rv_x(&v, tm.tmPitchAndFamily); rv_x(&v, tm.tmFirstChar); rv_x(&v, tm.tmLastChar); rv_x(&v, tm.tmDefaultChar);
    rv_x(&v, tm.tmBreakChar); rv_i(&v, tm.tmDigitizedAspectX); rv_i(&v, tm.tmDigitizedAspectY);
  } else for (int i = 0; i < 18; i++) rv_na(&v);
  if (oke) { rv_i(&v, e52.cx); rv_i(&v, e52.cy); } else { rv_na(&v); rv_na(&v); }
  if (pGdiGetCharDimensions && gcx) { rv_i(&v, gcx); rv_i(&v, gcy); } else { rv_na(&v); rv_na(&v); }
  rv_b(&v, okh, ehw.cx);
  rec_row("ftm", &k, &v);
  INT w[95]; if (GetCharWidth32W(dc, 0x20, 0x7e, w)) rec_hash("ftm_cw", &k, w, sizeof w);
  if (o) SelectObject(dc, o);
  ReleaseDC(NULL, dc);
}

static void lf_and_ftm(const char *src, const LOGFONTW *lf){
  lf_row(src, lf);
  if (!lf) return;
  HFONT f = CreateFontIndirectW(lf); ftm_row(src, f); DeleteObject(f);
}

/* ------------------------------------------------------------------ DPI / device */
static void dpi_info(HWND probe_wnd){
  rk k; kctx(&k); rv v; rv_reset(&v);
  dctx tc = pGetThreadDpiAwarenessContext ? pGetThreadDpiAwarenessContext() : NULL;
  rv_b(&v, tc && pGetAwarenessFromDpiAwarenessContext, tc && pGetAwarenessFromDpiAwarenessContext ? pGetAwarenessFromDpiAwarenessContext(tc) : 0);
  rv_b(&v, tc && pGetDpiFromDpiAwarenessContext, tc && pGetDpiFromDpiAwarenessContext ? pGetDpiFromDpiAwarenessContext(tc) : 0);
  rv_b(&v, pGetDpiForSystem != NULL, pGetDpiForSystem ? pGetDpiForSystem() : 0);
  int pa = -1; HRESULT hr = pGetProcessDpiAwareness ? pGetProcessDpiAwareness(NULL, &pa) : E_FAIL;
  rv_b(&v, SUCCEEDED(hr), pa);
  rv_i(&v, IsProcessDPIAware());
  rv_b(&v, pGetSystemDpiForProcess != NULL, pGetSystemDpiForProcess ? pGetSystemDpiForProcess(GetCurrentProcess()) : 0);
  rv_b(&v, pGetDpiForWindow && probe_wnd, pGetDpiForWindow && probe_wnd ? pGetDpiForWindow(probe_wnd) : 0);
  rv_b(&v, pGetDpiForWindow != NULL, pGetDpiForWindow ? pGetDpiForWindow(GetDesktopWindow()) : 0);
  POINT pt = {0, 0}; HMONITOR m = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
  UINT x = 0, y = 0;
  hr = pGetDpiForMonitor ? pGetDpiForMonitor(m, 0, &x, &y) : E_FAIL; rv_b(&v, SUCCEEDED(hr), x); rv_b(&v, SUCCEEDED(hr), y);
  hr = pGetDpiForMonitor ? pGetDpiForMonitor(m, 1, &x, &y) : E_FAIL; rv_b(&v, SUCCEEDED(hr), x);
  hr = pGetDpiForMonitor ? pGetDpiForMonitor(m, 2, &x, &y) : E_FAIL; rv_b(&v, SUCCEEDED(hr), x);
  HDC dc = GetDC(NULL); rv_i(&v, GetDeviceCaps(dc, LOGPIXELSX)); rv_i(&v, GetDeviceCaps(dc, LOGPIXELSY));
  static const struct { int c; const char *n; } caps[] = {
    {LOGPIXELSX, "LOGPIXELSX"}, {LOGPIXELSY, "LOGPIXELSY"}, {HORZRES, "HORZRES"}, {VERTRES, "VERTRES"},
    {DESKTOPHORZRES, "DESKTOPHORZRES"}, {DESKTOPVERTRES, "DESKTOPVERTRES"}, {HORZSIZE, "HORZSIZE"}, {VERTSIZE, "VERTSIZE"},
    {BITSPIXEL, "BITSPIXEL"}, {PLANES, "PLANES"}, {NUMCOLORS, "NUMCOLORS"}, {ASPECTX, "ASPECTX"}, {ASPECTY, "ASPECTY"},
    {ASPECTXY, "ASPECTXY"}, {TECHNOLOGY, "TECHNOLOGY"}, {RASTERCAPS, "RASTERCAPS"}, {TEXTCAPS, "TEXTCAPS"},
    {VREFRESH, "VREFRESH"}, {SIZEPALETTE, "SIZEPALETTE"}, {COLORRES, "COLORRES"}, {SHADEBLENDCAPS, "SHADEBLENDCAPS"},
    {CLIPCAPS, "CLIPCAPS"}, {LINECAPS, "LINECAPS"}, {CURVECAPS, "CURVECAPS"}, {POLYGONALCAPS, "POLYGONALCAPS"},
    {NUMFONTS, "NUMFONTS"}, {NUMPENS, "NUMPENS"}, {NUMBRUSHES, "NUMBRUSHES"}, {BLTALIGNMENT, "BLTALIGNMENT"} };
  int dcv[sizeof caps / sizeof *caps];
  for (unsigned i = 0; i < sizeof caps / sizeof *caps; i++) dcv[i] = GetDeviceCaps(dc, caps[i].c);
  ReleaseDC(NULL, dc);
  if (probe_wnd) { HDC wd = GetDC(probe_wnd); rv_i(&v, GetDeviceCaps(wd, LOGPIXELSX)); ReleaseDC(probe_wnd, wd); } else rv_na(&v);
  rec_row("dpi", &k, &v);
  for (unsigned i = 0; i < sizeof caps / sizeof *caps; i++) {
    kctx(&k); rk_s(&k, "cap", caps[i].n); rv_reset(&v); rv_i(&v, dcv[i]); rec_row("devcaps", &k, &v);
  }
  MONITORINFO mi; mi.cbSize = sizeof mi; kctx(&k); rv_reset(&v);
  if (GetMonitorInfoW(m, &mi)) { rv_rect(&v, &mi.rcMonitor); rv_rect(&v, &mi.rcWork); rv_x(&v, mi.dwFlags); }
  else { rv_rect_na(&v); rv_rect_na(&v); rv_na(&v); }
  rec_row("mon", &k, &v);
}

/* ------------------------------------------------------------------ system metrics */
#define SMN(x) {x, #x}
static const struct { int i; const char *n; } SMN_[] = {
  SMN(SM_CXSCREEN), SMN(SM_CYSCREEN), SMN(SM_CXVSCROLL), SMN(SM_CYHSCROLL), SMN(SM_CYCAPTION), SMN(SM_CXBORDER), SMN(SM_CYBORDER),
  SMN(SM_CXDLGFRAME), SMN(SM_CYDLGFRAME), SMN(SM_CYVTHUMB), SMN(SM_CXHTHUMB), SMN(SM_CXICON), SMN(SM_CYICON), SMN(SM_CXCURSOR),
  SMN(SM_CYCURSOR), SMN(SM_CYMENU), SMN(SM_CXFULLSCREEN), SMN(SM_CYFULLSCREEN), SMN(SM_CYKANJIWINDOW), SMN(SM_MOUSEPRESENT),
  SMN(SM_CYVSCROLL), SMN(SM_CXHSCROLL), SMN(SM_DEBUG), SMN(SM_SWAPBUTTON), SMN(SM_RESERVED1), SMN(SM_RESERVED2), SMN(SM_RESERVED3),
  SMN(SM_RESERVED4), SMN(SM_CXMIN), SMN(SM_CYMIN), SMN(SM_CXSIZE), SMN(SM_CYSIZE), SMN(SM_CXFRAME), SMN(SM_CYFRAME), SMN(SM_CXMINTRACK),
  SMN(SM_CYMINTRACK), SMN(SM_CXDOUBLECLK), SMN(SM_CYDOUBLECLK), SMN(SM_CXICONSPACING), SMN(SM_CYICONSPACING), SMN(SM_MENUDROPALIGNMENT),
  SMN(SM_PENWINDOWS), SMN(SM_DBCSENABLED), SMN(SM_CMOUSEBUTTONS), SMN(SM_SECURE), SMN(SM_CXEDGE), SMN(SM_CYEDGE), SMN(SM_CXMINSPACING),
  SMN(SM_CYMINSPACING), SMN(SM_CXSMICON), SMN(SM_CYSMICON), SMN(SM_CYSMCAPTION), SMN(SM_CXSMSIZE), SMN(SM_CYSMSIZE), SMN(SM_CXMENUSIZE),
  SMN(SM_CYMENUSIZE), SMN(SM_ARRANGE), SMN(SM_CXMINIMIZED), SMN(SM_CYMINIMIZED), SMN(SM_CXMAXTRACK), SMN(SM_CYMAXTRACK),
  SMN(SM_CXMAXIMIZED), SMN(SM_CYMAXIMIZED), SMN(SM_NETWORK), SMN(SM_CLEANBOOT), SMN(SM_CXDRAG), SMN(SM_CYDRAG), SMN(SM_SHOWSOUNDS),
  SMN(SM_CXMENUCHECK), SMN(SM_CYMENUCHECK), SMN(SM_SLOWMACHINE), SMN(SM_MIDEASTENABLED), SMN(SM_MOUSEWHEELPRESENT),
  SMN(SM_XVIRTUALSCREEN), SMN(SM_YVIRTUALSCREEN), SMN(SM_CXVIRTUALSCREEN), SMN(SM_CYVIRTUALSCREEN), SMN(SM_CMONITORS),
  SMN(SM_SAMEDISPLAYFORMAT), SMN(SM_IMMENABLED), SMN(SM_CXFOCUSBORDER), SMN(SM_CYFOCUSBORDER), SMN(SM_TABLETPC), SMN(SM_MEDIACENTER),
  SMN(SM_STARTER), SMN(SM_SERVERR2), SMN(SM_MOUSEHORIZONTALWHEELPRESENT), SMN(SM_CXPADDEDBORDER), SMN(SM_DIGITIZER),
  SMN(SM_MAXIMUMTOUCHES), SMN(SM_REMOTESESSION), SMN(SM_SHUTTINGDOWN), SMN(SM_REMOTECONTROL), SMN(SM_CARETBLINKINGENABLED),
  SMN(SM_CONVERTIBLESLATEMODE), SMN(SM_SYSTEMDOCKED) };
#define NSMN ((int)(sizeof SMN_ / sizeof *SMN_))
static const char *sm_name(int i, char *buf){
  for (int j = 0; j < NSMN; j++) if (SMN_[j].i == i) return SMN_[j].n + 3;
  sprintf(buf, "i%d", i); return buf;
}
/* indices: 0..99 (unnamed ones as iN) then the 0x1000.. / 0x2000.. block */
static int sm_index(int n){ if (n < 100) return n; static const int hi[] = {0x1000, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004}; return n - 100 < 6 ? hi[n - 100] : -1; }
#define SM_COUNT 106

static void sysmetrics(void){
  char nb[16]; rk k; rv v;
  for (int n = 0; n < SM_COUNT; n++) {
    int i = sm_index(n);
    kctx(&k); rk_i(&k, "idx", i); rk_s(&k, "name", sm_name(i, nb));
    rv_reset(&v); rv_i(&v, GetSystemMetrics(i)); rec_row("sm", &k, &v);
  }
}
static void sysmetrics_dpi(void){
  if (!pGetSystemMetricsForDpi) { rec_note("GetSystemMetricsForDpi not available"); return; }
  static const int dq[] = {96, 144}, df[] = {72, 96, 120, 144, 168, 192, 240, 288};
  const int *d = g_full ? df : dq; int nd = g_full ? 8 : 2;
  char nb[16]; rk k; rv v;
  for (int j = 0; j < nd; j++) for (int n = 0; n < SM_COUNT; n++) {
    int i = sm_index(n);
    rk_reset(&k); rk_i(&k, "dpi", d[j]); rk_i(&k, "idx", i); rk_s(&k, "name", sm_name(i, nb));
    rv_reset(&v); rv_i(&v, pGetSystemMetricsForDpi(i, d[j])); rec_row("sm_dpi", &k, &v);
  }
}

/* ------------------------------------------------------------------ SystemParametersInfo */
#define SPN(x, n) {x, #x, n}
static const struct { UINT a; const char *n; int nint; } SPI_[] = {
  SPN(SPI_GETBEEP, 1), SPN(SPI_GETMOUSE, 3), SPN(SPI_GETBORDER, 1), SPN(SPI_GETKEYBOARDSPEED, 1), SPN(SPI_GETSCREENSAVETIMEOUT, 1),
  SPN(SPI_GETSCREENSAVEACTIVE, 1), SPN(SPI_GETGRIDGRANULARITY, 1), SPN(SPI_GETKEYBOARDDELAY, 1), SPN(SPI_GETICONTITLEWRAP, 1),
  SPN(SPI_GETMENUDROPALIGNMENT, 1), SPN(SPI_GETFASTTASKSWITCH, 1), SPN(SPI_GETDRAGFULLWINDOWS, 1), SPN(SPI_GETKEYBOARDPREF, 1),
  SPN(SPI_GETSCREENREADER, 1), SPN(SPI_GETFONTSMOOTHING, 1), SPN(SPI_GETLOWPOWERACTIVE, 1), SPN(SPI_GETPOWEROFFACTIVE, 1),
  SPN(SPI_GETMOUSETRAILS, 1), SPN(SPI_GETSHOWSOUNDS, 1), SPN(SPI_GETSNAPTODEFBUTTON, 1), SPN(SPI_GETMOUSEHOVERWIDTH, 1),
  SPN(SPI_GETMOUSEHOVERHEIGHT, 1), SPN(SPI_GETMOUSEHOVERTIME, 1), SPN(SPI_GETWHEELSCROLLLINES, 1), SPN(SPI_GETMENUSHOWDELAY, 1),
  SPN(SPI_GETWHEELSCROLLCHARS, 1), SPN(SPI_GETSHOWIMEUI, 1), SPN(SPI_GETMOUSESPEED, 1), SPN(SPI_GETSCREENSAVERRUNNING, 1),
  SPN(SPI_GETACTIVEWINDOWTRACKING, 1), SPN(SPI_GETMENUANIMATION, 1), SPN(SPI_GETCOMBOBOXANIMATION, 1),
  SPN(SPI_GETLISTBOXSMOOTHSCROLLING, 1), SPN(SPI_GETGRADIENTCAPTIONS, 1), SPN(SPI_GETKEYBOARDCUES, 1), SPN(SPI_GETACTIVEWNDTRKZORDER, 1),
  SPN(SPI_GETHOTTRACKING, 1), SPN(SPI_GETMENUFADE, 1), SPN(SPI_GETSELECTIONFADE, 1), SPN(SPI_GETTOOLTIPANIMATION, 1),
  SPN(SPI_GETTOOLTIPFADE, 1), SPN(SPI_GETCURSORSHADOW, 1), SPN(SPI_GETMOUSESONAR, 1), SPN(SPI_GETMOUSECLICKLOCK, 1),
  SPN(SPI_GETMOUSEVANISH, 1), SPN(SPI_GETFLATMENU, 1), SPN(SPI_GETDROPSHADOW, 1), SPN(SPI_GETBLOCKSENDINPUTRESETS, 1),
  SPN(SPI_GETUIEFFECTS, 1), SPN(SPI_GETDISABLEOVERLAPPEDCONTENT, 1), SPN(SPI_GETCLIENTAREAANIMATION, 1), SPN(SPI_GETCLEARTYPE, 1),
  SPN(SPI_GETSPEECHRECOGNITION, 1), SPN(SPI_GETCARETBROWSING, 1), SPN(SPI_GETTHREADLOCALINPUTSETTINGS, 1),
  SPN(SPI_GETSYSTEMLANGUAGEBAR, 1), SPN(SPI_GETFOREGROUNDLOCKTIMEOUT, 1), SPN(SPI_GETACTIVEWNDTRKTIMEOUT, 1),
  SPN(SPI_GETFOREGROUNDFLASHCOUNT, 1), SPN(SPI_GETCARETWIDTH, 1), SPN(SPI_GETMOUSECLICKLOCKTIME, 1), SPN(SPI_GETFONTSMOOTHINGTYPE, 1),
  SPN(SPI_GETFONTSMOOTHINGCONTRAST, 1), SPN(SPI_GETFOCUSBORDERWIDTH, 1), SPN(SPI_GETFOCUSBORDERHEIGHT, 1),
  SPN(SPI_GETFONTSMOOTHINGORIENTATION, 1), SPN(SPI_GETMINIMUMHITRADIUS, 1), SPN(SPI_GETMESSAGEDURATION, 1),
  SPN(SPI_GETCONTACTVISUALIZATION, 1), SPN(SPI_GETGESTUREVISUALIZATION, 1), SPN(SPI_GETMOUSEWHEELROUTING, 1),
  SPN(SPI_GETPENVISUALIZATION, 1), SPN(SPI_GETCARETTIMEOUT, 1), SPN(SPI_GETHANDEDNESS, 1), SPN(SPI_GETDOCKMOVING, 1),
  SPN(SPI_GETSNAPSIZING, 1), SPN(SPI_GETDRAGFROMMAXIMIZE, 1), SPN(SPI_GETWINARRANGING, 1), SPN(SPI_GETLOGICALDPIOVERRIDE, 1),
  SPN(SPI_GETMOUSEDOCKTHRESHOLD, 1), SPN(SPI_GETMOUSEDRAGOUTTHRESHOLD, 1), SPN(SPI_GETMOUSESIDEMOVETHRESHOLD, 1) };

static void spi_scalar(void){
  rk k; rv v;
  for (unsigned j = 0; j < sizeof SPI_ / sizeof *SPI_; j++) {
    int b[16]; memset(b, 0, sizeof b);
    BOOL r = SystemParametersInfoW(SPI_[j].a, 0, b, 0);
    kctx(&k); rk_s(&k, "name", SPI_[j].n + 4); rv_reset(&v); rv_i(&v, r ? 1 : 0);
    for (int i = 0; i < 3; i++) { if (r && i < SPI_[j].nint) rv_i(&v, b[i]); else rv_na(&v); }
    rec_row("spi", &k, &v);
  }
  /* icon spacing: uiParam 0 + pv = query */
  int s = 0; BOOL r;
  r = SystemParametersInfoW(SPI_ICONHORIZONTALSPACING, 0, &s, 0); kctx(&k); rk_s(&k, "name", "ICONHORIZONTALSPACING");
  rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, s); rv_na(&v); rv_na(&v); rec_row("spi", &k, &v);
  s = 0; r = SystemParametersInfoW(SPI_ICONVERTICALSPACING, 0, &s, 0); kctx(&k); rk_s(&k, "name", "ICONVERTICALSPACING");
  rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, s); rv_na(&v); rv_na(&v); rec_row("spi", &k, &v);
  RECT wa; r = SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  kctx(&k); rk_s(&k, "name", "GETWORKAREA.lt"); rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, wa.left); rv_b(&v, r, wa.top); rv_na(&v); rec_row("spi", &k, &v);
  kctx(&k); rk_s(&k, "name", "GETWORKAREA.rb"); rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, wa.right); rv_b(&v, r, wa.bottom); rv_na(&v); rec_row("spi", &k, &v);
  MINIMIZEDMETRICS mm; memset(&mm, 0, sizeof mm); mm.cbSize = sizeof mm; r = SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof mm, &mm, 0);
  kctx(&k); rk_s(&k, "name", "GETMINIMIZEDMETRICS.w_hg_vg"); rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, mm.iWidth); rv_b(&v, r, mm.iHorzGap); rv_b(&v, r, mm.iVertGap); rec_row("spi", &k, &v);
  kctx(&k); rk_s(&k, "name", "GETMINIMIZEDMETRICS.arrange"); rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, mm.iArrange); rv_na(&v); rv_na(&v); rec_row("spi", &k, &v);
  ANIMATIONINFO ai; ai.cbSize = sizeof ai; ai.iMinAnimate = -1; r = SystemParametersInfoW(SPI_GETANIMATION, sizeof ai, &ai, 0);
  kctx(&k); rk_s(&k, "name", "GETANIMATION"); rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_b(&v, r, ai.iMinAnimate); rv_na(&v); rv_na(&v); rec_row("spi", &k, &v);
  HIGHCONTRASTW hc; memset(&hc, 0, sizeof hc); hc.cbSize = sizeof hc; r = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0);
  kctx(&k); rk_s(&k, "name", "GETHIGHCONTRAST.flags"); rv_reset(&v); rv_i(&v, r ? 1 : 0); if (r) rv_x(&v, hc.dwFlags); else rv_na(&v); rv_na(&v); rv_na(&v); rec_row("spi", &k, &v);
  int cmw = 0, cmh = 0; LONG cm = GetMenuCheckMarkDimensions(); cmw = LOWORD(cm); cmh = HIWORD(cm);
  kctx(&k); rk_s(&k, "name", "GetMenuCheckMarkDimensions"); rv_reset(&v); rv_i(&v, 1); rv_i(&v, cmw); rv_i(&v, cmh); rv_na(&v); rec_row("spi", &k, &v);
  UINT dct = GetDoubleClickTime(), cbt = GetCaretBlinkTime();
  kctx(&k); rk_s(&k, "name", "GetDoubleClickTime.GetCaretBlinkTime"); rv_reset(&v); rv_i(&v, 1); rv_i(&v, dct); rv_i(&v, cbt); rv_na(&v); rec_row("spi", &k, &v);
}

static void ncm_fonts(const char *pfx, const NONCLIENTMETRICSW *n, int realise){
  static const char *nm[] = {"caption", "smcaption", "menu", "status", "message"};
  const LOGFONTW *l[] = {&n->lfCaptionFont, &n->lfSmCaptionFont, &n->lfMenuFont, &n->lfStatusFont, &n->lfMessageFont};
  char s[64];
  for (int i = 0; i < 5; i++) { snprintf(s, sizeof s, "%s.%s", pfx, nm[i]); if (realise) lf_and_ftm(s, l[i]); else lf_row(s, l[i]); }
}

static void ncm_row(const char *src, UINT cb, BOOL r, DWORD gle, const NONCLIENTMETRICSW *n){
  rk k; kctx(&k); rk_s(&k, "src", src); rk_i(&k, "cb", cb);
  rv v; rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_i(&v, r ? 0 : (long long)gle);
  if (r) { rv_i(&v, n->iBorderWidth); rv_i(&v, n->iScrollWidth); rv_i(&v, n->iScrollHeight); rv_i(&v, n->iCaptionWidth); rv_i(&v, n->iCaptionHeight);
    rv_i(&v, n->iSmCaptionWidth); rv_i(&v, n->iSmCaptionHeight); rv_i(&v, n->iMenuWidth); rv_i(&v, n->iMenuHeight);
    if (cb >= sizeof(NONCLIENTMETRICSW)) rv_i(&v, n->iPaddedBorderWidth); else rv_na(&v); }
  else for (int i = 0; i < 10; i++) rv_na(&v);
  rec_row("spi_n", &k, &v);
}

static void spi_err(const char *c, BOOL r){
  DWORD e = GetLastError(); rk k; kctx(&k); rk_s(&k, "case", c); rv v; rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_i(&v, r ? 0 : (long long)e);
  rec_row("spi_err", &k, &v);
}

static void icm_row(const char *src, BOOL r, DWORD gle, const ICONMETRICSW *im){
  rk k; kctx(&k); rk_s(&k, "src", src); rv v; rv_reset(&v); rv_i(&v, r ? 1 : 0); rv_i(&v, r ? 0 : (long long)gle);
  if (r) { rv_i(&v, im->iHorzSpacing); rv_i(&v, im->iVertSpacing); rv_i(&v, im->iTitleWrap); } else { rv_na(&v); rv_na(&v); rv_na(&v); }
  rec_row("icm", &k, &v);
}

static void spi_struct(int realise){
  NONCLIENTMETRICSW n; BOOL r; DWORD e; char s[64];
  /* cbSize 504 (with iPaddedBorderWidth) and 500 (pre-Vista layout) */
  memset(&n, 0, sizeof n); n.cbSize = sizeof n; SetLastError(0); r = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0); e = GetLastError();
  ncm_row("spi", sizeof n, r, e, &n); if (r) ncm_fonts("ncm", &n, realise);
  UINT old = (UINT)offsetof(NONCLIENTMETRICSW, iPaddedBorderWidth);
  memset(&n, 0, sizeof n); n.cbSize = old; SetLastError(0); r = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, old, &n, 0); e = GetLastError();
  ncm_row("spi", old, r, e, &n); if (r) ncm_fonts("ncm500", &n, 0);
  ICONMETRICSW im; memset(&im, 0, sizeof im); im.cbSize = sizeof im; SetLastError(0); r = SystemParametersInfoW(SPI_GETICONMETRICS, sizeof im, &im, 0); e = GetLastError();
  icm_row("spi", r, e, &im); if (r) { if (realise) lf_and_ftm("icm.font", &im.lfFont); else lf_row("icm.font", &im.lfFont); }
  LOGFONTW lf; memset(&lf, 0, sizeof lf); r = SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0);
  if (r) { if (realise) lf_and_ftm("icontitle", &lf); else lf_row("icontitle", &lf); } else lf_row("icontitle", NULL);
  /* parameter protocol (results recorded, not asserted) */
  memset(&n, 0, sizeof n); n.cbSize = 0;   SetLastError(0); spi_err("ncm.cb0", SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, 0, &n, 0));
  memset(&n, 0, sizeof n); n.cbSize = 340; SetLastError(0); spi_err("ncm.cb340", SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, 340, &n, 0));
  memset(&n, 0, sizeof n); n.cbSize = sizeof n + 4; SetLastError(0); spi_err("ncm.cb508", SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof n + 4, &n, 0));
  memset(&n, 0, sizeof n); n.cbSize = sizeof n; SetLastError(0); spi_err("ncm.ui0_cb504", SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, 0, &n, 0));
  memset(&im, 0, sizeof im); im.cbSize = 0; SetLastError(0); spi_err("icm.cb0", SystemParametersInfoW(SPI_GETICONMETRICS, 0, &im, 0));
  SetLastError(0); spi_err("icontitle.ui0", SystemParametersInfoW(SPI_GETICONTITLELOGFONT, 0, &lf, 0));
  SetLastError(0); spi_err("icontitle.ui60", SystemParametersInfoW(SPI_GETICONTITLELOGFONT, 60, &lf, 0));
  if (!pSystemParametersInfoForDpi) { rec_note("SystemParametersInfoForDpi not available"); return; }
  static const UINT df[] = {96, 120, 144, 168, 192, 240}, dq[] = {96, 144};
  const UINT *d = g_full ? df : dq; int nd = g_full ? 6 : 2;
  for (int j = 0; j < nd; j++) {
    snprintf(s, sizeof s, "fordpi%u", d[j]);
    memset(&n, 0, sizeof n); n.cbSize = sizeof n; SetLastError(0); r = pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, d[j]); e = GetLastError();
    ncm_row(s, sizeof n, r, e, &n);
    if (r) { char p[64]; snprintf(p, sizeof p, "ncm%u", d[j]); ncm_fonts(p, &n, realise && strcmp(g_ctx, "proc") == 0); }
    memset(&im, 0, sizeof im); im.cbSize = sizeof im; SetLastError(0); r = pSystemParametersInfoForDpi(SPI_GETICONMETRICS, sizeof im, &im, 0, d[j]); e = GetLastError();
    icm_row(s, r, e, &im);
    memset(&lf, 0, sizeof lf); r = pSystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0, d[j]);
    { char p[64]; snprintf(p, sizeof p, "icontitle%u", d[j]); lf_row(p, r ? &lf : NULL); }
  }
  memset(&n, 0, sizeof n); n.cbSize = old; SetLastError(0); spi_err("fordpi.ncm.cb500", pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, old, &n, 0, 96));
  memset(&n, 0, sizeof n); n.cbSize = sizeof n; SetLastError(0); spi_err("fordpi.ncm.dpi0", pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 0));
  SetLastError(0); spi_err("fordpi.ncm.null", pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof n, NULL, 0, 96));
  RECT wa; SetLastError(0); spi_err("fordpi.workarea", pSystemParametersInfoForDpi(SPI_GETWORKAREA, sizeof wa, &wa, 0, 96));
  memset(&lf, 0, sizeof lf); SetLastError(0); spi_err("fordpi.icontitle.ui0", pSystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, 0, &lf, 0, 96));
  memset(&n, 0, sizeof n); n.cbSize = sizeof n; SetLastError(0); spi_err("fordpi.ncm.dpi1", pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0, 1));
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0)) {} /* restore nothing; read only */
}

static void dbu_row(void){
  LONG b = GetDialogBaseUnits(); rk k; kctx(&k); rv v; rv_reset(&v); rv_i(&v, LOWORD(b)); rv_i(&v, HIWORD(b)); rv_x(&v, (DWORD)b);
  rec_row("dbu", &k, &v);
}

/* ------------------------------------------------------------------ stock objects */
static void stock_fonts(void){
  static const struct { int id; const char *n; } S[] = {
    {OEM_FIXED_FONT, "OEM_FIXED_FONT"}, {ANSI_FIXED_FONT, "ANSI_FIXED_FONT"}, {ANSI_VAR_FONT, "ANSI_VAR_FONT"},
    {SYSTEM_FONT, "SYSTEM_FONT"}, {DEVICE_DEFAULT_FONT, "DEVICE_DEFAULT_FONT"}, {SYSTEM_FIXED_FONT, "SYSTEM_FIXED_FONT"},
    {DEFAULT_GUI_FONT, "DEFAULT_GUI_FONT"} };
  for (unsigned i = 0; i < sizeof S / sizeof *S; i++) {
    HGDIOBJ h = GetStockObject(S[i].id), h2 = GetStockObject(S[i].id);
    rk k; kctx(&k); rk_s(&k, "obj", S[i].n); rv v; rv_reset(&v);
    rv_i(&v, GetObjectType(h)); rv_i(&v, GetObjectW(h, 0, NULL)); rv_i(&v, h == h2); rv_i(&v, h == (HGDIOBJ)g_guifont);
    rec_row("stock", &k, &v);
    LOGFONTW lf; char s[64]; snprintf(s, sizeof s, "stock.%s", S[i].n);
    if (GetObjectW(h, sizeof lf, &lf) == (int)sizeof lf) { lf_row(s, &lf); ftm_row(s, (HFONT)h); } else lf_row(s, NULL);
  }
  ftm_row("dc_default", NULL);
}

/* ------------------------------------------------------------------ theme */
static void theme_info(void){
  rk k; rv v; kctx(&k); rv_reset(&v);
  rv_b(&v, pIsThemeActive != NULL, pIsThemeActive ? pIsThemeActive() : 0);
  rv_b(&v, pIsAppThemed != NULL, pIsAppThemed ? pIsAppThemed() : 0);
  if (pGetThemeAppProperties) rv_x(&v, pGetThemeAppProperties()); else rv_na(&v);
  BOOL c = 0; HRESULT hr = pDwmIsCompositionEnabled ? pDwmIsCompositionEnabled(&c) : E_FAIL; rv_b(&v, SUCCEEDED(hr), c);
  rec_row("theme", &k, &v);
  if (!pGetThemeSysSize) return;
  static const int ids[] = {SM_CXBORDER, SM_CYBORDER, SM_CXVSCROLL, SM_CYHSCROLL, SM_CXSIZE, SM_CYSIZE, SM_CXSMSIZE, SM_CYSMSIZE,
                            SM_CXMENUSIZE, SM_CYMENUSIZE, SM_CXPADDEDBORDER, 1201, 1202, 1203, 1204, 1205, 1206, 1207, 1208, 1209, 1210};
  HWND w = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, g_hi, NULL);
  HANDLE th = pOpenThemeData && w ? pOpenThemeData(w, L"WINDOW") : NULL;
  for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) for (int t = 0; t < 2; t++) {
    if (t && !th) continue;
    kctx(&k); rk_s(&k, "src", t ? "window" : "null"); rk_i(&k, "id", ids[i]); rv_reset(&v); rv_i(&v, pGetThemeSysSize(t ? th : NULL, ids[i]));
    rec_row("theme_sz", &k, &v);
  }
  if (pGetThemeSysFont) for (int id = 801; id <= 806; id++) {
    LOGFONTW lf; char s[48]; snprintf(s, sizeof s, "theme.null.%d", id);
    lf_row(s, SUCCEEDED(pGetThemeSysFont(NULL, id, &lf)) ? &lf : NULL);
    if (th) { snprintf(s, sizeof s, "theme.window.%d", id); lf_row(s, SUCCEEDED(pGetThemeSysFont(th, id, &lf)) ? &lf : NULL); }
  }
  if (th && pCloseThemeData) pCloseThemeData(th);
  static const struct { const WCHAR *cls; const char *cn; int part, state; } P[] = {
    {L"BUTTON", "BUTTON", 2, 1}, {L"BUTTON", "BUTTON", 3, 1}, {L"BUTTON", "BUTTON", 1, 1}, {L"COMBOBOX", "COMBOBOX", 1, 1},
    {L"SCROLLBAR", "SCROLLBAR", 1, 1}, {L"SCROLLBAR", "SCROLLBAR", 2, 1}, {L"SCROLLBAR", "SCROLLBAR", 3, 1}, {L"SCROLLBAR", "SCROLLBAR", 10, 1},
    {L"EDIT", "EDIT", 1, 1}, {L"HEADER", "HEADER", 1, 1}, {L"MENU", "MENU", 11, 1}, {L"MENU", "MENU", 12, 1}, {L"MENU", "MENU", 14, 1},
    {L"MENU", "MENU", 15, 1}, {L"WINDOW", "WINDOW", 1, 1}, {L"WINDOW", "WINDOW", 15, 1}, {L"WINDOW", "WINDOW", 17, 1}, {L"WINDOW", "WINDOW", 18, 1} };
  if (pOpenThemeData && pGetThemePartSize && w) {
    HDC dc = GetDC(NULL);
    for (unsigned i = 0; i < sizeof P / sizeof *P; i++) {
      HANDLE h = pOpenThemeData(w, P[i].cls);
      for (int ts = 0; ts < 3; ts++) {
        SIZE sz = {0, 0}; HRESULT hr2 = h ? pGetThemePartSize(h, dc, P[i].part, P[i].state, NULL, ts, &sz) : E_FAIL;
        kctx(&k); rk_s(&k, "cls", P[i].cn); rk_i(&k, "part", P[i].part); rk_i(&k, "state", P[i].state); rk_i(&k, "ts", ts);
        rv_reset(&v); rv_x(&v, (DWORD)hr2); rv_b(&v, SUCCEEDED(hr2), sz.cx); rv_b(&v, SUCCEEDED(hr2), sz.cy); rec_row("theme_part", &k, &v);
      }
      if (h && pCloseThemeData) pCloseThemeData(h);
    }
    ReleaseDC(NULL, dc);
  }
  if (w) DestroyWindow(w);
}

/* ------------------------------------------------------------------ AdjustWindowRectEx and real frames */
typedef struct { const char *n; DWORD s; int top; } stylespec;
static const stylespec STY[] = {
  {"overlappedwindow", WS_OVERLAPPEDWINDOW, 1}, {"caption", WS_CAPTION, 1}, {"caption_sysmenu", WS_CAPTION | WS_SYSMENU, 1},
  {"dialog", WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 1}, {"popup", WS_POPUP, 1}, {"popup_border", WS_POPUP | WS_BORDER, 1},
  {"popup_dlgframe", WS_POPUP | WS_DLGFRAME, 1}, {"popup_thickframe", WS_POPUP | WS_THICKFRAME, 1},
  {"caption_thickframe", WS_CAPTION | WS_THICKFRAME, 1}, {"overlapped_scroll", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL, 1},
  {"child", WS_CHILD, 0}, {"child_border", WS_CHILD | WS_BORDER, 0}, {"child_thickframe", WS_CHILD | WS_THICKFRAME, 0},
  {"child_caption", WS_CHILD | WS_CAPTION, 0}, {"child_dlgframe", WS_CHILD | WS_DLGFRAME, 0} };
static const struct { const char *n; DWORD x; } EXS[] = {
  {"0", 0}, {"windowedge", WS_EX_WINDOWEDGE}, {"clientedge", WS_EX_CLIENTEDGE}, {"staticedge", WS_EX_STATICEDGE},
  {"dlgmodalframe", WS_EX_DLGMODALFRAME}, {"toolwindow", WS_EX_TOOLWINDOW}, {"overlappedwindow", WS_EX_OVERLAPPEDWINDOW},
  {"palettewindow", WS_EX_PALETTEWINDOW}, {"dlgmodal_clientedge", WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE} };
#define NSTY ((int)(sizeof STY / sizeof *STY))
#define NEXS ((int)(sizeof EXS / sizeof *EXS))

static void awr_one(const char *table, int dpi, int si, int ei, int menu){
  RECT r = {0, 0, 100, 100}; BOOL ok;
  if (dpi) ok = pAdjustWindowRectExForDpi(&r, STY[si].s, menu, EXS[ei].x, dpi); else ok = AdjustWindowRectEx(&r, STY[si].s, menu, EXS[ei].x);
  rk k; if (dpi) { rk_reset(&k); rk_i(&k, "dpi", dpi); } else kctx(&k);
  rk_s(&k, "style", STY[si].n); rk_s(&k, "ex", EXS[ei].n); rk_i(&k, "menu", menu);
  rv v; rv_reset(&v); rv_i(&v, ok ? 1 : 0);
  if (ok) { rv_i(&v, -r.left); rv_i(&v, -r.top); rv_i(&v, r.right - 100); rv_i(&v, r.bottom - 100); } else rv_rect_na(&v);
  rec_row(table, &k, &v);
}
static void awr_all(int with_dpi){
  for (int si = 0; si < NSTY; si++) for (int ei = 0; ei < NEXS; ei++) for (int m = 0; m < 2; m++) awr_one("awr", 0, si, ei, m);
  if (!with_dpi || !pAdjustWindowRectExForDpi) return;
  static const int df[] = {96, 120, 144, 168, 192}, dq[] = {96, 144};
  const int *d = g_full ? df : dq; int nd = g_full ? 5 : 2;
  for (int j = 0; j < nd; j++) for (int si = 0; si < NSTY; si++) for (int ei = 0; ei < NEXS; ei++) for (int m = 0; m < 2; m++) awr_one("awr_dpi", d[j], si, ei, m);
}

static const WCHAR UMCLS[] = L"um_wnd";
static void real_windows(void){
  HWND parent = CreateWindowExW(0, UMCLS, L"parent", WS_POPUP, 0, 0, 600, 500, NULL, NULL, g_hi, NULL);
  for (int si = 0; si < NSTY; si++) for (int ei = 0; ei < NEXS; ei++) for (int m = 0; m < 2; m++) {
    if (m && !STY[si].top) continue;
    HMENU hm = NULL; if (m) { hm = CreateMenu(); AppendMenuW(hm, MF_STRING, 100, L"&File"); }
    HWND w = CreateWindowExW(EXS[ei].x, UMCLS, L"Win", STY[si].s, 10, 10, 300, 200, STY[si].top ? NULL : parent,
                             STY[si].top ? hm : (HMENU)(INT_PTR)1, g_hi, NULL);
    rk k; kctx(&k); rk_s(&k, "style", STY[si].n); rk_s(&k, "ex", EXS[ei].n); rk_i(&k, "menu", m);
    rv v; rv_reset(&v); rv_i(&v, w != NULL);
    if (w) {
      RECT wr, cr; POINT p = {0, 0}; GetWindowRect(w, &wr); GetClientRect(w, &cr); ClientToScreen(w, &p);
      rv_x(&v, (DWORD)GetWindowLongW(w, GWL_STYLE)); rv_x(&v, (DWORD)GetWindowLongW(w, GWL_EXSTYLE));
      rv_wh(&v, &wr); rv_wh(&v, &cr); rv_i(&v, p.x - wr.left); rv_i(&v, p.y - wr.top);
      DestroyWindow(w);
    } else for (int i = 0; i < 8; i++) rv_na(&v);
    if (hm && !w) DestroyMenu(hm);
    rec_row("win", &k, &v);
  }
  if (parent) DestroyWindow(parent);
}

/* ------------------------------------------------------------------ menus */
static void menubar(int width, int shown){
  static const WCHAR *items[] = {L"&File", L"&Edit", L"&View", L"&Insert", L"F&ormat", L"&Tools", L"&Window", L"&Help"};
  HMENU m = CreateMenu();
  for (int i = 0; i < 8; i++) { HMENU p = CreatePopupMenu(); AppendMenuW(p, MF_STRING, 200 + i, L"Item"); AppendMenuW(m, MF_POPUP, (UINT_PTR)p, items[i]); }
  HWND w = CreateWindowExW(0, UMCLS, L"Menu", WS_OVERLAPPEDWINDOW, 20, 20, width, 300, NULL, m, g_hi, NULL);
  rk k; kctx(&k); rk_i(&k, "width", width); rk_i(&k, "shown", shown);
  rv v; rv_reset(&v); rv_i(&v, w != NULL);
  if (!w) { DestroyMenu(m); for (int i = 0; i < 11; i++) rv_na(&v); rec_row("menu", &k, &v); return; }
  if (shown) { ShowWindow(w, SW_SHOWNOACTIVATE); UpdateWindow(w); }
  RECT wr, cr; POINT p = {0, 0}; GetWindowRect(w, &wr); GetClientRect(w, &cr); ClientToScreen(w, &p);
  rv_wh(&v, &wr); rv_wh(&v, &cr); rv_i(&v, p.y - wr.top);
  MENUBARINFO mb; memset(&mb, 0, sizeof mb); mb.cbSize = sizeof mb;
  BOOL ok = GetMenuBarInfo(w, OBJID_MENU, 0, &mb);
  if (ok) { RECT r = mb.rcBar; OffsetRect(&r, -wr.left, -wr.top); rv_rect(&v, &r); } else rv_rect_na(&v);
  rv_i(&v, GetMenuItemCount(m));
  /* rows = distinct item tops */
  int tops[16], nt = 0; RECT ir[8]; int okr[8];
  for (int i = 0; i < 8; i++) {
    okr[i] = GetMenuItemRect(w, m, i, &ir[i]);
    if (okr[i]) { OffsetRect(&ir[i], -wr.left, -wr.top); int f = 0; for (int j = 0; j < nt; j++) if (tops[j] == ir[i].top) f = 1; if (!f && nt < 16) tops[nt++] = ir[i].top; }
  }
  rv_i(&v, nt);
  rec_row("menu", &k, &v);
  for (int i = 0; i < 8; i++) {
    kctx(&k); rk_i(&k, "width", width); rk_i(&k, "shown", shown); rk_i(&k, "item", i); rv_reset(&v);
    if (okr[i]) rv_rect(&v, &ir[i]); else rv_rect_na(&v);
    rec_row("menu_item", &k, &v);
  }
  DestroyWindow(w);
}

static HMENU g_pop; static HWND g_popowner; static int g_popdone;
static void CALLBACK pop_timer(HWND h, UINT msg, UINT_PTR id, DWORD t){
  (void)msg; (void)t;
  KillTimer(h, id);
  if (id == 2 || g_popdone) { EndMenu(); return; }
  g_popdone = 1;
  HWND pw = NULL;   /* the visible popup menu window (class #32768) */
  for (HWND c = FindWindowExW(NULL, NULL, L"#32768", NULL); c; c = FindWindowExW(NULL, c, L"#32768", NULL))
    if (IsWindowVisible(c)) { pw = c; break; }
  RECT pr = {0, 0, 0, 0}; if (pw) GetWindowRect(pw, &pr);
  int n = GetMenuItemCount(g_pop);
  rk k; kctx(&k); rv v; rv_reset(&v); rv_i(&v, pw != NULL); rv_wh(&v, &pr); rv_i(&v, n);
  LONG cm = GetMenuCheckMarkDimensions(); rv_i(&v, LOWORD(cm)); rv_i(&v, HIWORD(cm));
  rv_b(&v, pw != NULL, pr.left - 100); rv_b(&v, pw != NULL, pr.top - 100);
  rec_row("popup", &k, &v);
  for (int i = 0; i < n; i++) {
    RECT r; kctx(&k); rk_i(&k, "item", i); rv_reset(&v);
    if (GetMenuItemRect(g_popowner, g_pop, i, &r)) { rv_i(&v, r.left - pr.left); rv_i(&v, r.top - pr.top); rv_wh(&v, &r); } else rv_rect_na(&v);
    rec_row("popup_item", &k, &v);
  }
  EndMenu();
}
static void popup_menu(void){
  HWND w = CreateWindowExW(0, UMCLS, L"Popup owner", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL, NULL, g_hi, NULL);
  if (!w) return;
  ShowWindow(w, SW_SHOW); SetForegroundWindow(w); UpdateWindow(w);
  HMENU p = CreatePopupMenu(), sub = CreatePopupMenu();
  AppendMenuW(sub, MF_STRING, 301, L"file1.txt");
  AppendMenuW(p, MF_STRING, 101, L"&Open\tCtrl+O");
  AppendMenuW(p, MF_STRING, 102, L"Save &As...");
  AppendMenuW(p, MF_SEPARATOR, 0, NULL);
  AppendMenuW(p, MF_STRING | MF_CHECKED, 103, L"&Word Wrap");
  AppendMenuW(p, MF_STRING, 104, L"&Font...");
  AppendMenuW(p, MF_POPUP, (UINT_PTR)sub, L"&Recent");
  AppendMenuW(p, MF_STRING | MF_GRAYED, 105, L"Disabled");
  AppendMenuW(p, MF_STRING, 106, L"&Default item");
  AppendMenuW(p, MF_STRING, 107, L"A rather long menu item text here");
  SetMenuDefaultItem(p, 106, FALSE);
  g_pop = p; g_popowner = w; g_popdone = 0;
  SetTimer(w, 1, 400, pop_timer); SetTimer(w, 2, 4000, pop_timer);
  rec_flush();
  TrackPopupMenuEx(p, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, 100, 100, w, NULL);
  KillTimer(w, 1); KillTimer(w, 2);
  if (!g_popdone) rec_note("popup: timer did not run inside TrackPopupMenuEx");
  DestroyMenu(p); DestroyWindow(w);
}

/* ------------------------------------------------------------------ dialogs */
typedef struct { const char *id; WORD atom; DWORD style; short x, y, cx, cy; const WCHAR *text; } ctlspec;
static const ctlspec CTL[] = {
  {"btn_def_50x14", 0x80, BS_DEFPUSHBUTTON | WS_TABSTOP, 7, 7, 50, 14, L"OK"},
  {"btn_50x14", 0x80, BS_PUSHBUTTON | WS_TABSTOP, 61, 7, 50, 14, L"Cancel"},
  {"btn_40x12", 0x80, BS_PUSHBUTTON, 115, 7, 40, 12, L"&Apply"},
  {"btn_50x23", 0x80, BS_PUSHBUTTON, 159, 7, 50, 23, L"Big"},
  {"chk_80x10", 0x80, BS_AUTOCHECKBOX | WS_TABSTOP, 7, 26, 80, 10, L"&Check box"},
  {"rad_80x10", 0x80, BS_AUTORADIOBUTTON, 7, 38, 80, 10, L"&Radio"},
  {"chk3_80x8", 0x80, BS_AUTO3STATE, 7, 50, 80, 8, L"Three state"},
  {"grp_100x40", 0x80, BS_GROUPBOX, 95, 26, 100, 40, L"Group"},
  {"edt_100x14", 0x81, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 7, 64, 100, 14, L"Edit"},
  {"edt_100x12", 0x81, ES_AUTOHSCROLL | WS_BORDER, 7, 80, 100, 12, L"Edit"},
  {"edt_nb_100x12", 0x81, ES_AUTOHSCROLL, 7, 94, 100, 12, L"Edit"},
  {"edt_ml_100x40", 0x81, ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | WS_BORDER, 7, 108, 100, 40, L"Multi"},
  {"stc_l_40x8", 0x82, SS_LEFT, 115, 70, 40, 8, L"&Name:"},
  {"stc_r_40x8", 0x82, SS_RIGHT, 115, 80, 40, 8, L"Size:"},
  {"stc_c_40x10", 0x82, SS_CENTER, 160, 70, 40, 10, L"Center"},
  {"stc_etch_100x1", 0x82, SS_ETCHEDHORZ, 115, 92, 100, 1, L""},
  {"stc_sunk_40x12", 0x82, SS_SUNKEN | SS_LEFT, 205, 70, 40, 12, L"Sunken"},
  {"cb_dl_100x100", 0x85, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 115, 96, 100, 100, L""},
  {"cb_dd_100x100", 0x85, CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 115, 112, 100, 100, L""},
  {"cb_s_100x60", 0x85, CBS_SIMPLE | WS_VSCROLL, 115, 128, 100, 60, L""},
  {"lb_80x30", 0x83, LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 7, 152, 80, 30, L""},
  {"lb_80x41", 0x83, LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 220, 100, 80, 41, L""},
  {"lb_ni_80x30", 0x83, LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER, 220, 150, 80, 30, L""} };
#define NCTL ((int)(sizeof CTL / sizeof *CTL))
#define DLG_CX 310
#define DLG_CY 190

typedef struct { const char *name; DWORD bits; int font; } dlgstyle;
static const dlgstyle DST[] = {
  {"setfont", DS_SETFONT, 1}, {"shellfont", DS_SHELLFONT, 1}, {"nofont", 0, 0}, {"fixedsys", DS_FIXEDSYS, 0},
  {"setfont_7fff", DS_SETFONT, 2} };
typedef struct { const char *name; DWORD style; DWORD ex; } dlgframe;
static const dlgframe DFR[] = {
  {"modal", WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 0},
  {"popup", WS_POPUP, 0},
  {"popup_border", WS_POPUP | WS_BORDER, 0},
  {"thick", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 0},
  {"modal_3d_ctx", WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_3DLOOK | DS_CONTEXTHELP, 0},
  {"tool", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_TOOLWINDOW},
  {"child_control", WS_CHILD | DS_CONTROL, WS_EX_CONTROLPARENT} };

typedef struct { DWORD b[4096]; int n; } tbuf;   /* n in bytes */
static void tb_align(tbuf *t, int a){ while (t->n % a) ((BYTE *)t->b)[t->n++] = 0; }
static void tb_w(tbuf *t, WORD w){ memcpy((BYTE *)t->b + t->n, &w, 2); t->n += 2; }
static void tb_d(tbuf *t, DWORD d){ memcpy((BYTE *)t->b + t->n, &d, 4); t->n += 4; }
static void tb_b(tbuf *t, BYTE b){ ((BYTE *)t->b)[t->n++] = b; }
static void tb_s(tbuf *t, const WCHAR *s){ do tb_w(t, *s); while (*s++); }

static void build_dlg(tbuf *t, int ex, DWORD style, DWORD exstyle, int font, WORD pt, WORD wt, BYTE it, BYTE cs, const WCHAR *face){
  t->n = 0;
  if (ex) { tb_w(t, 1); tb_w(t, 0xFFFF); tb_d(t, 0); tb_d(t, exstyle); tb_d(t, style); }
  else { tb_d(t, style); tb_d(t, exstyle); }
  tb_w(t, NCTL); tb_w(t, 0); tb_w(t, 0); tb_w(t, DLG_CX); tb_w(t, DLG_CY);
  tb_w(t, 0); tb_w(t, 0); tb_s(t, L"Probe");
  if (style & DS_SETFONT) {
    tb_w(t, font == 2 ? 0x7FFF : pt);
    if (ex) { tb_w(t, wt); tb_b(t, it); tb_b(t, cs); }
    tb_s(t, face);
  }
  for (int i = 0; i < NCTL; i++) {
    tb_align(t, 4);
    DWORD cst = CTL[i].style | WS_CHILD | WS_VISIBLE;
    if (ex) { tb_d(t, 0); tb_d(t, 0); tb_d(t, cst); } else { tb_d(t, cst); tb_d(t, 0); }
    tb_w(t, CTL[i].x); tb_w(t, CTL[i].y); tb_w(t, CTL[i].cx); tb_w(t, CTL[i].cy);
    if (ex) tb_d(t, 1000 + i); else tb_w(t, 1000 + i);
    tb_w(t, 0xFFFF); tb_w(t, CTL[i].atom); tb_s(t, CTL[i].text); tb_w(t, 0);
  }
}

static INT_PTR CALLBACK dlgproc(HWND h, UINT m, WPARAM w, LPARAM l){ (void)h; (void)w; (void)l; return m == WM_INITDIALOG; }

typedef struct { const char *face; int pt; int wt; int it; } dlgfont;
static void dlg_key(rk *k, int ex, const dlgstyle *ds, const dlgfont *df, const dlgframe *fr){
  kctx(k); rk_s(k, "tmpl", ex ? "ex" : "std"); rk_s(k, "style", ds->name);
  if (ds->font == 1) { rk_s(k, "face", df->face); rk_i(k, "pt", df->pt); rk_i(k, "wt", df->wt); rk_i(k, "it", df->it); }
  else if (ds->font == 2) { rk_s(k, "face", df->face); rk_s(k, "pt", "0x7fff"); rk_i(k, "wt", df->wt); rk_i(k, "it", df->it); }
  else { rk_s(k, "face", "~"); rk_s(k, "pt", "~"); rk_s(k, "wt", "~"); rk_s(k, "it", "~"); }
  rk_s(k, "frame", fr->name);
}

static int font_kind(HFONT f, HFONT dlgf){ if (!f) return 0; if (f == dlgf) return 1; if (f == g_guifont) return 2; return 3; }

static void one_dialog(int ex, const dlgstyle *ds, const dlgfont *df, const dlgframe *fr, HWND parent, int controls){
  static tbuf t; WCHAR face[LF_FACESIZE]; MultiByteToWideChar(CP_UTF8, 0, df->face, -1, face, LF_FACESIZE);
  DWORD style = fr->style | ds->bits;
  build_dlg(&t, ex, style, fr->ex, ds->font, (WORD)df->pt, (WORD)df->wt, (BYTE)df->it, DEFAULT_CHARSET, face);
  SetLastError(0);
  HWND d = CreateDialogIndirectParamW(g_hi, (LPCDLGTEMPLATEW)t.b, (fr->style & WS_CHILD) ? parent : NULL, dlgproc, 0);
  DWORD gle = GetLastError();
  rk k; dlg_key(&k, ex, ds, df, fr);
  rv v; rv_reset(&v); rv_i(&v, d != NULL); rv_i(&v, d ? 0 : (long long)gle);
  if (!d) { for (int i = 0; i < 15; i++) rv_na(&v); rec_row("dlg", &k, &v); return; }
  RECT wr, cr; GetWindowRect(d, &wr); GetClientRect(d, &cr);
  rv_x(&v, (DWORD)GetWindowLongW(d, GWL_STYLE)); rv_x(&v, (DWORD)GetWindowLongW(d, GWL_EXSTYLE));
  rv_wh(&v, &wr); rv_wh(&v, &cr);
  RECT b = {0, 0, 4, 8}; BOOL okb = MapDialogRect(d, &b); rv_b(&v, okb, b.right); rv_b(&v, okb, b.bottom);
  RECT c = {0, 0, 100, 100}; BOOL okc = MapDialogRect(d, &c); rv_b(&v, okc, c.right); rv_b(&v, okc, c.bottom);
  RECT e = {7, 7, 0, 0}; BOOL oke = MapDialogRect(d, &e); rv_b(&v, oke, e.left); rv_b(&v, oke, e.top);
  rv_b(&v, pGetDpiForWindow != NULL, pGetDpiForWindow ? pGetDpiForWindow(d) : 0);
  HFONT f = (HFONT)SendMessageW(d, WM_GETFONT, 0, 0);
  rv_i(&v, f == NULL); rv_i(&v, f == g_guifont);
  rec_row("dlg", &k, &v);
  /* font the dialog manager made: LOGFONT + realised metrics (key src = dialog key) */
  {
    char src[256];
    if (ds->font) snprintf(src, sizeof src, "dlg/%s/%s/%s/%s%d/%d/%d/%s", ex ? "ex" : "std", ds->name, df->face, ds->font == 2 ? "0x7fff:" : "",
                           df->pt, df->wt, df->it, fr->name);
    else snprintf(src, sizeof src, "dlg/%s/%s/%s", ex ? "ex" : "std", ds->name, fr->name);
    LOGFONTW lf;
    if (f && GetObjectW(f, sizeof lf, &lf) == (int)sizeof lf) { lf_row(src, &lf); ftm_row(src, f); } else lf_row(src, NULL);
  }
  if (controls) for (int i = 0; i < NCTL; i++) {
    HWND c2 = GetDlgItem(d, 1000 + i);
    rk kc; dlg_key(&kc, ex, ds, df, fr); rk_s(&kc, "cid", CTL[i].id);
    rv_reset(&v); rv_i(&v, c2 != NULL);
    if (!c2) { for (int j = 0; j < 13; j++) rv_na(&v); rec_row("ctl", &kc, &v); continue; }
    RECT r, rc; GetWindowRect(c2, &r); MapWindowPoints(NULL, d, (POINT *)&r, 2); GetClientRect(c2, &rc);
    rv_x(&v, (DWORD)GetWindowLongW(c2, GWL_STYLE)); rv_x(&v, (DWORD)GetWindowLongW(c2, GWL_EXSTYLE));
    rv_i(&v, r.left); rv_i(&v, r.top); rv_wh(&v, &r); rv_wh(&v, &rc);
    rv_i(&v, font_kind((HFONT)SendMessageW(c2, WM_GETFONT, 0, 0), f));
    rv_i(&v, CTL[i].x); rv_i(&v, CTL[i].y); rv_i(&v, CTL[i].cx); rv_i(&v, CTL[i].cy);
    rec_row("ctl", &kc, &v);
    if (CTL[i].atom == 0x81) {
      DWORD mg = (DWORD)SendMessageW(c2, EM_GETMARGINS, 0, 0); RECT er = {0, 0, 0, 0}; SendMessageW(c2, EM_GETRECT, 0, (LPARAM)&er);
      rv_reset(&v); rv_i(&v, LOWORD(mg)); rv_i(&v, HIWORD(mg)); rv_rect(&v, &er); rec_row("ctl_edit", &kc, &v);
    } else if (CTL[i].atom == 0x85) {
      LRESULT a = SendMessageW(c2, CB_GETITEMHEIGHT, (WPARAM)-1, 0), b2 = SendMessageW(c2, CB_GETITEMHEIGHT, 0, 0);
      RECT dr = {0, 0, 0, 0}; SendMessageW(c2, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&dr);
      COMBOBOXINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci; BOOL okci = GetComboBoxInfo(c2, &ci);
      rv_reset(&v); rv_b(&v, a != CB_ERR, a); rv_b(&v, b2 != CB_ERR, b2); rv_wh(&v, &dr);
      if (okci) { rv_rect(&v, &ci.rcItem); rv_rect(&v, &ci.rcButton); } else { rv_rect_na(&v); rv_rect_na(&v); }
      rec_row("ctl_cb", &kc, &v);
    } else if (CTL[i].atom == 0x83) {
      LRESULT a = SendMessageW(c2, LB_GETITEMHEIGHT, 0, 0);
      rv_reset(&v); rv_b(&v, a != LB_ERR, a); rec_row("ctl_lb", &kc, &v);
    } else if (CTL[i].atom == 0x80 && (CTL[i].style & 0xF) <= BS_DEFPUSHBUTTON) {
      SIZE is = {0, 0}; BOOL oki = (BOOL)SendMessageW(c2, BCM_GETIDEALSIZE, 0, (LPARAM)&is);
      RECT tm = {0, 0, 0, 0}; BOOL okt = (BOOL)SendMessageW(c2, BCM_GETTEXTMARGIN, 0, (LPARAM)&tm);
      rv_reset(&v); rv_b(&v, oki, is.cx); rv_b(&v, oki, is.cy);
      if (okt) rv_rect(&v, &tm); else rv_rect_na(&v);
      rec_row("ctl_btn", &kc, &v);
    }
  }
  DestroyWindow(d);
}

static const dlgfont FONTS_FULL[] = {
  {"MS Shell Dlg", 8, 0, 0}, {"MS Shell Dlg 2", 8, 0, 0}, {"MS Sans Serif", 8, 0, 0}, {"Microsoft Sans Serif", 8, 0, 0},
  {"Tahoma", 8, 0, 0}, {"Segoe UI", 8, 0, 0}, {"Segoe UI", 9, 0, 0}, {"MS Shell Dlg", 9, 0, 0}, {"MS Shell Dlg 2", 9, 0, 0},
  {"Tahoma", 9, 0, 0}, {"MS Shell Dlg", 10, 0, 0}, {"MS Shell Dlg 2", 10, 0, 0}, {"Tahoma", 10, 0, 0}, {"Segoe UI", 10, 0, 0},
  {"Arial", 8, 0, 0}, {"Arial", 10, 0, 0}, {"Courier New", 10, 0, 0}, {"Courier", 10, 0, 0}, {"System", 10, 0, 0},
  {"Verdana", 8, 0, 0}, {"Segoe UI", 12, 0, 0}, {"MS Shell Dlg", 12, 0, 0}, {"Microsoft Sans Serif", 12, 0, 0}, {"NoSuchFontXyz", 8, 0, 0},
  {"Segoe UI", 9, 700, 0}, {"MS Shell Dlg 2", 8, 700, 0}, {"Segoe UI", 9, 400, 1} };
static const dlgfont FONTS_QUICK[] = { {"MS Shell Dlg", 8, 0, 0}, {"MS Shell Dlg 2", 8, 0, 0}, {"Segoe UI", 9, 0, 0}, {"Tahoma", 8, 0, 0} };
static const dlgfont FONTS_CTX[] = { {"MS Shell Dlg", 8, 0, 0}, {"Segoe UI", 9, 0, 0} };
static const dlgfont FONT_NONE = {"~", 0, 0, 0};

static void dialogs(int ctxsweep){
  HWND parent = CreateWindowExW(WS_EX_CONTROLPARENT, UMCLS, L"dlgparent", WS_POPUP, 0, 0, 800, 600, NULL, NULL, g_hi, NULL);
  const dlgfont *F; int nf;
  if (ctxsweep) { F = FONTS_CTX; nf = 2; } else if (g_full) { F = FONTS_FULL; nf = (int)(sizeof FONTS_FULL / sizeof *FONTS_FULL); }
  else { F = FONTS_QUICK; nf = 4; }
  for (int ex = 0; ex < 2; ex++) {
    if (ctxsweep && ex) break;
    for (int s = 0; s < 2; s++) for (int i = 0; i < nf; i++) {
      if (!ex && F[i].wt) continue;             /* weight / italic only exist in DLGTEMPLATEEX */
      one_dialog(ex, &DST[s], &F[i], &DFR[0], parent, 1);
    }
    one_dialog(ex, &DST[2], &FONT_NONE, &DFR[0], parent, 1);
    one_dialog(ex, &DST[3], &FONT_NONE, &DFR[0], parent, 1);
    if (!ctxsweep) one_dialog(ex, &DST[4], &F[0], &DFR[0], parent, 1);
  }
  if (!ctxsweep) {
    /* frames: MS Shell Dlg 8 DS_SHELLFONT (std) under every frame style; also nofont */
    int nfr = g_full ? (int)(sizeof DFR / sizeof *DFR) : 2;
    for (int fr = 1; fr < nfr; fr++) {
      one_dialog(0, &DST[1], &F[0], &DFR[fr], parent, 0);
      one_dialog(0, &DST[2], &FONT_NONE, &DFR[fr], parent, 0);
    }
  }
  if (parent) DestroyWindow(parent);
}

/* ------------------------------------------------------------------ message boxes */
static HHOOK g_mbhook; static const char *g_mbcase; static int g_mbseen;
static void mb_record(HWND h){
  RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr);
  int nch = 0; for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) nch++;
  rk k; kctx(&k); rk_s(&k, "case", g_mbcase);
  rv v; rv_reset(&v); rv_i(&v, 1);
  rv_i(&v, wr.left); rv_i(&v, wr.top); rv_wh(&v, &wr); rv_wh(&v, &cr); rv_i(&v, nch);
  rv_x(&v, (DWORD)GetWindowLongW(h, GWL_STYLE)); rv_x(&v, (DWORD)GetWindowLongW(h, GWL_EXSTYLE));
  rec_row("mb", &k, &v);
  int i = 0; HWND first_btn = NULL; int first_id = 0;
  for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT), i++) {
    WCHAR cls[64] = L"", txt[512] = L""; GetClassNameW(c, cls, 64); GetWindowTextW(c, txt, 512);
    RECT r; GetWindowRect(c, &r); MapWindowPoints(NULL, h, (POINT *)&r, 2);
    int id = GetDlgCtrlID(c);
    kctx(&k); rk_s(&k, "case", g_mbcase); rk_i(&k, "i", i);
    rv_reset(&v); rv_w(&v, cls); rv_i(&v, id); rv_i(&v, r.left); rv_i(&v, r.top); rv_wh(&v, &r);
    if (txt[0]) rv_w(&v, txt); else rv_s(&v, "");
    rec_row("mb_ch", &k, &v);
    if (!first_btn && !lstrcmpiW(cls, L"Button") && id != IDHELP) { first_btn = c; first_id = id; }
    if (id == 0xFFFF && !lstrcmpiW(cls, L"Static")) {
      HFONT f = (HFONT)SendMessageW(c, WM_GETFONT, 0, 0); LOGFONTW lf; char s[96]; snprintf(s, sizeof s, "mb.%s", g_mbcase);
      if (f && GetObjectW(f, sizeof lf, &lf) == (int)sizeof lf) { lf_row(s, &lf); ftm_row(s, f); } else lf_row(s, NULL);
    }
  }
  if (first_btn) PostMessageW(h, WM_COMMAND, MAKEWPARAM(first_id, BN_CLICKED), (LPARAM)first_btn);
  else PostMessageW(h, WM_CLOSE, 0, 0);
}
static LRESULT CALLBACK mb_cbt(int code, WPARAM w, LPARAM l){
  if (code == HCBT_ACTIVATE && !g_mbseen) {
    WCHAR c[16] = L""; GetClassNameW((HWND)w, c, 16);
    if (!lstrcmpW(c, L"#32770")) { g_mbseen = 1; mb_record((HWND)w); }
  }
  return CallNextHookEx(g_mbhook, code, w, l);
}
static void one_mb(const char *cs, const WCHAR *text, const WCHAR *cap, UINT type){
  g_mbcase = cs; g_mbseen = 0;
  g_mbhook = SetWindowsHookExW(WH_CBT, mb_cbt, NULL, GetCurrentThreadId());
  rec_flush();
  int r = pMessageBoxTimeoutW ? pMessageBoxTimeoutW(NULL, text, cap, type | MB_SETFOREGROUND, 0, 5000) : MessageBoxW(NULL, text, cap, type | MB_SETFOREGROUND);
  UnhookWindowsHookEx(g_mbhook);
  rk k; kctx(&k); rk_s(&k, "case", cs);
  rv v; rv_reset(&v); rv_i(&v, r); rv_i(&v, g_mbseen); rec_row("mb_ret", &k, &v);
}
static void msgboxes(int nmax){
  static WCHAR longnospace[301], longwords[1024];
  for (int i = 0; i < 300; i++) longnospace[i] = L'x';
  longnospace[300] = 0;
  longwords[0] = 0; for (int i = 0; i < 16; i++) wcscat(longwords, L"The quick brown fox jumps over the lazy dog. ");
  struct { const char *c; const WCHAR *t; const WCHAR *cap; UINT ty; } M[] = {
    {"ok_short", L"Hello", L"Title", MB_OK},
    {"ok_sentence", L"The quick brown fox jumps over the lazy dog.", L"Title", MB_OK},
    {"ok_info", L"The quick brown fox jumps over the lazy dog.", L"Title", MB_OK | MB_ICONINFORMATION},
    {"ok_error", L"Hello", L"Title", MB_OK | MB_ICONERROR},
    {"okcancel", L"Hello", L"Title", MB_OKCANCEL},
    {"yesno_question", L"Save changes?", L"Title", MB_YESNO | MB_ICONQUESTION},
    {"yesnocancel_warn", L"Save changes to the document before closing?", L"Notepad", MB_YESNOCANCEL | MB_ICONWARNING},
    {"abortretryignore", L"Hello", L"Title", MB_ABORTRETRYIGNORE},
    {"retrycancel", L"Hello", L"Title", MB_RETRYCANCEL},
    {"canceltrycontinue", L"Hello", L"Title", MB_CANCELTRYCONTINUE},
    {"ok_help", L"Hello", L"Title", MB_OK | MB_HELP},
    {"multiline", L"Line one\nLine two\nLine three", L"Title", MB_OK},
    {"crlf", L"First line\r\n\r\nThird line after an empty one", L"Title", MB_OK},
    {"long_nospace", longnospace, L"Title", MB_OK},
    {"long_words", longwords, L"Title", MB_OK},
    {"empty", L"", L"Title", MB_OK},
    {"long_caption", L"Hi", L"A very long caption text that is much wider than the message text itself", MB_OK},
    {"tab", L"Name:\tValue\nOther:\tX", L"Title", MB_OK},
    {"right", L"Hello world", L"Title", MB_OK | MB_RIGHT} };
  int n = (int)(sizeof M / sizeof *M); if (nmax < n) n = nmax;
  for (int i = 0; i < n; i++) one_mb(M[i].c, M[i].t, M[i].cap, M[i].ty);
}

/* ------------------------------------------------------------------ context sweep */
static void per_context(int full_set){
  HWND pw = CreateWindowExW(0, UMCLS, L"dpiprobe", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, g_hi, NULL);
  dpi_info(pw);
  if (pw) DestroyWindow(pw);
  /* gdi32full!GetStockObject picks DEFAULT_GUI_FONT by the thread's DPI and SYSTEM/OEM/SYSTEM_FIXED by its awareness */
  g_guifont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  stock_fonts();
  sysmetrics();
  dbu_row();
  spi_struct(full_set);
  if (full_set) spi_scalar();
  awr_all(full_set);
  real_windows();
  dialogs(!full_set);
  if (full_set) { menubar(400, 0); menubar(160, 0); menubar(90, 0); }
  else menubar(400, 0);
}

static LRESULT CALLBACK umproc(HWND h, UINT m, WPARAM w, LPARAM l){ return DefWindowProcW(h, m, w, l); }

int main(int argc, char **argv){
  rec_init(&argc, argv, PROBE, PROBE_VER);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--quick")) g_full = 0;
    else if (!strcmp(argv[i], "--full")) g_full = 1;
    else if (!strcmp(argv[i], "--no-msgbox")) g_msgbox = 0;
    else if (!strcmp(argv[i], "--no-popup")) g_popup = 0;
    else rec_note("unknown argument %s", argv[i]);
  }
  g_hi = GetModuleHandleW(NULL);
  load_apis();
  INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES}; InitCommonControlsEx(&icc);
  WNDCLASSW wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = umproc; wc.hInstance = g_hi; wc.lpszClassName = UMCLS;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
  g_guifont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  rec_env("um.mode", "%s", g_full ? "full" : "quick");
  {
    /* PE subsystem version of this image (user32 behaves differently for < 6.0 images, e.g. the padded border) */
    BYTE *base = (BYTE *)g_hi; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    rec_env("um.subsystem_version", "%u.%u", nt->OptionalHeader.MajorSubsystemVersion, nt->OptionalHeader.MinorSubsystemVersion);
  }
  schemas();

  /* 1. process default context: everything */
  g_ctx = "proc";
  per_context(1);
  sysmetrics_dpi();
  theme_info();
  if (g_full) { menubar(400, 1); menubar(160, 1); }
  rec_flush();
  if (g_msgbox) msgboxes(g_full ? 100 : 3);
  if (g_popup && g_full) popup_menu();

  /* 2. thread DPI-awareness contexts */
  if (g_full && pSetThreadDpiAwarenessContext) {
    static const struct { const char *n; INT_PTR v; } C[] = {
      {"unaware", -1}, {"system", -2}, {"pm", -3}, {"pm2", -4}, {"gdiscaled", -5} };
    for (unsigned i = 0; i < sizeof C / sizeof *C; i++) {
      dctx old = pSetThreadDpiAwarenessContext((dctx)C[i].v);
      if (!old) { rec_note("SetThreadDpiAwarenessContext(%s) failed gle=%lu", C[i].n, GetLastError()); continue; }
      g_ctx = C[i].n;
      per_context(0);
      if (g_msgbox && (C[i].v == -1 || C[i].v == -4)) msgboxes(4);
      rec_flush();
      pSetThreadDpiAwarenessContext(old);
    }
    g_ctx = "proc"; g_guifont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  }
  rec_note("ui_metrics done");
  return rec_finish(0);
}
