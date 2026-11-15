#ifndef GIR_H
#define GIR_H
#include "t.h"
#define GIR_TAG(a,b,c,d) ((DWORD)(a)|((DWORD)(b)<<8)|((DWORD)(c)<<16)|((DWORD)(d)<<24))
typedef struct { HDC dc; HFONT f, old; } gir_dc;
BYTE *gir_fontdata(HDC dc, DWORD tag, DWORD *len);
int gir_read_cmap(HDC dc, WORD *gid);
int gir_name_family(HDC dc, WCHAR *out, int cap);
HFONT gir_mkfont(const WCHAR *face,int h,int wt,int cs);
int gir_open(gir_dc *d, const WCHAR *face, int h, char *why, size_t cap);
void gir_close(gir_dc *d);
#endif
