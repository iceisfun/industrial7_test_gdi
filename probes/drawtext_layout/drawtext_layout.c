/* drawtext_layout: DrawTextW / DrawTextExW layout results, numbers and strings only (no pixel is read).
 * A 32bpp memory DIB DC is the target; fonts are created with lfHeight < 0 (character height), DEFAULT_QUALITY.
 *
 * Tables:
 *   font   face;req_h                          | got h a d il el         GetTextFaceA + TEXTMETRICA of the realised font
 *   dtc    face;req_h;s;fl;w                   | ret l t r b             DrawTextW(flags | DT_CALCRECT) on the rectangle (10,20,10+w,20+60)
 *   dtm    face;req_h;s;fl;calc;w              | ret l t r b len str     ellipsis flags with DT_MODIFYSTRING, with (calc=1) and without (calc=0) DT_CALCRECT:
 *                                                                        return, the rectangle after the call, the length and text of the buffer after the call
 *   dtn    face;req_h;s;fl;w                   | ret l t r b len str     DrawTextW without DT_CALCRECT (placement flags, DT_NOCLIP ...); rectangle (10,20,10+w,20+60)
 *   dtx    face;req_h;s;fl;w                   | ret l t r b len str drawn   DrawTextExW with a DRAWTEXTPARAMS; drawn = uiLengthDrawn
 * `fl` is a mnemonic of the flag set (below, in g_fl / g_flm / g_fln / g_flx), `w` the rectangle width in pixels.
 * `str` is the buffer text after the call, or `=` when it equals the input; `len` is its length in WCHARs.
 * ret is the DrawText return value (text height in logical units, 0 on failure). The input rectangle is always at (10,20) so
 * that a changed left/top (alignment flags) shows.
 * usage: drawtext_layout.exe [recorder options] */
#include "rec.h"
#include <stdio.h>
#include <string.h>

typedef struct { const char *id; const WCHAR *s; } tstr;
static const tstr g_str[] = {
  {"word",    L"Hello"},
  {"fox",     L"The quick brown fox jumps over the lazy dog"},
  {"amp",     L"Save &As... && Close"},
  {"tabs",    L"Name\tSize\tType"},
  {"path",    L"C:\\Program Files\\Common Files\\Microsoft Shared\\readme.txt"},
  {"lines",   L"Line one\r\nLine two\nLine three"},
  {"indent",  L"  indent and trailing   "},
  {"longword",L"Supercalifragilisticexpialidocious"},
  {"empty",   L""},
};
#define NSTR ((int)(sizeof g_str / sizeof g_str[0]))
static const int g_w[] = {50, 160};
static const char *g_faces[] = {"Segoe UI", "Tahoma", "Arial", "Times New Roman", "Courier New"};
#define NFACE ((int)(sizeof g_faces / sizeof g_faces[0]))
static const int g_hts[] = {-12, -16};

typedef struct { const char *n; UINT fl; } fl_t;
#define TS(n) (DT_TABSTOP | ((n) << 8))
/* DT_CALCRECT tables */
static const fl_t g_fl[] = {
  {"def", 0}, {"sl", DT_SINGLELINE}, {"wb", DT_WORDBREAK}, {"wb_sl", DT_WORDBREAK | DT_SINGLELINE},
  {"np", DT_NOPREFIX}, {"wb_np", DT_WORDBREAK | DT_NOPREFIX}, {"hide", DT_HIDEPREFIX}, {"pfonly", DT_PREFIXONLY},
  {"hide_sl", DT_HIDEPREFIX | DT_SINGLELINE}, {"pfonly_sl", DT_PREFIXONLY | DT_SINGLELINE},
  {"et", DT_EXPANDTABS}, {"et_ts2", DT_EXPANDTABS | TS(2)}, {"et_ts8", DT_EXPANDTABS | TS(8)}, {"ts4_noet", TS(4)},
  {"et_np", DT_EXPANDTABS | DT_NOPREFIX}, {"et_sl", DT_EXPANDTABS | DT_SINGLELINE},
  {"ee", DT_END_ELLIPSIS}, {"ee_sl", DT_END_ELLIPSIS | DT_SINGLELINE}, {"pe", DT_PATH_ELLIPSIS}, {"pe_sl", DT_PATH_ELLIPSIS | DT_SINGLELINE},
  {"we", DT_WORD_ELLIPSIS}, {"we_sl", DT_WORD_ELLIPSIS | DT_SINGLELINE}, {"ee_wb", DT_END_ELLIPSIS | DT_WORDBREAK},
  {"noclip", DT_NOCLIP}, {"noclip_wb", DT_NOCLIP | DT_WORDBREAK},
  {"ctr", DT_CENTER}, {"right", DT_RIGHT}, {"ctr_sl", DT_CENTER | DT_SINGLELINE}, {"right_sl", DT_RIGHT | DT_SINGLELINE},
  {"vctr_sl", DT_VCENTER | DT_SINGLELINE}, {"bot_sl", DT_BOTTOM | DT_SINGLELINE}, {"vctr_nosl", DT_VCENTER}, {"bot_nosl", DT_BOTTOM},
  {"ctr_vctr_sl", DT_CENTER | DT_VCENTER | DT_SINGLELINE}, {"right_bot_sl", DT_RIGHT | DT_BOTTOM | DT_SINGLELINE},
  {"wb_ctr", DT_WORDBREAK | DT_CENTER}, {"wb_right", DT_WORDBREAK | DT_RIGHT}, {"wb_vctr", DT_WORDBREAK | DT_VCENTER}, {"wb_bot", DT_WORDBREAK | DT_BOTTOM},
  {"el", DT_EXTERNALLEADING}, {"el_wb", DT_EXTERNALLEADING | DT_WORDBREAK}, {"el_sl", DT_EXTERNALLEADING | DT_SINGLELINE},
  {"int", DT_INTERNAL}, {"int_wb", DT_INTERNAL | DT_WORDBREAK}, {"int_sl", DT_INTERNAL | DT_SINGLELINE},
  {"ec_wb", DT_EDITCONTROL | DT_WORDBREAK}, {"ec_wb_sl", DT_EDITCONTROL | DT_WORDBREAK | DT_SINGLELINE},
  {"rtl", DT_RTLREADING}, {"rtl_right", DT_RTLREADING | DT_RIGHT}, {"nfwcb_wb", DT_NOFULLWIDTHCHARBREAK | DT_WORDBREAK},
};
/* ellipsis + DT_MODIFYSTRING (calc and non calc) */
static const fl_t g_flm[] = {
  {"ee", DT_END_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE}, {"ee_ml", DT_END_ELLIPSIS | DT_MODIFYSTRING},
  {"pe", DT_PATH_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE}, {"pe_ml", DT_PATH_ELLIPSIS | DT_MODIFYSTRING},
  {"we", DT_WORD_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE}, {"we_ml", DT_WORD_ELLIPSIS | DT_MODIFYSTRING},
  {"ee_wb", DT_END_ELLIPSIS | DT_MODIFYSTRING | DT_WORDBREAK}, {"ee_np", DT_END_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE | DT_NOPREFIX},
  {"ee_ctr", DT_END_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE | DT_CENTER}, {"pe_right", DT_PATH_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE | DT_RIGHT},
  {"mod_only", DT_MODIFYSTRING | DT_SINGLELINE}, {"ee_nomod", DT_END_ELLIPSIS | DT_SINGLELINE},
};
/* no DT_CALCRECT */
static const fl_t g_fln[] = {
  {"sl", DT_SINGLELINE}, {"sl_np", DT_SINGLELINE | DT_NOPREFIX}, {"wb", DT_WORDBREAK}, {"def", 0}, {"noclip", DT_NOCLIP | DT_SINGLELINE},
  {"ctr_sl", DT_CENTER | DT_SINGLELINE}, {"right_sl", DT_RIGHT | DT_SINGLELINE}, {"vctr_sl", DT_VCENTER | DT_SINGLELINE},
  {"bot_sl", DT_BOTTOM | DT_SINGLELINE}, {"ctr_vctr_sl", DT_CENTER | DT_VCENTER | DT_SINGLELINE}, {"wb_ctr", DT_WORDBREAK | DT_CENTER},
  {"wb_vctr", DT_WORDBREAK | DT_VCENTER}, {"wb_bot", DT_WORDBREAK | DT_BOTTOM}, {"et_ts4", DT_EXPANDTABS | TS(4)}, {"el_wb", DT_EXTERNALLEADING | DT_WORDBREAK},
  {"int_wb", DT_INTERNAL | DT_WORDBREAK}, {"pfonly", DT_PREFIXONLY | DT_SINGLELINE},
};
/* DrawTextEx: name, flags, left margin, right margin, tab length (-1 = no params) */
typedef struct { const char *n; UINT fl; int lm, rm, tl; } flx_t;
static const flx_t g_flx[] = {
  {"noparams_wb", DT_WORDBREAK | DT_CALCRECT, 0, 0, -1},
  {"zero_wb_calc", DT_WORDBREAK | DT_CALCRECT, 0, 0, 0},
  {"margin3_5_wb_calc", DT_WORDBREAK | DT_CALCRECT, 3, 5, 0},
  {"margin3_5_wb", DT_WORDBREAK, 3, 5, 0},
  {"margin3_5_sl_calc", DT_SINGLELINE | DT_CALCRECT, 3, 5, 0},
  {"et_tl2_calc", DT_EXPANDTABS | DT_TABSTOP | DT_CALCRECT, 0, 0, 2},
  {"et_tl6_calc", DT_EXPANDTABS | DT_TABSTOP | DT_CALCRECT, 0, 0, 6},
  {"et_tl0_calc", DT_EXPANDTABS | DT_TABSTOP | DT_CALCRECT, 0, 0, 0},
  {"et_tl4_noparams_calc", DT_EXPANDTABS | DT_TABSTOP | DT_CALCRECT | (4 << 8), 0, 0, -1},
  {"ee_mod_sl", DT_END_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE, 3, 5, 0},
  {"ee_mod_sl_calc", DT_END_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE | DT_CALCRECT, 3, 5, 0},
  {"def_h20", 0, 0, 0, 0},
  {"wb_h20", DT_WORDBREAK, 0, 0, 0},
};

static HFONT mk(const char *face, int h){
  LOGFONTA lf; memset(&lf, 0, sizeof lf); lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynA(lf.lfFaceName, face, LF_FACESIZE);
  return CreateFontIndirectA(&lf);
}

/* rect after / buffer text after */
static void put_rect(rv *v, int ret, const RECT *r){ rv_i(v, ret); rv_i(v, r->left); rv_i(v, r->top); rv_i(v, r->right); rv_i(v, r->bottom); }
static void put_str(rv *v, const WCHAR *in, const WCHAR *buf){
  int n = lstrlenW(buf);
  rv_i(v, n);
  if(!lstrcmpW(in, buf)) rv_raw(v, "="); else rv_w(v, buf);
}

int main(int argc, char **argv){
  rec_init(&argc, argv, "drawtext_layout", "1");
  rec_schema("font", "face req_h", "got h:px a:px d:px il:px el:px");
  rec_schema("dtc", "face req_h s fl w", "ret:px l:px t:px r:px b:px");
  rec_schema("dtm", "face req_h s fl calc w", "ret:px l:px t:px r:px b:px len str");
  rec_schema("dtn", "face req_h s fl w", "ret:px l:px t:px r:px b:px len str");
  rec_schema("dtx", "face req_h s fl w", "ret:px l:px t:px r:px b:px len str drawn");
  rec_dib d; if(!rec_dib_create(&d, 600, 300)){ rec_note("dib create failed"); return rec_finish(1); }
  HDC dc = d.dc;
  SetBkMode(dc, TRANSPARENT);
  for(int fi = 0; fi < NFACE; fi++) for(int hi = 0; hi < 2; hi++){
    const char *face = g_faces[fi]; int h = g_hts[hi];
    HFONT f = mk(face, h); HFONT of = (HFONT)SelectObject(dc, f);
    { rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h);
      char got[LF_FACESIZE] = ""; TEXTMETRICA tm; int okt = GetTextMetricsA(dc, &tm); int okf = GetTextFaceA(dc, sizeof got, got) > 0;
      rv v; rv_reset(&v); if(okf) rv_s(&v, got); else rv_na(&v);
      if(okt){ rv_i(&v, tm.tmHeight); rv_i(&v, tm.tmAscent); rv_i(&v, tm.tmDescent); rv_i(&v, tm.tmInternalLeading); rv_i(&v, tm.tmExternalLeading); }
      else for(int i = 0; i < 5; i++) rv_na(&v);
      rec_row("font", &k, &v); }
    WCHAR buf[256];
    for(int si = 0; si < NSTR; si++) for(int wi = 0; wi < 2; wi++){
      int w = g_w[wi];
      for(unsigned fl = 0; fl < sizeof g_fl / sizeof *g_fl; fl++){
        lstrcpyW(buf, g_str[si].s);
        RECT r = {10, 20, 10 + w, 20 + 60};
        int ret = DrawTextW(dc, buf, -1, &r, g_fl[fl].fl | DT_CALCRECT);
        rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "fl", g_fl[fl].n); rk_i(&k, "w", w);
        rv v; rv_reset(&v); put_rect(&v, ret, &r); rec_row("dtc", &k, &v);
      }
      for(unsigned fl = 0; fl < sizeof g_flm / sizeof *g_flm; fl++) for(int calc = 0; calc < 2; calc++){
        lstrcpyW(buf, g_str[si].s);
        RECT r = {10, 20, 10 + w, 20 + 60};
        int ret = DrawTextW(dc, buf, -1, &r, g_flm[fl].fl | (calc ? DT_CALCRECT : 0));
        rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "fl", g_flm[fl].n); rk_i(&k, "calc", calc); rk_i(&k, "w", w);
        rv v; rv_reset(&v); put_rect(&v, ret, &r); put_str(&v, g_str[si].s, buf); rec_row("dtm", &k, &v);
      }
      for(unsigned fl = 0; fl < sizeof g_fln / sizeof *g_fln; fl++){
        lstrcpyW(buf, g_str[si].s);
        RECT r = {10, 20, 10 + w, 20 + 60};
        int ret = DrawTextW(dc, buf, -1, &r, g_fln[fl].fl);
        rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "fl", g_fln[fl].n); rk_i(&k, "w", w);
        rv v; rv_reset(&v); put_rect(&v, ret, &r); put_str(&v, g_str[si].s, buf); rec_row("dtn", &k, &v);
      }
      for(unsigned fl = 0; fl < sizeof g_flx / sizeof *g_flx; fl++){
        lstrcpyW(buf, g_str[si].s);
        int hh = (g_flx[fl].n[0] == 'd' || g_flx[fl].n[0] == 'w') && strstr(g_flx[fl].n, "_h20") ? 20 : 60;
        RECT r = {10, 20, 10 + w, 20 + hh};
        DRAWTEXTPARAMS p; memset(&p, 0, sizeof p); p.cbSize = sizeof p; p.iLeftMargin = g_flx[fl].lm; p.iRightMargin = g_flx[fl].rm; p.iTabLength = g_flx[fl].tl < 0 ? 0 : g_flx[fl].tl;
        p.uiLengthDrawn = 0xFFFF;
        int ret = DrawTextExW(dc, buf, -1, &r, g_flx[fl].fl, g_flx[fl].tl < 0 ? NULL : &p);
        rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "fl", g_flx[fl].n); rk_i(&k, "w", w);
        rv v; rv_reset(&v); put_rect(&v, ret, &r); put_str(&v, g_str[si].s, buf);
        if(g_flx[fl].tl < 0) rv_na(&v); else rv_i(&v, (int)p.uiLengthDrawn);
        rec_row("dtx", &k, &v);
      }
    }
    SelectObject(dc, of); DeleteObject(f);
  }
  rec_note("rectangle in (10,20,10+w,20+60) (def_h20 and wb_h20 of dtx: height 20); str '=' = buffer unchanged; drawn '!' = no DRAWTEXTPARAMS passed; uiLengthDrawn pre-set to 0xFFFF");
  rec_dib_free(&d);
  return rec_finish(0);
}
