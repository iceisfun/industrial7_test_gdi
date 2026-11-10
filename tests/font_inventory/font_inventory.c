#include "t.h"
#include <stdint.h>
#include <stdlib.h>
/* font_inventory conformance tests: protocol invariants of the font-data / font-object APIs that hold on any
 * Windows with at least one TrueType face installed. No Win10-specific numbers here (those come from the
 * golden generator over data/win10/). Every hard CHECK cites the decomp it was read from (Win10 22H2 corpus,
 * gdi32full.dll / win32kfull.sys); relations that were NOT verified in the decomp are reported with t_note only.
 *
 * Decomp evidence (http://docker1.dev.moonlightcompanies.com:8765/api/function/<mod>/<fn>/decomp):
 *  [D1] gdi32full!GetObjectW: font handle (type 0x0a) with pv==NULL returns 0x5c (92 = sizeof(LOGFONTW));
 *       c<=0x1a4 goes to win32kfull!GreExtGetObjectW, which for a font copies and returns min(c, stored size).
 *  [D2] gdi32full!GetObjectA: font, pv==NULL -> 0x3c (60); c==60 or c==92 ((c-0x3c)&~0x20)==0 -> LOGFONTA, returns 60;
 *       c==0xbc (188) -> ENUMLOGFONTEXA, returns 188.
 *  [D3] gdi32full!GetTextFaceW / GetTextFaceA: lpName!=NULL && c<1 -> SetLastError(87), return 0.
 *       win32kfull!GreGetTextFaceW: NULL buffer -> wcslen+1; buffer -> copies min(len+1,c) chars, last one set to 0,
 *       returns that count (NtGdiGetTextFaceW copies it out). GetTextFaceA converts c wide chars with
 *       WideCharToMultiByte and returns (bytes - 1) when the last byte is NUL.
 *  [D4] gdi32full!CreateFontIndirectW / CreateFontIndirectA: lplf==NULL -> return NULL.
 *  [D5] win32kfull!vIFIMetricsToTextMetricWStrict: tmDescent = tmHeight - tmAscent (always the last assignment).
 *  [D6] gdi32full!GetOutlineTextMetricsW passes cjCopy only when potm!=NULL; win32kfull!NtGdiGetOutlineTextMetricsInternalW
 *       passes (0,NULL) when cjCopy==0 or potm==NULL -> GreGetOutlineTextMetricsInternalW returns the needed size;
 *       cjCopy < 0xe9 -> fills a 0xe8 temp and copies/returns cjCopy; 0xe9 <= cjCopy < needed -> 0;
 *       bad DC -> SetLastError(6), 0.
 *  [D7] gdi32full!GetFontDataImpl: cjBuffer==0xFFFFFFFF or dwOffset+cjBuffer overflow -> GDI_ERROR without a kernel call;
 *       pvBuffer==NULL forces cjBuffer to 0 -> NtGdiGetFontData(...,0) returns the size of the table.
 *  [D8] win32kfull!NtGdiGetFontUnicodeRanges / GreGetFontUnicodeRanges: size = cRanges*4+16; NULL buffer returns it;
 *       buffer: cbThis=size, flAccel = 0 or 1, cRanges = count; returns size.
 */

static HDC mkdc(void){ return U(CreateCompatibleDC)(NULL); }
static HFONT mkfont(const WCHAR *face, int h, int wt, int it){
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=wt; lf.lfItalic=(BYTE)it; lf.lfCharSet=DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName,face,LF_FACESIZE); return U(CreateFontIndirectW)(&lf);
}
/* select the first installed TrueType face among a few UI faces; returns old font via *old, 0 if none */
static HFONT select_tt(HDC dc, HFONT *old, WCHAR *name, int cap){
  static const WCHAR *C[]={L"Arial",L"Tahoma",L"Segoe UI",L"Courier New",L"Times New Roman",L"Microsoft Sans Serif"};
  for(unsigned i=0;i<sizeof C/sizeof*C;i++){ HFONT f=mkfont(C[i],-16,FW_NORMAL,0); if(!f) continue;
    HFONT o=(HFONT)U(SelectObject)(dc,f); TEXTMETRICW tm;
    if(U(GetTextMetricsW)(dc,&tm) && (tm.tmPitchAndFamily&TMPF_TRUETYPE)){ WCHAR got[LF_FACESIZE]; U(GetTextFaceW)(dc,LF_FACESIZE,got);
      if(!lstrcmpiW(got,C[i])){ *old=o; lstrcpynW(name,C[i],cap); return f; } }
    U(SelectObject)(dc,o); U(DeleteObject)(f); }
  return NULL;
}
static uint32_t be32(const BYTE *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static unsigned be16(const BYTE *p){ return (p[0]<<8)|p[1]; }

TEST(fi_createfont_null){ /* [D4] */
  CHECK(U(CreateFontIndirectW)(NULL)==NULL);
  CHECK(U(CreateFontIndirectA)(NULL)==NULL);
}

TEST(fi_getobject_font_sizes){ /* [D1] [D2] */
  LOGFONTW in; memset(&in,0,sizeof in); in.lfHeight=-13; in.lfWidth=5; in.lfEscapement=0; in.lfWeight=FW_BOLD; in.lfItalic=1;
  in.lfUnderline=1; in.lfStrikeOut=0; in.lfCharSet=ANSI_CHARSET; in.lfOutPrecision=OUT_TT_PRECIS; in.lfClipPrecision=CLIP_DEFAULT_PRECIS;
  in.lfQuality=ANTIALIASED_QUALITY; in.lfPitchAndFamily=VARIABLE_PITCH|FF_SWISS; lstrcpyW(in.lfFaceName,L"Tahoma");
  HFONT f=U(CreateFontIndirectW)(&in); CHECK(f!=NULL); if(!f) return;
  EXPECT_EQ(U(GetObjectW)(f,0,NULL),sizeof(LOGFONTW));
  EXPECT_EQ(U(GetObjectA)(f,0,NULL),sizeof(LOGFONTA));
  LOGFONTW w; memset(&w,0xcc,sizeof w);
  EXPECT_EQ(U(GetObjectW)(f,sizeof w,&w),sizeof(LOGFONTW));
  CHECK(w.lfHeight==in.lfHeight && w.lfWidth==in.lfWidth && w.lfWeight==in.lfWeight && w.lfItalic==in.lfItalic &&
        w.lfUnderline==in.lfUnderline && w.lfStrikeOut==in.lfStrikeOut && w.lfCharSet==in.lfCharSet && w.lfOutPrecision==in.lfOutPrecision &&
        w.lfClipPrecision==in.lfClipPrecision && w.lfQuality==in.lfQuality && w.lfPitchAndFamily==in.lfPitchAndFamily);
  EXPECT_WSTR(w.lfFaceName,L"Tahoma");
  BYTE small[16]; memset(small,0xcc,sizeof small);
  EXPECT_EQ(U(GetObjectW)(f,10,small),10);              /* min(c, stored) */
  CHECK(!memcmp(small,&w,10));
  LOGFONTA a; memset(&a,0xcc,sizeof a);
  EXPECT_EQ(U(GetObjectA)(f,sizeof a,&a),sizeof(LOGFONTA));
  CHECK(a.lfHeight==-13 && a.lfWeight==FW_BOLD && a.lfItalic==1 && a.lfCharSet==ANSI_CHARSET);
  EXPECT_STR(a.lfFaceName,"Tahoma");
  BYTE big[sizeof(LOGFONTW)+8]; memset(big,0xcc,sizeof big);
  EXPECT_EQ(U(GetObjectA)(f,sizeof(LOGFONTW),big),sizeof(LOGFONTA)); /* c==92 -> LOGFONTA, 60 */
  ENUMLOGFONTEXA ea; memset(&ea,0,sizeof ea);
  EXPECT_EQ(U(GetObjectA)(f,sizeof ea,&ea),sizeof(ENUMLOGFONTEXA)); /* 188 */
  EXPECT_STR(ea.elfLogFont.lfFaceName,"Tahoma");
  U(DeleteObject)(f);
}

TEST(fi_textface_protocol){ /* [D3] */
  HDC dc=mkdc(); HFONT o; WCHAR face[LF_FACESIZE]; HFONT f=select_tt(dc,&o,face,LF_FACESIZE);
  if(!f){ U(DeleteDC)(dc); t_skip("no TrueType UI face installed"); return; }
  int len=lstrlenW(face);
  EXPECT_EQ(U(GetTextFaceW)(dc,0,NULL),len+1);
  WCHAR buf[LF_FACESIZE+4]; memset(buf,0xcc,sizeof buf);
  EXPECT_EQ(U(GetTextFaceW)(dc,LF_FACESIZE+4,buf),len+1); EXPECT_WSTR(buf,face);
  memset(buf,0xcc,sizeof buf);
  EXPECT_EQ(U(GetTextFaceW)(dc,3,buf),3); CHECK(buf[2]==0 && buf[0]==face[0] && buf[1]==face[1]);
  SET_ERR(0); EXPECT_EQ(U(GetTextFaceW)(dc,0,buf),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  SET_ERR(0); EXPECT_EQ(U(GetTextFaceW)(dc,-1,buf),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  char ab[LF_FACESIZE+4], fa[LF_FACESIZE*2]; WideCharToMultiByte(CP_ACP,0,face,-1,fa,sizeof fa,NULL,NULL);
  EXPECT_EQ(U(GetTextFaceA)(dc,0,NULL),len+1);
  memset(ab,0xcc,sizeof ab);
  EXPECT_EQ(U(GetTextFaceA)(dc,sizeof ab,ab),len); EXPECT_STR(ab,fa);       /* A returns strlen, W returns len+1 */
  memset(ab,0xcc,sizeof ab);
  EXPECT_EQ(U(GetTextFaceA)(dc,3,ab),2); CHECK(ab[2]==0 && ab[0]==fa[0] && ab[1]==fa[1]);
  SET_ERR(0); EXPECT_EQ(U(GetTextFaceA)(dc,0,ab),0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
  U(SelectObject)(dc,o); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(fi_textmetrics_cell_invariant){ /* [D5]: tmHeight == tmAscent + tmDescent, every face/height/style */
  static const WCHAR *FACES[]={L"Segoe UI",L"MS Shell Dlg",L"MS Shell Dlg 2",L"Microsoft Sans Serif",L"Tahoma",L"Arial",L"Courier New",
    L"Courier",L"MS Sans Serif",L"System",L"Terminal",L"Fixedsys",L"Small Fonts",L"Marlett",L"Times New Roman",L"",L"NoSuchFace12345"};
  static const int H[]={-8,-9,-11,-12,-13,-15,-16,-20,-24,-32,8,13,16,0};
  HDC dc=mkdc(); int n=0, apar=0;
  for(unsigned i=0;i<sizeof FACES/sizeof*FACES;i++) for(unsigned j=0;j<sizeof H/sizeof*H;j++) for(int s=0;s<4;s++){
    HFONT f=mkfont(FACES[i],H[j],(s&1)?FW_BOLD:FW_NORMAL,(s>>1)&1); HFONT o=(HFONT)U(SelectObject)(dc,f);
    TEXTMETRICW tw; TEXTMETRICA ta; CHECK(U(GetTextMetricsW)(dc,&tw));
    CHECKF(tw.tmHeight==tw.tmAscent+tw.tmDescent,"%ls h=%d s=%d: %ld != %ld+%ld",FACES[i],H[j],s,tw.tmHeight,tw.tmAscent,tw.tmDescent);
    if(U(GetTextMetricsA)(dc,&ta)){
      if(ta.tmHeight!=tw.tmHeight||ta.tmAscent!=tw.tmAscent||ta.tmDescent!=tw.tmDescent||ta.tmInternalLeading!=tw.tmInternalLeading||
         ta.tmAveCharWidth!=tw.tmAveCharWidth||ta.tmMaxCharWidth!=tw.tmMaxCharWidth||ta.tmWeight!=tw.tmWeight||ta.tmPitchAndFamily!=tw.tmPitchAndFamily||ta.tmCharSet!=tw.tmCharSet) apar++;
    }
    n++; U(SelectObject)(dc,o); U(DeleteObject)(f); }
  if(apar) t_note("GetTextMetricsA vs W numeric fields differ in %d of %d fonts (not decomp-verified; observation)",apar,n);
  U(DeleteDC)(dc);
}

TEST(fi_outline_tm_protocol){ /* [D6] */
  HDC dc=mkdc(); HFONT o; WCHAR face[LF_FACESIZE]; HFONT f=select_tt(dc,&o,face,LF_FACESIZE);
  if(!f){ U(DeleteDC)(dc); t_skip("no TrueType UI face installed"); return; }
  UINT need=U(GetOutlineTextMetricsW)(dc,0,NULL);
  CHECKF(need>=sizeof(OUTLINETEXTMETRICW),"need=%u",need);
  BYTE *b=(BYTE*)calloc(1,need+64);
  EXPECT_EQ(U(GetOutlineTextMetricsW)(dc,0,(OUTLINETEXTMETRICW*)b),need);   /* cjCopy 0 + buffer = size query */
  UINT part=0xe8 < sizeof(OUTLINETEXTMETRICW) ? 0xe8 : (UINT)sizeof(OUTLINETEXTMETRICW);
  EXPECT_EQ(U(GetOutlineTextMetricsW)(dc,part,(OUTLINETEXTMETRICW*)b),part);
  if(need-1>=0xe9){ EXPECT_EQ(U(GetOutlineTextMetricsW)(dc,need-1,(OUTLINETEXTMETRICW*)b),0); }
  memset(b,0,need+64);
  UINT r=U(GetOutlineTextMetricsW)(dc,need,(OUTLINETEXTMETRICW*)b); CHECK(r!=0);
  OUTLINETEXTMETRICW *otm=(OUTLINETEXTMETRICW*)b; TEXTMETRICW tm; U(GetTextMetricsW)(dc,&tm);
  if(r!=need) t_note("GetOutlineTextMetricsW full copy returned %u, size query %u",r,need);
  if(otm->otmSize!=need) t_note("otmSize %u != size query %u",otm->otmSize,need);
  if(otm->otmTextMetrics.tmHeight!=tm.tmHeight||otm->otmTextMetrics.tmAscent!=tm.tmAscent) t_note("otmTextMetrics differs from GetTextMetricsW (%ld/%ld vs %ld/%ld)",
     otm->otmTextMetrics.tmHeight,otm->otmTextMetrics.tmAscent,tm.tmHeight,tm.tmAscent);
  /* otmEMSquare vs head.unitsPerEm (observation) */
  BYTE head[54]; if(U(GetFontData)(dc,0x64616568 /*'head'*/,0,head,sizeof head)==sizeof head && be16(head+18)!=otm->otmEMSquare)
    t_note("otmEMSquare %u != head.unitsPerEm %u",otm->otmEMSquare,be16(head+18));
  free(b);
  SET_ERR(0); EXPECT_EQ(U(GetOutlineTextMetricsW)(NULL,0,NULL),0); EXPECT_ERR(ERROR_INVALID_HANDLE);
  U(SelectObject)(dc,o); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(fi_fontdata_protocol){ /* [D7] + OpenType file-format invariants of the returned bytes */
  HDC dc=mkdc(); HFONT o; WCHAR face[LF_FACESIZE]; HFONT f=select_tt(dc,&o,face,LF_FACESIZE);
  if(!f){ U(DeleteDC)(dc); t_skip("no TrueType UI face installed"); return; }
  DWORD sz=U(GetFontData)(dc,0,0,NULL,0);
  CHECKF(sz!=GDI_ERROR && sz>=12,"size %lu",sz); if(sz==GDI_ERROR||sz<12){ goto out; }
  EXPECT_EQ(U(GetFontData)(dc,0,0,NULL,100),sz);                       /* NULL buffer = size query */
  { BYTE *b=(BYTE*)malloc(sz); DWORD r=U(GetFontData)(dc,0,0,b,sz); EXPECT_EQ(r,sz);
    uint32_t v=be32(b); CHECKF(v==0x00010000||v==0x4f54544f||v==0x74727565,"sfnt version 0x%08x",v);
    BYTE part[8]; EXPECT_EQ(U(GetFontData)(dc,0,4,part,8),8); CHECK(!memcmp(part,b+4,8));
    /* every directory entry: GetFontData(tag) size == directory length (observation; kernel ulGetFontData not decomp-checked) */
    unsigned nt=be16(b+4), bad=0; for(unsigned i=0;i<nt && 12+16*i+16<=sz;i++){ const BYTE *e=b+12+16*i; DWORD tag=e[0]|(e[1]<<8)|(e[2]<<16)|((DWORD)e[3]<<24);
      DWORD tl=U(GetFontData)(dc,tag,0,NULL,0); if(tl!=be32(e+12)) bad++; }
    if(bad) t_note("%u of %u tables: GetFontData(tag) size != directory length",bad,nt);
    free(b); }
  { BYTE head[54]; DWORD r=U(GetFontData)(dc,0x64616568,0,head,sizeof head);
    EXPECT_EQ(r,54); if(r==54) CHECKF(be32(head+12)==0x5F0F3CF5,"head magic 0x%08x",be32(head+12)); }
  { /* buffers sized so that an implementation that ignores the rule cannot overrun them (Wine writes the whole font) */
    BYTE *x=(BYTE*)calloc(1,sz+64); EXPECT_EQ(U(GetFontData)(dc,0,0,x,0xFFFFFFFF),GDI_ERROR);  /* cb == GDI_ERROR rejected in user mode */
    EXPECT_EQ(U(GetFontData)(dc,0,0xFFFFFFF0,x,0x20),GDI_ERROR);                              /* offset + cb overflow */
    free(x); }
  { DWORD r=U(GetFontData)(dc,0x7a7a7a7a /*'zzzz'*/,0,NULL,0); if(r!=GDI_ERROR) t_note("absent table returned %lu",r); }
out:
  U(SelectObject)(dc,o); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(fi_unicode_ranges_protocol){ /* [D8] */
  HDC dc=mkdc(); HFONT o; WCHAR face[LF_FACESIZE]; HFONT f=select_tt(dc,&o,face,LF_FACESIZE);
  if(!f){ U(DeleteDC)(dc); t_skip("no TrueType UI face installed"); return; }
  DWORD sz=U(GetFontUnicodeRanges)(dc,NULL);
  CHECKF(sz>=16 && (sz-16)%4==0,"size %lu",sz);
  if(sz>=16){ GLYPHSET *g=(GLYPHSET*)calloc(1,sz+16);
    EXPECT_EQ(U(GetFontUnicodeRanges)(dc,g),sz);
    EXPECT_EQ(g->cbThis,sz); EXPECT_EQ(g->cRanges,(sz-16)/4); CHECK(g->flAccel==0||g->flAccel==1);
    unsigned long sum=0; int sorted=1; for(DWORD i=0;i<g->cRanges;i++){ sum+=g->ranges[i].cGlyphs; if(i && g->ranges[i].wcLow<=g->ranges[i-1].wcLow) sorted=0; }
    if(sum!=g->cGlyphsSupported) t_note("sum(cGlyphs)=%lu cGlyphsSupported=%lu",sum,g->cGlyphsSupported);
    if(!sorted) t_note("ranges not ascending");
    free(g); }
  U(SelectObject)(dc,o); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(fi_width_relations_observed){ /* NOT decomp-verified: reported as notes only (ABC sum vs width, extent vs sum of widths) */
  static const WCHAR *FACES[]={L"Segoe UI",L"Tahoma",L"Arial",L"Courier New",L"Microsoft Sans Serif",L"MS Shell Dlg",L"MS Shell Dlg 2"};
  static const int H[]={-11,-12,-13,-16};
  HDC dc=mkdc(); int abcbad=0, extbad=0, n=0;
  static const WCHAR S[]=L"The quick brown fox jumps over the lazy dog 0123456789";
  for(unsigned i=0;i<sizeof FACES/sizeof*FACES;i++) for(unsigned j=0;j<sizeof H/sizeof*H;j++){
    HFONT f=mkfont(FACES[i],H[j],FW_NORMAL,0), o=(HFONT)U(SelectObject)(dc,f);
    TEXTMETRICW tm; U(GetTextMetricsW)(dc,&tm);
    if(tm.tmPitchAndFamily&TMPF_TRUETYPE){ INT w[95]; ABC abc[95];
      if(U(GetCharWidth32W)(dc,0x20,0x7e,w) && U(GetCharABCWidthsW)(dc,0x20,0x7e,abc)){
        for(int c=0;c<95;c++) if(abc[c].abcA+(int)abc[c].abcB+abc[c].abcC!=w[c]) abcbad++;
        long sum=0; for(const WCHAR *p=S;*p;p++) sum+=w[*p-0x20]; SIZE ext; U(GetTextExtentPoint32W)(dc,S,lstrlenW(S),&ext);
        if(ext.cx!=sum) extbad++;
        if(ext.cy!=tm.tmHeight) extbad++;
        n++; } }
    U(SelectObject)(dc,o); U(DeleteObject)(f); }
  t_note("TrueType fonts checked %d: ABC sum != width in %d glyphs; extent != sum/height in %d cases (observation)",n,abcbad,extbad);
  U(DeleteDC)(dc);
}

TEST(fi_font_registry_present){ /* observation: the keys the font mapper reads */
  static const char *K[]={"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts","SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink","SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\GRE_Initialize"};
  for(unsigned i=0;i<sizeof K/sizeof*K;i++){ HKEY h; LONG r=RegOpenKeyExA(HKEY_LOCAL_MACHINE,K[i],0,KEY_READ,&h);
    if(r==ERROR_SUCCESS){ DWORD nv=0; RegQueryInfoKeyA(h,NULL,NULL,NULL,NULL,NULL,NULL,&nv,NULL,NULL,NULL,NULL); t_note("%s: %lu values",K[i],nv); RegCloseKey(h); }
    else t_note("%s: open failed %ld",K[i],r); }
}
