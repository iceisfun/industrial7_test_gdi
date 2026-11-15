#include "toe_common.h"
/* TextOutA / ExtTextOutA / PolyTextOutA / PolyTextOutW / SetTextAlign effects / DrawTextA/W on a 32bpp DIB section
 * (and a 1bpp one). The font is Arial 18 px with NONANTIALIASED_QUALITY, so every pixel is exactly the background
 * or the text colour (documented for NONANTIALIASED_QUALITY: glyphs are never antialiased). */

#define WHITE RGB(255,255,255)
#define BLACK RGB(0,0,0)
#define RED   RGB(255,0,0)
#define BLUE  RGB(0,0,255)
#define W_ 260
#define H_ 120

static void setup(E *e, COLORREF fg, COLORREF bk, int mode){
  e_clear(e,WHITE);
  U(SetTextColor)(e->dc,fg); U(SetBkColor)(e->dc,bk); U(SetBkMode)(e->dc,mode); U(SetTextAlign)(e->dc,0);
  U(MoveToEx)(e->dc,0,0,NULL);
}
static SIZE ext(E *e, const char *s){ SIZE z; U(GetTextExtentPoint32A)(e->dc,s,(int)strlen(s),&z); return z; }
static int only_two(E *e, const RECT *r, DWORD a, DWORD b){ int bad=0; e_sync(e); for(int y=r->top;y<r->bottom;y++) for(int x=r->left;x<r->right;x++){ DWORD p=e_get(e,x,y); if(p!=a&&p!=b) bad++; } return bad; }
/* ink pixels (!= WHITE) outside rectangle r */
static int outside(E *e, const RECT *r){ int n=0; e_sync(e); for(int y=0;y<e->h;y++) for(int x=0;x<e->w;x++) if(!(x>=r->left&&x<r->right&&y>=r->top&&y<r->bottom)&&e_get(e,x,y)!=c2p(WHITE)) n++; return n; }

TEST(textouta_ink_within_extent){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  static const char *ss[]={"Hello","HIT","Hamburg","MWM","Test String 123"};
  for(unsigned i=0;i<sizeof ss/sizeof*ss;i++){
    const char *s=ss[i]; int n=(int)strlen(s);
    setup(&e,BLACK,WHITE,TRANSPARENT);
    SIZE z=ext(&e,s);
    CHECK(U(TextOutA)(e.dc,20,20,s,n));
    RECT bb; int ink=e_bbox(&e,WHITE,NULL,&bb);
    CHECKF(ink>0,"%s: nothing drawn",s);
    RECT box={20,20,20+z.cx,20+z.cy};
    CHECKF(only_two(&e,&(RECT){0,0,W_,H_},c2p(WHITE),c2p(BLACK))==0,"%s: pixels other than background/text colour",s);
    CHECKF(bb.top>=20&&bb.bottom<20+z.cy,"%s: ink rows %ld..%ld outside extent rows 20..%ld",s,bb.top,bb.bottom,20+z.cy-1);
    ABC a0,a1; U(GetCharABCWidthsW)(e.dc,s[0],s[0],&a0); U(GetCharABCWidthsW)(e.dc,s[n-1],s[n-1],&a1);
    int minl=20+a0.abcA, maxr=20+(int)z.cx-a1.abcC;   /* ink spans [minl, maxr) up to rounding */
    CHECKF(labs(bb.left-minl)<=1&&labs((bb.right+1)-maxr)<=1,"%s: ink cols %ld..%ld, ABC predicts %d..%d (A %d C %d cx %ld)",s,bb.left,bb.right,minl,maxr-1,a0.abcA,a1.abcC,z.cx);
    CHECKF(bb.right+1-bb.left>=z.cx-a0.abcA-a1.abcC-1-2,"%s: ink width %ld too small for advances",s,bb.right+1-bb.left);
    /* the same drawing with the wide API and with ExtTextOutA(options 0) is pixel identical */
    DWORD *snap=e_snap(&e);
    setup(&e,BLACK,WHITE,TRANSPARENT); WCHAR w[64]; widen(s,w,64); CHECK(U(TextOutW)(e.dc,20,20,w,n));
    CHECKF(e_same(&e,snap),"%s: TextOutW differs from TextOutA (%d px)",s,e_diff(&e,snap));
    setup(&e,BLACK,WHITE,TRANSPARENT); CHECK(U(ExtTextOutA)(e.dc,20,20,0,NULL,s,n,NULL));
    CHECKF(e_same(&e,snap),"%s: ExtTextOutA differs from TextOutA (%d px)",s,e_diff(&e,snap));
    free(snap);
  }
  /* an empty string draws nothing and does not fail */
  setup(&e,BLACK,WHITE,TRANSPARENT); BOOL r=U(TextOutA)(e.dc,20,20,"",0);
  RECT bb; EQV(e_bbox(&e,WHITE,NULL,&bb),0); t_note("TextOutA(\"\",0) returned %d",r);
  e_close(&e);
}

TEST(textouta_text_colour){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  setup(&e,RGB(0,0,255),WHITE,TRANSPARENT); U(TextOutA)(e.dc,20,20,"Hello",5);
  RECT all={0,0,W_,H_}; CHECK(e_count(&e,c2p(RGB(0,0,255)),&all)>20);
  CHECKF(only_two(&e,&all,c2p(WHITE),c2p(RGB(0,0,255)))==0,"stray colours");
  e_close(&e);
}

TEST(textouta_bkmode){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  const char *s="Hello"; SIZE z=ext(&e,s); RECT box={20,20,20+z.cx,20+z.cy}; RECT all={0,0,W_,H_};
  /* OPAQUE: the background colour fills the whole extent rectangle, nothing outside it changes */
  setup(&e,BLUE,RED,OPAQUE); U(TextOutA)(e.dc,20,20,s,5);
  CHECKF(only_two(&e,&box,c2p(RED),c2p(BLUE))==0,"OPAQUE: extent box holds other colours");
  int bk=e_count(&e,c2p(RED),&box), ink=e_count(&e,c2p(BLUE),&box);
  CHECKF(ink>0&&bk>0&&bk+ink==z.cx*z.cy,"OPAQUE: bk %d ink %d box %ld",bk,ink,z.cx*z.cy);
  CHECKF(e_get(&e,20,20+z.cy-1)==c2p(RED)&&e_get(&e,20+z.cx-1,20+z.cy-1)==c2p(RED),"OPAQUE: bottom corners not bk colour");
  CHECKF(outside(&e,&box)==0,"OPAQUE: %d pixels changed outside the extent box",outside(&e,&box));
  /* TRANSPARENT: the background colour is never used */
  setup(&e,BLUE,RED,TRANSPARENT); U(TextOutA)(e.dc,20,20,s,5);
  CHECKF(e_count(&e,c2p(RED),&all)==0,"TRANSPARENT: bk colour appeared");
  CHECKF(only_two(&e,&all,c2p(WHITE),c2p(BLUE))==0,"TRANSPARENT: stray colours");
  CHECKF(e_get(&e,20,20+z.cy-1)==c2p(WHITE),"TRANSPARENT: pixel below the baseline changed");
  /* the ink is the same in both modes */
  DWORD *t=e_snap(&e); setup(&e,BLUE,RED,OPAQUE); U(TextOutA)(e.dc,20,20,s,5);
  int diff=0; for(int y=0;y<H_;y++) for(int x=0;x<W_;x++){ DWORD b=e_get(&e,x,y); if((t[y*W_+x]&0xFFFFFF)==c2p(BLUE)&&b!=c2p(BLUE)) diff++; if((t[y*W_+x]&0xFFFFFF)!=c2p(BLUE)&&b==c2p(BLUE)) diff++; }
  CHECKF(diff==0,"glyph shapes differ between OPAQUE and TRANSPARENT (%d px)",diff);
  free(t);
  e_close(&e);
}

/* ---- alignment */
static DWORD *draw_at(E *e, const char *s, UINT align, int x, int y){
  setup(e,BLACK,WHITE,TRANSPARENT); U(SetTextAlign)(e->dc,align); U(TextOutA)(e->dc,x,y,s,(int)strlen(s));
  return e_snap(e);
}
TEST(settextalign_horizontal_vertical_shift){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  static const char *ss[]={"Hello","Hamburg","Ill","WWWW W"};
  for(unsigned i=0;i<sizeof ss/sizeof*ss;i++){
    const char *s=ss[i]; SIZE z=ext(&e,s); int asc=e.tm.tmAscent;
    DWORD *ref=draw_at(&e,s,TA_LEFT|TA_TOP,60,30);
    DWORD *r=draw_at(&e,s,TA_RIGHT|TA_TOP,60+z.cx,30);
    int d=e_diff(&e,ref); CHECKF(d==0,"%s TA_RIGHT: %d px differ from TA_LEFT at x-cx",s,d); free(r);
    /* centre: the string starts at x - ceil(cx/2): exactly x - cx/2 for even cx, x - (cx/2 + 1) for odd cx (observed, stable) */
    DWORD *c=draw_at(&e,s,TA_CENTER|TA_TOP,60+z.cx/2,30); int dc1=e_diff(&e,ref);
    if(z.cx%2==0) CHECKF(dc1==0,"%s TA_CENTER (cx %ld even): %d px differ",s,z.cx,dc1);
    else { DWORD *ref2=draw_at(&e,s,TA_LEFT|TA_TOP,60-1,30); free(c); c=draw_at(&e,s,TA_CENTER|TA_TOP,60+z.cx/2,30);
           int dc2=e_diff(&e,ref2); CHECKF(dc2==0,"%s TA_CENTER (cx %ld odd): start is not x-ceil(cx/2) (%d px differ; x-floor(cx/2) differs by %d px)",s,z.cx,dc2,dc1);
           t_note("%s TA_CENTER cx %ld odd: starts at x-cx/2=%s x-(cx/2+1)=%s",s,z.cx,dc1==0?"yes":"no",dc2==0?"yes":"no"); free(ref2); }
    free(c);
    DWORD *b=draw_at(&e,s,TA_LEFT|TA_BOTTOM,60,30+z.cy); d=e_diff(&e,ref); CHECKF(d==0,"%s TA_BOTTOM at y+cy: %d px differ",s,d); free(b);
    DWORD *bl=draw_at(&e,s,TA_LEFT|TA_BASELINE,60,30+asc); d=e_diff(&e,ref); CHECKF(d==0,"%s TA_BASELINE at y+ascent: %d px differ",s,d); free(bl);
    DWORD *rb=draw_at(&e,s,TA_RIGHT|TA_BOTTOM,60+z.cx,30+z.cy); d=e_diff(&e,ref); CHECKF(d==0,"%s TA_RIGHT|TA_BOTTOM: %d px differ",s,d); free(rb);
    free(ref);
  }
  e_close(&e);
}

TEST(settextalign_updatecp){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  const char *s="Hello"; SIZE z=ext(&e,s);
  DWORD *ref=draw_at(&e,s,TA_LEFT|TA_TOP,40,30);
  /* TA_UPDATECP: x,y arguments are ignored, the current position is the reference point and is advanced */
  setup(&e,BLACK,WHITE,TRANSPARENT);
  EQV(U(SetTextAlign)(e.dc,TA_UPDATECP),0);
  U(MoveToEx)(e.dc,40,30,NULL);
  CHECK(U(TextOutA)(e.dc,500,500,s,5));
  POINT p={-1,-1}; CHECK(U(GetCurrentPositionEx)(e.dc,&p));
  CHECKF(p.x==40+z.cx&&p.y==30,"current position after TextOutA: %ld,%ld want %ld,30",p.x,p.y,40+z.cx);
  CHECKF(e_same(&e,ref),"UPDATECP drawing differs from TA_LEFT at the current position (%d px)",e_diff(&e,ref));
  /* the second call continues where the first stopped */
  CHECK(U(TextOutA)(e.dc,0,0,s,5)); U(GetCurrentPositionEx)(e.dc,&p);
  CHECKF(p.x==40+2*z.cx,"after two calls current x %ld want %ld",p.x,40+2*z.cx);
  RECT bb; e_bbox(&e,WHITE,NULL,&bb); CHECKF(bb.right+1<=40+2*z.cx&&bb.right+1>40+z.cx,"second string ink ends at %ld",bb.right+1);
  /* without TA_UPDATECP the current position is not touched */
  U(SetTextAlign)(e.dc,0); U(MoveToEx)(e.dc,7,8,NULL); U(TextOutA)(e.dc,40,30,s,5); U(GetCurrentPositionEx)(e.dc,&p);
  CHECKF(p.x==7&&p.y==8,"TextOutA without TA_UPDATECP moved the position to %ld,%ld",p.x,p.y);
  /* TA_RIGHT|TA_UPDATECP: string ends at the current position */
  setup(&e,BLACK,WHITE,TRANSPARENT); U(SetTextAlign)(e.dc,TA_RIGHT|TA_UPDATECP); U(MoveToEx)(e.dc,40+z.cx,30,NULL);
  U(TextOutA)(e.dc,0,0,s,5); U(GetCurrentPositionEx)(e.dc,&p);
  CHECKF(e_same(&e,ref),"TA_RIGHT|TA_UPDATECP drawing differs (%d px)",e_diff(&e,ref));
  t_note("TA_RIGHT|TA_UPDATECP: position before x=%ld after %ld,%ld (cx %ld)",40+z.cx,p.x,p.y,z.cx);
  /* ExtTextOutA with lpDx and TA_UPDATECP */
  setup(&e,BLACK,WHITE,TRANSPARENT); U(SetTextAlign)(e.dc,TA_UPDATECP); U(MoveToEx)(e.dc,40,30,NULL);
  INT dx[5]={20,20,20,20,20}; CHECK(U(ExtTextOutA)(e.dc,0,0,0,NULL,s,5,dx)); U(GetCurrentPositionEx)(e.dc,&p);
  t_note("ExtTextOutA TA_UPDATECP lpDx sum 100: position %ld,%ld",p.x,p.y);
  CHECKF(p.x==140&&p.y==30,"ExtTextOutA with lpDx and TA_UPDATECP: position %ld,%ld want 140,30",p.x,p.y);
  free(ref); e_close(&e);
}

/* ---- ExtTextOutA */
TEST(exttextouta_opaque_and_clipped){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  const char *s="Hello World"; int n=11; SIZE z=ext(&e,s); RECT all={0,0,W_,H_};
  RECT rc={30,10,200,70};
  /* ETO_OPAQUE fills lprc with the background colour, even when the mode is TRANSPARENT */
  setup(&e,BLUE,RED,TRANSPARENT);
  CHECK(U(ExtTextOutA)(e.dc,40,25,ETO_OPAQUE,&rc,s,n,NULL));
  CHECKF(only_two(&e,&rc,c2p(RED),c2p(BLUE))==0,"opaque rect holds other colours");
  CHECKF(e_get(&e,30,10)==c2p(RED)&&e_get(&e,199,69)==c2p(RED)&&e_get(&e,30,69)==c2p(RED)&&e_get(&e,199,10)==c2p(RED),"rect corners not bk colour");
  CHECKF(e_count(&e,c2p(BLUE),&rc)>20,"no text ink inside the rect");
  CHECKF(outside(&e,&rc)==0,"%d pixels changed outside the rect",outside(&e,&rc));
  /* the text is drawn on top of the filled rect, in the same place as plain TextOut */
  DWORD *t0=NULL; { setup(&e,BLUE,RED,TRANSPARENT); U(TextOutA)(e.dc,40,25,s,n); t0=e_snap(&e); }
  setup(&e,BLUE,RED,TRANSPARENT); U(ExtTextOutA)(e.dc,40,25,ETO_OPAQUE,&rc,s,n,NULL);
  int bad=0; for(int y=10;y<70;y++) for(int x=30;x<200;x++){ int ink0=(t0[y*W_+x]&0xFFFFFF)==c2p(BLUE); int ink1=e_get(&e,x,y)==c2p(BLUE); if(ink0!=ink1) bad++; }
  CHECKF(bad==0,"ink inside opaque rect differs from TextOut (%d px)",bad);
  /* no string: only the fill */
  setup(&e,BLUE,RED,OPAQUE); CHECK(U(ExtTextOutA)(e.dc,0,0,ETO_OPAQUE,&rc,NULL,0,NULL));
  CHECKF(e_count(&e,c2p(RED),&rc)==(rc.right-rc.left)*(rc.bottom-rc.top)&&outside(&e,&rc)==0,"rect-only fill wrong");
  /* ETO_CLIPPED: nothing outside lprc changes, and what is inside equals the unclipped drawing */
  RECT cl={60,22,100,44};
  setup(&e,BLUE,RED,TRANSPARENT); CHECK(U(ExtTextOutA)(e.dc,40,25,ETO_CLIPPED,&cl,s,n,NULL));
  CHECKF(outside(&e,&cl)==0,"ETO_CLIPPED: %d pixels outside the clip rect",outside(&e,&cl));
  bad=0; int inks=0; for(int y=cl.top;y<cl.bottom;y++) for(int x=cl.left;x<cl.right;x++){ if(e_get(&e,x,y)!=(t0[y*W_+x]&0xFFFFFF)) bad++; if(e_get(&e,x,y)==c2p(BLUE)) inks++; }
  CHECKF(bad==0&&inks>5,"ETO_CLIPPED: inside differs from unclipped in %d px, ink %d",bad,inks);
  /* without ETO_CLIPPED the same rect does not clip */
  setup(&e,BLUE,RED,TRANSPARENT); U(ExtTextOutA)(e.dc,40,25,0,&cl,s,n,NULL);
  CHECKF(e_same(&e,t0),"rect without ETO_CLIPPED changed the drawing (%d px)",e_diff(&e,t0));
  /* CLIPPED|OPAQUE: the fill is the rect, the text is clipped to it */
  RECT cr={60,22,100,44};
  setup(&e,BLUE,RED,OPAQUE); U(ExtTextOutA)(e.dc,40,25,ETO_CLIPPED|ETO_OPAQUE,&cr,s,n,NULL);
  CHECKF(outside(&e,&cr)==0&&only_two(&e,&cr,c2p(RED),c2p(BLUE))==0&&e_count(&e,c2p(RED),&cr)>0,"CLIPPED|OPAQUE wrong");
  free(t0); e_close(&e);
}

TEST(exttextouta_dx_overrides_advances){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  RECT bb1,bb4,bbd;
  setup(&e,BLACK,WHITE,TRANSPARENT); U(TextOutA)(e.dc,20,20,"H",1); e_bbox(&e,WHITE,NULL,&bb1);
  INT dx[4]={30,30,30,30};
  setup(&e,BLACK,WHITE,TRANSPARENT); CHECK(U(ExtTextOutA)(e.dc,20,20,0,NULL,"HHHH",4,dx)); e_bbox(&e,WHITE,NULL,&bbd);
  setup(&e,BLACK,WHITE,TRANSPARENT); U(TextOutA)(e.dc,20,20,"HHHH",4); e_bbox(&e,WHITE,NULL,&bb4);
  SIZE zh=ext(&e,"H");
  /* with lpDx the k-th glyph starts at x + sum of previous lpDx */
  CHECKF(bbd.left==bb1.left&&bbd.right==bb1.right+3*30,"lpDx 30: ink %ld..%ld, expected %ld..%ld",bbd.left,bbd.right,bb1.left,bb1.right+90);
  CHECKF(bb4.left==bb1.left&&bb4.right==bb1.right+3*zh.cx,"default advances: ink %ld..%ld, expected right %ld",bb4.left,bb4.right,bb1.right+3*zh.cx);
  /* a smaller spacing than the glyph width overlaps */
  INT d2[4]={5,5,5,5}; setup(&e,BLACK,WHITE,TRANSPARENT); U(ExtTextOutA)(e.dc,20,20,0,NULL,"HHHH",4,d2); e_bbox(&e,WHITE,NULL,&bbd);
  CHECKF(bbd.right==bb1.right+15,"lpDx 5: ink right %ld want %ld",bbd.right,bb1.right+15);
  /* lpDx equal to the natural advances is identical to NULL */
  INT nat[4]; for(int i=0;i<4;i++){ INT w=0; U(GetCharWidth32A)(e.dc,'H','H',&w); nat[i]=w; }
  setup(&e,BLACK,WHITE,TRANSPARENT); U(TextOutA)(e.dc,20,20,"HHHH",4); DWORD *a=e_snap(&e);
  setup(&e,BLACK,WHITE,TRANSPARENT); U(ExtTextOutA)(e.dc,20,20,0,NULL,"HHHH",4,nat); CHECKF(e_same(&e,a),"natural lpDx differs from NULL (%d px)",e_diff(&e,a)); free(a);
  e_close(&e);
}

TEST(exttextoutw_glyph_index){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  WCHAR w[]=L"Hamburg 42"; int n=10; WORD g[16]; DWORD r=U(GetGlyphIndicesW)(e.dc,w,n,g,GGI_MARK_NONEXISTING_GLYPHS); CHECK(r==(DWORD)n);
  setup(&e,BLACK,WHITE,TRANSPARENT); U(ExtTextOutW)(e.dc,20,20,0,NULL,w,n,NULL); DWORD *a=e_snap(&e);
  setup(&e,BLACK,WHITE,TRANSPARENT); CHECK(U(ExtTextOutW)(e.dc,20,20,ETO_GLYPH_INDEX,NULL,(LPCWSTR)g,n,NULL));
  CHECKF(e_same(&e,a),"ETO_GLYPH_INDEX drawing differs from the string drawing (%d px)",e_diff(&e,a));
  free(a); e_close(&e);
}

/* ---- PolyTextOut */
TEST(polytextout_two_strings){
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  WCHAR w1[]=L"Hello", w2[]=L"World 7"; char a1[]="Hello", a2[]="World 7";
  setup(&e,BLACK,WHITE,TRANSPARENT); U(TextOutW)(e.dc,10,10,w1,5); U(TextOutW)(e.dc,70,60,w2,7); DWORD *ref=e_snap(&e);
  POLYTEXTW pw[2]; memset(pw,0,sizeof pw);
  pw[0].x=10; pw[0].y=10; pw[0].n=5; pw[0].lpstr=w1;
  pw[1].x=70; pw[1].y=60; pw[1].n=7; pw[1].lpstr=w2;
  setup(&e,BLACK,WHITE,TRANSPARENT);
  BOOL r=U(PolyTextOutW)(e.dc,pw,2); CHECK(r);
  CHECKF(e_same(&e,ref),"PolyTextOutW differs from two TextOutW calls (%d px)",e_diff(&e,ref));
  RECT top={0,0,W_,50}, bot={0,50,W_,H_}, r2; e_bbox(&e,WHITE,&bot,&r2); SIZE z2=ext(&e,a2);
  CHECKF(r2.top>=60&&r2.bottom<60+z2.cy&&r2.left>=69&&r2.right<70+z2.cx,"second string ink %ld,%ld..%ld,%ld not at its own x,y",r2.left,r2.top,r2.right,r2.bottom);
  CHECK(e_bbox(&e,WHITE,&top,&r2)>0);
  /* ANSI */
  POLYTEXTA pa[2]; memset(pa,0,sizeof pa);
  pa[0].x=10; pa[0].y=10; pa[0].n=5; pa[0].lpstr=a1; pa[1].x=70; pa[1].y=60; pa[1].n=7; pa[1].lpstr=a2;
  setup(&e,BLACK,WHITE,TRANSPARENT); r=U(PolyTextOutA)(e.dc,pa,2); CHECK(r);
  CHECKF(e_same(&e,ref),"PolyTextOutA differs from PolyTextOutW/TextOutW (%d px)",e_diff(&e,ref));
  /* per-entry flags: ETO_OPAQUE with rcl fills with the bk colour, ETO_CLIPPED clips */
  RECT rc1={5,5,100,35}, rc2={70,60,100,80};
  pw[0].uiFlags=ETO_OPAQUE; pw[0].rcl=rc1; pw[1].uiFlags=ETO_CLIPPED; pw[1].rcl=rc2;
  setup(&e,BLUE,RED,TRANSPARENT); CHECK(U(PolyTextOutW)(e.dc,pw,2));
  CHECKF(e_get(&e,5,5)==c2p(RED)&&e_get(&e,99,34)==c2p(RED),"entry 0 ETO_OPAQUE did not fill its rect");
  RECT b2; e_bbox(&e,WHITE,&(RECT){0,50,W_,H_},&b2);
  CHECKF(b2.left>=70&&b2.right<100&&b2.top>=60&&b2.bottom<80,"entry 1 ETO_CLIPPED: ink %ld,%ld..%ld,%ld outside %d,%d..%d,%d",b2.left,b2.top,b2.right,b2.bottom,70,60,99,79);
  /* the alignment of the DC applies */
  pw[0].uiFlags=0; pw[1].uiFlags=0; setup(&e,BLACK,WHITE,TRANSPARENT); U(SetTextAlign)(e.dc,TA_RIGHT);
  SIZE z1=ext(&e,a1); pw[0].x=10+z1.cx; pw[1].x=70+z2.cx; CHECK(U(PolyTextOutW)(e.dc,pw,2));
  CHECKF(e_same(&e,ref),"TA_RIGHT PolyTextOutW differs (%d px)",e_diff(&e,ref));
  /* a single entry */
  setup(&e,BLACK,WHITE,TRANSPARENT); U(SetTextAlign)(e.dc,0); pw[0].x=10; pw[1].x=70; CHECK(U(PolyTextOutW)(e.dc,pw,1));
  RECT b3; CHECK(e_bbox(&e,WHITE,&bot,&b3)==0);
  free(ref); e_close(&e);
}

/* ---- 1bpp DIB */
TEST(textouta_1bpp_dib){
  HDC dc=U(CreateCompatibleDC)(NULL); struct { BITMAPINFOHEADER h; RGBQUAD pal[2]; } bi; memset(&bi,0,sizeof bi);
  int w=160,h=60; bi.h.biSize=sizeof bi.h; bi.h.biWidth=w; bi.h.biHeight=-h; bi.h.biPlanes=1; bi.h.biBitCount=1; bi.h.biCompression=BI_RGB; bi.h.biClrUsed=2;
  bi.pal[0].rgbRed=bi.pal[0].rgbGreen=bi.pal[0].rgbBlue=255;   /* index 0 white */
  void *bits=NULL; HBITMAP bm=U(CreateDIBSection)(dc,(BITMAPINFO*)&bi,DIB_RGB_COLORS,&bits,NULL,0);
  if(!bm||!bits){ if(bm) U(DeleteObject)(bm); U(DeleteDC)(dc); t_skip("1bpp DIB section could not be created"); return; }
  HBITMAP ob=(HBITMAP)U(SelectObject)(dc,bm);
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=-18; lf.lfWeight=400; lf.lfCharSet=ANSI_CHARSET; lf.lfQuality=NONANTIALIASED_QUALITY; lstrcpyW(lf.lfFaceName,L"Arial");
  HFONT f=U(CreateFontIndirectW)(&lf); HFONT of=(HFONT)U(SelectObject)(dc,f);
  WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(dc,LF_FACESIZE,got);
  if(lstrcmpiW(got,L"Arial")) t_skip("Arial not realised");
  else {
    int stride=((w+31)/32)*4; memset(bits,0,(size_t)stride*h);
    U(SetBkMode)(dc,TRANSPARENT); U(SetTextColor)(dc,RGB(0,0,0));
    SIZE z; U(GetTextExtentPoint32A)(dc,"Hello",5,&z);
    CHECK(U(TextOutA)(dc,10,10,"Hello",5)); U(GdiFlush)();
    int minx=999,maxx=-1,miny=999,maxy=-1,n=0; unsigned char *b=(unsigned char*)bits;
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) if(b[y*stride+x/8]&(0x80>>(x&7))){ n++; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
    CHECKF(n>20,"1bpp: only %d inked pixels",n);
    CHECKF(minx>=10&&maxx<10+z.cx&&miny>=10&&maxy<10+z.cy,"1bpp ink %d,%d..%d,%d outside extent box %d,%d..%ld,%ld",minx,miny,maxx,maxy,10,10,10+z.cx-1,10+z.cy-1);
    /* white text on a white palette entry-0 background draws index 0 = no change */
    memset(bits,0,(size_t)stride*h); U(SetTextColor)(dc,RGB(255,255,255)); U(TextOutA)(dc,10,10,"Hello",5); U(GdiFlush)();
    int m=0; for(int i=0;i<stride*h;i++) m+=b[i]!=0; CHECKF(m==0,"1bpp: white text on white changed %d bytes",m);
  }
  U(SelectObject)(dc,of); U(DeleteObject)(f); U(SelectObject)(dc,ob); U(DeleteObject)(bm); U(DeleteDC)(dc);
}

/* ---- DrawTextA / DrawTextW (user32) */
TEST(drawtext_extents_and_alignment){
  REQUIRE(U(DrawTextA)); REQUIRE(U(DrawTextW));
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  const char *s="Hello"; SIZE z=ext(&e,s); WCHAR w[16]; widen(s,w,16);
  RECT r={10,10,250,100};
  int ret=U(DrawTextA)(e.dc,s,-1,&r,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
  CHECKF(ret==e.tm.tmHeight&&r.left==10&&r.top==10&&r.bottom==10+e.tm.tmHeight&&r.right==10+z.cx,"DT_CALCRECT: ret %d rect %ld,%ld..%ld,%ld want right %ld",ret,r.left,r.top,r.right,r.bottom,10+z.cx);
  RECT rw={10,10,250,100}; int rw_ret=U(DrawTextW)(e.dc,w,-1,&rw,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
  CHECKF(rw_ret==ret&&!memcmp(&rw,&r,sizeof r),"DrawTextW CALCRECT differs from A");
  /* drawing: A == W pixel for pixel, ink inside the rect */
  setup(&e,BLACK,WHITE,TRANSPARENT); RECT d1={20,20,220,60}; ret=U(DrawTextA)(e.dc,s,-1,&d1,DT_SINGLELINE|DT_NOPREFIX);
  CHECKF(ret==e.tm.tmHeight,"DrawTextA draw returned %d want tmHeight %ld",ret,e.tm.tmHeight);
  DWORD *a=e_snap(&e); RECT bb; CHECK(e_bbox(&e,WHITE,NULL,&bb)>20);
  CHECKF(bb.left>=20&&bb.right<20+z.cx&&bb.top>=20&&bb.bottom<20+z.cy,"DT_LEFT|DT_TOP ink %ld,%ld..%ld,%ld outside text box",bb.left,bb.top,bb.right,bb.bottom);
  setup(&e,BLACK,WHITE,TRANSPARENT); RECT d2=d1; U(DrawTextW)(e.dc,w,-1,&d2,DT_SINGLELINE|DT_NOPREFIX); CHECKF(e_same(&e,a),"DrawTextW differs from A (%d px)",e_diff(&e,a));
  /* same as TextOut at the rect's top-left */
  setup(&e,BLACK,WHITE,TRANSPARENT); U(TextOutA)(e.dc,20,20,s,5); CHECKF(e_same(&e,a),"DrawText (DT_LEFT|DT_TOP) differs from TextOutA at the corner (%d px)",e_diff(&e,a));
  /* DT_RIGHT, DT_CENTER, DT_BOTTOM, DT_VCENTER */
  setup(&e,BLACK,WHITE,TRANSPARENT); RECT d3=d1; U(DrawTextA)(e.dc,s,-1,&d3,DT_SINGLELINE|DT_NOPREFIX|DT_RIGHT);
  DWORD *ref=draw_at(&e,s,0,220-z.cx,20); setup(&e,BLACK,WHITE,TRANSPARENT); d3=d1; U(DrawTextA)(e.dc,s,-1,&d3,DT_SINGLELINE|DT_NOPREFIX|DT_RIGHT);
  CHECKF(e_same(&e,ref),"DT_RIGHT: %d px differ from TextOut at right-cx",e_diff(&e,ref)); free(ref);
  setup(&e,BLACK,WHITE,TRANSPARENT); d3=d1; U(DrawTextA)(e.dc,s,-1,&d3,DT_SINGLELINE|DT_NOPREFIX|DT_BOTTOM);
  ref=draw_at(&e,s,0,20,60-e.tm.tmHeight); setup(&e,BLACK,WHITE,TRANSPARENT); d3=d1; U(DrawTextA)(e.dc,s,-1,&d3,DT_SINGLELINE|DT_NOPREFIX|DT_BOTTOM);
  CHECKF(e_same(&e,ref),"DT_BOTTOM: %d px differ",e_diff(&e,ref)); free(ref);
  setup(&e,BLACK,WHITE,TRANSPARENT); d3=d1; U(DrawTextA)(e.dc,s,-1,&d3,DT_SINGLELINE|DT_NOPREFIX|DT_CENTER|DT_VCENTER); e_bbox(&e,WHITE,NULL,&bb);
  long cxm=(bb.left+bb.right+1)/2, rcx=(20+220)/2; long cym=(bb.top+bb.bottom+1)/2, rcy=(20+60)/2;
  CHECKF(labs(cxm-rcx)<=z.cx/4&&labs(cym-rcy)<=e.tm.tmHeight/2,"centred ink centre %ld,%ld vs rect centre %ld,%ld",cxm,cym,rcx,rcy);
  t_note("DT_CENTER|DT_VCENTER ink %ld,%ld..%ld,%ld rect 20,20..220,60",bb.left,bb.top,bb.right,bb.bottom);
  free(a); e_close(&e);
}

TEST(drawtext_wordbreak_lines){
  REQUIRE(U(DrawTextA));
  E e; E_OPEN(e,W_,H_,NONANTIALIASED_QUALITY,18);
  const char *s="Hello World"; SIZE zh=ext(&e,"Hello"), zw=ext(&e,"World"), zf=ext(&e,s);
  RECT r={0,0,(int)(zf.cx-5),200};   /* narrower than the whole string, wider than each word */
  int ret=U(DrawTextA)(e.dc,s,-1,&r,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
  CHECKF(ret==2*e.tm.tmHeight&&r.bottom==2*e.tm.tmHeight,"word break into 2 lines: ret %d bottom %ld want %ld",ret,r.bottom,2*e.tm.tmHeight);
  CHECKF(r.right==(zh.cx>zw.cx?zh.cx:zw.cx),"wrapped width %ld want widest word %ld",r.right,(zh.cx>zw.cx?zh.cx:zw.cx));
  RECT r2={0,0,1000,200}; ret=U(DrawTextA)(e.dc,s,-1,&r2,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
  CHECKF(ret==e.tm.tmHeight&&r2.right==zf.cx,"unwrapped: ret %d right %ld want %ld",ret,r2.right,zf.cx);
  RECT r3={0,0,1000,200}; ret=U(DrawTextA)(e.dc,"a\nb\nc",-1,&r3,DT_CALCRECT|DT_NOPREFIX);
  CHECKF(ret==3*e.tm.tmHeight,"three lines: ret %d want %ld",ret,3*e.tm.tmHeight);
  e_close(&e);
}
