/*
 * Synthetic fixture: a fragment program's non-dependent texture read with result format 0 (no conversion) on the GPU.
 *
 * Contract (the interpreter's, gfx_soft.c shade_interp -> usse_texfn_raw -> usse_pack_iter_raw): an iterator texture
 * read whose result format is 0 hands the program the texel's native packed word -- a U8U8U8U8 or block-decoded texel
 * as ONE 32-bit word, R in the low byte -- not four F32 words. A program that then moves PA0 bit for bit (a VBW copy
 * to its U8 colour output) shows that word's bytes as its colour. usse_spirv.c's iterator prologue wrote four F32
 * words for every format but F16, so such a program drew the float bits of red as its colour (a whole menu
 * background turned to noise). Cube maps keep the float path (usse_texfn_raw returns 0 for them).
 *
 * The program is one NOP (the prologue is what's under test); the fragment layout is built here: one texture
 * iterator, unit 0, into PA0. The translator emits SPIR-V, which this host can't execute, so the check is on what
 * the module does: result format 0 must pack the sample -- four float->uint conversions, three shifts and three ORs
 * more than the F32 module -- and a cube unit must translate exactly as F32 does. With SPIRV_VAL every module is
 * validated. fail-before: the result-format-0 module equals the F32 one (0 extra conversions); and (sRGB) the raw module
 * decoding a gamma texture before the pack (6 pow, fa401fc).
 *
 *     cc -O1 -g -Wall -D_GNU_SOURCE -I ../gfx -I ../core -I ../../recompiler -I ../.. usse_iterraw_test.c ../gfx/usse.c ../gfx/usse_spirv.c -o /tmp/usse_iterraw_test -lm && /tmp/usse_iterraw_test
 *     (SPIRV_VAL=<spirv-val> also validates every module; SPIRV_TMP=<dir> for the module files, default /tmp)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "usse.h"
#include "usse_internal.h"
#include "usse_spirv.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;   /* rt.h's write-watch: disabled */
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

static int checks, failed;
static void ck(int cond, const char *what) { checks++; printf("%-84s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) failed++; }

/* synthetic GXP header (usse.c's H_* layout, as usse_cf_test.c) */
#define GXP_ADDR 0x10000000u
#define H_FLAGS 20
#define H_PRIM_REGS 48
#define H_PRIM_INSTR_COUNT 60
#define H_PRIM_OFF 64
#define CODE_OFF 128
#define NOP 0xF800014000000000ull
static void put32(uint32_t a, uint32_t v) { memcpy(recomp_mem + a, &v, 4); }
static void put64(uint32_t a, uint64_t v) { memcpy(recomp_mem + a, &v, 8); }

static UsseProgram *load_nop_fragment(void) {
	static uint32_t seq;
	uint32_t g = GXP_ADDR + (++seq) * 0x10000;   /* a fresh address per program (the decoder caches by address) */
	memset(recomp_mem + g, 0, CODE_OFF + 64);
	put32(g + 8, CODE_OFF + 8);
	put32(g + H_FLAGS, 1);
	put32(g + H_PRIM_REGS, 16);
	put32(g + H_PRIM_INSTR_COUNT, 1);
	put32(g + H_PRIM_OFF, CODE_OFF - H_PRIM_OFF);
	put64(g + CODE_OFF, NOP);
	return usse_decode_uncached(g);
}
static int count_op(const SpirvModule *m, uint32_t op) {
	int c = 0;
	for (int i = 5; i < m->nwords; ) { uint32_t wc = m->words[i] >> 16; if ((m->words[i] & 0xFFFF) == op) c++; if (!wc) break; i += (int)wc; }
	return c;
}
/* how many GLSL.std.450 Pow (26) the module computes: srgb_fixup's two curves (decode, encode) take one each per colour
 * channel -- the raw-word module must carry only the encode (a gamma texture is NOT decoded on that path) */
static int count_pow(const SpirvModule *m) {
	int c = 0;
	for (int i = 5; i < m->nwords; ) { uint32_t wc = m->words[i] >> 16; if ((m->words[i] & 0xFFFF) == 12 && wc >= 5 && m->words[i + 4] == 26) c++; if (!wc) break; i += (int)wc; }
	return c;
}
static int validate(const SpirvModule *m, const char *name) {
	const char *val = getenv("SPIRV_VAL"); if (!val || !*val) return 1;
	const char *dir = getenv("SPIRV_TMP") ? getenv("SPIRV_TMP") : "/tmp";
	char path[256]; snprintf(path, sizeof path, "%s/usse_iterraw_%s.spv", dir, name);
	for (char *q = path + strlen(dir) + 1; *q; q++) if (!((*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') || *q == '.' || *q == '_')) *q = '_';
	FILE *f = fopen(path, "wb"); if (!f) return 0;
	fwrite(m->words, 4, (size_t)m->nwords, f); fclose(f);
	char cmd[600]; snprintf(cmd, sizeof cmd, "%s %s", val, path);
	int rc = system(cmd);
	remove(path);
	return rc == 0;
}
typedef struct { int ok, ftou, shl, bor, pw, words; } Shape;
static Shape translate(const char *name, int result_fmt, uint32_t cube_mask) {
	UsseProgram *p = load_nop_fragment();
	UsseFragLayout fl; memset(&fl, 0, sizeof fl);
	fl.niters = 1; fl.color_type = 0; fl.color_comps = 4;
	UsseIter *it = &fl.iters[0];
	it->is_tex = 1; it->tex_coord_src = 0; it->tex_unit = 0; it->tex_proj = 0; it->result_fmt = result_fmt; it->pa = 0;
	usse_spirv_set_canonical_varyings(1);
	uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
	SpirvModule m = usse_spirv_translate(p, 0, &fl, sa, 16, units, &nsmp, 0, cube_mask, 0, 0);
	Shape r = { m.ok, 0, 0, 0, 0, m.nwords };
	char what[160];
	snprintf(what, sizeof what, "[%s] translates (%d sampler slot%s)", name, nsmp, nsmp == 1 ? "" : "s");
	ck(m.ok && nsmp == 1, what);
	if (!m.ok) printf("     err: %s\n", m.err);
	else {
		r.pw = count_pow(&m); r.ftou = count_op(&m, 109); r.shl = count_op(&m, 196); r.bor = count_op(&m, 197);
		snprintf(what, sizeof what, "[%s] spirv-val accepts the module%s", name, getenv("SPIRV_VAL") ? "" : " (SPIRV_VAL unset: skipped)");
		ck(validate(&m, name), what);
	}
	usse_spirv_free(&m);
	usse_program_free(p);
	return r;
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }

	Shape f32 = translate("result format 1 (F32)", 1, 0);
	Shape raw = translate("result format 0 (native word)", 0, 0);
	Shape cube = translate("result format 0, cube unit", 0, 1);
	Shape cubef = translate("result format 1, cube unit", 1, 1);
	char what[200];
	snprintf(what, sizeof what, "format 0 packs one unorm8x4 word: +%d FToU (4), +%d shifts (3), +%d ORs (3)",
	         raw.ftou - f32.ftou, raw.shl - f32.shl, raw.bor - f32.bor);
	ck(raw.ok && f32.ok && raw.ftou - f32.ftou == 4 && raw.shl - f32.shl == 3 && raw.bor - f32.bor == 3, what);
	/* sRGB: the interpreter's raw texel (gfx_soft.c usse_texfn_raw) is the stored bytes -- no tex_gamma -- so a gamma
	 * texture over plain data must not be decoded before the packing (srgb_fixup's decode half); a non-gamma texture
	 * over an sRGB target is still re-encoded (its encode half: the GPU decoded the target's stored bytes) */
	snprintf(what, sizeof what, "format 0: sRGB encode only, no decode (%d pow; F32 %d = both curves)", raw.pw, f32.pw);
	ck(raw.ok && f32.ok && f32.pw == 6 && raw.pw == 3, what);
	snprintf(what, sizeof what, "a cube unit keeps the float path (%d words, F32 cube %d)", cube.words, cubef.words);
	ck(cube.ok && cubef.ok && cube.words == cubef.words && cube.ftou == cubef.ftou && cube.shl == cubef.shl && cube.pw == cubef.pw, what);

	printf("%d checks, %d failed\n", checks, failed);
	if (failed) { printf("FAILED\n"); return 1; }
	printf("all passed\n");
	return 0;
}
