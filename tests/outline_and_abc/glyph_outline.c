#include "oa_common.h"
/* GetGlyphOutlineA/W/Wow. Expectations and sources: EVIDENCE.md in this directory. */

static const WCHAR *k_tt[] = { L"Arial", L"Times New Roman" };

static int adv32(HDC dc, UINT ch){ INT w = -1; if(!U(GetCharWidth32W)(dc, ch, ch, &w)) return -1; return w; }

/* Walk a GGO_NATIVE/GGO_BEZIER stream. Returns 1 when the stream is well formed (all sizes consistent). */
static int walk_outline(const BYTE *b, DWORD size, int bezier, int *nc, int *nline, int *nq, int *nc3){
  DWORD off = 0; *nc = *nline = *nq = *nc3 = 0;
  while(off < size){
    const TTPOLYGONHEADER *h = (const TTPOLYGONHEADER *)(b + off);
    DWORD end, p;
    if(size - off < sizeof *h) return 0;
    if(h->dwType != TT_POLYGON_TYPE) return 0;
    if(h->cb < sizeof *h || h->cb > size - off) return 0;
    end = off + h->cb; p = off + sizeof *h;
    while(p < end){
      const TTPOLYCURVE *c = (const TTPOLYCURVE *)(b + p);
      DWORD cbc;
      if(end - p < 4 + 8) return 0;
      cbc = 4 + (DWORD)c->cpfx * 8;
      if(c->cpfx == 0 || cbc > end - p) return 0;
      if(c->wType == TT_PRIM_LINE) (*nline)++;
      else if(c->wType == TT_PRIM_QSPLINE){ if(bezier) return 0; (*nq)++; }
      else if(c->wType == 3 /* TT_PRIM_CSPLINE */){ if(!bezier) return 0; (*nc3)++; }
      else return 0;
      p += cbc;
    }
    if(p != end) return 0;
    (*nc)++; off = end;
  }
  return off == size;
}

TEST(go_metrics_vs_width){
  int i;
  for(i = 0; i < 2; i++){
    oa_font d; GLYPHMETRICS g; DWORD r; int adv; UINT ch;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    for(ch = 'A'; ch <= 'Z'; ch++){
      memset(&g, 0xcc, sizeof g);
      r = U(GetGlyphOutlineW)(d.dc, ch, GGO_METRICS, &g, 0, NULL, &oa_ident);
      adv = adv32(d.dc, ch);
      CHECKF(r != GDI_ERROR, "%ls '%c' GGO_METRICS failed", k_tt[i], ch);
      if(r == GDI_ERROR) continue;
      CHECKF(g.gmCellIncX == adv, "%ls '%c' gmCellIncX %d vs GetCharWidth32 %d", k_tt[i], ch, g.gmCellIncX, adv);
      CHECKF(g.gmCellIncY == 0, "%ls '%c' gmCellIncY %d", k_tt[i], ch, g.gmCellIncY);
      CHECKF(g.gmBlackBoxX > 0 && g.gmBlackBoxY > 0, "%ls '%c' black box %ux%u", k_tt[i], ch, g.gmBlackBoxX, g.gmBlackBoxY);
      CHECKF(g.gmBlackBoxY <= (UINT)d.tm.tmHeight, "%ls '%c' black box height %u > tmHeight %ld", k_tt[i], ch, g.gmBlackBoxY, d.tm.tmHeight);
      CHECKF(g.gmptGlyphOrigin.y > 0 && g.gmptGlyphOrigin.y <= d.tm.tmAscent, "%ls '%c' origin.y %ld tmAscent %ld", k_tt[i], ch, g.gmptGlyphOrigin.y, d.tm.tmAscent);
      if(ch == 'A') t_note("%ls 'A' GGO_METRICS ret %lu: bb %ux%u org %ld,%ld inc %d,%d", k_tt[i], r, g.gmBlackBoxX, g.gmBlackBoxY, g.gmptGlyphOrigin.x, g.gmptGlyphOrigin.y, g.gmCellIncX, g.gmCellIncY);
    }
    /* the ANSI entry point agrees with the Unicode one for ASCII */
    for(ch = 'A'; ch <= 'Z'; ch++){
      GLYPHMETRICS ga, gw; DWORD ra, rw;
      ra = U(GetGlyphOutlineA)(d.dc, ch, GGO_METRICS, &ga, 0, NULL, &oa_ident);
      rw = U(GetGlyphOutlineW)(d.dc, ch, GGO_METRICS, &gw, 0, NULL, &oa_ident);
      CHECKF(ra == rw && !memcmp(&ga, &gw, sizeof ga), "%ls '%c' A/W GGO_METRICS differ", k_tt[i], ch);
    }
    oa_close(&d);
  }
}

TEST(go_native_stream){
  int i;
  for(i = 0; i < 2; i++){
    oa_font d; DWORD size, r; BYTE *buf; GLYPHMETRICS g1, g2; int nc, nl, nq, n3; UINT ch; static const UINT cs[] = { 'A', 'O', 'g', '8', '&' };
    int k;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    for(k = 0; k < 5; k++){
      ch = cs[k];
      size = U(GetGlyphOutlineW)(d.dc, ch, GGO_NATIVE, &g1, 0, NULL, &oa_ident);
      CHECKF(size != GDI_ERROR && size >= sizeof(TTPOLYGONHEADER), "%ls '%c' native size query %lu", k_tt[i], ch, size);
      if(size == GDI_ERROR || size == 0) continue;
      buf = (BYTE *)calloc(1, size + 8); if(!buf){ t_skip("oom"); break; }
      memset(&g2, 0xcc, sizeof g2);
      r = U(GetGlyphOutlineW)(d.dc, ch, GGO_NATIVE, &g2, size, buf, &oa_ident);
      CHECKF(r == size, "%ls '%c' fill returned %lu, size query %lu", k_tt[i], ch, r, size);
      CHECKF(!memcmp(&g1, &g2, sizeof g1), "%ls '%c' GLYPHMETRICS differ between size query and fill", k_tt[i], ch);
      CHECKF(walk_outline(buf, size, 0, &nc, &nl, &nq, &n3), "%ls '%c' native stream malformed (cb/type walk)", k_tt[i], ch);
      CHECKF(nc >= 1 && (nq + nl) >= 2, "%ls '%c' contours %d lines %d quads %d", k_tt[i], ch, nc, nl, nq);
      if(ch == 'A') CHECKF(nc == 2, "%ls 'A' has %d contours (outer + counter expected)", k_tt[i], nc);
      if(ch == 'O') CHECKF(nc == 2, "%ls 'O' has %d contours", k_tt[i], nc);
      t_note("%ls '%c' native: %lu bytes, %d contours, %d lines, %d quads, bb %ux%u", k_tt[i], ch, size, nc, nl, nq, g1.gmBlackBoxX, g1.gmBlackBoxY);
      /* a buffer that is too small does not succeed */
      if(size > 1){ r = U(GetGlyphOutlineW)(d.dc, ch, GGO_NATIVE, &g2, size - 1, buf, &oa_ident); t_note("%ls '%c' native cb=size-1 returns %lx", k_tt[i], ch, r); }
      /* unhinted and hinted outlines both parse */
      r = U(GetGlyphOutlineW)(d.dc, ch, GGO_NATIVE | GGO_UNHINTED, &g2, 0, NULL, &oa_ident);
      CHECK(r != GDI_ERROR && r >= sizeof(TTPOLYGONHEADER));
      if(r != GDI_ERROR && r){
        BYTE *b2 = (BYTE *)calloc(1, r + 8);
        if(b2){ DWORD r2 = U(GetGlyphOutlineW)(d.dc, ch, GGO_NATIVE | GGO_UNHINTED, &g2, r, b2, &oa_ident);
          CHECK(r2 == r); CHECK(walk_outline(b2, r, 0, &nc, &nl, &nq, &n3)); free(b2); }
      }
      /* GGO_BEZIER: cubic curves only (no quadratic) */
      r = U(GetGlyphOutlineW)(d.dc, ch, GGO_BEZIER, &g2, 0, NULL, &oa_ident);
      CHECKF(r != GDI_ERROR && r >= sizeof(TTPOLYGONHEADER), "%ls '%c' GGO_BEZIER size %lu", k_tt[i], ch, r);
      if(r != GDI_ERROR && r){
        BYTE *b2 = (BYTE *)calloc(1, r + 8);
        if(b2){ DWORD r2 = U(GetGlyphOutlineW)(d.dc, ch, GGO_BEZIER, &g2, r, b2, &oa_ident);
          CHECK(r2 == r);
          CHECKF(walk_outline(b2, r, 1, &nc, &nl, &nq, &n3), "%ls '%c' bezier stream malformed", k_tt[i], ch);
          t_note("%ls '%c' bezier: %lu bytes, %d contours, %d lines, %d cubic", k_tt[i], ch, r, nc, nl, n3);
          free(b2); }
      }
      free(buf);
    }
    /* blank glyph has no outline: size 0 (not an error) */
    r = U(GetGlyphOutlineW)(d.dc, ' ', GGO_NATIVE, &g1, 0, NULL, &oa_ident);
    t_note("%ls space GGO_NATIVE returns %lx cellincX %d", k_tt[i], r, g1.gmCellIncX);
    EXPECT_EQ(r, 0);
    oa_close(&d);
  }
}

static DWORD pitch_for(int fmt, UINT bbx){
  if(fmt == GGO_BITMAP) return ((bbx + 31) / 32) * 4;
  return (bbx + 3) & ~3u;
}

TEST(go_bitmap_formats){
  static const struct { int fmt; int maxv; const char *n; } F[] = {
    { GGO_BITMAP, 1, "GGO_BITMAP" }, { GGO_GRAY2_BITMAP, 4, "GGO_GRAY2" },
    { GGO_GRAY4_BITMAP, 16, "GGO_GRAY4" }, { GGO_GRAY8_BITMAP, 64, "GGO_GRAY8" } };
  int i, k;
  for(i = 0; i < 2; i++){
    oa_font d;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    for(k = 0; k < 4; k++){
      GLYPHMETRICS g, gm; DWORD size, r, rows, pitch; BYTE *buf; UINT ch = 'A'; DWORD x, y; int maxseen = 0, bad = 0, nz = 0;
      size = U(GetGlyphOutlineW)(d.dc, ch, F[k].fmt, &g, 0, NULL, &oa_ident);
      CHECKF(size != GDI_ERROR && size > 0, "%ls %s size query %lu", k_tt[i], F[k].n, size);
      if(size == GDI_ERROR || !size) continue;
      rows = g.gmBlackBoxY; pitch = pitch_for(F[k].fmt, g.gmBlackBoxX);
      CHECKF(size == rows * pitch, "%ls %s size %lu != rows %lu * pitch %lu (bb %ux%u)", k_tt[i], F[k].n, size, rows, pitch, g.gmBlackBoxX, g.gmBlackBoxY);
      buf = (BYTE *)calloc(1, size + 8); if(!buf){ t_skip("oom"); break; }
      memset(buf, 0xee, size);
      r = U(GetGlyphOutlineW)(d.dc, ch, F[k].fmt, &gm, size, buf, &oa_ident);
      CHECKF(r == size, "%ls %s fill returned %lu (size %lu)", k_tt[i], F[k].n, r, size);
      CHECKF(!memcmp(&g, &gm, sizeof g), "%ls %s GLYPHMETRICS differ between query and fill", k_tt[i], F[k].n);
      CHECKF(g.gmCellIncX == adv32(d.dc, ch), "%ls %s gmCellIncX %d vs width %d", k_tt[i], F[k].n, g.gmCellIncX, adv32(d.dc, ch));
      if(F[k].fmt == GGO_BITMAP){
        for(y = 0; y < rows; y++) for(x = 0; x < pitch; x++){ if(buf[y * pitch + x]) nz++; }
        /* in the padding bits of each row (past gmBlackBoxX) nothing is set */
        for(y = 0; y < rows; y++) for(x = g.gmBlackBoxX; x < pitch * 8; x++) if(buf[y * pitch + x / 8] & (0x80 >> (x & 7))) bad++;
        CHECKF(nz > 0, "%ls GGO_BITMAP 'A' is blank", k_tt[i]);
        CHECKF(bad == 0, "%ls GGO_BITMAP 'A' has %d bits set beyond gmBlackBoxX", k_tt[i], bad);
      } else {
        for(y = 0; y < rows; y++) for(x = 0; x < g.gmBlackBoxX; x++){
          int v = buf[y * pitch + x]; if(v > maxseen) maxseen = v; if(v > F[k].maxv) bad++; if(v) nz++; }
        CHECKF(bad == 0, "%ls %s: %d values above %d", k_tt[i], F[k].n, bad, F[k].maxv);
        CHECKF(nz > 0, "%ls %s 'A' is blank", k_tt[i], F[k].n);
        CHECKF(maxseen > 0 && maxseen <= F[k].maxv, "%ls %s max value %d", k_tt[i], F[k].n, maxseen);
        t_note("%ls %s 'A': bb %ux%u size %lu max value %d nonzero px %d", k_tt[i], F[k].n, g.gmBlackBoxX, g.gmBlackBoxY, size, maxseen, nz);
      }
      free(buf);
    }
    /* the OR of GGO_BITMAP and GGO_GRAY8_BITMAP (1|6 = 7) is not a documented format */
    { GLYPHMETRICS g; DWORD r = U(GetGlyphOutlineW)(d.dc, 'A', GGO_BITMAP | GGO_GRAY8_BITMAP, &g, 0, NULL, &oa_ident);
      t_note("%ls GGO_BITMAP|GGO_GRAY8_BITMAP (=7) returns 0x%lx", k_tt[i], r); }
    oa_close(&d);
  }
}

TEST(go_glyph_index_and_errors){
  oa_font d; GLYPHMETRICS gc, gi, g; DWORD r, rc, ri; WORD idx = 0; WCHAR a = 'A';
  if(!oa_open(&d, L"Arial", -20, 1)){ t_skip("Arial not available as TrueType"); return; }
  CHECK(U(GetGlyphIndicesW)(d.dc, &a, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) == 1 && idx != 0xFFFF);
  rc = U(GetGlyphOutlineW)(d.dc, 'A', GGO_NATIVE, &gc, 0, NULL, &oa_ident);
  ri = U(GetGlyphOutlineW)(d.dc, idx, GGO_NATIVE | GGO_GLYPH_INDEX, &gi, 0, NULL, &oa_ident);
  CHECKF(rc == ri && rc != GDI_ERROR, "native size by char %lu by index %lu", rc, ri);
  CHECK(!memcmp(&gc, &gi, sizeof gc));
  rc = U(GetGlyphOutlineW)(d.dc, 'A', GGO_METRICS, &gc, 0, NULL, &oa_ident);
  ri = U(GetGlyphOutlineW)(d.dc, idx, GGO_METRICS | GGO_GLYPH_INDEX, &gi, 0, NULL, &oa_ident);
  CHECK(rc != GDI_ERROR && ri != GDI_ERROR && !memcmp(&gc, &gi, sizeof gc));
  /* MAT2 is required (documented) */
  SetLastError(0); r = U(GetGlyphOutlineW)(d.dc, 'A', GGO_METRICS, &g, 0, NULL, NULL);
  t_note("NULL MAT2 returns 0x%lx, last error %lu", r, GetLastError());
  EXPECT_EQ(r, GDI_ERROR);
  /* invalid DC */
  r = U(GetGlyphOutlineW)(NULL, 'A', GGO_METRICS, &g, 0, NULL, &oa_ident); EXPECT_EQ(r, GDI_ERROR);
  /* doubling matrix doubles the advance */
  { MAT2 m2 = { {0,2}, {0,0}, {0,0}, {0,2} }; GLYPHMETRICS g2;
    rc = U(GetGlyphOutlineW)(d.dc, 'A', GGO_METRICS, &g2, 0, NULL, &m2);
    CHECK(rc != GDI_ERROR);
    t_note("2x matrix: cellInc %d,%d (1x: %d,%d) bb %ux%u (1x %ux%u)", g2.gmCellIncX, g2.gmCellIncY, gc.gmCellIncX, gc.gmCellIncY, g2.gmBlackBoxX, g2.gmBlackBoxY, gc.gmBlackBoxX, gc.gmBlackBoxY);
    /* observed: the 2x advance is the unhinted scaled value, within one unit of twice the hinted 1x advance */
    CHECKF(g2.gmCellIncX >= 2 * gc.gmCellIncX - 1 && g2.gmCellIncX <= 2 * gc.gmCellIncX + 1, "2x matrix cellIncX %d vs 1x %d", g2.gmCellIncX, gc.gmCellIncX);
    CHECK(g2.gmBlackBoxX >= 2 * gc.gmBlackBoxX - 2 && g2.gmBlackBoxX <= 2 * gc.gmBlackBoxX + 2); }
  oa_close(&d);
}

TEST(go_symbol_missing_glyph){
  oa_font d; GLYPHMETRICS g; DWORD r; WORD idx = 0; WCHAR miss = 0x0100; UINT nglyphs; BYTE maxp[6];
  if(!oa_open(&d, L"Symbol", -20, 1)){ t_skip("Symbol not available as TrueType"); return; }
  r = U(GetFontData)(d.dc, 0x7078616d /* 'maxp' */, 4, maxp, 2);
  nglyphs = (r == 2) ? ((UINT)maxp[0] << 8 | maxp[1]) : 0;
  t_note("Symbol: face '%ls' numGlyphs %u charset %d", d.face, nglyphs, d.tm.tmCharSet);
  U(GetGlyphIndicesW)(d.dc, &miss, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS);
  t_note("Symbol: GetGlyphIndicesW(U+0100, MARK) -> 0x%04x", idx);
  /* a glyph index past numGlyphs is an error */
  if(nglyphs){
    r = U(GetGlyphOutlineW)(d.dc, nglyphs + 100, GGO_METRICS | GGO_GLYPH_INDEX, &g, 0, NULL, &oa_ident);
    t_note("GGO_GLYPH_INDEX %u (numGlyphs %u) returns 0x%lx", nglyphs + 100, nglyphs, r);
    EXPECT_EQ(r, GDI_ERROR);
    r = U(GetGlyphOutlineW)(d.dc, nglyphs - 1, GGO_METRICS | GGO_GLYPH_INDEX, &g, 0, NULL, &oa_ident);
    t_note("GGO_GLYPH_INDEX %u (last) returns 0x%lx", nglyphs - 1, r);
    EXPECT_NE(r, GDI_ERROR);
  }
  r = U(GetGlyphOutlineW)(d.dc, 0xFFFF, GGO_METRICS | GGO_GLYPH_INDEX, &g, 0, NULL, &oa_ident);
  t_note("GGO_GLYPH_INDEX 0xFFFF returns 0x%lx", r);
  EXPECT_EQ(r, GDI_ERROR);
  /* a character with no glyph in the font */
  r = U(GetGlyphOutlineW)(d.dc, 0x0100, GGO_METRICS, &g, 0, NULL, &oa_ident);
  t_note("char U+0100 GGO_METRICS returns 0x%lx cellinc %d bb %ux%u", r, g.gmCellIncX, g.gmBlackBoxX, g.gmBlackBoxY);
  r = U(GetGlyphOutlineA)(d.dc, 0x41, GGO_METRICS, &g, 0, NULL, &oa_ident);
  t_note("Symbol 'A' GGO_METRICS returns 0x%lx cellinc %d bb %ux%u", r, g.gmCellIncX, g.gmBlackBoxX, g.gmBlackBoxY);
  oa_close(&d);
}

TEST(go_bitmap_font_rejected){
  static const WCHAR *k_bmp[] = { L"Terminal", L"MS Sans Serif", L"Small Fonts" };
  int i, tried = 0;
  for(i = 0; i < 3; i++){
    oa_font d; GLYPHMETRICS g; DWORD r;
    if(!oa_open(&d, k_bmp[i], -13, 0)){ oa_close(&d); continue; }
    tried++;
    r = U(GetGlyphOutlineW)(d.dc, 'A', GGO_METRICS, &g, 0, NULL, &oa_ident);
    t_note("%ls GGO_METRICS returns 0x%lx", k_bmp[i], r);
    EXPECT_EQ(r, GDI_ERROR);
    r = U(GetGlyphOutlineW)(d.dc, 'A', GGO_BITMAP, &g, 0, NULL, &oa_ident);
    t_note("%ls GGO_BITMAP returns 0x%lx", k_bmp[i], r);
    EXPECT_EQ(r, GDI_ERROR);
    r = U(GetGlyphOutlineW)(d.dc, 'A', GGO_NATIVE, &g, 0, NULL, &oa_ident);
    EXPECT_EQ(r, GDI_ERROR);
    oa_close(&d);
  }
  if(!tried) t_skip("no raster face realised as non-TrueType");
}

TEST(go_wow_and_ansi){
  int i;
  REQUIRE(UN(GetGlyphOutlineWow));
  for(i = 0; i < 2; i++){
    oa_font d; GLYPHMETRICS ga, gw; DWORD ra, rw, sa, sw; UINT ch;
    if(!oa_open(&d, k_tt[i], -20, 1)){ t_skip("%ls not available as TrueType", k_tt[i]); continue; }
    for(ch = 'A'; ch <= 'Z'; ch += 5){
      memset(&ga, 0, sizeof ga); memset(&gw, 0, sizeof gw);
      ra = U(GetGlyphOutlineA)(d.dc, ch, GGO_METRICS, &ga, 0, NULL, &oa_ident);
      rw = UN(GetGlyphOutlineWow)(d.dc, ch, GGO_METRICS, &gw, 0, NULL, &oa_ident);
      if(ch == 'A') t_note("%ls Wow 'A' GGO_METRICS returns 0x%lx (GetGlyphOutlineA 0x%lx)", k_tt[i], rw, ra);
      CHECKF(ra == rw && !memcmp(&ga, &gw, sizeof ga), "%ls Wow vs A differ for '%c'", k_tt[i], ch);
      sa = U(GetGlyphOutlineA)(d.dc, ch, GGO_NATIVE, &ga, 0, NULL, &oa_ident);
      sw = UN(GetGlyphOutlineWow)(d.dc, ch, GGO_NATIVE, &gw, 0, NULL, &oa_ident);
      CHECKF(sa == sw, "%ls Wow native size %lu vs A %lu for '%c'", k_tt[i], sw, sa, ch);
    }
    /* ANSI 0x80 is the euro sign in ACP 1252; the W call with the converted character agrees */
    if(GetACP() == 1252){
      GLYPHMETRICS g1, g2; DWORD r1, r2;
      r1 = U(GetGlyphOutlineA)(d.dc, 0x80, GGO_METRICS, &g1, 0, NULL, &oa_ident);
      r2 = U(GetGlyphOutlineW)(d.dc, 0x20AC, GGO_METRICS, &g2, 0, NULL, &oa_ident);
      CHECKF(r1 == r2 && !memcmp(&g1, &g2, sizeof g1), "%ls A(0x80) vs W(U+20AC) differ", k_tt[i]);
      r1 = UN(GetGlyphOutlineWow)(d.dc, 0x80, GGO_METRICS, &g1, 0, NULL, &oa_ident);
      CHECKF(r1 == r2 && !memcmp(&g1, &g2, sizeof g1), "%ls Wow(0x80) vs W(U+20AC) differ", k_tt[i]);
    } else t_note("ACP %u: euro arm skipped", GetACP());
    oa_close(&d);
  }
}
