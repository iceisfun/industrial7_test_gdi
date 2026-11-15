/* char_placement: GetCharacterPlacementW / GetCharacterPlacementA with every GCP_ flag combination that is meaningful
 * for Latin text, on the standard faces and a fixed set of strings. Numbers only (no pixels).
 *
 * Tables (all keys carry the conditions; values are positional per the S line):
 *   font    face;req_h                                  | got h a d il el        GetTextFaceA + TEXTMETRICA of the realised font (screen-compatible memory DC)
 *   nat     api;face;req_h;s                            | gcp0 cx cy extent_cx extent_cy      return value (cy<<16|cx) of flags 0 and GetTextExtentPoint32W
 *   gcp     api;face;req_h;s;combo                      | flags maxext ret cx cy nglyphs nmaxfit dx caret glyphs out order class
 *   gcpm    api;face;req_h;s;mode                       | ret cx cy nglyphs nmaxfit array      which output pointers are supplied (NULL results, only lpDx ...)
 * api = W or A (A converts the string with the ANSI code page first). `combo` names a flag set and the nMaxExtent used (ext=nat+k is
 * relative to the flags-0 width of the same string/face; the absolute value is in column maxext). Every output array is pre-filled
 * with a sentinel (0x7777), so an element the call does not write shows as 30583 (dx, caret, order), 0x7777 (glyphs, out) or 0x77 (class).
 * Arrays are the first n elements, comma separated (dx and caret signed decimal, glyphs and out hex, order decimal, class decimal);
 * nglyphs and nmaxfit are the structure fields after the call (nglyphs is set to the character count before the call; nmaxfit to 0x5555).
 * ret column is the raw DWORD in hex (0 = failure; else cy<<16 | cx).
 * usage: char_placement.exe [recorder options] */
#include "rec.h"
#include <stdio.h>
#include <string.h>

#define SENT 0x7777

typedef struct { const char *id; const WCHAR *s; } tstr;
static const tstr g_str[] = {
  {"word",   L"Hello"},
  {"avat",   L"AVAT"},
  {"kern2",  L"To AV Ty"},
  {"spaces", L"a  b   c"},
  {"digits", L"0123456789"},
  {"accent", L"\x00C0\x00E9\x00F1\x00FC\x00DF"},
  {"sent",   L"Hello world"},
};
#define NSTR ((int)(sizeof g_str / sizeof g_str[0]))

/* ext: 0 not passed (0), 1 nat/2, 2 nat-1, 3 nat, 4 nat+7, 5 nat+60 ; cls: -1 lpClass is output-only, else the pre-filled class byte */
typedef struct { const char *name; DWORD fl; int ext; int cls; } combo;
static const combo g_combo[] = {
  {"flags0",           0,                                             0, -1},
  {"flags0_ext_half",  0,                                             1, -1},
  {"kern",             GCP_USEKERNING,                                0, -1},
  {"maxext_half",      GCP_MAXEXTENT,                                 1, -1},
  {"maxext_nat1",      GCP_MAXEXTENT,                                 2, -1},
  {"maxext_nat",       GCP_MAXEXTENT,                                 3, -1},
  {"maxext_nat7",      GCP_MAXEXTENT,                                 4, -1},
  {"maxext_nat60",     GCP_MAXEXTENT,                                 5, -1},
  {"kern_maxext_half", GCP_USEKERNING | GCP_MAXEXTENT,                1, -1},
  {"justify",          GCP_JUSTIFY,                                   0, -1},
  {"justify_ext_nat7", GCP_JUSTIFY,                                   4, -1},
  {"just_max_half",    GCP_JUSTIFY | GCP_MAXEXTENT,                   1, -1},
  {"just_max_nat",     GCP_JUSTIFY | GCP_MAXEXTENT,                   3, -1},
  {"just_max_nat7",    GCP_JUSTIFY | GCP_MAXEXTENT,                   4, -1},
  {"just_max_nat60",   GCP_JUSTIFY | GCP_MAXEXTENT,                   5, -1},
  {"kern_just_nat60",  GCP_USEKERNING | GCP_JUSTIFY | GCP_MAXEXTENT,  5, -1},
  {"shape",            GCP_GLYPHSHAPE,                                0, -1},
  {"shape_kern",       GCP_GLYPHSHAPE | GCP_USEKERNING,               0, -1},
  {"shape_just_nat60", GCP_GLYPHSHAPE | GCP_JUSTIFY | GCP_MAXEXTENT,  5, -1},
  {"reorder",          GCP_REORDER,                                   0, -1},
  {"ligate",           GCP_LIGATE,                                    0, -1},
  {"diacritic",        GCP_DIACRITIC,                                 0, -1},
  {"numlatin",         GCP_NUMERICSLATIN,                             0, -1},
  {"numlocal",         GCP_NUMERICSLOCAL,                             0, -1},
  {"symswap",          GCP_SYMSWAPOFF,                                0, -1},
  {"classin0",         GCP_CLASSIN,                                   0, 0},
  {"classin_latin",    GCP_CLASSIN,                                   0, GCPCLASS_LATIN},
  {"classin_neutral",  GCP_CLASSIN,                                   0, GCPCLASS_NEUTRAL},
  {"classin_latnum",   GCP_CLASSIN,                                   0, GCPCLASS_LATINNUMBER},
  {"classin_shape",    GCP_CLASSIN | GCP_GLYPHSHAPE,                  0, GCPCLASS_LATIN},
  {"classin_kern_max", GCP_CLASSIN | GCP_USEKERNING | GCP_MAXEXTENT,  4, GCPCLASS_LATIN},
};
#define NCOMBO ((int)(sizeof g_combo / sizeof g_combo[0]))

static const char *g_faces[] = {"Segoe UI", "Tahoma", "Arial", "Times New Roman", "Courier New", "System"};
#define NFACE ((int)(sizeof g_faces / sizeof g_faces[0]))
static const int g_hts[] = {-12, -16};

static HFONT mk(const char *face, int h){
  LOGFONTA lf; memset(&lf, 0, sizeof lf); lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynA(lf.lfFaceName, face, LF_FACESIZE);
  return CreateFontIndirectA(&lf);
}

/* comma lists: out must hold n * 12 chars */
static void list_i(char *o, const INT *a, int n){ o[0] = 0; for(int i = 0; i < n; i++){ char t[16]; sprintf(t, i ? ",%d" : "%d", a[i]); strcat(o, t); } }
static void list_u(char *o, const UINT *a, int n){ o[0] = 0; for(int i = 0; i < n; i++){ char t[16]; sprintf(t, i ? ",%u" : "%u", a[i]); strcat(o, t); } }
static void list_w(char *o, const WORD *a, int n){ o[0] = 0; for(int i = 0; i < n; i++){ char t[16]; sprintf(t, i ? ",%04x" : "%04x", a[i]); strcat(o, t); } }
static void list_b(char *o, const BYTE *a, int n){ o[0] = 0; for(int i = 0; i < n; i++){ char t[16]; sprintf(t, i ? ",%u" : "%u", a[i]); strcat(o, t); } }

typedef struct {
  DWORD ret; int nglyphs, nmaxfit;
  INT dx[64], caret[64]; UINT order[64]; WORD glyphs[64], out[64]; BYTE cls[64];
  int n;
} gres;

/* one call. api 'W' or 'A'. use: bit0 outstring, bit1 order, bit2 dx, bit3 caret, bit4 class, bit5 glyphs ; 0x3F = all. null_results = pass NULL */
static void do_call(HDC dc, char api, const WCHAR *w, DWORD fl, int maxext, int cls, unsigned use, int null_results, gres *g){
  int n = lstrlenW(w);
  char a[64]; WCHAR wo[64]; char ao[64];
  memset(g, 0, sizeof *g); g->n = n;
  for(int i = 0; i < 64; i++){ g->dx[i] = SENT; g->caret[i] = SENT; g->order[i] = SENT; g->glyphs[i] = SENT; g->out[i] = SENT; g->cls[i] = (BYTE)(cls >= 0 ? cls : 0x77); wo[i] = SENT; ao[i] = 0x77; }
  if(api == 'A'){ memset(a, 0, sizeof a); WideCharToMultiByte(CP_ACP, 0, w, n, a, sizeof a, NULL, NULL); }
  if(api == 'W'){
    GCP_RESULTSW r; memset(&r, 0, sizeof r); r.lStructSize = sizeof r;
    if(use & 1) r.lpOutString = wo;
    if(use & 2) r.lpOrder = g->order;
    if(use & 4) r.lpDx = g->dx;
    if(use & 8) r.lpCaretPos = g->caret;
    if(use & 16) r.lpClass = (LPSTR)g->cls;
    if(use & 32) r.lpGlyphs = g->glyphs;
    r.nGlyphs = n; r.nMaxFit = 0x5555;
    g->ret = GetCharacterPlacementW(dc, w, n, maxext, null_results ? NULL : &r, fl);
    g->nglyphs = (int)r.nGlyphs; g->nmaxfit = r.nMaxFit;
    for(int i = 0; i < n; i++) g->out[i] = wo[i];
  } else {
    GCP_RESULTSA r; memset(&r, 0, sizeof r); r.lStructSize = sizeof r;
    if(use & 1) r.lpOutString = ao;
    if(use & 2) r.lpOrder = g->order;
    if(use & 4) r.lpDx = g->dx;
    if(use & 8) r.lpCaretPos = g->caret;
    if(use & 16) r.lpClass = (LPSTR)g->cls;
    if(use & 32) r.lpGlyphs = (LPWSTR)g->glyphs;
    r.nGlyphs = n; r.nMaxFit = 0x5555;
    g->ret = GetCharacterPlacementA(dc, a, n, maxext, null_results ? NULL : &r, fl);
    g->nglyphs = (int)r.nGlyphs; g->nmaxfit = r.nMaxFit;
    for(int i = 0; i < n; i++) g->out[i] = (BYTE)ao[i];
  }
}

static void put_res(rv *v, const gres *g, int cls_shown){
  char t[1024];
  rv_x(v, g->ret); rv_i(v, (int)(g->ret & 0xFFFF)); rv_i(v, (int)(g->ret >> 16)); rv_i(v, g->nglyphs); rv_i(v, g->nmaxfit);
  list_i(t, g->dx, g->n); rv_raw(v, t); list_i(t, g->caret, g->n); rv_raw(v, t); list_w(t, g->glyphs, g->n); rv_raw(v, t);
  list_w(t, g->out, g->n); rv_raw(v, t); list_u(t, g->order, g->n); rv_raw(v, t); list_b(t, g->cls, g->n); rv_raw(v, t);
}

static int ext_for(int kind, int nat){
  switch(kind){ case 1: return nat / 2; case 2: return nat - 1; case 3: return nat; case 4: return nat + 7; case 5: return nat + 60; }
  return 0;
}

int main(int argc, char **argv){
  rec_init(&argc, argv, "char_placement", "1");
  rec_schema("font", "face req_h", "got h:px a:px d:px il:px el:px");
  rec_schema("nat", "api face req_h s", "gcp0 cx:px cy:px extent_cx:px extent_cy:px");
  rec_schema("gcp", "api face req_h s combo", "flags maxext:px ret cx:px cy:px nglyphs nmaxfit dx caret glyphs out order class");
  rec_schema("gcpm", "api face req_h s mode", "ret cx:px cy:px nglyphs nmaxfit array");
  HDC sdc = GetDC(NULL); HDC dc = CreateCompatibleDC(sdc); ReleaseDC(NULL, sdc);
  SetMapMode(dc, MM_TEXT);
  for(int fi = 0; fi < NFACE; fi++) for(int hi = 0; hi < 2; hi++){
    const char *face = g_faces[fi]; int h = g_hts[hi];
    HFONT f = mk(face, h); HFONT of = (HFONT)SelectObject(dc, f);
    { rk k; rk_reset(&k); rk_s(&k, "face", face); rk_i(&k, "req_h", h);
      char got[LF_FACESIZE] = ""; TEXTMETRICA tm; int okt = GetTextMetricsA(dc, &tm); int okf = GetTextFaceA(dc, sizeof got, got) > 0;
      rv v; rv_reset(&v); if(okf) rv_s(&v, got); else rv_na(&v);
      if(okt){ rv_i(&v, tm.tmHeight); rv_i(&v, tm.tmAscent); rv_i(&v, tm.tmDescent); rv_i(&v, tm.tmInternalLeading); rv_i(&v, tm.tmExternalLeading); }
      else for(int i = 0; i < 5; i++) rv_na(&v);
      rec_row("font", &k, &v); }
    for(int si = 0; si < NSTR; si++){
      const WCHAR *s = g_str[si].s;
      for(int ai = 0; ai < 2; ai++){
        char api = ai ? 'A' : 'W';
        gres g0; do_call(dc, api, s, 0, 0, -1, 0x3F, 0, &g0);
        int nat = (int)(g0.ret & 0xFFFF);
        { SIZE sz = {0, 0}; GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
          rk k; rk_reset(&k); rk_s(&k, "api", api == 'W' ? "W" : "A"); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id);
          rv v; rv_reset(&v); rv_x(&v, g0.ret); rv_i(&v, (int)(g0.ret & 0xFFFF)); rv_i(&v, (int)(g0.ret >> 16)); rv_i(&v, sz.cx); rv_i(&v, sz.cy);
          rec_row("nat", &k, &v); }
        for(int ci = 0; ci < NCOMBO; ci++){
          const combo *c = &g_combo[ci]; int me = ext_for(c->ext, nat);
          gres g; do_call(dc, api, s, c->fl, me, c->cls, 0x3F, 0, &g);
          rk k; rk_reset(&k); rk_s(&k, "api", api == 'W' ? "W" : "A"); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "combo", c->name);
          rv v; rv_reset(&v); rv_x(&v, c->fl); rv_i(&v, me); put_res(&v, &g, 1);
          rec_row("gcp", &k, &v);
        }
      }
      /* which pointers are supplied (W, flags 0 unless named) */
      if(si < 2){
        static const struct { const char *m; DWORD fl; unsigned use; int nul; int what; } md[] = {
          {"results_null", 0, 0, 1, 0}, {"none", 0, 0, 0, 0}, {"dx_only", 0, 4, 0, 1}, {"caret_only", 0, 8, 0, 2},
          {"glyphs_only_shape", GCP_GLYPHSHAPE, 32, 0, 3}, {"glyphs_only_flags0", 0, 32, 0, 3}, {"out_only", 0, 1, 0, 4},
          {"class_only", 0, 16, 0, 5}, {"order_only", GCP_REORDER, 2, 0, 6},
        };
        for(unsigned mi = 0; mi < sizeof md / sizeof *md; mi++){
          gres g; do_call(dc, 'W', s, md[mi].fl, 0, -1, md[mi].use, md[mi].nul, &g);
          char t[1024] = "";
          switch(md[mi].what){ case 1: list_i(t, g.dx, g.n); break; case 2: list_i(t, g.caret, g.n); break; case 3: list_w(t, g.glyphs, g.n); break;
            case 4: list_w(t, g.out, g.n); break; case 5: list_b(t, g.cls, g.n); break; case 6: list_u(t, g.order, g.n); break; }
          rk k; rk_reset(&k); rk_s(&k, "api", "W"); rk_s(&k, "face", face); rk_i(&k, "req_h", h); rk_s(&k, "s", g_str[si].id); rk_s(&k, "mode", md[mi].m);
          rv v; rv_reset(&v); rv_x(&v, g.ret); rv_i(&v, (int)(g.ret & 0xFFFF)); rv_i(&v, (int)(g.ret >> 16)); rv_i(&v, g.nglyphs); rv_i(&v, g.nmaxfit);
          if(t[0]) rv_raw(&v, t); else rv_s(&v, "");
          rec_row("gcpm", &k, &v);
        }
      }
    }
    SelectObject(dc, of); DeleteObject(f);
  }
  rec_note("columns: gcp0/ret = raw DWORD (cy<<16|cx; 0 = failure). dx and caret are signed decimal lists, glyphs and out are hex WORD lists, order and class are decimal lists; the sentinel for an unwritten element is 30583 (dx caret order), 7777 hex (glyphs, W out), 0077 (A out), 119 (class). nglyphs/nmaxfit are the GCP_RESULTS fields after the call (inputs: nGlyphs = character count, nMaxFit = 0x5555)");
  DeleteDC(dc);
  return rec_finish(0);
}
