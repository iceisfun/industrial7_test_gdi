/* glyph_bitmaps / ggo: GetGlyphOutlineW rasterisation fingerprints.
 *
 * For each outline face x height (H_FULL) x char (g_all; core only for the alias faces and in --quick):
 *   GGO_METRICS (GLYPHMETRICS), GGO_BITMAP, GGO_GRAY2_BITMAP, GGO_GRAY4_BITMAP, GGO_GRAY8_BITMAP, identity MAT2,
 *   by character code and again by glyph index (GGO_GLYPH_INDEX, index from GetGlyphIndicesW), and (full) with GGO_UNHINTED.
 * Buffers: the GGO return value (`*_len`, bytes, rows DWORD-padded as returned) is recorded as is; hashes and raw B lines
 * use the REPACKED glyph: rows byte-aligned, width gmBlackBoxX, height gmBlackBoxY; GGO_BITMAP 1bpp MSB-first,
 * GRAY2 levels 0..4 packed 4bpp, GRAY4 levels 0..16 at 8bpp, GRAY8 levels 0..64 at 8bpp (raw levels, NOT 0..255).
 * If the returned size is not padded_stride * gmBlackBoxY the raw buffer is hashed instead and an N line says so.
 */
#include "gb.h"

static const MAT2 IDENT = { {0,1}, {0,0}, {0,0}, {0,1} };
static const UINT FMTS[4] = { GGO_BITMAP, GGO_GRAY2_BITMAP, GGO_GRAY4_BITMAP, GGO_GRAY8_BITMAP };
static const char *FMT_N[4] = { "bm", "g2", "g4", "g8" };

typedef struct {
  DWORD ret;                 /* GGO return (size) or GDI_ERROR */
  GLYPHMETRICS gm;
  uint8_t *pk; size_t pkn;   /* repacked bytes (or raw if the size did not match) */
  int packed_ok;
} gres;

static void gres_free(gres *g){ free(g->pk); memset(g,0,sizeof *g); }

/* repack one GGO buffer: fi = index in FMTS */
static void repack(gres *g, int fi, const uint8_t *buf, DWORD n){
  int w=(int)g->gm.gmBlackBoxX, h=(int)g->gm.gmBlackBoxY;
  size_t sst = fi==0 ? (size_t)((w+31)/32)*4 : (size_t)((w+3)&~3);
  g->packed_ok = (n == sst*(size_t)h);
  if(!g->packed_ok){ g->pk=(uint8_t*)malloc(n?n:1); memcpy(g->pk,buf,n); g->pkn=n; return; }
  size_t dst = fi==0 ? (size_t)(w+7)/8 : fi==1 ? (size_t)(w+1)/2 : (size_t)w;
  g->pkn=dst*(size_t)h; g->pk=(uint8_t*)calloc(1,g->pkn?g->pkn:1);
  for(int y=0;y<h;y++){
    const uint8_t *s=buf+sst*y; uint8_t *d=g->pk+dst*y;
    if(fi==0) memcpy(d,s,dst);
    else if(fi==1){ for(int x=0;x<w;x++) d[x/2] |= (uint8_t)((s[x]&0xF) << ((x&1)?0:4)); }
    else memcpy(d,s,dst);
  }
}

/* one GGO call pair (size query, then fetch). fmt index -1 = GGO_METRICS. */
static void ggo(HDC dc, UINT ch, int fi, UINT extra, gres *g){
  memset(g,0,sizeof *g);
  UINT fmt=(fi<0?GGO_METRICS:FMTS[fi])|extra;
  memset(&g->gm,0,sizeof g->gm);
  DWORD n=GetGlyphOutlineW(dc,ch,fmt,&g->gm,0,NULL,&IDENT);
  g->ret=n;
  if(fi<0 || n==GDI_ERROR) return;
  if(n==0){ g->packed_ok=1; return; }
  uint8_t *buf=(uint8_t*)calloc(1,n);
  GLYPHMETRICS gm2; memset(&gm2,0,sizeof gm2);
  DWORD r=GetGlyphOutlineW(dc,ch,fmt,&gm2,n,buf,&IDENT);
  if(r==GDI_ERROR){ g->ret=GDI_ERROR; free(buf); return; }
  g->gm=gm2; repack(g,fi,buf,n); free(buf);
}

static void gm_ints(const GLYPHMETRICS *gm, int32_t o[6]){
  o[0]=(int32_t)gm->gmBlackBoxX; o[1]=(int32_t)gm->gmBlackBoxY; o[2]=gm->gmptGlyphOrigin.x; o[3]=gm->gmptGlyphOrigin.y;
  o[4]=gm->gmCellIncX; o[5]=gm->gmCellIncY;
}
static int gm_eq(const GLYPHMETRICS *a, const GLYPHMETRICS *b){ int32_t x[6],y[6]; gm_ints(a,x); gm_ints(b,y); return !memcmp(x,y,sizeof x); }
static int gres_eq(const gres *a, const gres *b){
  if(a->ret!=b->ret || !gm_eq(&a->gm,&b->gm) || a->pkn!=b->pkn) return 0;
  return a->pkn==0 || !memcmp(a->pk,b->pk,a->pkn);
}
static void rv_gm(rv *v, const gres *g){
  if(g->ret==GDI_ERROR){ for(int i=0;i<6;i++) rv_na(v); return; }
  int32_t o[6]; gm_ints(&g->gm,o); for(int i=0;i<6;i++) rv_i(v,o[i]);
}
static void rv_lensha(rv *v, const gres *g){
  if(g->ret==GDI_ERROR){ rv_na(v); rv_na(v); return; }
  char h[17]; rv_u(v,g->ret); gb_short_hash(h,g->pk?g->pk:(const uint8_t*)"",g->pkn); rv_raw(v,h);
}
static void agg_add(rec_sha256_ctx *c, UINT ch, const gres *g){
  int32_t hdr[9]; hdr[0]=(int32_t)ch; hdr[1]=(int32_t)g->ret; gm_ints(&g->gm,hdr+2); hdr[8]=(int32_t)g->pkn;
  rec_sha256_update(c,hdr,sizeof hdr); if(g->pkn) rec_sha256_update(c,g->pk,g->pkn);
}
static int is_core(WCHAR c){ for(int i=0;g_core[i];i++) if(g_core[i]==c) return 1; return 0; }

static void key_glyph(rk *k, const char *face, int h, WCHAR c){
  rk_reset(k); rk_s(k,"face",face); rk_i(k,"req_h",h); rk_x(k,"u",c);
  WCHAR s[2]={c,0}; rk_w(k,"c",s);
}

static void raw_bitmap(const char *table, const rk *k, int fi, const gres *g){
  if(g->ret==GDI_ERROR || !g->pkn || !g->packed_ok) return;
  int bpp = fi==0?1 : fi==1?4 : 8;
  rec_bitmap(table,k,(int)g->gm.gmBlackBoxX,(int)g->gm.gmBlackBoxY,bpp,g->pk,g->pkn);
}

/* raw unhinted bytes only at these heights (keeps the volume modest) */
static int unhinted_raw_h(int h){ return h==-11 || h==-12 || h==-16 || h==-24; }

void sec_ggo(void){
  rec_schema("gm","face req_h u c","ret gi bbx:px bby:px ox:px oy:px incx:px incy:px bm_len:B bm_sha g2_len:B g2_sha g4_len:B g4_sha g8_len:B g8_sha gm_same gi_same");
  rec_schema("gmf","face req_h u c fmt","bbx:px bby:px ox:px oy:px incx:px incy:px");
  if(g_full) rec_schema("gmu","face req_h u c","ret bbx:px bby:px ox:px oy:px incx:px incy:px bm_len:B bm_sha g8_len:B g8_sha gm_same_hinted");
  rec_note("gm: *_len = GGO return value (bytes incl. DWORD row padding); *_sha = first 64 bits of SHA-256 of the repacked glyph "
           "(byte-aligned rows: bm 1bpp, g2 4bpp levels 0..4, g4/g8 8bpp levels 0..16/0..64); gm_same bit i (bm=1 g2=2 g4=4 g8=8) = "
           "that format's GLYPHMETRICS equal GGO_METRICS; gi_same bit (metrics=16, bm=1 g2=2 g4=4 g8=8) = GGO_GLYPH_INDEX with "
           "GetGlyphIndicesW's index gives identical metrics+bytes; ggo_all H line = SHA-256 over (u, ret, 6 metrics, len, bytes) "
           "for every char of the set in order; B pixel values are raw GGO levels");
  HDC dc=CreateCompatibleDC(NULL);
  const int *hs=g_full?H_FULL:H_QUICK; int nh=g_full?13:3;
  int mismatch_notes=0;
  for(int fi_face=0;fi_face<g_nfaces;fi_face++){
    if(!gb_face_on(fi_face)) continue;
    const gb_face *F=&g_faces[fi_face];
    for(int hi=0;hi<nh;hi++){
      int h=hs[hi];
      HFONT f=gb_font(F->face,h,FW_NORMAL,0,0,0,DEFAULT_QUALITY), o=(HFONT)SelectObject(dc,f);
      if(F->raster){
        /* raster faces have no outlines: one row ('A') documents what GGO does */
        gres m; ggo(dc,'A',-1,0,&m); rk k; key_glyph(&k,F->face,h,'A');
        rv v; rv_reset(&v); if(m.ret==GDI_ERROR) rv_na(&v); else rv_u(&v,m.ret); rv_na(&v); rv_gm(&v,&m);
        gres b; ggo(dc,'A',3,0,&b); for(int i=0;i<3;i++){ rv_na(&v); rv_na(&v); } rv_lensha(&v,&b); rv_na(&v); rv_na(&v);
        rec_row("gm",&k,&v); gres_free(&m); gres_free(&b);
        SelectObject(dc,o); DeleteObject(f); continue;
      }
      /* agg[hint][set: 0 = this face's set, 1 = core subset (same order for every face)][fmt] */
      rec_sha256_ctx agg[2][2][5]; for(int u=0;u<2;u++) for(int s2=0;s2<2;s2++) for(int j=0;j<5;j++) rec_sha256_init(&agg[u][s2][j]);
      int full_set = g_full && !F->alias;
      const WCHAR *set = full_set ? g_all : g_core; int nset = full_set ? g_nall : (int)wcslen(g_core);
      for(int ci=0;ci<nset;ci++){
        WCHAR c=set[ci]; WORD gi=0xFFFF; DWORD gr=GetGlyphIndicesW(dc,&c,1,&gi,GGI_MARK_NONEXISTING_GLYPHS);
        gres m, b[4], mi, bi;
        int cc=is_core(c);
        ggo(dc,c,-1,0,&m); agg_add(&agg[0][0][0],c,&m); if(cc) agg_add(&agg[0][1][0],c,&m);
        int gm_same=0, gi_same=0;
        for(int j=0;j<4;j++){ ggo(dc,c,j,0,&b[j]); agg_add(&agg[0][0][j+1],c,&b[j]); if(cc) agg_add(&agg[0][1][j+1],c,&b[j]); if(b[j].ret!=GDI_ERROR && gm_eq(&b[j].gm,&m.gm)) gm_same|=1<<j;
          if(!b[j].packed_ok && b[j].ret!=GDI_ERROR && mismatch_notes<20){ mismatch_notes++; rec_note("ggo size mismatch face=%s h=%d u=%x fmt=%s ret=%lu bbx=%u bby=%u",F->face,h,c,FMT_N[j],(unsigned long)b[j].ret,b[j].gm.gmBlackBoxX,b[j].gm.gmBlackBoxY); } }
        if(gr!=GDI_ERROR && gi!=0xFFFF){
          ggo(dc,gi,-1,GGO_GLYPH_INDEX,&mi); if(gres_eq(&mi,&m)) gi_same|=16; gres_free(&mi);
          for(int j=0;j<4;j++){ ggo(dc,gi,j,GGO_GLYPH_INDEX,&bi); if(gres_eq(&bi,&b[j])) gi_same|=1<<j; gres_free(&bi); }
        }
        rk k; key_glyph(&k,F->face,h,c);
        rv v; rv_reset(&v); if(m.ret==GDI_ERROR) rv_na(&v); else rv_u(&v,m.ret);
        if(gr==GDI_ERROR) rv_na(&v); else rv_u(&v,gi);
        rv_gm(&v,&m); for(int j=0;j<4;j++) rv_lensha(&v,&b[j]); rv_x(&v,gm_same); rv_x(&v,gi_same);
        rec_row("gm",&k,&v);
        for(int j=0;j<4;j++) if(b[j].ret!=GDI_ERROR && !(gm_same&(1<<j))){
          rk k2=k; rk_s(&k2,"fmt",FMT_N[j]); rv w; rv_reset(&w); rv_gm(&w,&b[j]); rec_row("gmf",&k2,&w); }
        if(is_core(c) && !F->alias){
          raw_bitmap("ggo_bm",&k,0,&b[0]); raw_bitmap("ggo_g2",&k,1,&b[1]); raw_bitmap("ggo_g4",&k,2,&b[2]); raw_bitmap("ggo_g8",&k,3,&b[3]);
        }
        if(g_full){
          gres um, ub[4]; ggo(dc,c,-1,GGO_UNHINTED,&um); agg_add(&agg[1][0][0],c,&um); if(cc) agg_add(&agg[1][1][0],c,&um);
          for(int j=0;j<4;j++){ ggo(dc,c,j,GGO_UNHINTED,&ub[j]); agg_add(&agg[1][0][j+1],c,&ub[j]); if(cc) agg_add(&agg[1][1][j+1],c,&ub[j]); }
          if(!F->alias){
            rv u; rv_reset(&u); if(um.ret==GDI_ERROR) rv_na(&u); else rv_u(&u,um.ret); rv_gm(&u,&um);
            rv_lensha(&u,&ub[0]); rv_lensha(&u,&ub[3]); rv_i(&u,gres_eq(&ub[3],&b[3]) && gres_eq(&ub[0],&b[0]) && gm_eq(&um.gm,&m.gm));
            rec_row("gmu",&k,&u);
            if(is_core(c) && unhinted_raw_h(h)){ raw_bitmap("ggou_bm",&k,0,&ub[0]); raw_bitmap("ggou_g8",&k,3,&ub[3]); }
          }
          gres_free(&um); for(int j=0;j<4;j++) gres_free(&ub[j]);
        }
        gres_free(&m); for(int j=0;j<4;j++) gres_free(&b[j]);
      }
      static const char *AGG_N[5]={"metrics","bm","g2","g4","g8"};
      for(int u=0;u<(g_full?2:1);u++) for(int s2=0;s2<(full_set?2:1);s2++) for(int j=0;j<5;j++){
        uint8_t d[32]; rec_sha256_final(&agg[u][s2][j],d);
        rk k; rk_reset(&k); rk_s(&k,"face",F->face); rk_i(&k,"req_h",h); rk_s(&k,"set",(full_set&&!s2)?"all":"core");
        rk_s(&k,"fmt",AGG_N[j]); rk_s(&k,"hint",u?"unhinted":"hinted");
        rec_hash("ggo_all",&k,d,32);  /* hash of the digest: stable 64-bit id of the whole set */
      }
      SelectObject(dc,o); DeleteObject(f);
      rec_flush();
    }
  }
  DeleteDC(dc);
}
