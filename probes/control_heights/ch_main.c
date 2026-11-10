/* control_heights: text-derived geometry of real user32 / comctl32 controls, per UI font and theme state.
 *
 * Answers the D12 control questions (docs/research/01 section 3): row heights (19/17), text heights (15/13),
 * header height (24), and the same for every common control, plus every input on the way (TEXTMETRIC of
 * each font, "..." extent, system metrics, theme part sizes and margins).
 *
 * Everything is measured on real controls that are children of one hidden top-level window owned by this
 * process; every control, image list and font is destroyed again. Nothing is shown except a popup menu
 * (TrackPopupMenu, dismissed by a timer) and message boxes (dismissed by a timer), see ch_modal.c.
 *
 * Conditions: the process conditions (DPI, awareness, comctl32 5.82 vs 6 by manifest variant) are in REC/ENV.
 * Per row: `font` (a name from the font list below; `default` = no WM_SETFONT), `theme` (`app` = whatever the
 * process gets; `off` = SetWindowTheme(h, L"", L"") on the control, only with comctl32 >= 6), and a per-family
 * `cfg` (style / image list / caption combination). Units are device pixels of this process's DPI context.
 *
 * usage: control_heights.exe [recorder options] [--quick | --full] [--only fam,fam] [--no-modal]
 *   --full (default): all fonts and configurations. --quick: 5 fonts, a subset of configurations.
 *   --only: families lv,hdr,tv,ctl,bar,modal,font,sys,theme.   --no-modal: skip menus and message boxes.
 *
 * Wine plumbing: the default headless prefix has no display driver (CreateWindowEx fails, error 1400). Use a copy of
 * the base prefix with HKCU\Software\Wine\Drivers Graphics=null and pass it as TP_BASE_PREFIX to tools/wine_probe.sh.
 *
 * Tables (keys before |; all lengths px; "!" = call failed or the control left the field untouched):
 *   sys          name | value                 system metrics, dialog base units, NONCLIENTMETRICS sizes, theming state
 *   font         font | kind req_h lf_* got_face h a d il el ave max wt ovh pf cs dots_cx/cy ("..." extent: header
 *                text cy) alpha_cx/cy (52 letters) ag_cy gcd_cx/cy (GdiGetCharDimensions);  H font_cw_ascii
 *   theme_part   cls part state ts | hr cx cy  (GetThemePartSize, ts min/true/draw); theme_margin, theme_int
 *   ctlfont      ctl theme | null face lf_h lf_wt tm_h tm_il   (font a control uses when it gets no WM_SETFONT)
 *   lv           font theme view cfg | item 0 LVIR_BOUNDS, row tops 1/2, row_h, LABEL, ICON, SELECTBOUNDS, subitem 1,
 *                GETITEMSPACING (large/small), APPROXIMATEVIEWRECT(3), GETVIEWRECT, COUNTPERPAGE, GETSTRINGWIDTH,
 *                header visible / window height / HDM_LAYOUT cy
 *   lv_auto      font theme cfg str | LVSCW_AUTOSIZE and _USEHEADER widths (col 2 = last column fill)
 *   hdr          font theme cfg | HDM_LAYOUT result (x y cx cy flags, prc top), heights, HDM_GETITEMRECT, bitmap margin
 *   tv           font theme cfg | TVM_GETITEMHEIGHT, indent, item rects (full / text), visible count, state image size
 *   btn          font theme cfg cap | BCM_GETIDEALSIZE (cx preset 0 and 100), BCM_GETTEXTMARGIN, DrawText extent
 *   edit         font theme cfg | window/client size, EM_GETRECT, EM_GETMARGINS, EM_POSFROMCHAR (line 2 for multi)
 *   cb / cbex    font theme cfg | closed height, CB_GETITEMHEIGHT -1 / 0, COMBOBOXINFO, dropped rect, list item height
 *   lb           font theme cfg | LB_GETITEMHEIGHT, item rects, window/client height
 *   tt           font theme cfg txt | TTM_GETBUBBLESIZE, TTM_GETMARGIN, TTM_ADJUSTRECT
 *   tb           font theme cfg | TB_GETBUTTONSIZE, TB_GETPADDING, item rects, height, TB_GETMAXSIZE, rows, TB_GETMETRICS
 *   sb / rb / tab  status bar, rebar band, tab control geometry
 *   trk / ud / pb  theme cfg | trackbar thumb/channel, up-down vs buddy, progress (font-independent)
 *   ink          font theme cfg ctl | WM_PRINT render: dark-pixel bbox (grey < 128) and count;  H ink_img (8bpp grey)
 *   menubar(_win), menupop(_win), msgbox, msgbox_ctl   menus and MessageBoxW layout (NONCLIENTMETRICS fonts)
 */
#include "ch.h"

int g_full = 1, g_cc6 = 0, g_dpi = 96;
HWND g_top; HINSTANCE g_hi; HDC g_dc;
const char *g_themes[2] = {"app", "off"}; int g_nthemes = 1;
static const char *g_only = NULL;
static int g_modal = 1;

/* ---- the font list -------------------------------------------------------------------------- */
static fspec FONTS[] = {
  /* name           kind face                    px  pt stock               ncm quick ctl */
  {"default",       0, NULL,                     0,  0, 0,                  0,  1, 1},
  {"segoe-12",      1, "Segoe UI",             -12,  0, 0,                  0,  1, 1},
  {"segoe9pt",      2, "Segoe UI",               0,  9, 0,                  0,  0, 1},
  {"tahoma-11",     1, "Tahoma",               -11,  0, 0,                  0,  0, 1},
  {"tahoma8pt",     2, "Tahoma",                 0,  8, 0,                  0,  0, 1},
  {"shelldlg-11",   1, "MS Shell Dlg",         -11,  0, 0,                  0,  1, 1},
  {"shelldlg-12",   1, "MS Shell Dlg",         -12,  0, 0,                  0,  0, 1},
  {"shelldlg8pt",   2, "MS Shell Dlg",           0,  8, 0,                  0,  0, 1},
  {"shelldlg2-11",  1, "MS Shell Dlg 2",       -11,  0, 0,                  0,  0, 1},
  {"mssans-11",     1, "Microsoft Sans Serif", -11,  0, 0,                  0,  0, 1},
  {"mssans8pt",     2, "Microsoft Sans Serif",   0,  8, 0,                  0,  0, 1},
  {"arial-12",      1, "Arial",                -12,  0, 0,                  0,  0, 1},
  {"courier-13",    1, "Courier",              -13,  0, 0,                  0,  0, 1},
  {"couriernew-13", 1, "Courier New",          -13,  0, 0,                  0,  0, 1},
  {"guifont",       3, NULL,                     0,  0, DEFAULT_GUI_FONT,   0,  1, 1},
  {"system",        3, NULL,                     0,  0, SYSTEM_FONT,        0,  1, 1},
  {"ncm-message",   4, NULL,                     0,  0, 0,                  4,  0, 1},
  {"ncm-caption",   4, NULL,                     0,  0, 0,                  0,  1, 0},
  {"ncm-smcaption", 4, NULL,                     0,  0, 0,                  1,  0, 0},
  {"ncm-menu",      4, NULL,                     0,  0, 0,                  2,  1, 0},
  {"ncm-status",    4, NULL,                     0,  0, 0,                  3,  1, 0},
  {"icontitle",     4, NULL,                     0,  0, 0,                  5,  1, 0},
  {"ansivar",       3, NULL,                     0,  0, ANSI_VAR_FONT,      0,  0, 0},
  {"ansifixed",     3, NULL,                     0,  0, ANSI_FIXED_FONT,    0,  0, 0},
  {"oemfixed",      3, NULL,                     0,  0, OEM_FIXED_FONT,     0,  0, 0},
  {"sysfixed",      3, NULL,                     0,  0, SYSTEM_FIXED_FONT,  0,  0, 0},
  {"devdefault",    3, NULL,                     0,  0, DEVICE_DEFAULT_FONT,0,  0, 0},
};
fspec *g_fonts = FONTS; int g_nfonts = sizeof FONTS / sizeof *FONTS;

static void font_make(fspec *f){
  LOGFONTW lf; memset(&lf, 0, sizeof lf);
  f->h = NULL; f->req_h = 0;
  if (f->kind == 0) return;
  if (f->kind == 3) { f->h = (HFONT)GetStockObject(f->stock); if (f->h && GetObjectW(f->h, sizeof lf, &lf)) f->req_h = lf.lfHeight; return; }
  if (f->kind == 4) {
    if (f->ncm == 5) { if (!SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0)) return; }
    else {
      NONCLIENTMETRICSW m; memset(&m, 0, sizeof m); m.cbSize = sizeof m;
      if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) return;
      const LOGFONTW *src[] = {&m.lfCaptionFont, &m.lfSmCaptionFont, &m.lfMenuFont, &m.lfStatusFont, &m.lfMessageFont};
      lf = *src[f->ncm];
    }
  } else {
    lf.lfHeight = f->kind == 2 ? -MulDiv(f->pt, g_dpi, 72) : f->px;
    lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
    MultiByteToWideChar(CP_ACP, 0, f->face, -1, lf.lfFaceName, LF_FACESIZE);
  }
  f->req_h = lf.lfHeight;
  f->h = CreateFontIndirectW(&lf);
}
static void font_free(fspec *f){ if (f->h && f->kind != 3) DeleteObject(f->h); f->h = NULL; }

/* ---- helpers --------------------------------------------------------------------------------- */
HWND mk(const WCHAR *cls, DWORD style, DWORD ex, int x, int y, int w, int h, HWND parent, int id, const WCHAR *text){
  return CreateWindowExW(ex, cls, text, style, x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hi, NULL);
}
void prep(HWND h, const fspec *f, const char *theme){
  if (!h) return;
  if (!strcmp(theme, "off")) SetWindowTheme(h, L"", L"");
  if (f && f->h) SendMessageW(h, WM_SETFONT, (WPARAM)f->h, TRUE);
}
void kb(rk *k, const char *cfg, const fspec *f, const char *theme){
  rk_reset(k); rk_s(k, "font", f ? f->name : "default"); rk_s(k, "theme", theme); if (cfg) rk_s(k, "cfg", cfg);
}
void rv_rect(rv *v, const RECT *r, int ok){
  if (ok) { rv_i(v, r->left); rv_i(v, r->top); rv_i(v, r->right); rv_i(v, r->bottom); }
  else for (int i = 0; i < 4; i++) rv_na(v);
}
void rv_ok(rv *v, int ok, long long x){ if (ok) rv_i(v, x); else rv_na(v); }
void rv_s32(rv *v, int x){ if (x == NA_SENT) rv_na(v); else rv_i(v, x); }
void rel_rect(HWND child, RECT *r){
  GetWindowRect(child, r);
  HWND p = GetParent(child);
  if (p) MapWindowPoints(NULL, p, (POINT *)r, 2);
}
void pump(void){ MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
HIMAGELIST mkil(int cx, int cy, int n){
  HIMAGELIST il = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, n, 1);
  if (il) ImageList_SetImageCount(il, n);
  return il;
}
int fam_on(const char *fam){
  if (!g_only) return 1;
  size_t n = strlen(fam); const char *p = g_only;
  while (*p) { const char *e = strchr(p, ','); size_t l = e ? (size_t)(e - p) : strlen(p);
    if (l == n && !strncmp(p, fam, n)) return 1;
    if (!e) break;
    p = e + 1; }
  return 0;
}

/* default font a control picks without WM_SETFONT: one row per (ctl, theme) */
void ctlfont(const char *ctl, HWND h, const fspec *f, const char *theme){
  static char seen[256][48]; static int nseen;
  if (f && f->kind != 0) return;
  char id[48]; snprintf(id, sizeof id, "%s|%s", ctl, theme);
  for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], id)) return;
  if (nseen < 256) strcpy(seen[nseen++], id);
  HFONT hf = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
  rk k; rk_reset(&k); rk_s(&k, "ctl", ctl); rk_s(&k, "theme", theme);
  rv v; rv_reset(&v); rv_i(&v, hf == NULL);
  LOGFONTW lf; HFONT use = hf ? hf : (HFONT)GetStockObject(SYSTEM_FONT);
  if (GetObjectW(use, sizeof lf, &lf)) { rv_w(&v, lf.lfFaceName[0] ? lf.lfFaceName : L""); rv_i(&v, lf.lfHeight); rv_i(&v, lf.lfWeight); }
  else { rv_na(&v); rv_na(&v); rv_na(&v); }
  HFONT o = (HFONT)SelectObject(g_dc, use); TEXTMETRICW tm;
  if (GetTextMetricsW(g_dc, &tm)) { rv_i(&v, tm.tmHeight); rv_i(&v, tm.tmInternalLeading); } else { rv_na(&v); rv_na(&v); }
  SelectObject(g_dc, o);
  rec_row("ctlfont", &k, &v);
}

/* Render the whole window with WM_PRINT into a white 32bpp DIB; record the bbox of dark (grey < 128) pixels
 * and a hash of the 8bpp grey image. Rendering depends on ClearType settings; the bbox is the useful part. */
void ink(const char *ctl, const char *cfg, HWND h, const fspec *f, const char *theme){
  RECT wr; GetWindowRect(h, &wr); int w = wr.right - wr.left, hh = wr.bottom - wr.top;
  rk k; kb(&k, cfg, f, theme); rk_s(&k, "ctl", ctl);
  rv v; rv_reset(&v); rv_i(&v, w); rv_i(&v, hh);
  rec_dib d;
  if (w <= 0 || hh <= 0 || w > 1024 || hh > 1024 || !rec_dib_create(&d, w, hh)) { for (int i = 0; i < 5; i++) rv_na(&v); rec_row("ink", &k, &v); return; }
  SendMessageW(h, WM_PRINT, (WPARAM)d.dc, PRF_NONCLIENT | PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
  GdiFlush();
  uint8_t *g = (uint8_t *)malloc((size_t)w * hh);
  size_t n = rec_gray_from_dib(d.bits, w, hh, 8, g, (size_t)w * hh);
  int l = w, t = hh, r = -1, b = -1, cnt = 0;
  for (int y = 0; y < hh; y++) for (int x = 0; x < w; x++) if (g[(size_t)y * w + x] < 128) {
    cnt++; if (x < l) l = x; if (x > r) r = x; if (y < t) t = y; if (y > b) b = y; }
  if (cnt) { rv_i(&v, l); rv_i(&v, t); rv_i(&v, r + 1); rv_i(&v, b + 1); } else { rv_i(&v, -1); rv_i(&v, -1); rv_i(&v, -1); rv_i(&v, -1); }
  rv_i(&v, cnt);
  rec_row("ink", &k, &v);
  rec_hash("ink_img", &k, g, n);
  free(g); rec_dib_free(&d);
}

/* ---- top-level window ------------------------------------------------------------------------ */
static LRESULT CALLBACK top_proc(HWND h, UINT m, WPARAM w, LPARAM l){
  if (m == WM_NOTIFY) return 0;
  return DefWindowProcW(h, m, w, l);
}

/* ---- font table ------------------------------------------------------------------------------ */
typedef LONG (WINAPI *GdiGetCharDimensions_t)(HDC, LPTEXTMETRICW, LONG *);
static void run_fonts(void){
  rec_schema("font", "font", "kind req_h lf_h lf_w lf_wt lf_cs lf_q lf_face got_face h:px a:px d:px il:px el:px ave:px max:px wt ovh:px pf cs "
             "dots_cx:px dots_cy:px alpha_cx:px alpha_cy:px ag_cy:px gcd_cx:px gcd_cy:px");
  GdiGetCharDimensions_t gcd = (GdiGetCharDimensions_t)GetProcAddress(GetModuleHandleA("gdi32.dll"), "GdiGetCharDimensions");
  static const WCHAR ALPHA[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (int i = 0; i < g_nfonts; i++) {
    fspec *f = &g_fonts[i];
    if (f->kind == 0 || (!g_full && !f->quick)) continue;
    rk k; rk_reset(&k); rk_s(&k, "font", f->name);
    rv v; rv_reset(&v); rv_i(&v, f->kind); rv_i(&v, f->req_h);
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    if (f->h && GetObjectW(f->h, sizeof lf, &lf)) {
      rv_i(&v, lf.lfHeight); rv_i(&v, lf.lfWidth); rv_i(&v, lf.lfWeight); rv_i(&v, lf.lfCharSet); rv_i(&v, lf.lfQuality);
      rv_w(&v, lf.lfFaceName[0] ? lf.lfFaceName : L"");
    } else for (int j = 0; j < 6; j++) rv_na(&v);
    if (!f->h) { for (int j = 0; j < 21; j++) rv_na(&v); rec_row("font", &k, &v); continue; }
    HFONT o = (HFONT)SelectObject(g_dc, f->h);
    WCHAR got[LF_FACESIZE] = L"";
    if (GetTextFaceW(g_dc, LF_FACESIZE, got) > 0) rv_w(&v, got[0] ? got : L""); else rv_na(&v);
    TEXTMETRICW tm;
    if (GetTextMetricsW(g_dc, &tm)) {
      rv_i(&v, tm.tmHeight); rv_i(&v, tm.tmAscent); rv_i(&v, tm.tmDescent); rv_i(&v, tm.tmInternalLeading); rv_i(&v, tm.tmExternalLeading);
      rv_i(&v, tm.tmAveCharWidth); rv_i(&v, tm.tmMaxCharWidth); rv_i(&v, tm.tmWeight); rv_i(&v, tm.tmOverhang); rv_x(&v, tm.tmPitchAndFamily); rv_i(&v, tm.tmCharSet);
    } else for (int j = 0; j < 11; j++) rv_na(&v);
    SIZE s;
    if (GetTextExtentPoint32W(g_dc, L"...", 3, &s)) { rv_i(&v, s.cx); rv_i(&v, s.cy); } else { rv_na(&v); rv_na(&v); }
    if (GetTextExtentPoint32W(g_dc, ALPHA, 52, &s)) { rv_i(&v, s.cx); rv_i(&v, s.cy); } else { rv_na(&v); rv_na(&v); }
    if (GetTextExtentPoint32W(g_dc, L"Ag", 2, &s)) rv_i(&v, s.cy); else rv_na(&v);
    if (gcd) { LONG cy = 0; LONG cx = gcd(g_dc, NULL, &cy); rv_i(&v, cx); rv_i(&v, cy); } else { rv_na(&v); rv_na(&v); }
    rec_row("font", &k, &v);
    INT cw[95]; if (GetCharWidth32W(g_dc, 0x20, 0x7e, cw)) rec_hash("font_cw_ascii", &k, cw, sizeof cw);
    SelectObject(g_dc, o);
  }
}

/* ---- system constants ------------------------------------------------------------------------ */
static void run_sys(void){
  rec_schema("sys", "name", "value");
  static const struct { const char *n; int i; } SM[] = {
    {"SM_CXEDGE", SM_CXEDGE}, {"SM_CYEDGE", SM_CYEDGE}, {"SM_CXBORDER", SM_CXBORDER}, {"SM_CYBORDER", SM_CYBORDER},
    {"SM_CXFIXEDFRAME", SM_CXFIXEDFRAME}, {"SM_CYFIXEDFRAME", SM_CYFIXEDFRAME}, {"SM_CYMENU", SM_CYMENU},
    {"SM_CXMENUSIZE", SM_CXMENUSIZE}, {"SM_CYMENUSIZE", SM_CYMENUSIZE}, {"SM_CXMENUCHECK", SM_CXMENUCHECK},
    {"SM_CYMENUCHECK", SM_CYMENUCHECK}, {"SM_CXVSCROLL", SM_CXVSCROLL}, {"SM_CYVSCROLL", SM_CYVSCROLL},
    {"SM_CXHSCROLL", SM_CXHSCROLL}, {"SM_CYHSCROLL", SM_CYHSCROLL}, {"SM_CXHTHUMB", SM_CXHTHUMB}, {"SM_CYVTHUMB", SM_CYVTHUMB},
    {"SM_CXSMICON", SM_CXSMICON}, {"SM_CYSMICON", SM_CYSMICON}, {"SM_CXICON", SM_CXICON}, {"SM_CYICON", SM_CYICON},
    {"SM_CXICONSPACING", SM_CXICONSPACING}, {"SM_CYICONSPACING", SM_CYICONSPACING}, {"SM_CYCAPTION", SM_CYCAPTION},
    {"SM_CYSMCAPTION", SM_CYSMCAPTION}, {"SM_CXFOCUSBORDER", SM_CXFOCUSBORDER}, {"SM_CYFOCUSBORDER", SM_CYFOCUSBORDER},
    {"SM_CXDLGFRAME", SM_CXDLGFRAME}, {"SM_CYDLGFRAME", SM_CYDLGFRAME}, {"SM_CXSIZEFRAME", SM_CXSIZEFRAME},
    {"SM_CXPADDEDBORDER", SM_CXPADDEDBORDER}, {"SM_CXMINTRACK", SM_CXMINTRACK}, {"SM_CXMIN", SM_CXMIN},
  };
  rk k; rv v;
#define SYSROW(name, val) do { rk_reset(&k); rk_s(&k, "name", name); rv_reset(&v); rv_i(&v, (long long)(val)); rec_row("sys", &k, &v); } while (0)
  for (unsigned i = 0; i < sizeof SM / sizeof *SM; i++) SYSROW(SM[i].n, GetSystemMetrics(SM[i].i));
  LONG dbu = GetDialogBaseUnits(); SYSROW("GetDialogBaseUnits.x", LOWORD(dbu)); SYSROW("GetDialogBaseUnits.y", HIWORD(dbu));
  LONG cmd = GetMenuCheckMarkDimensions(); SYSROW("GetMenuCheckMarkDimensions.x", LOWORD(cmd)); SYSROW("GetMenuCheckMarkDimensions.y", HIWORD(cmd));
  SYSROW("LOGPIXELSY", g_dpi);
  SYSROW("GetDpiForWindow.top", GetDpiForWindow(g_top));
  SYSROW("IsAppThemed", IsAppThemed()); SYSROW("IsThemeActive", IsThemeActive()); SYSROW("GetThemeAppProperties", GetThemeAppProperties());
  SYSROW("comctl32.major", g_cc6 ? 6 : 5);
  RECT wa; if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) { SYSROW("workarea.w", wa.right - wa.left); SYSROW("workarea.h", wa.bottom - wa.top); }
  NONCLIENTMETRICSW m; memset(&m, 0, sizeof m); m.cbSize = sizeof m;
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) {
    SYSROW("ncm.iMenuHeight", m.iMenuHeight); SYSROW("ncm.iMenuWidth", m.iMenuWidth); SYSROW("ncm.iCaptionHeight", m.iCaptionHeight);
    SYSROW("ncm.iScrollWidth", m.iScrollWidth); SYSROW("ncm.iBorderWidth", m.iBorderWidth); SYSROW("ncm.iPaddedBorderWidth", m.iPaddedBorderWidth);
  }
#undef SYSROW
}

/* ---- theme part sizes / margins -------------------------------------------------------------- */
static void run_theme(void){
  rec_schema("theme_part", "cls part state ts", "hr cx:px cy:px");
  rec_schema("theme_margin", "cls part state prop", "hr l:px r:px t:px b:px");
  rec_schema("theme_int", "cls part state prop", "hr value");
  static const struct { const WCHAR *cls; const char *cn; int part, state; } P[] = {
    {L"Header", "Header", 1, 1}, {L"Header", "Header", 1, 0}, {L"Header", "Header", 2, 1}, {L"Header", "Header", 3, 1},
    {L"Header", "Header", 4, 1}, {L"Header", "Header", 5, 1}, {L"Header", "Header", 6, 1}, {L"Header", "Header", 7, 1},
    {L"ListView", "ListView", 1, 1}, {L"ListView", "ListView", 0, 0},
    {L"TreeView", "TreeView", 1, 1}, {L"TreeView", "TreeView", 2, 1}, {L"TreeView", "TreeView", 2, 2},
    {L"Button", "Button", 1, 1}, {L"Button", "Button", 2, 1}, {L"Button", "Button", 3, 1}, {L"Button", "Button", 4, 1},
    {L"Button", "Button", 6, 1}, {L"Button", "Button", 7, 1},
    {L"ComboBox", "ComboBox", 1, 1}, {L"ComboBox", "ComboBox", 4, 1}, {L"ComboBox", "ComboBox", 6, 1},
    {L"Edit", "Edit", 1, 1}, {L"Tab", "Tab", 1, 1}, {L"Tab", "Tab", 9, 0},
    {L"Toolbar", "Toolbar", 1, 1}, {L"Toolbar", "Toolbar", 3, 1}, {L"Toolbar", "Toolbar", 5, 1},
    {L"Status", "Status", 1, 0}, {L"Status", "Status", 3, 0},
    {L"Rebar", "Rebar", 1, 0}, {L"Rebar", "Rebar", 3, 1},
    {L"Progress", "Progress", 1, 0}, {L"Progress", "Progress", 3, 0},
    {L"Trackbar", "Trackbar", 1, 1}, {L"Trackbar", "Trackbar", 3, 1}, {L"Trackbar", "Trackbar", 4, 1},
    {L"Spin", "Spin", 1, 1}, {L"Spin", "Spin", 2, 1},
    {L"Tooltip", "Tooltip", 1, 1}, {L"Tooltip", "Tooltip", 3, 1},
    {L"Menu", "Menu", 7, 1}, {L"Menu", "Menu", 8, 1}, {L"Menu", "Menu", 9, 0}, {L"Menu", "Menu", 11, 1}, {L"Menu", "Menu", 12, 1},
    {L"Menu", "Menu", 13, 0}, {L"Menu", "Menu", 14, 1}, {L"Menu", "Menu", 15, 0}, {L"Menu", "Menu", 16, 1},
  };
  static const struct { int id; const char *n; } MP[] = { {3601, "SIZINGMARGINS"}, {3602, "CONTENTMARGINS"}, {3603, "CAPTIONMARGINS"} };
  static const struct { int id; const char *n; } IP[] = { {2403, "BORDERSIZE"}, {2414, "TEXTBORDERSIZE"}, {2416, "WIDTH"}, {2417, "HEIGHT"}, {2420, "MINDPI1"} };
  for (unsigned i = 0; i < sizeof P / sizeof *P; i++) {
    HTHEME th = OpenThemeData(g_top, P[i].cls);
    rk k; rv v;
    for (int ts = 0; ts < 3; ts++) {
      static const char *TSN[] = {"min", "true", "draw"};
      rk_reset(&k); rk_s(&k, "cls", P[i].cn); rk_i(&k, "part", P[i].part); rk_i(&k, "state", P[i].state); rk_s(&k, "ts", TSN[ts]);
      rv_reset(&v);
      if (!th) { rv_raw(&v, "nothemedata"); rv_na(&v); rv_na(&v); }
      else {
        SIZE s = {NA_SENT, NA_SENT};
        HRESULT hr = GetThemePartSize(th, ts == 2 ? g_dc : NULL, P[i].part, P[i].state, NULL, (THEMESIZE)ts, &s);
        rv_x(&v, (unsigned)hr); if (SUCCEEDED(hr)) { rv_s32(&v, s.cx); rv_s32(&v, s.cy); } else { rv_na(&v); rv_na(&v); }
      }
      rec_row("theme_part", &k, &v);
    }
    if (th) {
      for (unsigned j = 0; j < sizeof MP / sizeof *MP; j++) {
        MARGINS mg = {NA_SENT, NA_SENT, NA_SENT, NA_SENT};
        HRESULT hr = GetThemeMargins(th, NULL, P[i].part, P[i].state, MP[j].id, NULL, &mg);
        if (FAILED(hr)) continue;   /* most combinations are not defined: keep only the defined ones */
        rk_reset(&k); rk_s(&k, "cls", P[i].cn); rk_i(&k, "part", P[i].part); rk_i(&k, "state", P[i].state); rk_s(&k, "prop", MP[j].n);
        rv_reset(&v); rv_x(&v, (unsigned)hr); rv_s32(&v, mg.cxLeftWidth); rv_s32(&v, mg.cxRightWidth); rv_s32(&v, mg.cyTopHeight); rv_s32(&v, mg.cyBottomHeight);
        rec_row("theme_margin", &k, &v);
      }
      for (unsigned j = 0; j < sizeof IP / sizeof *IP; j++) {
        int val = NA_SENT; HRESULT hr = GetThemeInt(th, P[i].part, P[i].state, IP[j].id, &val);
        if (FAILED(hr)) continue;
        rk_reset(&k); rk_s(&k, "cls", P[i].cn); rk_i(&k, "part", P[i].part); rk_i(&k, "state", P[i].state); rk_s(&k, "prop", IP[j].n);
        rv_reset(&v); rv_x(&v, (unsigned)hr); rv_s32(&v, val);
        rec_row("theme_int", &k, &v);
      }
      CloseThemeData(th);
    }
  }
}

/* ---- main ------------------------------------------------------------------------------------ */
int main(int argc, char **argv){
  rec_init(&argc, argv, "control_heights", CH_VERSION);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--quick")) g_full = 0;
    else if (!strcmp(argv[i], "--full")) g_full = 1;
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) g_only = argv[++i];
    else if (!strcmp(argv[i], "--no-modal")) g_modal = 0;
  }
  g_hi = GetModuleHandleW(NULL);
  INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_USEREX_CLASSES |
                              ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS};
  BOOL icc_ok = InitCommonControlsEx(&icc);
  HMODULE cc = GetModuleHandleA("comctl32.dll");
  DLLGETVERSIONPROC gv = cc ? (DLLGETVERSIONPROC)GetProcAddress(cc, "DllGetVersion") : NULL;
  DLLVERSIONINFO dvi = {sizeof dvi};
  if (gv && SUCCEEDED(gv(&dvi))) g_cc6 = dvi.dwMajorVersion >= 6;
  g_nthemes = g_cc6 ? 2 : 1;
  rec_env("control_heights.mode", "%s", g_full ? "full" : "quick");
  rec_env("control_heights.icc", "%d", (int)icc_ok);
  rec_env("control_heights.cc_dllversion", "%lu.%lu.%lu", dvi.dwMajorVersion, dvi.dwMinorVersion, dvi.dwBuildNumber);
  rec_env("control_heights.themes", "%s", g_cc6 ? "app,off" : "app");

  WNDCLASSW wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = top_proc; wc.hInstance = g_hi; wc.lpszClassName = L"CHProbeTop";
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassW(&wc);
  g_top = CreateWindowExW(0, L"CHProbeTop", L"control_heights", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 700, 560, NULL, NULL, g_hi, NULL);
  if (!g_top) { rec_note("top-level window creation failed err=%lu", GetLastError()); return rec_finish(2); }
  g_dc = GetDC(NULL);
  g_dpi = GetDeviceCaps(g_dc, LOGPIXELSY);
  for (int i = 0; i < g_nfonts; i++) font_make(&g_fonts[i]);
  rec_flush();

  if (fam_on("sys")) run_sys();
  if (fam_on("font")) run_fonts();
  if (fam_on("theme")) run_theme();
  rec_flush();

  /* schemas first (a table's S line precedes its rows), then the matrix family-major */
  if (fam_on("lv")) lv_schemas();
  if (fam_on("hdr")) hdr_schemas();
  if (fam_on("tv")) tv_schemas();
  if (fam_on("ctl")) ctl_schemas();
  if (fam_on("bar")) bar_schemas();
  if (g_modal && fam_on("modal")) modal_schemas();
  rec_schema("ctlfont", "ctl theme", "null face lf_h lf_wt tm_h:px tm_il:px");
  rec_schema("ink", "font theme cfg ctl", "w:px h:px l:px t:px r:px b:px n");

  struct { const char *fam; void (*fn)(const fspec *, const char *); } FAM[] = {
    {"lv", run_lv}, {"hdr", run_hdr}, {"tv", run_tv}, {"ctl", run_ctl}, {"bar", run_bar} };
  for (unsigned fi = 0; fi < sizeof FAM / sizeof *FAM; fi++) {
    if (!fam_on(FAM[fi].fam)) continue;
    for (int i = 0; i < g_nfonts; i++) {
      fspec *f = &g_fonts[i];
      if (!f->ctl || (!g_full && !f->quick)) continue;
      if (f->kind != 0 && !f->h) { rec_note("font %s not realised; skipped", f->name); continue; }
      for (int t = 0; t < g_nthemes; t++) { FAM[fi].fn(f, g_themes[t]); pump(); }
      rec_flush();
    }
  }
  if (fam_on("bar")) for (int t = 0; t < g_nthemes; t++) run_bar_fontless(g_themes[t]);
  rec_flush();
  if (g_modal && fam_on("modal")) run_modal();

  for (int i = 0; i < g_nfonts; i++) font_free(&g_fonts[i]);
  ReleaseDC(NULL, g_dc);
  DestroyWindow(g_top); pump();
  rec_note("control_heights done");
  return rec_finish(0);
}
