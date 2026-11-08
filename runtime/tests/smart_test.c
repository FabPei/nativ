/*
 * runtime/media/smart.c (SceSmart) driven the way Soul Sacrifice Delta's AR mode calls it: Init, a learned image
 * target registered with scene mapping, Start, frames of SceneMappingRun with the state word at result+8 read back,
 * the two point/landmark counts, a state-4 style Reset/Stop/Start, and the exit order. Calls out of order must be
 * refused and logged, and a point/landmark query never answers a negative count (the game stores it as one).
 *
 * Fail-before: -DSMART_TEST_BEFORE drives the same sequence through the generic stub (stubret.h: 0, nothing written),
 * which is what every SceSmart import got before smart.c.
 *
 *     gcc -O1 -g -Wall -Wno-unused-function $INC smart_test.c -o /tmp/smart_test && /tmp/smart_test
 */
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include "../media/smart.c"
#include "../core/stubret.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }
static int toasts;
void overlay_toast_titled(const char *title, const char *body, double s) { toasts++; printf("     toast (%.0f s): %s: %s\n", s, title, body); }

static int fails;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }
static uint32_t call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
	Cpu c = {0}; c.r[0] = a0; c.r[1] = a1; c.r[2] = a2; c.r[3] = a3; c.r[13] = 0x100000;
#ifndef SMART_TEST_BEFORE
	for (const HleEntry *e = hle_smart; e->name; e++) if (!strcmp(e->name, name)) { e->fn(&c); return c.r[0]; }
#endif
	return stub_family_result(name, 0);   /* the generic stub: an answer, nothing written */
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 1; }
	const uint32_t param = 0x200000, idp = 0x200100, tparam = 0x200110, result = 0x200200, image = 0x300000, buf = 0x400000;
	WR32(param + 4, 0x81759f5du); WR32(param + 8, 0x81759f7bu);   /* the game's alloc / free callbacks */

	check(call("sceSmartSceneMappingRun", result, image, 0, 0) == SMART_ERR_STATE, "Run before Init is refused");
	check(call("sceSmartInit", param, 0, 0, 0) == 0, "Init");
	check(call("sceSmartInit", param, 0, 0, 0) == SMART_ERR_STATE, "a second Init is refused");
	WR32(idp, 0xFFFFFFFFu);   /* the game presets -1 and retries while it stays -1 */
	check(call("sceSmartCreateLearnedImageTarget", idp, tparam, 0, 0) == 0, "CreateLearnedImageTarget");
	uint32_t id = RD32(idp);
	printf("     target id %d\n", (int)id);
	check(id != 0xFFFFFFFFu && id != 0, "a real target handle is written (no -1 retry loop)");
	check(call("sceSmartSceneMappingRegisterTarget", id, 0, 0, 0) == 0, "RegisterTarget");
	check(call("sceSmartSceneMappingRegisterTarget", 0xFFFFFFFFu, 0, 0, 0) == SMART_ERR_STATE, "RegisterTarget(-1) is refused");
	check(call("sceSmartSceneMappingRun", result, image, 0, 0) == SMART_ERR_STATE, "Run before Start is refused");
	check(call("sceSmartSceneMappingStart", 0, 0, 0, 0) == 0, "SceneMapping Start(0)");
	check(toasts == 1, "the player is told once that nothing will be found");

	int stays0 = 1;
	for (int f = 0; f < 120; f++) {
		WR32(result + SMART_STATE_OFF, f % 5);   /* whatever was there: a stale "tracking" (2) or "failed" (4) */
		if (call("sceSmartSceneMappingRun", result, image, (uint32_t)f, 0) != 0) stays0 = 0;
		if (RD32(result + SMART_STATE_OFF) != 0) stays0 = 0;
	}
	printf("     state after 120 runs: %u\n", RD32(result + SMART_STATE_OFF));
	check(stays0, "120 runs: each answers 0 and writes state 0 (nothing found), whatever was in the word");
	check(call("sceSmartSceneMappingGetInitializationPointInfo", buf, 0x40, 0, 0) == 0, "no initialisation points");
	check(call("sceSmartSceneMappingGetLandmarkInfo", buf, 0x200, 0, 0) == 0, "no landmarks");
	check(call("sceSmartSceneMappingPropagateResult", result, 0, 0, 0) == 0, "PropagateResult while mapping");

	/* the game's state-4 recovery: Reset, Stop, Start(0) */
	check(call("sceSmartSceneMappingReset", 0, 0, 0, 0) == 0 && call("sceSmartSceneMappingStop", 0, 0, 0, 0) == 0 && call("sceSmartSceneMappingStart", 0, 0, 0, 0) == 0, "Reset, Stop, Start");
	check(call("sceSmartSceneMappingStart", 0, 0, 0, 0) == SMART_ERR_STATE, "a second Start is refused");

	/* exit order */
	check(call("sceSmartSceneMappingStop", 0, 0, 0, 0) == 0, "Stop");
	check(call("sceSmartSceneMappingStop", 0, 0, 0, 0) == SMART_ERR_STATE, "Stop when stopped is refused");
	uint32_t n = call("sceSmartSceneMappingGetLandmarkInfo", buf, 0x200, 0, 0);
	check(n == 0, "a landmark query while stopped still answers 0 entries, never a negative count");
	check(call("sceSmartSceneMappingUnregisterTarget", id, 0, 0, 0) == 0, "UnregisterTarget");
	check(call("sceSmartSceneMappingUnregisterTarget", id, 0, 0, 0) == SMART_ERR_STATE, "unregistering twice is refused");
	check(call("sceSmartDestroyTarget", id, 0, 0, 0) == 0, "DestroyTarget");
	check(call("sceSmartDestroyTarget", id, 0, 0, 0) == SMART_ERR_STATE, "destroying twice is refused");
	check(call("sceSmartRelease", 0, 0, 0, 0) == 0, "Release");
	check(call("sceSmartRelease", 0, 0, 0, 0) == SMART_ERR_STATE, "Release when not initialised is refused");
	check(call("sceSmartInit", param, 0, 0, 0) == 0 && call("sceSmartRelease", 0, 0, 0, 0) == 0, "the AR mode can be entered again");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
