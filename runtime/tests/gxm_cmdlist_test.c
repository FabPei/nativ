/* gxm_cmdlist_test.c -- deferred contexts' command lists at the GXM boundary (runtime/gfx/gxm_sony.c).
 *
 * Contract (SceGxm deferred contexts): a draw made on a context between sceGxmBeginCommandList and sceGxmEndCommandList is
 * recorded into that command list; nothing is drawn then. sceGxmExecuteCommandList(immediate context, list) runs the
 * list's draws, in order, in the immediate context's current scene -- every time it is executed. A draw on a context that
 * is not recording is drawn at once. Re-recording a list replaces its draws.
 * Observed (AUTO-01 TW-3): Tearaway records part of each frame into lists recorded before the scene that executes them;
 * drawn at record time, they landed in the wrong scene, out of order -- its picture was blocky posterised rings.
 *
 * Synthetic: a context's program pointers in a guest arena (no game data); gfx_draw is this test's, counting the draws
 * by their index count.
 *   I="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."
 *   cc -O1 -g -w -D_GNU_SOURCE $I gxm_cmdlist_test.c -o /tmp/gxm_cmdlist_test -lm -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/gxm_cmdlist_test
 */
#include <sys/mman.h>
#include "../gfx/gxm_sony.c"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int w) { (void)a; (void)v; (void)w; }
/* the few host functions the draw observer reaches (the rest of the renderer is unresolved and never called) */
int gfxcov_on(void) { return 0; }   /* (the coverage record: off; the draw observer asks on every draw) */
int guest_mem_ranges(uint32_t lo[], uint32_t hi[], int max) { if (max < 1) return 0; lo[0] = 0x81000000u; hi[0] = 0x83000000u; return 1; }
void gfx_vis_write(uint32_t addr, uint32_t count) { (void)addr; (void)count; }
void gfx_texture_level(const GfxTexture *t, int level, GfxTexture *out) { (void)level; *out = *t; }
uint64_t display_frames(void) { return 0; }
void gfxcov_gap(const char *key, const char *value, int partial, const char *note) { (void)key; (void)value; (void)partial; (void)note; }
static uint32_t drawn[64]; static int ndrawn;
void gfx_draw(const GfxDraw *d) { if (ndrawn < 64) drawn[ndrawn] = d->count; ndrawn++; }

static int fails, n;
static void check(const char *what, int ok) { n++; printf("%-78s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

static Cpu c;
static void call(void (*fn)(Cpu *), uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t stk0) {
	c.r[0] = a0; c.r[1] = a1; c.r[2] = a2; c.r[3] = a3; WR32(c.r[13], stk0); fn(&c);
}
static void context(uint32_t cx, uint32_t vp, uint32_t fp) { WR32(cx + CX_VPROG, vp); WR32(cx + CX_FPROG, fp); }

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { puts("no arena"); return 2; }
	c.r[13] = 0x82000000u;
	const uint32_t VP = 0x81000000u, FP = 0x81000100u, IMM = 0x81010000u, DEF = 0x81020000u, L1 = 0x81030000u, L2 = 0x81030040u;
	WR32(VP, 0x81000800u); WR32(FP, 0x81000900u);   /* each program's GXP pointer (nothing else: no attributes, streams, textures) */
	context(IMM, VP, FP); context(DEF, VP, FP);

	call(o_draw, IMM, 3, 0, 0x81040000u, 6);                          /* immediate: drawn at once */
	check("a draw on the immediate context is drawn at once", ndrawn == 1 && drawn[0] == 6);
	call(o_begin_command_list, DEF, 0, 0, 0, 0);
	call(o_draw, DEF, 3, 0, 0x81040000u, 9);
	call(o_draw, DEF, 3, 0, 0x81040000u, 12);
	check("draws on a recording deferred context are not drawn at record time", ndrawn == 1);
	call(o_end_command_list, DEF, L1, 0, 0, 0);
	call(o_draw, IMM, 3, 0, 0x81040000u, 3);
	check("the immediate context keeps drawing at once meanwhile", ndrawn == 2 && drawn[1] == 3);
	call(o_execute_command_list, IMM, L1, 0, 0, 0);
	check("ExecuteCommandList runs the list's draws, in order", ndrawn == 4 && drawn[2] == 9 && drawn[3] == 12);
	call(o_execute_command_list, IMM, L1, 0, 0, 0);
	check("executing it again runs them again", ndrawn == 6 && drawn[4] == 9 && drawn[5] == 12);
	call(o_draw, DEF, 3, 0, 0x81040000u, 15);
	check("after EndCommandList the deferred context's draw is not swallowed by a list", ndrawn == 7 && drawn[6] == 15);
	call(o_begin_command_list, DEF, 0, 0, 0, 0);
	call(o_draw, DEF, 3, 0, 0x81040000u, 21);
	call(o_end_command_list, DEF, L1, 0, 0, 0);                        /* re-recorded under the same list */
	call(o_begin_command_list, DEF, 0, 0, 0, 0);
	call(o_end_command_list, DEF, L2, 0, 0, 0);                        /* an empty list */
	int before = ndrawn;
	call(o_execute_command_list, IMM, L1, 0, 0, 0);
	check("a re-recorded list holds only its new draws", ndrawn == before + 1 && drawn[before] == 21);
	call(o_execute_command_list, IMM, L2, 0, 0, 0);
	call(o_execute_command_list, IMM, 0x81039999u, 0, 0, 0);
	check("an empty list and an unknown one draw nothing", ndrawn == before + 1);
	printf("%d checks, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
