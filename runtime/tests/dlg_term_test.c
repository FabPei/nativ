/* dlg_term_test.c -- the common-dialog Term contract: a Term with no dialog in use must return
 * SCE_COMMON_DIALOG_ERROR_NOT_IN_USE (0x80020411), as the console does, while a real Init..Term
 * still terminates with 0. The save-data dialog's pre-init Term returned 0 here, which the game's
 * dialog flow reads (measured 2026-11-04 against a working reference run's exact return code).
 *
 * Case 1 is the fail-before / pass-after case: the pre-fix Term returned 0 on a fresh state.
 * Cases 2-3 pin the surrounding lifecycle (an in-use dialog's Term returns 0 and closes it; a
 * further Term is NOT_IN_USE again).
 *
 * Includes apputil.c directly (override APPUTIL_UNDER_TEST to compile a different copy for a
 * fail-before run) and supplies its few external symbols. Build and run: see README.md here.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef APPUTIL_UNDER_TEST
#define APPUTIL_UNDER_TEST "../core/apputil.c"
#endif
#include APPUTIL_UNDER_TEST
#include "../core/imedlg.c"   /* (HLE-LIB-05: apputil.c's IME dialog shares its state) */

/* ---- apputil.c expects these (same shim style as monitor_test.c / kernel_test.c) */
uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }
uint32_t guest_alloc(uint32_t size, uint32_t align) { (void)align; static uint32_t heap = 0x81000000; uint32_t a = heap; heap += size; return a; }
uint32_t guest_call(Cpu *parent, uint32_t fn, uint32_t a0, uint32_t a1) { (void)parent; (void)fn; (void)a0; (void)a1; return 0; }   /* (HLE-LIB-06: the in-game IME hands its events to the game) */
void gwatch_read_open(uint32_t addr, uint32_t len) { (void)addr; (void)len; }
int net_online(void) { return 0; }
const char *net_online_needs(void) { return ""; }
int overlay_draws_hud(void) { return 0; }
void overlay_toast_titled(const char *title, const char *body, double seconds) { (void)title; (void)body; (void)seconds; }
void plat_random(void *buf, size_t n) { memset(buf, 0x5a, n); }
const char *settings_str(const char *key, const char *def, char *buf, int bufsize) { (void)key; (void)buf; (void)bufsize; return def; }
void settings_set_str(const char *key, const char *v) { (void)key; (void)v; }
void settings_set_own(const char *key, const char *v) { (void)key; (void)v; }
int settings_save(void) { return 1; }

static int fails;
static void check(const char *what, int ok) {
	printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) fails++;
}

int main(void) {
	Cpu cpu = {0};

	/* Case 1 (fail-before / pass-after): Term on a fresh state (no dialog ever initialized). */
	h_save_dlg_term(&cpu);
	check("pre-init save-dlg Term returns NOT_IN_USE (0x80020411)", cpu.r[0] == 0x80020411u);

	/* Case 2: an in-use dialog's Term terminates it and returns 0 (the state the lifecycle
	 * leaves after Init; set here directly -- Init's param handling is covered by game runs). */
	sd.open = 1; sd.status = ST_RUNNING; sd.sub = ST_RUNNING;
	memset(&cpu, 0, sizeof cpu);
	h_save_dlg_term(&cpu);
	check("in-use save-dlg Term returns 0", cpu.r[0] == 0);
	check("in-use save-dlg Term resets the state", !sd.open && sd.status == ST_NONE && sd.sub == ST_NONE);

	/* Case 3: once terminated, Term is NOT_IN_USE again. */
	memset(&cpu, 0, sizeof cpu);
	h_save_dlg_term(&cpu);
	check("save-dlg Term after Term returns NOT_IN_USE", cpu.r[0] == 0x80020411u);

	/* Case 4: the simple dialogs share the same contract (their Term handler is generated from
	 * one macro): a Term before any Init is NOT_IN_USE. */
	memset(&cpu, 0, sizeof cpu);
	h_msg_dlg_term(&cpu);
	check("pre-init simple-dialog Term returns NOT_IN_USE", cpu.r[0] == 0x80020411u);
	dialog_open[DLG_MSG] = 1;
	memset(&cpu, 0, sizeof cpu);
	h_msg_dlg_term(&cpu);
	check("in-use simple-dialog Term returns 0 and closes", cpu.r[0] == 0 && !dialog_open[DLG_MSG]);

	if (fails) { printf("%d checks FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
