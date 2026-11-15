#ifndef OA_COMMON_H
#define OA_COMMON_H
#include "t.h"

/* Shared helpers for the outline_and_abc area. Every GDI call goes through U()/UN(). */
typedef struct { HDC dc; HFONT f, old; TEXTMETRICW tm; WCHAR face[LF_FACESIZE]; int ok; } oa_font;

/* Create face at pixel height h (negative = character height), select into a screen DC.
 * need_tt: 1 = require realised TrueType face of that name, 0 = require a NON-TrueType (raster) realisation
 * of that name, -1 = accept anything. Returns 1 on success, 0 (with a NOTE) otherwise; on failure everything it created is released. */
int  oa_open(oa_font *d, const WCHAR *face, int h, int need_tt);
void oa_close(oa_font *d);
/* mapping: MM_TEXT, identity world transform, GM_COMPATIBLE (explicitly set so the tests do not depend on defaults) */
void oa_reset_dc(HDC dc);
int  oa_wide_from_ansi(const char *a, WCHAR *w, int cap);

/* declarations of exports that are not in the SDK headers */
UDECL(int,   GetETM,              (HDC, void *));
UDECL(DWORD, GetGlyphOutlineWow,  (HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2 *));
UDECL(DWORD, GetStringBitmapA,    (HDC, LPCSTR, UINT, UINT, BYTE *));
UDECL(DWORD, GetStringBitmapW,    (HDC, LPCWSTR, UINT, UINT, BYTE *));
UDECL(BOOL,  GetCharABCWidthsFloatI, (HDC, UINT, UINT, LPWORD, LPABCFLOAT));

extern const MAT2 oa_ident;   /* eM11 = eM22 = 1.0 (16.16 fixed), eM12 = eM21 = 0 */
#endif
