/* control_heights: SysListView32, SysHeader32 (standalone), SysTreeView32. */
#include "ch.h"

/* ---------------------------------------------------------------- list view */
typedef struct { const char *name; int view; DWORD ex; int ilsm; int ilnorm; int nohdr; int quick; } lvcfg;
static const char *VIEWN[] = {"icon", "report", "smallicon", "list"};   /* LVS_ICON 0, LVS_REPORT 1, LVS_SMALLICON 2, LVS_LIST 3 */
static const lvcfg LVC[] = {
  {"plain",   1, 0,                     0,  0, 0, 1},
  {"il16",    1, 0,                     16, 0, 0, 1},
  {"cb",      1, LVS_EX_CHECKBOXES,     0,  0, 0, 1},
  {"il16cb",  1, LVS_EX_CHECKBOXES,     16, 0, 0, 0},
  {"fullrow", 1, LVS_EX_FULLROWSELECT,  0,  0, 0, 0},
  {"grid",    1, LVS_EX_GRIDLINES,      0,  0, 0, 0},
  {"dbuf",    1, LVS_EX_DOUBLEBUFFER,   0,  0, 0, 0},
  {"frgd",    1, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER, 0, 0, 0, 1},
  {"frgdil16",1, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER, 16, 0, 0, 0},
  {"nohdr",   1, 0,                     0,  0, 1, 1},
  {"il12",    1, 0,                     12, 0, 0, 0},
  {"il24",    1, 0,                     24, 0, 0, 0},
  {"il32",    1, 0,                     32, 0, 0, 0},
  {"plain",   3, 0,                     0,  0, 0, 1},
  {"il16",    3, 0,                     16, 0, 0, 0},
  {"cb",      3, LVS_EX_CHECKBOXES,     0,  0, 0, 0},
  {"plain",   2, 0,                     0,  0, 0, 0},
  {"il16",    2, 0,                     16, 0, 0, 0},
  {"plain",   0, 0,                     0,  0, 0, 1},
  {"il32",    0, 0,                     0, 32, 0, 0},
  {"il48",    0, 0,                     0, 48, 0, 0},
  {"cb",      0, LVS_EX_CHECKBOXES,     0, 32, 0, 0},
};
static const WCHAR *ITEMS[] = {L"Item one", L"Item two", L"Third item"};

void lv_schemas(void){
  rec_schema("lv", "font theme view cfg",
    "r0_l:px r0_t:px r0_r:px r0_b:px r1_t:px r2_t:px row_h:px lbl_l:px lbl_t:px lbl_r:px lbl_b:px ico_l:px ico_t:px ico_r:px ico_b:px "
    "sel_l:px sel_r:px sub1_l:px sub1_r:px spc_cx:px spc_cy:px spcsm_cx:px spcsm_cy:px avr_cx:px avr_cy:px vr_r:px vr_b:px cpp strw:px "
    "hdr_vis hdr_h:px hdr_lay_cy:px hdr_lay_ret");
  rec_schema("lv_auto", "font theme cfg str", "w0_auto:px w1_auto:px w0_hdr:px w1_hdr:px w2_fill:px strw:px hstrw0:px hstrw1:px");
}

static HWND lv_make(const fspec *f, const char *theme, int view, DWORD ex, int nohdr, HIMAGELIST *ils, int ilsm, int ilnorm){
  HWND h = mk(WC_LISTVIEWW, WS_CHILD | WS_VISIBLE | view | LVS_SHAREIMAGELISTS | (nohdr ? LVS_NOCOLUMNHEADER : 0), 0, 0, 0, 400, 300, g_top, 100, NULL);
  if (!h) return NULL;
  if (!strcmp(theme, "off")) { SetWindowTheme(h, L"", L""); HWND hd = (HWND)SendMessageW(h, LVM_GETHEADER, 0, 0); if (hd) SetWindowTheme(hd, L"", L""); }
  if (f->h) SendMessageW(h, WM_SETFONT, (WPARAM)f->h, TRUE);
  if (ex) SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, ex, ex);
  ils[0] = ils[1] = NULL;
  if (ilsm) { ils[0] = mkil(ilsm, ilsm, 4); SendMessageW(h, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ils[0]); }
  if (ilnorm) { ils[1] = mkil(ilnorm, ilnorm, 4); SendMessageW(h, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)ils[1]); }
  return h;
}
static void lv_free(HWND h, HIMAGELIST *ils){
  DestroyWindow(h);
  if (ils[0]) ImageList_Destroy(ils[0]);
  if (ils[1]) ImageList_Destroy(ils[1]);
}

static void lv_one(const fspec *f, const char *theme, const lvcfg *c){
  HIMAGELIST ils[2];
  HWND h = lv_make(f, theme, c->view, c->ex, c->nohdr, ils, c->ilsm, c->ilnorm);
  rk k; kb(&k, NULL, f, theme); rk_s(&k, "view", VIEWN[c->view]); rk_s(&k, "cfg", c->name);
  rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("lv", &k, &v); return; }
  ctlfont("listview", h, f, theme);
  LVCOLUMNW col; memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH;
  col.pszText = (LPWSTR)L"Name"; col.cx = 150; SendMessageW(h, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
  col.pszText = (LPWSTR)L"Size"; col.cx = 80;  SendMessageW(h, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
  for (int i = 0; i < 3; i++) {
    LVITEMW it; memset(&it, 0, sizeof it); it.mask = LVIF_TEXT | ((c->ilsm || c->ilnorm) ? LVIF_IMAGE : 0);
    it.iItem = i; it.pszText = (LPWSTR)ITEMS[i]; it.iImage = 0;
    SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)&it);
    LVITEMW si; memset(&si, 0, sizeof si); si.iSubItem = 1; si.pszText = (LPWSTR)L"1 KB";
    SendMessageW(h, LVM_SETITEMTEXTW, i, (LPARAM)&si);
  }
  RECT r0, r1, r2, lb, ic, sel, sub;
  r0.left = LVIR_BOUNDS; int ok0 = (int)SendMessageW(h, LVM_GETITEMRECT, 0, (LPARAM)&r0);
  r1.left = LVIR_BOUNDS; int ok1 = (int)SendMessageW(h, LVM_GETITEMRECT, 1, (LPARAM)&r1);
  r2.left = LVIR_BOUNDS; int ok2 = (int)SendMessageW(h, LVM_GETITEMRECT, 2, (LPARAM)&r2);
  lb.left = LVIR_LABEL;  int okl = (int)SendMessageW(h, LVM_GETITEMRECT, 0, (LPARAM)&lb);
  ic.left = LVIR_ICON;   int oki = (int)SendMessageW(h, LVM_GETITEMRECT, 0, (LPARAM)&ic);
  sel.left = LVIR_SELECTBOUNDS; int oks = (int)SendMessageW(h, LVM_GETITEMRECT, 0, (LPARAM)&sel);
  sub.left = LVIR_BOUNDS; sub.top = 1; int oksub = c->view == 1 ? (int)SendMessageW(h, LVM_GETSUBITEMRECT, 0, (LPARAM)&sub) : 0;
  rv_rect(&v, &r0, ok0); rv_ok(&v, ok1, r1.top); rv_ok(&v, ok2, r2.top); rv_ok(&v, ok0, r0.bottom - r0.top);
  rv_rect(&v, &lb, okl); rv_rect(&v, &ic, oki);
  rv_ok(&v, oks, sel.left); rv_ok(&v, oks, sel.right);
  rv_ok(&v, oksub, sub.left); rv_ok(&v, oksub, sub.right);
  DWORD sp = (DWORD)SendMessageW(h, LVM_GETITEMSPACING, FALSE, 0), sps = (DWORD)SendMessageW(h, LVM_GETITEMSPACING, TRUE, 0);
  rv_i(&v, LOWORD(sp)); rv_i(&v, HIWORD(sp)); rv_i(&v, LOWORD(sps)); rv_i(&v, HIWORD(sps));
  DWORD avr = (DWORD)SendMessageW(h, LVM_APPROXIMATEVIEWRECT, 3, MAKELPARAM(-1, -1));
  rv_i(&v, LOWORD(avr)); rv_i(&v, HIWORD(avr));
  RECT vr; int okv = (int)SendMessageW(h, LVM_GETVIEWRECT, 0, (LPARAM)&vr);
  rv_ok(&v, okv, vr.right); rv_ok(&v, okv, vr.bottom);
  rv_i(&v, (int)SendMessageW(h, LVM_GETCOUNTPERPAGE, 0, 0));
  rv_i(&v, (int)SendMessageW(h, LVM_GETSTRINGWIDTHW, 0, (LPARAM)L"Item one"));
  HWND hd = (HWND)SendMessageW(h, LVM_GETHEADER, 0, 0);
  if (hd) {
    RECT wr; GetWindowRect(hd, &wr);
    RECT rc = {0, 0, 400, 300}; WINDOWPOS wp; memset(&wp, 0, sizeof wp); HDLAYOUT hl = {&rc, &wp};
    int lr = (int)SendMessageW(hd, HDM_LAYOUT, 0, (LPARAM)&hl);
    rv_i(&v, IsWindowVisible(hd) ? 1 : 0); rv_i(&v, wr.bottom - wr.top); rv_i(&v, wp.cy); rv_i(&v, lr);
  } else { rv_raw(&v, "nohdr"); rv_na(&v); rv_na(&v); rv_na(&v); }
  rec_row("lv", &k, &v);
  lv_free(h, ils);
}

static void lv_auto(const fspec *f, const char *theme, const char *cfg, int il, const char *sname, const WCHAR *s){
  HIMAGELIST ils[2];
  HWND h = lv_make(f, theme, LVS_REPORT, 0, 0, ils, il, 0);
  rk k; kb(&k, cfg, f, theme); rk_s(&k, "str", sname);
  rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("lv_auto", &k, &v); return; }
  LVCOLUMNW col; memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH;
  col.pszText = (LPWSTR)L"Hdr"; col.cx = 60; SendMessageW(h, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
  col.pszText = (LPWSTR)L"A long column header"; col.cx = 60; SendMessageW(h, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
  col.pszText = (LPWSTR)L"x"; col.cx = 50; SendMessageW(h, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
  LVITEMW it; memset(&it, 0, sizeof it); it.mask = LVIF_TEXT | (il ? LVIF_IMAGE : 0); it.pszText = (LPWSTR)s;
  SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)&it);
  LVITEMW si; memset(&si, 0, sizeof si); si.iSubItem = 1; si.pszText = (LPWSTR)s; SendMessageW(h, LVM_SETITEMTEXTW, 0, (LPARAM)&si);
  SendMessageW(h, LVM_SETCOLUMNWIDTH, 0, LVSCW_AUTOSIZE);           rv_i(&v, (int)SendMessageW(h, LVM_GETCOLUMNWIDTH, 0, 0));
  SendMessageW(h, LVM_SETCOLUMNWIDTH, 1, LVSCW_AUTOSIZE);           rv_i(&v, (int)SendMessageW(h, LVM_GETCOLUMNWIDTH, 1, 0));
  SendMessageW(h, LVM_SETCOLUMNWIDTH, 0, LVSCW_AUTOSIZE_USEHEADER); rv_i(&v, (int)SendMessageW(h, LVM_GETCOLUMNWIDTH, 0, 0));
  SendMessageW(h, LVM_SETCOLUMNWIDTH, 1, LVSCW_AUTOSIZE_USEHEADER); rv_i(&v, (int)SendMessageW(h, LVM_GETCOLUMNWIDTH, 1, 0));
  SendMessageW(h, LVM_SETCOLUMNWIDTH, 2, LVSCW_AUTOSIZE_USEHEADER); rv_i(&v, (int)SendMessageW(h, LVM_GETCOLUMNWIDTH, 2, 0));
  rv_i(&v, (int)SendMessageW(h, LVM_GETSTRINGWIDTHW, 0, (LPARAM)s));
  rv_i(&v, (int)SendMessageW(h, LVM_GETSTRINGWIDTHW, 0, (LPARAM)L"Hdr"));
  rv_i(&v, (int)SendMessageW(h, LVM_GETSTRINGWIDTHW, 0, (LPARAM)L"A long column header"));
  rec_row("lv_auto", &k, &v);
  lv_free(h, ils);
}

void run_lv(const fspec *f, const char *theme){
  for (unsigned i = 0; i < sizeof LVC / sizeof *LVC; i++) if (g_full || LVC[i].quick) lv_one(f, theme, &LVC[i]);
  static const struct { const char *n; const WCHAR *s; int quick; } S[] = {
    {"W", L"W", 1}, {"Name", L"Name", 0}, {"item", L"Item one", 1}, {"fox", L"The quick brown fox jumps", 0},
    {"M10", L"MMMMMMMMMM", 0}, {"i10", L"iiiiiiiiii", 0}, {"empty", L"", 0} };
  for (unsigned i = 0; i < sizeof S / sizeof *S; i++) {
    if (!g_full && !S[i].quick) continue;
    lv_auto(f, theme, "plain", 0, S[i].n, S[i].s);
    if (g_full) lv_auto(f, theme, "il16", 16, S[i].n, S[i].s);
  }
}

/* ---------------------------------------------------------------- header */
typedef struct { const char *name; DWORD style; int il; int fmt; int quick; } hdcfg;
static const hdcfg HDC_[] = {
  {"plain",    HDS_HORZ, 0, 0, 1},
  {"buttons",  HDS_BUTTONS, 0, 0, 1},
  {"flat",     HDS_FLAT, 0, 0, 0},
  {"btnflat",  HDS_BUTTONS | HDS_FLAT, 0, 0, 0},
  {"il16",     HDS_BUTTONS, 16, 0, 1},
  {"il24",     HDS_BUTTONS, 24, 0, 0},
  {"sortup",   HDS_BUTTONS, 0, HDF_SORTUP, 1},
  {"sortdown", HDS_BUTTONS, 0, HDF_SORTDOWN, 0},
  {"il16sort", HDS_BUTTONS, 16, HDF_SORTUP, 0},
  {"split",    HDS_BUTTONS, 0, HDF_SPLITBUTTON, 0},
  {"filter",   HDS_BUTTONS | HDS_FILTERBAR, 0, 0, 0},
  {"hidden",   HDS_BUTTONS | HDS_HIDDEN, 0, 0, 0},
};
void hdr_schemas(void){
  rec_schema("hdr", "font theme cfg",
    "lay_ret lay_x:px lay_y:px lay_cx:px lay_cy:px lay_flags rc_top:px win_h:px cli_h:px "
    "it0_l:px it0_t:px it0_r:px it0_b:px it1_l:px it1_r:px bmpmargin:px");
}
static void hdr_one(const fspec *f, const char *theme, const hdcfg *c){
  HWND h = mk(WC_HEADERW, WS_CHILD | WS_VISIBLE | c->style, 0, 0, 0, 400, 30, g_top, 101, NULL);
  rk k; kb(&k, c->name, f, theme); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("hdr", &k, &v); return; }
  prep(h, f, theme);
  ctlfont("header", h, f, theme);
  HIMAGELIST il = NULL;
  if (c->il) { il = mkil(c->il, c->il, 4); SendMessageW(h, HDM_SETIMAGELIST, HDSIL_NORMAL, (LPARAM)il); }
  static const WCHAR *T[] = {L"Name", L"Size"};
  for (int i = 0; i < 2; i++) {
    HDITEMW it; memset(&it, 0, sizeof it); it.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT | (il ? HDI_IMAGE : 0);
    it.cxy = i ? 80 : 120; it.pszText = (LPWSTR)T[i]; it.fmt = HDF_STRING | HDF_LEFT | (i == 1 ? c->fmt : 0) | (il ? HDF_IMAGE : 0); it.iImage = 0;
    SendMessageW(h, HDM_INSERTITEMW, i, (LPARAM)&it);
  }
  RECT rc = {0, 0, 400, 300}; WINDOWPOS wp; memset(&wp, 0, sizeof wp); HDLAYOUT hl = {&rc, &wp};
  int lr = (int)SendMessageW(h, HDM_LAYOUT, 0, (LPARAM)&hl);
  rv_i(&v, lr); rv_i(&v, wp.x); rv_i(&v, wp.y); rv_i(&v, wp.cx); rv_i(&v, wp.cy); rv_x(&v, wp.flags); rv_i(&v, rc.top);
  SetWindowPos(h, wp.hwndInsertAfter, wp.x, wp.y, wp.cx, wp.cy, wp.flags);
  RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr);
  rv_i(&v, wr.bottom - wr.top); rv_i(&v, cr.bottom - cr.top);
  RECT i0, i1; int o0 = (int)SendMessageW(h, HDM_GETITEMRECT, 0, (LPARAM)&i0), o1 = (int)SendMessageW(h, HDM_GETITEMRECT, 1, (LPARAM)&i1);
  rv_rect(&v, &i0, o0); rv_ok(&v, o1, i1.left); rv_ok(&v, o1, i1.right);
  rv_i(&v, (int)SendMessageW(h, HDM_GETBITMAPMARGIN, 0, 0));
  rec_row("hdr", &k, &v);
  DestroyWindow(h); if (il) ImageList_Destroy(il);
}
void run_hdr(const fspec *f, const char *theme){
  for (unsigned i = 0; i < sizeof HDC_ / sizeof *HDC_; i++) if (g_full || HDC_[i].quick) hdr_one(f, theme, &HDC_[i]);
}

/* ---------------------------------------------------------------- tree view */
typedef struct { const char *name; DWORD style; int cb; int il; int quick; } tvcfg;
#define TV_LINES (TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT)
static const tvcfg TVC[] = {
  {"plain",     0, 0, 0, 1},
  {"lines",     TV_LINES, 0, 0, 1},
  {"cb",        0, 1, 0, 1},
  {"il16",      0, 0, 16, 1},
  {"il16cb",    0, 1, 16, 0},
  {"lines_il16",TV_LINES, 0, 16, 0},
  {"il24",      0, 0, 24, 0},
  {"il12",      0, 0, 12, 0},
  {"noneven",   TVS_NONEVENHEIGHT, 0, 0, 0},
  {"fullrow",   TVS_FULLROWSELECT, 0, 0, 0},
};
void tv_schemas(void){
  rec_schema("tv", "font theme cfg",
    "ih:px indent:px root_l:px root_t:px root_r:px root_b:px rtxt_l:px rtxt_t:px rtxt_r:px rtxt_b:px "
    "ctxt_l:px ctxt_t:px ctxt_r:px ctxt_b:px visible st_cx:px st_cy:px");
}
static void tv_one(const fspec *f, const char *theme, const tvcfg *c){
  HWND h = mk(WC_TREEVIEWW, WS_CHILD | WS_VISIBLE | c->style, 0, 0, 0, 300, 300, g_top, 102, NULL);
  rk k; kb(&k, c->name, f, theme); rv v; rv_reset(&v);
  if (!h) { rv_raw(&v, "nocreate"); rec_row("tv", &k, &v); return; }
  if (c->cb) SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | TVS_CHECKBOXES);
  prep(h, f, theme);
  ctlfont("treeview", h, f, theme);
  HIMAGELIST il = NULL;
  if (c->il) { il = mkil(c->il, c->il, 4); SendMessageW(h, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)il); }
  TVINSERTSTRUCTW is; memset(&is, 0, sizeof is);
  is.hParent = TVI_ROOT; is.hInsertAfter = TVI_LAST; is.item.mask = TVIF_TEXT | (il ? TVIF_IMAGE | TVIF_SELECTEDIMAGE : 0);
  is.item.pszText = (LPWSTR)L"Root item";
  HTREEITEM root = (HTREEITEM)SendMessageW(h, TVM_INSERTITEMW, 0, (LPARAM)&is);
  is.hParent = root; is.item.pszText = (LPWSTR)L"Child item";
  HTREEITEM child = (HTREEITEM)SendMessageW(h, TVM_INSERTITEMW, 0, (LPARAM)&is);
  SendMessageW(h, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
  rv_i(&v, (int)SendMessageW(h, TVM_GETITEMHEIGHT, 0, 0));
  rv_i(&v, (int)SendMessageW(h, TVM_GETINDENT, 0, 0));
  RECT r; int ok;
  *(HTREEITEM *)&r = root;  ok = (int)SendMessageW(h, TVM_GETITEMRECT, FALSE, (LPARAM)&r); rv_rect(&v, &r, ok);
  *(HTREEITEM *)&r = root;  ok = (int)SendMessageW(h, TVM_GETITEMRECT, TRUE, (LPARAM)&r);  rv_rect(&v, &r, ok);
  *(HTREEITEM *)&r = child; ok = (int)SendMessageW(h, TVM_GETITEMRECT, TRUE, (LPARAM)&r);  rv_rect(&v, &r, ok);
  rv_i(&v, (int)SendMessageW(h, TVM_GETVISIBLECOUNT, 0, 0));
  HIMAGELIST st = (HIMAGELIST)SendMessageW(h, TVM_GETIMAGELIST, TVSIL_STATE, 0);
  int cx, cy; if (st && ImageList_GetIconSize(st, &cx, &cy)) { rv_i(&v, cx); rv_i(&v, cy); } else { rv_na(&v); rv_na(&v); }
  rec_row("tv", &k, &v);
  DestroyWindow(h); if (il) ImageList_Destroy(il);
  if (c->cb && st) ImageList_Destroy(st);   /* TVS_CHECKBOXES: the state image list outlives the control */
}
void run_tv(const fspec *f, const char *theme){
  for (unsigned i = 0; i < sizeof TVC / sizeof *TVC; i++) if (g_full || TVC[i].quick) tv_one(f, theme, &TVC[i]);
}
