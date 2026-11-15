/* exttextout_extents: ExtTextOutW with ETO_CLIPPED / ETO_OPAQUE / ETO_GLYPH_INDEX / ETO_PDY and lpDx, measured as the bounding box of
 * the pixels it touched in a 32bpp DIB (white background), plus the text extent and the current position afterwards.
 * Adds to tests/text_output_extents (which pins opaque fill, clip, dx and glyph-index equality for Arial 18): this records the numbers
 * for four faces and the cases that area does not pin (ETO_PDY pairs, negative and zero advances, partial clips, a rectangle
 * without ETO_CLIPPED/ETO_OPAQUE, current position after an UPDATECP call with dx).
 *
 *   font   face;req_h                       | got h a d il el      realised font (NONANTIALIASED_QUALITY)
 *   eto    face;req_h;s;mode               | ok ext_cx ext_cy ink_l ink_t ink_r ink_b ink_n bk_l bk_t bk_r bk_b bk_n oth_n cp_x cp_y
 * Drawing: DIB 300x100 white, text colour black, background colour RGB(255,0,0), TRANSPARENT background mode unless mode says
 * OPAQUE-via-flag (the rectangle fill uses the background colour either way), TA_UPDATECP with the current position set to
 * (20,40) (so x,y arguments are ignored and cp_x/cp_y report the advance; text is placed at the position).
 * ink_* = bounding box (inclusive l,t and exclusive r,b) and count of pure black pixels; bk_* the same for pure red pixels;
 * oth_n = count of any other non-white pixel (0 with NONANTIALIASED_QUALITY). A box with count 0 is `!` (no pixel).
 * ext_* = GetTextExtentPoint32W of the string (glyph-index modes: of the glyph-index array through GetTextExtentPointI).
 * `mode` names the call: see g_mode. rect = ETO rectangle (l,t,r,b) given in the table; "ext" is the string extent box at (20,40).
 * usage: exttextout_extents.exe [recorder options] */
#include "rec.h"
#include <stdio.h>
#include <string.h>

typedef struct { const char *id; const WCHAR *s; } tstr;
static const tstr g_str[] = {
  {"word", L"Hello"}, {"avat", L"AVAT"}, {"wgj", L"Wg.j"}, {"spaces", L"a  b"},
};
#define NSTR ((int)(sizeof g_str / sizeof g_str[0]))
static const char *g_faces[] = {"Segoe UI", "Arial", "Times New Roman", "Courier New"};
#define NFACE ((int)(sizeof g_faces / sizeof g_faces[0]))
#define OX 20
#define OY 40

/* dxk: 0 NULL, 1 natural advances, 2 natural+2, 3 natural-1, 4 zero, 5 all 12, 6 = {natural, 0, natural, ...} (alternating zero)
 * rk: 0 no rectangle, 1 text box, 2 left half of the text box, 3 text box grown by 4 on each side, 4 box shrunk by 3 on every side,
 *     5 only the glyph rows' top half, 6 empty rectangle (l=r)
 * fl: ETO_ flags; pdy: dx array is (dx,dy) pairs; dyk: 0 none, 1 +1 per glyph, 2 -2 per glyph, 3 +3 on the second glyph only */
typedef struct { const char *n; UINT fl; int dxk, rk, dyk, gi, nostr; } mode_t_;
static const mode_t_ g_mode[] = {
  {"plain",            0,                                   0, 0, 0, 0, 0},
  {"dx_nat",           0,                                   1, 0, 0, 0, 0},
  {"dx_plus2",         0,                                   2, 0, 0, 0, 0},
  {"dx_minus1",        0,                                   3, 0, 0, 0, 0},
  {"dx_zero",          0,                                   4, 0, 0, 0, 0},
  {"dx_alt0",          0,                                   6, 0, 0, 0, 0},
  {"rect_noflag",      0,                                   0, 2, 0, 0, 0},
  {"clip_box",         ETO_CLIPPED,                         0, 1, 0, 0, 0},
  {"clip_half",        ETO_CLIPPED,                         0, 2, 0, 0, 0},
  {"clip_shrunk",      ETO_CLIPPED,                         0, 4, 0, 0, 0},
  {"clip_tophalf",     ETO_CLIPPED,                         0, 5, 0, 0, 0},
  {"clip_empty",       ETO_CLIPPED,                         0, 6, 0, 0, 0},
  {"clip_dx_plus2",    ETO_CLIPPED,                         2, 2, 0, 0, 0},
  {"opaque_box",       ETO_OPAQUE,                          0, 1, 0, 0, 0},
  {"opaque_grown",     ETO_OPAQUE,                          0, 3, 0, 0, 0},
  {"opaque_shrunk",    ETO_OPAQUE,                          0, 4, 0, 0, 0},
  {"opaque_empty",     ETO_OPAQUE,                          0, 6, 0, 0, 0},
  {"opaque_nostr",     ETO_OPAQUE,                          0, 1, 0, 0, 1},
  {"opaque_clip_half", ETO_OPAQUE | ETO_CLIPPED,            0, 2, 0, 0, 0},
  {"opaque_clip_grown",ETO_OPAQUE | ETO_CLIPPED,            0, 3, 0, 0, 0},
  {"opaque_dx_plus2",  ETO_OPAQUE,                          2, 1, 0, 0, 0},
  {"gi_plain",         ETO_GLYPH_INDEX,                     0, 0, 0, 1, 0},
  {"gi_dx_plus2",      ETO_GLYPH_INDEX,                     2, 0, 0, 1, 0},
  {"gi_clip_half",     ETO_GLYPH_INDEX | ETO_CLIPPED,       0, 2, 0, 1, 0},
  {"gi_opaque_box",    ETO_GLYPH_INDEX | ETO_OPAQUE,        0, 1, 0, 1, 0},
  {"pdy_zero",         ETO_PDY,                             1, 0, 0, 0, 0},
  {"pdy_plus1",        ETO_PDY,                             1, 0, 1, 0, 0},
  {"pdy_minus2",       ETO_PDY,                             1, 0, 2, 0, 0},
  {"pdy_step3",        ETO_PDY,                             1, 0, 3, 0, 0},
  {"pdy_dx_plus2",     ETO_PDY,                             2, 0, 1, 0, 0},
  {"gi_pdy_plus1",     ETO_GLYPH_INDEX | ETO_PDY,           1, 0, 1, 1, 0},
  {"pdy_nodx",         ETO_PDY,                             0, 0, 0, 0, 0},
  {"pdy_clip_half",    ETO_PDY | ETO_CLIPPED,               1, 2, 1, 0, 0},
};
#define NMODE ((int)(sizeof g_mode / sizeof g_mode[0]))

static HFONT mk(const char *face, int h){
  LOGFONTA lf; memset(&lf, 0, sizeof lf); lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = NONANTIALIASED_QUALITY;
  lstrcpynA(lf.lfFaceName, face, LF_FACESIZE);
  return CreateFontIndirectA(&lf);
}

typedef struct { int l, t, r, b, n; } box;
static void box_add(box *b, int x, int y){ if(!b->n){ b->l = x; b->t = y; b->r = x + 1; b->b = y + 1; } else { if(x < b->l) b->l = x; if(y < b->t) b->t = y; if(x + 1 > b->r) b->r = x + 1; if(y + 1 > b->b) b->b = y + 1; } b->n++; }
static void put_box(rv *v, const box *b){ if(!b->n){ for(int i = 0; i < 4; i++) rv_na(v); rv_i(v, 0); } else { rv_i(v, b->l); rv_i(v, b->t); rv_i(v, b->r); rv_i(v, b->b); rv_i(v, b->n); } }

int main(int argc, char **argv){
  rec_init(&argc, argv, "exttextout_extents", "1");
  rec_schema("font", "face req_h", "got h:px a:px d:px il:px el:px");
  rec_schema("eto", "face req_h s mode", "ok ext_cx:px ext_cy:px ink_l:px ink_t:px ink_r:px ink_b:px ink_n bk_l:px bk_t:px bk_r:px bk_b:px bk_n oth_n cp_x:px cp_y:px");
  rec_dib d; if(!rec_dib_create(&d, 300, 100)){ rec_note("dib create failed"); return rec_finish(1); }
  HDC dc = d.dc;
  static const int hts[] = {-16, -24};
  for(int fi = 0; fi < NFACE; fi++) for(int hi = 0; hi < 2; hi++){
    const char *face = g_faces[fi]; int h = hts[hi];
    HFONT f = mk(face, h); HFONT of = (HFONT)SelectObject(dc, f);
    TEXTMETRICA tm; int okt = GetTextMetricsA(dc, &tm);
    { rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h);
      char got[LF_FACESIZE] = ""; int okf = GetTextFaceA(dc, sizeof got, got) > 0;
      rv v; rv_reset(&v); if(okf) rv_s(&v, got); else rv_na(&v);
      if(okt){ rv_i(&v, tm.tmHeight); rv_i(&v, tm.tmAscent); rv_i(&v, tm.tmDescent); rv_i(&v, tm.tmInternalLeading); rv_i(&v, tm.tmExternalLeading); }
      else for(int i = 0; i < 5; i++) rv_na(&v);
      rec_row("font", &k, &v); }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0, 0, 0)); SetBkColor(dc, RGB(255, 0, 0));
    for(int si = 0; si < NSTR; si++){
      const WCHAR *s = g_str[si].s; int n = lstrlenW(s);
      WORD gi[16]; INT nat[16]; SIZE esz = {0, 0};
      GetGlyphIndicesW(dc, s, n, gi, GGI_MARK_NONEXISTING_GLYPHS);
      for(int i = 0; i < n; i++){ SIZE c; GetTextExtentPoint32W(dc, s + i, 1, &c); nat[i] = c.cx; }
      GetTextExtentPoint32W(dc, s, n, &esz);
      for(int mi = 0; mi < NMODE; mi++){
        const mode_t_ *m = &g_mode[mi];
        rec_dib_clear(&d, 0x00FFFFFF);
        /* dx array: n entries, or 2n with ETO_PDY (dx,dy) */
        INT dx[64]; int pdy = (m->fl & ETO_PDY) != 0;
        for(int i = 0; i < n; i++){
          int x = nat[i];
          switch(m->dxk){ case 2: x = nat[i] + 2; break; case 3: x = nat[i] - 1; break; case 4: x = 0; break; case 6: x = (i & 1) ? 0 : nat[i]; break; }
          int y = 0;
          switch(m->dyk){ case 1: y = 1; break; case 2: y = -2; break; case 3: y = (i == 1) ? 3 : 0; break; }
          if(pdy){ dx[2 * i] = x; dx[2 * i + 1] = y; } else dx[i] = x;
        }
        int ascent = okt ? tm.tmAscent : 0, th = okt ? tm.tmHeight : 0;
        RECT rc = {0, 0, 0, 0}; int use_rc = 1;
        int bl = OX, bt = OY, br = OX + esz.cx, bb = OY + th;
        switch(m->rk){
          case 0: use_rc = 0; break;
          case 1: rc = (RECT){bl, bt, br, bb}; break;
          case 2: rc = (RECT){bl, bt, bl + esz.cx / 2, bb}; break;
          case 3: rc = (RECT){bl - 4, bt - 4, br + 4, bb + 4}; break;
          case 4: rc = (RECT){bl + 3, bt + 3, br - 3, bb - 3}; break;
          case 5: rc = (RECT){bl, bt, br, bt + th / 2}; break;
          case 6: rc = (RECT){bl + 5, bt, bl + 5, bb}; break;
        }
        SetTextAlign(dc, TA_UPDATECP | TA_LEFT | TA_TOP);
        MoveToEx(dc, OX, OY, NULL);
        int ok;
        if(m->nostr) ok = ExtTextOutW(dc, 0, 0, m->fl, use_rc ? &rc : NULL, NULL, 0, NULL);
        else ok = ExtTextOutW(dc, 0, 0, m->fl, use_rc ? &rc : NULL, m->gi ? (LPCWSTR)gi : s, n, m->dxk ? dx : NULL);
        POINT cp = {0, 0}; GetCurrentPositionEx(dc, &cp);
        SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
        GdiFlush();
        box ink = {0}, bk = {0}; int oth = 0;
        for(int y = 0; y < 100; y++) for(int x = 0; x < 300; x++){
          uint32_t p = d.bits[y * 300 + x] & 0xFFFFFF;
          if(p == 0xFFFFFF) continue;
          if(p == 0x000000) box_add(&ink, x, y); else if(p == 0xFF0000) box_add(&bk, x, y); else oth++;
        }
        SIZE ez = esz;
        if(m->gi){ if(!GetTextExtentPointI(dc, gi, n, &ez)) ez.cx = ez.cy = -1; }
        rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "mode", m->n);
        rv v; rv_reset(&v); rv_i(&v, ok); rv_i(&v, ez.cx); rv_i(&v, ez.cy); put_box(&v, &ink); put_box(&v, &bk); rv_i(&v, oth); rv_i(&v, cp.x); rv_i(&v, cp.y);
        rec_row("eto", &k, &v);
      }
    }
    SelectObject(dc, of); DeleteObject(f);
  }
  rec_note("start position (20,40) via MoveToEx with TA_UPDATECP; ink = pure black, bk = pure red (the background colour of OPAQUE fills), box r/b exclusive; '!' box = no such pixel; modes: see g_mode in the probe source (rect kinds: text box = extent at the start, half = left half, grown/shrunk = +-4/3 px, tophalf, empty = zero width)");
  rec_dib_free(&d);
  return rec_finish(0);
}
