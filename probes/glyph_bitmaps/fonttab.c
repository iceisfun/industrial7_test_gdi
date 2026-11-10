/* glyph_bitmaps / fonttab: GetFontData tables of the realised UI fonts (explains hinting, gasp, embedded strikes).
 * Font selected at lfHeight -12 (table contents do not depend on the size); regular, bold (700) and italic requests.
 *   ffile  whole font (dwTable 0): length, sha, sfnt version, numTables, directory tags
 *   ftab   one row per table: requested list + every tag present in the directory; len (GetFontData size query) + sha16
 *   fhead/fhhea/fos2/fmaxp  parsed header fields (font design units)
 *   gasp   rangeMaxPPEM -> behavior flags (1 gridfit, 2 dogray, 4 symmetric gridfit, 8 symmetric smoothing)
 *   eblc   embedded bitmap strikes: ppemX ppemY glyph range bitDepth flags (empty => no EBLC table)
 *   hdmx   pixel sizes with device records (pixelSize:maxWidth list)
 *   vdmxr / vdmx  VDMX ratio groups and per-ppem yMax/yMin (ppem <= 48)
 */
#include "gb.h"

#define TAG(a,b,c,d) ((DWORD)(a) | ((DWORD)(b)<<8) | ((DWORD)(c)<<16) | ((DWORD)(d)<<24))
static unsigned u16(const uint8_t *p, size_t n, size_t o){ return o+2<=n ? (unsigned)(p[o]<<8|p[o+1]) : 0; }
static int s16(const uint8_t *p, size_t n, size_t o){ return (int)(short)u16(p,n,o); }
static unsigned long u32(const uint8_t *p, size_t n, size_t o){ return o+4<=n ? ((unsigned long)p[o]<<24|(unsigned long)p[o+1]<<16|(unsigned long)p[o+2]<<8|p[o+3]) : 0; }
static void tagstr(char o[5], DWORD t){ for(int i=0;i<4;i++){ char c=(char)((t>>(8*i))&0xFF); o[i]=c; } o[4]=0; }

static uint8_t *get_table(HDC dc, DWORD tag, DWORD *len){
  DWORD n=GetFontData(dc,tag,0,NULL,0); *len=n;
  if(n==GDI_ERROR || n==0) return NULL;
  uint8_t *b=(uint8_t*)malloc(n); DWORD r=GetFontData(dc,tag,0,b,n);
  if(r!=n){ free(b); *len=GDI_ERROR; return NULL; }
  return b;
}

static const char *REQ[] = {"cmap","hmtx","hhea","OS/2","head","maxp","EBLC","EBDT","EBSC","gasp","hdmx","LTSH","VDMX","cvt ","fpgm","prep","kern","name","post","glyf","loca","GDEF","GPOS","GSUB","DSIG"};

static void one_font(HDC dc, const char *face, int wt, int it){
  HFONT f=gb_font(face,-12,wt,it,0,0,DEFAULT_QUALITY), o=(HFONT)SelectObject(dc,f);
  WCHAR got[LF_FACESIZE]=L""; GetTextFaceW(dc,LF_FACESIZE,got);
  rk kb; rk_reset(&kb); rk_s(&kb,"face",face); rk_i(&kb,"wt",wt); rk_i(&kb,"it",it);
  DWORD flen; uint8_t *file=get_table(dc,0,&flen);
  DWORD tags[64]; int nt=0;
  { rv v; rv_reset(&v); rv_w(&v,got);
    if(!file){ rv_na(&v); rv_na(&v); rv_na(&v); rv_na(&v); rv_na(&v); }
    else { char h[17]; gb_short_hash(h,file,flen); rv_u(&v,flen); rv_raw(&v,h); rv_x(&v,u32(file,flen,0));
      unsigned n=u16(file,flen,4); rv_u(&v,n);
      char tl[64*5+1]; int tlen=0; tl[0]=0;
      for(unsigned i=0;i<n && nt<64;i++){ DWORD t=0; for(int j=0;j<4;j++){ size_t off=12+16*i+j; t|=(DWORD)(off<flen?file[off]:0)<<(8*j); }
        tags[nt++]=t; char ts[5]; tagstr(ts,t); tlen+=snprintf(tl+tlen,sizeof tl-tlen,"%s%s",i?",":"",ts); }
      rv_s(&v,tl); }
    rec_row("ffile",&kb,&v); }
  /* table list: requested ones, then present ones not requested */
  DWORD all[96]; int na=0;
  for(unsigned i=0;i<sizeof REQ/sizeof*REQ;i++) all[na++]=TAG(REQ[i][0],REQ[i][1],REQ[i][2],REQ[i][3]);
  for(int i=0;i<nt;i++){ int dup=0; for(int j=0;j<na;j++) if(all[j]==tags[i]) dup=1; if(!dup && na<96) all[na++]=tags[i]; }
  for(int i=0;i<na;i++){
    DWORD len; uint8_t *t=get_table(dc,all[i],&len); char ts[5]; tagstr(ts,all[i]);
    rk k=kb; rk_s(&k,"tag",ts); rv v; rv_reset(&v);
    if(len==GDI_ERROR){ rv_na(&v); rv_na(&v); } else { char h[17]; gb_short_hash(h,t?t:(const uint8_t*)"",t?len:0); rv_u(&v,len); rv_raw(&v,h); }
    rec_row("ftab",&k,&v);
    if(!t) continue;
    size_t n=len; rv p; rv_reset(&p);
    if(!memcmp(ts,"head",4)){
      rv_x(&p,u32(t,n,4)); rv_x(&p,u32(t,n,8)); rv_x(&p,u16(t,n,16)); rv_u(&p,u16(t,n,18)); rv_x(&p,u16(t,n,44)); rv_u(&p,u16(t,n,46)); rv_i(&p,s16(t,n,50));
      rv_i(&p,s16(t,n,36)); rv_i(&p,s16(t,n,38)); rv_i(&p,s16(t,n,40)); rv_i(&p,s16(t,n,42));
      rec_row("fhead",&kb,&p);
    } else if(!memcmp(ts,"hhea",4)){
      rv_i(&p,s16(t,n,4)); rv_i(&p,s16(t,n,6)); rv_i(&p,s16(t,n,8)); rv_u(&p,u16(t,n,10)); rv_u(&p,u16(t,n,34)); rec_row("fhhea",&kb,&p);
    } else if(!memcmp(ts,"OS/2",4)){
      unsigned ver=u16(t,n,0); rv_u(&p,ver); rv_i(&p,s16(t,n,2)); rv_u(&p,u16(t,n,4)); rv_x(&p,u16(t,n,8)); rv_x(&p,u16(t,n,62));
      rv_i(&p,s16(t,n,68)); rv_i(&p,s16(t,n,70)); rv_i(&p,s16(t,n,72)); rv_u(&p,u16(t,n,74)); rv_u(&p,u16(t,n,76));
      if(ver>=1) rv_x(&p,u32(t,n,78)); else rv_na(&p);
      if(ver>=2){ rv_i(&p,s16(t,n,86)); rv_i(&p,s16(t,n,88)); } else { rv_na(&p); rv_na(&p); }
      rec_row("fos2",&kb,&p);
    } else if(!memcmp(ts,"maxp",4)){
      rv_u(&p,u16(t,n,4)); rec_row("fmaxp",&kb,&p);
    } else if(!memcmp(ts,"gasp",4)){
      unsigned nr=u16(t,n,2);
      for(unsigned r=0;r<nr && r<64;r++){ rk k2=kb; rk_i(&k2,"i",r); rv g; rv_reset(&g); rv_u(&g,u16(t,n,4+4*r)); rv_x(&g,u16(t,n,6+4*r)); rec_row("gasp",&k2,&g); }
    } else if(!memcmp(ts,"EBLC",4)){
      unsigned long ns=u32(t,n,4);
      for(unsigned long r=0;r<ns && r<128;r++){ size_t b=8+48*(size_t)r; rk k2=kb; rk_i(&k2,"i",(long long)r); rv e; rv_reset(&e);
        if(b+48>n){ for(int i=0;i<10;i++) rv_na(&e); rec_row("eblc",&k2,&e); break; }   /* truncated table: one ! row */
        rv_u(&e,t[b+44]); rv_u(&e,t[b+45]); rv_u(&e,u16(t,n,b+40)); rv_u(&e,u16(t,n,b+42)); rv_u(&e,t[b+46]);
        rv_x(&e,t[b+47]); rv_u(&e,u32(t,n,b+8));
        rv_i(&e,(signed char)t[b+16]); rv_i(&e,(signed char)t[b+17]); rv_u(&e,t[b+18]);
        rec_row("eblc",&k2,&e); }
    } else if(!memcmp(ts,"hdmx",4)){
      unsigned nr=u16(t,n,2); unsigned long sz=u32(t,n,4); char lst[2048]; int ll=0; lst[0]=0;
      int ovf=0;
      for(unsigned r=0;r<nr && r<255 && !ovf;r++){ size_t b=8+(size_t)sz*r; if(b+1<n){
          int w=snprintf(lst+ll,sizeof lst-(size_t)ll,"%s%u:%u",ll?",":"",t[b],t[b+1]);
          if(w<0 || (size_t)(ll+w)>=sizeof lst){ lst[ll]=0; ovf=1; } else ll+=w; } }
      rv_u(&p,u16(t,n,0)); rv_u(&p,nr); rv_u(&p,sz); if(ovf) rv_na(&p); else rv_raw(&p,ll?lst:"~"); rec_row("hdmx",&kb,&p);
    } else if(!memcmp(ts,"VDMX",4)){
      unsigned nratios=u16(t,n,4);
      for(unsigned r=0;r<nratios && r<16;r++){
        size_t rb=6+4*r; unsigned off=u16(t,n,6+4*nratios+2*r);
        unsigned recs=u16(t,n,off); unsigned st=off+2<n?t[off+2]:0, en=off+3<n?t[off+3]:0;
        rk k2=kb; rk_i(&k2,"ratio",r); rv g; rv_reset(&g);
        rv_u(&g,rb<n?t[rb]:0); rv_u(&g,rb+1<n?t[rb+1]:0); rv_u(&g,rb+2<n?t[rb+2]:0); rv_u(&g,rb+3<n?t[rb+3]:0); rv_u(&g,recs); rv_u(&g,st); rv_u(&g,en);
        rec_row("vdmxr",&k2,&g);
        for(unsigned e=0;e<recs && e<256;e++){ size_t eb=off+4+6*e; unsigned ppem=u16(t,n,eb); if(ppem>48) continue;
          rk k3=k2; rk_i(&k3,"ppem",ppem); rv x; rv_reset(&x); rv_i(&x,s16(t,n,eb+2)); rv_i(&x,s16(t,n,eb+4)); rec_row("vdmx",&k3,&x); }
      }
    }
    free(t);
  }
  free(file);
  SelectObject(dc,o); DeleteObject(f);
}

void sec_fonttab(void){
  rec_schema("ffile","face wt it","got len:B sha sfnt_ver num_tables tags");
  rec_schema("ftab","face wt it tag","len:B sha");
  rec_schema("fhead","face wt it","fontrev csum_adj flags upem:fu macstyle lowest_rec_ppem:px loca_fmt xmin:fu ymin:fu xmax:fu ymax:fu");
  rec_schema("fhhea","face wt it","asc:fu desc:fu gap:fu adv_max:fu num_hmetrics");
  rec_schema("fos2","face wt it","ver avg_w:fu wt_class fstype fssel typo_asc:fu typo_desc:fu typo_gap:fu win_asc:fu win_desc:fu codepage1 xh:fu caph:fu");
  rec_schema("fmaxp","face wt it","num_glyphs");
  rec_schema("gasp","face wt it i","max_ppem:px behavior");
  rec_schema("eblc","face wt it i","ppem_x:px ppem_y:px start_gid end_gid bit_depth flags num_subtables hori_asc:px hori_desc:px hori_wmax:px");
  rec_schema("hdmx","face wt it","ver num_records rec_size:B sizes");
  rec_schema("vdmxr","face wt it ratio","charset x y_start y_end recs start_sz:px end_sz:px");
  rec_schema("vdmx","face wt it ratio ppem","ymax:px ymin:px");
  HDC dc=CreateCompatibleDC(NULL);
  for(int fi=0;fi<g_nfaces;fi++){
    if(!gb_face_on(fi)) continue;
    one_font(dc,g_faces[fi].face,FW_NORMAL,0);
    if(g_full){ one_font(dc,g_faces[fi].face,FW_BOLD,0); one_font(dc,g_faces[fi].face,FW_NORMAL,1); }
    rec_flush();
  }
  DeleteDC(dc);
}
