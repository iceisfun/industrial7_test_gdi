#include "toe_common.h"
/* DPI scale getters exported by gdi32 (undocumented): observation only (NOTE), plus the documented user32 DPI
 * queries they relate to. The test executable carries no manifest, so it is DPI unaware. */

UDECL(UINT,GetDCDpiScaleValue,(HDC));
UDECL(UINT,GetBitmapDpiScaleValue,(HBITMAP));
UDECL(BOOL,GetCurrentDpiInfo,(HMONITOR,void*));

TEST(dpi_scale_getters_observe){
  REQUIRE(UN(GetDCDpiScaleValue)); REQUIRE(UN(GetBitmapDpiScaleValue)); REQUIRE(UN(GetCurrentDpiInfo));
  REQUIRE(U(GetDpiForSystem)); REQUIRE(U(GetThreadDpiAwarenessContext)); REQUIRE(U(GetAwarenessFromDpiAwarenessContext));
  int aw=(int)U(GetAwarenessFromDpiAwarenessContext)(U(GetThreadDpiAwarenessContext)());
  UINT sysdpi=U(GetDpiForSystem)();
  HDC scr=U(GetDC)(NULL); int lp=scr?U(GetDeviceCaps)(scr,LOGPIXELSX):-1;
  if(aw!=DPI_AWARENESS_UNAWARE) t_note("process is not DPI unaware (awareness %d)",aw);
  t_note("thread awareness %d, GetDpiForSystem %u, screen LOGPIXELSX %d",aw,sysdpi,lp);
  /* documented: an unaware process always sees 96 */
  if(aw==DPI_AWARENESS_UNAWARE){ CHECKF(sysdpi==96,"unaware GetDpiForSystem %u",sysdpi); CHECKF(lp==96,"unaware screen LOGPIXELSX %d",lp); }
  CHECKF(scr&&(UINT)lp==sysdpi,"screen LOGPIXELSX %d != GetDpiForSystem %u",lp,sysdpi);
  if(scr){ UINT v=UN(GetDCDpiScaleValue)(scr); t_note("GetDCDpiScaleValue(screen DC) = %u",v); }
  HDC mem=U(CreateCompatibleDC)(NULL); UINT mv=UN(GetDCDpiScaleValue)(mem); t_note("GetDCDpiScaleValue(memory DC) = %u",mv);
  E e; if(e_open(&e,32,32,DEFAULT_QUALITY,18)){ UINT dv=UN(GetDCDpiScaleValue)(e.dc); t_note("GetDCDpiScaleValue(DIB memory DC) = %u",dv);
    UINT bv=UN(GetBitmapDpiScaleValue)(e.bmp); t_note("GetBitmapDpiScaleValue(DIB section) = %u",bv); }
  e_close(&e);
  HBITMAP cb=U(CreateCompatibleBitmap)(scr?scr:mem,8,8); if(cb){ t_note("GetBitmapDpiScaleValue(compatible bitmap) = %u",UN(GetBitmapDpiScaleValue)(cb)); U(DeleteObject)(cb); }
  t_note("GetDCDpiScaleValue(NULL) = %u",UN(GetDCDpiScaleValue)(NULL));
  t_note("GetBitmapDpiScaleValue(NULL) = %u",UN(GetBitmapDpiScaleValue)(NULL));
  unsigned char buf[0x60]; memset(buf,0xCC,sizeof buf);
  HMONITOR mon=U(MonitorFromPoint)((POINT){0,0},MONITOR_DEFAULTTOPRIMARY);
  BOOL r=UN(GetCurrentDpiInfo)(mon,buf); DWORD *d=(DWORD*)buf;
  t_note("GetCurrentDpiInfo(primary) = %d: %u %u %u %u %u %u %u %u",r,d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7]);
  /* the callers of GetCurrentDpiInfo use the dword at offset 8 as the monitor scale in percent */
  if(r&&aw==DPI_AWARENESS_UNAWARE&&lp==96) t_note("scale percent field %u",d[2]);
  U(DeleteDC)(mem); if(scr) U(ReleaseDC)(NULL,scr);
}
