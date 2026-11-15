#include "oa_common.h"

const MAT2 oa_ident = { {0,1}, {0,0}, {0,0}, {0,1} };

void oa_reset_dc(HDC dc){
  U(SetMapMode)(dc, MM_TEXT);
  U(SetGraphicsMode)(dc, GM_COMPATIBLE);
  U(SetWindowOrgEx)(dc, 0, 0, NULL); U(SetViewportOrgEx)(dc, 0, 0, NULL);
}

int oa_open(oa_font *d, const WCHAR *face, int h, int need_tt){
  LOGFONTW lf; WCHAR got[LF_FACESIZE] = L"";
  memset(d, 0, sizeof *d);
  d->dc = U(GetDC)(NULL);
  if(!d->dc){ t_note("GetDC(NULL) failed"); return 0; }
  oa_reset_dc(d->dc);
  memset(&lf, 0, sizeof lf); lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
  d->f = U(CreateFontIndirectW)(&lf);
  d->old = (HFONT)U(SelectObject)(d->dc, d->f);
  U(GetTextFaceW)(d->dc, LF_FACESIZE, got);
  lstrcpynW(d->face, got, LF_FACESIZE);
  if(!d->f || !U(GetTextMetricsW)(d->dc, &d->tm)){ t_note("%ls %d: CreateFontIndirectW/GetTextMetricsW failed", face, h); oa_close(d); return 0; }
  {
    int tt = (d->tm.tmPitchAndFamily & TMPF_TRUETYPE) != 0;
    if(lstrcmpiW(got, face)){ t_note("%ls %d: realised as %ls", face, h, got); if(need_tt >= 0){ oa_close(d); return 0; } }
    else if(need_tt == 1 && !tt){ t_note("%ls %d: not realised as TrueType (pf 0x%x)", face, h, d->tm.tmPitchAndFamily); oa_close(d); return 0; }
    else if(need_tt == 0 && tt){ t_note("%ls %d: realised as TrueType, wanted raster", face, h); oa_close(d); return 0; }
  }
  d->ok = 1;
  return 1;
}
void oa_close(oa_font *d){
  if(d->dc){ if(d->old) U(SelectObject)(d->dc, d->old); U(ReleaseDC)(NULL, d->dc); }
  if(d->f) U(DeleteObject)(d->f);
  memset(d, 0, sizeof *d);
}
int oa_wide_from_ansi(const char *a, WCHAR *w, int cap){ return MultiByteToWideChar(CP_ACP, 0, a, -1, w, cap); }
