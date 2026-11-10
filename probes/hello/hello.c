/* hello: smallest end-to-end recorder; exercises every record type (ENV S R H B X N) so that the
 * build -> payload -> Win10 VM -> COM1 -> split -> validate chain can be proven with one boot.
 * Data: GetTextMetrics + a 19-char extent for 5 UI faces at -11/-12 on a screen DC, a sha256 of
 * GetCharWidth32 over 0x20..0x7E for each, and one 8bpp greyscale bitmap of "Ag" (Segoe UI -12, ClearType
 * off: NONANTIALIASED_QUALITY and ANTIALIASED_QUALITY) rendered black-on-white into a 32bpp DIB section.
 * usage: hello.exe [recorder options, see rec.h] [--blob N]   (--blob emits N pseudo-random bytes as an X blob) */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tm_row(HDC dc, const char *face, int h){
  LOGFONTA lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=FW_NORMAL; lf.lfCharSet=DEFAULT_CHARSET;
  snprintf(lf.lfFaceName,sizeof lf.lfFaceName,"%s",face);
  HFONT f=CreateFontIndirectA(&lf), o=(HFONT)SelectObject(dc,f);
  TEXTMETRICA tm; memset(&tm,0,sizeof tm); char got[LF_FACESIZE]="";
  int okf=GetTextFaceA(dc,sizeof got,got)>0, okt=GetTextMetricsA(dc,&tm);
  SIZE sz={0,0}; int oke=GetTextExtentPoint32A(dc,"The quick brown fox",19,&sz);
  rk k; rk_reset(&k); rk_s(&k,"face",face); rk_i(&k,"req_h",h);
  rv v; rv_reset(&v);
  if(okf) rv_s(&v,got); else rv_na(&v);
  if(okt){ rv_i(&v,tm.tmHeight); rv_i(&v,tm.tmAscent); rv_i(&v,tm.tmDescent); rv_i(&v,tm.tmInternalLeading); rv_i(&v,tm.tmExternalLeading);
    rv_i(&v,tm.tmAveCharWidth); rv_i(&v,tm.tmMaxCharWidth); rv_i(&v,tm.tmWeight); rv_x(&v,tm.tmPitchAndFamily); rv_i(&v,tm.tmCharSet); }
  else for(int i=0;i<10;i++) rv_na(&v);
  if(oke){ rv_i(&v,sz.cx); rv_i(&v,sz.cy); } else { rv_na(&v); rv_na(&v); }
  rec_row("tm",&k,&v);
  INT w[95]; if(GetCharWidth32A(dc,0x20,0x7e,w)) rec_hash("cw_ascii",&k,w,sizeof w);
  SelectObject(dc,o); DeleteObject(f);
}

static void glyph_bitmap(int quality, const char *qname){
  rec_dib d; if(!rec_dib_create(&d,24,18)) { rec_note("dib create failed"); return; }
  LOGFONTA lf; memset(&lf,0,sizeof lf); lf.lfHeight=-12; lf.lfWeight=FW_NORMAL; lf.lfCharSet=DEFAULT_CHARSET; lf.lfQuality=(BYTE)quality;
  strcpy(lf.lfFaceName,"Segoe UI");
  HFONT f=CreateFontIndirectA(&lf), o=(HFONT)SelectObject(d.dc,f);
  SetBkMode(d.dc,TRANSPARENT); SetTextColor(d.dc,RGB(0,0,0)); TextOutA(d.dc,2,1,"Ag",2); GdiFlush();
  uint8_t g[24*18]; size_t n=rec_gray_from_dib(d.bits,24,18,8,g,sizeof g);
  rk k; rk_reset(&k); rk_s(&k,"face","Segoe UI"); rk_i(&k,"req_h",-12); rk_s(&k,"text","Ag"); rk_s(&k,"quality",qname);
  rec_bitmap("glyph",&k,24,18,8,g,n);
  uint8_t g4[12*18]; n=rec_gray_from_dib(d.bits,24,18,4,g4,sizeof g4);
  rec_bitmap("glyph4",&k,24,18,4,g4,n);
  SelectObject(d.dc,o); DeleteObject(f); rec_dib_free(&d);
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"hello","1");
  int blob=0; for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--blob") && i+1<argc) blob=atoi(argv[++i]);
  HDC dc=GetDC(NULL);
  rec_schema("tm","face req_h","got h:px a:px d:px il:px el:px ave:px max:px weight pf cs ext19_cx:px ext19_cy:px");
  static const char *faces[]={"Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma","Arial","Courier New","System","NoSuchFace"};
  for(unsigned i=0;i<sizeof faces/sizeof*faces;i++){ tm_row(dc,faces[i],-11); tm_row(dc,faces[i],-12); }
  ReleaseDC(NULL,dc);
  glyph_bitmap(NONANTIALIASED_QUALITY,"nonaa");
  glyph_bitmap(ANTIALIASED_QUALITY,"aa");
  glyph_bitmap(CLEARTYPE_QUALITY,"cleartype");
  if(blob>0){ uint8_t *b=(uint8_t*)malloc(blob); uint32_t s=12345; for(int i=0;i<blob;i++){ s=s*1103515245u+12345u; b[i]=(uint8_t)(s>>16); } rec_blob("lcg12345",b,blob); free(b); }
  rec_note("hello done");
  return rec_finish(0);
}
