/* gaps.c -- GAPS-01: the runtime's gap counter (gaps.h). Test: runtime/tests/gaps_test.c */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gaps.h"

#if defined(__GNUC__) || defined(__clang__)
extern uint64_t display_frames(void) __attribute__((weak));   /* display.c: presented frames (absent in a unit test) */
#define GAPS_FRAME() (display_frames ? display_frames() : 0)
#else
extern uint64_t display_frames(void);
#define GAPS_FRAME() display_frames()
#endif

#define GAPS_SLOTS 2048   /* power of two; a run meets tens of keys, a bad one a few hundred */
typedef struct { uint64_t id; uint64_t first_frame, count; uint32_t key; int cat; char what[112]; } Gap;
static Gap gaps[GAPS_SLOTS];
static int ngaps, nfull;
static pthread_mutex_t gaps_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *const cat_names[GAP_NCAT] = {
	"tex_format", "usse_op", "shader_soft", "spirv", "gpu_soft", "gxm_state", "transfer", "gpu_req", "hle_stub",
	"nid_unknown", "audio_codec", "insn"
};
const char *gaps_cat_name(int cat) { return cat >= 0 && cat < GAP_NCAT ? cat_names[cat] : "?"; }

static uint32_t slot_of(uint64_t id) { uint64_t h = id * 0x9e3779b97f4a7c15ull; return (uint32_t)(h >> 40) & (GAPS_SLOTS - 1); }

void gap_note(int cat, uint32_t key, const char *what) {
	if (cat < 0 || cat >= GAP_NCAT) return;
	const uint64_t id = (uint64_t)(cat + 1) << 32 | key;
	uint32_t s = slot_of(id);
	for (int probe = 0; probe < GAPS_SLOTS; probe++, s = (s + 1) & (GAPS_SLOTS - 1)) {   /* the hot path: no lock */
		const uint64_t have = __atomic_load_n(&gaps[s].id, __ATOMIC_ACQUIRE);
		if (have == id) { __atomic_add_fetch(&gaps[s].count, 1, __ATOMIC_RELAXED); return; }
		if (!have) break;
	}
	pthread_mutex_lock(&gaps_lock);   /* first time for this key: claim a slot (another thread may just have) */
	s = slot_of(id);
	for (int probe = 0; probe < GAPS_SLOTS; probe++, s = (s + 1) & (GAPS_SLOTS - 1)) {
		if (gaps[s].id == id) { __atomic_add_fetch(&gaps[s].count, 1, __ATOMIC_RELAXED); pthread_mutex_unlock(&gaps_lock); return; }
		if (!gaps[s].id) break;
	}
	if (gaps[s].id || ngaps >= GAPS_SLOTS - 1) { nfull++; pthread_mutex_unlock(&gaps_lock); return; }
	Gap *g = &gaps[s];
	g->cat = cat; g->key = key; g->first_frame = GAPS_FRAME(); g->count = 1;
	snprintf(g->what, sizeof g->what, "%s", what ? what : "");
	__atomic_store_n(&g->id, id, __ATOMIC_RELEASE);
	ngaps++;
	const char *j = getenv("RECOMP_GAPS_JSON");
	if (j && *j) gaps_write_json(j);   /* (rare: once per new key) a run killed at its time limit keeps the file */
	pthread_mutex_unlock(&gaps_lock);
}

/* the slots in a stable order: category, then first frame, then key */
static int by_order(const void *a, const void *b) {
	const Gap *x = *(const Gap *const *)a, *y = *(const Gap *const *)b;
	if (x->cat != y->cat) return x->cat - y->cat;
	if (x->first_frame != y->first_frame) return x->first_frame < y->first_frame ? -1 : 1;
	return x->key < y->key ? -1 : x->key > y->key;
}
static int sorted(Gap **out) {
	int n = 0;
	for (int i = 0; i < GAPS_SLOTS; i++) if (__atomic_load_n(&gaps[i].id, __ATOMIC_ACQUIRE)) out[n++] = &gaps[i];
	qsort(out, n, sizeof *out, by_order);
	return n;
}
static int named(const Gap *g) { return g->what[0] && gap_hash(g->what) == g->key; }   /* gap_str: the name is the key */

int gaps_distinct(int cat) {
	int n = 0;
	for (int i = 0; i < GAPS_SLOTS; i++) if (__atomic_load_n(&gaps[i].id, __ATOMIC_ACQUIRE) && gaps[i].cat == cat) n++;
	return n;
}

void gaps_line(char *out, int size) {
	static Gap *list[GAPS_SLOTS];
	const int n = sorted(list);
	int at = snprintf(out, size, "GAPS:");
	for (int c = 0; c < GAP_NCAT && at < size; c++) {
		int k = 0; for (int i = 0; i < n; i++) if (list[i]->cat == c) k++;
		at += snprintf(out + at, at < size ? size - at : 0, "%s %s %d", c ? "," : "", cat_names[c], k);
		if (!k) continue;
		int shown = 0;
		for (int i = 0; i < n && at < size; i++) {
			if (list[i]->cat != c) continue;
			if (shown == 4) { at += snprintf(out + at, size - at, ",..."); break; }
			if (named(list[i])) at += snprintf(out + at, size - at, "%s%s", shown ? "," : " (", list[i]->what);
			else at += snprintf(out + at, size - at, "%s%#x", shown ? "," : " (", list[i]->key);
			shown++;
		}
		if (at < size) at += snprintf(out + at, size - at, ")");
	}
}

static void json_str(FILE *f, const char *s) {
	fputc('"', f);
	for (; *s; s++) {
		const unsigned char ch = (unsigned char)*s;
		if (ch == '"' || ch == '\\') fprintf(f, "\\%c", ch);
		else if (ch < 0x20) fprintf(f, "\\u%04x", ch);
		else fputc(ch, f);
	}
	fputc('"', f);
}

int gaps_write_json(const char *path) {
	static Gap *list[GAPS_SLOTS];
	const int n = sorted(list);
	char tmp[1024]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f) return -1;
	fprintf(f, "{\n  \"version\": 1,\n  \"total\": %d,\n  \"dropped\": %d,\n  \"categories\": {", n, nfull);
	for (int c = 0; c < GAP_NCAT; c++) {
		int k = 0; for (int i = 0; i < n; i++) if (list[i]->cat == c) k++;
		fprintf(f, "%s\n    \"%s\": {\"distinct\": %d, \"keys\": [", c ? "," : "", cat_names[c], k);
		int first = 1;
		for (int i = 0; i < n; i++) {
			if (list[i]->cat != c) continue;
			fprintf(f, "%s\n      {\"key\": \"%#x\", \"what\": ", first ? "" : ",", list[i]->key); json_str(f, list[i]->what);
			fprintf(f, ", \"first_frame\": %llu, \"count\": %llu}", (unsigned long long)list[i]->first_frame,
			        (unsigned long long)__atomic_load_n(&list[i]->count, __ATOMIC_RELAXED));
			first = 0;
		}
		fprintf(f, "%s]}", first ? "" : "\n    ");
	}
	fprintf(f, "\n  }\n}\n");
	fclose(f);
	remove(path);
	return rename(tmp, path);
}

void gaps_report(void) {
	static char line[2048];
	pthread_mutex_lock(&gaps_lock);
	gaps_line(line, sizeof line);
	fprintf(stderr, "\n%s\n", line);
	static Gap *list[GAPS_SLOTS];
	const int n = sorted(list);
	for (int i = 0; i < n; i++) {
		char key[16]; if (named(list[i])) snprintf(key, sizeof key, "-"); else snprintf(key, sizeof key, "%#x", list[i]->key);
		fprintf(stderr, "  gap %-11s %-10s first f%-6llu n %-9llu %s\n", cat_names[list[i]->cat], key,
		        (unsigned long long)list[i]->first_frame, (unsigned long long)__atomic_load_n(&list[i]->count, __ATOMIC_RELAXED),
		        list[i]->what);
	}
	if (nfull) fprintf(stderr, "  gap table full: %d more keys not recorded\n", nfull);
	const char *j = getenv("RECOMP_GAPS_JSON");
	if (j && *j) gaps_write_json(j);
	pthread_mutex_unlock(&gaps_lock);
	fflush(stderr);
}
