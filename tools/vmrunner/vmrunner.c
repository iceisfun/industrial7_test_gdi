/* vmrunner: in-guest plan runner for tools/vm_run.sh (Win10 VM). Usage, from run.cmd:
 *     %R%\vmrunner.exe %R%\plan.txt > COM1
 * Its stdout (the COM1 handle opened by cmd) is inherited by every child as stdout, so probe output goes
 * straight to the serial port; stderr of each child goes to %TEMP% and is relayed afterwards as I7P ERR lines.
 * Plan lines (relative exe paths resolve against the plan's directory; '#' comments):
 *   run <timeout_s> <name> <exe> [args...]   run a probe; killed (TerminateProcess) after timeout_s
 *   cmd <timeout_s> <name> <cmd.exe /c command line>
 *   setres <W>x<H> [persist]                 ChangeDisplaySettings (persist = CDS_UPDATEREGISTRY)
 *   shot <name> [wait_ms]                    print "I7P SHOT name=<name>" and sleep (host takes a QMP screendump)
 *   sleep <ms>
 *   echo <text>
 * Markers written (one line each, LF):
 *   I7P RUNNER v=1 plan=<path> time=<local iso>
 *   I7P BEGIN name=<n> seq=<i> timeout=<s>
 *   I7P ERR name=<n> | <escaped stderr line>
 *   I7P END name=<n> seq=<i> rc=<exit code> ms=<elapsed> timeout=<0|1>
 *   I7P DONE n=<steps> failed=<count>
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HANDLE out;
static void put(const char *s){ DWORD w; size_t n=strlen(s), o=0; while(o<n){ if(!WriteFile(out,s+o,(DWORD)(n-o),&w,NULL)||!w) break; o+=w; } }
static void putf(const char *fmt, ...){ char b[4096]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap); put(b); }
static void esc(char *d, size_t cap, const char *s){ static const char H[]="0123456789ABCDEF"; size_t o=0;
  if(!*s){ snprintf(d,cap,"~"); return; }
  for(;*s && o+4<cap;s++){ unsigned char c=(unsigned char)*s; if(c<=0x20||c>=0x7f||c=='%'||c==';'||c=='|'||c=='~'||c=='!'){ d[o++]='%'; d[o++]=H[c>>4]; d[o++]=H[c&15]; } else d[o++]=(char)c; }
  d[o]=0; }
static void now(char *d, int cap){ SYSTEMTIME t; GetLocalTime(&t); snprintf(d,cap,"%04d-%02d-%02dT%02d:%02d:%02d.%03d",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds); }
static char *tok(char **p){ while(**p==' '||**p=='\t') (*p)++; if(!**p) return NULL; char *s=*p; while(**p && **p!=' ' && **p!='\t') (*p)++; if(**p){ **p=0; (*p)++; } return s; }

static int run_step(int seq, const char *name, int tmo, const char *cmdline){
  char tmp[MAX_PATH], errp[MAX_PATH], ts[40]; GetTempPathA(sizeof tmp,tmp); snprintf(errp,sizeof errp,"%si7p_%lu_%d.err",tmp,GetCurrentProcessId(),seq);
  SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};
  HANDLE herr=CreateFileA(errp,GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,NULL);
  HANDLE hout; DuplicateHandle(GetCurrentProcess(),out,GetCurrentProcess(),&hout,0,TRUE,DUPLICATE_SAME_ACCESS);
  HANDLE hin=CreateFileA("NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
  STARTUPINFOA si; memset(&si,0,sizeof si); si.cb=sizeof si; si.dwFlags=STARTF_USESTDHANDLES; si.hStdInput=hin; si.hStdOutput=hout; si.hStdError=herr;
  PROCESS_INFORMATION pi; char *cl=_strdup(cmdline);
  now(ts,sizeof ts); putf("I7P BEGIN name=%s seq=%d timeout=%d time=%s\n",name,seq,tmo,ts);
  DWORD t0=GetTickCount(); int timed=0; DWORD rc=0xFFFFFFFF;
  /* job object: a timeout kills the whole tree (cmd.exe children included); KILL_ON_JOB_CLOSE as a backstop */
  HANDLE job=CreateJobObjectA(NULL,NULL);
  if(job){ JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; memset(&li,0,sizeof li); li.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job,JobObjectExtendedLimitInformation,&li,sizeof li); }
  if(CreateProcessA(NULL,cl,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_BREAKAWAY_FROM_JOB,NULL,NULL,&si,&pi)
     || CreateProcessA(NULL,cl,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si,&pi)){
    if(job) AssignProcessToJobObject(job,pi.hProcess);
    ResumeThread(pi.hThread);
    if(WaitForSingleObject(pi.hProcess,(DWORD)tmo*1000)==WAIT_TIMEOUT){ timed=1; if(job) TerminateJobObject(job,0xDEAD); TerminateProcess(pi.hProcess,0xDEAD); WaitForSingleObject(pi.hProcess,5000); }
    GetExitCodeProcess(pi.hProcess,&rc); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  } else { putf("I7P ERR name=%s | CreateProcess%%20failed%%20gle=%lu\n",name,GetLastError()); }
  if(job) CloseHandle(job);
  DWORD ms=GetTickCount()-t0; free(cl);
  CloseHandle(hout); CloseHandle(hin); CloseHandle(herr);
  /* relay stderr (bounded) */
  FILE *f=fopen(errp,"rb"); if(f){ char line[2048], e[6200]; int n=0; while(fgets(line,sizeof line,f) && n<200){ size_t l=strlen(line); while(l&&(line[l-1]=='\n'||line[l-1]=='\r')) line[--l]=0; esc(e,sizeof e,line); putf("I7P ERR name=%s | %s\n",name,e); n++; } fclose(f); }
  DeleteFileA(errp);
  putf("I7P END name=%s seq=%d rc=%ld ms=%lu timeout=%d\n",name,seq,(long)rc,ms,timed);
  return timed || rc!=0;
}

int main(int argc, char **argv){
  out=GetStdHandle(STD_OUTPUT_HANDLE);
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
  if(argc<2){ put("I7P ERR name=vmrunner | usage%20vmrunner%20plan.txt\n"); return 2; }
  char dir[MAX_PATH]; snprintf(dir,sizeof dir,"%s",argv[1]); char *sl=strrchr(dir,'\\'); if(sl) sl[1]=0; else strcpy(dir,".\\");
  FILE *pf=fopen(argv[1],"rb"); char ts[40]; now(ts,sizeof ts);
  putf("I7P RUNNER v=1 plan=%s time=%s\n",argv[1],ts);
  if(!pf){ put("I7P ERR name=vmrunner | cannot%20open%20plan\n"); put("I7P DONE n=0 failed=1\n"); return 2; }
  char line[4096]; int seq=0, failed=0;
  while(fgets(line,sizeof line,pf)){
    size_t l=strlen(line); while(l&&(line[l-1]=='\n'||line[l-1]=='\r')) line[--l]=0;
    char *p=line, *verb=tok(&p); if(!verb || verb[0]=='#') continue;
    if(!strcmp(verb,"run")||!strcmp(verb,"cmd")){
      char *t=tok(&p), *name=tok(&p); if(!t||!name) continue; int tmo=atoi(t); if(tmo<=0) tmo=300;
      char cl[8192];
      if(!strcmp(verb,"run")){ char *exe=tok(&p); if(!exe) continue; while(*p==' ')p++;
        if(exe[0]=='\\' || (exe[0]&&exe[1]==':')) snprintf(cl,sizeof cl,"\"%s\" %s",exe,p); else snprintf(cl,sizeof cl,"\"%s%s\" %s",dir,exe,p); }
      else { while(*p==' ')p++; snprintf(cl,sizeof cl,"cmd.exe /c %s",p); }
      failed+=run_step(seq++,name,tmo,cl);
    } else if(!strcmp(verb,"setres")){
      char *r=tok(&p), *pers=tok(&p); int w=0,h=0; if(r) sscanf(r,"%dx%d",&w,&h);
      DEVMODEA dm; memset(&dm,0,sizeof dm); dm.dmSize=sizeof dm; EnumDisplaySettingsA(NULL,ENUM_CURRENT_SETTINGS,&dm);
      dm.dmPelsWidth=w; dm.dmPelsHeight=h; dm.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT;
      LONG rc=ChangeDisplaySettingsA(&dm,pers&&!strcmp(pers,"persist")?CDS_UPDATEREGISTRY:0);
      memset(&dm,0,sizeof dm); dm.dmSize=sizeof dm; EnumDisplaySettingsA(NULL,ENUM_CURRENT_SETTINGS,&dm);
      putf("I7P SETRES want=%dx%d rc=%ld now=%lux%lux%lu\n",w,h,rc,dm.dmPelsWidth,dm.dmPelsHeight,dm.dmBitsPerPel);
      Sleep(1500);
    } else if(!strcmp(verb,"shot")){
      char *n=tok(&p), *w=tok(&p); putf("I7P SHOT name=%s\n",n?n:"shot"); Sleep(w?atoi(w):3000);
    } else if(!strcmp(verb,"sleep")){ char *w=tok(&p); Sleep(w?atoi(w):1000); }
    else if(!strcmp(verb,"echo")){ while(*p==' ')p++; putf("I7P ECHO %s\n",p); }
    else putf("I7P ERR name=vmrunner | unknown%%20verb%%20%s\n",verb);
  }
  fclose(pf);
  putf("I7P DONE n=%d failed=%d\n",seq,failed);
  return failed?1:0;
}
