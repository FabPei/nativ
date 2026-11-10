/* system_lang_test.c -- GAME-LANG on apputil.c itself: sceAppUtilSystemParamGetInt(1 = language) answers the
 * language game_lang.h decides, here "auto" with a German host locale (window_host_locale, defined below as window.c's
 * is) -> 4; the enter button (2) stays cross (1), the date format (4) 0, an unknown id the parameter error.
 *
 * Fail-before: apputil.c wrote 1 (English US) whatever the setting or the locale -- a German player saw every game in
 * English. Includes apputil.c (APPUTIL_UNDER_TEST overrides the copy for a fail-before run). Build: README.md here.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#ifndef APPUTIL_UNDER_TEST
#define APPUTIL_UNDER_TEST "../core/apputil.c"
#endif
#include APPUTIL_UNDER_TEST
#include "../core/imedlg.c"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }
uint32_t guest_alloc(uint32_t size, uint32_t align) { (void)align; static uint32_t heap = 0x81000000; uint32_t a = heap; heap += size; return a; }
void gwatch_read_open(uint32_t addr, uint32_t len) { (void)addr; (void)len; }
int net_online(void) { return 0; }
const char *net_online_needs(void) { return 0; }
int overlay_draws_hud(void) { return 0; }
void overlay_toast_titled(const char *title, const char *body, double seconds) { (void)title; (void)body; (void)seconds; }
void plat_random(void *buf, size_t n) { memset(buf, 0x5a, n); }
const char *settings_str(const char *key, const char *def, char *buf, int bufsize) { (void)key; (void)buf; (void)bufsize; return def; }
void settings_set_str(const char *key, const char *v) { (void)key; (void)v; }
void settings_set_own(const char *key, const char *v) { (void)key; (void)v; }
int settings_save(void) { return 1; }
uint32_t guest_call(Cpu *parent, uint32_t fn, uint32_t a0, uint32_t a1) { (void)parent; (void)fn; (void)a0; (void)a1; return 0; }
int sched_block_begin(void) { return 0; }
void sched_block_end(void) {}
int window_host_locale(char *lang, int nl, char *country, int nc) { snprintf(lang, (size_t)nl, "de"); snprintf(country, (size_t)nc, "DE"); return 1; }

static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static HleFn find(const char *n) { for (const HleEntry *e = hle_apputil; e->name; e++) if (!strcmp(e->name, n)) return e->fn; return 0; }
static Cpu cpu;
static uint32_t param(uint32_t id, uint32_t out) {
	HleFn f = find("sceAppUtilSystemParamGetInt"); memset(&cpu, 0, sizeof cpu); cpu.r[0] = id; cpu.r[1] = out; cpu.r[13] = 0x10000;
	if (!f) { printf("  missing sceAppUtilSystemParamGetInt\n"); return 0xffffffffu; }
	f(&cpu); return cpu.r[0];
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 1; }
	unsetenv("RECOMP_GAME_LANG"); unsetenv("RECOMP_HEADLESS");
	const uint32_t out = 0x200000;
	WR32(out, 0xffffffffu);
	check("language (1), setting auto, host locale de_DE -> German (4)", param(1, out) == 0 && RD32(out) == 4);
	WR32(out, 0xffffffffu);
	check("asked again: the same, decided once for the run", param(1, out) == 0 && RD32(out) == 4);
	WR32(out, 0xffffffffu);
	check("enter button (2) stays cross (1)", param(2, out) == 0 && RD32(out) == 1);
	check("date format (4) stays 0", param(4, out) == 0 && RD32(out) == 0);
	check("an unknown id (8) is the parameter error", param(8, out) == 0x80100600u);
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
