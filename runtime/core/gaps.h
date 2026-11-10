/* gaps.h -- GAPS-01: one counter for everything the runtime meets and cannot do, but runs on past (gaps.c).
 *
 * Before: each such place printed its own line somewhere in the log (or nothing), so a gap showed up in the picture
 * (a magenta texture, a missing draw) long before anyone read the line. Now every site also calls gap(), and the end
 * of every run prints one fixed line plus a table:
 *     GAPS: tex_format 3 (0x95000000,...), usse_op 0, hle_stub 12, ...
 * on stderr (test.log, session.log) and writes gaps.json (RECOMP_GAPS_JSON, `vitarecomp test` sets it next to
 * test.png; rewritten whenever a new gap appears, so a run stopped at its time limit leaves it too).
 *
 * Cost: a key is registered once; after that a site pays one hash probe and one relaxed atomic add.
 * The call goes through a weak reference, so a unit test that compiles one runtime source without gaps.c links and
 * simply counts nothing (runtime/tests/gaps_test.c tests the counter itself).
 */
#ifndef RECOMP_GAPS_H
#define RECOMP_GAPS_H
#include <stdint.h>

enum {
	GAP_TEX_FORMAT,     /* a texture format the sampler does not decode (sampled as magenta) */
	GAP_USSE_OP,        /* a USSE encoding the decoder does not know or runs as a no-op */
	GAP_SHADER_SOFT,    /* the software renderer could not interpret a draw's shaders (stand-in or skipped) */
	GAP_SPIRV,          /* a shader the GPU translator could not translate (op) */
	GAP_GPU_SOFT,       /* a draw the GPU path sent to the software path (reason) */
	GAP_GXM_STATE,      /* libgxm state set but not honoured (or approximated) by the backends */
	GAP_TRANSFER,       /* a transfer (copy/fill/downscale) format or layout not performed */
	GAP_GPU_REQ,        /* a GPU driver request (misc info, bridge) not implemented */
	GAP_HLE_STUB,       /* an import with no implementation and no module export (returns a stand-in value) */
	GAP_NID_UNKNOWN,    /* the same for an import whose NID has no name in the database (<lib>_<8 hex>, analyze.py) */
	GAP_AUDIO_CODEC,    /* an audio codec type the decoder does not implement */
	GAP_MISSED_FN,      /* a call to code that is neither recompiled nor an import (main.c recomp_call_external: the run stops; key = address, what = its image) */
	GAP_INSN,           /* a guest instruction the recompiler left UNIMPL (the run stops there) */
	GAP_NCAT
};

#if defined(__GNUC__) || defined(__clang__)
extern void gap_note(int cat, uint32_t key, const char *what) __attribute__((weak));
static inline void gap(int cat, uint32_t key, const char *what) { if (gap_note) gap_note(cat, key, what); }
#else
void gap_note(int cat, uint32_t key, const char *what);
static inline void gap(int cat, uint32_t key, const char *what) { gap_note(cat, key, what); }
#endif
/* a gap named by a string (an import, a fallback reason): the key is the name's hash, the name is kept */
static inline uint32_t gap_hash(const char *s) { uint32_t h = 2166136261u; while (s && *s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }
static inline void gap_str(int cat, const char *name) { gap(cat, gap_hash(name), name); }
/* an import named <lib>_<8 upper-case hex digits>: the pipeline's name for a NID the database does not know */
static inline int gap_unnamed_nid(const char *name) {
	int n = 0; while (name && name[n]) n++;
	if (n < 10 || name[n - 9] != '_') return 0;
	for (int i = n - 8; i < n; i++) if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'A' && name[i] <= 'F'))) return 0;
	return 1;
}

/* gaps.c */
const char *gaps_cat_name(int cat);
int gaps_distinct(int cat);                 /* keys seen in a category */
void gaps_line(char *out, int size);        /* "GAPS: tex_format 1 (0x95000000), usse_op 0, ..." */
void gaps_report(void);                     /* the line + table on stderr, gaps.json (main.c: at exit) */
int gaps_write_json(const char *path);
#endif
