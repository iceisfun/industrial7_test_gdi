#include "t.h"
/* font_metrics: invariants of CreateFontIndirect / SelectObject / GetObject / GetTextFace / GetTextMetrics /
 * GetOutlineTextMetrics that hold on ANY Windows (no recorded Win10 numbers here; the golden generator adds those
 * from data/win10/font_metrics). Every hard CHECK cites the Win10 22H2 code it was verified in (corpus API
 * decomp, docs/research/02_win10_font_mapper.md); anything only observed is t_note.
 *
 * Verified code facts used below:
 *  [V1] gdi32full!CreateFontIndirectW @0x6140: lplf == NULL -> returns NULL.
 *  [V2] gdi32full!GetObjectW @0x19f60: font handle (type 0x0a) with pv == NULL -> 0x5c (sizeof LOGFONTW);
 *       handle types 0x01 (DC), 0x04, 0x21 -> SetLastError(6) and 0.
 *  [V3] win32kfull!GreExtGetObjectW @0x99f88 + NtGdiExtGetObjectW @0x99e80: font -> memcpy(min(c, stored size))
 *       of the STORED LOGFONT (no substitution, no realised height), returns that count.
 *  [V4] gdi32full!GetTextFaceW @0x66e0: lpName != NULL && c < 1 -> SetLastError(87), 0.
 *       win32kfull!GreGetTextFaceW @0x5af50 / NtGdiGetTextFaceW @0x5ae40: NULL buffer -> length incl. NUL;
 *       buffer -> copies min(c, len) chars, last one forced to NUL, returns min(c, len).
 *  [V5] win32kfull!vIFIMetricsToTextMetricWStrict (research 02 6.2): tmDescent = tmHeight - tmAscent;
 *       tmUnderlined/tmStruckOut come from the DC's LFONT; tmOverhang = 0 for TrueType;
 *       TrueType tmDigitizedAspectX = LOGPIXELSY.
 *  [V6] gdi32full!GetTextMetricsAImpl @0x4ce0 + vTextMetricWToTextMetricStrict @0x4e30: TEXTMETRICA = the 11 LONG
 *       fields and the 5 trailing bytes of the W struct (chars from the TMW_INTERNAL bytes).
 *  [V7] gdi32full!GetOutlineTextMetricsW @0x220e0 passes cjCopy = potm ? cjCopy : 0;
 *       win32kfull!NtGdiGetOutlineTextMetricsInternalW @0x61f90: cjCopy == 0 or potm == NULL -> size query;
 *       GreGetOutlineTextMetricsInternalW @0x620d4: bad DC -> SetLastError(6), 0; NULL buffer -> total size;
 *       cjCopy < 0xe9 -> copies cjCopy bytes of the fixed part, returns cjCopy; 0xe9 <= cjCopy < size -> 0.
 *  [V8] win32kfull!GreGetCharABCWidthsW (research 02 6.5): A+B+C == round(fxD) == GetCharWidth32 - overhang.
 *       GetTextExtentPoint32W cy = the cached tmHeight. */

static HDC screen_dc(void) { return U(GetDC)(NULL); }
static void release_dc(HDC dc) { U(ReleaseDC)(NULL, dc); }

static HFONT mkfont(const WCHAR *face, int h, int wt, int it) {
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = h; lf.lfWeight = wt; lf.lfItalic = (BYTE)it; lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return U(CreateFontIndirectW)(&lf);
}

static const WCHAR *FACES[] = { L"Arial", L"Tahoma", L"Segoe UI", L"Courier New", L"Times New Roman",
                                L"MS Shell Dlg", L"MS Shell Dlg 2", L"Microsoft Sans Serif", L"System", L"Terminal",
                                L"Courier", L"MS Sans Serif", L"" };
static const int HEIGHTS[] = { -6, -8, -11, -12, -13, -16, -24, -40, 0, 8, 13, 16, 20, 40 };
#define NFACES (int)(sizeof FACES / sizeof *FACES)
#define NHEIGHTS (int)(sizeof HEIGHTS / sizeof *HEIGHTS)

TEST(createfont_null_fails) {
    REQUIRE(U(CreateFontIndirectW));
    CHECK(U(CreateFontIndirectW)(NULL) == NULL);              /* [V1] */
}

TEST(tm_height_is_ascent_plus_descent) {
    HDC dc = screen_dc(); REQUIRE(dc);
    for (int fi = 0; fi < NFACES; fi++) for (int hi = 0; hi < NHEIGHTS; hi++) for (int wt = 400; wt <= 700; wt += 300) {
        HFONT f = mkfont(FACES[fi], HEIGHTS[hi], wt, 0); CHECK(f != NULL); if (!f) continue;
        HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
        BOOL ok = U(GetTextMetricsW)(dc, &tm);
        CHECKF(ok, "face %ls h %d", FACES[fi], HEIGHTS[hi]);
        if (ok) CHECKF(tm.tmHeight == tm.tmAscent + tm.tmDescent, "face %ls h %d wt %d: %ld != %ld + %ld",   /* [V5] */
                       FACES[fi], HEIGHTS[hi], wt, tm.tmHeight, tm.tmAscent, tm.tmDescent);
        if (ok && HEIGHTS[hi] < 0 && (tm.tmPitchAndFamily & TMPF_TRUETYPE) && tm.tmHeight - tm.tmInternalLeading != -HEIGHTS[hi])
            t_note("TT %ls lfHeight %d: tmHeight-tmInternalLeading = %ld (driver-supplied internal leading)",
                   FACES[fi], HEIGHTS[hi], tm.tmHeight - tm.tmInternalLeading);
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(tm_truetype_overhang_and_aspect) {
    HDC dc = screen_dc(); REQUIRE(dc);
    int lpy = U(GetDeviceCaps)(dc, LOGPIXELSY);
    static const WCHAR *tt[] = { L"Arial", L"Tahoma", L"Courier New", L"Times New Roman" };
    for (int i = 0; i < 4; i++) for (int it = 0; it <= 1; it++) {
        HFONT f = mkfont(tt[i], -12, 400, it); HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
        if (U(GetTextMetricsW)(dc, &tm) && (tm.tmPitchAndFamily & TMPF_TRUETYPE)) {
            CHECKF(tm.tmOverhang == 0, "%ls it %d overhang %ld", tt[i], it, tm.tmOverhang);          /* [V5] */
            CHECKF(tm.tmDigitizedAspectX == lpy, "%ls aspectX %ld logpixelsy %d", tt[i], tm.tmDigitizedAspectX, lpy); /* [V5] */
        } else t_note("%ls not realised as TrueType", tt[i]);
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(tm_underline_strikeout_from_logfont) {
    HDC dc = screen_dc(); REQUIRE(dc);
    for (int fi = 0; fi < NFACES; fi++) for (int us = 0; us < 4; us++) {
        LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -12; lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfUnderline = (BYTE)(us & 1); lf.lfStrikeOut = (BYTE)((us >> 1) & 1);
        lstrcpynW(lf.lfFaceName, FACES[fi], LF_FACESIZE);
        HFONT f = U(CreateFontIndirectW)(&lf); HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
        CHECK(U(GetTextMetricsW)(dc, &tm));
        CHECKF(!!tm.tmUnderlined == (us & 1), "%ls ul %d got %d", FACES[fi], us & 1, tm.tmUnderlined);   /* [V5] */
        CHECKF(!!tm.tmStruckOut == ((us >> 1) & 1), "%ls so %d got %d", FACES[fi], (us >> 1) & 1, tm.tmStruckOut);
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(tm_a_w_parity) {
    HDC dc = screen_dc(); REQUIRE(dc);
    for (int fi = 0; fi < NFACES; fi++) for (int hi = 0; hi < NHEIGHTS; hi += 3) {
        HFONT f = mkfont(FACES[fi], HEIGHTS[hi], 400, hi & 1); HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW w; TEXTMETRICA a; memset(&w, 0, sizeof w); memset(&a, 0xcc, sizeof a);
        BOOL okw = U(GetTextMetricsW)(dc, &w), oka = U(GetTextMetricsA)(dc, &a);
        CHECK(okw && oka);
        if (okw && oka) {                                                                                     /* [V6] */
            int eq = a.tmHeight == w.tmHeight && a.tmAscent == w.tmAscent && a.tmDescent == w.tmDescent &&
                a.tmInternalLeading == w.tmInternalLeading && a.tmExternalLeading == w.tmExternalLeading &&
                a.tmAveCharWidth == w.tmAveCharWidth && a.tmMaxCharWidth == w.tmMaxCharWidth && a.tmWeight == w.tmWeight &&
                a.tmOverhang == w.tmOverhang && a.tmDigitizedAspectX == w.tmDigitizedAspectX &&
                a.tmDigitizedAspectY == w.tmDigitizedAspectY && a.tmItalic == w.tmItalic && a.tmUnderlined == w.tmUnderlined &&
                a.tmStruckOut == w.tmStruckOut && a.tmPitchAndFamily == w.tmPitchAndFamily && a.tmCharSet == w.tmCharSet;
            CHECKF(eq, "face %ls h %d: TEXTMETRICA numeric fields differ from W", FACES[fi], HEIGHTS[hi]);
        }
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(getobject_logfont_roundtrip) {
    for (int fi = 0; fi < NFACES; fi++) for (int hi = 0; hi < NHEIGHTS; hi++) {
        LOGFONTW lf; memset(&lf, 0, sizeof lf);
        lf.lfHeight = HEIGHTS[hi]; lf.lfWidth = hi; lf.lfWeight = 100 * (hi % 9); lf.lfItalic = (BYTE)(hi & 1);
        lf.lfCharSet = (BYTE)(hi & 2 ? ANSI_CHARSET : DEFAULT_CHARSET); lf.lfQuality = (BYTE)(hi % 7);
        lf.lfPitchAndFamily = (BYTE)(FF_SWISS | VARIABLE_PITCH);
        lstrcpynW(lf.lfFaceName, FACES[fi], LF_FACESIZE);
        HFONT f = U(CreateFontIndirectW)(&lf); CHECK(f != NULL); if (!f) continue;
        { int q = U(GetObjectW)(f, 0, NULL); EXPECT_EQ(q, sizeof(LOGFONTW)); }         /* [V2] */
        LOGFONTW back; memset(&back, 0xcc, sizeof back);
        { int q = U(GetObjectW)(f, sizeof back, &back); EXPECT_EQ(q, sizeof(LOGFONTW)); } /* [V3] */
        CHECKF(!memcmp(&back, &lf, sizeof lf), "face %ls h %d: stored LOGFONT differs from the request", FACES[fi], HEIGHTS[hi]);
        /* partial copy: exactly c bytes, the rest untouched [V3] */
        BYTE part[sizeof(LOGFONTW)]; memset(part, 0xab, sizeof part);
        { int q = U(GetObjectW)(f, 10, part); EXPECT_EQ(q, 10); }
        CHECK(!memcmp(part, &lf, 10));
        CHECK(part[10] == 0xab && part[sizeof part - 1] == 0xab);
        U(DeleteObject)(f);
    }
}

TEST(getobject_logfonta_observed) {
    HFONT f = mkfont(L"Tahoma", -11, 700, 1); REQUIRE(f);
    LOGFONTA la; memset(&la, 0, sizeof la);
    int r = U(GetObjectA)(f, sizeof la, &la);
    t_note("GetObjectA(font, sizeof LOGFONTA=%u) = %d face \"%s\" h %ld wt %ld it %d", (unsigned)sizeof la, r, la.lfFaceName, la.lfHeight, la.lfWeight, la.lfItalic);
    U(DeleteObject)(f);
}

TEST(getobject_dc_handle_fails) {
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    LOGFONTW lf;
    SET_ERR(0);
    int r = U(GetObjectW)(dc, sizeof lf, &lf);
    EXPECT_FAIL(r == 0, ERROR_INVALID_HANDLE);                                          /* [V2] */
    U(DeleteDC)(dc);
}

TEST(selectobject_returns_previous_font) {
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    HFONT a = mkfont(L"Arial", -12, 400, 0), b = mkfont(L"Tahoma", -11, 400, 0);
    HGDIOBJ cur = U(GetCurrentObject)(dc, OBJ_FONT);
    CHECK(cur != NULL);
    if (cur != U(GetStockObject)(SYSTEM_FONT)) t_note("fresh memory DC font %p is not SYSTEM_FONT", cur);
    /* (results in locals: the EXPECT_ macros evaluate their arguments twice on failure) */
    HGDIOBJ p0 = U(SelectObject)(dc, a);
    EXPECT_EQ(p0, cur);
    HGDIOBJ c1 = U(GetCurrentObject)(dc, OBJ_FONT); EXPECT_EQ(c1, a);
    HGDIOBJ r1 = U(SelectObject)(dc, b); EXPECT_EQ(r1, a);
    HGDIOBJ r2 = U(SelectObject)(dc, b); EXPECT_EQ(r2, b);
    HGDIOBJ r3 = U(SelectObject)(dc, p0); EXPECT_EQ(r3, b);
    HGDIOBJ c2 = U(GetCurrentObject)(dc, OBJ_FONT); EXPECT_EQ(c2, p0);
    U(DeleteDC)(dc); U(DeleteObject)(a); U(DeleteObject)(b);
}

TEST(textface_buffer_protocol) {
    HDC dc = screen_dc(); REQUIRE(dc);
    for (int fi = 0; fi < NFACES; fi++) {
        HFONT f = mkfont(FACES[fi], -12, 400, 0); HGDIOBJ o = U(SelectObject)(dc, f);
        int need = U(GetTextFaceW)(dc, 0, NULL);
        WCHAR full[LF_FACESIZE * 2]; memset(full, 0, sizeof full);
        int got = U(GetTextFaceW)(dc, LF_FACESIZE * 2, full);
        CHECKF(need > 0 && got == need, "%ls need %d got %d", FACES[fi], need, got);              /* [V4] */
        CHECKF(got == lstrlenW(full) + 1, "%ls got %d len %d", FACES[fi], got, lstrlenW(full));
        if (need >= 3) {
            WCHAR tr[8]; memset(tr, 0xcc, sizeof tr);
            { int q = U(GetTextFaceW)(dc, 3, tr); EXPECT_EQ(q, 3); }                                             /* [V4] truncation */
            CHECK(tr[0] == full[0] && tr[1] == full[1] && tr[2] == 0);
        }
        WCHAR one[2] = { 0x1234, 0x1234 };
        { int q = U(GetTextFaceW)(dc, 1, one); EXPECT_EQ(q, 1); }                                                /* only the NUL */
        CHECK(one[0] == 0 && one[1] == 0x1234);
        SET_ERR(0);
        { int q = U(GetTextFaceW)(dc, 0, full); EXPECT_FAIL(q == 0, ERROR_INVALID_PARAMETER); }                 /* [V4] */
        SET_ERR(0);
        { int q = U(GetTextFaceW)(dc, -1, full); EXPECT_FAIL(q == 0, ERROR_INVALID_PARAMETER); }
        /* A/W parity for the ASCII face names */
        char fa[LF_FACESIZE * 2] = "";
        int ga = U(GetTextFaceA)(dc, sizeof fa, fa);
        char fw[LF_FACESIZE * 2] = ""; WideCharToMultiByte(CP_ACP, 0, full, -1, fw, sizeof fw, NULL, NULL);
        if (ga <= 0 || strcmp(fa, fw)) t_note("GetTextFaceA %d \"%s\" vs W \"%s\"", ga, fa, fw);
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(otm_buffer_protocol) {
    HDC dc = screen_dc(); REQUIRE(dc);
    static const WCHAR *tt[] = { L"Arial", L"Tahoma", L"Segoe UI", L"Courier New", L"Times New Roman" };
    for (int i = 0; i < 5; i++) for (int hi = 0; hi < NHEIGHTS; hi += 2) {
        HFONT f = mkfont(tt[i], HEIGHTS[hi], 400, 0); HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm); U(GetTextMetricsW)(dc, &tm);
        if (!(tm.tmPitchAndFamily & TMPF_TRUETYPE)) { t_note("%ls not TrueType here", tt[i]); goto next; }
        UINT need = U(GetOutlineTextMetricsW)(dc, 0, NULL);                                       /* [V7] size query */
        CHECKF(need >= sizeof(OUTLINETEXTMETRICW), "%ls need %u", tt[i], need);
        if (need < sizeof(OUTLINETEXTMETRICW)) goto next;
        BYTE *buf = (BYTE *)calloc(1, need + 64);
        { UINT q = U(GetOutlineTextMetricsW)(dc, 0, (OUTLINETEXTMETRICW *)buf); EXPECT_EQ(q, need); }            /* [V7] cjCopy 0 = query */
        /* small buffer (< 0xe9): partial copy of the fixed part */
        memset(buf, 0xee, need + 64);
        { UINT q = U(GetOutlineTextMetricsW)(dc, 100, (OUTLINETEXTMETRICW *)buf); EXPECT_EQ(q, 100); }           /* [V7] */
        CHECK(buf[100] == 0xee);
        CHECK(((OUTLINETEXTMETRICW *)buf)->otmTextMetrics.tmHeight == tm.tmHeight);
        /* between 0xe9 and need: fails */
        if (need - 1 >= 0xe9) { UINT q = U(GetOutlineTextMetricsW)(dc, need - 1, (OUTLINETEXTMETRICW *)buf); EXPECT_EQ(q, 0); }   /* [V7] */
        /* full */
        memset(buf, 0xee, need + 64);
        UINT ret = U(GetOutlineTextMetricsW)(dc, need, (OUTLINETEXTMETRICW *)buf);
        OUTLINETEXTMETRICW *otm = (OUTLINETEXTMETRICW *)buf;
        CHECKF(ret > 0 && ret <= need, "%ls ret %u need %u", tt[i], ret, need);
        if (ret != need) t_note("%ls h %d: OTM full call returned %u, size query %u", tt[i], HEIGHTS[hi], ret, need);
        if (ret) {
            CHECKF(otm->otmSize == ret, "%ls otmSize %u ret %u", tt[i], otm->otmSize, ret);
            const TEXTMETRICW *t = &otm->otmTextMetrics;
            CHECKF(t->tmHeight == tm.tmHeight && t->tmAscent == tm.tmAscent && t->tmDescent == tm.tmDescent &&
                   t->tmInternalLeading == tm.tmInternalLeading && t->tmExternalLeading == tm.tmExternalLeading &&
                   t->tmAveCharWidth == tm.tmAveCharWidth && t->tmMaxCharWidth == tm.tmMaxCharWidth &&
                   t->tmWeight == tm.tmWeight && t->tmPitchAndFamily == tm.tmPitchAndFamily && t->tmCharSet == tm.tmCharSet,
                   "%ls h %d: otmTextMetrics differs from GetTextMetricsW", tt[i], HEIGHTS[hi]);
            /* name offsets point inside the returned block */
            size_t offs[4] = { (size_t)otm->otmpFamilyName, (size_t)otm->otmpFaceName, (size_t)otm->otmpStyleName, (size_t)otm->otmpFullName };
            for (int k = 0; k < 4; k++) CHECKF(offs[k] >= sizeof(OUTLINETEXTMETRICW) && offs[k] < ret, "%ls name offset %d = %zu", tt[i], k, offs[k]);
            if (otm->otmptSuperscriptSize.x != otm->otmptSubscriptSize.x || otm->otmptSuperscriptSize.y != otm->otmptSubscriptSize.y)
                t_note("%ls h %d: superscript size %ld,%ld != subscript %ld,%ld", tt[i], HEIGHTS[hi],
                       otm->otmptSuperscriptSize.x, otm->otmptSuperscriptSize.y, otm->otmptSubscriptSize.x, otm->otmptSubscriptSize.y);
        }
        free(buf);
    next:
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(otm_bad_dc) {
    REQUIRE(U(GetOutlineTextMetricsW));
    SET_ERR(0);
    UINT r = U(GetOutlineTextMetricsW)(NULL, 0, NULL);
    EXPECT_FAIL(r == 0, ERROR_INVALID_HANDLE);                                                   /* [V7] */
}

TEST(otm_raster_font_observed) {
    HDC dc = screen_dc(); REQUIRE(dc);
    static const WCHAR *ras[] = { L"System", L"Terminal", L"MS Sans Serif", L"Courier", L"Fixedsys" };
    for (int i = 0; i < 5; i++) {
        HFONT f = mkfont(ras[i], 0, 400, 0); HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm); U(GetTextMetricsW)(dc, &tm);
        t_note("%ls: pf 0x%02x OTM size %u", ras[i], tm.tmPitchAndFamily, U(GetOutlineTextMetricsW)(dc, 0, NULL));
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(gettextmetrics_bad_dc) {
    TEXTMETRICW tm;
    CHECK(!U(GetTextMetricsW)(NULL, &tm));            /* gdi32full!GetTextMetricsW: handle not a DC of this process -> 0 */
}

TEST(abc_sum_equals_width_truetype) {
    HDC dc = screen_dc(); REQUIRE(dc);
    static const WCHAR *tt[] = { L"Arial", L"Tahoma", L"Segoe UI", L"Courier New", L"Times New Roman" };
    static const int hs[] = { -8, -11, -12, -13, -16, -24, 13, 16 };
    for (int i = 0; i < 5; i++) for (int hi = 0; hi < 8; hi++) for (int wt = 400; wt <= 700; wt += 300) {
        HFONT f = mkfont(tt[i], hs[hi], wt, 0); HGDIOBJ o = U(SelectObject)(dc, f);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm); U(GetTextMetricsW)(dc, &tm);
        if (tm.tmPitchAndFamily & TMPF_TRUETYPE) {
            ABC abc[95]; INT w[95];
            BOOL ka = U(GetCharABCWidthsW)(dc, 0x20, 0x7e, abc), kw = U(GetCharWidth32W)(dc, 0x20, 0x7e, w);
            CHECK(ka && kw);
            int bad = 0;
            for (int c = 0; ka && kw && c < 95; c++)
                if (abc[c].abcA + (int)abc[c].abcB + abc[c].abcC != w[c] - tm.tmOverhang) bad++;   /* [V8] */
            CHECKF(bad == 0, "%ls h %d wt %d: %d chars with A+B+C != width", tt[i], hs[hi], wt, bad);
            /* extent: cy == tmHeight [V8]; cx vs summed widths only observed (advances summed in 28.4, rounded once) */
            static const WCHAR s[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            SIZE sz = { 0, 0 };
            CHECK(U(GetTextExtentPoint32W)(dc, s, 52, &sz));
            CHECKF(sz.cy == tm.tmHeight, "%ls h %d: extent cy %ld tmHeight %ld", tt[i], hs[hi], sz.cy, tm.tmHeight);
            long sum = 0; INT w2[52];
            for (int c = 0; c < 52; c++) { U(GetCharWidth32W)(dc, s[c], s[c], &w2[c]); sum += w2[c]; }
            if (sum != sz.cx) t_note("%ls h %d wt %d: extent cx %ld, sum of GetCharWidth32 %ld", tt[i], hs[hi], wt, sz.cx, sum);
            int fit = -1; INT dx[52];
            if (U(GetTextExtentExPointW)(dc, s, 52, 100000, &fit, dx, &sz)) {
                if (fit != 52 || dx[51] != sz.cx) t_note("%ls h %d: ExPoint fit %d last dx %d cx %ld", tt[i], hs[hi], fit, dx[51], sz.cx);
            }
        }
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}

TEST(stock_fonts_getobject_and_tm) {
    HDC dc = screen_dc(); REQUIRE(dc);
    static const int ids[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    for (int i = 0; i < 7; i++) {
        HFONT f = (HFONT)U(GetStockObject)(ids[i]); CHECKF(f != NULL, "stock %d", ids[i]); if (!f) continue;
        { int q = U(GetObjectW)(f, 0, NULL); EXPECT_EQ(q, sizeof(LOGFONTW)); }                  /* [V2] */
        LOGFONTW lf; memset(&lf, 0, sizeof lf);
        { int q = U(GetObjectW)(f, sizeof lf, &lf); EXPECT_EQ(q, sizeof(LOGFONTW)); }          /* [V3] */
        HGDIOBJ o = U(SelectObject)(dc, f); CHECK(o != NULL);
        TEXTMETRICW tm; memset(&tm, 0, sizeof tm);
        CHECK(U(GetTextMetricsW)(dc, &tm));
        CHECKF(tm.tmHeight == tm.tmAscent + tm.tmDescent, "stock %d", ids[i]);                  /* [V5] */
        WCHAR face[64] = L""; U(GetTextFaceW)(dc, 64, face);
        t_note("stock %d: LOGFONT h %ld face \"%ls\" -> GetTextFace \"%ls\" tm %ld/%ld/%ld il %ld ave %ld", ids[i], lf.lfHeight,
               lf.lfFaceName, face, tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmAveCharWidth);
        U(SelectObject)(dc, o);
    }
    release_dc(dc);
}

TEST(language_info_and_placement_observed) {
    HDC dc = screen_dc(); REQUIRE(dc);
    static const WCHAR s[] = L"Hello World";
    for (int fi = 0; fi < NFACES; fi++) {
        HFONT f = mkfont(FACES[fi], -12, 400, 0); HGDIOBJ o = U(SelectObject)(dc, f);
        DWORD fli = U(GetFontLanguageInfo)(dc);
        CHECK(fli != GCP_ERROR);
        GCP_RESULTSW r; memset(&r, 0, sizeof r); r.lStructSize = sizeof r;
        DWORD g = U(GetCharacterPlacementW)(dc, s, 11, 0, &r, 0);
        SIZE sz = { 0, 0 }; U(GetTextExtentPoint32W)(dc, s, 11, &sz);
        if (LOWORD(g) != sz.cx || HIWORD(g) != sz.cy)
            t_note("%ls: GetCharacterPlacementW %ux%u vs extent %ldx%ld (fli 0x%lx)", FACES[fi], LOWORD(g), HIWORD(g), sz.cx, sz.cy, fli);
        U(SelectObject)(dc, o); U(DeleteObject)(f);
    }
    release_dc(dc);
}
