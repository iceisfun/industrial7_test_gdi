/* control_heights: user32-class controls (Button, Edit, ComboBox, ListBox, Static; comctl32 v6 versions when the
 * manifest activates them), ComboBoxEx, tooltips, and the WM_PRINT ink boxes. */
#include "ch.h"

void ctl_schemas(void){
  rec_schema("btn", "font theme cfg cap",
    "ideal_ret ideal_cx:px ideal_cy:px ideal100_ret ideal100_cx:px ideal100_cy:px tmarg_ret tmarg_l:px tmarg_t:px tmarg_r:px tmarg_b:px dt_cx:px dt_cy:px");
  rec_schema("edit", "font theme cfg",
    "win_h:px cli_w:px cli_h:px rc_l:px rc_t:px rc_r:px rc_b:px marg_l:px marg_r:px pos0_x:px pos0_y:px posl2_y:px lines");
  rec_schema("cb", "font theme cfg",
    "win_h:px ih_sel:px ih_item:px item_l:px item_t:px item_r:px item_b:px btn_l:px btn_t:px btn_r:px btn_b:px drop_h:px lb_ih:px");
  rec_schema("cbex", "font theme cfg", "win_h:px ih_sel:px ih_item:px combo_h:px edit_h:px");
  rec_schema("lb", "font theme cfg", "ih:px it0_l:px it0_t:px it0_r:px it0_b:px it1_t:px win_h:px cli_h:px");
  rec_schema("tt", "font theme cfg txt", "bub_cx:px bub_cy:px marg_l:px marg_t:px marg_r:px marg_b:px adj_l:px adj_t:px adj_r:px adj_b:px");
}

/* ---------------------------------------------------------------- buttons */
static const struct { const char *n; DWORD s; int quick; } BS[] = {
  {"push", BS_PUSHBUTTON, 1}, {"defpush", BS_DEFPUSHBUTTON, 0}, {"check", BS_AUTOCHECKBOX, 1}, {"radio", BS_AUTORADIOBUTTON, 1},
  {"3state", BS_AUTO3STATE, 0}, {"group", BS_GROUPBOX, 1}, {"pushlike", BS_AUTOCHECKBOX | BS_PUSHLIKE, 0},
  {"split", BS_SPLITBUTTON, 0}, {"cmdlink", BS_COMMANDLINK, 0}, {"multiline", BS_PUSHBUTTON | BS_MULTILINE, 0},
  {"check_right", BS_AUTOCHECKBOX | BS_LEFTTEXT, 0},
};
static const struct { const char *n; const WCHAR *s; int quick; } CAP[] = {
  {"OK", L"OK", 1}, {"Cancel", L"Cancel", 0}, {"Apply", L"&Apply", 0}, {"Checkbox", L"Check box", 1},
  {"long", L"A much longer caption for sizing tests", 0}, {"W", L"W", 0}, {"empty", L"", 0},
};
static void btn_one(const fspec *f, const char *theme, int bi, int ci){
  HWND h = mk(L"BUTTON", WS_CHILD | WS_VISIBLE | BS[bi].s, 0, 10, 10, 200, 30, g_top, 200, CAP[ci].s);
  rk k; kb(&k, BS[bi].n, f, theme); rk_s(&k, "cap", CAP[ci].n); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("btn", &k, &v); return; }
  prep(h, f, theme);
  ctlfont("button", h, f, theme);
  SIZE s = {0, 0}; int r = (int)SendMessageW(h, BCM_GETIDEALSIZE, 0, (LPARAM)&s);
  rv_i(&v, r); rv_ok(&v, r, s.cx); rv_ok(&v, r, s.cy);
  s.cx = 100; s.cy = 0; r = (int)SendMessageW(h, BCM_GETIDEALSIZE, 0, (LPARAM)&s);
  rv_i(&v, r); rv_ok(&v, r, s.cx); rv_ok(&v, r, s.cy);
  RECT m = {NA_SENT, NA_SENT, NA_SENT, NA_SENT}; r = (int)SendMessageW(h, BCM_GETTEXTMARGIN, 0, (LPARAM)&m);
  rv_i(&v, r); if (r) { rv_s32(&v, m.left); rv_s32(&v, m.top); rv_s32(&v, m.right); rv_s32(&v, m.bottom); } else for (int i = 0; i < 4; i++) rv_na(&v);
  HFONT hf = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
  HFONT o = (HFONT)SelectObject(g_dc, hf ? hf : GetStockObject(SYSTEM_FONT));
  RECT dr = {0, 0, 0, 0}; DrawTextW(g_dc, CAP[ci].s, -1, &dr, DT_CALCRECT | DT_SINGLELINE);
  SelectObject(g_dc, o);
  rv_i(&v, dr.right - dr.left); rv_i(&v, dr.bottom - dr.top);
  rec_row("btn", &k, &v);
  DestroyWindow(h);
}

/* ---------------------------------------------------------------- edit */
static const struct { const char *n; DWORD s; DWORD ex; int w, h; int quick; } ED[] = {
  {"single",       ES_AUTOHSCROLL, 0, 200, 23, 0},
  {"single_ce",    ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 23, 1},
  {"single_ce20",  ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 20, 0},
  {"single_ce30",  ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 30, 0},
  {"single_border",ES_AUTOHSCROLL | WS_BORDER, 0, 200, 23, 0},
  {"multi_ce",     ES_MULTILINE | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, 200, 100, 1},
  {"multi",        ES_MULTILINE | ES_AUTOVSCROLL, 0, 200, 100, 0},
};
static void edit_one(const fspec *f, const char *theme, int i){
  int multi = (ED[i].s & ES_MULTILINE) != 0;
  HWND h = mk(L"EDIT", WS_CHILD | WS_VISIBLE | ED[i].s, ED[i].ex, 10, 10, ED[i].w, ED[i].h, g_top, 201, multi ? L"Ag\r\nBg\r\nCg" : L"Agy");
  rk k; kb(&k, ED[i].n, f, theme); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("edit", &k, &v); return; }
  prep(h, f, theme);
  ctlfont("edit", h, f, theme);
  RECT wr, cr, rc; GetWindowRect(h, &wr); GetClientRect(h, &cr);
  rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.right); rv_i(&v, cr.bottom);
  SendMessageW(h, EM_GETRECT, 0, (LPARAM)&rc); rv_rect(&v, &rc, 1);
  DWORD mg = (DWORD)SendMessageW(h, EM_GETMARGINS, 0, 0); rv_i(&v, LOWORD(mg)); rv_i(&v, HIWORD(mg));
  LRESULT p0 = SendMessageW(h, EM_POSFROMCHAR, 0, 0);
  rv_ok(&v, p0 != -1, (short)LOWORD(p0)); rv_ok(&v, p0 != -1, (short)HIWORD(p0));
  if (multi) { LRESULT p2 = SendMessageW(h, EM_POSFROMCHAR, 4, 0); rv_ok(&v, p2 != -1, (short)HIWORD(p2)); } else rv_na(&v);
  rv_i(&v, (int)SendMessageW(h, EM_GETLINECOUNT, 0, 0));
  rec_row("edit", &k, &v);
  DestroyWindow(h);
}

/* ---------------------------------------------------------------- combo box */
static const struct { const char *n; DWORD s; int quick; } CB[] = {
  {"dropdown", CBS_DROPDOWN | CBS_AUTOHSCROLL, 1}, {"dropdownlist", CBS_DROPDOWNLIST, 1}, {"simple", CBS_SIMPLE, 0},
  {"dropdownlist_nih", CBS_DROPDOWNLIST | CBS_NOINTEGRALHEIGHT, 0},
};
static void cb_one(const fspec *f, const char *theme, int i){
  HWND h = mk(L"COMBOBOX", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CB[i].s, 0, 10, 10, 150, 200, g_top, 202, NULL);
  rk k; kb(&k, CB[i].n, f, theme); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("cb", &k, &v); return; }
  prep(h, f, theme);
  ctlfont("combobox", h, f, theme);
  static const WCHAR *S[] = {L"First", L"Second item", L"Third"};
  for (int j = 0; j < 3; j++) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)S[j]);
  SendMessageW(h, CB_SETCURSEL, 0, 0);
  RECT wr; GetWindowRect(h, &wr); rv_i(&v, wr.bottom - wr.top);
  rv_i(&v, (int)SendMessageW(h, CB_GETITEMHEIGHT, (WPARAM)-1, 0));
  rv_i(&v, (int)SendMessageW(h, CB_GETITEMHEIGHT, 0, 0));
  COMBOBOXINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
  int ok = GetComboBoxInfo(h, &ci);
  rv_rect(&v, &ci.rcItem, ok); rv_rect(&v, &ci.rcButton, ok);
  RECT dr; int okd = (int)SendMessageW(h, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&dr); rv_ok(&v, okd, dr.bottom - dr.top);
  if (ok && ci.hwndList) rv_i(&v, (int)SendMessageW(ci.hwndList, LB_GETITEMHEIGHT, 0, 0)); else rv_na(&v);
  rec_row("cb", &k, &v);
  DestroyWindow(h);
}
static const struct { const char *n; DWORD s; int il; int quick; } CBX[] = {
  {"dropdown", CBS_DROPDOWN, 0, 1}, {"dropdownlist", CBS_DROPDOWNLIST, 0, 0}, {"dropdown_il16", CBS_DROPDOWN, 16, 0}, {"dropdownlist_il16", CBS_DROPDOWNLIST, 16, 1},
};
static void cbex_one(const fspec *f, const char *theme, int i){
  HWND h = mk(WC_COMBOBOXEXW, WS_CHILD | WS_VISIBLE | CBX[i].s, 0, 10, 10, 150, 200, g_top, 203, NULL);
  rk k; kb(&k, CBX[i].n, f, theme); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("cbex", &k, &v); return; }
  prep(h, f, theme);
  ctlfont("comboboxex", h, f, theme);
  HIMAGELIST il = NULL;
  if (CBX[i].il) { il = mkil(CBX[i].il, CBX[i].il, 4); SendMessageW(h, CBEM_SETIMAGELIST, 0, (LPARAM)il); }
  static const WCHAR *S[] = {L"First", L"Second item", L"Third"};
  for (int j = 0; j < 3; j++) {
    COMBOBOXEXITEMW it; memset(&it, 0, sizeof it); it.mask = CBEIF_TEXT | (il ? CBEIF_IMAGE | CBEIF_SELECTEDIMAGE : 0);
    it.iItem = j; it.pszText = (LPWSTR)S[j];
    SendMessageW(h, CBEM_INSERTITEMW, 0, (LPARAM)&it);
  }
  SendMessageW(h, CB_SETCURSEL, 0, 0);
  RECT wr; GetWindowRect(h, &wr); rv_i(&v, wr.bottom - wr.top);
  rv_i(&v, (int)SendMessageW(h, CB_GETITEMHEIGHT, (WPARAM)-1, 0));
  rv_i(&v, (int)SendMessageW(h, CB_GETITEMHEIGHT, 0, 0));
  HWND c = (HWND)SendMessageW(h, CBEM_GETCOMBOCONTROL, 0, 0), e = (HWND)SendMessageW(h, CBEM_GETEDITCONTROL, 0, 0);
  if (c) { GetWindowRect(c, &wr); rv_i(&v, wr.bottom - wr.top); } else rv_na(&v);
  if (e) { GetWindowRect(e, &wr); rv_i(&v, wr.bottom - wr.top); } else rv_na(&v);
  rec_row("cbex", &k, &v);
  DestroyWindow(h); if (il) ImageList_Destroy(il);
}

/* ---------------------------------------------------------------- list box */
static const struct { const char *n; DWORD s; int quick; } LB[] = {
  {"std", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 1}, {"nih", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT, 0},
  {"odfixed", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_BORDER, 0}, {"noborder", LBS_NOTIFY, 0},
};
static void lb_one(const fspec *f, const char *theme, int i){
  HWND h = mk(L"LISTBOX", WS_CHILD | WS_VISIBLE | LB[i].s, 0, 10, 10, 150, 100, g_top, 204, NULL);
  rk k; kb(&k, LB[i].n, f, theme); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("lb", &k, &v); return; }
  prep(h, f, theme);
  ctlfont("listbox", h, f, theme);
  static const WCHAR *S[] = {L"First", L"Second item", L"Third"};
  for (int j = 0; j < 3; j++) SendMessageW(h, LB_ADDSTRING, 0, (LPARAM)S[j]);
  rv_i(&v, (int)SendMessageW(h, LB_GETITEMHEIGHT, 0, 0));
  RECT r0, r1; int o0 = SendMessageW(h, LB_GETITEMRECT, 0, (LPARAM)&r0) != LB_ERR, o1 = SendMessageW(h, LB_GETITEMRECT, 1, (LPARAM)&r1) != LB_ERR;
  rv_rect(&v, &r0, o0); rv_ok(&v, o1, r1.top);
  RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr); rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.bottom);
  rec_row("lb", &k, &v);
  DestroyWindow(h);
}

/* ---------------------------------------------------------------- tooltips */
static const struct { const char *n; const WCHAR *s; int maxw; int quick; } TT[] = {
  {"Tip", L"Tip", 0, 1}, {"longer", L"A longer tooltip text", 0, 0},
  {"wrap200", L"A very long tooltip text that will need to wrap across several lines at the maximum width", 200, 1},
  {"two_lines", L"Line one\r\nLine two", 300, 0},
};
static void tt_one(const fspec *f, const char *theme, int balloon, int i){
  HWND h = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | (balloon ? TTS_BALLOON : 0),
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_top, NULL, g_hi, NULL);
  rk k; kb(&k, balloon ? "balloon" : "std", f, theme); rk_s(&k, "txt", TT[i].n); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("tt", &k, &v); return; }
  prep(h, f, theme);
  ctlfont(balloon ? "tooltip_balloon" : "tooltip", h, f, theme);
  if (TT[i].maxw) SendMessageW(h, TTM_SETMAXTIPWIDTH, 0, TT[i].maxw);
  TTTOOLINFOW ti; memset(&ti, 0, sizeof ti);
  ti.cbSize = g_cc6 ? sizeof ti : TTTOOLINFOW_V2_SIZE;
  ti.hwnd = g_top; ti.uId = 1; ti.rect.right = 50; ti.rect.bottom = 50; ti.lpszText = (LPWSTR)TT[i].s; ti.hinst = g_hi;
  SendMessageW(h, TTM_ADDTOOLW, 0, (LPARAM)&ti);
  DWORD bs = (DWORD)SendMessageW(h, TTM_GETBUBBLESIZE, 0, (LPARAM)&ti);
  rv_i(&v, LOWORD(bs)); rv_i(&v, HIWORD(bs));
  RECT m = {NA_SENT, NA_SENT, NA_SENT, NA_SENT}; SendMessageW(h, TTM_GETMARGIN, 0, (LPARAM)&m);
  rv_s32(&v, m.left); rv_s32(&v, m.top); rv_s32(&v, m.right); rv_s32(&v, m.bottom);
  RECT a = {0, 0, 100, 20}; int ok = (int)SendMessageW(h, TTM_ADJUSTRECT, TRUE, (LPARAM)&a); rv_rect(&v, &a, ok);
  rec_row("tt", &k, &v);
  DestroyWindow(h);
}

/* ---------------------------------------------------------------- WM_PRINT ink boxes */
static void ink_all(const fspec *f, const char *theme){
  static const struct { const WCHAR *cls; const char *n; DWORD s; DWORD ex; int w, h; const WCHAR *t; } IK[] = {
    {L"STATIC", "static", SS_LEFT, 0, 200, 30, L"Sample text Ag"},
    {L"STATIC", "static_center", SS_CENTER | SS_CENTERIMAGE, 0, 200, 30, L"Sample text Ag"},
    {L"BUTTON", "push", BS_PUSHBUTTON, 0, 75, 23, L"OK"},
    {L"BUTTON", "check", BS_AUTOCHECKBOX, 0, 120, 17, L"Check box"},
    {L"BUTTON", "radio", BS_AUTORADIOBUTTON, 0, 120, 17, L"Radio"},
    {L"BUTTON", "group", BS_GROUPBOX, 0, 200, 60, L"Group"},
    {L"EDIT", "edit_ce", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 150, 23, L"Agy"},
  };
  for (unsigned i = 0; i < sizeof IK / sizeof *IK; i++) {
    HWND h = mk(IK[i].cls, WS_CHILD | WS_VISIBLE | IK[i].s, IK[i].ex, 10, 10, IK[i].w, IK[i].h, g_top, 205, IK[i].t);
    if (!h) continue;
    prep(h, f, theme);
    if (!strcmp(IK[i].n, "static")) ctlfont("static", h, f, theme);
    ink(IK[i].n, "ink", h, f, theme);
    DestroyWindow(h);
  }
}

void run_ctl(const fspec *f, const char *theme){
  for (unsigned b = 0; b < sizeof BS / sizeof *BS; b++) {
    if (!g_full && !BS[b].quick) continue;
    for (unsigned c = 0; c < sizeof CAP / sizeof *CAP; c++) if (g_full || CAP[c].quick) btn_one(f, theme, b, c);
  }
  for (unsigned i = 0; i < sizeof ED / sizeof *ED; i++) if (g_full || ED[i].quick) edit_one(f, theme, i);
  for (unsigned i = 0; i < sizeof CB / sizeof *CB; i++) if (g_full || CB[i].quick) cb_one(f, theme, i);
  for (unsigned i = 0; i < sizeof CBX / sizeof *CBX; i++) if (g_full || CBX[i].quick) cbex_one(f, theme, i);
  for (unsigned i = 0; i < sizeof LB / sizeof *LB; i++) if (g_full || LB[i].quick) lb_one(f, theme, i);
  for (int b = 0; b < 2; b++) for (unsigned i = 0; i < sizeof TT / sizeof *TT; i++) if (g_full || (TT[i].quick && !b)) tt_one(f, theme, b, i);
  if (g_full) ink_all(f, theme);
}
