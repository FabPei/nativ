/* modload_test.c -- MODLOAD-01: game modules loaded on demand (sceKernelLoadStartModule / sceKernelStopUnloadModule
 * of a module the game ships in app0 outside sce_module/, e.g. "app0:bin/<name>.self").
 *
 * The contract (the console loader's): loading a known module copies its image in, registers its exports and clears
 * the import cache (so the main program's import reaches it), runs module_start(args, argp), writes its result to
 * *status and returns a module id (> 0); unloading runs module_stop, takes the exports away and clears the cache
 * again; a path with no module image is an error, not a silent success; the game's sce_module/ modules, loaded and
 * started with the program, answer with an id without loading anything twice.
 *
 * Before (MODLOAD-01): both calls were unimplemented imports -> the generic stub (r0 = 0, nothing loaded). A title
 * that loads its scene modules this way reached its own fatal path at start-up. Fail-before run:
 *   cc ... -DMODLOAD_UNDER_TEST='"modload_before.c"' modload_test.c   (the old generic-stub behaviour)
 *
 * Run (WSL, from this folder):
 *   cc -O1 -g -Wall -Wno-unused-function -I ../core -I ../../recompiler modload_test.c -o /tmp/modload_test && /tmp/modload_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hle.h"

/* ---- shims (as the other fixtures) */
uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

/* the loader main.c provides, faked: images "a.img" (start 0x88001001, stop 0x88001101) and "b.img" (start only) */
static int loads, drops, resets;
static uint32_t calls[8][3]; static int ncalls;
int image_load_module(const char *img, uint32_t *start, uint32_t *stop, uint32_t *lo, uint32_t *hi) {
	if (strstr(img, "a.img")) { *start = 0x88001001; *stop = 0x88001101; *lo = 0x88000000; *hi = 0x88010000; }
	else if (strstr(img, "b.img")) { *start = 0x89001001; *stop = 0; *lo = 0x89000000; *hi = 0x89010000; }
	else return -1;
	loads++; return 0;
}
void image_drop_exports(uint32_t lo, uint32_t hi) { (void)lo; (void)hi; drops++; }
void imports_reset(void) { resets++; }
uint32_t guest_call4(Cpu *parent, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
	(void)parent; (void)a2; (void)a3;
	if (ncalls < 8) { calls[ncalls][0] = fn; calls[ncalls][1] = a0; calls[ncalls][2] = a1; ncalls++; }
	return fn == 0x88001001 ? 0 : fn == 0x89001001 ? 1 : 7;   /* a's module_start 0 (SCE_KERNEL_START_SUCCESS), b's 1 */
}

#ifndef MODLOAD_UNDER_TEST
#define MODLOAD_UNDER_TEST "../core/modload.c"
#endif
#include MODLOAD_UNDER_TEST

static int fails;
static void check(const char *what, int ok) { printf("%-66s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

static HleFn find(const char *n) { for (const HleEntry *e = hle_modload; e->name; e++) if (!strcmp(e->name, n)) return e->fn; return 0; }
static Cpu c;
#define SP 0x10000
#define STR 0x20000
#define STATUS 0x30000
static uint32_t call6(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t opt, uint32_t status) {
	HleFn f = find(name);
	memset(&c, 0, sizeof c); c.r[13] = SP; c.r[0] = a0; c.r[1] = a1; c.r[2] = a2; c.r[3] = 0;
	*(uint32_t *)G(SP) = opt; *(uint32_t *)G(SP + 4) = status;
	if (!f) return 0;   /* (no implementation: the generic stub's r0 = 0) */
	f(&c); return c.r[0];
}
static uint32_t load(const char *path, uint32_t args, uint32_t argp) {
	strcpy((char *)G(STR), path); *(uint32_t *)G(STATUS) = 0xdead;
	return call6("sceKernelLoadStartModule", STR, args, argp, 0, STATUS);
}

int main(void) {
	recomp_mem = calloc(1, 0x100000);
	setenv("RECOMP_LOAD_MODULES", "bin/a.self=/x/a.img;modules_psvita/B.suprx=/x/b.img", 1);

	uint32_t id = load("app0:bin/a.self", 4, 0x1234);
	check("1. LoadStartModule(app0:bin/a.self) returns a module id (> 0)", id && !(id & 0x80000000u));
	check("1. its image was loaded and the import cache cleared", loads == 1 && resets == 1);
	check("1. module_start ran with (args, argp)", ncalls == 1 && calls[0][0] == 0x88001001 && calls[0][1] == 4 && calls[0][2] == 0x1234);
	check("1. *status = module_start's result", *(uint32_t *)G(STATUS) == 0);

	uint32_t r = call6("sceKernelStopUnloadModule", id, 0, 0, 0, STATUS);
	check("2. StopUnloadModule -> 0, module_stop ran", r == 0 && ncalls == 2 && calls[1][0] == 0x88001101);
	check("2. its exports dropped and the import cache cleared", drops == 1 && resets == 2);
	check("2. unloading it again -> MODULEMGR_NOENT", call6("sceKernelStopUnloadModule", id, 0, 0, 0, 0) == 0x8002D01Cu);

	uint32_t id2 = load("app0:/modules_psvita/b.suprx", 0, 0);   /* (path case and a leading slash do not matter) */
	check("3. a second module loads (case-insensitive path), status 1", id2 && !(id2 & 0x80000000u) && *(uint32_t *)G(STATUS) == 1 && loads == 2);
	uint32_t id3 = load("app0:bin/a.self", 0, 0);
	check("3. reloading a: its image copied in again (fresh .data)", id3 == id && loads == 3 && resets == 4);

	check("4. a path with no module image -> an error (0x80010002)", load("app0:bin/missing.self", 0, 0) == 0x80010002u);
	uint32_t pre = load("app0:sce_module/libfios2.suprx", 0, 0);
	check("5. a sce_module/ module (loaded at start) -> an id, nothing loaded", pre && !(pre & 0x80000000u) && loads == 3);
	check("5. ... and unloading it -> 0", call6("sceKernelStopUnloadModule", pre, 0, 0, 0, 0) == 0);

	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
