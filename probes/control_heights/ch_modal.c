/* control_heights: menus (menu bar of a hidden window, a TrackPopupMenu popup measured from a timer and closed
 * with EndMenu) and MessageBoxW layouts (measured from a thread timer, closed by posting its first button). */
#include "ch.h"

void modal_schemas(void){
  rec_schema("menubar", "cfg item", "ret l:px t:px r:px b:px h:px");
  rec_schema("menubar_win", "cfg", "win_w:px win_h:px cli_w:px cli_h:px nc_h:px");
  rec_schema("menupop", "cfg item", "ret l:px t:px r:px b:px h:px");
  rec_schema("menupop_win", "cfg", "found win_w:px win_h:px cli_w:px cli_h:px");
  rec_schema("msgbox", "case", "found win_w:px win_h:px cli_w:px cli_h:px nchild text_tm_h:px text_face");
  rec_schema("msgbox_ctl", "case idx", "cls id x:px y:px w:px h:px");
}

typedef struct { const WCHAR *cls; HWND found; int any; } findctx;
static BOOL CALLBACK find_cb(HWND h, LPARAM l){
  findctx *c = (findctx *)l;
  WCHAR n[64]; if (GetClassNameW(h, n, 64) && !lstrcmpW(n, c->cls) && (c->any || IsWindowVisible(h))) { c->found = h; return FALSE; }
  return TRUE;
}
static HWND find_thread_window(const WCHAR *cls, int any){
  findctx ctx = {cls, NULL, any};
  EnumThreadWindows(GetCurrentThreadId(), find_cb, (LPARAM)&ctx);
  return ctx.found;
}

/* ---------------------------------------------------------------- menu bar */
static HMENU make_bar(int many){
  HMENU m = CreateMenu();
  static const WCHAR *T[] = {L"&File", L"&Edit", L"&View", L"&Help", L"&Tools", L"&Window", L"&Favorites", L"F&ormat", L"&Bookmarks", L"&Debug"};
  int n = many ? 10 : 4;
  for (int i = 0; i < n; i++) { HMENU p = CreatePopupMenu(); AppendMenuW(p, MF_STRING, 2000 + i, L"Item"); AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)p, T[i]); }
  return m;
}
static void menubar_one(const char *cfg, int w, int many){
  HWND h = CreateWindowExW(0, L"CHProbeTop", L"menubar", WS_OVERLAPPEDWINDOW, 0, 0, w, 300, NULL, NULL, g_hi, NULL);
  if (!h) { rec_note("menubar window failed"); return; }
  HMENU m = make_bar(many);
  SetMenu(h, m);
  SetWindowPos(h, NULL, 0, 0, w, 300, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOMOVE);
  int n = GetMenuItemCount(m);
  rk k; rv v; RECT wr; GetWindowRect(h, &wr);
  for (int i = 0; i <= n; i++) {
    MENUBARINFO mbi; memset(&mbi, 0, sizeof mbi); mbi.cbSize = sizeof mbi;
    int ok = GetMenuBarInfo(h, OBJID_MENU, i, &mbi);
    rk_reset(&k); rk_s(&k, "cfg", cfg); if (i) rk_i(&k, "item", i); else rk_s(&k, "item", "bar");   /* idItem 0 = whole bar, i = item i-1 */
    rv_reset(&v); rv_i(&v, ok);
    if (ok) { rv_i(&v, mbi.rcBar.left - wr.left); rv_i(&v, mbi.rcBar.top - wr.top); rv_i(&v, mbi.rcBar.right - wr.left); rv_i(&v, mbi.rcBar.bottom - wr.top); rv_i(&v, mbi.rcBar.bottom - mbi.rcBar.top); }
    else for (int j = 0; j < 5; j++) rv_na(&v);
    rec_row("menubar", &k, &v);
  }
  RECT cr; GetClientRect(h, &cr);
  rk_reset(&k); rk_s(&k, "cfg", cfg); rv_reset(&v);
  rv_i(&v, wr.right - wr.left); rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.right); rv_i(&v, cr.bottom); rv_i(&v, (wr.bottom - wr.top) - cr.bottom);
  rec_row("menubar_win", &k, &v);
  SetMenu(h, NULL); DestroyMenu(m); DestroyWindow(h);
}

/* ---------------------------------------------------------------- popup */
static struct { const char *cfg; HMENU m; int done; int ticks; } g_pop;
static VOID CALLBACK pop_timer(HWND hw, UINT msg, UINT_PTR id, DWORD t){
  if (g_pop.done) return;
  HWND p = find_thread_window(L"#32768", 0);
  if (!p && ++g_pop.ticks < 40) return;
  g_pop.done = 1;
  rk k; rv v;
  rk_reset(&k); rk_s(&k, "cfg", g_pop.cfg); rv_reset(&v); rv_i(&v, p != NULL);
  RECT wr = {0, 0, 0, 0}, cr = {0, 0, 0, 0};
  if (p) { GetWindowRect(p, &wr); GetClientRect(p, &cr); rv_i(&v, wr.right - wr.left); rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.right); rv_i(&v, cr.bottom); }
  else for (int j = 0; j < 4; j++) rv_na(&v);
  rec_row("menupop_win", &k, &v);
  if (p) {
    int n = GetMenuItemCount(g_pop.m);
    for (int i = 0; i < n; i++) {
      RECT r; int ok = GetMenuItemRect(NULL, g_pop.m, i, &r);
      rk_reset(&k); rk_s(&k, "cfg", g_pop.cfg); rk_i(&k, "item", i);
      rv_reset(&v); rv_i(&v, ok);
      if (ok) { rv_i(&v, r.left - wr.left); rv_i(&v, r.top - wr.top); rv_i(&v, r.right - wr.left); rv_i(&v, r.bottom - wr.top); rv_i(&v, r.bottom - r.top); }
      else for (int j = 0; j < 5; j++) rv_na(&v);
      rec_row("menupop", &k, &v);
    }
  }
  EndMenu();
}
static void menupop_one(const char *cfg, int variant){
  HMENU m = CreatePopupMenu(), sub = CreatePopupMenu();
  AppendMenuW(sub, MF_STRING, 3100, L"Sub item");
  AppendMenuW(m, MF_STRING, 3001, L"&Open\tCtrl+O");
  AppendMenuW(m, MF_STRING, 3002, L"Save &As...");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING | MF_CHECKED, 3003, L"Word wrap");
  AppendMenuW(m, MF_STRING | MF_GRAYED, 3004, L"Disabled");
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)sub, L"More");
  AppendMenuW(m, MF_STRING, 3005, L"A much longer menu item caption");
  if (variant == 1) { MENUINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi; mi.fMask = MIM_STYLE; mi.dwStyle = MNS_CHECKORBMP; SetMenuInfo(m, &mi); }
  HWND owner = CreateWindowExW(0, L"CHProbeTop", L"popup owner", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL, NULL, g_hi, NULL);
  memset(&g_pop, 0, sizeof g_pop); g_pop.cfg = cfg; g_pop.m = m;
  UINT_PTR t1 = SetTimer(owner, 1, 150, pop_timer);
  rec_flush();
  TrackPopupMenuEx(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, 100, 100, owner, NULL);
  KillTimer(owner, t1);
  if (!g_pop.done) { rk k; rv v; rk_reset(&k); rk_s(&k, "cfg", cfg); rv_reset(&v); rv_raw(&v, "0"); for (int j = 0; j < 4; j++) rv_na(&v); rec_row("menupop_win", &k, &v); }
  DestroyMenu(m); DestroyWindow(owner); pump();
}

/* ---------------------------------------------------------------- message boxes */
static struct { const char *cs; int done; int ticks; } g_mb;
static VOID CALLBACK mb_timer(HWND hw, UINT msg, UINT_PTR id, DWORD t){
  if (g_mb.done) return;
  HWND d = find_thread_window(L"#32770", g_mb.ticks > 50);   /* after 5 s also accept an invisible dialog */
  if (!d) { if (++g_mb.ticks == 100) rec_note("msgbox %s: dialog not found", g_mb.cs); return; }
  g_mb.done = 1;
  RECT wr, cr; GetWindowRect(d, &wr); GetClientRect(d, &cr);
  int n = 0, first_btn = 0; HWND btn = NULL, txt = NULL;
  for (HWND c = GetWindow(d, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    char cls[64] = ""; GetClassNameA(c, cls, sizeof cls);
    RECT r; rel_rect(c, &r); int idc = GetDlgCtrlID(c);
    rk k; rk_reset(&k); rk_s(&k, "case", g_mb.cs); rk_i(&k, "idx", n);
    rv v; rv_reset(&v); rv_s(&v, cls); rv_i(&v, idc); rv_i(&v, r.left); rv_i(&v, r.top); rv_i(&v, r.right - r.left); rv_i(&v, r.bottom - r.top);
    rec_row("msgbox_ctl", &k, &v);
    if (!btn && !lstrcmpiA(cls, "Button")) { btn = c; first_btn = idc; }
    if (!txt && !lstrcmpiA(cls, "Static") && idc == 0xFFFF) txt = c;
    n++;
  }
  rk k; rk_reset(&k); rk_s(&k, "case", g_mb.cs);
  rv v; rv_reset(&v); rv_i(&v, 1); rv_i(&v, wr.right - wr.left); rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.right); rv_i(&v, cr.bottom); rv_i(&v, n);
  HFONT hf = txt ? (HFONT)SendMessageW(txt, WM_GETFONT, 0, 0) : NULL;
  if (hf) {
    HFONT o = (HFONT)SelectObject(g_dc, hf); TEXTMETRICW tm; WCHAR face[LF_FACESIZE] = L"";
    int ok = GetTextMetricsW(g_dc, &tm); GetTextFaceW(g_dc, LF_FACESIZE, face); SelectObject(g_dc, o);
    rv_ok(&v, ok, tm.tmHeight); rv_w(&v, face[0] ? face : L"");
  } else { rv_na(&v); rv_na(&v); }
  rec_row("msgbox", &k, &v);
  if (btn) PostMessageW(d, WM_COMMAND, MAKEWPARAM(first_btn, BN_CLICKED), (LPARAM)btn);
  else PostMessageW(d, WM_CLOSE, 0, 0);
}
static void msgbox_one(const char *cs, const WCHAR *text, UINT type){
  memset(&g_mb, 0, sizeof g_mb); g_mb.cs = cs;
  UINT_PTR t = SetTimer(NULL, 0, 100, mb_timer);
  rec_flush();
  MessageBoxW(NULL, text, L"Caption", type);
  KillTimer(NULL, t);
  if (!g_mb.done) { rk k; rk_reset(&k); rk_s(&k, "case", cs); rv v; rv_reset(&v); rv_i(&v, 0); for (int j = 0; j < 7; j++) rv_na(&v); rec_row("msgbox", &k, &v); }
  pump();
}

void run_modal(void){
  menubar_one("w600", 600, 0);
  menubar_one("w200_many", 200, 1);
  menupop_one("popup", 0);
  if (g_full) menupop_one("popup_checkorbmp", 1);
  msgbox_one("ok_short", L"Hello", MB_OK);
  msgbox_one("okcancel_warn_long", L"A considerably longer message box text that should wrap onto more than one line when it exceeds "
             L"the maximum width the message box allows for its text area, which depends on the work area size.", MB_OKCANCEL | MB_ICONWARNING);
  if (g_full) {
    msgbox_one("ync_question_3lines", L"Line one\nLine two\nLine three", MB_YESNOCANCEL | MB_ICONQUESTION);
    msgbox_one("ari_error", L"Error", MB_ABORTRETRYIGNORE | MB_ICONERROR);
    msgbox_one("ok_info", L"Information", MB_OK | MB_ICONINFORMATION);
    msgbox_one("ok_empty", L"", MB_OK);
    msgbox_one("ctc", L"Cancel, try or continue", MB_CANCELTRYCONTINUE);
    msgbox_one("ok_wide_word", L"Averyveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryverylongwordwithoutanybreakopportunity", MB_OK);
  }
}
