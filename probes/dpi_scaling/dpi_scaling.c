/* dpi_scaling: the DPI dimension of the D12 data. The same core measurements, repeated per DPI-awareness context, so a
 * sweep over system DPI 96/120/144/168/192 (tools/vm_run.sh --dpi ..., one boot) x manifest variants x API variants
 * gives every (system DPI, process awareness, thread awareness) combination that comctl32/user32 see.
 *
 * Per thread context (ctx=proc|unaware|system|pm|pm2|gdis; --quick: proc only):
 *   ctx      awareness/DPI APIs, screen DC caps, GetDialogBaseUnits, GetDpiForMonitor
 *   sm       GetSystemMetrics (50 indices)
 *   ncm/ncmf SystemParametersInfo NONCLIENTMETRICS numbers / LOGFONTs (+ ICONMETRICS, icon title font)
 *   stock    the 7 stock fonts: GetObject LOGFONT + realised face/TEXTMETRIC on a screen DC
 *   ptconv   the pt -> px rules at this context's LOGPIXELSY next to what DEFAULT_GUI_FONT / NCM really hold
 *   tm/cw    UI faces at 7..14 pt (lfHeight = -MulDiv(pt, LOGPIXELSY, 72)): TEXTMETRIC, extents, char widths
 *   dlg      DS_SETFONT / DS_SHELLFONT dialogs: font, MapDialogRect, window/client size, window DPI
 *   ctl      comctl32/user32 control heights (list view row/header, header, tree, button, edit, combo, list box,
 *            status bar, tab) with the control default font, DEFAULT_GUI_FONT and the NCM message font
 *   awr      AdjustWindowRectEx in this context
 *   win      a real top-level window: logical vs physical rect, GetDpiForWindow, Logical<->Physical point mapping
 *   cap      (--full) a small visible window drawn in this context and captured from the screen in physical pixels:
 *            what DWM bitmap stretching (unaware) or GDI scaling (gdis) does to text
 * Once per manifest exe (ctx=proc, not in API children): smdpi (GetSystemMetricsForDpi), ncm/ncmf src=fordpi (SystemParametersInfoForDpi),
 *   awr dpi=N (AdjustWindowRectExForDpi), muldiv (kernel32 MulDiv grid).
 * API variants (children): the no-manifest exe re-runs itself after its own recording with --api MODE, which calls
 *   SetProcessDPIAware / SetProcessDpiAwareness / SetProcessDpiAwarenessContext before anything else; each child
 *   writes its own recording with variant=api-MODE (file <tag>.api-MODE.rec).
 * usage: dpi_scaling.exe [recorder options] [--quick|--full] [--no-children] [--no-cap] [--api MODE]
 * Units: px = device pixels in the measuring context's coordinate space (logical for virtualized contexts; the
 * `win`/`cap` tables also give the physical values). Heights follow docs/PROBE_FORMAT.md section 3. */
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include "rec.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ dynamic APIs (Win10-only ones may be absent) */
typedef HANDLE DCTX;   /* DPI_AWARENESS_CONTEXT */
typedef DCTX (WINAPI *pSetThreadCtx)(DCTX);
typedef DCTX (WINAPI *pGetThreadCtx)(void);
typedef int  (WINAPI *pAwFromCtx)(DCTX);
typedef UINT (WINAPI *pDpiFromCtx)(DCTX);
typedef UINT (WINAPI *pGetDpiForSystem)(void);
typedef UINT (WINAPI *pGetDpiForWindow)(HWND);
typedef int  (WINAPI *pGetSmForDpi)(int, UINT);
typedef BOOL (WINAPI *pSpiForDpi)(UINT, UINT, PVOID, UINT, UINT);
typedef BOOL (WINAPI *pAwrForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef UINT (WINAPI *pGetSysDpiForProcess)(HANDLE);
typedef BOOL (WINAPI *pLtoP)(HWND, LPPOINT);
typedef BOOL (WINAPI *pSetProcCtx)(DCTX);
typedef BOOL (WINAPI *pSetProcAware)(void);
typedef HRESULT (WINAPI *pShcSetAw)(int);
typedef HRESULT (WINAPI *pShcGetAw)(HANDLE, int *);
typedef HRESULT (WINAPI *pShcDpiForMon)(HMONITOR, int, UINT *, UINT *);
typedef HRESULT (WINAPI *pDwmFlush)(void);
typedef DCTX (WINAPI *pGetWinCtx)(HWND);
static struct {
  pSetThreadCtx SetThreadCtx; pGetThreadCtx GetThreadCtx; pAwFromCtx AwFromCtx; pDpiFromCtx DpiFromCtx;
  pGetDpiForSystem GetDpiForSystem; pGetDpiForWindow GetDpiForWindow; pGetSmForDpi GetSmForDpi; pSpiForDpi SpiForDpi;
  pAwrForDpi AwrForDpi; pGetSysDpiForProcess GetSysDpiForProcess; pLtoP LtoP, PtoL; pSetProcCtx SetProcCtx;
  pSetProcAware SetProcAware; pShcSetAw ShcSetAw; pShcGetAw ShcGetAw; pShcDpiForMon ShcDpiForMon; pDwmFlush DwmFlush;
  pGetWinCtx GetWinCtx;
} A;
static void load_apis(void){
  HMODULE u=LoadLibraryA("user32.dll"), s=LoadLibraryA("shcore.dll"), d=LoadLibraryA("dwmapi.dll");
#define G(m,f,n) A.f=(void*)(m?GetProcAddress(m,n):NULL)
  G(u,SetThreadCtx,"SetThreadDpiAwarenessContext"); G(u,GetThreadCtx,"GetThreadDpiAwarenessContext");
  G(u,AwFromCtx,"GetAwarenessFromDpiAwarenessContext"); G(u,DpiFromCtx,"GetDpiFromDpiAwarenessContext");
  G(u,GetDpiForSystem,"GetDpiForSystem"); G(u,GetDpiForWindow,"GetDpiForWindow"); G(u,GetSmForDpi,"GetSystemMetricsForDpi");
  G(u,SpiForDpi,"SystemParametersInfoForDpi"); G(u,AwrForDpi,"AdjustWindowRectExForDpi");
  G(u,GetSysDpiForProcess,"GetSystemDpiForProcess"); G(u,LtoP,"LogicalToPhysicalPointForPerMonitorDPI");
  G(u,PtoL,"PhysicalToLogicalPointForPerMonitorDPI"); G(u,SetProcCtx,"SetProcessDpiAwarenessContext");
  G(u,SetProcAware,"SetProcessDPIAware"); G(u,GetWinCtx,"GetWindowDpiAwarenessContext");
  G(s,ShcSetAw,"SetProcessDpiAwareness"); G(s,ShcGetAw,"GetProcessDpiAwareness"); G(s,ShcDpiForMon,"GetDpiForMonitor");
  G(d,DwmFlush,"DwmFlush");
#undef G
}

/* ------------------------------------------------------------------ options / context list */
static int g_full=1, g_children=1, g_cap=1;
static const char *g_api=NULL;
static long long g_api_ret=0; static DWORD g_api_err=0; static int g_api_avail=0;
#define CTX(v) ((DCTX)(LONG_PTR)(v))
static const struct { const char *name; LONG_PTR v; } CTXS[]={
  {"proc",0},{"unaware",-1},{"system",-2},{"pm",-3},{"pm2",-4},{"gdis",-5}};
#define NCTX ((int)(sizeof CTXS/sizeof*CTXS))
static const char *g_ctx="proc";

static int ctx_enter(int i, DCTX *saved){     /* returns 1 if the thread is now in context i */
  *saved=NULL; g_ctx=CTXS[i].name;
  if(!CTXS[i].v) return 1;
  if(!A.SetThreadCtx) return 0;
  *saved=A.SetThreadCtx(CTX(CTXS[i].v));
  return *saved!=NULL;
}
static void ctx_leave(DCTX saved){ if(saved && A.SetThreadCtx) A.SetThreadCtx(saved); g_ctx="proc"; }

static int logpixy(void){ HDC dc=GetDC(NULL); int y=GetDeviceCaps(dc,LOGPIXELSY); ReleaseDC(NULL,dc); return y; }
static void kctx(rk *k){ rk_reset(k); rk_s(k,"ctx",g_ctx); }
static void pump(DWORD ms){ DWORD t0=GetTickCount(); MSG m;
  do { while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); } Sleep(10); } while(GetTickCount()-t0<ms); }

/* ------------------------------------------------------------------ helpers for rows */
static void rv_lf(rv *v, const LOGFONTW *lf){
  rv_i(v,lf->lfHeight); rv_i(v,lf->lfWidth); rv_i(v,lf->lfWeight); rv_i(v,lf->lfItalic); rv_i(v,lf->lfCharSet);
  rv_i(v,lf->lfQuality); rv_x(v,lf->lfPitchAndFamily); rv_w(v,lf->lfFaceName); }
#define LF_COLS "lfh:px lfw:px weight italic cs quality pf face"
static void rv_lf_na(rv *v){ for(int i=0;i<8;i++) rv_na(v); }

static const WCHAR *STD43=L"The quick brown fox jumps over the lazy dog";
static const WCHAR *ALPHA52=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
/* TEXTMETRIC of the font selected in dc: h a d il el ave max weight ovh digx digy pf cs  + extents */
#define TM_COLS "h:px a:px d:px il:px el:px ave:px max:px weight ovh:px digx digy pf cs ext43_cx:px ext43_cy:px alpha_cx:px"
static int rv_tm(rv *v, HDC dc){
  TEXTMETRICW tm; SIZE s1={0,0}, s2={0,0};
  if(!GetTextMetricsW(dc,&tm)){ for(int i=0;i<16;i++) rv_na(v); return 0; }
  rv_i(v,tm.tmHeight); rv_i(v,tm.tmAscent); rv_i(v,tm.tmDescent); rv_i(v,tm.tmInternalLeading); rv_i(v,tm.tmExternalLeading);
  rv_i(v,tm.tmAveCharWidth); rv_i(v,tm.tmMaxCharWidth); rv_i(v,tm.tmWeight); rv_i(v,tm.tmOverhang);
  rv_i(v,tm.tmDigitizedAspectX); rv_i(v,tm.tmDigitizedAspectY); rv_x(v,tm.tmPitchAndFamily); rv_i(v,tm.tmCharSet);
  if(GetTextExtentPoint32W(dc,STD43,43,&s1)){ rv_i(v,s1.cx); rv_i(v,s1.cy); } else { rv_na(v); rv_na(v); }
  if(GetTextExtentPoint32W(dc,ALPHA52,52,&s2)) rv_i(v,s2.cx); else rv_na(v);
  return tm.tmHeight;
}
static int font_tm_height(HDC dc, HFONT f){
  HFONT o=f?(HFONT)SelectObject(dc,f):NULL; TEXTMETRICW tm; int h=-1;
  if(GetTextMetricsW(dc,&tm)) h=tm.tmHeight;
  if(o) SelectObject(dc,o);
  return h; }

/* ------------------------------------------------------------------ table: ctx */
static void t_ctx(int set_ok){
  rk k; rv v; kctx(&k); rv_reset(&v);
  rv_i(&v,set_ok);
  DCTX c=A.GetThreadCtx?A.GetThreadCtx():NULL;
  if(c){ rv_x(&v,(unsigned long long)(ULONG_PTR)c); rv_i(&v,A.AwFromCtx?A.AwFromCtx(c):-99); rv_i(&v,A.DpiFromCtx?(long long)A.DpiFromCtx(c):-99); }
  else { rv_na(&v); rv_na(&v); rv_na(&v); }
  if(A.GetDpiForSystem) rv_i(&v,A.GetDpiForSystem()); else rv_na(&v);
  if(A.GetSysDpiForProcess) rv_i(&v,A.GetSysDpiForProcess(GetCurrentProcess())); else rv_na(&v);
  rv_i(&v,IsProcessDPIAware());
  int aw=-1; if(A.ShcGetAw && A.ShcGetAw(NULL,&aw)==S_OK) rv_i(&v,aw); else rv_na(&v);
  HDC dc=GetDC(NULL);
  rv_i(&v,GetDeviceCaps(dc,LOGPIXELSX)); rv_i(&v,GetDeviceCaps(dc,LOGPIXELSY));
  rv_i(&v,GetDeviceCaps(dc,HORZRES)); rv_i(&v,GetDeviceCaps(dc,VERTRES));
  rv_i(&v,GetDeviceCaps(dc,DESKTOPHORZRES)); rv_i(&v,GetDeviceCaps(dc,DESKTOPVERTRES));
  ReleaseDC(NULL,dc);
  long bu=GetDialogBaseUnits(); rv_i(&v,LOWORD(bu)); rv_i(&v,HIWORD(bu));
  POINT p0={0,0}; HMONITOR mon=MonitorFromPoint(p0,MONITOR_DEFAULTTOPRIMARY);
  for(int t=0;t<3;t++){ UINT x=0,y=0; if(A.ShcDpiForMon && A.ShcDpiForMon(mon,t,&x,&y)==S_OK){ rv_i(&v,x); rv_i(&v,y);} else { rv_na(&v); rv_na(&v);} }
  MONITORINFO mi; mi.cbSize=sizeof mi;
  if(GetMonitorInfoW(mon,&mi)){ rv_i(&v,mi.rcMonitor.right-mi.rcMonitor.left); rv_i(&v,mi.rcMonitor.bottom-mi.rcMonitor.top);
    rv_i(&v,mi.rcWork.right-mi.rcWork.left); rv_i(&v,mi.rcWork.bottom-mi.rcWork.top); }
  else for(int i=0;i<4;i++) rv_na(&v);
  if(A.GetDpiForWindow) rv_i(&v,A.GetDpiForWindow(GetDesktopWindow())); else rv_na(&v);
  rec_row("ctx",&k,&v);
}

/* ------------------------------------------------------------------ table: sm */
#define SMI(n) {#n, SM_##n}
static const struct { const char *n; int i; } SMS[]={
  SMI(CXSCREEN),SMI(CYSCREEN),SMI(CXVSCROLL),SMI(CYHSCROLL),SMI(CYCAPTION),SMI(CXBORDER),SMI(CYBORDER),SMI(CXDLGFRAME),
  SMI(CYDLGFRAME),SMI(CYVTHUMB),SMI(CXHTHUMB),SMI(CXICON),SMI(CYICON),SMI(CXCURSOR),SMI(CYCURSOR),SMI(CYMENU),
  SMI(CXFULLSCREEN),SMI(CYFULLSCREEN),SMI(CYVSCROLL),SMI(CXHSCROLL),SMI(CXMIN),SMI(CYMIN),SMI(CXSIZE),SMI(CYSIZE),
  SMI(CXFRAME),SMI(CYFRAME),SMI(CXMINTRACK),SMI(CYMINTRACK),SMI(CXDOUBLECLK),SMI(CYDOUBLECLK),SMI(CXICONSPACING),
  SMI(CYICONSPACING),SMI(CXEDGE),SMI(CYEDGE),SMI(CXMINSPACING),SMI(CYMINSPACING),SMI(CXSMICON),SMI(CYSMICON),
  SMI(CYSMCAPTION),SMI(CXSMSIZE),SMI(CYSMSIZE),SMI(CXMENUSIZE),SMI(CYMENUSIZE),SMI(CXMINIMIZED),SMI(CYMINIMIZED),
  SMI(CXMAXTRACK),SMI(CYMAXTRACK),SMI(CXMAXIMIZED),SMI(CYMAXIMIZED),SMI(CXDRAG),SMI(CYDRAG),SMI(CXMENUCHECK),
  SMI(CYMENUCHECK),SMI(CXFOCUSBORDER),SMI(CYFOCUSBORDER),{"CXPADDEDBORDER",92},SMI(CXVIRTUALSCREEN),SMI(CYVIRTUALSCREEN)};
#define NSMS ((int)(sizeof SMS/sizeof*SMS))
static void t_sm(void){
  for(int i=0;i<NSMS;i++){ rk k; rv v; kctx(&k); rk_s(&k,"name",SMS[i].n); rv_reset(&v); rv_i(&v,GetSystemMetrics(SMS[i].i)); rec_row("sm",&k,&v); }
}
static const int FORDPI_FULL[]={72,96,120,144,168,192,216,240,288,384};
static const int FORDPI_QUICK[]={96,144};
static void t_smdpi(void){
  const int *d=g_full?FORDPI_FULL:FORDPI_QUICK; int nd=g_full?10:2;
  for(int i=0;i<NSMS;i++) for(int j=0;j<nd;j++){
    rk k; rv v; rk_reset(&k); rk_s(&k,"name",SMS[i].n); rk_i(&k,"dpi",d[j]); rv_reset(&v);
    if(A.GetSmForDpi){ SetLastError(0); int r=A.GetSmForDpi(SMS[i].i,d[j]); rv_i(&v,r); } else rv_na(&v);
    rec_row("smdpi",&k,&v);
  }
}

/* ------------------------------------------------------------------ tables: ncm, ncmf, icm */
static void ncm_rows(const char *src, int dpi, int ok, const NONCLIENTMETRICSW *n, const ICONMETRICSW *im, int okim, const LOGFONTW *itf, int okitf){
  rk k; rv v; kctx(&k); rk_s(&k,"src",src); if(dpi) rk_i(&k,"dpi",dpi); else rk_s(&k,"dpi","~");
  rv_reset(&v);
  if(ok){ rv_i(&v,n->iBorderWidth); rv_i(&v,n->iScrollWidth); rv_i(&v,n->iScrollHeight); rv_i(&v,n->iCaptionWidth);
    rv_i(&v,n->iCaptionHeight); rv_i(&v,n->iSmCaptionWidth); rv_i(&v,n->iSmCaptionHeight); rv_i(&v,n->iMenuWidth);
    rv_i(&v,n->iMenuHeight); rv_i(&v,n->iPaddedBorderWidth); }
  else for(int i=0;i<10;i++) rv_na(&v);
  if(okim){ rv_i(&v,im->iHorzSpacing); rv_i(&v,im->iVertSpacing); rv_i(&v,im->iTitleWrap); } else { rv_na(&v); rv_na(&v); rv_na(&v); }
  rec_row("ncm",&k,&v);
  const struct { const char *n; const LOGFONTW *lf; int ok; } F[]={
    {"caption",&n->lfCaptionFont,ok},{"smcaption",&n->lfSmCaptionFont,ok},{"menu",&n->lfMenuFont,ok},
    {"status",&n->lfStatusFont,ok},{"message",&n->lfMessageFont,ok},{"icon",&im->lfFont,okim},{"icontitle",itf,okitf}};
  for(unsigned i=0;i<sizeof F/sizeof*F;i++){
    rk k2=k; rk_s(&k2,"font",F[i].n); rv_reset(&v); if(F[i].ok) rv_lf(&v,F[i].lf); else rv_lf_na(&v); rec_row("ncmf",&k2,&v); }
}
static void t_ncm_spi(void){
  NONCLIENTMETRICSW n; memset(&n,0,sizeof n); n.cbSize=sizeof n;
  int ok=SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof n,&n,0);
  ICONMETRICSW im; memset(&im,0,sizeof im); im.cbSize=sizeof im; int okim=SystemParametersInfoW(SPI_GETICONMETRICS,sizeof im,&im,0);
  LOGFONTW itf; memset(&itf,0,sizeof itf); int okitf=SystemParametersInfoW(SPI_GETICONTITLELOGFONT,sizeof itf,&itf,0);
  ncm_rows("spi",0,ok,&n,&im,okim,&itf,okitf);
}
static void t_ncm_fordpi(void){
  const int *d=g_full?FORDPI_FULL:FORDPI_QUICK; int nd=g_full?10:2;
  for(int j=0;j<nd;j++){
    NONCLIENTMETRICSW n; memset(&n,0,sizeof n); n.cbSize=sizeof n;
    ICONMETRICSW im; memset(&im,0,sizeof im); im.cbSize=sizeof im; LOGFONTW itf; memset(&itf,0,sizeof itf);
    int ok=0,okim=0,okitf=0;
    if(A.SpiForDpi){ ok=A.SpiForDpi(SPI_GETNONCLIENTMETRICS,sizeof n,&n,0,d[j]); okim=A.SpiForDpi(SPI_GETICONMETRICS,sizeof im,&im,0,d[j]);
      okitf=A.SpiForDpi(SPI_GETICONTITLELOGFONT,sizeof itf,&itf,0,d[j]); }
    ncm_rows("fordpi",d[j],ok,&n,&im,okim,&itf,okitf);
  }
}

/* ------------------------------------------------------------------ table: stock */
static void t_stock(void){
  static const struct { const char *n; int o; } S[]={{"SYSTEM_FONT",SYSTEM_FONT},{"DEFAULT_GUI_FONT",DEFAULT_GUI_FONT},
    {"ANSI_VAR_FONT",ANSI_VAR_FONT},{"ANSI_FIXED_FONT",ANSI_FIXED_FONT},{"OEM_FIXED_FONT",OEM_FIXED_FONT},
    {"DEVICE_DEFAULT_FONT",DEVICE_DEFAULT_FONT},{"SYSTEM_FIXED_FONT",SYSTEM_FIXED_FONT}};
  HDC dc=GetDC(NULL);
  for(unsigned i=0;i<sizeof S/sizeof*S;i++){
    rk k; rv v; kctx(&k); rk_s(&k,"obj",S[i].n); rv_reset(&v);
    HFONT f=(HFONT)GetStockObject(S[i].o); LOGFONTW lf;
    if(f && GetObjectW(f,sizeof lf,&lf)) rv_lf(&v,&lf); else rv_lf_na(&v);
    HFONT o=f?(HFONT)SelectObject(dc,f):NULL; WCHAR face[LF_FACESIZE]=L"";
    if(o && GetTextFaceW(dc,LF_FACESIZE,face)) rv_w(&v,face); else rv_na(&v);
    rv_tm(&v,dc);
    if(o) SelectObject(dc,o);
    rec_row("stock",&k,&v);
  }
  ReleaseDC(NULL,dc);
}

/* ------------------------------------------------------------------ table: ptconv + muldiv */
static void t_ptconv(void){
  rk k; rv v; kctx(&k); rv_reset(&v);
  int y=logpixy(); rv_i(&v,y);
  for(int pt=8;pt<=9;pt++){ rv_i(&v,MulDiv(pt,y,72)); }
  rv_i(&v,(8*y+36)/72);
  LOGFONTW lf; HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
  if(f && GetObjectW(f,sizeof lf,&lf)) rv_i(&v,lf.lfHeight); else rv_na(&v);
  NONCLIENTMETRICSW n; memset(&n,0,sizeof n); n.cbSize=sizeof n;
  if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof n,&n,0)){ rv_i(&v,n.lfMessageFont.lfHeight); rv_i(&v,n.lfCaptionFont.lfHeight); } else { rv_na(&v); rv_na(&v); }
  if(SystemParametersInfoW(SPI_GETICONTITLELOGFONT,sizeof lf,&lf,0)) rv_i(&v,lf.lfHeight); else rv_na(&v);
  rec_row("ptconv",&k,&v);
}
static void t_muldiv(void){
  static const int D[]={96,120,144,168,192};
  for(int pt=5;pt<=16;pt++) for(int j=0;j<5;j++){
    rk k; rv v; rk_reset(&k); rk_i(&k,"pt",pt); rk_i(&k,"dpi",D[j]); rv_reset(&v);
    rv_i(&v,MulDiv(pt,D[j],72)); rv_i(&v,MulDiv(-pt,D[j],72)); rv_i(&v,-MulDiv(pt,D[j],72)); rec_row("muldiv",&k,&v); }
}

/* ------------------------------------------------------------------ tables: tm, cw (+H cw_ascii, abc) */
static const char *FACES_FULL[]={"Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma","Microsoft Sans Serif","MS Sans Serif",
  "Arial","Courier New","Courier","System","Terminal","Fixedsys"};
static const char *FACES_QUICK[]={"Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma"};
static const int PTS_FULL[]={7,8,9,10,11,12,14}, PTS_SUB[]={8,9};
static void t_tm(int detail){
  const char **faces; int nf; const int *pts; int np;
  if(detail && g_full){ faces=FACES_FULL; nf=12; pts=PTS_FULL; np=7; }
  else if(detail){ faces=FACES_QUICK; nf=4; pts=PTS_SUB; np=2; }
  else { faces=FACES_QUICK; nf=4; pts=PTS_SUB; np=2; }
  HDC dc=GetDC(NULL); int y=GetDeviceCaps(dc,LOGPIXELSY);
  for(int fi=0;fi<nf;fi++) for(int pi=0;pi<np;pi++){
    LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=-MulDiv(pts[pi],y,72); lf.lfWeight=FW_NORMAL; lf.lfCharSet=DEFAULT_CHARSET;
    MultiByteToWideChar(CP_ACP,0,faces[fi],-1,lf.lfFaceName,LF_FACESIZE);
    HFONT f=CreateFontIndirectW(&lf), o=(HFONT)SelectObject(dc,f);
    rk k; rv v; kctx(&k); rk_s(&k,"face",faces[fi]); rk_i(&k,"pt",pts[pi]); rv_reset(&v);
    rv_i(&v,y); rv_i(&v,lf.lfHeight);
    WCHAR got[LF_FACESIZE]=L""; if(GetTextFaceW(dc,LF_FACESIZE,got)) rv_w(&v,got); else rv_na(&v);
    rv_tm(&v,dc);
    rec_row("tm",&k,&v);
    INT w[95];
    if(GetCharWidth32W(dc,0x20,0x7e,w)){
      rec_hash("cw_ascii",&k,w,sizeof w);
      if(detail){ char buf[95*6]; int n=0; for(int i=0;i<95;i++) n+=snprintf(buf+n,sizeof buf-n,i?",%d":"%d",w[i]);
        rv_reset(&v); rv_raw(&v,buf); rec_row("cw",&k,&v); }
    }
    ABC abc[95]; if(GetCharABCWidthsW(dc,0x20,0x7e,abc)) rec_hash("abc_ascii",&k,abc,sizeof abc);
    SelectObject(dc,o); DeleteObject(f);
  }
  ReleaseDC(NULL,dc);
}

/* ------------------------------------------------------------------ table: dlg */
static INT_PTR CALLBACK dlgproc(HWND h, UINT m, WPARAM w, LPARAM l){ (void)h;(void)w;(void)l; return m==WM_INITDIALOG; }
static HWND make_dialog(const WCHAR *face, int pt, DWORD extra_style){
  static DWORD buf[256]; memset(buf,0,sizeof buf);
  DLGTEMPLATE *t=(DLGTEMPLATE*)buf;
  t->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|extra_style|(face?DS_SETFONT:0);
  t->cdit=0; t->x=10; t->y=10; t->cx=100; t->cy=50;
  WORD *p=(WORD*)(t+1); *p++=0; *p++=0; *p++=0;           /* menu, class, title "" */
  if(face){ *p++=(WORD)pt; while(*face) *p++=*face++; *p++=0; }
  return CreateDialogIndirectParamW(GetModuleHandleW(NULL),t,NULL,dlgproc,0);
}
static void t_dlg(void){
  static const struct { const WCHAR *f; int pt; } D[]={{L"MS Shell Dlg",8},{L"MS Shell Dlg 2",8},{L"Segoe UI",9},
    {L"Tahoma",8},{L"Microsoft Sans Serif",8},{NULL,0}};
  static const char *DN[]={"MS Shell Dlg","MS Shell Dlg 2","Segoe UI","Tahoma","Microsoft Sans Serif","nofont"};
  int nd=g_full?6:3;
  for(int i=0;i<nd;i++) for(int s=0;s<2;s++){
    if(!D[i].f && s) continue;
    HWND h=make_dialog(D[i].f,D[i].pt,s?DS_FIXEDSYS:0);
    rk k; rv v; kctx(&k); rk_s(&k,"font",DN[i]); if(D[i].f) rk_i(&k,"pt",D[i].pt); else rk_s(&k,"pt","~");
    rk_s(&k,"style",s?"shellfont":"setfont"); rv_reset(&v);
    if(!h){ rv_i(&v,0); rv_i(&v,GetLastError()); for(int j=0;j<28;j++) rv_na(&v); rec_row("dlg",&k,&v); continue; }
    rv_i(&v,1); rv_i(&v,0);
    HFONT f=(HFONT)SendMessageW(h,WM_GETFONT,0,0); LOGFONTW lf;
    if(f && GetObjectW(f,sizeof lf,&lf)) { rv_i(&v,lf.lfHeight); rv_w(&v,lf.lfFaceName); } else { rv_na(&v); rv_na(&v); }
    HDC dc=GetDC(h); HFONT o=f?(HFONT)SelectObject(dc,f):NULL;
    WCHAR got[LF_FACESIZE]=L""; if(GetTextFaceW(dc,LF_FACESIZE,got)) rv_w(&v,got); else rv_na(&v);
    rv_tm(&v,dc); if(o) SelectObject(dc,o); ReleaseDC(h,dc);
    RECT r={0,0,4,8}; if(MapDialogRect(h,&r)){ rv_i(&v,r.right); rv_i(&v,r.bottom);} else { rv_na(&v); rv_na(&v); }
    RECT r2={0,0,100,50}; if(MapDialogRect(h,&r2)){ rv_i(&v,r2.right); rv_i(&v,r2.bottom);} else { rv_na(&v); rv_na(&v); }
    RECT cr,wr; GetClientRect(h,&cr); GetWindowRect(h,&wr);
    rv_i(&v,cr.right-cr.left); rv_i(&v,cr.bottom-cr.top); rv_i(&v,wr.right-wr.left); rv_i(&v,wr.bottom-wr.top);
    if(A.GetDpiForWindow) rv_i(&v,A.GetDpiForWindow(h)); else rv_na(&v);
    rec_row("dlg",&k,&v);
    DestroyWindow(h);
  }
}

/* ------------------------------------------------------------------ table: ctl */
static LRESULT CALLBACK parentproc(HWND h, UINT m, WPARAM w, LPARAM l){ return DefWindowProcW(h,m,w,l); }
static void ctl_row(const char *font, const char *cls, const char *metric, int ok, long long val){
  rk k; rv v; kctx(&k); rk_s(&k,"font",font); rk_s(&k,"cls",cls); rk_s(&k,"m",metric); rv_reset(&v);
  if(ok) rv_i(&v,val); else rv_na(&v); rec_row("ctl",&k,&v); }
static int wh(HWND h){ RECT r; if(!h || !GetWindowRect(h,&r)) return -1; return r.bottom-r.top; }
static void ctl_fonth(const char *font, const char *cls, HWND h){
  HFONT f=(HFONT)SendMessageW(h,WM_GETFONT,0,0); HDC dc=GetDC(h); int th=font_tm_height(dc,f); ReleaseDC(h,dc);
  ctl_row(font,cls,"font_h",th>=0,th);
  LOGFONTW lf; ctl_row(font,cls,"font_lfh",f && GetObjectW(f,sizeof lf,&lf),f?lf.lfHeight:0); }
static HWND mk(HWND par, const WCHAR *cls, const WCHAR *txt, DWORD st, DWORD ex, int x, int y, int w, int h, HFONT f){
  HWND c=CreateWindowExW(ex,cls,txt,WS_CHILD|WS_VISIBLE|st,x,y,w,h,par,NULL,GetModuleHandleW(NULL),NULL);
  if(c && f) SendMessageW(c,WM_SETFONT,(WPARAM)f,TRUE);
  return c; }
static void t_ctl(void){
  HWND par=CreateWindowExW(0,L"dpiscal_parent",L"p",WS_OVERLAPPEDWINDOW,0,0,640,480,NULL,NULL,GetModuleHandleW(NULL),NULL);
  if(!par){ rec_note("ctl: parent window failed gle=%lu ctx=%s",GetLastError(),g_ctx); return; }
  if(A.GetDpiForWindow){ ctl_row("~","parent","dpi",1,A.GetDpiForWindow(par)); }
  NONCLIENTMETRICSW n; memset(&n,0,sizeof n); n.cbSize=sizeof n; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof n,&n,0);
  HFONT ncmf=CreateFontIndirectW(&n.lfMessageFont);
  const char *FN[]={"none","dgf","ncm"}; HFONT FF[]={NULL,(HFONT)GetStockObject(DEFAULT_GUI_FONT),ncmf};
  int nfonts=3;
  for(int fi=0;fi<nfonts;fi++){
    const char *fn=FN[fi]; HFONT f=FF[fi];
    /* list view, report view, 3 items */
    HWND lv=mk(par,WC_LISTVIEWW,L"",LVS_REPORT,WS_EX_CLIENTEDGE,0,0,300,200,f);
    if(lv){ LVCOLUMNW c; memset(&c,0,sizeof c); c.mask=LVCF_TEXT|LVCF_WIDTH; c.cx=100; c.pszText=L"Name";
      SendMessageW(lv,LVM_INSERTCOLUMNW,0,(LPARAM)&c);
      for(int i=0;i<3;i++){ LVITEMW it; memset(&it,0,sizeof it); it.mask=LVIF_TEXT; it.iItem=i; it.pszText=L"Item"; SendMessageW(lv,LVM_INSERTITEMW,0,(LPARAM)&it); }
      RECT r0={LVIR_BOUNDS},r1={LVIR_BOUNDS}; r0.left=LVIR_BOUNDS; r1.left=LVIR_BOUNDS;
      int ok0=(int)SendMessageW(lv,LVM_GETITEMRECT,0,(LPARAM)&r0), ok1=(int)SendMessageW(lv,LVM_GETITEMRECT,1,(LPARAM)&r1);
      ctl_row(fn,"listview","row_h",ok0&&ok1,r1.top-r0.top);
      ctl_row(fn,"listview","item0_top",ok0,r0.top);
      ctl_row(fn,"listview","item0_h",ok0,r0.bottom-r0.top);
      RECT rl; rl.left=LVIR_LABEL; int okl=(int)SendMessageW(lv,LVM_GETITEMRECT,0,(LPARAM)&rl);
      ctl_row(fn,"listview","label_h",okl,rl.bottom-rl.top);
      HWND hd=(HWND)SendMessageW(lv,LVM_GETHEADER,0,0); ctl_row(fn,"listview","header_h",hd!=NULL,wh(hd));
      DWORD sp=(DWORD)SendMessageW(lv,LVM_GETITEMSPACING,TRUE,0); ctl_row(fn,"listview","spacing_small_cy",1,HIWORD(sp));
      ctl_fonth(fn,"listview",lv); DestroyWindow(lv);
    } else ctl_row(fn,"listview","create",0,0);
    /* header, standalone, HDM_LAYOUT */
    HWND hd=mk(par,WC_HEADERW,L"",HDS_HORZ,0,0,0,300,30,f);
    if(hd){ HDITEMW hi; memset(&hi,0,sizeof hi); hi.mask=HDI_TEXT|HDI_WIDTH; hi.cxy=100; hi.pszText=L"Name"; SendMessageW(hd,HDM_INSERTITEMW,0,(LPARAM)&hi);
      RECT rc={0,0,300,300}; WINDOWPOS wp; memset(&wp,0,sizeof wp); HDLAYOUT hl={&rc,&wp};
      int ok=(int)SendMessageW(hd,HDM_LAYOUT,0,(LPARAM)&hl); ctl_row(fn,"header","layout_cy",ok,wp.cy);
      ctl_fonth(fn,"header",hd); DestroyWindow(hd); }
    /* tree view */
    HWND tv=mk(par,WC_TREEVIEWW,L"",TVS_HASLINES,WS_EX_CLIENTEDGE,0,0,200,200,f);
    if(tv){ TVINSERTSTRUCTW is; memset(&is,0,sizeof is); is.hInsertAfter=TVI_LAST; is.item.mask=TVIF_TEXT; is.item.pszText=L"Node";
      SendMessageW(tv,TVM_INSERTITEMW,0,(LPARAM)&is);
      ctl_row(fn,"treeview","item_h",1,(int)SendMessageW(tv,TVM_GETITEMHEIGHT,0,0));
      ctl_row(fn,"treeview","indent",1,(int)SendMessageW(tv,TVM_GETINDENT,0,0));
      ctl_fonth(fn,"treeview",tv); DestroyWindow(tv); }
    /* button ideal size (comctl32 v6 only) */
    HWND bt=mk(par,L"BUTTON",L"Cancel",BS_PUSHBUTTON,0,0,0,75,23,f);
    if(bt){ SIZE s={0,0}; int ok=(int)SendMessageW(bt,BCM_GETIDEALSIZE,0,(LPARAM)&s);
      ctl_row(fn,"button","ideal_cx",ok,s.cx); ctl_row(fn,"button","ideal_cy",ok,s.cy); ctl_fonth(fn,"button",bt); DestroyWindow(bt); }
    HWND cb=mk(par,L"BUTTON",L"Check",BS_AUTOCHECKBOX,0,0,0,75,23,f);
    if(cb){ SIZE s={0,0}; int ok=(int)SendMessageW(cb,BCM_GETIDEALSIZE,0,(LPARAM)&s);
      ctl_row(fn,"checkbox","ideal_cx",ok,s.cx); ctl_row(fn,"checkbox","ideal_cy",ok,s.cy); DestroyWindow(cb); }
    /* edit: formatting rect inside a 120x30 control, with and without WS_EX_CLIENTEDGE */
    for(int e=0;e<2;e++){
      HWND ed=mk(par,L"EDIT",L"Text",ES_AUTOHSCROLL|(e?0:WS_BORDER),e?WS_EX_CLIENTEDGE:0,0,0,120,30,f);
      if(!ed) continue;
      const char *cn=e?"edit_ce":"edit_border";
      RECT r; SendMessageW(ed,EM_GETRECT,0,(LPARAM)&r); DWORD mg=(DWORD)SendMessageW(ed,EM_GETMARGINS,0,0);
      RECT cr; GetClientRect(ed,&cr);
      ctl_row(fn,cn,"rect_top",1,r.top); ctl_row(fn,cn,"rect_left",1,r.left); ctl_row(fn,cn,"rect_h",1,r.bottom-r.top);
      ctl_row(fn,cn,"client_h",1,cr.bottom); ctl_row(fn,cn,"margin_l",1,LOWORD(mg)); ctl_row(fn,cn,"margin_r",1,HIWORD(mg));
      if(!e) ctl_fonth(fn,"edit",ed);
      DestroyWindow(ed); }
    /* combo boxes: closed height and item heights */
    for(int c=0;c<2;c++){
      HWND co=mk(par,L"COMBOBOX",L"",(c?CBS_DROPDOWN:CBS_DROPDOWNLIST)|WS_VSCROLL,0,0,0,150,200,f);
      if(!co) continue;
      const char *cn=c?"combo_dropdown":"combo_list";
      SendMessageW(co,CB_ADDSTRING,0,(LPARAM)L"Item");
      ctl_row(fn,cn,"win_h",1,wh(co));
      ctl_row(fn,cn,"sel_item_h",1,(int)SendMessageW(co,CB_GETITEMHEIGHT,(WPARAM)-1,0));
      ctl_row(fn,cn,"item_h",1,(int)SendMessageW(co,CB_GETITEMHEIGHT,0,0));
      COMBOBOXINFO ci; memset(&ci,0,sizeof ci); ci.cbSize=sizeof ci;
      if(GetComboBoxInfo(co,&ci)){ ctl_row(fn,cn,"item_rect_h",1,ci.rcItem.bottom-ci.rcItem.top); ctl_row(fn,cn,"button_w",1,ci.rcButton.right-ci.rcButton.left); }
      if(!c) ctl_fonth(fn,"combo",co);
      DestroyWindow(co); }
    /* list box */
    HWND lb=mk(par,L"LISTBOX",L"",WS_BORDER|LBS_NOTIFY,0,0,0,150,100,f);
    if(lb){ SendMessageW(lb,LB_ADDSTRING,0,(LPARAM)L"Item"); ctl_row(fn,"listbox","item_h",1,(int)SendMessageW(lb,LB_GETITEMHEIGHT,0,0)); DestroyWindow(lb); }
    /* status bar */
    HWND sb=mk(par,STATUSCLASSNAMEW,L"Ready",0,0,0,0,0,0,f);
    if(sb){ SendMessageW(sb,WM_SIZE,0,0); ctl_row(fn,"status","win_h",1,wh(sb)); ctl_fonth(fn,"status",sb); DestroyWindow(sb); }
    /* tab */
    HWND tb=mk(par,WC_TABCONTROLW,L"",0,0,0,0,300,100,f);
    if(tb){ TCITEMW ti; memset(&ti,0,sizeof ti); ti.mask=TCIF_TEXT; ti.pszText=L"General"; SendMessageW(tb,TCM_INSERTITEMW,0,(LPARAM)&ti);
      RECT r; int ok=(int)SendMessageW(tb,TCM_GETITEMRECT,0,(LPARAM)&r); ctl_row(fn,"tab","item_h",ok,r.bottom-r.top); ctl_row(fn,"tab","item_w",ok,r.right-r.left);
      DestroyWindow(tb); }
  }
  DeleteObject(ncmf); DestroyWindow(par);
}

/* ------------------------------------------------------------------ table: awr */
static const struct { const char *n; DWORD st, ex; BOOL menu; } AWR[]={
  {"overlapped",WS_OVERLAPPEDWINDOW,0,FALSE},{"overlapped_menu",WS_OVERLAPPEDWINDOW,0,TRUE},
  {"dialog",WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME,WS_EX_DLGMODALFRAME,FALSE},
  {"tool",WS_OVERLAPPEDWINDOW,WS_EX_TOOLWINDOW,FALSE},{"popup_border",WS_POPUP|WS_BORDER,0,FALSE},
  {"popup_thick",WS_POPUP|WS_THICKFRAME,0,FALSE},{"child_clientedge",WS_CHILD|WS_BORDER,WS_EX_CLIENTEDGE,FALSE}};
#define NAWR ((int)(sizeof AWR/sizeof*AWR))
static void awr_row(const char *style, int dpi, int ok, RECT r){
  rk k; rv v; kctx(&k); rk_s(&k,"style",style); if(dpi) rk_i(&k,"dpi",dpi); else rk_s(&k,"dpi","ctx"); rv_reset(&v);
  if(ok){ rv_i(&v,r.left); rv_i(&v,r.top); rv_i(&v,r.right); rv_i(&v,r.bottom); } else for(int i=0;i<4;i++) rv_na(&v);
  rec_row("awr",&k,&v); }
static void t_awr(int fordpi){
  for(int i=0;i<NAWR;i++){
    RECT r={0,0,100,100}; int ok=AdjustWindowRectEx(&r,AWR[i].st,AWR[i].menu,AWR[i].ex); awr_row(AWR[i].n,0,ok,r);
    if(!fordpi) continue;
    const int *d=g_full?FORDPI_FULL:FORDPI_QUICK; int nd=g_full?10:2;
    for(int j=0;j<nd;j++){ RECT r2={0,0,100,100}; int ok2=A.AwrForDpi?A.AwrForDpi(&r2,AWR[i].st,AWR[i].menu,AWR[i].ex,d[j]):0; awr_row(AWR[i].n,d[j],ok2,r2); }
  }
}

/* ------------------------------------------------------------------ table: win (logical vs physical) */
static void t_win(void){
  HWND h=CreateWindowExW(0,L"dpiscal_parent",L"w",WS_OVERLAPPEDWINDOW,100,100,300,200,NULL,NULL,GetModuleHandleW(NULL),NULL);
  rk k; rv v; kctx(&k); rv_reset(&v);
  if(!h){ for(int j=0;j<19;j++) rv_na(&v); rec_row("win",&k,&v); return; }
  rv_i(&v,1);
  if(A.GetDpiForWindow) rv_i(&v,A.GetDpiForWindow(h)); else rv_na(&v);
  if(A.GetWinCtx && A.AwFromCtx) rv_i(&v,A.AwFromCtx(A.GetWinCtx(h))); else rv_na(&v);
  RECT wr,cr; GetWindowRect(h,&wr); GetClientRect(h,&cr);
  rv_i(&v,wr.left); rv_i(&v,wr.top); rv_i(&v,wr.right-wr.left); rv_i(&v,wr.bottom-wr.top); rv_i(&v,cr.right); rv_i(&v,cr.bottom);
  /* the same window seen from a per-monitor-v2 thread = physical pixels */
  DCTX sv=A.SetThreadCtx?A.SetThreadCtx(CTX(-4)):NULL;
  if(sv){ RECT pr,pc; GetWindowRect(h,&pr); GetClientRect(h,&pc);
    rv_i(&v,pr.left); rv_i(&v,pr.top); rv_i(&v,pr.right-pr.left); rv_i(&v,pr.bottom-pr.top); rv_i(&v,pc.right); rv_i(&v,pc.bottom);
    A.SetThreadCtx(sv); }
  else for(int i=0;i<6;i++) rv_na(&v);
  POINT p={200,150}; if(A.LtoP && A.LtoP(h,&p)){ rv_i(&v,p.x); rv_i(&v,p.y);} else { rv_na(&v); rv_na(&v); }
  POINT q={200,150}; if(A.PtoL && A.PtoL(h,&q)){ rv_i(&v,q.x); rv_i(&v,q.y);} else { rv_na(&v); rv_na(&v); }
  rec_row("win",&k,&v);
  DestroyWindow(h);
}

/* ------------------------------------------------------------------ table: cap (screen capture of a drawn window) */
static LRESULT CALLBACK capproc(HWND h, UINT m, WPARAM w, LPARAM l){
  if(m==WM_PAINT){ PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT r; GetClientRect(h,&r);
    FillRect(dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH)); FrameRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));
    HFONT o=(HFONT)SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT)); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(0,0,0));
    TextOutW(dc,3,2,L"Ag Hx",5); SelectObject(dc,o); EndPaint(h,&ps); return 0; }
  return DefWindowProcW(h,m,w,l); }
static void t_cap(void){
  const int LW=48, LH=18;  /* logical client size in the creating context */
  HWND h=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"dpiscal_cap",L"",WS_POPUP,40,40,LW,LH,NULL,NULL,GetModuleHandleW(NULL),NULL);
  rk k; kctx(&k);
  if(!h){ rec_note("cap: window failed ctx=%s gle=%lu",g_ctx,GetLastError()); return; }
  ShowWindow(h,SW_SHOWNOACTIVATE); SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
  UpdateWindow(h); pump(250); if(A.DwmFlush) A.DwmFlush(); Sleep(120); pump(30);
  DCTX sv=A.SetThreadCtx?A.SetThreadCtx(CTX(-4)):NULL;   /* read the screen in physical pixels */
  RECT pr; GetWindowRect(h,&pr); int w=pr.right-pr.left, ht=pr.bottom-pr.top;
  rv v; rv_reset(&v); rv_i(&v,pr.left); rv_i(&v,pr.top); rv_i(&v,w); rv_i(&v,ht); rv_i(&v,sv!=NULL);
  if(w>0 && ht>0 && w<=256 && ht<=128){
    rec_dib d; if(rec_dib_create(&d,w,ht)){
      HDC sdc=GetDC(NULL); BitBlt(d.dc,0,0,w,ht,sdc,pr.left,pr.top,SRCCOPY|CAPTUREBLT); ReleaseDC(NULL,sdc); GdiFlush();
      static uint8_t g[256*128]; size_t n=rec_gray_from_dib(d.bits,w,ht,4,g,sizeof g);
      int ink=0; for(int i=0;i<w*ht;i++){ uint32_t px=d.bits[i]; int gr=(((px>>16)&255)*77+((px>>8)&255)*150+(px&255)*29)>>8; if(gr<128) ink++; }
      rv_i(&v,ink); rec_row("cap",&k,&v);
      rec_bitmap("cap_img",&k,w,ht,4,g,n);
      rec_dib_free(&d);
    } else { rv_na(&v); rec_row("cap",&k,&v); }
  } else { rv_na(&v); rec_row("cap",&k,&v); }
  if(sv) A.SetThreadCtx(sv);
  DestroyWindow(h); pump(20);
}

/* ------------------------------------------------------------------ API variants (children) */
static void apply_api(const char *m){
  g_api=m; g_api_avail=1; SetLastError(0);
  if(!strcmp(m,"spda")){ if(A.SetProcAware) g_api_ret=A.SetProcAware(); else g_api_avail=0; }
  else if(!strncmp(m,"shc",3)){ if(A.ShcSetAw) g_api_ret=(long long)A.ShcSetAw(atoi(m+3)); else g_api_avail=0; }
  else if(!strncmp(m,"pctx",4)){ int c=atoi(m+4); if(A.SetProcCtx) g_api_ret=A.SetProcCtx(CTX(-c)); else g_api_avail=0; }
  else g_api_avail=0;
  g_api_err=GetLastError();
}
static const char *API_MODES[]={"spda","shc0","shc1","shc2","pctx1","pctx2","pctx3","pctx4","pctx5"};
static void run_children(const char *tag, const char *out){
  char self[MAX_PATH]; GetModuleFileNameA(NULL,self,sizeof self);
  HANDLE ho=GetStdHandle(STD_OUTPUT_HANDLE), he=GetStdHandle(STD_ERROR_HANDLE), hoi=NULL, hei=NULL;
  DuplicateHandle(GetCurrentProcess(),ho,GetCurrentProcess(),&hoi,0,TRUE,DUPLICATE_SAME_ACCESS);
  if(he) DuplicateHandle(GetCurrentProcess(),he,GetCurrentProcess(),&hei,0,TRUE,DUPLICATE_SAME_ACCESS);
  for(unsigned i=0;i<sizeof API_MODES/sizeof*API_MODES;i++){
    char cl[2048]; int n=snprintf(cl,sizeof cl,"\"%s\" --api %s --variant api-%s %s",self,API_MODES[i],API_MODES[i],g_cap?"":"--no-cap");
    if(tag) n+=snprintf(cl+n,sizeof cl-n," --tag %s",tag);
    if(out) n+=snprintf(cl+n,sizeof cl-n," --out \"%s.api-%s\"",out,API_MODES[i]);
    STARTUPINFOA si; memset(&si,0,sizeof si); si.cb=sizeof si; si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdInput=GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput=hoi?hoi:ho; si.hStdError=hei?hei:he;
    PROCESS_INFORMATION pi;
    if(CreateProcessA(NULL,cl,NULL,NULL,TRUE,0,NULL,NULL,&si,&pi)){
      /* pump while waiting: the child's USER calls may send messages to this process's windows/queue */
      DWORD t0=GetTickCount(), r; MSG m;
      while((r=MsgWaitForMultipleObjects(1,&pi.hProcess,FALSE,500,QS_ALLINPUT))!=WAIT_OBJECT_0 && GetTickCount()-t0<120000)
        while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); }
      if(r!=WAIT_OBJECT_0){ TerminateProcess(pi.hProcess,9); fprintf(stderr,"child %s timeout\n",API_MODES[i]); }
      CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    } else fprintf(stderr,"child %s CreateProcess gle=%lu\n",API_MODES[i],GetLastError());
  }
  if(hoi) CloseHandle(hoi);
  if(hei) CloseHandle(hei);
}
static int exe_is_base(void){
  char p[MAX_PATH]; GetModuleFileNameA(NULL,p,sizeof p); char *b=strrchr(p,'\\'); b=b?b+1:p; char *s=strrchr(b,'/'); if(s) b=s+1;
  char *dot=strchr(b,'.'); return dot && !_stricmp(dot,".exe"); }

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv){
  load_apis();
  const char *tag=NULL, *out=NULL;
  for(int i=1;i<argc;i++){                     /* pre-scan: the API must be applied before anything touches USER */
    if(!strcmp(argv[i],"--api") && i+1<argc) apply_api(argv[i+1]);
    else if(!strcmp(argv[i],"--tag") && i+1<argc) tag=argv[i+1];
    else if(!strcmp(argv[i],"--out") && i+1<argc) out=argv[i+1]; }
  rec_init(&argc,argv,"dpi_scaling","1");
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--quick")) g_full=0; else if(!strcmp(argv[i],"--full")) g_full=1;
    else if(!strcmp(argv[i],"--no-children")) g_children=0; else if(!strcmp(argv[i],"--no-cap")) g_cap=0;
    else if(!strcmp(argv[i],"--api") && i+1<argc) i++; }
  rec_note("mode=%s api=%s",g_full?"full":"quick",g_api?g_api:"none");
  WNDCLASSW wc; memset(&wc,0,sizeof wc); wc.hInstance=GetModuleHandleW(NULL); wc.hCursor=LoadCursor(NULL,IDC_ARROW);
  wc.lpfnWndProc=parentproc; wc.lpszClassName=L"dpiscal_parent"; wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); RegisterClassW(&wc);
  wc.lpfnWndProc=capproc; wc.lpszClassName=L"dpiscal_cap"; RegisterClassW(&wc);
  INITCOMMONCONTROLSEX icc={sizeof icc, ICC_LISTVIEW_CLASSES|ICC_TREEVIEW_CLASSES|ICC_BAR_CLASSES|ICC_TAB_CLASSES|ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};
  InitCommonControlsEx(&icc);

  rec_schema("api","mode","avail ret err");
  { rk k; rv v; rk_reset(&k); rk_s(&k,"mode",g_api?g_api:"none"); rv_reset(&v); rv_i(&v,g_api_avail); rv_x(&v,(unsigned long long)g_api_ret); rv_i(&v,g_api_err); rec_row("api",&k,&v); }
  rec_schema("ctx","ctx","set_ok cur aw ctxdpi sysdpi procsysdpi proc_aware shc_aw logpx logpy horzres:px vertres:px deskhorz:px deskvert:px "
                          "dlgbu_x:px dlgbu_y:px mon_eff_x mon_eff_y mon_ang_x mon_ang_y mon_raw_x mon_raw_y mon_w:px mon_h:px work_w:px work_h:px desktop_wdpi");
  rec_schema("sm","ctx name","val:px");
  rec_schema("smdpi","name dpi","val:px");
  rec_schema("ncm","ctx src dpi","border:px scroll_w:px scroll_h:px caption_w:px caption_h:px smcaption_w:px smcaption_h:px menu_w:px menu_h:px padded:px icon_hspace:px icon_vspace:px icon_wrap");
  rec_schema("ncmf","ctx src dpi font",LF_COLS);
  rec_schema("stock","ctx obj",LF_COLS " got " TM_COLS);
  rec_schema("ptconv","ctx","logpy muldiv8:px muldiv9:px dgf_rule:px dgf_lfh:px ncm_msg_lfh:px ncm_cap_lfh:px icontitle_lfh:px");
  rec_schema("muldiv","pt dpi","pos neg negpos");
  rec_schema("tm","ctx face pt","logpy req_h:px got " TM_COLS);
  rec_schema("cw","ctx face pt","w32_20_7e:px");
  rec_schema("dlg","ctx font pt style","ok gle lfh:px lfface got " TM_COLS " bu4_x:px bu8_y:px map100_cx:px map50_cy:px client_w:px client_h:px win_w:px win_h:px wdpi");
  rec_schema("ctl","ctx font cls m","val:px");
  rec_schema("awr","ctx style dpi","l:px t:px r:px b:px");
  rec_schema("win","ctx","ok wdpi waw x:px y:px w:px h:px cw:px ch:px phys_x:px phys_y:px phys_w:px phys_h:px phys_cw:px phys_ch:px l2p_x l2p_y p2l_x p2l_y");
  rec_schema("cap","ctx","phys_x:px phys_y:px phys_w:px phys_h:px pm2_ok ink_px");

  int nctx=(g_full && !g_api)?NCTX:1;
  for(int i=0;i<nctx;i++){
    DCTX sv; int ok=ctx_enter(i,&sv);
    if(!ok){ rk k; rv v; kctx(&k); rv_reset(&v); rv_i(&v,0); for(int j=0;j<26;j++) rv_na(&v); rec_row("ctx",&k,&v); g_ctx="proc"; continue; }
    t_ctx(1); t_sm(); t_ncm_spi(); t_stock(); t_ptconv();
    t_tm(i==0 && !g_api);
    t_dlg(); t_ctl(); t_awr(i==0 && !g_api); t_win();
    if(g_full && g_cap){ int c=CTXS[i].v; if(c==0||c==-1||c==-4||c==-5) t_cap(); }
    if(i==0 && !g_api){ t_smdpi(); t_ncm_fordpi(); t_muldiv(); }   /* DPI-argument APIs: once per manifest exe */
    rec_flush();
    ctx_leave(sv);
  }
  rec_note("dpi_scaling done");
  int rc=rec_finish(0);
  if(g_full && g_children && !g_api && exe_is_base()) run_children(tag,out);
  return rc;
}
