/*
 * Synthetic USSE control-flow fixture (NIGHT-01 USSE-CF): the GPU translator's structured forms.
 *
 * usse_spirv.c turned only predicated forward branches ("(p) BR +k": skip the next k-1 instructions) into structured
 * ifs; any unconditional or backward branch made the whole draw fall back to the CPU interpreter. Compiled shaders use
 * two more forms: an if/else (a skip range whose last instruction is an unconditional forward branch over the else part)
 * and while loops (an unconditional backward branch to the loop's header; predicated forward branches to just past it
 * are breaks, to it continues). Seen in 17 vertex programs of one title and 2 of another (every draw on the CPU).
 *
 * The programs here are hand-encoded branch and NOP words (control group, docs/usse-isa.md: op1 = 0x1F, predicate in
 * bits 58-56, function 40-38 = 1 for BR / 5 for NOP, a signed 20-bit offset for BR), decoded by the real decoder and
 * handed to usse_spirv_translate. fail-before: "outside the structured forms translated"; pass-after: the module is
 * made, with the expected number of OpLoopMerge / OpSelectionMerge instructions, and -- when SPIRV_VAL names a
 * spirv-val binary -- the validator accepts it. Forms still outside (two back edges to one header, a loop that leaves
 * its enclosing range) must fail cleanly, never emit a broken module.
 *
 *     cc -O1 -g -Wall -D_GNU_SOURCE -I ../gfx -I ../core -I ../../recompiler -I ../.. usse_cf_test.c ../gfx/usse.c ../gfx/usse_spirv.c -o /tmp/usse_cf_test -lm && /tmp/usse_cf_test
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
static void ck(int cond, const char *what) { checks++; printf("%-72s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) failed++; }

/* synthetic GXP header (usse.c's H_* layout, as usse_sop2_test.c) */
#define GXP_ADDR 0x10000000u
#define H_FLAGS 20
#define H_PRIM_REGS 48
#define H_PRIM_INSTR_COUNT 60
#define H_PRIM_OFF 64
#define CODE_OFF 128
static void put32(uint32_t a, uint32_t v) { memcpy(recomp_mem + a, &v, 4); }
static void put64(uint32_t a, uint64_t v) { memcpy(recomp_mem + a, &v, 8); }

enum { P_NONE = 0, P0 = 1, P1 = 2, P2 = 3, NP0 = 5, NP1 = 6 };   /* the extended predicate field */
#define NOP 0xF800014000000000ull
static uint64_t br(int pred, int off) { return 0xF800004000000000ull | (uint64_t)pred << 56 | ((uint64_t)(uint32_t)off & 0xFFFFF); }

/* USSE-CALL words: a return (control group, function 40-38 = 2), a call (BR with save_link, bit 41, and br_type 1),
 * the two SPEC no-ops used as markers (DFENCE: op2 1 opcat 2; EMIT: the vertex program's last word) */
#define RET 0xF800008000000000ull
#define FENCE 0xF920000000000000ull
#define EMIT 0xFB275000A0200000ull
static uint64_t call(int off) { return 0xF800024000000000ull | ((uint64_t)(uint32_t)off & 0xFFFFF); }
enum { NOP_K, FENCE_K, EMIT_K, RET_K, BR_K, OTHER_K };
static UsseProgram *load(const uint64_t *w, int n, int fragment) {
	static uint32_t seq;
	uint32_t g = GXP_ADDR + (++seq) * 0x10000;   /* a fresh address per program (the decoder caches by address) */
	memset(recomp_mem + g, 0, CODE_OFF + 8 * (size_t)n + 64);
	put32(g + 8, CODE_OFF + 8 * (uint32_t)n);
	put32(g + H_FLAGS, fragment ? 1 : 0);
	put32(g + H_PRIM_REGS, 16);
	put32(g + H_PRIM_INSTR_COUNT, (uint32_t)n);
	put32(g + H_PRIM_OFF, CODE_OFF - H_PRIM_OFF);
	for (int i = 0; i < n; i++) put64(g + CODE_OFF + 8 * (uint32_t)i, w[i]);
	return usse_decode_uncached(g);
}
/* how many instructions of opcode `op` the module holds (the stream after the 5-word header) */
static int count_op(const SpirvModule *m, uint32_t op) {
	int c = 0;
	for (int i = 5; i < m->nwords; ) { uint32_t wc = m->words[i] >> 16; if ((m->words[i] & 0xFFFF) == op) c++; if (!wc) break; i += (int)wc; }
	return c;
}
static int kind_of(const UOp *o) {
	if (o->op == OP_BR) return BR_K;
	if (o->op != OP_NOP) return OTHER_K;
	return o->sub == USSE_NOP_FENCE ? FENCE_K : o->sub == USSE_NOP_EMIT ? EMIT_K : o->sub == USSE_NOP_RET ? RET_K : NOP_K;
}
static void code_is(const char *name, const uint64_t *w, int n, const int *want, int nwant) {
	UsseProgram *p = load(w, n, 0); int nc = 0; const UOp *c = p ? usse_program_code(p, 0, &nc) : 0;
	int same = c && nc == nwant; for (int i = 0; same && i < nc; i++) same = kind_of(&c[i]) == want[i];
	char what[160]; snprintf(what, sizeof what, "[%s] decoded order (%d instructions, want %d)", name, nc, nwant); ck(same, what);
	if (p) usse_program_free(p);
}
static int validate(const SpirvModule *m, const char *name) {
	const char *val = getenv("SPIRV_VAL"); if (!val || !*val) return 1;
	const char *dir = getenv("SPIRV_TMP") ? getenv("SPIRV_TMP") : "/tmp";   /* (a Windows spirv-val from WSL: SPIRV_TMP=. in a /mnt/c folder) */
	char path[256]; snprintf(path, sizeof path, "%s/usse_cf_%s.spv", dir, name);
	for (char *q = path + strlen(dir); *q; q++) if (*q == ' ' || *q == '/' || *q == '(' || *q == ')' || *q == '+') *q = '_';
	FILE *f = fopen(path, "wb"); if (!f) return 0;
	fwrite(m->words, 4, (size_t)m->nwords, f); fclose(f);
	char cmd[600]; snprintf(cmd, sizeof cmd, "%s %s", val, path);
	int rc = system(cmd);
	remove(path);
	return rc == 0;
}
static void case_ok(const char *name, const uint64_t *w, int n, int fragment, int loops, int min_sel) {
	UsseProgram *p = load(w, n, fragment);
	char what[160];
	snprintf(what, sizeof what, "[%s] decodes (%d instructions)", name, n); ck(p != 0, what);
	if (!p) return;
	usse_spirv_set_canonical_varyings(1);
	uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
	SpirvModule m = usse_spirv_translate(p, 0, fragment ? usse_fragment_layout(p) : 0, sa, 16, units, &nsmp, 0, 0, 0, 0);
	snprintf(what, sizeof what, "[%s] translates", name); ck(m.ok, what);
	if (!m.ok) printf("     err: %s\n", m.err);
	else {
		int nl = count_op(&m, 246), ns = count_op(&m, 247);
		snprintf(what, sizeof what, "[%s] %d OpLoopMerge (want %d), %d OpSelectionMerge (want >= %d)", name, nl, loops, ns, min_sel);
		ck(nl == loops && ns >= min_sel, what);
		snprintf(what, sizeof what, "[%s] spirv-val accepts the module%s", name, getenv("SPIRV_VAL") ? "" : " (SPIRV_VAL unset: skipped)");
		ck(validate(&m, name), what);
	}
	usse_spirv_free(&m);
	usse_program_free(p);
}
static void case_refused(const char *name, const uint64_t *w, int n) {
	UsseProgram *p = load(w, n, 1);
	usse_spirv_set_canonical_varyings(1);
	uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
	SpirvModule m = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, &nsmp, 0, 0, 0, 0);
	char what[160]; snprintf(what, sizeof what, "[%s] refused cleanly (%s)", name, m.ok ? "translated!" : m.err);
	ck(!m.ok && m.err[0], what);
	usse_spirv_free(&m);
	usse_program_free(p);
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }

	/* the old form still: (p0) BR +3 skips 1-2 */
	const uint64_t skip[] = { br(P0, 3), NOP, NOP, NOP };
	case_ok("skip", skip, 4, 1, 0, 1);
	/* if/else: (p0) BR +3 -> 3 (then = 1..2, its last instruction BR +3 -> 5 jumps over the else part 3..4) */
	const uint64_t ifelse[] = { br(P0, 3), NOP, br(P_NONE, 3), NOP, NOP, NOP };
	case_ok("if/else", ifelse, 6, 1, 0, 1);
	/* a while loop: header 0, (p0) BR +4 -> 5 = past the back edge (break), back edge 4: BR -4 -> 0 */
	const uint64_t loop[] = { NOP, br(P0, 4), NOP, NOP, br(P_NONE, -4), NOP };
	case_ok("loop + break", loop, 6, 1, 1, 1);
	/* continue: (!p1) BR +3 -> 5 = the back edge */
	const uint64_t cont[] = { NOP, br(P0, 5), br(NP1, 3), NOP, NOP, br(P_NONE, -5), NOP };
	case_ok("loop + break + continue", cont, 7, 1, 1, 2);
	/* the shape the failing programs have: an if/else whose else part holds two loops in a row, a skip inside one
	 * 0 (p0) BR +3 -> 3 | 1 NOP | 2 BR +12 -> 14 | 3 NOP (loop A) | 4 (!p1) BR +4 -> 8 | 5 (p2) BR +2 -> 7 | 6 NOP |
	 * 7 BR -4 -> 3 | 8 NOP (loop B) | 9 (p0) BR +4 -> 13 | 10 NOP | 11 NOP | 12 BR -4 -> 8 | 13 NOP | 14 NOP */
	const uint64_t shape[] = { br(P0, 3), NOP, br(P_NONE, 12), NOP, br(NP1, 4), br(P2, 2), NOP, br(P_NONE, -4),
	                           NOP, br(P0, 4), NOP, NOP, br(P_NONE, -4), NOP, NOP };
	case_ok("if/else holding two loops (vertex)", shape, 15, 0, 2, 4);
	case_ok("if/else holding two loops (fragment)", shape, 15, 1, 2, 4);
	/* nested loops: outer 0..6, inner 2..4 */
	const uint64_t nested[] = { NOP, br(P0, 6), NOP, br(P1, 2), br(P_NONE, -2), NOP, br(P_NONE, -6), NOP };
	case_ok("nested loops", nested, 8, 1, 2, 2);

	/* still outside: two back edges to one header; a loop whose back edge leaves the skip range it starts in */
	const uint64_t twoback[] = { NOP, br(P0, 4), br(P_NONE, -2), NOP, br(P_NONE, -4), NOP };
	case_refused("two back edges to one header", twoback, 6);
	const uint64_t crossing[] = { br(P0, 3), NOP, NOP, br(P_NONE, -2), NOP };
	case_refused("loop leaving its skip range", crossing, 5);

	/* USSE-CALL: a subroutine 0..r ending in a return, called by branches to 0 (save_link) after it. Taken literally a
	 * call is a backward branch into the body again: refused ("outside the structured forms"), and the interpreter
	 * looped to its guard. Inlined at decode: main from r + 1, the subroutine's copy at each call, no branch left. */
	{	const uint64_t calls[] = { FENCE, FENCE, RET, NOP, call(-4), NOP, call(-6), EMIT };
		const int want[] = { NOP_K, FENCE_K, FENCE_K, NOP_K, FENCE_K, FENCE_K, EMIT_K };
		code_is("calls inlined in order", calls, 8, want, 7);
		case_ok("two calls (vertex)", calls, 8, 0, 0, 0);
		case_ok("two calls (fragment)", calls, 8, 1, 0, 0);
		/* a skip in main over a call: re-aimed at the call's place after the copy: (p0) BR +3 (3 -> 6) becomes BR +4 */
		const uint64_t skip_call[] = { FENCE, FENCE, RET, br(P0, 3), call(-4), NOP, EMIT };
		const int want2[] = { BR_K, FENCE_K, FENCE_K, NOP_K, EMIT_K };
		code_is("skip over a call re-aimed", skip_call, 7, want2, 5);
		UsseProgram *q = load(skip_call, 7, 1); int nq = 0; const UOp *cq = usse_program_code(q, 0, &nq);
		ck(q && nq == 5 && cq[0].imm == 4, "[skip over a call] the skip's offset is 4 (past the inlined copy)");
		usse_program_free(q);
		case_ok("skip over a call", skip_call, 7, 1, 0, 1);
		/* a predicated call is not a form this knows: left as decoded, and refused cleanly */
		const uint64_t pcall[] = { FENCE, RET, NOP, call(-3) | (uint64_t)P0 << 56, EMIT };
		case_refused("predicated call", pcall, 5);
	}

	printf("%d checks, %d failed\n", checks, failed);
	if (failed) { printf("FAILED\n"); return 1; }
	printf("all passed\n");
	return 0;
}
