/* glyph_bitmaps / text: rendered text into DIB sections (TextOutW, ExtTextOutW variants, DrawTextW) and style synthesis.
 *
 * Canvas: top-down DIB section selected into a CreateCompatibleDC(NULL), MM_TEXT. Size w = extent.cx + |req_h| + 12,
 * h = tmHeight + 4; the text origin is (4,2) with TA_TOP|TA_LEFT and SetBkMode TRANSPARENT unless a method says otherwise.
 *   dib=d32: 32bpp BI_RGB; dib=d8: 8bpp with a 256-entry grey palette (index i = RGB(i,i,i)); dib=d1: 1bpp black/white.
 *   col=bow: black text (RGB 0,0,0) on white; col=wob: white text on black.
 * Grey of a 32bpp pixel = (R*77 + G*150 + B*29) >> 8 (as rec_gray_from_dib); d8 grey = palette index.
 * Ink of a pixel = 255-grey (bow) or grey (wob). ink box = smallest rectangle with ink > 0, as l t r b with r/b EXCLUSIVE,
 * in canvas pixels (origin (4,2) is where the text was drawn); ink_sum = sum of ink; ink_px = pixels with ink > 0;
 * colour_px = 32bpp pixels with R != G or G != B (ClearType fringes). sha = first 64 bits of SHA-256 over the canvas:
 * d32 as R,G,B bytes per pixel (alpha dropped), d8 as indices, d1 as bits, rows without padding.
 * txb / styb B lines: the ink box crop, 8bpp grey levels (0..255) when the crop has no colour, else 24bpp R,G,B.
 * txprof: per-row and per-column ink sums over the ink box (comma lists), short strings, bow, d32 only.
 */
#include "gb.h"

#define TAG_HEAD 0x64616568u   /* 'head' as GetFontData wants it (first char in the low byte) */

int gb_dib_new(gb_dib *d, int w, int h, int bpp){
  memset(d,0,sizeof *d);
  struct { BITMAPINFOHEADER h; RGBQUAD pal[256]; } bi; memset(&bi,0,sizeof bi);
  bi.h.biSize=sizeof bi.h; bi.h.biWidth=w; bi.h.biHeight=-h; bi.h.biPlanes=1; bi.h.biBitCount=(WORD)bpp; bi.h.biCompression=BI_RGB;
  if(bpp==8){ bi.h.biClrUsed=256; for(int i=0;i<256;i++){ bi.pal[i].rgbRed=bi.pal[i].rgbGreen=bi.pal[i].rgbBlue=(BYTE)i; } }
  if(bpp==1){ bi.h.biClrUsed=2; bi.pal[1].rgbRed=bi.pal[1].rgbGreen=bi.pal[1].rgbBlue=255; }
  d->dc=CreateCompatibleDC(NULL); if(!d->dc) return 0;
  void *bits=NULL; d->bmp=CreateDIBSection(d->dc,(BITMAPINFO*)&bi,DIB_RGB_COLORS,&bits,NULL,0);
  if(!d->bmp){ DeleteDC(d->dc); d->dc=NULL; return 0; }
  d->old=(HBITMAP)SelectObject(d->dc,d->bmp); d->bits=(uint8_t*)bits; d->w=w; d->h=h; d->bpp=bpp;
  d->stride=((w*bpp+31)/32)*4;
  return 1;
}
void gb_dib_fill(gb_dib *d, int white){
  GdiFlush();
  if(d->bpp==32){ for(int y=0;y<d->h;y++){ uint32_t *r=(uint32_t*)(d->bits+(size_t)d->stride*y); for(int x=0;x<d->w;x++) r[x]=white?0x00FFFFFFu:0; } }
  else memset(d->bits, d->bpp==8 ? (white?255:0) : (white?0xFF:0), (size_t)d->stride*d->h);
}
void gb_dib_free(gb_dib *d){ if(d->dc){ SelectObject(d->dc,d->old); DeleteObject(d->bmp); DeleteDC(d->dc); } memset(d,0,sizeof *d); }
int gb_dib_grey(const gb_dib *d, int x, int y){
  const uint8_t *r=d->bits+(size_t)d->stride*y;
  if(d->bpp==32){ const uint8_t *p=r+4*x; return (p[2]*77+p[1]*150+p[0]*29)>>8; }
  if(d->bpp==8) return r[x];
  return (r[x>>3]>>(7-(x&7)))&1 ? 255 : 0;
}
static int is_colour(const gb_dib *d, int x, int y){
  if(d->bpp!=32) return 0;
  const uint8_t *p=d->bits+(size_t)d->stride*y+4*x; return p[0]!=p[1] || p[1]!=p[2];
}

typedef struct { int l,t,r,b; long long sum; int px, cpx; char sha[17]; } ink_t;
static void canvas_sha(const gb_dib *d, char out[17]){
  rec_sha256_ctx c; rec_sha256_init(&c);
  for(int y=0;y<d->h;y++){ const uint8_t *r=d->bits+(size_t)d->stride*y;
    if(d->bpp==32){ for(int x=0;x<d->w;x++){ uint8_t t[3]={r[4*x+2],r[4*x+1],r[4*x]}; rec_sha256_update(&c,t,3); } }
    else rec_sha256_update(&c,r,(size_t)(d->w*d->bpp+7)/8); }
  uint8_t dg[32]; rec_sha256_final(&c,dg); rec_hex(out,dg,8);
}
static void ink(const gb_dib *d, int bow, ink_t *k){
  GdiFlush(); memset(k,0,sizeof *k); k->l=d->w; k->t=d->h; k->r=0; k->b=0;
  for(int y=0;y<d->h;y++) for(int x=0;x<d->w;x++){
    int g=gb_dib_grey(d,x,y), v=bow?255-g:g;
    if(is_colour(d,x,y)) k->cpx++;
    if(v>0){ k->sum+=v; k->px++; if(x<k->l)k->l=x; if(y<k->t)k->t=y; if(x+1>k->r)k->r=x+1; if(y+1>k->b)k->b=y+1; }
  }
  if(!k->px){ k->l=k->t=k->r=k->b=0; }
  canvas_sha(d,k->sha);
}
static void rv_ink(rv *v, const ink_t *k){ rv_i(v,k->l); rv_i(v,k->t); rv_i(v,k->r); rv_i(v,k->b); rv_i(v,k->sum); rv_i(v,k->px); rv_i(v,k->cpx); rv_raw(v,k->sha); }
static void crop_bitmap(const char *table, const rk *key, const gb_dib *d, const ink_t *k){
  if(!k->px) return;
  int w=k->r-k->l, h=k->b-k->t, col=0;
  for(int y=k->t;y<k->b && !col;y++) for(int x=k->l;x<k->r;x++) if(is_colour(d,x,y)){ col=1; break; }
  size_t n=(size_t)w*h*(col?3:1); uint8_t *b=(uint8_t*)malloc(n), *p=b;
  for(int y=k->t;y<k->b;y++) for(int x=k->l;x<k->r;x++){
    if(col){ const uint8_t *s=d->bits+(size_t)d->stride*y+4*x; *p++=s[2]; *p++=s[1]; *p++=s[0]; } else *p++=(uint8_t)gb_dib_grey(d,x,y); }
  rec_bitmap(table,key,w,h,col?24:8,b,n); free(b);
}

typedef struct { const char *id; const WCHAR *s; int shortstr; } tstr;
static const tstr STRS[] = {
  {"Name",L"Name",1}, {"OK",L"OK",1}, {"Cancel",L"Cancel",1}, {"Size",L"Size",1}, {"Hg",L"Hg",1},
  {"fox",L"The quick brown fox jumps over the lazy dog",0}, {"digits",L"0123456789 +-*/=()[]{}",0},
  {"latin1",L"\x00C5ngstr\x00F6m caf\x00E9 \x00A9 \x00B0 \x00B5 \x00DF \x00D7",0},
};
#define NSTR (int)(sizeof STRS/sizeof*STRS)
static const int Q4[] = {NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY, DEFAULT_QUALITY};

/* canvas size for a string in a font (measured on a scratch DC) */
static void canvas_size(HFONT f, const WCHAR *s, int req_h, int *w, int *h, SIZE *ext, TEXTMETRICW *tm){
  HDC dc=CreateCompatibleDC(NULL); HFONT o=(HFONT)SelectObject(dc,f);
  memset(tm,0,sizeof *tm); GetTextMetricsW(dc,tm); ext->cx=ext->cy=0; GetTextExtentPoint32W(dc,s,lstrlenW(s),ext);
  SelectObject(dc,o); DeleteDC(dc);
  *w=ext->cx+(req_h<0?-req_h:req_h)+12; *h=tm->tmHeight+4;
}

/* ---------------------------------------------------------------- tx */
void sec_text(void){
  rec_schema("tx","face req_h q col dib s","w:px h:px ext_cx:px ext_cy:px ink_l:px ink_t:px ink_r:px ink_b:px ink_sum ink_px colour_px sha");
  rec_schema("txprof","face req_h q s","rows cols");
  static const int HT_FULL[] = {-11,-12,-13,-16,-20}, HT_QUICK[] = {-11,-12};
  const int *hs=g_full?HT_FULL:HT_QUICK; int nh=g_full?5:2;
  for(int fi=0;fi<g_nfaces;fi++){
    if(!gb_face_on(fi)) continue;
    const char *face=g_faces[fi].face;
    for(int hi=0;hi<nh;hi++) for(int qi=0;qi<4;qi++){
      HFONT f=gb_font(face,hs[hi],FW_NORMAL,0,0,0,Q4[qi]);
      for(int si=0;si<NSTR;si++){
        const WCHAR *s=STRS[si].s; int w,h; SIZE ext; TEXTMETRICW tm; canvas_size(f,s,hs[hi],&w,&h,&ext,&tm);
        for(int bpp=32;bpp>=8;bpp-=24) for(int bow=1;bow>=0;bow--){
          gb_dib d; if(!gb_dib_new(&d,w,h,bpp)){ rec_note("dib fail %dx%dx%d",w,h,bpp); continue; }
          gb_dib_fill(&d,bow); HFONT o=(HFONT)SelectObject(d.dc,f);
          SetBkMode(d.dc,TRANSPARENT); SetTextAlign(d.dc,TA_TOP|TA_LEFT); SetTextColor(d.dc,bow?RGB(0,0,0):RGB(255,255,255));
          TextOutW(d.dc,4,2,s,lstrlenW(s)); GdiFlush();
          ink_t k; ink(&d,bow,&k);
          rk key; rk_reset(&key); rk_s(&key,"face",face); rk_i(&key,"req_h",hs[hi]); rk_s(&key,"q",gb_qname(Q4[qi]));
          rk_s(&key,"col",bow?"bow":"wob"); rk_s(&key,"dib",bpp==32?"d32":"d8"); rk_s(&key,"s",STRS[si].id);
          rv v; rv_reset(&v); rv_i(&v,w); rv_i(&v,h); rv_i(&v,ext.cx); rv_i(&v,ext.cy); rv_ink(&v,&k); rec_row("tx",&key,&v);
          if(STRS[si].shortstr && bow && bpp==32){
            crop_bitmap("txb",&key,&d,&k);
            char rows[2048], cols[4096]; int rl=0, cl=0; rows[0]=cols[0]=0;
            for(int y=k.t;y<k.b;y++){ long long sm=0; for(int x=k.l;x<k.r;x++) sm+=255-gb_dib_grey(&d,x,y); rl+=snprintf(rows+rl,sizeof rows-rl,"%s%lld",y>k.t?",":"",sm); }
            for(int x=k.l;x<k.r;x++){ long long sm=0; for(int y=k.t;y<k.b;y++) sm+=255-gb_dib_grey(&d,x,y); cl+=snprintf(cols+cl,sizeof cols-cl,"%s%lld",x>k.l?",":"",sm); }
            rk pk; rk_reset(&pk); rk_s(&pk,"face",face); rk_i(&pk,"req_h",hs[hi]); rk_s(&pk,"q",gb_qname(Q4[qi])); rk_s(&pk,"s",STRS[si].id);
            rv pv; rv_reset(&pv); rv_raw(&pv,rl?rows:"~"); rv_raw(&pv,cl?cols:"~"); rec_row("txprof",&pk,&pv);
          }
          SelectObject(d.dc,o); gb_dib_free(&d);
        }
      }
      DeleteObject(f);
    }
    rec_flush();
  }
}

/* ---------------------------------------------------------------- txm */
enum { M_TEXTOUT, M_ETO0, M_OPAQUE, M_CLIPPED, M_OPQCLIP, M_GI, M_DX, M_DRAWTEXT, M_DTPREFIX, M_D1, M_N };
static const char *M_NAME[M_N] = {"textout","eto_0","eto_opaque","eto_clipped","eto_opaque_clipped","eto_glyph_index","eto_dx","drawtext","drawtext_prefix","d1_textout"};

void sec_methods(void){
  rec_schema("txm","face req_h q s m","ret ink_l:px ink_t:px ink_r:px ink_b:px ink_sum ink_px colour_px sha same_as_textout");
  rec_note("txm: bow, d32 canvas (d1_textout: 1bpp); eto_clipped clips to x 4..4+ext/2 y 2..2+tmHeight; eto_opaque fills the "
           "whole canvas with the background via ETO_OPAQUE; eto_dx passes GetCharWidth32W widths; eto_glyph_index passes "
           "GetGlyphIndicesW(flags 0); drawtext = DrawTextW DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP in (4,2,w,h); "
           "drawtext_prefix draws L\"&\"+s without DT_NOPREFIX; ret = API return value; same_as_textout = canvas identical "
           "to the textout canvas (! for d1)");
  static const int HM[] = {-11,-12,-16}; static const int QM[] = {NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY};
  static const int SM[] = {2,1,5};  /* Cancel, OK, fox */
  for(int fi=0;fi<g_nfaces;fi++){
    if(!gb_face_on(fi) || (g_faces[fi].alias && g_full)) continue;
    const char *face=g_faces[fi].face;
    for(int hi=0;hi<(g_full?3:2);hi++) for(int qi=0;qi<3;qi++){
      HFONT f=gb_font(face,HM[hi],FW_NORMAL,0,0,0,QM[qi]);
      for(int si=0;si<3;si++){
        const WCHAR *s=STRS[SM[si]].s; int n=lstrlenW(s); int w,h; SIZE ext; TEXTMETRICW tm; canvas_size(f,s,HM[hi],&w,&h,&ext,&tm);
        uint8_t *ref=NULL; size_t refn=0;
        for(int m=0;m<M_N;m++){
          gb_dib d; if(!gb_dib_new(&d,w,h,m==M_D1?1:32)) continue;
          gb_dib_fill(&d,1); HFONT o=(HFONT)SelectObject(d.dc,f);
          SetBkMode(d.dc,TRANSPARENT); SetTextAlign(d.dc,TA_TOP|TA_LEFT); SetTextColor(d.dc,RGB(0,0,0)); SetBkColor(d.dc,RGB(255,255,255));
          long long ret=0; RECT all={0,0,w,h}, clip={4,2,4+ext.cx/2,2+tm.tmHeight};
          switch(m){
            case M_TEXTOUT: case M_D1: ret=TextOutW(d.dc,4,2,s,n); break;
            case M_ETO0: ret=ExtTextOutW(d.dc,4,2,0,NULL,s,n,NULL); break;
            case M_OPAQUE: ret=ExtTextOutW(d.dc,4,2,ETO_OPAQUE,&all,s,n,NULL); break;
            case M_CLIPPED: ret=ExtTextOutW(d.dc,4,2,ETO_CLIPPED,&clip,s,n,NULL); break;
            case M_OPQCLIP: ret=ExtTextOutW(d.dc,4,2,ETO_OPAQUE|ETO_CLIPPED,&clip,s,n,NULL); break;
            case M_GI: { WORD gi[128]; DWORD r=GetGlyphIndicesW(d.dc,s,n,gi,0); ret = r==GDI_ERROR ? -1 : ExtTextOutW(d.dc,4,2,ETO_GLYPH_INDEX,NULL,(LPCWSTR)gi,n,NULL); break; }
            case M_DX: { INT dx[128]; for(int i=0;i<n;i++){ INT cw=0; GetCharWidth32W(d.dc,s[i],s[i],&cw); dx[i]=cw; } ret=ExtTextOutW(d.dc,4,2,0,NULL,s,n,dx); break; }
            case M_DRAWTEXT: { RECT r={4,2,w,h}; ret=DrawTextW(d.dc,s,n,&r,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP); break; }
            case M_DTPREFIX: { WCHAR b[130]; b[0]=L'&'; lstrcpynW(b+1,s,128); RECT r={4,2,w,h}; ret=DrawTextW(d.dc,b,-1,&r,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOCLIP); break; }
          }
          GdiFlush(); ink_t k; ink(&d,1,&k);
          size_t cn=(size_t)d.stride*d.h; int same=-1;
          if(m==M_TEXTOUT){ ref=(uint8_t*)malloc(cn); memcpy(ref,d.bits,cn); refn=cn; }
          else if(m!=M_D1 && ref && refn==cn) same=!memcmp(ref,d.bits,cn);
          rk key; rk_reset(&key); rk_s(&key,"face",face); rk_i(&key,"req_h",HM[hi]); rk_s(&key,"q",gb_qname(QM[qi])); rk_s(&key,"s",STRS[SM[si]].id); rk_s(&key,"m",M_NAME[m]);
          rv v; rv_reset(&v); rv_i(&v,ret); rv_ink(&v,&k); if(same<0) rv_na(&v); else rv_i(&v,same); rec_row("txm",&key,&v);
          if(m==M_D1 && STRS[SM[si]].shortstr) crop_bitmap("txm_d1",&key,&d,&k);
          SelectObject(d.dc,o); gb_dib_free(&d);
        }
        free(ref);
      }
      DeleteObject(f);
    }
    rec_flush();
  }
}

/* ---------------------------------------------------------------- sty */
typedef struct { const char *id; int wt, it, ul, so; } style_t;
static const style_t STY[] = { {"reg",400,0,0,0}, {"b",700,0,0,0}, {"sb",600,0,0,0}, {"l",300,0,0,0}, {"i",400,1,0,0}, {"bi",700,1,0,0}, {"u",400,0,1,0}, {"s",400,0,0,1}, {"bu",700,0,1,0} };

void sec_styles(void){
  rec_schema("sty","face req_h st q","got h:px asc:px desc:px ave:px max:px weight italic ul so ovh:px otm otm_fssel otm_italic_angle "
             "us_pos:px us_size:px so_pos:px so_size:px full_name style_name fontdata_len:B head_csum ext_cancel:px "
             "ink_l:px ink_t:px ink_r:px ink_b:px ink_sum ink_px colour_px sha");
  rec_note("sty: Hg rendered black on white at (4,2) into a d32 canvas; fontdata_len/head_csum identify the font file actually used "
           "(a real bold/italic face has its own file, a synthesized one shares the regular file); otm = GetOutlineTextMetricsW size or !");
  static const int HS[] = {-11,-12,-16}; static const int QS[] = {NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY};
  for(int fi=0;fi<g_nfaces;fi++){
    if(!gb_face_on(fi)) continue;
    const char *face=g_faces[fi].face;
    for(int hi=0;hi<(g_full?3:1);hi++) for(unsigned st=0;st<sizeof STY/sizeof*STY;st++) for(int qi=0;qi<2;qi++){
      const style_t *S=&STY[st];
      HFONT f=gb_font(face,HS[g_full?hi:1],S->wt,S->it,S->ul,S->so,QS[qi]);
      int w,h; SIZE ext; TEXTMETRICW tm; canvas_size(f,L"Hg",HS[g_full?hi:1],&w,&h,&ext,&tm);
      gb_dib d; if(!gb_dib_new(&d,w,h,32)){ DeleteObject(f); continue; }
      gb_dib_fill(&d,1); HFONT o=(HFONT)SelectObject(d.dc,f);
      WCHAR got[LF_FACESIZE]=L""; GetTextFaceW(d.dc,LF_FACESIZE,got);
      rk key; rk_reset(&key); rk_s(&key,"face",face); rk_i(&key,"req_h",HS[g_full?hi:1]); rk_s(&key,"st",S->id); rk_s(&key,"q",gb_qname(QS[qi]));
      rv v; rv_reset(&v); rv_w(&v,got);
      rv_i(&v,tm.tmHeight); rv_i(&v,tm.tmAscent); rv_i(&v,tm.tmDescent); rv_i(&v,tm.tmAveCharWidth); rv_i(&v,tm.tmMaxCharWidth); rv_i(&v,tm.tmWeight);
      rv_i(&v,tm.tmItalic); rv_i(&v,tm.tmUnderlined); rv_i(&v,tm.tmStruckOut); rv_i(&v,tm.tmOverhang);
      UINT on=GetOutlineTextMetricsW(d.dc,0,NULL);
      if(on && on!=GDI_ERROR){
        OUTLINETEXTMETRICW *otm=(OUTLINETEXTMETRICW*)calloc(1,on);
        if(GetOutlineTextMetricsW(d.dc,on,otm)){
          rv_u(&v,on); rv_x(&v,otm->otmfsSelection); rv_i(&v,otm->otmItalicAngle); rv_i(&v,otm->otmsUnderscorePosition); rv_i(&v,otm->otmsUnderscoreSize);
          rv_i(&v,otm->otmsStrikeoutPosition); rv_i(&v,(int)otm->otmsStrikeoutSize);
          rv_w(&v,(const WCHAR*)((const BYTE*)otm+(UINT_PTR)otm->otmpFullName)); rv_w(&v,(const WCHAR*)((const BYTE*)otm+(UINT_PTR)otm->otmpStyleName));
        } else for(int i=0;i<9;i++) rv_na(&v);
        free(otm);
      } else for(int i=0;i<9;i++) rv_na(&v);
      DWORD fl=GetFontData(d.dc,0,0,NULL,0); if(fl==GDI_ERROR) rv_na(&v); else rv_u(&v,fl);
      uint8_t cs[4]; DWORD r=GetFontData(d.dc,TAG_HEAD,8,cs,4);
      if(r==4) rv_x(&v,(unsigned long)cs[0]<<24|(unsigned long)cs[1]<<16|(unsigned long)cs[2]<<8|cs[3]); else rv_na(&v);
      SIZE ec={0,0}; GetTextExtentPoint32W(d.dc,L"Cancel",6,&ec); rv_i(&v,ec.cx);
      SetBkMode(d.dc,TRANSPARENT); SetTextAlign(d.dc,TA_TOP|TA_LEFT); SetTextColor(d.dc,RGB(0,0,0));
      TextOutW(d.dc,4,2,L"Hg",2); GdiFlush(); ink_t k; ink(&d,1,&k); rv_ink(&v,&k);
      rec_row("sty",&key,&v);
      crop_bitmap("styb",&key,&d,&k);
      SelectObject(d.dc,o); gb_dib_free(&d); DeleteObject(f);
    }
    rec_flush();
  }
}
