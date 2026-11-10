/* control_heights: toolbar, status bar, rebar, tab (per font) and progress, trackbar, up-down (font-independent). */
#include "ch.h"

void bar_schemas(void){
  ch_schema("tb", "font theme cfg",
    "bs_cx:px bs_cy:px pad_cx:px pad_cy:px it0_l:px it0_t:px it0_r:px it0_b:px it1_l:px it1_r:px win_h:px max_cx:px max_cy:px rows "
    "m_cxpad:px m_cypad:px m_cxbarpad:px m_cybarpad:px m_cxspc:px m_cyspc:px");
  ch_schema("sb", "font theme cfg", "win_h:px cli_h:px r0_l:px r0_t:px r0_r:px r0_b:px brd_h:px brd_v:px brd_between:px");
  ch_schema("rb", "font theme cfg",
    "bar_h:px row_h:px r_l:px r_t:px r_r:px r_b:px bb_l:px bb_t:px bb_r:px bb_b:px bm_l:px bm_t:px bm_r:px bm_b:px win_h:px child_h:px");
  ch_schema("tab", "font theme cfg",
    "it0_l:px it0_t:px it0_r:px it0_b:px it1_l:px it1_r:px itN_l:px itN_t:px itN_r:px itN_b:px rows "
    "adjf_l:px adjf_t:px adjf_r:px adjf_b:px adjt_l:px adjt_t:px adjt_r:px adjt_b:px");
  ch_schema("trk", "theme cfg", "th_l:px th_t:px th_r:px th_b:px ch_l:px ch_t:px ch_r:px ch_b:px th_len:px");
  ch_schema("ud", "theme cfg", "ud_x:px ud_y:px ud_w:px ud_h:px bud_x:px bud_y:px bud_w:px bud_h:px");
  ch_schema("pb", "theme cfg", "win_w:px win_h:px");
}

/* ---------------------------------------------------------------- toolbar */
static const struct { const char *n; DWORD s; int quick; } TBS[] = {
  {"std", 0, 0}, {"flat", TBSTYLE_FLAT, 1}, {"list", TBSTYLE_FLAT | TBSTYLE_LIST, 1}, {"autosize", TBSTYLE_FLAT, 0},
};
static void tb_one(const fspec *f, const char *theme, int si, int il, int text){
  char cfg[64]; snprintf(cfg, sizeof cfg, "%s_il%d_%s", TBS[si].n, il, text ? "text" : "notext");
  HWND h = mk(TOOLBARCLASSNAMEW, WS_CHILD | WS_VISIBLE | CCS_TOP | TBS[si].s, 0, 0, 0, 0, 0, g_top, 300, NULL);
  rk k; kb(&k, cfg, f, theme); rv v; rv_reset(&v);
  if (!h) { ch_fail("tb", &k, "nocreate"); return; }
  SendMessageW(h, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
  prep(h, f, theme);
  ctlfont("toolbar", h, f, theme);
  HIMAGELIST hil = NULL;
  if (il) { hil = mkil(il, il, 4); SendMessageW(h, TB_SETIMAGELIST, 0, (LPARAM)hil); }
  static const WCHAR *T[] = {L"Open", L"Save", L"Properties"};
  TBBUTTON b[3]; memset(b, 0, sizeof b);
  for (int i = 0; i < 3; i++) {
    b[i].iBitmap = il ? i : I_IMAGENONE; b[i].idCommand = 1000 + i; b[i].fsState = TBSTATE_ENABLED;
    b[i].fsStyle = BTNS_BUTTON | (!strcmp(TBS[si].n, "autosize") ? BTNS_AUTOSIZE : 0) | (text && (TBS[si].s & TBSTYLE_LIST) ? BTNS_SHOWTEXT : 0);
    b[i].iString = text ? (INT_PTR)T[i] : -1;
  }
  SendMessageW(h, TB_ADDBUTTONSW, 3, (LPARAM)b);
  SendMessageW(h, TB_AUTOSIZE, 0, 0);
  DWORD bs = (DWORD)SendMessageW(h, TB_GETBUTTONSIZE, 0, 0), pd = (DWORD)SendMessageW(h, TB_GETPADDING, 0, 0);
  rv_i(&v, LOWORD(bs)); rv_i(&v, HIWORD(bs)); rv_i(&v, LOWORD(pd)); rv_i(&v, HIWORD(pd));
  RECT r0, r1; int o0 = (int)SendMessageW(h, TB_GETITEMRECT, 0, (LPARAM)&r0), o1 = (int)SendMessageW(h, TB_GETITEMRECT, 1, (LPARAM)&r1);
  rv_rect(&v, &r0, o0); rv_ok(&v, o1, r1.left); rv_ok(&v, o1, r1.right);
  RECT wr; GetWindowRect(h, &wr); rv_i(&v, wr.bottom - wr.top);
  SIZE ms = {0, 0}; int om = (int)SendMessageW(h, TB_GETMAXSIZE, 0, (LPARAM)&ms); rv_ok(&v, om, ms.cx); rv_ok(&v, om, ms.cy);
  rv_i(&v, (int)SendMessageW(h, TB_GETROWS, 0, 0));
  TBMETRICS tm; memset(&tm, 0, sizeof tm); tm.cbSize = sizeof tm; tm.dwMask = TBMF_PAD | TBMF_BARPAD | TBMF_BUTTONSPACING;
  tm.cxPad = tm.cyPad = tm.cxBarPad = tm.cyBarPad = tm.cxButtonSpacing = tm.cyButtonSpacing = NA_SENT;
  SendMessageW(h, TB_GETMETRICS, 0, (LPARAM)&tm);
  rv_s32(&v, tm.cxPad); rv_s32(&v, tm.cyPad); rv_s32(&v, tm.cxBarPad); rv_s32(&v, tm.cyBarPad); rv_s32(&v, tm.cxButtonSpacing); rv_s32(&v, tm.cyButtonSpacing);
  rec_row("tb", &k, &v);
  DestroyWindow(h); if (hil) ImageList_Destroy(hil);
}

/* ---------------------------------------------------------------- status bar */
static void sb_one(const fspec *f, const char *theme, const char *cfg, DWORD style, int parts){
  HWND h = mk(STATUSCLASSNAMEW, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, 0, g_top, 301, NULL);
  rk k; kb(&k, cfg, f, theme); rv v; rv_reset(&v);
  if (!h) { ch_fail("sb", &k, "nocreate"); return; }
  prep(h, f, theme);
  ctlfont("statusbar", h, f, theme);
  if (parts) { int p[3] = {100, 200, -1}; SendMessageW(h, SB_SETPARTS, 3, (LPARAM)p); }
  SendMessageW(h, SB_SETTEXTW, 0, (LPARAM)L"Ready");
  SendMessageW(h, WM_SIZE, 0, 0);
  RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr); rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.bottom);
  RECT r; int ok = (int)SendMessageW(h, SB_GETRECT, 0, (LPARAM)&r); rv_rect(&v, &r, ok);
  int b[3] = {NA_SENT, NA_SENT, NA_SENT}; SendMessageW(h, SB_GETBORDERS, 0, (LPARAM)b); rv_s32(&v, b[0]); rv_s32(&v, b[1]); rv_s32(&v, b[2]);
  rec_row("sb", &k, &v);
  DestroyWindow(h);
}

/* ---------------------------------------------------------------- rebar */
static void rb_one(const fspec *f, const char *theme, const char *cfg, DWORD style, int child){
  HWND h = mk(REBARCLASSNAMEW, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | RBS_VARHEIGHT | CCS_NODIVIDER | style, 0, 0, 0, 400, 0, g_top, 302, NULL);
  rk k; kb(&k, cfg, f, theme); rv v; rv_reset(&v);
  if (!h) { ch_fail("rb", &k, "nocreate"); return; }
  prep(h, f, theme);
  ctlfont("rebar", h, f, theme);
  REBARINFO ri; memset(&ri, 0, sizeof ri); ri.cbSize = sizeof ri; SendMessageW(h, RB_SETBARINFO, 0, (LPARAM)&ri);
  HWND c = NULL;
  if (child == 1) c = mk(L"EDIT", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 0, 0, 200, 20, h, 303, L"edit");
  if (child == 2) c = mk(L"COMBOBOX", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 0, 0, 0, 200, 200, h, 304, NULL);
  if (c) prep(c, f, theme);
  REBARBANDINFOW bi; memset(&bi, 0, sizeof bi);
  bi.cbSize = g_cc6 ? sizeof bi : REBARBANDINFOW_V3_SIZE;
  bi.fMask = RBBIM_TEXT | RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE;
  bi.fStyle = RBBS_CHILDEDGE; bi.lpText = (LPWSTR)L"Address"; bi.hwndChild = c; bi.cx = 300;
  RECT cr = {0, 0, 0, 0}; if (c) GetWindowRect(c, &cr);
  bi.cxMinChild = c ? 100 : 0; bi.cyMinChild = c ? cr.bottom - cr.top : 0;
  SendMessageW(h, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&bi);
  rv_i(&v, (int)SendMessageW(h, RB_GETBARHEIGHT, 0, 0));
  rv_i(&v, (int)SendMessageW(h, RB_GETROWHEIGHT, 0, 0));
  RECT r; int ok = (int)SendMessageW(h, RB_GETRECT, 0, (LPARAM)&r); rv_rect(&v, &r, ok);
  RECT bb = {NA_SENT, NA_SENT, NA_SENT, NA_SENT}; SendMessageW(h, RB_GETBANDBORDERS, 0, (LPARAM)&bb);
  rv_s32(&v, bb.left); rv_s32(&v, bb.top); rv_s32(&v, bb.right); rv_s32(&v, bb.bottom);
  MARGINS bm = {NA_SENT, NA_SENT, NA_SENT, NA_SENT}; SendMessageW(h, RB_GETBANDMARGINS, 0, (LPARAM)&bm);
  rv_s32(&v, bm.cxLeftWidth); rv_s32(&v, bm.cyTopHeight); rv_s32(&v, bm.cxRightWidth); rv_s32(&v, bm.cyBottomHeight);
  RECT wr; GetWindowRect(h, &wr); rv_i(&v, wr.bottom - wr.top);
  rv_ok(&v, c != NULL, cr.bottom - cr.top);
  rec_row("rb", &k, &v);
  DestroyWindow(h);
}

/* ---------------------------------------------------------------- tab */
static const struct { const char *n; DWORD s; int ntab; int il; int quick; } TBC[] = {
  {"single", 0, 3, 0, 1}, {"multi", TCS_MULTILINE, 8, 0, 1}, {"buttons", TCS_BUTTONS, 3, 0, 0},
  {"fixed", TCS_FIXEDWIDTH, 3, 0, 0}, {"il16", 0, 3, 16, 0}, {"bottom", TCS_BOTTOM, 3, 0, 0},
};
static void tab_one(const fspec *f, const char *theme, int i){
  HWND h = mk(WC_TABCONTROLW, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBC[i].s, 0, 0, 0, 300, 200, g_top, 305, NULL);
  rk k; kb(&k, TBC[i].n, f, theme); rv v; rv_reset(&v);
  if (!h) { ch_fail("tab", &k, "nocreate"); return; }
  prep(h, f, theme);
  ctlfont("tab", h, f, theme);
  HIMAGELIST il = NULL;
  if (TBC[i].il) { il = mkil(TBC[i].il, TBC[i].il, 4); SendMessageW(h, TCM_SETIMAGELIST, 0, (LPARAM)il); }
  static const WCHAR *T[] = {L"General", L"Advanced", L"A longer tab caption", L"Security", L"Details", L"Sharing", L"Previous Versions", L"Customize"};
  for (int j = 0; j < TBC[i].ntab; j++) {
    TCITEMW it; memset(&it, 0, sizeof it); it.mask = TCIF_TEXT | (il ? TCIF_IMAGE : 0); it.pszText = (LPWSTR)T[j]; it.iImage = 0;
    SendMessageW(h, TCM_INSERTITEMW, j, (LPARAM)&it);
  }
  RECT r0, r1, rn; int n = TBC[i].ntab - 1;
  int o0 = (int)SendMessageW(h, TCM_GETITEMRECT, 0, (LPARAM)&r0), o1 = (int)SendMessageW(h, TCM_GETITEMRECT, 1, (LPARAM)&r1), on = (int)SendMessageW(h, TCM_GETITEMRECT, n, (LPARAM)&rn);
  rv_rect(&v, &r0, o0); rv_ok(&v, o1, r1.left); rv_ok(&v, o1, r1.right); rv_rect(&v, &rn, on);
  rv_i(&v, (int)SendMessageW(h, TCM_GETROWCOUNT, 0, 0));
  RECT a = {0, 0, 300, 200}; SendMessageW(h, TCM_ADJUSTRECT, FALSE, (LPARAM)&a); rv_rect(&v, &a, 1);
  RECT b = {0, 0, 200, 100}; SendMessageW(h, TCM_ADJUSTRECT, TRUE, (LPARAM)&b); rv_rect(&v, &b, 1);
  rec_row("tab", &k, &v);
  DestroyWindow(h); if (il) ImageList_Destroy(il);
}

void run_bar(const fspec *f, const char *theme){
  static const int ILS[] = {0, 16, 24, 32};
  for (unsigned s = 0; s < sizeof TBS / sizeof *TBS; s++) for (int il = 0; il < 4; il++) for (int t = 0; t < 2; t++) {
    if (!g_full && (!TBS[s].quick || ILS[il] != 16)) continue;
    tb_one(f, theme, s, ILS[il], t);
  }
  sb_one(f, theme, "simple", 0, 0);
  if (g_full) { sb_one(f, theme, "parts", 0, 1); sb_one(f, theme, "grip_parts", SBARS_SIZEGRIP, 1); }
  rb_one(f, theme, "text_only", 0, 0);
  rb_one(f, theme, "edit20", 0, 1);
  if (g_full) { rb_one(f, theme, "combo", 0, 2); rb_one(f, theme, "edit20_bandborders", RBS_BANDBORDERS, 1); }
  for (unsigned i = 0; i < sizeof TBC / sizeof *TBC; i++) if (g_full || TBC[i].quick) tab_one(f, theme, i);
}

/* ---------------------------------------------------------------- font-independent: trackbar, up-down, progress */
void run_bar_fontless(const char *theme){
  static const struct { const char *n; DWORD s; int w, h; int thlen; } TK[] = {
    {"horz", 0, 200, 30, 0}, {"horz45", 0, 200, 45, 0}, {"noticks", TBS_NOTICKS, 200, 30, 0}, {"both", TBS_BOTH, 200, 40, 0},
    {"top", TBS_TOP, 200, 30, 0}, {"vert", TBS_VERT, 30, 200, 0}, {"selrange", TBS_ENABLESELRANGE, 200, 30, 0},
    {"fixed30", TBS_FIXEDLENGTH, 200, 40, 30},
  };
  for (unsigned i = 0; i < sizeof TK / sizeof *TK; i++) {
    HWND h = mk(TRACKBAR_CLASSW, WS_CHILD | WS_VISIBLE | TK[i].s, 0, 10, 10, TK[i].w, TK[i].h, g_top, 400, NULL);
    rk k; rk_reset(&k); rk_s(&k, "theme", theme); rk_s(&k, "cfg", TK[i].n); rv v; rv_reset(&v);
    if (!h) { ch_fail("trk", &k, "nocreate"); continue; }
    prep(h, NULL, theme);
    if (TK[i].thlen) SendMessageW(h, TBM_SETTHUMBLENGTH, TK[i].thlen, 0);
    RECT t, c; SendMessageW(h, TBM_GETTHUMBRECT, 0, (LPARAM)&t); SendMessageW(h, TBM_GETCHANNELRECT, 0, (LPARAM)&c);
    rv_rect(&v, &t, 1); rv_rect(&v, &c, 1); rv_i(&v, (int)SendMessageW(h, TBM_GETTHUMBLENGTH, 0, 0));
    rec_row("trk", &k, &v);
    DestroyWindow(h);
  }
  static const struct { const char *n; DWORD s; int buddy; } UD[] = {
    {"alignright", UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS, 1}, {"alignleft", UDS_ALIGNLEFT | UDS_SETBUDDYINT, 1},
    {"horz_right", UDS_HORZ | UDS_ALIGNRIGHT, 1}, {"nobuddy", UDS_ARROWKEYS, 0},
  };
  for (unsigned i = 0; i < sizeof UD / sizeof *UD; i++) {
    HWND e = UD[i].buddy ? mk(L"EDIT", WS_CHILD | WS_VISIBLE | ES_NUMBER, WS_EX_CLIENTEDGE, 50, 10, 100, 23, g_top, 401, L"5") : NULL;
    if (e) prep(e, NULL, theme);
    HWND h = mk(UPDOWN_CLASSW, WS_CHILD | WS_VISIBLE | UD[i].s, 0, 0, 0, 0, 0, g_top, 402, NULL);
    rk k; rk_reset(&k); rk_s(&k, "theme", theme); rk_s(&k, "cfg", UD[i].n); rv v; rv_reset(&v);
    if (!h) { ch_fail("ud", &k, "nocreate"); if (e) DestroyWindow(e); continue; }
    prep(h, NULL, theme);
    if (e) SendMessageW(h, UDM_SETBUDDY, (WPARAM)e, 0);
    RECT r; rel_rect(h, &r); rv_i(&v, r.left); rv_i(&v, r.top); rv_i(&v, r.right - r.left); rv_i(&v, r.bottom - r.top);
    if (e) { rel_rect(e, &r); rv_i(&v, r.left); rv_i(&v, r.top); rv_i(&v, r.right - r.left); rv_i(&v, r.bottom - r.top); }
    else for (int j = 0; j < 4; j++) rv_na(&v);
    rec_row("ud", &k, &v);
    DestroyWindow(h); if (e) DestroyWindow(e);
  }
  static const struct { const char *n; DWORD s; int w, h; } PB[] = { {"horz", 0, 200, 18}, {"smooth", PBS_SMOOTH, 200, 18}, {"vert", PBS_VERTICAL, 18, 100} };
  for (unsigned i = 0; i < sizeof PB / sizeof *PB; i++) {
    HWND h = mk(PROGRESS_CLASSW, WS_CHILD | WS_VISIBLE | PB[i].s, 0, 10, 10, PB[i].w, PB[i].h, g_top, 403, NULL);
    rk k; rk_reset(&k); rk_s(&k, "theme", theme); rk_s(&k, "cfg", PB[i].n); rv v; rv_reset(&v);
    if (!h) { ch_fail("pb", &k, "nocreate"); continue; }
    prep(h, NULL, theme);
    SendMessageW(h, PBM_SETPOS, 50, 0);
    RECT wr; GetWindowRect(h, &wr); rv_i(&v, wr.right - wr.left); rv_i(&v, wr.bottom - wr.top);
    rec_row("pb", &k, &v);
    if (g_full) ink("progress", PB[i].n, h, NULL, theme);
    DestroyWindow(h);
  }
}
