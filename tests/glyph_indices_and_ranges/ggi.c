#include "t.h"
#include "gir.h"

/* ---- cmap reading through GetFontData ---- */
static unsigned rd16(const BYTE *p){ return (p[0]<<8)|p[1]; }
static unsigned long rd32(const BYTE *p){ return ((unsigned long)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }

BYTE *gir_fontdata(HDC dc, DWORD tag, DWORD *len){
  DWORD n=U(GetFontData)(dc,tag,0,NULL,0);
  if(n==GDI_ERROR||n==0||n>(64u<<20)){ *len=0; return NULL; }
  BYTE *b=(BYTE*)malloc(n); if(!b){ *len=0; return NULL; }
  if(U(GetFontData)(dc,tag,0,b,n)!=n){ free(b); *len=0; return NULL; }
  *len=n; return b;
}

/* Fills gid[0..0xFFFF] with the glyph index the font's Unicode cmap gives each BMP code unit (0 = absent).
 * Reads the (3,10) format 12 subtable if present, else (3,1) format 4. Returns 1 on success. */
int gir_read_cmap(HDC dc, WORD *gid){
  DWORD len; BYTE *t=gir_fontdata(dc,GIR_TAG('c','m','a','p'),&len); if(!t||len<4){ free(t); return 0; }
  memset(gid,0,65536*sizeof(WORD));
  unsigned nt=rd16(t+2); unsigned long off4=0, off12=0;
  for(unsigned i=0;i<nt && 4+8u*(i+1)<=len;i++){
    unsigned pid=rd16(t+4+8*i), eid=rd16(t+6+8*i); unsigned long o=rd32(t+8+8*i);
    if(o+4>len) continue;
    unsigned fmt=rd16(t+o);
    if(pid==3&&eid==1&&fmt==4) off4=o;
    if(pid==3&&eid==10&&fmt==12) off12=o;
  }
  int ok=0;
  if(off12){
    unsigned long ng=rd32(t+off12+12);
    for(unsigned long g=0;g<ng && off12+16+12*(g+1)<=len;g++){
      const BYTE *r=t+off12+16+12*g; unsigned long s=rd32(r), e=rd32(r+4), sg=rd32(r+8);
      for(unsigned long c=s;c<=e&&c<0x10000;c++) gid[c]=(WORD)(sg+(c-s));
    }
    ok=1;
  } else if(off4){
    const BYTE *s=t+off4; unsigned segx2=rd16(s+6), seg=segx2/2;
    const BYTE *endc=s+14, *startc=s+16+segx2, *delta=startc+segx2, *ro=delta+segx2;
    if(off4+16+4ul*segx2<=len){
      for(unsigned i=0;i<seg;i++){
        unsigned e=rd16(endc+2*i), st=rd16(startc+2*i), d=rd16(delta+2*i), r=rd16(ro+2*i);
        for(unsigned c=st;c<=e&&c<0xFFFF;c++){
          unsigned g;
          if(r==0) g=(c+d)&0xFFFF;
          else { const BYTE *gp=ro+2*i+r+2*(c-st); if(gp+2>t+len) continue; g=rd16(gp); if(g) g=(g+d)&0xFFFF; }
          gid[c]=(WORD)g;
        }
      }
      ok=1;
    }
  }
  free(t); return ok;
}

/* Family name (nameID 1, platform 3) from the 'name' table, as UTF-16 into out. */
int gir_name_family(HDC dc, WCHAR *out, int cap){
  DWORD len; BYTE *t=gir_fontdata(dc,GIR_TAG('n','a','m','e'),&len); if(!t||len<6){ free(t); return 0; }
  unsigned n=rd16(t+2), so=rd16(t+4); int ok=0;
  for(unsigned i=0;i<n && 6+12u*(i+1)<=len;i++){
    const BYTE *r=t+6+12*i;
    if(rd16(r)==3 && rd16(r+6)==1 && rd16(r+2)==1){
      unsigned l=rd16(r+8), o=so+rd16(r+10); if(o+l>len) continue;
      int k=0; for(unsigned j=0;j+1<l&&k<cap-1;j+=2) out[k++]=(WCHAR)rd16(t+o+j); out[k]=0; ok=1; break;
    }
  }
  free(t); return ok;
}

HFONT gir_mkfont(const WCHAR *face,int h,int wt,int cs){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=wt; lf.lfCharSet=(BYTE)cs; lstrcpynW(lf.lfFaceName,face,LF_FACESIZE);
  return U(CreateFontIndirectW)(&lf);
}

/* Open a DC with the named TrueType face selected and its identity verified three ways:
 * GetTextFaceW, the family name of the OUTLINETEXTMETRICW and the nameID 1 record of the font's 'name' table.
 * Returns 1, or 0 with a reason in why. */
int gir_open(gir_dc *d, const WCHAR *face, int h, char *why, size_t cap){
  memset(d,0,sizeof *d);
  d->dc=U(GetDC)(NULL); if(!d->dc){ snprintf(why,cap,"GetDC(NULL) failed"); return 0; }
  d->f=gir_mkfont(face,h,400,DEFAULT_CHARSET); d->old=(HFONT)U(SelectObject)(d->dc,d->f);
  WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(d->dc,LF_FACESIZE,got);
  if(lstrcmpiW(got,face)){ snprintf(why,cap,"%ls realised as %ls", face, got); return 0; }
  TEXTMETRICW tm; if(!U(GetTextMetricsW)(d->dc,&tm) || !(tm.tmPitchAndFamily&TMPF_TRUETYPE)){ snprintf(why,cap,"%ls not realised as TrueType", face); return 0; }
  UINT sz=U(GetOutlineTextMetricsW)(d->dc,0,NULL); if(!sz||sz>65536){ snprintf(why,cap,"%ls: no outline metrics", face); return 0; }
  OUTLINETEXTMETRICW *o=(OUTLINETEXTMETRICW*)malloc(sz);
  if(!U(GetOutlineTextMetricsW)(d->dc,sz,o)){ free(o); snprintf(why,cap,"%ls: GetOutlineTextMetricsW failed", face); return 0; }
  int ok=!lstrcmpiW((WCHAR*)((BYTE*)o+(size_t)o->otmpFamilyName),face); free(o);
  if(!ok){ snprintf(why,cap,"%ls: outline family name differs", face); return 0; }
  WCHAR nm[128]=L""; if(!gir_name_family(d->dc,nm,128) || lstrcmpiW(nm,face)){ snprintf(why,cap,"%ls: 'name' table family is %ls", face, nm); return 0; }
  return 1;
}
void gir_close(gir_dc *d){
  if(d->dc){ if(d->old) U(SelectObject)(d->dc,d->old); U(ReleaseDC)(NULL,d->dc); }
  if(d->f) U(DeleteObject)(d->f);
  memset(d,0,sizeof *d);
}

static const WCHAR *k_tt[]={L"Arial",L"Segoe UI"};
static WORD g_gid[65536];

/* first BMP code unit from a preference list that the font's cmap lacks, else first absent scalar >= 0x0370 */
static int pick_absent(const WORD *gid){
  static const int pref[]={0x0378,0x4E00,0xE000,0x1100,0x0FFF,0xA000,0x3042};
  for(unsigned i=0;i<sizeof pref/sizeof*pref;i++) if(!gid[pref[i]]) return pref[i];
  for(int c=0x370;c<0xD800;c++) if(!gid[c]) return c;
  return -1;
}

TEST(ggi_pair_marked){
  int done=0;
  for(unsigned fi=0;fi<2;fi++){
    gir_dc d; char why[160];
    if(!gir_open(&d,k_tt[fi],-16,why,sizeof why)){ t_note("%s",why); gir_close(&d); continue; }
    if(!gir_read_cmap(d.dc,g_gid)){ t_note("%ls: no readable Unicode cmap", k_tt[fi]); gir_close(&d); continue; }
    int pres=0x41, abs_=pick_absent(g_gid);
    CHECKF(g_gid[pres]!=0, "%ls: cmap lacks U+0041", k_tt[fi]);
    if(abs_<0){ t_note("%ls: no absent BMP char", k_tt[fi]); gir_close(&d); continue; }
    WORD one[1]={0xAAAA}, oneabs[1]={0xAAAA};
    WCHAR cp=(WCHAR)pres, ca=(WCHAR)abs_;
    DWORD r1=U(GetGlyphIndicesW)(d.dc,&cp,1,one,GGI_MARK_NONEXISTING_GLYPHS);
    DWORD r2=U(GetGlyphIndicesW)(d.dc,&ca,1,oneabs,GGI_MARK_NONEXISTING_GLYPHS);
    t_note("%ls: present U+%04X cmap gid %u, single %u; absent U+%04X single %04X (ret %lu,%lu)", k_tt[fi],pres,g_gid[pres],one[0],abs_,oneabs[0],r1,r2);
    EXPECT_EQ(r1,1); EXPECT_EQ(r2,1);
    EXPECT_EQ(oneabs[0],0xFFFF);          /* the double check of the cmap read: the lookup agrees the char is absent */
    EXPECT_NE(one[0],0xFFFF);
    EXPECT_EQ(one[0],g_gid[pres]);
    /* pair, both orders */
    WCHAR pa[2]={cp,ca}, ap[2]={ca,cp}; WORD o1[2]={0xAAAA,0xAAAA}, o2[2]={0xAAAA,0xAAAA};
    DWORD n1=U(GetGlyphIndicesW)(d.dc,pa,2,o1,GGI_MARK_NONEXISTING_GLYPHS);
    DWORD n2=U(GetGlyphIndicesW)(d.dc,ap,2,o2,GGI_MARK_NONEXISTING_GLYPHS);
    EXPECT_EQ(n1,2); EXPECT_EQ(n2,2);
    EXPECT_EQ(o1[0],one[0]); EXPECT_EQ(o1[1],0xFFFF);
    EXPECT_EQ(o2[0],0xFFFF); EXPECT_EQ(o2[1],one[0]);
    /* flag off: record the absent marker, assert only count and present slot */
    WORD f1[2]={0xAAAA,0xAAAA}, f2[2]={0xAAAA,0xAAAA};
    DWORD m1=U(GetGlyphIndicesW)(d.dc,pa,2,f1,0);
    DWORD m2=U(GetGlyphIndicesW)(d.dc,ap,2,f2,0);
    t_note("%ls: flag off, absent U+%04X -> %04X (%04X reversed)", k_tt[fi], abs_, f1[1], f2[0]);
    EXPECT_EQ(m1,2); EXPECT_EQ(m2,2);
    EXPECT_EQ(f1[0],one[0]); EXPECT_EQ(f2[1],one[0]);
    done++;
    gir_close(&d);
  }
  if(!done) t_skip("no verified TrueType face (Arial, Segoe UI)");
}

/* ANSI variant: the bytes are converted with the code page of the DC's font charset (observed: Greek charset
 * font -> 1253), so the A result equals the W result of the converted characters. */
static void a_vs_w(HDC dc, int cp, const char *what){
  char s[256]; WCHAR w[256]; int n=0;
  for(int b=0x20;b<=0xFF;b++) s[n++]=(char)b;
  int nw=MultiByteToWideChar(cp,0,s,n,w,256);
  for(int fl=0;fl<2;fl++){
    WORD ga[256], gw[256]; memset(ga,0xAA,sizeof ga); memset(gw,0xAA,sizeof gw);
    DWORD fv=fl?GGI_MARK_NONEXISTING_GLYPHS:0;
    DWORD ra=U(GetGlyphIndicesA)(dc,s,n,ga,fv), rw=U(GetGlyphIndicesW)(dc,w,nw,gw,fv);
    int bad=0, first=-1; for(int i=0;i<n;i++) if(ga[i]!=gw[i]){ bad++; if(first<0) first=i; }
    CHECKF(ra==(DWORD)n && rw==(DWORD)nw && nw==n, "%s flag %d: counts A %lu W %lu n %d nw %d", what, fl, ra, rw, n, nw);
    CHECKF(bad==0, "%s flag %d: %d bytes differ (first 0x%02X: A %04X W %04X)", what, fl, bad, first<0?0:0x20+first, first<0?0:ga[first], first<0?0:gw[first]);
  }
}
TEST(ggi_ansi_equals_wide){
  gir_dc d; char why[160];
  if(!gir_open(&d,L"Arial",-16,why,sizeof why)){ t_skip("%s",why); gir_close(&d); return; }
  if(GetACP()!=1252){ t_skip("ANSI code page is %u, not 1252", GetACP()); gir_close(&d); return; }
  a_vs_w(d.dc,1252,"Arial ANSI charset, CP 1252");
  static WORD cm[65536]; WORD ga[1]={0xAAAA}; char eacute=(char)0xE9, euro=(char)0x80;
  if(gir_read_cmap(d.dc,cm)){
    EXPECT_EQ(U(GetGlyphIndicesA)(d.dc,&eacute,1,ga,GGI_MARK_NONEXISTING_GLYPHS),1); EXPECT_EQ(ga[0],cm[0xE9]);
    ga[0]=0xAAAA;
    EXPECT_EQ(U(GetGlyphIndicesA)(d.dc,&euro,1,ga,GGI_MARK_NONEXISTING_GLYPHS),1);   EXPECT_EQ(ga[0],cm[0x20AC]);
  }
  /* Greek-charset realisation of the same face: the A call follows the font charset's code page, not the ACP */
  HFONT g=gir_mkfont(L"Arial",-16,400,GREEK_CHARSET), o=(HFONT)U(SelectObject)(d.dc,g);
  WCHAR got[LF_FACESIZE]=L""; U(GetTextFaceW)(d.dc,LF_FACESIZE,got);
  if(lstrcmpiW(got,L"Arial")||U(GetTextCharset)(d.dc)!=GREEK_CHARSET) t_note("Greek charset not realised as Arial (%ls, %u)", got, U(GetTextCharset)(d.dc));
  else {
    a_vs_w(d.dc,1253,"Arial GREEK charset, CP 1253");
    char alpha=(char)0xE1; WORD a1[1]={0xAAAA}, w52[1]={0xAAAA}, walpha[1]={0xAAAA}; WCHAR w=L'\xE1', wa=L'\x3B1';
    EXPECT_EQ(U(GetGlyphIndicesA)(d.dc,&alpha,1,a1,GGI_MARK_NONEXISTING_GLYPHS),1);
    EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,&w,1,w52,GGI_MARK_NONEXISTING_GLYPHS),1);
    EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,&wa,1,walpha,GGI_MARK_NONEXISTING_GLYPHS),1);
    CHECKF(a1[0]!=w52[0], "byte 0xE1 under a Greek font equals U+00E1 (%04X), not U+03B1", a1[0]);
    CHECKF(a1[0]==walpha[0], "byte 0xE1 under a Greek font: glyph %04X, U+03B1 has %04X", a1[0], walpha[0]);
  }
  U(SelectObject)(d.dc,o); U(DeleteObject)(g);
  gir_close(&d);
}

TEST(ggi_count_and_arg_semantics){
  gir_dc d; char why[160];
  if(!gir_open(&d,L"Arial",-16,why,sizeof why)){ t_skip("%s",why); gir_close(&d); return; }
  WCHAR c=L'A'; char ca='A'; WORD o=0xAAAA;
  /* observed (identical on three boots); no documentation for these arguments */
  EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,&c,0,&o,0),GDI_ERROR);   EXPECT_EQ(o,0xAAAA);
  EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,&c,0,NULL,0),GDI_ERROR);
  EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,&c,1,NULL,0),GDI_ERROR);
  EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,NULL,1,&o,0),GDI_ERROR);
  EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,&c,-1,&o,0),GDI_ERROR);
  EXPECT_EQ(U(GetGlyphIndicesW)(NULL,&c,1,&o,0),GDI_ERROR);
  EXPECT_EQ(U(GetGlyphIndicesA)(d.dc,&ca,1,NULL,0),GDI_ERROR);
  o=0xAAAA; DWORD r=U(GetGlyphIndicesA)(d.dc,&ca,0,&o,0);
  t_note("GetGlyphIndicesA with cc=0 returns %lu (the W variant returns GDI_ERROR)", r);
  EXPECT_EQ(r,0); EXPECT_EQ(o,0xAAAA);
  SetLastError(0); U(GetGlyphIndicesW)(d.dc,&c,0,&o,0); t_note("last error after the cc=0 failure: %lu", GetLastError());
  /* an undefined flag bit is not rejected */
  o=0xAAAA; r=U(GetGlyphIndicesW)(d.dc,&c,1,&o,0x2); t_note("flags=2: return %lu glyph %04X", r, o);
  /* a long run is processed in full */
  { enum { N=3000 }; static WCHAR big[N]; static WORD go[N]; for(int i=0;i<N;i++) big[i]=(WCHAR)(0x41+(i%26));
    EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,big,N,go,0),N); WORD one[1]; WCHAR z=L'Z'; U(GetGlyphIndicesW)(d.dc,&z,1,one,0); EXPECT_EQ(go[25],one[0]); EXPECT_EQ(go[N-1],go[(N-1)%26]); }
  gir_close(&d);
}

TEST(ggi_surrogate_units_are_separate_chars){
  gir_dc d; char why[160]; static WORD cm[65536];
  if(!gir_open(&d,L"Arial",-16,why,sizeof why)){ t_skip("%s",why); gir_close(&d); return; }
  if(!gir_read_cmap(d.dc,cm)){ t_skip("no BMP cmap"); gir_close(&d); return; }
  WCHAR sp[2]={0xD83D,0xDE00}; WORD go[2]={0xAAAA,0xAAAA};
  CHECK(!cm[0xD83D] && !cm[0xDE00]);
  EXPECT_EQ(U(GetGlyphIndicesW)(d.dc,sp,2,go,GGI_MARK_NONEXISTING_GLYPHS),2);
  EXPECT_EQ(go[0],0xFFFF); EXPECT_EQ(go[1],0xFFFF);
  gir_close(&d);
}

/* GetFontUnicodeRanges describes exactly the BMP characters of the Unicode cmap for these faces (observed). */
TEST(font_unicode_ranges_vs_cmap){
  int done=0;
  for(unsigned fi=0;fi<2;fi++){
    gir_dc d; char why[160]; static WORD cm[65536]; static char inr[65536];
    if(!gir_open(&d,k_tt[fi],-16,why,sizeof why)){ t_note("%s",why); gir_close(&d); continue; }
    if(!gir_read_cmap(d.dc,cm)){ t_note("%ls: no cmap", k_tt[fi]); gir_close(&d); continue; }
    DWORD sz=U(GetFontUnicodeRanges)(d.dc,NULL);
    CHECK(sz>=16 && sz!=GDI_ERROR);
    if(sz>(1u<<20)){ CHECKF(0,"%ls: implausible GetFontUnicodeRanges size %lu", k_tt[fi], sz); gir_close(&d); continue; }
    GLYPHSET *gs=(GLYPHSET*)calloc(1,sz>16?sz:16); if(!gs){ CHECK(0); gir_close(&d); continue; } DWORD r=U(GetFontUnicodeRanges)(d.dc,gs);
    EXPECT_EQ(r,sz); EXPECT_EQ(gs->cbThis,sz); EXPECT_EQ(sz,16+4*gs->cRanges);
    memset(inr,0,sizeof inr); DWORD tot=0, over=0, under=0;
    for(DWORD i=0;i<gs->cRanges;i++){ tot+=gs->ranges[i].cGlyphs; for(unsigned k=0;k<gs->ranges[i].cGlyphs;k++){ unsigned c=gs->ranges[i].wcLow+k; if(c<65536){ inr[c]=1; if(!cm[c]) over++; } } }
    for(int c=0;c<65536;c++) if(cm[c]&&!inr[c]) under++;
    EXPECT_EQ(gs->cGlyphsSupported,tot);
    CHECKF(over==0, "%ls: %lu ranged chars absent from cmap", k_tt[fi], over);
    CHECKF(under==0, "%ls: %lu cmap chars missing from ranges", k_tt[fi], under);
    for(DWORD i=1;i<gs->cRanges;i++) CHECK(gs->ranges[i].wcLow > gs->ranges[i-1].wcLow+gs->ranges[i-1].cGlyphs-1);
    t_note("%ls: %lu ranges, %lu glyphs supported, flAccel %lu", k_tt[fi], gs->cRanges, gs->cGlyphsSupported, gs->flAccel);
    free(gs); done++; gir_close(&d);
  }
  if(!done) t_skip("no verified TrueType face");
}
