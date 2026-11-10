/* gaps_test.c -- GAPS-01: the runtime's gap counter (runtime/core/gaps.c, gaps.h) and one real site reporting into it.
 *
 * Contract: every place the runtime meets something it cannot do and runs on past calls gap(category, key, what);
 * each key is counted (first frame, number of notes); the end of a run prints one fixed line
 * "GAPS: tex_format N (keys), usse_op N, ..." with every category (0 when none), and gaps.json carries the same.
 * Before (local-integration 615d3fd): no counter -- each site printed its own line or nothing, so a gap was seen in the
 * picture (a magenta texture) before anyone read the log. Synthetic: made-up formats, names and USSE words, no game data.
 *
 *   I="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."
 *   cc -O1 -g -w -D_GNU_SOURCE $I gaps_test.c -o /tmp/gaps_test -lm -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/gaps_test
 * Fail-before for the site: -DUSSE_C='"<615d3fd's usse.c>"' (decodes I8MAD2 as a no-op without a word).
 * (the unresolved symbols are usse.c's guest-memory helpers, which decode_one never reaches)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../core/gaps.c"
#ifndef USSE_C
#define USSE_C "../gfx/usse.c"
#endif
#include USSE_C

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int w) { (void)a; (void)v; (void)w; }
static uint64_t fake_frame;
uint64_t display_frames(void) { return fake_frame; }

static int fails, n;
static void check(const char *what, int ok) { n++; printf("%-86s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

static void *spam(void *arg) { (void)arg; for (int i = 0; i < 100000; i++) gap(GAP_GPU_SOFT, 0x77, "test: contended key"); return 0; }

static char *slurp(const char *p) {
	FILE *f = fopen(p, "rb"); if (!f) return 0;
	static char buf[1 << 16]; size_t k = fread(buf, 1, sizeof buf - 1, f); buf[k] = 0; fclose(f); return buf;
}

int main(void) {
	char line[2048];
	gaps_line(line, sizeof line);
	printf("  %s\n", line);
	check("a run without gaps: the line names every category with 0",
	      !strcmp(line, "GAPS: tex_format 0, usse_op 0, shader_soft 0, spirv 0, gpu_soft 0, gxm_state 0, transfer 0, gpu_req 0, hle_stub 0, nid_unknown 0, audio_codec 0, missed_function 0, insn 0"));

	fake_frame = 12;
	gap(GAP_TEX_FORMAT, 0x95000000u, "texture format not decoded");
	gap(GAP_TEX_FORMAT, 0x95000000u, "texture format not decoded");
	fake_frame = 40;
	gap(GAP_TEX_FORMAT, 0x0c001000u, "texture format not decoded");
	gap(GAP_TEX_FORMAT, 0x95000000u, "texture format not decoded");
	gap_str(GAP_HLE_STUB, "sceTestMadeUpFunction");
	gap_str(GAP_HLE_STUB, "sceTestMadeUpFunction");
	check("an import named <lib>_<8 hex> is an unknown NID; a plain name and a short hex tail are not",
	      gap_unnamed_nid("SceTestLib_1A2B3C4D") && !gap_unnamed_nid("sceTestMadeUpFunction") && !gap_unnamed_nid("Lib_1A2B3C4") && !gap_unnamed_nid("SceTestLib_1a2b3c4d"));
	check("two texture formats: 2 distinct keys", gaps_distinct(GAP_TEX_FORMAT) == 2);
	{ int s = slot_of((uint64_t)(GAP_TEX_FORMAT + 1) << 32 | 0x95000000u); while (gaps[s].key != 0x95000000u) s = (s + 1) & (GAPS_SLOTS - 1);
	  check("a key: its number of notes (3) and the frame it was first seen (12)", gaps[s].count == 3 && gaps[s].first_frame == 12); }

	/* a real site: the USSE decoder meets I8MAD2 (op1 0x19), which it runs as a no-op */
	(void)decode_one((uint64_t)0x19 << 59, 0);
	check("site: usse.c decode_one counts I8MAD2 (op1 0x19) as a usse_op gap, key 0x1900", gaps_distinct(GAP_USSE_OP) == 1);

	pthread_t th[4];
	for (int i = 0; i < 4; i++) pthread_create(&th[i], 0, spam, 0);
	gap(GAP_GPU_SOFT, 0x77, "test: contended key");
	for (int i = 0; i < 4; i++) pthread_join(th[i], 0);
	{ int s = slot_of((uint64_t)(GAP_GPU_SOFT + 1) << 32 | 0x77); while (gaps[s].key != 0x77) s = (s + 1) & (GAPS_SLOTS - 1);
	  check("4 threads x 100000 notes + 1 of one key: exactly 400001, one key", gaps[s].count == 400001 && gaps_distinct(GAP_GPU_SOFT) == 1); }

	gaps_line(line, sizeof line);
	printf("  %s\n", line);
	check("the line: texture formats with their keys in first-seen order",
	      strstr(line, "GAPS: tex_format 2 (0x95000000,0xc001000)") == line);
	gap(GAP_MISSED_FN, 0x81044f3cu, "missed function in eboot.img (lr 0x81000101)");
	gaps_line(line, sizeof line);
	check("GAPS-02: a missed function is its own category, keyed by its address",
	      strstr(line, "missed_function 1 (0x81044f3c)") && gaps_distinct(GAP_MISSED_FN) == 1);
	check("the line: a named gap by its name, an empty category still 0",
	      strstr(line, "hle_stub 1 (sceTestMadeUpFunction)") && strstr(line, "insn 0") && strstr(line, "usse_op 1 (0x1900)"));

	const char *jp = "gaps_test_out.json";
	check("gaps.json written", gaps_write_json(jp) == 0);
	const char *j = slurp(jp);
	check("gaps.json: 6 keys in all, the texture format's count and first frame, the stub's name",
	      j && strstr(j, "\"total\": 6") && strstr(j, "{\"key\": \"0x95000000\", \"what\": \"texture format not decoded\", \"first_frame\": 12, \"count\": 3}")
	      && strstr(j, "\"what\": \"sceTestMadeUpFunction\"") && strstr(j, "\"insn\": {\"distinct\": 0, \"keys\": []}"));
	remove(jp);

	setenv("RECOMP_GAPS_JSON", jp, 1);
	gap(GAP_INSN, 0x81000123u, "unimplemented instruction (run stops)");
	j = slurp(jp);
	check("RECOMP_GAPS_JSON: a new key rewrites the file at once (a run killed at its time limit keeps it)", j && strstr(j, "\"0x81000123\""));
	remove(jp);
	printf("%d checks, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
