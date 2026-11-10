/* rec.h - recorder for GDI data probes (format: docs/PROBE_FORMAT.md, version REC_FORMAT).
 *
 * A probe is a RECORDER, not a test: it measures and writes stable, line-oriented, diffable rows.
 * No dependencies beyond kernel32/user32/gdi32/advapi32 (comctl32 is loaded dynamically for its version).
 *
 *   int main(int argc, char **argv) {
 *       rec_init(&argc, argv, "hello", "1");          // parses --out/--com1/--stdout/--tag/--env, writes REC + ENV
 *       rec_schema("tm", "face req_h", "h:px a:px d:px");
 *       rk k; rk_reset(&k); rk_s(&k, "face", "Segoe UI"); rk_i(&k, "req_h", -12);
 *       rv v; rv_reset(&v); rv_i(&v, 15); rv_i(&v, 12); rv_i(&v, 3);
 *       rec_row("tm", &k, &v);
 *       return rec_finish(0);                         // writes END <probe> rows=N crc32=X status=ok
 *   }
 *
 * Output lines (all ASCII, LF only, every token escaped by rec_esc):
 *   REC 1 <probe> v=<ver> variant=<variant> tag=<tag>
 *   ENV <probe> <key> <value>
 *   S   <probe> <table> <idcol;idcol...> | <col[:unit]> <col[:unit]> ...
 *   R   <probe> <table> <k=v;k=v...> | <val> <val> ...
 *   H   <probe> <table> <key> sha256=<16 hex> len=<N>
 *   B   <probe> <table> <key> <w>x<h>x<bpp> <hex|b64>:<data>
 *   N   <probe> <free text, escaped as one token>
 *   X   <probe> <name> len=<N> sha256=<64 hex> chunks=<K>        then K lines
 *   XC  <probe> <name> <seq> <b64 chunk>
 *   END <probe> rows=<N> crc32=<8 HEX> status=<ok|crash|abort|fail>
 */
#ifndef REC_H
#define REC_H
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define REC_FORMAT 1

/* ---------- lifecycle ---------- */
/* Consumes the recorder options from argv (compacting argc/argv) and writes the REC line + ENV block.
 * Options: --out FILE | --com1 | --stdout (default)   --tag T   --env full|min|none   --variant V
 * The probe's own options remain in argv[1..argc-1]. Returns 0, or exits with code 3 if the sink can't open. */
int  rec_init(int *argc, char **argv, const char *probe, const char *version);
int  rec_finish(int rc);                 /* END line (status ok if rc==0 else fail); flushes; returns rc */
void rec_flush(void);
const char *rec_probe_name(void);
int  rec_rows(void);                     /* lines written so far (excluding END) */

/* ---------- rows ---------- */
#define RK_MAX 1024
#define RV_MAX 8192
typedef struct { char b[RK_MAX]; int n; } rk;   /* key builder: k=v;k=v (escaped) */
typedef struct { char b[RV_MAX]; int n; } rv;   /* value builder: v v v (escaped) */
void rk_reset(rk *k);
void rk_s(rk *k, const char *name, const char *val);
void rk_w(rk *k, const char *name, const WCHAR *val);     /* UTF-16 -> UTF-8 -> escaped */
void rk_i(rk *k, const char *name, long long val);
void rk_x(rk *k, const char *name, unsigned long long val); /* 0x%x */
void rv_reset(rv *v);
void rv_s(rv *v, const char *s);
void rv_w(rv *v, const WCHAR *s);
void rv_i(rv *v, long long x);
void rv_u(rv *v, unsigned long long x);
void rv_x(rv *v, unsigned long long x);                   /* 0x%x */
void rv_f(rv *v, double x);                               /* %.6g */
void rv_na(rv *v);                                        /* "!" = call failed / not available */
void rv_raw(rv *v, const char *tok);                      /* caller guarantees tok is already a valid token */

void rec_schema(const char *table, const char *idcols, const char *cols); /* idcols "a b" -> a;b, cols "x:px y" */
void rec_row(const char *table, const rk *key, const rv *vals);
void rec_rowf(const char *table, const char *rawkey, const char *fmt, ...); /* rawkey/fmt output must be valid tokens */
void rec_env(const char *key, const char *fmt, ...);      /* value escaped as one token */
void rec_envw(const char *key, const WCHAR *val);
void rec_note(const char *fmt, ...);                      /* N line; escaped as one token */
void rec_hash(const char *table, const rk *key, const void *data, size_t len);
/* Tiny bitmap: data is w*h pixels packed MSB-first at bpp (1,2,4,8,24,32), rows padded to a byte. hex when <= 64 bytes. */
void rec_bitmap(const char *table, const rk *key, int w, int h, int bpp, const void *data, size_t len);
void rec_blob(const char *name, const void *data, size_t len);  /* X + XC lines, 3072 bytes/chunk */

/* ---------- utilities (also usable standalone) ---------- */
size_t rec_esc(char *dst, size_t cap, const char *src); /* returns length; always NUL-terminates */
typedef struct { uint32_t h[8]; uint64_t n; uint8_t buf[64]; size_t bl; } rec_sha256_ctx;
void rec_sha256_init(rec_sha256_ctx *c);
void rec_sha256_update(rec_sha256_ctx *c, const void *p, size_t n);
void rec_sha256_final(rec_sha256_ctx *c, uint8_t out[32]);
void rec_sha256(const void *p, size_t n, uint8_t out[32]);
uint32_t rec_crc32(uint32_t crc, const void *p, size_t n);   /* IEEE 802.3, crc=0 to start */
void rec_hex(char *dst, const void *p, size_t n);             /* lowercase, NUL-terminated, dst >= 2n+1 */
size_t rec_b64(char *dst, const void *p, size_t n);           /* NUL-terminated, dst >= 4*ceil(n/3)+1 */
int  rec_file_sha256(const char *path, uint8_t out[32], unsigned long long *size);

/* ---------- GDI helpers ---------- */
/* Render into a 32bpp top-down DIB (white background), then reduce to greyscale.
 * rec_gray_from_dib: bits = BGRA top-down w*h; out = packed grey at bpp 8/4/2/1 (MSB first, rows byte-padded).
 * Grey = (R*77 + G*150 + B*29) >> 8 (BT.601, integer). Returns bytes written. */
size_t rec_gray_from_dib(const uint32_t *bits, int w, int h, int bpp, uint8_t *out, size_t cap);
typedef struct { HDC dc; HBITMAP bmp, old; uint32_t *bits; int w, h; } rec_dib;
int  rec_dib_create(rec_dib *d, int w, int h);  /* memory DC + DIB section, filled white; 1 on success */
void rec_dib_clear(rec_dib *d, uint32_t bgra);
void rec_dib_free(rec_dib *d);

#endif
