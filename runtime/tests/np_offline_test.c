/* np_offline_test.c -- the NP service state of a game run offline (net_online() off, the default): np.c hle_np_offline.
 *
 * The contract (a Vita that is not signed in to PSN): sceNpGetServiceState answers SCE_NP_SERVICE_STATE_SIGNED_OUT (1);
 * a callback registered with sceNpRegisterServiceStateCallback(cb, arg) is called once, as cb(SIGNED_OUT, arg), at the
 * next sceNpCheckCallback, and not again while the state stays the same; after sceNpUnregisterServiceStateCallback it is
 * not called; a registration replaces the one before (one callback, as online np.c keeps it).
 *
 * Before: offline, these were unimplemented imports -> the generic stubs (0 for Register/Check -- the callback never
 * called --, "not initialised" for GetServiceState). One title's start-up waits to learn the NP state before it takes
 * its offline path and sat behind a cleared screen. Fail-before: hle_np_offline does not exist (does not compile).
 *
 * Run (WSL, from this folder):
 *   cc -O1 -g -Wall -Wno-unused-function -D_GNU_SOURCE -I ../core -I ../net -I ../platform -I ../../recompiler -I ../.. np_offline_test.c -o /tmp/np_offline_test -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/np_offline_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "hle.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

/* the guest calls np.c makes, recorded */
static uint32_t calls[8][3]; static int ncalls;
uint32_t guest_call4(Cpu *parent, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
	(void)parent; (void)a2; (void)a3;
	if (ncalls < 8) { calls[ncalls][0] = fn; calls[ncalls][1] = a0; calls[ncalls][2] = a1; }
	ncalls++;
	return 0;
}

#include "../net/np.c"

static int fails;
static void check(const char *what, int ok) { printf("%-70s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static HleFn find(const char *n) { for (const HleEntry *e = hle_np_offline; e->name; e++) if (!strcmp(e->name, n)) return e->fn; return 0; }
static Cpu cpu;
static uint32_t call(const char *name, uint32_t a0, uint32_t a1) {
	HleFn f = find(name);
	memset(&cpu, 0, sizeof cpu); cpu.r[0] = a0; cpu.r[1] = a1; cpu.r[13] = 0x10000;
	if (!f) return 0xffffffffu;
	f(&cpu); return cpu.r[0];
}
#define STATE 0x20000

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }

	*(uint32_t *)G(STATE) = 0xdead;
	check("sceNpGetServiceState -> 0", call("sceNpGetServiceState", STATE, 0) == 0);
	check("  state SIGNED_OUT (1)", *(uint32_t *)G(STATE) == 1);

	check("sceNpCheckCallback with nothing registered -> 0, no call", call("sceNpCheckCallback", 0, 0) == 0 && ncalls == 0);
	check("sceNpRegisterServiceStateCallback(0x81000101, 0x1234) -> 0", call("sceNpRegisterServiceStateCallback", 0x81000101, 0x1234) == 0);
	check("  not called at registration", ncalls == 0);
	check("sceNpCheckCallback -> 0", call("sceNpCheckCallback", 0, 0) == 0);
	check("  the callback ran once as cb(SIGNED_OUT, arg)", ncalls == 1 && calls[0][0] == 0x81000101 && calls[0][1] == 1 && calls[0][2] == 0x1234);
	call("sceNpCheckCallback", 0, 0);
	check("  the state is unchanged: not again at the next check", ncalls == 1);

	call("sceNpRegisterServiceStateCallback", 0x81000201, 0x55);
	call("sceNpCheckCallback", 0, 0);
	check("a new registration hears the state at the next check (and only it)", ncalls == 2 && calls[1][0] == 0x81000201 && calls[1][1] == 1 && calls[1][2] == 0x55);

	call("sceNpRegisterServiceStateCallback", 0x81000301, 0);
	check("sceNpUnregisterServiceStateCallback -> 0", call("sceNpUnregisterServiceStateCallback", 0, 0) == 0);
	call("sceNpCheckCallback", 0, 0);
	check("  an unregistered callback is not called", ncalls == 2);

	check("online's table still answers ONLINE (3), unchanged", ({ *(uint32_t *)G(STATE) = 0; memset(&cpu, 0, sizeof cpu); cpu.r[0] = STATE; h_np_state(&cpu); *(uint32_t *)G(STATE) == 3; }));

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
