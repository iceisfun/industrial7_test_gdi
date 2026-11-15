#include "oa_common.h"
/* GetStringBitmapA/W (undocumented). Signature from the kernel entry (decompiled):
 *   UINT NtGdiGetStringBitmapW(HDC, WCHAR *pwsz, UINT cwc (must be 1), UINT cjBuf, BYTE *pjBuf)
 * and gdi32full!GetStringBitmapA (converts one ANSI character, then calls the W form).
 * Output starts with an 8-byte header { int cx; int cy } followed by cy rows of ((cx+7)/8) bytes (1 bpp). */

static void dump_one(const WCHAR *tag, HDC dc, WCHAR ch){
  BYTE buf[2048]; DWORD r0, r; int cx, cy;
  memset(buf, 0x5a, sizeof buf);
  r0 = UN(GetStringBitmapW)(dc, &ch, 1, 0, NULL);
  r  = UN(GetStringBitmapW)(dc, &ch, 1, sizeof buf, buf);
  memcpy(&cx, buf, 4); memcpy(&cy, buf + 4, 4);
  t_note("%ls U+%04x: size-query returns %lu, fill returns %lu, header cx %d cy %d", tag, ch, r0, r, r ? cx : -1, r ? cy : -1);
}

TEST(stringbitmap_records){
  static const WCHAR *k_f[] = { L"Terminal", L"MS Sans Serif", L"Arial" };
  int i; REQUIRE(UN(GetStringBitmapW)); REQUIRE(UN(GetStringBitmapA));
  for(i = 0; i < 3; i++){
    oa_font d; BYTE bw[2048], ba[2048]; DWORD rw, ra; WCHAR ch = 'A'; char cha = 'A'; int cx, cy, pitch;
    if(!oa_open(&d, k_f[i], -13, -1)){ oa_close(&d); continue; }
    t_note("%ls realised pf 0x%x (TrueType=%d)", d.face, d.tm.tmPitchAndFamily, (d.tm.tmPitchAndFamily & TMPF_TRUETYPE) != 0);
    dump_one(k_f[i], d.dc, ch);
    dump_one(k_f[i], d.dc, ' ');
    { static const WCHAR cs[] = { 'A', 'i', 'W', ' ', '1' }; unsigned q;
      for(q = 0; q < sizeof cs / sizeof *cs; q++){
        BYTE b[2048]; DWORD r; INT wd; WCHAR c = cs[q];
        r = UN(GetStringBitmapW)(d.dc, &c, 1, sizeof b, b);
        U(GetCharWidth32W)(d.dc, c, c, &wd);
        if(r >= 8){ int cx2, cy2; memcpy(&cx2, b, 4); memcpy(&cy2, b + 4, 4);
          t_note("%ls U+%04x header cx %d cy %d, advance %d, tmHeight %ld", k_f[i], c, cx2, cy2, wd, d.tm.tmHeight);
          CHECKF(cy2 == d.tm.tmHeight, "%ls U+%04x bitmap height %d vs tmHeight %ld", k_f[i], c, cy2, d.tm.tmHeight);
          if(!(d.tm.tmPitchAndFamily & TMPF_TRUETYPE)) CHECKF(cx2 == wd, "%ls U+%04x bitmap width %d vs advance %d", k_f[i], c, cx2, wd); }
        else CHECKF(0, "%ls U+%04x returned %lu", k_f[i], c, r);
      } }
    memset(bw, 0x5a, sizeof bw); memset(ba, 0x5a, sizeof ba);
    rw = UN(GetStringBitmapW)(d.dc, &ch, 1, sizeof bw, bw);
    ra = UN(GetStringBitmapA)(d.dc, &cha, 1, sizeof ba, ba);
    CHECKF(ra == rw, "%ls A returns %lu, W returns %lu", k_f[i], ra, rw);
    if(ra == rw && rw) CHECKF(!memcmp(ba, bw, rw), "%ls A/W data differ", k_f[i]);
    if(rw){
      memcpy(&cx, bw, 4); memcpy(&cy, bw + 4, 4); pitch = (cx + 7) / 8;
      CHECKF(cx > 0 && cy > 0 && rw == (DWORD)(8 + pitch * cy), "%ls size %lu vs header cx %d cy %d (8 + rows*pitch = %d)", k_f[i], rw, cx, cy, 8 + pitch * cy);
      { int x, y, set = 0; for(y = 0; y < cy; y++) for(x = 0; x < pitch; x++) if(bw[8 + y * pitch + x]) set++;
        CHECK(set > 0); }
    }
    /* A count above two characters is rejected by the ANSI wrapper */
    ra = UN(GetStringBitmapA)(d.dc, "ABC", 3, sizeof ba, ba); t_note("%ls A with 3 chars returns %lu", k_f[i], ra);
    EXPECT_EQ(ra, 0);
    /* W with cwc != 1 is rejected by the kernel entry */
    { WCHAR two[2] = { 'A', 'B' }; rw = UN(GetStringBitmapW)(d.dc, two, 2, sizeof bw, bw); t_note("%ls W with cwc=2 returns %lu", k_f[i], rw); EXPECT_EQ(rw, 0); }
    /* too small a buffer */
    /* observed: a too-small buffer is not written and the call returns the required size */
    ch = 'A'; { DWORD need = UN(GetStringBitmapW)(d.dc, &ch, 1, 0, NULL); memset(bw, 0x5a, 64);
      rw = UN(GetStringBitmapW)(d.dc, &ch, 1, 4, bw); t_note("%ls W cjBuf=4 returns %lu (required %lu)", k_f[i], rw, need);
      EXPECT_EQ(rw, need); { int z, n = 0; for(z = 0; z < 64; z++) if(bw[z] != 0x5a) n++; CHECKF(n == 0, "%ls buffer written despite cjBuf=4", k_f[i]); } }
    rw = UN(GetStringBitmapW)(NULL, &ch, 1, sizeof bw, bw); t_note("%ls W NULL dc returns %lu", k_f[i], rw); EXPECT_EQ(rw, 0);
    oa_close(&d);
  }
}
