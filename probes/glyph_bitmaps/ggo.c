/* glyph_bitmaps / ggo: GetGlyphOutlineW rasterisation fingerprints.
 *
 * For each outline face x height (H_FULL) x char (g_all; core only for the alias faces and in --quick):
 *   GGO_METRICS (GLYPHMETRICS), GGO_BITMAP, GGO_GRAY2_BITMAP, GGO_GRAY4_BITMAP, GGO_GRAY8_BITMAP, identity MAT2,
 *   by character code and again by glyph index (GGO_GLYPH_INDEX, index from GetGlyphIndicesW); GGO_NATIVE outlines hinted
 *   and with GGO_UNHINTED (gmo); the bitmap formats again with GGO_UNHINTED, recorded only as a parity mask: the 22H2
 *   decomp (win32kfull!GreGetGlyphOutlineInternal) passes GGO_UNHINTED (0x100) only to PFFOBJ::QueryTrueTypeOutline,
 *   i.e. GGO_NATIVE/GGO_BEZIER; GGO_METRICS and the bitmap formats ignore it, so their unhinted data is expected to equal
 *   the hinted data (unh_same 0x1f) and is not stored twice.
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

/* GGO_NATIVE (TTPOLYGONHEADER/TTPOLYCURVE list, FIXED 16.16 device px, y up, relative to the glyph origin). ret = size
 * query result (GDI_ERROR = failed, 0 = empty glyph); *out = fetched bytes (NULL when ret is 0 or failed). */
static DWORD ggo_native(HDC dc, UINT ch, UINT extra, uint8_t **out){
  static const MAT2 I = { {0,1}, {0,0}, {0,0}, {0,1} };
  GLYPHMETRICS gm; memset(&gm,0,sizeof gm); *out=NULL;
  DWORD n=GetGlyphOutlineW(dc,ch,GGO_NATIVE|extra,&gm,0,NULL,&I);
  if(n==GDI_ERROR || n==0) return n;
  uint8_t *b=(uint8_t*)calloc(1,n); if(!b) return GDI_ERROR;
  if(GetGlyphOutlineW(dc,ch,GGO_NATIVE|extra,&gm,n,b,&I)==GDI_ERROR){ free(b); return GDI_ERROR; }
  *out=b; return n;
}
static void rv_nat(rv *v, DWORD n, const uint8_t *b){
  if(n==GDI_ERROR){ rv_na(v); rv_na(v); return; }
  char h[17]; rv_u(v,n); gb_short_hash(h,b?b:(const uint8_t*)"",b?n:0); rv_raw(v,h);
}
static void agg_nat(rec_sha256_ctx *c, UINT ch, DWORD n, const uint8_t *b){
  int32_t hdr[2]; hdr[0]=(int32_t)ch; hdr[1]=(int32_t)n; rec_sha256_update(c,hdr,sizeof hdr); if(b && n!=GDI_ERROR) rec_sha256_update(c,b,n);
}
#define NAT_RAW_MAX 3000   /* bytes; 2x as hex must stay well inside rec.h RV_MAX (a token that does not fit is dropped) */

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

/* raw GGO_NATIVE bytes only at these heights (keeps the volume modest) */
static int unhinted_raw_h(int h){ return h==-11 || h==-12 || h==-16 || h==-24; }

void sec_ggo(void){
  rec_schema("gm","face req_h u c","ret gi bbx:px bby:px ox:px oy:px incx:px incy:px bm_len:B bm_sha g2_len:B g2_sha g4_len:B g4_sha g8_len:B g8_sha gm_same gi_same");
  rec_schema("gmf","face req_h u c fmt","bbx:px bby:px ox:px oy:px incx:px incy:px");
  rec_schema("gmo","face req_h u c","nat_len:B nat_sha natu_len:B natu_sha nat_same unh_same");
  rec_schema("ggo_nat","face req_h u c hint","len:B hex");
  rec_note("gmo: GGO_NATIVE outline size+hash hinted (nat) and with GGO_UNHINTED (natu); nat_same = 1 if both are identical; "
           "unh_same = mask (metrics=16 bm=1 g2=2 g4=4 g8=8) of formats whose GGO_UNHINTED result (metrics+bytes) equals the "
           "hinted one (decomp: 0x1f expected). ggo_nat: raw GGO_NATIVE bytes as hex (TTPOLYGONHEADER/TTPOLYCURVE, "
           "little-endian, FIXED 16.16 px, y up, origin = glyph origin) for core chars at -11 -12 -16 -24; ! if > 3000 bytes");
  rec_note("gm: *_len = GGO return value (bytes incl. DWORD row padding); *_sha = first 64 bits of SHA-256 of the repacked glyph "
           "(byte-aligned rows: bm 1bpp, g2 4bpp levels 0..4, g4/g8 8bpp levels 0..16/0..64); gm_same bit i (bm=1 g2=2 g4=4 g8=8) = "
           "that format's GLYPHMETRICS equal GGO_METRICS; gi_same bit (metrics=16, bm=1 g2=2 g4=4 g8=8) = GGO_GLYPH_INDEX with "
           "GetGlyphIndicesW's index gives identical metrics+bytes; ggo_all H line = SHA-256 over (u, ret, 6 metrics, len, bytes) "
           "for every char of the set in order (fmt=native: SHA-256 over (u, ret, bytes)); B pixel values are raw GGO levels");
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
      /* agg[set: 0 = this face's set, 1 = core subset (same order for every face)][fmt: metrics bm g2 g4 g8 native nativeU] */
      rec_sha256_ctx agg[2][7]; for(int s2=0;s2<2;s2++) for(int j=0;j<7;j++) rec_sha256_init(&agg[s2][j]);
      int full_set = g_full && !F->alias;
      const WCHAR *set = full_set ? g_all : g_core; int nset = full_set ? g_nall : (int)wcslen(g_core);
      for(int ci=0;ci<nset;ci++){
        WCHAR c=set[ci]; WORD gi=0xFFFF; DWORD gr=GetGlyphIndicesW(dc,&c,1,&gi,GGI_MARK_NONEXISTING_GLYPHS);
        gres m, b[4], mi, bi;
        int cc=is_core(c);
        ggo(dc,c,-1,0,&m); agg_add(&agg[0][0],c,&m); if(cc) agg_add(&agg[1][0],c,&m);
        int gm_same=0, gi_same=0;
        for(int j=0;j<4;j++){ ggo(dc,c,j,0,&b[j]); agg_add(&agg[0][j+1],c,&b[j]); if(cc) agg_add(&agg[1][j+1],c,&b[j]); if(b[j].ret!=GDI_ERROR && gm_eq(&b[j].gm,&m.gm)) gm_same|=1<<j;
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
        { /* outlines hinted / unhinted, and the GGO_UNHINTED parity of metrics + bitmaps */
          uint8_t *nb=NULL, *nu=NULL; DWORD nn=ggo_native(dc,c,0,&nb), nnu=ggo_native(dc,c,GGO_UNHINTED,&nu);
          agg_nat(&agg[0][5],c,nn,nb); agg_nat(&agg[0][6],c,nnu,nu); if(cc){ agg_nat(&agg[1][5],c,nn,nb); agg_nat(&agg[1][6],c,nnu,nu); }
          int unh=0; gres ux; ggo(dc,c,-1,GGO_UNHINTED,&ux); if(gres_eq(&ux,&m)) unh|=16; gres_free(&ux);
          for(int j=0;j<4;j++){ ggo(dc,c,j,GGO_UNHINTED,&ux); if(gres_eq(&ux,&b[j])) unh|=1<<j; gres_free(&ux); }
          if(!F->alias){
            rv u; rv_reset(&u); rv_nat(&u,nn,nb); rv_nat(&u,nnu,nu);
            rv_i(&u, nn==nnu && (nn==GDI_ERROR || !nb || !memcmp(nb,nu,nn))); rv_x(&u,unh); rec_row("gmo",&k,&u);
            if(cc && unhinted_raw_h(h)) for(int hu=0;hu<2;hu++){
              DWORD n2=hu?nnu:nn; const uint8_t *b2=hu?nu:nb;
              rk k2=k; rk_s(&k2,"hint",hu?"unhinted":"hinted"); rv x; rv_reset(&x);
              if(n2==GDI_ERROR){ rv_na(&x); rv_na(&x); }
              else { rv_u(&x,n2);
                if(!b2 || !n2) rv_raw(&x,"~");
                else if(n2>NAT_RAW_MAX) rv_na(&x);
                else { char *hx=(char*)malloc(2*(size_t)n2+1); rec_hex(hx,b2,n2); rv_raw(&x,hx); free(hx); } }
              rec_row("ggo_nat",&k2,&x);
            }
          }
          free(nb); free(nu);
        }
        gres_free(&m); for(int j=0;j<4;j++) gres_free(&b[j]);
      }
      static const char *AGG_N[7]={"metrics","bm","g2","g4","g8","native","native"};
      for(int s2=0;s2<(full_set?2:1);s2++) for(int j=0;j<7;j++){
        uint8_t d[32]; rec_sha256_final(&agg[s2][j],d);
        rk k; rk_reset(&k); rk_s(&k,"face",F->face); rk_i(&k,"req_h",h); rk_s(&k,"set",(full_set&&!s2)?"all":"core");
        rk_s(&k,"fmt",AGG_N[j]); rk_s(&k,"hint",j==6?"unhinted":"hinted");
        rec_hash("ggo_all",&k,d,32);  /* hash of the digest: stable 64-bit id of the whole set */
      }
      SelectObject(dc,o); DeleteObject(f);
      rec_flush();
    }
  }
  DeleteDC(dc);
}
