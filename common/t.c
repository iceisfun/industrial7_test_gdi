#include "t.h"
#include <setjmp.h>
#include "modules.h"

static const char *g_mods[] = { T_MODULE_LIST, NULL };
static int g_pass, g_fail, g_cur_fail, g_cur_skip;
static const char *g_cur;
static jmp_buf g_jb;
static volatile int g_in_test;

HMODULE t_module(const char *dll) { return LoadLibraryA(dll); }   /* refcounted; modules stay loaded */
FARPROC t_proc_in(const char *dll, const char *name) { HMODULE m = t_module(dll); return m ? GetProcAddress(m, name) : NULL; }
FARPROC t_proc_ord(const char *dll, unsigned ord) { HMODULE m = t_module(dll); return m ? GetProcAddress(m, (LPCSTR)(ULONG_PTR)ord) : NULL; }
FARPROC t_proc(const char *name) {
    for (int i = 0; g_mods[i]; i++) { FARPROC p = t_proc_in(g_mods[i], name); if (p) return p; }
    return NULL;
}
int t_is_wine(void) { HMODULE n = GetModuleHandleA("ntdll.dll"); return n && GetProcAddress(n, "wine_get_version") != NULL; }

#define MAXT 8192
static struct { const char *n; t_fn f; } g_t[MAXT];
static int g_nt;
void t_register(const char *n, t_fn f) { if (g_nt < MAXT) { g_t[g_nt].n = n; g_t[g_nt].f = f; g_nt++; } }
static struct { const char *n; t_child_fn f; } g_c[512];
static int g_nc;
void t_register_child(const char *n, t_child_fn f) { if (g_nc < 512) { g_c[g_nc].n = n; g_c[g_nc].f = f; g_nc++; } }

void t_check(int ok, const char *file, int line, const char *expr, const char *fmt, ...) {
    if (ok) { g_pass++; return; }
    g_fail++; g_cur_fail++;
    printf("  FAIL %s:%d: %s", file, line, expr);
    if (fmt) { va_list a; va_start(a, fmt); printf(" ("); vprintf(fmt, a); printf(")"); va_end(a); }
    printf("\n");
}
void t_note(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("  NOTE "); vprintf(fmt, a); printf("\n"); va_end(a); }
void t_skip(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("  SKIP "); vprintf(fmt, a); printf("\n"); va_end(a); g_cur_skip = 1; }

/* A crash inside a test is reported as a failure of that test and the run continues. */
static LONG CALLBACK veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (!g_in_test) return EXCEPTION_CONTINUE_SEARCH;
    if (c == 0x40010006 || c == 0x406D1388 || c == DBG_PRINTEXCEPTION_C || c == 0x4001000A) return EXCEPTION_CONTINUE_SEARCH;
    if ((c & 0xF0000000) != 0xC0000000) return EXCEPTION_CONTINUE_SEARCH;
    printf("  FAIL exception 0x%08lx at %p in %s\n", c, ep->ExceptionRecord->ExceptionAddress, g_cur);
    g_fail++; g_cur_fail++;
    g_in_test = 0;
    longjmp(g_jb, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

const char *t_self_path(void) { static char p[MAX_PATH * 2]; if (!p[0]) GetModuleFileNameA(NULL, p, sizeof p); return p; }
HANDLE t_spawn_child(const char *name, const char *extra, DWORD flags, HANDLE hin, HANDLE hout, HANDLE herr, BOOL inherit, const char *env, PROCESS_INFORMATION *pi_out) {
    char cmd[4096]; STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (hin || hout || herr) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdInput = hin; si.hStdOutput = hout; si.hStdError = herr; }
    snprintf(cmd, sizeof cmd, "\"%s\" --child %s %s", t_self_path(), name, extra ? extra : "");
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, inherit, flags, (LPVOID)env, NULL, &si, &pi)) return NULL;
    if (pi_out) *pi_out = pi; else CloseHandle(pi.hThread);
    return pi.hProcess;
}
DWORD t_run_child(const char *name, const char *extra, DWORD timeout_ms) {
    PROCESS_INFORMATION pi; DWORD code = 0xDEAD0000;
    HANDLE h = t_spawn_child(name, extra, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return 0xDEAD0000;
    if (WaitForSingleObject(h, timeout_ms) != WAIT_OBJECT_0) { TerminateProcess(h, 1); WaitForSingleObject(h, 5000); code = 0xDEAD0001; }
    else GetExitCodeProcess(h, &code);
    CloseHandle(pi.hThread); CloseHandle(h);
    return code;
}

static char g_tmp[MAX_PATH];
static void rmtree(const char *dir) {
    char pat[MAX_PATH + 4], sub[MAX_PATH + 300]; WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            snprintf(sub, sizeof sub, "%s\\%s", dir, fd.cFileName);
            SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rmtree(sub); else DeleteFileA(sub);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}
static void cleanup_tmp(void) { if (g_tmp[0]) { char d[MAX_PATH]; strcpy(d, g_tmp); d[strlen(d) - 1] = 0; rmtree(d); g_tmp[0] = 0; } }
const char *t_tmpdir(void) {
    if (!g_tmp[0]) {
        char base[MAX_PATH]; if (!GetTempPathA(sizeof base, base)) strcpy(base, ".\\");
        snprintf(g_tmp, sizeof g_tmp, "%s" T_TMP_PREFIX "_%lu\\", base, GetCurrentProcessId());
        CreateDirectoryA(g_tmp, NULL);
        atexit(cleanup_tmp);
    }
    return g_tmp;
}
void t_tmpfile(char *out, size_t cap, const char *leaf) { snprintf(out, cap, "%s%s", t_tmpdir(), leaf); }

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : NULL;
    int ran = 0, tskip = 0, tfail = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc > 2 && !strcmp(argv[1], "--child")) {
        for (int i = 0; i < g_nc; i++) if (!strcmp(g_c[i].n, argv[2])) return g_c[i].f(argc - 2, argv + 2);
        return 0xC0DE0001;
    }
    AddVectoredExceptionHandler(1, veh);
    if (!t_module(g_mods[0])) { printf("FATAL cannot load %s\n", g_mods[0]); return 2; }
    for (int i = 0; i < g_nt; i++) {
        if (filter && !strstr(g_t[i].n, filter)) continue;
        g_cur = g_t[i].n; g_cur_fail = g_cur_skip = 0;
        printf("RUN  %s\n", g_cur);
        SetLastError(0);
        if (!setjmp(g_jb)) { g_in_test = 1; g_t[i].f(); g_in_test = 0; }
        ran++;
        if (g_cur_fail) tfail++; else if (g_cur_skip) tskip++;
        printf("%s %s\n", g_cur_fail ? "FAIL" : g_cur_skip ? "SKIP" : "OK  ", g_cur);
    }
    printf("SUMMARY tests=%d failed=%d skipped=%d checks_pass=%d checks_fail=%d\n", ran, tfail, tskip, g_pass, g_fail);
    return tfail ? 1 : 0;
}
