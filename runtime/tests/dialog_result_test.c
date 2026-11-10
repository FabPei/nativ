/* dialog_result_test.c -- what the dialogs that finish at once answer (apputil.c), offline:
 *
 * - sceMsgDialogGetResult fills SceMsgDialogResult {mode, result, buttonId} as the console does after the player
 *   confirmed the message: mode as opened (SceMsgDialogParam.mode at +80), result OK (0), buttonId the first
 *   button (1: OK / YES / BUTTON1) -- or INVALID (0) for a user message with button type NONE and a progress bar,
 *   which have no button to take.
 * - sceNetCheckDialogGetResult offline: the check cannot pass, the user backs out of the "could not connect"
 *   page -- result USER_CANCELED (1), psnModeSucceeded 0; online it stays OK (0) and 1.
 *
 * Fail-before: GetResult wrote only word 0 -- mode 0, buttonId left as the game had it (0 = no button) -- and the
 * offline net check answered OK. One title then waited for a sign-in for ever behind its dimmed title screen.
 * Includes apputil.c (APPUTIL_UNDER_TEST overrides the copy for a fail-before run). Build: README.md here.
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
static int online;
int net_online(void) { return online; }
const char *net_online_needs(void) { return 0; }
int overlay_draws_hud(void) { return 0; }
void overlay_toast_titled(const char *title, const char *body, double seconds) { (void)title; (void)body; (void)seconds; }
void plat_random(void *buf, size_t n) { memset(buf, 0x5a, n); }
const char *settings_str(const char *key, const char *def, char *buf, int bufsize) { (void)key; (void)buf; (void)bufsize; return def; }
void settings_set_str(const char *key, const char *v) { (void)key; (void)v; }
void settings_set_own(const char *key, const char *v) { (void)key; (void)v; }
int settings_save(void) { return 1; }

static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static HleFn find(const char *n) { for (const HleEntry *e = hle_apputil; e->name; e++) if (!strcmp(e->name, n)) return e->fn; return 0; }
static Cpu cpu;
static uint32_t call(const char *n, uint32_t a0) { HleFn f = find(n); memset(&cpu, 0, sizeof cpu); cpu.r[0] = a0; cpu.r[13] = 0x10000; if (!f) { printf("  missing %s\n", n); return 0xffffffffu; } f(&cpu); return cpu.r[0]; }
#define PARAM 0x20000
#define USERMSG 0x20200
#define RES 0x20400
static uint32_t rd(uint32_t a) { uint32_t v; memcpy(&v, recomp_mem + a, 4); return v; }
static void wr(uint32_t a, uint32_t v) { memcpy(recomp_mem + a, &v, 4); }

/* open a message dialog of `mode` (user-message button type `btype`), read its result; returns buttonId */
static void msg(uint32_t mode, uint32_t btype, uint32_t want_button, const char *what) {
	memset(recomp_mem + PARAM, 0, 0x600);
	wr(PARAM + 80, mode);
	if (mode == 1) { wr(PARAM + 84, USERMSG); wr(USERMSG, btype); }
	memset(recomp_mem + RES, 0xee, 12); wr(RES, 0xdead); wr(RES + 8, 0);   /* the game zeroed buttonId */
	int ok = call("sceMsgDialogInit", PARAM) == 0 && call("sceMsgDialogGetStatus", 0) == 2 && call("sceMsgDialogGetResult", RES) == 0;
	char w[160]; snprintf(w, sizeof w, "%s: mode %u, result OK, button %u (got %u/%u/%u)", what, mode, want_button, rd(RES), rd(RES + 4), rd(RES + 8));
	check(w, ok && rd(RES) == mode && rd(RES + 4) == 0 && rd(RES + 8) == want_button);
	call("sceMsgDialogTerm", 0);
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }

	msg(1, 0, 1, "user message, button type OK");
	msg(1, 1, 1, "user message, YESNO (the player took YES)");
	msg(1, 2, 0, "user message, button type NONE");
	msg(2, 0, 1, "system message");
	msg(3, 0, 1, "error-code message");
	msg(4, 0, 0, "progress bar");

	for (online = 0; online < 2; online++) {
		memset(recomp_mem + RES, 0xee, 8);
		int ok = call("sceNetCheckDialogInit", PARAM) == 0 && call("sceNetCheckDialogGetStatus", 0) == 2 && call("sceNetCheckDialogGetResult", RES) == 0;
		char w[160]; snprintf(w, sizeof w, "net check %s: result %s, psnModeSucceeded %d (got %u/%u)", online ? "online" : "offline",
		                      online ? "OK (0)" : "USER_CANCELED (1)", online, rd(RES), rd(RES + 4));
		check(w, ok && rd(RES) == (online ? 0u : 1u) && rd(RES + 4) == (uint32_t)online);
		call("sceNetCheckDialogTerm", 0);
	}
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
