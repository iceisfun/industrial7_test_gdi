#include "toe_common.h"
#include <math.h>
/* DC text state (colours, background mode, alignment) and mapping modes / window and viewport transforms. */

TEST(text_state_defaults_and_roundtrip){
  HDC dc=U(CreateCompatibleDC)(NULL); CHECK(dc);
  EQV(U(GetTextColor)(dc),RGB(0,0,0));
  EQV(U(GetBkColor)(dc),RGB(255,255,255));
  EQV(U(GetBkMode)(dc),OPAQUE);
  EQV(U(GetTextAlign)(dc),TA_LEFT|TA_TOP|TA_NOUPDATECP);
  EQV(U(GetTextAlign)(dc),0);
  EQV(U(GetMapMode)(dc),MM_TEXT);
  /* Set* return the previous value, Get* the new one */
  EQV(U(SetTextColor)(dc,RGB(10,20,30)),RGB(0,0,0));
  EQV(U(GetTextColor)(dc),RGB(10,20,30));
  EQV(U(SetTextColor)(dc,RGB(1,2,3)),RGB(10,20,30));
  EQV(U(SetBkColor)(dc,RGB(200,100,50)),RGB(255,255,255));
  EQV(U(GetBkColor)(dc),RGB(200,100,50));
  EQV(U(SetBkMode)(dc,TRANSPARENT),OPAQUE);
  EQV(U(GetBkMode)(dc),TRANSPARENT);
  EQV(U(SetBkMode)(dc,OPAQUE),TRANSPARENT);
  EQV(U(SetBkMode)(dc,TRANSPARENT),OPAQUE);
  EQV(U(SetTextAlign)(dc,TA_RIGHT|TA_BOTTOM),0);
  EQV(U(GetTextAlign)(dc),TA_RIGHT|TA_BOTTOM);
  EQV(U(SetTextAlign)(dc,TA_CENTER|TA_BASELINE|TA_UPDATECP),TA_RIGHT|TA_BOTTOM);
  EQV(U(GetTextAlign)(dc),TA_CENTER|TA_BASELINE|TA_UPDATECP);
  EQV(U(SetTextAlign)(dc,TA_LEFT|TA_TOP),TA_CENTER|TA_BASELINE|TA_UPDATECP);
  EQV(U(GetTextAlign)(dc),0);
  /* a fresh DC is not affected by the changes on another one */
  HDC dc2=U(CreateCompatibleDC)(NULL);
  EQV(U(GetBkColor)(dc2),RGB(255,255,255)); EQV(U(GetBkMode)(dc2),OPAQUE); EQV(U(GetTextColor)(dc2),0);
  U(DeleteDC)(dc2);
  /* SaveDC / RestoreDC restore these attributes */
  int sv=U(SaveDC)(dc); U(SetTextColor)(dc,RGB(9,9,9)); U(SetBkMode)(dc,OPAQUE); U(SetTextAlign)(dc,TA_RIGHT);
  U(RestoreDC)(dc,sv);
  EQV(U(GetTextColor)(dc),RGB(1,2,3)); EQV(U(GetBkMode)(dc),TRANSPARENT); EQV(U(GetTextAlign)(dc),0);
  U(DeleteDC)(dc);
}

TEST(text_state_invalid){
  /* documented: failure returns CLR_INVALID (colours), 0 (GetBkMode, SetBkMode), GDI_ERROR (alignment) */
  EQV(U(GetTextColor)(NULL),CLR_INVALID);
  EQV(U(GetBkColor)(NULL),CLR_INVALID);
  EQV(U(SetTextColor)(NULL,0),CLR_INVALID);
  EQV(U(SetBkColor)(NULL,0),CLR_INVALID);
  EQV(U(GetBkMode)(NULL),0);
  EQV(U(SetBkMode)(NULL,OPAQUE),0);
  EQV(U(GetTextAlign)(NULL),GDI_ERROR);
  EQV(U(SetTextAlign)(NULL,0),GDI_ERROR);
  EQV(U(GetMapMode)(NULL),0);
  EQV(U(SetMapMode)(NULL,MM_TEXT),0);
  HDC dc=U(CreateCompatibleDC)(NULL);
  /* observed on Windows 10: SetBkMode does not validate its argument (documented as failing with 0); recorded only */
  { int r0=U(SetBkMode)(dc,0), g0=U(GetBkMode)(dc), r3=U(SetBkMode)(dc,3), g3=U(GetBkMode)(dc); U(SetBkMode)(dc,OPAQUE);
    t_note("SetBkMode(dc,0) returned %d (GetBkMode then %d); SetBkMode(dc,3) returned %d (GetBkMode then %d)",r0,g0,r3,g3); }
  EQV(U(GetBkMode)(dc),OPAQUE);
  EQV(U(SetMapMode)(dc,0),0); EQV(U(SetMapMode)(dc,9),0); EQV(U(GetMapMode)(dc),MM_TEXT);
  U(DeleteDC)(dc);
}

TEST(map_mode_set_get_and_fixed_ratios){
  HDC dc=U(CreateCompatibleDC)(NULL);
  int dpix=U(GetDeviceCaps)(dc,LOGPIXELSX), dpiy=U(GetDeviceCaps)(dc,LOGPIXELSY);
  static const int modes[]={MM_LOMETRIC,MM_HIMETRIC,MM_LOENGLISH,MM_HIENGLISH,MM_TWIPS,MM_ISOTROPIC,MM_ANISOTROPIC,MM_TEXT};
  int prev=MM_TEXT;
  for(unsigned i=0;i<sizeof modes/sizeof*modes;i++){
    int r=U(SetMapMode)(dc,modes[i]); CHECKF(r==prev,"SetMapMode(%d) returned %d want previous %d",modes[i],r,prev);
    CHECKF(U(GetMapMode)(dc)==modes[i],"GetMapMode after SetMapMode(%d) = %d",modes[i],U(GetMapMode)(dc));
    prev=modes[i];
  }
  /* fixed-scale modes: the logical extent covers the physical size of the device (HORZSIZE x VERTSIZE in mm, converted
   * to the mode's unit) mapped onto HORZRES x VERTRES pixels; y points up, so the viewport y extent is negative. */
  double hs=U(GetDeviceCaps)(dc,HORZSIZE), vs=U(GetDeviceCaps)(dc,VERTSIZE); int hr=U(GetDeviceCaps)(dc,HORZRES), vr=U(GetDeviceCaps)(dc,VERTRES);
  struct { int mode; double per_mm; } fx[]={{MM_LOENGLISH,100/25.4},{MM_HIENGLISH,1000/25.4},{MM_TWIPS,1440/25.4},{MM_LOMETRIC,10},{MM_HIMETRIC,100}};
  t_note("device caps: HORZSIZE %g VERTSIZE %g HORZRES %d VERTRES %d LOGPIXELS %dx%d",hs,vs,hr,vr,dpix,dpiy);
  for(unsigned i=0;i<sizeof fx/sizeof*fx;i++){
    U(SetMapMode)(dc,fx[i].mode);
    SIZE w,v; U(GetWindowExtEx)(dc,&w); U(GetViewportExtEx)(dc,&v);
    CHECKF(labs(w.cx-(long)(hs*fx[i].per_mm+0.5))<=1&&labs(w.cy-(long)(vs*fx[i].per_mm+0.5))<=1,"mode %d: window ext %ldx%ld, want about %.1fx%.1f",fx[i].mode,w.cx,w.cy,hs*fx[i].per_mm,vs*fx[i].per_mm);
    CHECKF(v.cx==hr&&v.cy==-vr,"mode %d: viewport ext %ldx%ld want %dx%d",fx[i].mode,v.cx,v.cy,hr,-vr);
    POINT p[2]={{0,0},{100,100}};
    CHECK(U(LPtoDP)(dc,p,2));
    double ex=100.0*hr/(hs*fx[i].per_mm), ey=100.0*vr/(vs*fx[i].per_mm);
    CHECKF(p[0].x==0&&p[0].y==0,"mode %d: origin maps to %ld,%ld",fx[i].mode,p[0].x,p[0].y);
    CHECKF(fabs(p[1].x-ex)<=1.5&&fabs(p[1].y+ey)<=1.5,"mode %d: 100 units map to %ld,%ld want about %.1f,%.1f",fx[i].mode,p[1].x,p[1].y,ex,-ey);
    /* SetWindowExtEx / SetViewportExtEx are ignored in fixed modes */
    SIZE old; U(SetWindowExtEx)(dc,7,9,&old); SIZE w2; U(GetWindowExtEx)(dc,&w2);
    CHECKF(w2.cx==w.cx&&w2.cy==w.cy,"mode %d: SetWindowExtEx changed the extent to %ldx%ld",fx[i].mode,w2.cx,w2.cy);
    U(SetViewportExtEx)(dc,7,9,&old); SIZE v2; U(GetViewportExtEx)(dc,&v2);
    CHECKF(v2.cx==v.cx&&v2.cy==v.cy,"mode %d: SetViewportExtEx changed the extent to %ldx%ld",fx[i].mode,v2.cx,v2.cy);
  }
  U(SetMapMode)(dc,MM_TEXT);
  SIZE w,v; CHECK(U(GetWindowExtEx)(dc,&w)); CHECK(U(GetViewportExtEx)(dc,&v));
  CHECKF(w.cx==1&&w.cy==1&&v.cx==1&&v.cy==1,"MM_TEXT extents %ldx%ld / %ldx%ld",w.cx,w.cy,v.cx,v.cy);
  POINT o; CHECK(U(GetWindowOrgEx)(dc,&o)); CHECK(o.x==0&&o.y==0); CHECK(U(GetViewportOrgEx)(dc,&o)); CHECK(o.x==0&&o.y==0);
  U(DeleteDC)(dc);
}

TEST(map_anisotropic_transform){
  HDC dc=U(CreateCompatibleDC)(NULL);
  EQV(U(SetMapMode)(dc,MM_ANISOTROPIC),MM_TEXT);
  SIZE old={-1,-1};
  CHECK(U(SetWindowExtEx)(dc,100,200,&old)); t_note("SetWindowExtEx previous extent on fresh MM_ANISOTROPIC: %ldx%ld",old.cx,old.cy);
  CHECK(U(SetViewportExtEx)(dc,300,100,&old));
  SIZE w,v; CHECK(U(GetWindowExtEx)(dc,&w)); CHECK(U(GetViewportExtEx)(dc,&v));
  EQV(w.cx,100); EQV(w.cy,200); EQV(v.cx,300); EQV(v.cy,100);
  /* previous values come back through the out parameters */
  SIZE o2; CHECK(U(SetWindowExtEx)(dc,100,200,&o2)); EQV(o2.cx,100); EQV(o2.cy,200);
  CHECK(U(SetViewportExtEx)(dc,300,100,NULL));
  POINT po; CHECK(U(SetWindowOrgEx)(dc,10,20,&po)); EQV(po.x,0); EQV(po.y,0);
  CHECK(U(SetWindowOrgEx)(dc,10,20,&po)); EQV(po.x,10); EQV(po.y,20);
  CHECK(U(SetViewportOrgEx)(dc,1000,2000,&po)); EQV(po.x,0); EQV(po.y,0);
  CHECK(U(SetViewportOrgEx)(dc,1000,2000,&po)); EQV(po.x,1000); EQV(po.y,2000);
  POINT g; CHECK(U(GetWindowOrgEx)(dc,&g)); EQV(g.x,10); EQV(g.y,20);
  CHECK(U(GetViewportOrgEx)(dc,&g)); EQV(g.x,1000); EQV(g.y,2000);
  /* documented formulas: Dx = (Lx - wOrgX) * vExtX / wExtX + vOrgX */
  POINT p[3]={{10,20},{110,220},{20,40}};
  CHECK(U(LPtoDP)(dc,p,3));
  CHECKF(p[0].x==1000&&p[0].y==2000,"LPtoDP origin -> %ld,%ld",p[0].x,p[0].y);
  CHECKF(p[1].x==1300&&p[1].y==2100,"LPtoDP (110,220) -> %ld,%ld want 1300,2100",p[1].x,p[1].y);
  CHECKF(p[2].x==1030&&p[2].y==2010,"LPtoDP (20,40) -> %ld,%ld want 1030,2010",p[2].x,p[2].y);
  POINT d[3]={{1000,2000},{1300,2100},{1030,2010}};
  CHECK(U(DPtoLP)(dc,d,3));
  CHECKF(d[0].x==10&&d[0].y==20&&d[1].x==110&&d[1].y==220&&d[2].x==20&&d[2].y==40,"DPtoLP: %ld,%ld %ld,%ld %ld,%ld",d[0].x,d[0].y,d[1].x,d[1].y,d[2].x,d[2].y);
  /* round trip within one unit for arbitrary points */
  for(int x=-37;x<200;x+=19) for(int y=-91;y<300;y+=23){
    POINT q={x,y}; U(LPtoDP)(dc,&q,1); U(DPtoLP)(dc,&q,1);
    CHECKF(labs(q.x-x)<=1&&labs(q.y-y)<=1,"round trip (%d,%d) -> (%ld,%ld)",x,y,q.x,q.y);
  }
  /* zero points is TRUE, no-op */
  CHECK(U(LPtoDP)(dc,p,0)); CHECK(U(DPtoLP)(dc,p,0));
  U(SetMapMode)(dc,MM_TEXT);
  U(DeleteDC)(dc);
}

TEST(map_offset_and_scale){
  HDC dc=U(CreateCompatibleDC)(NULL);
  U(SetMapMode)(dc,MM_ANISOTROPIC);
  U(SetWindowExtEx)(dc,100,200,NULL); U(SetViewportExtEx)(dc,300,100,NULL);
  U(SetWindowOrgEx)(dc,5,6,NULL); U(SetViewportOrgEx)(dc,50,60,NULL);
  POINT prev={-1,-1},cur;
  CHECK(U(OffsetViewportOrgEx)(dc,7,-9,&prev)); EQV(prev.x,50); EQV(prev.y,60);
  U(GetViewportOrgEx)(dc,&cur); EQV(cur.x,57); EQV(cur.y,51);
  CHECK(U(OffsetViewportOrgEx)(dc,3,3,NULL)); U(GetViewportOrgEx)(dc,&cur); EQV(cur.x,60); EQV(cur.y,54);
  CHECK(U(OffsetWindowOrgEx)(dc,-4,10,&prev)); EQV(prev.x,5); EQV(prev.y,6);
  U(GetWindowOrgEx)(dc,&cur); EQV(cur.x,1); EQV(cur.y,16);
  CHECK(U(OffsetWindowOrgEx)(dc,1,1,NULL)); U(GetWindowOrgEx)(dc,&cur); EQV(cur.x,2); EQV(cur.y,17);
  /* the offset takes effect in the transform */
  POINT p={2,17}; U(LPtoDP)(dc,&p,1); EQV(p.x,60); EQV(p.y,54);
  /* documented: xNew = xOld * Numerator / Denominator */
  SIZE sprev; SIZE ext;
  CHECK(U(ScaleWindowExtEx)(dc,3,2,1,4,&sprev)); EQV(sprev.cx,100); EQV(sprev.cy,200);
  U(GetWindowExtEx)(dc,&ext); EQV(ext.cx,150); EQV(ext.cy,50);
  CHECK(U(ScaleViewportExtEx)(dc,2,1,5,2,&sprev)); EQV(sprev.cx,300); EQV(sprev.cy,100);
  U(GetViewportExtEx)(dc,&ext); EQV(ext.cx,600); EQV(ext.cy,250);
  CHECK(U(ScaleWindowExtEx)(dc,1,1,1,1,NULL)); U(GetWindowExtEx)(dc,&ext); EQV(ext.cx,150); EQV(ext.cy,50);
  /* scaled transform: (2,17) -> viewport origin; (2+150,17+50) -> + (600,250) */
  POINT q[2]={{2,17},{152,67}}; U(LPtoDP)(dc,q,2);
  CHECKF(q[0].x==60&&q[0].y==54&&q[1].x==660&&q[1].y==304,"scaled LPtoDP: %ld,%ld %ld,%ld",q[0].x,q[0].y,q[1].x,q[1].y);
  U(SetMapMode)(dc,MM_TEXT);
  /* origin functions work in MM_TEXT too and a mode change keeps them */
  CHECK(U(SetViewportOrgEx)(dc,11,12,NULL)); CHECK(U(OffsetViewportOrgEx)(dc,1,1,&prev)); EQV(prev.x,11); EQV(prev.y,12);
  /* the window origin (2,17) set above is independent of the mapping mode: Dx = (0-2)+12, Dy = (0-17)+13 */
  POINT t={0,0}; U(LPtoDP)(dc,&t,1); EQV(t.x,10); EQV(t.y,-4);
  U(DeleteDC)(dc);
}

TEST(map_isotropic_keeps_ratio){
  HDC dc=U(CreateCompatibleDC)(NULL);
  U(SetMapMode)(dc,MM_ISOTROPIC);
  U(SetWindowExtEx)(dc,100,100,NULL); U(SetViewportExtEx)(dc,200,400,NULL);
  SIZE w,v; U(GetWindowExtEx)(dc,&w); U(GetViewportExtEx)(dc,&v);
  t_note("MM_ISOTROPIC window 100x100 viewport 200x400 -> window %ldx%ld viewport %ldx%ld",w.cx,w.cy,v.cx,v.cy);
  /* documented: one logical unit has the same size horizontally and vertically */
  CHECKF(labs(v.cx*w.cy-v.cy*w.cx)<=labs(w.cx*w.cy)/50+1,"scale differs: %ld/%ld vs %ld/%ld",v.cx,w.cx,v.cy,w.cy);
  POINT p[2]={{0,0},{100,100}}; U(LPtoDP)(dc,p,2);
  CHECKF(p[1].x-p[0].x==p[1].y-p[0].y,"100 units: dx %ld dy %ld",p[1].x-p[0].x,p[1].y-p[0].y);
  U(SetMapMode)(dc,MM_TEXT);
  U(DeleteDC)(dc);
}

/* A font created with a logical height in a fixed mapping mode: tmHeight comes back in logical units. */
TEST(map_loenglish_font_height){
  E e; E_OPEN(e,64,64,DEFAULT_QUALITY,18);
  EQV(U(SetMapMode)(e.dc,MM_LOENGLISH),MM_TEXT);
  SIZE w,v; U(GetWindowExtEx)(e.dc,&w); U(GetViewportExtEx)(e.dc,&v);
  double ky=(double)-v.cy/w.cy, kx=(double)v.cx/w.cx;     /* device pixels per logical unit */
  int N=25; int devpx=(int)(N*ky+0.5);
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfWeight=400; lf.lfCharSet=ANSI_CHARSET; lstrcpyW(lf.lfFaceName,L"Arial");
  /* reference: the same font in MM_TEXT at the device height that N logical units map to */
  U(SetMapMode)(e.dc,MM_TEXT);
  lf.lfHeight=-devpx; HFONT fr=U(CreateFontIndirectW)(&lf); HFONT o1=(HFONT)U(SelectObject)(e.dc,fr);
  TEXTMETRICW tr; U(GetTextMetricsW)(e.dc,&tr); SIZE sr; U(GetTextExtentPoint32A)(e.dc,"Hello World",11,&sr);
  U(SelectObject)(e.dc,o1);
  EQV(U(SetMapMode)(e.dc,MM_LOENGLISH),MM_TEXT);
  for(int sign=-1;sign<=1;sign+=2){
    lf.lfHeight=sign*N; HFONT f=U(CreateFontIndirectW)(&lf); HFONT o=(HFONT)U(SelectObject)(e.dc,f);
    TEXTMETRICW tm; CHECK(U(GetTextMetricsW)(e.dc,&tm));
    SIZE s; U(GetTextExtentPoint32A)(e.dc,"Hello World",11,&s);
    t_note("MM_LOENGLISH lfHeight %d: tmHeight %ld asc %ld desc %ld; reference %d px font: tmHeight %ld (%.1f logical) cx %ld (%.1f logical); extent cx %ld cy %ld",
           sign*N,tm.tmHeight,tm.tmAscent,tm.tmDescent,devpx,tr.tmHeight,tr.tmHeight/ky,sr.cx,sr.cx/kx,s.cx,s.cy);
    CHECKF(tm.tmHeight==tm.tmAscent+tm.tmDescent,"lfHeight %d: ascent+descent != height",sign*N);
    CHECKF(s.cy==tm.tmHeight,"lfHeight %d: extent cy %ld tmHeight %ld",sign*N,s.cy,tm.tmHeight);
    if(sign<0){
      /* negative lfHeight: character height N logical units = devpx pixels; metrics are reported in logical units */
      CHECKF(fabs(tm.tmHeight-tr.tmHeight/ky)<=1.5,"lfHeight -%d: tmHeight %ld want about %.1f",N,tm.tmHeight,tr.tmHeight/ky);
      CHECKF(fabs(s.cx-sr.cx/kx)<=2.0,"lfHeight -%d: extent cx %ld want about %.1f",N,s.cx,sr.cx/kx);
    } else {
      /* positive lfHeight: cell height (tmHeight) of N logical units */
      CHECKF(labs(tm.tmHeight-N)<=1,"lfHeight %d: tmHeight %ld",N,tm.tmHeight);
    }
    U(SelectObject)(e.dc,o); U(DeleteObject)(f);
  }
  /* LPtoDP of the logical text height gives back the device height (y flipped) */
  POINT p={0,0},q={0,N}; U(LPtoDP)(e.dc,&p,1); U(LPtoDP)(e.dc,&q,1);
  CHECKF(labs((p.y-q.y)-devpx)<=1,"%d logical units = %ld px, want %d",N,p.y-q.y,devpx);
  EQV(U(SetMapMode)(e.dc,MM_TEXT),MM_LOENGLISH);
  U(DeleteObject)(fr);
  e_close(&e);
}
