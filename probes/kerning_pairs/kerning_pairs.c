/* kerning_pairs: GetKerningPairsW / GetKerningPairsA recorder.
 *
 * For each standard TrueType UI / reference face (plus bitmap faces and one absent name as negative controls) at one
 * main height (-12) and a large one (-32), regular and bold, on a screen DC (GetDC(NULL), MM_TEXT, current boot DPI):
 *   - the pair count (GetKerningPairsW(dc,0,NULL)), the count from GetKerningPairsA, and the return of a full read,
 *   - the call contract: return for a buffer one pair too small, and for a NULL buffer with a non-zero count,
 *   - a SHA-256 prefix + length of the pairs in the order returned (H line `h_ret`) and of the pairs sorted by
 *     (first, second) (`h_sorted`), each pair packed as three little-endian int32 (first, second, amount),
 *   - whether the returned order is already sorted, the min/max/sum of the amounts, the number of non-zero amounts,
 *   - samples: first 4 and last 4 pairs in returned order, first 4 and last 4 in sorted order, as `first/second/amount`
 *     (hex code units, signed amount).
 * The A recordings (GetKerningPairsA) carry the same hashes over the pairs the A entry point returned.
 *
 * usage: kerning_pairs.exe [recorder options, see rec.h] [--quick]
 *
 * Tables (S lines carry the column names):
 *   kern     face;req_h;wt;it;api    got + counts + stats + samples (api = W or A)
 *   kerncall face;req_h;wt;it        call contract (W entry point) */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FACES[] = {
  "Segoe UI","MS Shell Dlg","MS Shell Dlg 2","Tahoma","Arial","Courier New","Microsoft Sans Serif","Times New Roman",
  "Verdana","Consolas","Calibri","Lucida Console","Segoe UI Symbol","Georgia","Trebuchet MS","Cambria","System","Terminal","Fixedsys","NoSuchFace I7"
};
static const char *FACES_QUICK[] = {"Segoe UI","Tahoma","Arial","System"};
static const int HEIGHTS[] = {-12,-32};

static void wide(WCHAR *d, const char *s){ int i=0; for(;s[i]&&i<LF_FACESIZE-1;i++) d[i]=(WCHAR)(unsigned char)s[i]; d[i]=0; }
static void nas(rv *v, int n){ for(int i=0;i<n;i++) rv_na(v); }
static int cmp(const void *a, const void *b){
  const int32_t *x=(const int32_t*)a, *y=(const int32_t*)b;
  if(x[0]!=y[0]) return x[0]<y[0]?-1:1;
  if(x[1]!=y[1]) return x[1]<y[1]?-1:1;
  return x[2]<y[2]?-1:(x[2]>y[2]); }
static void sample(rv *v, const int32_t *p, int n, int from, int cnt){
  for(int i=0;i<cnt;i++){ int j=from+i; char t[64];
    if(j>=0 && j<n){ snprintf(t,sizeof t,"%x/%x/%d",p[3*j],p[3*j+1],p[3*j+2]); rv_s(v,t); } else rv_na(v); } }

static void kern_row(HDC dc, const rk *base, const char *face, int api, const WCHAR *got_w){
  rk k=*base; rk_s(&k,"api",api?"A":"W");
  rv v; rv_reset(&v); if(got_w[0]) rv_w(&v,got_w); else rv_na(&v);
  DWORD np = api? GetKerningPairsA(dc,0,NULL) : GetKerningPairsW(dc,0,NULL);
  rv_i(&v,np);
  KERNINGPAIR *kp = (np>0 && np<2000000)? (KERNINGPAIR*)calloc(np,sizeof *kp) : NULL;
  DWORD got = kp ? (api? GetKerningPairsA(dc,np,kp) : GetKerningPairsW(dc,np,kp)) : 0;
  rv_i(&v,got);
  (void)face;
  if(!kp || got!=np){ nas(&v,13); rec_row("kern",&k,&v); free(kp); return; }
  int32_t *p=(int32_t*)malloc(sizeof(int32_t)*3*np), *s=(int32_t*)malloc(sizeof(int32_t)*3*np);
  long long sum=0; int mn=0,mx=0,nz=0;
  for(DWORD i=0;i<np;i++){ p[3*i]=kp[i].wFirst; p[3*i+1]=kp[i].wSecond; p[3*i+2]=kp[i].iKernAmount;
    int a=kp[i].iKernAmount; sum+=a; if(i==0||a<mn) mn=a; if(i==0||a>mx) mx=a; if(a) nz++; }
  memcpy(s,p,sizeof(int32_t)*3*np); qsort(s,np,3*sizeof(int32_t),cmp);
  int sorted=!memcmp(p,s,sizeof(int32_t)*3*np);
  rv_i(&v,sorted); rv_i(&v,mn); rv_i(&v,mx); rv_i(&v,sum); rv_i(&v,nz);
  sample(&v,p,np,0,4); sample(&v,p,np,(int)np-4,4); /* 8 columns */
  /* sorted samples go into a second row so that the line stays readable */
  rec_row("kern",&k,&v);
  rv w; rv_reset(&w); sample(&w,s,np,0,4); sample(&w,s,np,(int)np-4,4);
  rec_row("kernsorted",&k,&w);
  rk hk=k; rec_hash("h_ret",&hk,p,sizeof(int32_t)*3*np); rec_hash("h_sorted",&hk,s,sizeof(int32_t)*3*np);
  free(p); free(s); free(kp);
}

static void one(HDC dc, const char *face, int h, int wt, int it){
  rk k; rk_reset(&k); rk_s(&k,"face",face); rk_i(&k,"req_h",h); rk_i(&k,"wt",wt); rk_i(&k,"it",it);
  LOGFONTW lf; memset(&lf,0,sizeof lf); lf.lfHeight=h; lf.lfWeight=wt; lf.lfItalic=(BYTE)it; lf.lfCharSet=DEFAULT_CHARSET; wide(lf.lfFaceName,face);
  HFONT hf=CreateFontIndirectW(&lf); HGDIOBJ of=hf?SelectObject(dc,hf):NULL;
  if(!hf || !of || of==HGDI_ERROR){
    for(int a=0;a<2;a++){ rk k2=k; rk_s(&k2,"api",a?"A":"W"); rv v; rv_reset(&v); nas(&v,16); rec_row("kern",&k2,&v); }
    if(hf) DeleteObject(hf);
    return; }
  WCHAR gw[LF_FACESIZE+1]; memset(gw,0,sizeof gw); GetTextFaceW(dc,LF_FACESIZE,gw);
  kern_row(dc,&k,face,0,gw); kern_row(dc,&k,face,1,gw);
  /* call contract (W) */
  { rv c; rv_reset(&c); DWORD np=GetKerningPairsW(dc,0,NULL); rv_i(&c,np);
    KERNINGPAIR *kb=(KERNINGPAIR*)calloc(np+2,sizeof *kb);   /* full-size buffer: the short call must not be able to overrun it */
    rv_i(&c, np>1? GetKerningPairsW(dc,np-1,kb) : GetKerningPairsW(dc,1,kb));     /* too small (or exact for count 0/1) */
    rv_i(&c, np>0? GetKerningPairsW(dc,np,NULL) : GetKerningPairsW(dc,1,NULL));     /* NULL buffer, non-zero count */
    rec_row("kerncall",&k,&c); free(kb); }
  SelectObject(dc,of); DeleteObject(hf);
}

int main(int argc, char **argv){
  rec_init(&argc,argv,"kerning_pairs","1");
  int quick=0;
  for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--quick")) quick=1;
  HDC dc=GetDC(NULL);
  rec_env("kerning_pairs.mode","%s",quick?"quick":"full");
  rec_note("pairs are packed as int32 (first,second,amount) for the hashes; samples are first/second in hex, amount signed; kernsorted = pairs sorted by (first,second)");
  rec_schema("kern","face req_h wt it api","got count ret sorted min max sum nonzero r0 r1 r2 r3 r4 r5 r6 r7");
  rec_schema("kernsorted","face req_h wt it api","s0 s1 s2 s3 s4 s5 s6 s7");
  rec_schema("kerncall","face req_h wt it","count ret_small ret_nullbuf");
  const char **fc=quick?FACES_QUICK:FACES; int nf=quick?(int)(sizeof FACES_QUICK/sizeof*FACES_QUICK):(int)(sizeof FACES/sizeof*FACES);
  for(int f=0;f<nf;f++) for(int hi=0;hi<2;hi++) for(int wt=400;wt<=700;wt+=300){
    if(quick && hi) continue;
    one(dc,fc[f],HEIGHTS[hi],wt,0); }
  ReleaseDC(NULL,dc);
  return rec_finish(0);
}
