/* tm_hello: tiny GDI metrics probe used to evaluate Wine vs Win10 and to smoke-test the VM serial path.
 * usage: tm_hello.exe            -> stdout
 *        tm_hello.exe --com1     -> also/only writes via CreateFile("\\\\.\\COM1")
 * Output: line-oriented "TM face=.. req_h=.. dpi=.. got=.. h= a= d= il= el= ave= max= w= ow= ..." */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static HANDLE out; static int viacom;
static void put(const char *s){ DWORD n; WriteFile(out, s, (DWORD)strlen(s), &n, NULL); }
static void probe(HDC dc, const char *face, int h, int dpi){
  char b[640]; LOGFONTA lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=FW_NORMAL;
  lf.lfCharSet=DEFAULT_CHARSET; strncpy(lf.lfFaceName,face,31);
  HFONT f=CreateFontIndirectA(&lf); HFONT o=SelectObject(dc,f);
  TEXTMETRICA tm; char got[64]="?"; GetTextFaceA(dc,64,got); GetTextMetricsA(dc,&tm);
  SIZE sz={0,0}; GetTextExtentPoint32A(dc,"The quick brown fox",19,&sz);
  int w[3]={0,0,0}; GetCharWidth32A(dc,'i','i',&w[0]); GetCharWidth32A(dc,'W','W',&w[1]); GetCharWidth32A(dc,'0','0',&w[2]);
  snprintf(b,sizeof b,"TM face=%s req_h=%d dpi=%d got=%s h=%ld a=%ld d=%ld il=%ld el=%ld ave=%ld max=%ld ow=%ld pf=0x%x ext19=%ldx%ld w_i=%d w_W=%d w_0=%d\n",
    face,h,dpi,got,tm.tmHeight,tm.tmAscent,tm.tmDescent,tm.tmInternalLeading,tm.tmExternalLeading,tm.tmAveCharWidth,tm.tmMaxCharWidth,tm.tmOverhang,tm.tmPitchAndFamily,sz.cx,sz.cy,w[0],w[1],w[2]);
  put(b); SelectObject(dc,o); DeleteObject(f);
}
static int CALLBACK en(const LOGFONTA*lf,const TEXTMETRICA*tm,DWORD t,LPARAM p){ (*(int*)p)++; return 1; }
int main(int argc,char**argv){
  viacom = argc>1 && !strcmp(argv[1],"--com1");
  out = viacom ? CreateFileA("\\\\.\\COM1",GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL) : GetStdHandle(STD_OUTPUT_HANDLE);
  if(out==INVALID_HANDLE_VALUE){ return 3; }
  HDC dc=CreateCompatibleDC(NULL); int dpi=GetDeviceCaps(dc,LOGPIXELSY);
  char b[256]; OSVERSIONINFOA v; (void)v;
  snprintf(b,sizeof b,"HELLO dpiY=%d dpiX=%d bpp=%d horzres=%d\n",dpi,GetDeviceCaps(dc,LOGPIXELSX),GetDeviceCaps(dc,BITSPIXEL),GetDeviceCaps(dc,HORZRES)); put(b);
  static const char*faces[]={"Segoe UI","Tahoma","MS Shell Dlg","MS Shell Dlg 2","MS Sans Serif","Arial","Courier New","System","NoSuchFace"};
  static const int hs[]={-11,-12,-16};
  for(unsigned i=0;i<sizeof faces/sizeof*faces;i++) for(unsigned j=0;j<3;j++) probe(dc,faces[i],hs[j],dpi);
  int n=0; LOGFONTA q; memset(&q,0,sizeof q); q.lfCharSet=DEFAULT_CHARSET; EnumFontFamiliesExA(dc,&q,en,(LPARAM)&n,0);
  snprintf(b,sizeof b,"ENUMCOUNT %d\nDONE\n",n); put(b);
  if(viacom){ CloseHandle(out); }
  return 0; }
