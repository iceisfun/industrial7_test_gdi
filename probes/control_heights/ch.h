/* control_heights: shared declarations (see ch_main.c for the overview). */
#ifndef CH_H
#define CH_H
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define _WIN32_IE 0x0A00
#include "rec.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CH_VERSION "1"
#define NA_SENT (-123456)       /* sentinel: struct field the control did not fill -> "!" */

/* A font condition. kind: 0 = no WM_SETFONT (control default), 1 = CreateFontIndirect(face, px),
 * 2 = CreateFontIndirect(face, -MulDiv(pt, LOGPIXELSY, 72)), 3 = GetStockObject(stock),
 * 4 = NONCLIENTMETRICS / icon-title LOGFONT (ncm = 0 caption, 1 smcaption, 2 menu, 3 status, 4 message, 5 icontitle) */
typedef struct {
  const char *name; int kind; const char *face; int px; int pt; int stock; int ncm;
  int quick;   /* in --quick */
  int ctl;     /* used for the control matrix (else font table only) */
  HFONT h;     /* realised (NULL for kind 0) */
  int req_h;   /* lfHeight passed / found */
} fspec;

extern int g_full;          /* --full (default) vs --quick */
extern int g_cc6;           /* comctl32 major >= 6 */
extern int g_dpi;           /* LOGPIXELSY of the screen DC in this process's DPI context */
extern HWND g_top;          /* hidden top-level parent */
extern HINSTANCE g_hi;
extern HDC g_dc;            /* screen DC used for text measurement */
extern fspec *g_fonts; extern int g_nfonts;
extern const char *g_themes[2]; extern int g_nthemes;

/* helpers (ch_main.c) */
HWND mk(const WCHAR *cls, DWORD style, DWORD ex, int x, int y, int w, int h, HWND parent, int id, const WCHAR *text);
void prep(HWND h, const fspec *f, const char *theme);          /* SetWindowTheme(off) then WM_SETFONT */
void kb(rk *k, const char *ctl_cfg_name, const fspec *f, const char *theme); /* font;theme;cfg */
void ctlfont(const char *ctl, HWND h, const fspec *f, const char *theme);    /* default-font table, deduped */
HIMAGELIST mkil(int cx, int cy, int n);
void rv_rect(rv *v, const RECT *r, int ok);
void rv_ok(rv *v, int ok, long long x);
void rv_s32(rv *v, int x);                                     /* NA_SENT -> "!" */
void rel_rect(HWND child, RECT *r);                            /* window rect in parent client coords */
void pump(void);
void ink(const char *ctl, const char *cfg, HWND h, const fspec *f, const char *theme);
int  fam_on(const char *fam);                                  /* --only filter */

/* families */
void lv_schemas(void);  void run_lv(const fspec *f, const char *theme);
void hdr_schemas(void); void run_hdr(const fspec *f, const char *theme);
void tv_schemas(void);  void run_tv(const fspec *f, const char *theme);
void ctl_schemas(void); void run_ctl(const fspec *f, const char *theme);
void bar_schemas(void); void run_bar(const fspec *f, const char *theme);
void run_bar_fontless(const char *theme);
void modal_schemas(void); void run_modal(void);
#endif
