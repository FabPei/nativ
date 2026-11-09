/* usse_outfmt_test.c -- a fragment program variant's output register format, by GXP (runtime/gfx/usse.c, usse.h).
 *
 * Contract: sceGxmShaderPatcherCreateFragmentProgram(patcher, id, outputFormat, ...) fixes the register format a
 * NATIVECOLOR program's colour reaches the surface in (SceGxmOutputRegisterFormat: 0 DECLARED, 1 UCHAR4, 2 CHAR4,
 * 3 USHORT2, 4 SHORT2, 5 HALF4, 6 HALF2, 7 FLOAT2, 8 FLOAT). The patcher hook records it per GXP
 * (usse_set_fp_output_format); the renderers look it up per draw (usse_fp_output_format: 0 when never recorded, the last
 * format recorded otherwise). One raw 32-bit register: UCHAR4, CHAR4, USHORT2, SHORT2, HALF2, FLOAT
 * (usse_output_format_raw32) -- O0 goes to a 32-bit surface as it is. RECOMP_FP_OUTFMT="<gxp>:<fmt>,...,*:<fmt>"
 * overrides the table (a replay has no patcher calls).
 * Observed (AUTO-01 TW-10): Tearaway's scene programs declare F16 x4 output yet pack U8 into O0 (created UCHAR4);
 * read as F16 lanes its frame was saturated green, then blocky rings after its post pass.
 *
 * Synthetic: made-up GXP addresses, no game data.
 *   I="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."
 *   cc -O1 -g -w -D_GNU_SOURCE $I usse_outfmt_test.c -o /tmp/usse_outfmt_test -lm -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/usse_outfmt_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gfx/usse.c"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int w) { (void)a; (void)v; (void)w; }

static int fails, n;
static void check(const char *what, int ok) { n++; printf("%-86s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(int argc, char **argv) {
	if (argc > 1) {   /* the override (read once per process: checked in a child started with it set) */
		usse_set_fp_output_format(0x81234570u, USSE_OUT_HALF4); usse_set_fp_output_format(0x81234580u, USSE_OUT_FLOAT);
		check("RECOMP_FP_OUTFMT: a GXP it names takes its format over the table's", usse_fp_output_format(0x81234570u) == USSE_OUT_UCHAR4);
		check("RECOMP_FP_OUTFMT: '*' gives the rest its format, recorded or not", usse_fp_output_format(0x81234580u) == USSE_OUT_HALF4 && usse_fp_output_format(0x81239990u) == USSE_OUT_HALF4);
		fflush(stdout); return fails ? 1 : 0;
	}
	check("a GXP never recorded: DECLARED (0)", usse_fp_output_format(0x81234560u) == USSE_OUT_DECLARED);
	usse_set_fp_output_format(0x81234560u, USSE_OUT_UCHAR4);
	usse_set_fp_output_format(0x81234570u, USSE_OUT_HALF4);
	check("recorded formats come back per GXP (UCHAR4, HALF4)", usse_fp_output_format(0x81234560u) == USSE_OUT_UCHAR4 && usse_fp_output_format(0x81234570u) == USSE_OUT_HALF4);
	usse_set_fp_output_format(0x81234560u, USSE_OUT_FLOAT);
	check("a second variant of the same GXP: the last format recorded", usse_fp_output_format(0x81234560u) == USSE_OUT_FLOAT);
	/* GXPs that collide in the table's hash (4096 slots, addresses 4096 * 4 apart hash alike modulo the multiplier) */
	int all = 1;
	for (uint32_t i = 0; i < 300; i++) usse_set_fp_output_format(0x82000000u + 0x4000u * i, 1 + (int)(i % 8));
	for (uint32_t i = 0; i < 300; i++) all &= usse_fp_output_format(0x82000000u + 0x4000u * i) == 1 + (int)(i % 8);
	check("300 GXPs, colliding slots included: each keeps its own format", all);
	usse_set_fp_output_format(0, USSE_OUT_UCHAR4);
	check("GXP 0 is not recorded", usse_fp_output_format(0) == USSE_OUT_DECLARED);
	check("raw 32-bit register formats: UCHAR4 CHAR4 USHORT2 SHORT2 HALF2 FLOAT",
	      usse_output_format_raw32(1) && usse_output_format_raw32(2) && usse_output_format_raw32(3) && usse_output_format_raw32(4) && usse_output_format_raw32(6) && usse_output_format_raw32(8));
	check("not raw 32-bit: DECLARED HALF4 FLOAT2", !usse_output_format_raw32(0) && !usse_output_format_raw32(5) && !usse_output_format_raw32(7));
	/* usse_fragment_colour_packed8: synthetic micro-op programs (NATIVECOLOR, colour at O0) */
	{
		static UOp code[4]; UsseProgram p;
		#define OPW(i, o, b, n, t, m, sb, sn, st) do { memset(&code[i], 0, sizeof code[i]); code[i].op = o; code[i].dst.bank = b; code[i].dst.num = n; code[i].dst.type = t; code[i].mask = m; code[i].reps = 1; \
			code[i].s1.bank = sb; code[i].s1.num = sn; code[i].s1.type = st; for (int q = 0; q < 4; q++) code[i].s1.swiz[q] = (uint8_t)q; } while (0)
		#define PROG(n) do { memset(&p, 0, sizeof p); p.is_fragment = 1; p.fl.native_color = 1; p.fl.color_pa_start = 0; p.prim = code; p.nprim = n; } while (0)
		/* Tearaway's scene programs: VPCK U8 <- F16 into PA0, then F16 x4 moved PA0-1 -> O0-1 */
		OPW(0, OP_VPCK, BK_PA, 0, TY_U8, 0xf, BK_PA, 2, TY_F16); OPW(1, OP_VMOV, BK_O, 0, TY_F16, 0xf, BK_PA, 0, TY_F16); PROG(2);
		check("an 8-bit pack moved into O0: packed (written raw)", usse_fragment_colour_packed8(&p) == 1);
		/* the same with lane 2 rewritten (F16 mask 4 lands in the second word): still packed */
		OPW(1, OP_VNMAD, BK_PA, 0, TY_F16, 0x4, BK_PA, 8, TY_F16); OPW(2, OP_VMOV, BK_O, 0, TY_F16, 0xf, BK_PA, 0, TY_F16); PROG(3);
		check("a later F16 write to the second word only: the colour word stays packed", usse_fragment_colour_packed8(&p) == 1);
		/* lane 0 rewritten after the pack: not packed */
		OPW(1, OP_VNMAD, BK_PA, 0, TY_F16, 0x1, BK_PA, 8, TY_F16); PROG(3);
		check("an F16 write over the packed word before the move: not packed", usse_fragment_colour_packed8(&p) == 0);
		/* Tearaway's post pass: VPCK F16 <- U8 straight into O0 */
		OPW(0, OP_VPCK, BK_O, 0, TY_F16, 0xf, BK_TEMP, 0, TY_U8); PROG(1);
		check("a pack into F16 lanes (the post pass): not packed (declared type)", usse_fragment_colour_packed8(&p) == 0);
		/* not NATIVECOLOR: never packed */
		OPW(0, OP_VPCK, BK_O, 0, TY_U8, 0xf, BK_PA, 2, TY_F16); PROG(1); p.fl.native_color = 0;
		check("a program without NATIVECOLOR: not packed", usse_fragment_colour_packed8(&p) == 0);
	}
	fflush(stdout);
	char cmd[512]; snprintf(cmd, sizeof cmd, "RECOMP_FP_OUTFMT='81234570:1,*:5' '%s' env", argv[0]);
	check("(the override's checks, in a child started with RECOMP_FP_OUTFMT set)", system(cmd) == 0);
	printf("%d checks, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
