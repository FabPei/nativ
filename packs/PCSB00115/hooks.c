/*
 * Silent Hill: Book of Memories (PCSB00115): its hooks, built into this game alone (runtime/build.py RECOMP_PACK_SOURCES),
 * placed by pack.toml's HOOKS= / IHOOKS=.
 */
#include "hooks.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* SH-LOAD-ORDER (a title-specific timing fix). The boot logos (thread fn_8119744c,
 * byte 0x8178f214 "logos playing", ~14 s) render under the game's render lock (a recursive lightweight mutex taken only while
 * byte 0x81d29230 is set). The first load runs on the main thread meanwhile (fn_81197636: while the logos play it starts no
 * loading screen); its end -- the "stop loading screen" tail at 0x811978fa, and fn_811975ca, the same on its own -- waits for
 * the loading screen to stop, switches the render lock off and frees the loading screen's textures. The game assumes that
 * end comes after the logos (on the console's storage it does): the logo thread then starts the loading screen, and the tail
 * stops it. On the host the load ends within the first second: the lock goes off under the logo thread (two renderers in one
 * GXM context: NOT_WITHIN_SCENE, the "Error with sceGxmDraw" abort, a driver crash) and the textures the loading screen is
 * about to use are freed (its "ui\ui_loading_screen.tga" abort after the logos). Both hooks hold that tail until the logos
 * are over, the order the game was written for. */
static void sh_wait_logos(Cpu *c, const char *where) {
	if (!RD8(0x8178f214u)) return;
	struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
	while (RD8(0x8178f214u)) usleep(10000);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fprintf(stderr, "sh_wait_logos: %s held %.1f s until the boot logos ended (lr %#x)\n", where,
	        (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9, c->r[14]);
}
void recomp_ihook_sh_wait_logos_load(Cpu *c, int reg) { (void)reg; sh_wait_logos(c, "end of load (fn_81197636)"); }
void recomp_hook_sh_wait_logos_stop(Cpu *c) { sh_wait_logos(c, "stop loading screen (fn_811975ca)"); }

/* SH-SKIP-PODS (title-specific timing fix, as SH-LOAD-ORDER). The loading-screen thread (fn_811969f2) reads a config value
 * ("global") with the archives bypassed: it sets byte 0x81c82be8 (0x81196a56), reads, and clears it (0x81196a72). The byte
 * is global, though, and the file lookup in the mounted archives (fn_812059d6, fn_81205874) skips both archives while it is
 * set -- for every thread. On the console the main thread isn't looking anything up in that moment; on the host the level
 * load overlaps the loading screen's start, and the main thread's lookup of a file that is in a mounted archive (pp2general's
 * art\fx\dfx_spark.tex) falls back to the loose file, fails, and the game stops with "'fx\dfx_spark.tga': Error loading
 * file". The flag counts here only for the thread that set it: the four reads of it in the lookups give any other thread 0. */
extern uint32_t kernel_current_tid(void);
static volatile uint32_t sh_skip_owner;   /* the thread that set the byte; 0: none */
void recomp_ihook_sh_skip_set(Cpu *c, int reg) { (void)c; (void)reg; sh_skip_owner = kernel_current_tid(); }
void recomp_ihook_sh_skip_clear(Cpu *c, int reg) { (void)c; (void)reg; sh_skip_owner = 0; }
void recomp_ihook_sh_skip_read(Cpu *c, int reg) {   /* after `ldrb rN, [rN]` of the byte */
	if (reg < 0 || !c->r[reg]) return;
	const uint32_t me = kernel_current_tid();
	if (me == sh_skip_owner) return;
	static int said; if (said++ < 8) fprintf(stderr, "sh_skip_pods: thread %#x's archive lookup ignores the loading screen's skip flag (set by %#x, lr %#x)\n", me, sh_skip_owner, c->r[14]);
	c->r[reg] = 0;
}
