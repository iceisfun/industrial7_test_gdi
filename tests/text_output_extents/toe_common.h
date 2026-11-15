#ifndef TOE_COMMON_H
#define TOE_COMMON_H
#include "t.h"
#include <stdlib.h>

/* Offscreen rendering environment: a memory DC holding a top-down 32bpp DIB section and an Arial font of a fixed
 * pixel height. No window is involved. Pixels are 0x00RRGGBB. */
typedef struct {
  HDC dc; HBITMAP bmp, obmp; HFONT f, of; DWORD *px; int w, h; TEXTMETRICW tm;
} E;

static DWORD c2p(COLORREF c){ return ((DWORD)GetRValue(c)<<16)|((DWORD)GetGValue(c)<<8)|GetBValue(c); }

/* quality: NONANTIALIASED_QUALITY gives pixels that are exactly background or text colour. */
static int e_open(E *e, int w, int h, int quality, int px){
  memset(e,0,sizeof *e); e->w=w; e->h=h;
  e->dc=U(CreateCompatibleDC)(NULL); if(!e->dc) return 0;
  BITMAPINFO bi; memset(&bi,0,sizeof bi);
  bi.bmiHeader.biSize=sizeof bi.bmiHeader; bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h;
  bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
  void *bits=NULL;
  e->bmp=U(CreateDIBSection)(e->dc,&bi,DIB_RGB_COLORS,&bits,NULL,0); e->px=(DWORD*)bits;
  if(!e->bmp||!bits) return 0;
  e->obmp=(HBITMAP)U(SelectObject)(e->dc,e->bmp);
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=-px; lf.lfWeight=400; lf.lfCharSet=ANSI_CHARSET;
  lf.lfQuality=(BYTE)quality; lstrcpyW(lf.lfFaceName,L"Arial");
  e->f=U(CreateFontIndirectW)(&lf); if(!e->f) return 0;
  e->of=(HFONT)U(SelectObject)(e->dc,e->f);
  WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(e->dc,LF_FACESIZE,got);
  if(!U(GetTextMetricsW)(e->dc,&e->tm)) return 0;
  if(lstrcmpiW(got,L"Arial")||!(e->tm.tmPitchAndFamily&TMPF_TRUETYPE)) return 0;
  return 1;
}
static void e_close(E *e){
  if(e->dc){ if(e->of) U(SelectObject)(e->dc,e->of); if(e->obmp) U(SelectObject)(e->dc,e->obmp); }
  if(e->f) U(DeleteObject)(e->f);
  if(e->bmp) U(DeleteObject)(e->bmp);
  if(e->dc) U(DeleteDC)(e->dc);
  memset(e,0,sizeof *e);
}
#define E_OPEN(e,w,h,q,px) do{ if(!e_open(&(e),w,h,q,px)){ e_close(&(e)); t_skip("Arial TrueType %d px or the DIB could not be set up",px); return; } }while(0)

static void e_sync(E *e){ U(GdiFlush)(); }
static void e_clear(E *e, COLORREF c){ DWORD p=c2p(c); for(int i=0;i<e->w*e->h;i++) e->px[i]=p; }
static DWORD e_get(E *e, int x, int y){ if(x<0||y<0||x>=e->w||y>=e->h) return 0xDEADBEEF; return e->px[y*e->w+x]&0xFFFFFF; }
/* bounding box of all pixels != bg inside clip (half open); returns the ink pixel count; box left>right when empty */
static int e_bbox(E *e, COLORREF bg, const RECT *clip, RECT *out){
  RECT c={0,0,e->w,e->h}; if(clip) c=*clip;
  DWORD b=c2p(bg); int n=0; out->left=out->top=0x7fffffff; out->right=out->bottom=-1;
  e_sync(e);
  for(int y=c.top;y<c.bottom;y++) for(int x=c.left;x<c.right;x++) if((e->px[y*e->w+x]&0xFFFFFF)!=b){
    n++; if(x<out->left) out->left=x; if(y<out->top) out->top=y; if(x>out->right) out->right=x; if(y>out->bottom) out->bottom=y; }
  return n;
}
static int e_count(E *e, DWORD pixel, const RECT *r){
  e_sync(e); int n=0; for(int y=r->top;y<r->bottom;y++) for(int x=r->left;x<r->right;x++) if(e_get(e,x,y)==pixel) n++; return n;
}
static DWORD *e_snap(E *e){ e_sync(e); DWORD *s=(DWORD*)malloc((size_t)e->w*e->h*4); memcpy(s,e->px,(size_t)e->w*e->h*4); return s; }
static int e_same(E *e, const DWORD *s){ e_sync(e); return !memcmp(s,e->px,(size_t)e->w*e->h*4); }
/* number of pixels that differ between the buffer and a snapshot */
static int e_diff(E *e, const DWORD *s){ e_sync(e); int n=0; for(int i=0;i<e->w*e->h;i++) if(s[i]!=e->px[i]) n++; return n; }

static long sumw(HDC dc, const WCHAR *s, int n){ long t=0; for(int i=0;i<n;i++){ INT w=0; U(GetCharWidth32W)(dc,s[i],s[i],&w); t+=w; } return t; }
static void widen(const char *a, WCHAR *w, int cap){ int i=0; for(;a[i]&&i<cap-1;i++) w[i]=(WCHAR)(unsigned char)a[i]; w[i]=0; }
/* evaluates its first argument exactly once (EXPECT_EQ evaluates it twice, which breaks Set* calls) */
#define EQV(a,b) do{ unsigned long long v_=(unsigned long long)(ULONG_PTR)(a); unsigned long long w_=(unsigned long long)(ULONG_PTR)(b); \
  t_check(v_==w_,__FILE__,__LINE__,#a " == " #b,"got 0x%llx want 0x%llx",v_,w_); }while(0)
#endif
