/* gfx_formats_test.c -- texture and colour-target formats of the software sampler (runtime/gfx/gfx_soft.c), through which
 * the GPU path also decodes guest textures (gfx_decode_texture_f32) and round-trips render targets (gfx_encode/decode_
 * target_rgba8). Synthetic pixels in a small guest arena, no game data.
 *
 * Contracts (SceGxm formats):
 *  - texture base 0x0e U2U10U10U10, SWIZZLE4 ABGR: R in bits 0-9, G 10-19, B 20-29, A 30-31 (A /3);
 *  - texture base 0x13 F32M: one 32-bit float component (placed by the SWIZZLE1 modes, as F32);
 *  - colour format 0x60800000 U2U10U10U10: the same fields, in the 8-bit formats' component orders; what a render target
 *    stores in this format reads back as a 0x0e texture with the same values.
 * Observed (AUTO-01 GR-13): Gravity Rush renders its scene into a 0x60800000 target and composites it as a 0x0e texture;
 * neither was known, every texel came out as the fallback magenta (1, 0, 1, 1), and so did the whole frame.
 *
 *   I="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."
 *   cc -O1 -g -w -D_GNU_SOURCE $I gfx_formats_test.c -o /tmp/gfx_formats_test -lm -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/gfx_formats_test
 * (the unresolved symbols are the renderer's shader/window entry points, which these checks never reach)
 */
#include <sys/mman.h>
#include "../gfx/gfx_soft.c"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int w) { (void)a; (void)v; (void)w; }

static int fails, n;
static void check(const char *what, int ok) { n++; printf("%-80s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static int near(float a, float b) { return fabsf(a - b) < 0.002f; }

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { puts("no arena"); return 2; }
	const uint32_t T = 0x81000000u;
	/* U2U10U10U10 texture, 2x1, linear, stride 8: (R 1023, G 512, B 0, A 3) and (R 0, G 0, B 1023, A 1) */
	WR32(T, 1023u | 512u << 10 | 0u << 20 | 3u << 30);
	WR32(T + 4, 0u | 0u << 10 | 1023u << 20 | 1u << 30);
	GfxTexture t; memset(&t, 0, sizeof t);
	t.data = T; t.width = 2; t.height = 1; t.format = 0x0e000000u | 8;   /* base 0x0e, swizzle 0 (ABGR), stride 8 */
	float px[8];
	gfx_decode_texture_f32(&t, px);
	check("U2U10U10U10 texel 0: R 1, G 512/1023, B 0, A 1", near(px[0], 1) && near(px[1], 512 / 1023.f) && near(px[2], 0) && near(px[3], 1));
	check("U2U10U10U10 texel 1: R 0, G 0, B 1, A 1/3", near(px[4], 0) && near(px[5], 0) && near(px[6], 1) && near(px[7], 1 / 3.f));
	check("U2U10U10U10 is not the magenta fallback", !(px[0] == 1 && px[1] == 0 && px[2] == 1));
	t.format = 0x0e000000u | 0x1000u | 8;   /* swizzle 1 (ARGB): R and B swap places */
	gfx_decode_texture_f32(&t, px);
	check("U2U10U10U10 ARGB swizzle swaps R and B", near(px[0], 0) && near(px[2], 1));

	/* F32M texture, 1x1: 0.625 in R (SWIZZLE1 _R: 0, 0, 1 elsewhere) */
	float f = 0.625f; uint32_t fb; memcpy(&fb, &f, 4); WR32(T + 0x100, fb);
	t.data = T + 0x100; t.width = 1; t.height = 1; t.format = 0x13000000u | 8;
	gfx_decode_texture_f32(&t, px);
	check("F32M texel: R 0.625 (as F32), G 0, B 0, A 1", near(px[0], 0.625f) && near(px[1], 0) && near(px[2], 0) && near(px[3], 1));
	check("F32M is 4 bytes a texel (the GPU upload's size)", gfx_texture_bpp(&t) == 4);

	/* a U2U10U10U10 colour target: what it stores, the 0x0e texture reads */
	GfxTarget g; memset(&g, 0, sizeof g);
	g.data = T + 0x200; g.width = 2; g.height = 1; g.stride = 2; g.format = 0x60800000u;
	uint8_t in[8] = {255, 128, 0, 255, 0, 0, 255, 0}, out[8];
	gfx_encode_target_rgba8(&g, in);
	uint32_t w0 = RD32(T + 0x200), w1 = RD32(T + 0x204);
	check("target 0x60800000 stores pixel 0 as U2U10U10U10 (R 1023, G 514, B 0, A 3)", w0 == (1023u | 514u << 10 | 3u << 30));
	check("target 0x60800000 stores pixel 1 (R 0, G 0, B 1023, A 0)", w1 == (1023u << 20));
	gfx_decode_target_rgba8(&g, out);
	check("target round trip: the RGBA8 the GPU path keeps comes back", !memcmp(in, out, 8));
	t.data = T + 0x200; t.width = 2; t.height = 1; t.format = 0x0e000000u | 8;
	gfx_decode_texture_f32(&t, px);
	check("the target's memory read as a 0x0e texture: the same colours",
	      near(px[0], 1) && near(px[1], 514 / 1023.f) && near(px[2], 0) && near(px[3], 1) && near(px[6], 1) && near(px[7], 0));
	printf("%d checks, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
