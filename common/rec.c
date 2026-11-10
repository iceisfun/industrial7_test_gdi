/* rec.c - recorder implementation. See rec.h and docs/PROBE_FORMAT.md. */
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================ hashing / encoding */
static const uint32_t K256[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha_block(rec_sha256_ctx *c, const uint8_t *p){
  uint32_t w[64],a,b,d,e,f,g,h,cc,t1,t2; int i;
  for(i=0;i<16;i++) w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
  for(i=16;i<64;i++){ uint32_t s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3), s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
  a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
  for(i=0;i<64;i++){
    t1=h+(ROR(e,6)^ROR(e,11)^ROR(e,25))+((e&f)^(~e&g))+K256[i]+w[i];
    t2=(ROR(a,2)^ROR(a,13)^ROR(a,22))+((a&b)^(a&cc)^(b&cc));
    h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
  c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
void rec_sha256_init(rec_sha256_ctx *c){
  static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(c->h,iv,sizeof iv); c->n=0; c->bl=0; }
void rec_sha256_update(rec_sha256_ctx *c, const void *pv, size_t n){
  const uint8_t *p=(const uint8_t*)pv; c->n+=n;
  while(n){ size_t k=64-c->bl; if(k>n)k=n; memcpy(c->buf+c->bl,p,k); c->bl+=k; p+=k; n-=k;
    if(c->bl==64){ sha_block(c,c->buf); c->bl=0; } }
}
void rec_sha256_final(rec_sha256_ctx *c, uint8_t out[32]){
  uint64_t bits=c->n*8; uint8_t pad=0x80, z=0, len[8]; int i;
  rec_sha256_update(c,&pad,1); while(c->bl!=56) rec_sha256_update(c,&z,1);
  for(i=0;i<8;i++) len[i]=(uint8_t)(bits>>(56-8*i));
  rec_sha256_update(c,len,8);
  for(i=0;i<8;i++){ out[4*i]=(uint8_t)(c->h[i]>>24); out[4*i+1]=(uint8_t)(c->h[i]>>16); out[4*i+2]=(uint8_t)(c->h[i]>>8); out[4*i+3]=(uint8_t)c->h[i]; }
}
void rec_sha256(const void *p, size_t n, uint8_t out[32]){ rec_sha256_ctx c; rec_sha256_init(&c); rec_sha256_update(&c,p,n); rec_sha256_final(&c,out); }

uint32_t rec_crc32(uint32_t crc, const void *pv, size_t n){
  static uint32_t T[256]; static int init;
  const uint8_t *p=(const uint8_t*)pv;
  if(!init){ for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int k=0;k<8;k++) c= c&1 ? 0xEDB88320u^(c>>1) : c>>1; T[i]=c; } init=1; }
  crc=~crc; while(n--) crc=T[(crc^*p++)&0xff]^(crc>>8); return ~crc;
}
void rec_hex(char *d, const void *pv, size_t n){
  static const char H[]="0123456789abcdef"; const uint8_t *p=(const uint8_t*)pv;
  for(size_t i=0;i<n;i++){ d[2*i]=H[p[i]>>4]; d[2*i+1]=H[p[i]&15]; } d[2*n]=0; }
size_t rec_b64(char *d, const void *pv, size_t n){
  static const char A[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t *p=(const uint8_t*)pv; size_t o=0,i;
  for(i=0;i+2<n;i+=3){ uint32_t v=(uint32_t)p[i]<<16|(uint32_t)p[i+1]<<8|p[i+2];
    d[o++]=A[v>>18]; d[o++]=A[(v>>12)&63]; d[o++]=A[(v>>6)&63]; d[o++]=A[v&63]; }
  if(n-i==1){ uint32_t v=(uint32_t)p[i]<<16; d[o++]=A[v>>18]; d[o++]=A[(v>>12)&63]; d[o++]='='; d[o++]='='; }
  else if(n-i==2){ uint32_t v=(uint32_t)p[i]<<16|(uint32_t)p[i+1]<<8; d[o++]=A[v>>18]; d[o++]=A[(v>>12)&63]; d[o++]=A[(v>>6)&63]; d[o++]='='; }
  d[o]=0; return o;
}
int rec_file_sha256(const char *path, uint8_t out[32], unsigned long long *size){
  HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
  if(f==INVALID_HANDLE_VALUE) return 0;
  rec_sha256_ctx c; rec_sha256_init(&c); static uint8_t buf[65536]; DWORD got; unsigned long long tot=0;
  while(ReadFile(f,buf,sizeof buf,&got,NULL) && got){ rec_sha256_update(&c,buf,got); tot+=got; }
  CloseHandle(f); rec_sha256_final(&c,out); if(size)*size=tot; return 1;
}

/* ============================================================ escaping */
/* Token alphabet: printable ASCII 0x21..0x7E except % ; | ~ ! which are %XX-escaped, as is everything else.
 * The empty string is the token "~". The bare token "!" means "not available / call failed". */
static int esc_needed(unsigned char c){ return c<=0x20 || c>=0x7f || c=='%' || c==';' || c=='|' || c=='~' || c=='!'; }
size_t rec_esc(char *d, size_t cap, const char *s){
  static const char H[]="0123456789ABCDEF"; size_t o=0;
  if(!cap) return 0;
  if(!s || !*s){ if(cap>1){ d[0]='~'; d[1]=0; return 1; } d[0]=0; return 0; }
  for(;*s;s++){ unsigned char c=(unsigned char)*s;
    if(esc_needed(c)){ if(o+3>=cap) break; d[o++]='%'; d[o++]=H[c>>4]; d[o++]=H[c&15]; }
    else { if(o+1>=cap) break; d[o++]=(char)c; } }
  d[o]=0; return o;
}
static void w2u(char *d, int cap, const WCHAR *w){ if(!w){ d[0]=0; return; } int n=WideCharToMultiByte(CP_UTF8,0,w,-1,d,cap,NULL,NULL); if(n<=0) d[0]=0; d[cap-1]=0; }

/* ============================================================ sink */
static struct {
  HANDLE h; int close_h; char buf[65536]; size_t bl;
  char probe[64]; char probe_esc[200]; uint32_t crc; int rows; int finished; int envmode;
} R;

static void sink_write(const void *p, size_t n){
  if(R.bl+n>sizeof R.buf){ rec_flush(); }
  if(n>sizeof R.buf){ DWORD w; WriteFile(R.h,p,(DWORD)n,&w,NULL); return; }
  memcpy(R.buf+R.bl,p,n); R.bl+=n;
}
void rec_flush(void){ if(R.bl && R.h && R.h!=INVALID_HANDLE_VALUE){ DWORD w; size_t o=0; while(o<R.bl){ if(!WriteFile(R.h,R.buf+o,(DWORD)(R.bl-o),&w,NULL)||!w) break; o+=w; } } R.bl=0; }
/* every record line goes through here: counted, CRC'd, newline-terminated */
static void emit_line(const char *line, size_t n){
  if(R.finished) return;
  R.crc=rec_crc32(R.crc,line,n); R.crc=rec_crc32(R.crc,"\n",1); R.rows++;
  sink_write(line,n); sink_write("\n",1);
}
/* small growable line builder */
typedef struct { char *b; size_t n, cap; } lb;
static void lb_put(lb *l, const char *s, size_t n){ if(l->n+n+1>l->cap){ size_t c=l->cap?l->cap*2:1024; while(c<l->n+n+1)c*=2; l->b=(char*)realloc(l->b,c); l->cap=c; } memcpy(l->b+l->n,s,n); l->n+=n; l->b[l->n]=0; }
static void lb_s(lb *l, const char *s){ lb_put(l,s,strlen(s)); }
static void lb_tok(lb *l, const char *s){ char t[4096]; rec_esc(t,sizeof t,s); lb_s(l,t); }
static void lb_head(lb *l, const char *tag){ lb_s(l,tag); lb_s(l," "); lb_s(l,R.probe_esc); }
static void lb_emit(lb *l){ emit_line(l->b,l->n); free(l->b); l->b=NULL; l->n=l->cap=0; }

const char *rec_probe_name(void){ return R.probe; }
int rec_rows(void){ return R.rows; }

/* ============================================================ builders */
void rk_reset(rk *k){ k->n=0; k->b[0]=0; }
static void rk_put(rk *k, const char *name, const char *esc){
  int r=snprintf(k->b+k->n, RK_MAX-k->n, "%s%s=%s", k->n?";":"", name, esc); if(r>0 && k->n+r<RK_MAX) k->n+=r; else k->b[k->n]=0; }
void rk_s(rk *k, const char *name, const char *val){ char t[600]; rec_esc(t,sizeof t,val); rk_put(k,name,t); }
void rk_w(rk *k, const char *name, const WCHAR *val){ char u[400]; w2u(u,sizeof u,val); rk_s(k,name,u); }
void rk_i(rk *k, const char *name, long long v){ char t[32]; snprintf(t,sizeof t,"%lld",v); rk_put(k,name,t); }
void rk_x(rk *k, const char *name, unsigned long long v){ char t[32]; snprintf(t,sizeof t,"0x%llx",v); rk_put(k,name,t); }
void rv_reset(rv *v){ v->n=0; v->b[0]=0; }
void rv_raw(rv *v, const char *tok){ int r=snprintf(v->b+v->n, RV_MAX-v->n, "%s%s", v->n?" ":"", tok); if(r>0 && v->n+r<RV_MAX) v->n+=r; else v->b[v->n]=0; }
void rv_s(rv *v, const char *s){ char t[2048]; rec_esc(t,sizeof t,s); rv_raw(v,t); }
void rv_w(rv *v, const WCHAR *s){ char u[1024]; w2u(u,sizeof u,s); rv_s(v,u); }
void rv_i(rv *v, long long x){ char t[32]; snprintf(t,sizeof t,"%lld",x); rv_raw(v,t); }
void rv_u(rv *v, unsigned long long x){ char t[32]; snprintf(t,sizeof t,"%llu",x); rv_raw(v,t); }
void rv_x(rv *v, unsigned long long x){ char t[32]; snprintf(t,sizeof t,"0x%llx",x); rv_raw(v,t); }
void rv_f(rv *v, double x){ char t[40]; snprintf(t,sizeof t,"%.6g",x); rv_raw(v,t); }
void rv_na(rv *v){ rv_raw(v,"!"); }

/* ============================================================ record lines */
void rec_schema(const char *table, const char *idcols, const char *cols){
  lb l={0}; lb_head(&l,"S"); lb_s(&l," "); lb_tok(&l,table); lb_s(&l," ");
  if(!idcols||!*idcols) lb_s(&l,"~"); else { for(const char *p=idcols;*p;p++){ char c=*p==' '?';':*p; lb_put(&l,&c,1);} }
  lb_s(&l," |"); if(cols && *cols){ lb_s(&l," "); lb_s(&l,cols); } lb_emit(&l);
}
void rec_row(const char *table, const rk *key, const rv *vals){
  lb l={0}; lb_head(&l,"R"); lb_s(&l," "); lb_tok(&l,table); lb_s(&l," "); lb_s(&l,key&&key->n?key->b:"~");
  lb_s(&l," |"); if(vals && vals->n){ lb_s(&l," "); lb_s(&l,vals->b); } lb_emit(&l);
}
void rec_rowf(const char *table, const char *rawkey, const char *fmt, ...){
  char vals[RV_MAX]; va_list ap; va_start(ap,fmt); vsnprintf(vals,sizeof vals,fmt,ap); va_end(ap);
  lb l={0}; lb_head(&l,"R"); lb_s(&l," "); lb_tok(&l,table); lb_s(&l," "); lb_s(&l,rawkey&&*rawkey?rawkey:"~");
  lb_s(&l," |"); if(*vals){ lb_s(&l," "); lb_s(&l,vals); } lb_emit(&l);
}
static void env_line(const char *key, const char *val){ lb l={0}; lb_head(&l,"ENV"); lb_s(&l," "); lb_tok(&l,key); lb_s(&l," "); lb_tok(&l,val); lb_emit(&l); }
static void env_na(const char *key){ lb l={0}; lb_head(&l,"ENV"); lb_s(&l," "); lb_tok(&l,key); lb_s(&l," !"); lb_emit(&l); } /* value not available: bare ! */
void rec_env(const char *key, const char *fmt, ...){ char v[3000]; va_list ap; va_start(ap,fmt); vsnprintf(v,sizeof v,fmt,ap); va_end(ap); env_line(key,v); }
void rec_envw(const char *key, const WCHAR *val){ char u[2000]; w2u(u,sizeof u,val); env_line(key,u); }
void rec_note(const char *fmt, ...){ char v[3000]; va_list ap; va_start(ap,fmt); vsnprintf(v,sizeof v,fmt,ap); va_end(ap);
  lb l={0}; lb_head(&l,"N"); lb_s(&l," "); lb_tok(&l,v); lb_emit(&l); }
void rec_hash(const char *table, const rk *key, const void *data, size_t len){
  uint8_t d[32]; char hx[65], t[64]; rec_sha256(data,len,d); rec_hex(hx,d,8);
  lb l={0}; lb_head(&l,"H"); lb_s(&l," "); lb_tok(&l,table); lb_s(&l," "); lb_s(&l,key&&key->n?key->b:"~");
  snprintf(t,sizeof t," sha256=%s len=%llu",hx,(unsigned long long)len); lb_s(&l,t); lb_emit(&l);
}
void rec_bitmap(const char *table, const rk *key, int w, int h, int bpp, const void *data, size_t len){
  char t[64]; lb l={0}; lb_head(&l,"B"); lb_s(&l," "); lb_tok(&l,table); lb_s(&l," "); lb_s(&l,key&&key->n?key->b:"~");
  snprintf(t,sizeof t," %dx%dx%d ",w,h,bpp); lb_s(&l,t);
  if(len<=64){ char *x=(char*)malloc(2*len+1); rec_hex(x,data,len); lb_s(&l,"hex:"); lb_s(&l,len?x:"~"); free(x); }
  else { char *x=(char*)malloc(4*(len/3+1)+1); rec_b64(x,data,len); lb_s(&l,"b64:"); lb_s(&l,x); free(x); }
  lb_emit(&l);
}
void rec_blob(const char *name, const void *data, size_t len){
  const size_t CH=3072; uint8_t d[32]; char hx[65], t[200], nm[256]; size_t chunks=(len+CH-1)/CH;
  rec_sha256(data,len,d); rec_hex(hx,d,32); rec_esc(nm,sizeof nm,name);
  lb l={0}; lb_head(&l,"X"); snprintf(t,sizeof t," %s len=%llu sha256=%s chunks=%llu",nm,(unsigned long long)len,hx,(unsigned long long)chunks); lb_s(&l,t); lb_emit(&l);
  static char b[4200];
  for(size_t i=0;i<chunks;i++){ size_t n=len-i*CH; if(n>CH)n=CH; rec_b64(b,(const uint8_t*)data+i*CH,n);
    lb c={0}; lb_head(&c,"XC"); snprintf(t,sizeof t," %s %llu ",nm,(unsigned long long)i); lb_s(&c,t); lb_s(&c,b); lb_emit(&c); }
}

/* ============================================================ ENV block */
static int reg_str(HKEY root, const WCHAR *path, const WCHAR *name, char *out, int cap){
  HKEY k; out[0]=0; if(RegOpenKeyExW(root,path,0,KEY_READ|KEY_WOW64_64KEY,&k)) return 0;
  WCHAR buf[1024]; DWORD t=0, n=sizeof buf - sizeof(WCHAR); int ok=0;
  if(!RegQueryValueExW(k,name,NULL,&t,(BYTE*)buf,&n)){
    if(t==REG_SZ||t==REG_EXPAND_SZ||t==REG_MULTI_SZ){ buf[n/sizeof(WCHAR)]=0; w2u(out,cap,buf); ok=1; }
    else if(t==REG_DWORD && n==4){ snprintf(out,cap,"%lu",*(DWORD*)buf); ok=1; }
    else if(t==REG_QWORD && n==8){ snprintf(out,cap,"%llu",*(unsigned long long*)buf); ok=1; }
    else { char hx[129]; rec_hex(hx,buf,n>64?64:n); snprintf(out,cap,"type%lu:%s",t,hx); ok=1; } }
  RegCloseKey(k); return ok;
}
static void env_reg(const char *key, HKEY root, const WCHAR *path, const WCHAR *name){
  char v[2048]; if(reg_str(root,path,name,v,sizeof v)) env_line(key,v); else env_na(key); }

/* enumerate all values of a key, sorted by name (case-insensitive), as "name=value\n" lines; hash + count */
typedef struct { char name[300]; char val[700]; } kv;
static int kvcmp(const void *a, const void *b){ return _stricmp(((const kv*)a)->name,((const kv*)b)->name); }
static int reg_enum(HKEY root, const WCHAR *path, kv **out){
  HKEY k; *out=NULL; if(RegOpenKeyExW(root,path,0,KEY_READ|KEY_WOW64_64KEY,&k)) return -1;
  int cap=64,n=0; kv *a=(kv*)malloc(cap*sizeof(kv));
  for(DWORD i=0;;i++){ WCHAR nm[300], val[1024]; DWORD nl=300, vl=sizeof val - sizeof(WCHAR), t;
    LONG r=RegEnumValueW(k,i,nm,&nl,NULL,&t,(BYTE*)val,&vl); if(r==ERROR_NO_MORE_ITEMS) break; if(r) continue;
    if(n==cap){ cap*=2; a=(kv*)realloc(a,cap*sizeof(kv)); }
    w2u(a[n].name,sizeof a[n].name,nm);
    if(t==REG_SZ||t==REG_EXPAND_SZ||t==REG_MULTI_SZ){ val[vl/sizeof(WCHAR)]=0; if(t==REG_MULTI_SZ){ for(DWORD j=0;j+1<vl/sizeof(WCHAR);j++) if(!val[j]) val[j]=L','; } w2u(a[n].val,sizeof a[n].val,val); }
    else if(t==REG_DWORD) snprintf(a[n].val,sizeof a[n].val,"%lu",*(DWORD*)val);
    else { char hx[129]; rec_hex(hx,val,vl>64?64:vl); snprintf(a[n].val,sizeof a[n].val,"type%lu:%s",t,hx); }
    n++; }
  RegCloseKey(k); qsort(a,n,sizeof(kv),kvcmp); *out=a; return n;
}
static void env_regkey_digest(const char *key, HKEY root, const WCHAR *path){
  kv *a; int n=reg_enum(root,path,&a); if(n<0){ env_na(key); return; }
  rec_sha256_ctx c; rec_sha256_init(&c); uint8_t d[32]; char hx[65];
  for(int i=0;i<n;i++){ rec_sha256_update(&c,a[i].name,strlen(a[i].name)); rec_sha256_update(&c,"=",1); rec_sha256_update(&c,a[i].val,strlen(a[i].val)); rec_sha256_update(&c,"\n",1); }
  rec_sha256_final(&c,d); rec_hex(hx,d,8); rec_env(key,"count=%d,sha256=%s",n,hx); free(a);
}
static void env_lf(const char *key, const LOGFONTW *lf){
  char face[200]; w2u(face,sizeof face,lf->lfFaceName);
  rec_env(key,"h=%ld,w=%ld,wt=%ld,it=%d,cs=%d,q=%d,pf=0x%x,face=%s",lf->lfHeight,lf->lfWidth,lf->lfWeight,lf->lfItalic,lf->lfCharSet,lf->lfQuality,lf->lfPitchAndFamily,face);
}
static void iso(char *d, int cap, const SYSTEMTIME *t){ snprintf(d,cap,"%04d-%02d-%02dT%02d:%02d:%02d.%03d",t->wYear,t->wMonth,t->wDay,t->wHour,t->wMinute,t->wSecond,t->wMilliseconds); }

typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOEXW*);
typedef UINT (WINAPI *GetDpiForSystem_t)(void);
typedef HANDLE (WINAPI *GetThreadDpiAwarenessContext_t)(void);
typedef int (WINAPI *GetAwarenessFromDpiAwarenessContext_t)(HANDLE);
typedef BOOL (WINAPI *IsProcessDPIAware_t)(void);
typedef BOOL (WINAPI *IsThemeActive_t)(void);
typedef struct { DWORD cbSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformID; } DLLVERSIONINFO_;
typedef HRESULT (WINAPI *DllGetVersion_t)(DLLVERSIONINFO_*);
typedef int (WINAPI *LocaleName_t)(LPWSTR,int);

static void env_min(int argc, char **argv, const char *version){
  char v[2048];
  /* probe / process */
  char path[MAX_PATH]; GetModuleFileNameA(NULL,path,sizeof path);
  const char *base=strrchr(path,'\\'); base=base?base+1:path; env_line("probe.exe",base);
  uint8_t d[32]; unsigned long long sz=0; char hx[65];
  if(rec_file_sha256(path,d,&sz)){ rec_hex(hx,d,8); rec_env("probe.exe_sha256","%s",hx); rec_env("probe.exe_size","%llu",sz); }
  rec_env("probe.version","%s",version); rec_env("probe.format","%d",REC_FORMAT);
  rec_env("probe.bits","%d",(int)(sizeof(void*)*8));
  v[0]=0; for(int i=1;i<argc;i++){ strncat(v,i>1?" ":"",sizeof v-strlen(v)-1); strncat(v,argv[i],sizeof v-strlen(v)-1); } env_line("probe.args",v);
  /* OS */
  OSVERSIONINFOEXW ov; memset(&ov,0,sizeof ov); ov.dwOSVersionInfoSize=sizeof ov;
  RtlGetVersion_t rgv=(RtlGetVersion_t)(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"),"RtlGetVersion");
  if(rgv && !rgv(&ov)) rec_env("os.version","%lu.%lu.%lu",ov.dwMajorVersion,ov.dwMinorVersion,ov.dwBuildNumber); else env_na("os.version");
  rec_env("os.sp","%u.%u",ov.wServicePackMajor,ov.wServicePackMinor);
  const WCHAR *cv=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  env_reg("os.ubr",HKEY_LOCAL_MACHINE,cv,L"UBR");
  env_reg("os.display_version",HKEY_LOCAL_MACHINE,cv,L"DisplayVersion");
  env_reg("os.product",HKEY_LOCAL_MACHINE,cv,L"ProductName");
  env_reg("os.edition",HKEY_LOCAL_MACHINE,cv,L"EditionID");
  env_reg("os.buildlab",HKEY_LOCAL_MACHINE,cv,L"BuildLabEx");
  /* host */
  DWORD n=sizeof v; if(GetComputerNameA(v,&n)) env_line("host.computer",v); else env_na("host.computer");
  n=sizeof v; if(GetUserNameA(v,&n)) env_line("host.user",v); else env_na("host.user");
  DWORD sid=0; ProcessIdToSessionId(GetCurrentProcessId(),&sid); rec_env("host.session","%lu",sid);
  { HANDLE tok; TOKEN_ELEVATION el={0}; DWORD rl; int e=-1; if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&tok)){ if(GetTokenInformation(tok,TokenElevation,&el,sizeof el,&rl)) e=(int)el.TokenIsElevated; CloseHandle(tok);} rec_env("host.elevated","%d",e); }
  /* time */
  SYSTEMTIME lt,ut; GetLocalTime(&lt); GetSystemTime(&ut); iso(v,sizeof v,&lt); env_line("time.local",v); iso(v,sizeof v,&ut); strncat(v,"Z",sizeof v-strlen(v)-1); env_line("time.utc",v);
  TIME_ZONE_INFORMATION tz; DWORD tr=GetTimeZoneInformation(&tz); rec_env("time.tz_bias_min","%ld",tz.Bias); rec_envw("time.tz_name",tz.StandardName); rec_env("time.tz_state","%lu",tr);
  rec_env("time.uptime_ms","%llu",(unsigned long long)GetTickCount64());
  /* locale */
  WCHAR w[200]; HMODULE k32=GetModuleHandleA("kernel32.dll");
  LocaleName_t uln=(LocaleName_t)(void*)GetProcAddress(k32,"GetUserDefaultLocaleName"), sln=(LocaleName_t)(void*)GetProcAddress(k32,"GetSystemDefaultLocaleName");
  if(uln && uln(w,200)) rec_envw("locale.user",w); else env_na("locale.user");
  if(sln && sln(w,200)) rec_envw("locale.system",w); else env_na("locale.system");
  rec_env("locale.lcid_user","0x%04lx",GetUserDefaultLCID()); rec_env("locale.lcid_system","0x%04lx",GetSystemDefaultLCID());
  rec_env("locale.lcid_thread","0x%04lx",GetThreadLocale());
  rec_env("locale.ui_user","0x%04x",GetUserDefaultUILanguage()); rec_env("locale.ui_system","0x%04x",GetSystemDefaultUILanguage());
  rec_env("locale.acp","%u",GetACP()); rec_env("locale.oemcp","%u",GetOEMCP());
  { char kl[KL_NAMELENGTH]; if(GetKeyboardLayoutNameA(kl)) env_line("locale.kbd",kl); }
  /* DPI */
  HMODULE u32=GetModuleHandleA("user32.dll");
  GetDpiForSystem_t gds=(GetDpiForSystem_t)(void*)GetProcAddress(u32,"GetDpiForSystem");
  GetThreadDpiAwarenessContext_t gtc=(GetThreadDpiAwarenessContext_t)(void*)GetProcAddress(u32,"GetThreadDpiAwarenessContext");
  GetAwarenessFromDpiAwarenessContext_t gac=(GetAwarenessFromDpiAwarenessContext_t)(void*)GetProcAddress(u32,"GetAwarenessFromDpiAwarenessContext");
  IsProcessDPIAware_t ipa=(IsProcessDPIAware_t)(void*)GetProcAddress(u32,"IsProcessDPIAware");
  if(gds) rec_env("dpi.system","%u",gds()); else env_na("dpi.system");
  if(gtc && gac){ HANDLE c=gtc(); int a=gac(c); rec_env("dpi.awareness","%d",a); rec_env("dpi.context","0x%llx",(unsigned long long)(ULONG_PTR)c); }
  else env_na("dpi.awareness");
  if(ipa) rec_env("dpi.process_aware","%d",ipa()?1:0);
  HDC sdc=GetDC(NULL);
  rec_env("dpi.logpixels","%dx%d",GetDeviceCaps(sdc,LOGPIXELSX),GetDeviceCaps(sdc,LOGPIXELSY));
  rec_env("display.res","%dx%d",GetDeviceCaps(sdc,HORZRES),GetDeviceCaps(sdc,VERTRES));
  rec_env("display.desktopres","%dx%d",GetDeviceCaps(sdc,DESKTOPHORZRES),GetDeviceCaps(sdc,DESKTOPVERTRES));
  rec_env("display.bpp","%d",GetDeviceCaps(sdc,BITSPIXEL)*GetDeviceCaps(sdc,PLANES));
  rec_env("display.vrefresh","%d",GetDeviceCaps(sdc,VREFRESH));
  rec_env("display.size_mm","%dx%d",GetDeviceCaps(sdc,HORZSIZE),GetDeviceCaps(sdc,VERTSIZE));
  ReleaseDC(NULL,sdc);
  { DEVMODEA dm; memset(&dm,0,sizeof dm); dm.dmSize=sizeof dm; if(EnumDisplaySettingsA(NULL,ENUM_CURRENT_SETTINGS,&dm)) rec_env("display.mode","%lux%lux%lu@%lu",dm.dmPelsWidth,dm.dmPelsHeight,dm.dmBitsPerPel,dm.dmDisplayFrequency); }
  rec_env("display.monitors","%d",GetSystemMetrics(SM_CMONITORS));
  env_reg("dpi.reg_logpixels",HKEY_CURRENT_USER,L"Control Panel\\Desktop",L"LogPixels");
  env_reg("dpi.reg_win8dpiscaling",HKEY_CURRENT_USER,L"Control Panel\\Desktop",L"Win8DpiScaling");
  env_reg("dpi.reg_applieddpi",HKEY_CURRENT_USER,L"Control Panel\\Desktop\\WindowMetrics",L"AppliedDPI");
  env_reg("dpi.reg_hw_logpixels",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts",L"LogPixels");
  /* comctl32 + theme (version depends on the exe's manifest: 5.82 without, 6.x with the v6 dependency) */
  HMODULE cc=LoadLibraryA("comctl32.dll");
  if(cc){ DllGetVersion_t dgv=(DllGetVersion_t)(void*)GetProcAddress(cc,"DllGetVersion"); DLLVERSIONINFO_ dv={sizeof dv};
    if(dgv && !dgv(&dv)) rec_env("comctl32.version","%lu.%lu.%lu",dv.dwMajorVersion,dv.dwMinorVersion,dv.dwBuildNumber); else env_na("comctl32.version");
    char p[MAX_PATH]; GetModuleFileNameA(cc,p,sizeof p); env_line("comctl32.path",p); }
  else env_na("comctl32.version");
  HMODULE ux=LoadLibraryA("uxtheme.dll");
  if(ux){ IsThemeActive_t ita=(IsThemeActive_t)(void*)GetProcAddress(ux,"IsThemeActive"); if(ita) rec_env("theme.active","%d",ita()?1:0); }
}

static void env_full(void){
  char v[2048];
  /* system metrics (pixels, in the process's DPI context) */
  static const struct { int id; const char *n; } SM[]={
    {SM_CXSCREEN,"CXSCREEN"},{SM_CYSCREEN,"CYSCREEN"},{SM_CXVIRTUALSCREEN,"CXVIRTUALSCREEN"},{SM_CYVIRTUALSCREEN,"CYVIRTUALSCREEN"},
    {SM_CXFULLSCREEN,"CXFULLSCREEN"},{SM_CYFULLSCREEN,"CYFULLSCREEN"},{SM_CXMAXIMIZED,"CXMAXIMIZED"},{SM_CYMAXIMIZED,"CYMAXIMIZED"},
    {SM_CYCAPTION,"CYCAPTION"},{SM_CYSMCAPTION,"CYSMCAPTION"},{SM_CYMENU,"CYMENU"},{SM_CYMENUSIZE,"CYMENUSIZE"},{SM_CXMENUSIZE,"CXMENUSIZE"},
    {SM_CXSIZE,"CXSIZE"},{SM_CYSIZE,"CYSIZE"},{SM_CXSMSIZE,"CXSMSIZE"},{SM_CYSMSIZE,"CYSMSIZE"},
    {SM_CXBORDER,"CXBORDER"},{SM_CYBORDER,"CYBORDER"},{SM_CXEDGE,"CXEDGE"},{SM_CYEDGE,"CYEDGE"},
    {SM_CXFIXEDFRAME,"CXFIXEDFRAME"},{SM_CYFIXEDFRAME,"CYFIXEDFRAME"},{SM_CXSIZEFRAME,"CXSIZEFRAME"},{SM_CYSIZEFRAME,"CYSIZEFRAME"},
    {SM_CXPADDEDBORDER,"CXPADDEDBORDER"},{SM_CXVSCROLL,"CXVSCROLL"},{SM_CYHSCROLL,"CYHSCROLL"},{SM_CYVSCROLL,"CYVSCROLL"},{SM_CXHSCROLL,"CXHSCROLL"},
    {SM_CXHTHUMB,"CXHTHUMB"},{SM_CYVTHUMB,"CYVTHUMB"},{SM_CXICON,"CXICON"},{SM_CYICON,"CYICON"},{SM_CXSMICON,"CXSMICON"},{SM_CYSMICON,"CYSMICON"},
    {SM_CXCURSOR,"CXCURSOR"},{SM_CYCURSOR,"CYCURSOR"},{SM_CXICONSPACING,"CXICONSPACING"},{SM_CYICONSPACING,"CYICONSPACING"},
    {SM_CXMENUCHECK,"CXMENUCHECK"},{SM_CYMENUCHECK,"CYMENUCHECK"},{SM_CXMIN,"CXMIN"},{SM_CYMIN,"CYMIN"},{SM_CXMINTRACK,"CXMINTRACK"},{SM_CYMINTRACK,"CYMINTRACK"},
    {SM_CXDLGFRAME,"CXDLGFRAME"},{SM_CYDLGFRAME,"CYDLGFRAME"},{SM_CXFOCUSBORDER,"CXFOCUSBORDER"},{SM_CYFOCUSBORDER,"CYFOCUSBORDER"},
    {SM_CXDRAG,"CXDRAG"},{SM_CYDRAG,"CYDRAG"},{SM_CXDOUBLECLK,"CXDOUBLECLK"},{SM_CYDOUBLECLK,"CYDOUBLECLK"},{SM_CYKANJIWINDOW,"CYKANJIWINDOW"},
    {SM_DBCSENABLED,"DBCSENABLED"},{SM_IMMENABLED,"IMMENABLED"},{SM_MIDEASTENABLED,"MIDEASTENABLED"},{SM_REMOTESESSION,"REMOTESESSION"},{SM_CMOUSEBUTTONS,"CMOUSEBUTTONS"}};
  for(unsigned i=0;i<sizeof SM/sizeof*SM;i++){ char k[64]; snprintf(k,sizeof k,"sm.%s",SM[i].n); rec_env(k,"%d",GetSystemMetrics(SM[i].id)); }
  /* SPI fonts + metrics */
  NONCLIENTMETRICSW ncm; memset(&ncm,0,sizeof ncm); ncm.cbSize=sizeof ncm;
  if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof ncm,&ncm,0)){
    rec_env("ncm.metrics","border=%d,scrollw=%d,scrollh=%d,captionw=%d,captionh=%d,smcaptionw=%d,smcaptionh=%d,menuw=%d,menuh=%d,paddedborder=%d",
      ncm.iBorderWidth,ncm.iScrollWidth,ncm.iScrollHeight,ncm.iCaptionWidth,ncm.iCaptionHeight,ncm.iSmCaptionWidth,ncm.iSmCaptionHeight,ncm.iMenuWidth,ncm.iMenuHeight,ncm.iPaddedBorderWidth);
    env_lf("ncm.caption",&ncm.lfCaptionFont); env_lf("ncm.smcaption",&ncm.lfSmCaptionFont); env_lf("ncm.menu",&ncm.lfMenuFont);
    env_lf("ncm.status",&ncm.lfStatusFont); env_lf("ncm.message",&ncm.lfMessageFont);
  } else env_na("ncm.metrics");
  { ICONMETRICSW im; memset(&im,0,sizeof im); im.cbSize=sizeof im; if(SystemParametersInfoW(SPI_GETICONMETRICS,sizeof im,&im,0)){ rec_env("icm.metrics","hspacing=%d,vspacing=%d,wrap=%d",im.iHorzSpacing,im.iVertSpacing,im.iTitleWrap); env_lf("icm.font",&im.lfFont);} }
  { UINT a=0; SystemParametersInfoW(SPI_GETFONTSMOOTHING,0,&a,0); rec_env("spi.fontsmoothing","%u",a);
    a=0; SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE,0,&a,0); rec_env("spi.fontsmoothingtype","%u",a);
    a=0; SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST,0,&a,0); rec_env("spi.fontsmoothingcontrast","%u",a);
    a=0; SystemParametersInfoW(SPI_GETFONTSMOOTHINGORIENTATION,0,&a,0); rec_env("spi.fontsmoothingorientation","%u",a);
    BOOL b=0; SystemParametersInfoW(SPI_GETCLEARTYPE,0,&b,0); rec_env("spi.cleartype","%d",b?1:0); }
  /* stock fonts as LOGFONT */
  static const struct { int id; const char *n; } ST[]={{SYSTEM_FONT,"SYSTEM_FONT"},{SYSTEM_FIXED_FONT,"SYSTEM_FIXED_FONT"},{OEM_FIXED_FONT,"OEM_FIXED_FONT"},
    {ANSI_FIXED_FONT,"ANSI_FIXED_FONT"},{ANSI_VAR_FONT,"ANSI_VAR_FONT"},{DEVICE_DEFAULT_FONT,"DEVICE_DEFAULT_FONT"},{DEFAULT_GUI_FONT,"DEFAULT_GUI_FONT"}};
  for(unsigned i=0;i<sizeof ST/sizeof*ST;i++){ LOGFONTW lf; char k[64]; snprintf(k,sizeof k,"stock.%s",ST[i].n);
    if(GetObjectW(GetStockObject(ST[i].id),sizeof lf,&lf)) env_lf(k,&lf); else env_na(k); }
  /* fonts directory fingerprint: count, bytes, sha256(16) over sorted lowercase "name\tsize\n" */
  { char dir[MAX_PATH], pat[MAX_PATH]; GetWindowsDirectoryA(dir,sizeof dir); strncat(dir,"\\Fonts",sizeof dir-strlen(dir)-1);
    snprintf(pat,sizeof pat,"%s\\*",dir); WIN32_FIND_DATAA fd; HANDLE f=FindFirstFileA(pat,&fd);
    int cap=1024,n=0; kv *a=(kv*)malloc(cap*sizeof(kv)); unsigned long long tot=0;
    if(f!=INVALID_HANDLE_VALUE){ do{ if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue; if(n==cap){cap*=2;a=(kv*)realloc(a,cap*sizeof(kv));}
        snprintf(a[n].name,sizeof a[n].name,"%s",fd.cFileName); CharLowerA(a[n].name);
        unsigned long long s=((unsigned long long)fd.nFileSizeHigh<<32)|fd.nFileSizeLow; tot+=s; snprintf(a[n].val,sizeof a[n].val,"%llu",s); n++; } while(FindNextFileA(f,&fd)); FindClose(f); }
    qsort(a,n,sizeof(kv),kvcmp); rec_sha256_ctx c; rec_sha256_init(&c);
    for(int i=0;i<n;i++){ rec_sha256_update(&c,a[i].name,strlen(a[i].name)); rec_sha256_update(&c,"\t",1); rec_sha256_update(&c,a[i].val,strlen(a[i].val)); rec_sha256_update(&c,"\n",1); }
    uint8_t d[32]; char hx[65]; rec_sha256_final(&c,d); rec_hex(hx,d,8); rec_env("fonts.dir","files=%d,bytes=%llu,sha256=%s",n,tot,hx); free(a);
    static const char *KF[]={"segoeui.ttf","segoeuib.ttf","tahoma.ttf","tahomabd.ttf","micross.ttf","arial.ttf","cour.ttf","couree.fon","coure.fon","sserife.fon","serife.fon","vgasys.fon","vgaoem.fon","vgafix.fon","8514sys.fon","marlett.ttf","smalle.fon","ssee0874.fon"};
    for(unsigned i=0;i<sizeof KF/sizeof*KF;i++){ char p[MAX_PATH], k[80]; uint8_t h[32]; unsigned long long sz; snprintf(p,sizeof p,"%s\\%s",dir,KF[i]); snprintf(k,sizeof k,"fonts.file.%s",KF[i]);
      if(rec_file_sha256(p,h,&sz)){ rec_hex(hx,h,8); rec_env(k,"size=%llu,sha256=%s",sz,hx); } else env_na(k); } }
  /* font registry */
  const WCHAR *FS=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes";
  env_regkey_digest("reg.fontsubstitutes",HKEY_LOCAL_MACHINE,FS);
  static const WCHAR *SUBS[]={L"MS Shell Dlg",L"MS Shell Dlg 2",L"Helv",L"Helvetica",L"Tms Rmn",L"Times",L"Courier",L"MS Sans Serif"};
  for(unsigned i=0;i<sizeof SUBS/sizeof*SUBS;i++){ char k[100], u[100]; w2u(u,sizeof u,SUBS[i]); snprintf(k,sizeof k,"reg.fontsubst.%s",u); env_reg(k,HKEY_LOCAL_MACHINE,FS,SUBS[i]); }
  env_regkey_digest("reg.fonts",HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");
  env_regkey_digest("reg.systemlink",HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink");
  { kv *a; int n=reg_enum(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\GRE_Initialize",&a);
    if(n>=0){ for(int i=0;i<n;i++){ char k[340]; snprintf(k,sizeof k,"reg.gre_init.%s",a[i].name); env_line(k,a[i].val); } free(a); } else env_na("reg.gre_init"); }
  { kv *a; int n=reg_enum(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontDPI",&a);
    if(n>=0){ for(int i=0;i<n;i++){ char k[340]; snprintf(k,sizeof k,"reg.fontdpi.%s",a[i].name); env_line(k,a[i].val); } free(a); } }
  (void)v;
}

/* ============================================================ init / finish */
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep){
  if(!R.finished){ rec_note("crash code=0x%08lx addr=%p rows=%d",ep->ExceptionRecord->ExceptionCode,ep->ExceptionRecord->ExceptionAddress,R.rows);
    char t[160]; snprintf(t,sizeof t,"END %s rows=%d crc32=%08X status=crash",R.probe_esc,R.rows,R.crc); R.finished=1;
    sink_write(t,strlen(t)); sink_write("\n",1); rec_flush(); }
  return EXCEPTION_EXECUTE_HANDLER;
}
int rec_init(int *pargc, char **argv, const char *probe, const char *version){
  int argc=*pargc, o=1; const char *out=NULL, *tag=NULL, *variant=NULL; int com1=0; R.envmode=2;
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--out") && i+1<argc) out=argv[++i];
    else if(!strcmp(argv[i],"--com1")) com1=1;
    else if(!strcmp(argv[i],"--stdout")) { out=NULL; com1=0; }
    else if(!strcmp(argv[i],"--tag") && i+1<argc) tag=argv[++i];
    else if(!strcmp(argv[i],"--variant") && i+1<argc) variant=argv[++i];
    else if(!strcmp(argv[i],"--env") && i+1<argc){ i++; R.envmode=!strcmp(argv[i],"none")?0:!strcmp(argv[i],"min")?1:2; }
    else argv[o++]=argv[i]; }
  argv[o]=NULL; *pargc=o;
  snprintf(R.probe,sizeof R.probe,"%s",probe); rec_esc(R.probe_esc,sizeof R.probe_esc,probe);
  if(com1){ R.h=CreateFileA("\\\\.\\COM1",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL); R.close_h=1; }
  else if(out){ R.h=CreateFileA(out,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); R.close_h=1; }
  else { R.h=GetStdHandle(STD_OUTPUT_HANDLE); R.close_h=0; }
  if(!R.h || R.h==INVALID_HANDLE_VALUE) ExitProcess(3);
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
  SetUnhandledExceptionFilter(crash_filter);
  /* variant: explicit, else the exe name suffix after the first '.', e.g. hello.v6pm2.exe -> v6pm2 */
  char vbuf[64]="base";
  if(variant) snprintf(vbuf,sizeof vbuf,"%s",variant);
  else { char p[MAX_PATH]; GetModuleFileNameA(NULL,p,sizeof p); char *b=strrchr(p,'\\'); b=b?b+1:p; char *e=strrchr(b,'.'); if(e)*e=0; char *d=strchr(b,'.'); if(d&&d[1]) snprintf(vbuf,sizeof vbuf,"%s",d+1); }
  { char t[400], ev[100], et[200], ever[100]; rec_esc(ev,sizeof ev,vbuf); rec_esc(et,sizeof et,tag?tag:""); rec_esc(ever,sizeof ever,version);
    snprintf(t,sizeof t,"REC %d %s v=%s variant=%s tag=%s",REC_FORMAT,R.probe_esc,ever,ev,et); emit_line(t,strlen(t)); }
  if(R.envmode>=1) env_min(*pargc,argv,version);
  if(R.envmode>=2) env_full();
  rec_flush();
  return 0;
}
int rec_finish(int rc){
  if(!R.finished){ char t[160]; snprintf(t,sizeof t,"END %s rows=%d crc32=%08X status=%s",R.probe_esc,R.rows,R.crc,rc?"fail":"ok");
    R.finished=1; sink_write(t,strlen(t)); sink_write("\n",1); }
  rec_flush(); if(R.close_h){ CloseHandle(R.h); R.h=NULL; }
  return rc;
}

/* ============================================================ GDI helpers */
size_t rec_gray_from_dib(const uint32_t *bits, int w, int h, int bpp, uint8_t *out, size_t cap){
  size_t stride=((size_t)w*bpp+7)/8, need=stride*h; if(need>cap) return 0; memset(out,0,need);
  for(int y=0;y<h;y++) for(int x=0;x<w;x++){ uint32_t p=bits[(size_t)y*w+x];
    unsigned g=(((p>>16)&255)*77+((p>>8)&255)*150+(p&255)*29)>>8;
    if(bpp==8) out[y*stride+x]=(uint8_t)g;
    else { unsigned q=g>>(8-bpp); size_t bit=(size_t)x*bpp; out[y*stride+bit/8]|=(uint8_t)(q<<(8-bpp-(bit%8))); } }
  return need;
}
int rec_dib_create(rec_dib *d, int w, int h){
  memset(d,0,sizeof *d); BITMAPINFO bi; memset(&bi,0,sizeof bi);
  bi.bmiHeader.biSize=sizeof bi.bmiHeader; bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
  d->dc=CreateCompatibleDC(NULL); if(!d->dc) return 0;
  d->bmp=CreateDIBSection(d->dc,&bi,DIB_RGB_COLORS,(void**)&d->bits,NULL,0); if(!d->bmp){ DeleteDC(d->dc); return 0; }
  d->old=(HBITMAP)SelectObject(d->dc,d->bmp); d->w=w; d->h=h; rec_dib_clear(d,0x00FFFFFF); return 1;
}
void rec_dib_clear(rec_dib *d, uint32_t c){ GdiFlush(); for(size_t i=0;i<(size_t)d->w*d->h;i++) d->bits[i]=c; }
void rec_dib_free(rec_dib *d){ if(d->dc){ SelectObject(d->dc,d->old); DeleteObject(d->bmp); DeleteDC(d->dc); } memset(d,0,sizeof *d); }
