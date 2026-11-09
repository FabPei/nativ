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
	fflush(stdout);
	char cmd[512]; snprintf(cmd, sizeof cmd, "RECOMP_FP_OUTFMT='81234570:1,*:5' '%s' env", argv[0]);
	check("(the override's checks, in a child started with RECOMP_FP_OUTFMT set)", system(cmd) == 0);
	printf("%d checks, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
