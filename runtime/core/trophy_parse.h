/*
 * HLE-LIB-02: reading a game's trophy set (app0/sce_sys/trophy/<NPCOMMID>_<nn>/TROPHY.TRP) -- pure and
 * header-only, so runtime/tests/trophy_test.c checks it with a synthetic TRP.
 * Format (big-endian): header magic 0xDCA24D00 +0, version +4, file size +8 (u64), entry count +0x10, entry size
 * +0x14; entries from +0x40: name[32], offset +0x20 (u64), size +0x28 (u64), flags +0x30. In a decrypted copy the
 * .SFM entries are XML: TROPCONF.SFM lists <trophy id="NNN" hidden="yes|no" ttype="P|G|S|B" pid="NNN" [gid="N"]>,
 * TROP_<lang>.SFM (TROP.SFM: the default) adds <name> and <detail> per trophy and <title-name>/<title-detail>.
 * Icons: ICON0.PNG (the set), GR<NNN>.PNG (a group), TROP<NNN>.PNG (a trophy). Nothing is decrypted here.
 */
#ifndef RECOMP_TROPHY_PARSE_H
#define RECOMP_TROPHY_PARSE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TROPHY_MAX 128
enum { TROPHY_GRADE_UNKNOWN = 0, TROPHY_GRADE_PLATINUM = 1, TROPHY_GRADE_GOLD = 2, TROPHY_GRADE_SILVER = 3, TROPHY_GRADE_BRONZE = 4 };
typedef struct { int32_t id, grade, gid, pid; int hidden; char name[128]; char detail[512]; } TrophyDef;

static inline uint32_t trp_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint64_t trp_be64(const uint8_t *p) { return (uint64_t)trp_be32(p) << 32 | trp_be32(p + 4); }
/* an entry by name: 1 and its bytes, or 0 */
static inline int trp_find(const uint8_t *b, size_t n, const char *name, const uint8_t **out, size_t *len) {
	if (n < 0x40 || trp_be32(b) != 0xDCA24D00u) return 0;
	const uint32_t cnt = trp_be32(b + 0x10), esz = trp_be32(b + 0x14);
	if (esz < 0x34 || 0x40 + (uint64_t)cnt * esz > n) return 0;
	for (uint32_t i = 0; i < cnt; i++) {
		const uint8_t *e = b + 0x40 + (size_t)i * esz;
		char nm[33]; memcpy(nm, e, 32); nm[32] = 0;
		if (strcmp(nm, name)) continue;
		const uint64_t off = trp_be64(e + 0x20), sz = trp_be64(e + 0x28);
		if (off > n || sz > n - off) return 0;
		*out = b + off; *len = (size_t)sz; return 1;
	}
	return 0;
}
/* an attribute's value inside one tag [t, end): copied into v (n bytes); 0 if absent */
static inline int trp_attr(const char *t, const char *end, const char *a, char *v, size_t n) {
	const size_t al = strlen(a);
	for (const char *p = t; p + al + 2 < end; p++) {
		if ((p == t || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') && !memcmp(p, a, al) && p[al] == '=' && p[al + 1] == '"') {
			const char *s = p + al + 2, *q = s; while (q < end && *q != '"') q++;
			size_t k = (size_t)(q - s); if (k >= n) k = n - 1; memcpy(v, s, k); v[k] = 0; return 1;
		}
	}
	return 0;
}
/* XML text with the five entities decoded */
static inline void trp_text(const char *s, const char *e, char *out, size_t n) {
	size_t k = 0;
	while (s < e && k + 1 < n) {
		if (*s == '&') {
			static const struct { const char *ent; char c; } m[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
			int hit = 0;
			for (size_t j = 0; j < 5 && !hit; j++) { size_t l = strlen(m[j].ent); if ((size_t)(e - s) >= l && !memcmp(s, m[j].ent, l)) { out[k++] = m[j].c; s += l; hit = 1; } }
			if (hit) continue;
		}
		out[k++] = *s++;
	}
	out[k] = 0;
}
/* the text of the first <tag>...</tag> in [s, e) */
static inline int trp_elem(const char *s, const char *e, const char *tag, char *out, size_t n) {
	char open[48], close[48]; snprintf(open, sizeof open, "<%s>", tag); snprintf(close, sizeof close, "</%s>", tag);
	const size_t ol = strlen(open), cl = strlen(close);
	for (const char *p = s; p + ol <= e; p++) if (!memcmp(p, open, ol)) {
		const char *a = p + ol;
		for (const char *q = a; q + cl <= e; q++) if (!memcmp(q, close, cl)) { trp_text(a, q, out, n); return 1; }
		return 0;
	}
	return 0;
}
static inline int trp_grade(char t) { return t == 'P' ? TROPHY_GRADE_PLATINUM : t == 'G' ? TROPHY_GRADE_GOLD : t == 'S' ? TROPHY_GRADE_SILVER : t == 'B' ? TROPHY_GRADE_BRONZE : TROPHY_GRADE_UNKNOWN; }
/* the trophies of an SFM (TROPCONF or TROP_xx) into defs[], indexed by id; names filled when present.
 * Returns the number of trophies (the highest id + 1), 0 on nothing usable. */
static inline int trp_parse_sfm(const char *x, size_t n, TrophyDef *defs, int with_names) {
	const char *e = x + n; int count = 0;
	for (const char *p = x; p + 8 < e; p++) {
		if (memcmp(p, "<trophy ", 8)) continue;
		const char *gt = p; while (gt < e && *gt != '>') gt++;
		char v[16];
		if (!trp_attr(p, gt, "id", v, sizeof v)) continue;
		const int id = atoi(v); if (id < 0 || id >= TROPHY_MAX) continue;
		TrophyDef *d = &defs[id]; d->id = id;
		if (!with_names) {   /* (the attributes are TROPCONF's: a names file only adds the texts) */
			d->grade = trp_attr(p, gt, "ttype", v, sizeof v) ? trp_grade(v[0]) : TROPHY_GRADE_UNKNOWN;
			d->hidden = trp_attr(p, gt, "hidden", v, sizeof v) && !strcmp(v, "yes");
			d->pid = trp_attr(p, gt, "pid", v, sizeof v) ? atoi(v) : -1;
			d->gid = trp_attr(p, gt, "gid", v, sizeof v) ? atoi(v) : -1;
		} else {
			const char *end = gt; while (end + 9 <= e && memcmp(end, "</trophy>", 9)) end++;
			trp_elem(gt, end, "name", d->name, sizeof d->name);
			trp_elem(gt, end, "detail", d->detail, sizeof d->detail);
		}
		if (id + 1 > count) count = id + 1;
	}
	return count;
}

/* ---- unlocking (sceNpTrophyUnlockTrophy), on a 128-bit flag array (SceNpTrophyFlagArray: bit id%32 of word id/32) */
#define TROPHY_ERR_INVALID_TROPHY_ID     0x8055160Eu
#define TROPHY_ERR_ALREADY_UNLOCKED      0x8055160Fu
#define TROPHY_ERR_PLATINUM_CANNOT_UNLOCK 0x80551610u
static inline int trophy_is_set(const uint32_t f[4], int id) { return (f[id >> 5] >> (id & 31)) & 1; }
/* unlock id; *plat: the platinum id when this unlock completed the set (it is unlocked too), else -1 */
static inline uint32_t trophy_unlock(const TrophyDef *d, int count, uint32_t f[4], int id, int32_t *plat) {
	*plat = -1;
	if (id < 0 || id >= count || d[id].grade == TROPHY_GRADE_UNKNOWN) return TROPHY_ERR_INVALID_TROPHY_ID;
	if (d[id].grade == TROPHY_GRADE_PLATINUM) return TROPHY_ERR_PLATINUM_CANNOT_UNLOCK;
	if (trophy_is_set(f, id)) return TROPHY_ERR_ALREADY_UNLOCKED;
	f[id >> 5] |= 1u << (id & 31);
	int p = -1; for (int i = 0; i < count; i++) if (d[i].grade == TROPHY_GRADE_PLATINUM) p = i;
	if (p >= 0 && !trophy_is_set(f, p)) {
		int all = 1;
		for (int i = 0; i < count && all; i++) if (d[i].grade != TROPHY_GRADE_UNKNOWN && d[i].grade != TROPHY_GRADE_PLATINUM && d[i].pid == p) all = trophy_is_set(f, i);
		if (all) { f[p >> 5] |= 1u << (p & 31); *plat = p; }
	}
	return 0;
}
#endif
