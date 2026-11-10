/*
 * HLE-LIB-02: SceNpTrophy offline. A context reads the game's own trophy set from its app0
 * (sce_sys/trophy/<NPCOMMID>_<nn>/TROPHY.TRP, trophy_parse.h: TROPCONF.SFM for ids/grades/hidden/platinum links,
 * TROP_<nn>.SFM in the system language the game sees (apputil.c, GAME-LANG), else TROP_01 (English) or TROP.SFM -- for
 * names, the PNG icons);
 * unlocks persist per player in <RECOMP_FS or fs>/trophy/<NPCOMMID>_<nn>.dat (magic "NTR1", the 128-bit flag array,
 * 128 RTC ticks) and show a toast. Error codes and struct layouts as the console's (Vita3K's SceNpTrophy module was the
 * reference for the call shapes: GameDetails/GameData/GroupDetails/GroupData/Details/Data). Before this file the set was
 * placeholders (Init, the queries, Unlock) plus a Create* that accepted everything: no trophy was ever unlocked.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include "hle.h"
#include "overlay.h"
#include "trophy_parse.h"
extern int apputil_game_lang(void) __attribute__((weak));   /* (GAME-LANG) apputil.c: the system language the game sees */

#define E_NOT_INITIALIZED     0x80551601u
#define E_ALREADY_INITIALIZED 0x80551602u
#define E_INVALID_ARGUMENT    0x80551604u
#define E_INSUFFICIENT_BUFFER 0x80551605u
#define E_INVALID_HANDLE      0x80551608u
#define E_INVALID_CONTEXT     0x80551609u
#define E_INVALID_NPCOMMID    0x8055160Au
#define E_INVALID_GROUP_ID    0x8055160Du
#define E_CONTEXT_EXISTS      0x80551616u
#define E_ICON_NOT_FOUND      0x80551618u
#define E_TRP_NOT_FOUND       0x80551619u

#define MAX_CTX 4
typedef struct {
	int used; char id[16];            /* "NPWR00000_00" */
	uint8_t *trp; size_t trp_len;
	TrophyDef defs[TROPHY_MAX]; int count; char title[128], detail[512];
	uint32_t flags[4]; uint64_t ticks[TROPHY_MAX];
} Ctx;
static Ctx ctxs[MAX_CTX];
static int inited, handles;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static const char *fs_root(void) { return getenv("RECOMP_FS") ? getenv("RECOMP_FS") : "fs"; }
static uint8_t *read_all(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb"); if (!f) return 0;
	fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *b = l > 0 ? malloc((size_t)l) : 0;
	if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = 0; }
	fclose(f); *n = b ? (size_t)l : 0; return b;
}
static void state_path(const Ctx *x, char *p, size_t n) { snprintf(p, n, "%s/trophy/%s.dat", fs_root(), x->id); }
static void state_load(Ctx *x) {
	char p[1024]; state_path(x, p, sizeof p); size_t n; uint8_t *b = read_all(p, &n);
	if (b && n >= 4 + 16 + 8 * TROPHY_MAX && !memcmp(b, "NTR1", 4)) { memcpy(x->flags, b + 4, 16); memcpy(x->ticks, b + 20, 8 * TROPHY_MAX); }
	free(b);
}
static void state_save(const Ctx *x) {
	char d[1024]; snprintf(d, sizeof d, "%s/trophy", fs_root()); mkdir(d, 0755);
	char p[1024]; state_path(x, p, sizeof p);
	FILE *f = fopen(p, "wb"); if (!f) { fprintf(stderr, "    trophy: can't write %s\n", p); return; }
	fwrite("NTR1", 1, 4, f); fwrite(x->flags, 1, 16, f); fwrite(x->ticks, 1, 8 * TROPHY_MAX, f); fclose(f);
}
static uint64_t rtc_tick_now(void) {   /* SceRtcTick: microseconds since 0001-01-01 */
	struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u + 62135596800000000ull;
}
static Ctx *ctx_of(uint32_t h) { return h >= 1 && h <= MAX_CTX && ctxs[h - 1].used ? &ctxs[h - 1] : 0; }
static uint32_t check_call(uint32_t ctx, uint32_t handle, Ctx **out) {   /* the common start of a query */
	if (!inited) return E_NOT_INITIALIZED;
	if (!(*out = ctx_of(ctx))) return E_INVALID_CONTEXT;
	if ((int32_t)handle < 1 || (int32_t)handle > handles) return E_INVALID_HANDLE;
	return 0;
}
static void put_str(uint32_t a, const char *s, size_t n) { size_t k = strlen(s); if (k >= n) k = n - 1; memcpy(G(a), s, k); memset(G(a + (uint32_t)k), 0, n - k); }

static void h_init(Cpu *c) { pthread_mutex_lock(&lock); uint32_t r = inited ? E_ALREADY_INITIALIZED : 0; inited = 1; pthread_mutex_unlock(&lock); RET(r); }
static void h_term(Cpu *c) {
	pthread_mutex_lock(&lock);
	uint32_t r = inited ? 0 : E_NOT_INITIALIZED; inited = 0;
	for (int i = 0; i < MAX_CTX; i++) { free(ctxs[i].trp); memset(&ctxs[i], 0, sizeof ctxs[i]); }
	pthread_mutex_unlock(&lock); RET(r);
}
/* (SceNpTrophyContext *out, const SceNpCommunicationId * {data[9], term, num, dummy}, const SceNpCommunicationSignature *, u64 options) */
static void h_create_context(Cpu *c) {
	if (!inited) { RET(E_NOT_INITIALIZED); return; }
	if (!A0 || !A1) { RET(E_INVALID_ARGUMENT); return; }
	char id[16]; memcpy(id, G(A1), 9); id[9] = 0; const uint32_t num = RD8(A1 + 10);
	if (num > 99) { RET(E_INVALID_NPCOMMID); return; }
	snprintf(id + 9, sizeof id - 9, "_%02u", num);
	pthread_mutex_lock(&lock);
	int k = -1;
	for (int i = 0; i < MAX_CTX; i++) if (ctxs[i].used && !strcmp(ctxs[i].id, id)) { pthread_mutex_unlock(&lock); RET(E_CONTEXT_EXISTS); return; }
	for (int i = 0; i < MAX_CTX && k < 0; i++) if (!ctxs[i].used) k = i;
	if (k < 0) { pthread_mutex_unlock(&lock); RET(E_INVALID_CONTEXT); return; }
	Ctx *x = &ctxs[k]; memset(x, 0, sizeof *x); snprintf(x->id, sizeof x->id, "%s", id);
	char p[1024]; snprintf(p, sizeof p, "%s/app0/sce_sys/trophy/%s/TROPHY.TRP", fs_root(), id);
	x->trp = read_all(p, &x->trp_len);
	const uint8_t *e; size_t n;
	if (!x->trp || !trp_find(x->trp, x->trp_len, "TROPCONF.SFM", &e, &n)) {
		fprintf(stderr, "    trophy: no trophy set at %s\n", p); free(x->trp); x->trp = 0;
		pthread_mutex_unlock(&lock); RET(E_TRP_NOT_FOUND); return;
	}
	x->count = trp_parse_sfm((const char *)e, n, x->defs, 0);
	char own[16]; snprintf(own, sizeof own, "TROP_%02d.SFM", apputil_game_lang ? apputil_game_lang() : 1);   /* (GAME-LANG) the game's language, else English */
	if (trp_find(x->trp, x->trp_len, own, &e, &n) || trp_find(x->trp, x->trp_len, "TROP_01.SFM", &e, &n) || trp_find(x->trp, x->trp_len, "TROP.SFM", &e, &n)) {
		trp_parse_sfm((const char *)e, n, x->defs, 1);
		trp_elem((const char *)e, (const char *)e + n, "title-name", x->title, sizeof x->title);
		trp_elem((const char *)e, (const char *)e + n, "title-detail", x->detail, sizeof x->detail);
	}
	state_load(x); x->used = 1;
	int unl = 0; for (int i = 0; i < x->count; i++) unl += trophy_is_set(x->flags, i);
	fprintf(stderr, "    trophy: %s \"%s\": %d trophies, %d unlocked\n", id, x->title, x->count, unl);
	pthread_mutex_unlock(&lock);
	WR32(A0, (uint32_t)k + 1); RET(0);
}
static void h_destroy_context(Cpu *c) {
	pthread_mutex_lock(&lock); Ctx *x = ctx_of(A0); uint32_t r = !inited ? E_NOT_INITIALIZED : !x ? E_INVALID_CONTEXT : 0;
	if (x) { free(x->trp); memset(x, 0, sizeof *x); }
	pthread_mutex_unlock(&lock); RET(r);
}
static void h_create_handle(Cpu *c) {
	if (!inited) { RET(E_NOT_INITIALIZED); return; }
	if (!A0) { RET(E_INVALID_ARGUMENT); return; }
	pthread_mutex_lock(&lock); const int h = ++handles; pthread_mutex_unlock(&lock);
	WR32(A0, (uint32_t)h); RET(0);
}
static void h_destroy_handle(Cpu *c) { RET(!inited ? E_NOT_INITIALIZED : (int32_t)A0 < 1 || (int32_t)A0 > handles ? E_INVALID_HANDLE : 0); }
static void h_abort_handle(Cpu *c) { h_destroy_handle(c); }

/* (ctx, handle, id, SceNpTrophyId *platinumId) */
static void h_unlock(Cpu *c) {
	pthread_mutex_lock(&lock);
	Ctx *x; uint32_t r = check_call(A0, A1, &x); int32_t plat = -1;
	if (!r) r = trophy_unlock(x->defs, x->count, x->flags, (int32_t)A2, &plat);
	if (!r) {
		const uint64_t t = rtc_tick_now(); x->ticks[A2] = t; if (plat >= 0) x->ticks[plat] = t;
		state_save(x);
		static const char *const grade[] = {"", "Platinum", "Gold", "Silver", "Bronze"};
		const int g = x->defs[A2].grade; char title[64]; snprintf(title, sizeof title, "Trophy unlocked (%s)", grade[g >= 0 && g <= 4 ? g : 0]);
		overlay_toast_titled(title, x->defs[A2].name[0] ? x->defs[A2].name : "Trophy", 5.0);
		if (plat >= 0) overlay_toast_titled("Trophy unlocked (Platinum)", x->defs[plat].name[0] ? x->defs[plat].name : "Platinum", 6.0);
		fprintf(stderr, "    trophy: %s #%u unlocked%s\n", x->id, A2, plat >= 0 ? " (and the platinum)" : "");
	}
	pthread_mutex_unlock(&lock);
	if (A3) WR32(A3, (uint32_t)plat);
	RET(r);
}
/* (ctx, handle, SceNpTrophyFlagArray *flags (128 bits), u32 *count) */
static void h_unlock_state(Cpu *c) {
	pthread_mutex_lock(&lock);
	Ctx *x; uint32_t r = check_call(A0, A1, &x);
	if (!r && (!A2 || !A3)) r = E_INVALID_ARGUMENT;
	if (!r) { for (int i = 0; i < 4; i++) WR32(A2 + 4 * i, x->flags[i]); WR32(A3, (uint32_t)x->count); }
	pthread_mutex_unlock(&lock); RET(r);
}
static void counts(const Ctx *x, int gid, int all, uint32_t num[5], uint32_t unl[5]) {   /* [0] total, [1..4] per grade */
	memset(num, 0, 5 * 4); memset(unl, 0, 5 * 4);
	for (int i = 0; i < x->count; i++) {
		const TrophyDef *d = &x->defs[i];
		if (d->grade == TROPHY_GRADE_UNKNOWN || (!all && d->gid != gid)) continue;
		num[0]++; num[d->grade]++;
		if (trophy_is_set(x->flags, i)) { unl[0]++; unl[d->grade]++; }
	}
}
/* (ctx, handle, SceNpTrophyGameDetails *, SceNpTrophyGameData *) */
static void h_game_info(Cpu *c) {
	pthread_mutex_lock(&lock);
	Ctx *x; uint32_t r = check_call(A0, A1, &x);
	if (!r) {
		uint32_t num[5], unl[5]; counts(x, 0, 1, num, unl);
		int groups = 0; for (int i = 0; i < x->count; i++) if (x->defs[i].gid + 1 > groups) groups = x->defs[i].gid + 1;
		if (A2) { WR32(A2 + 4, (uint32_t)groups); for (int g = 0; g < 5; g++) WR32(A2 + 8 + 4 * g, num[g]); put_str(A2 + 0x1c, x->title, 128); put_str(A2 + 0x9c, x->detail, 1024); }
		if (A3) { for (int g = 0; g < 5; g++) WR32(A3 + 4 + 4 * g, unl[g]); WR32(A3 + 0x18, num[0] ? unl[0] * 100 / num[0] : 0); }
	}
	pthread_mutex_unlock(&lock); RET(r);
}
/* (ctx, handle, group id, SceNpTrophyGroupDetails *, SceNpTrophyGroupData *) -- the 5th argument on the stack */
static void h_group_info(Cpu *c) {
	pthread_mutex_lock(&lock);
	Ctx *x; uint32_t r = check_call(A0, A1, &x); const int32_t gid = (int32_t)A2; const uint32_t dat = STK(0);
	if (!r) {
		uint32_t num[5], unl[5]; counts(x, gid, 0, num, unl);
		if (!num[0]) r = E_INVALID_GROUP_ID;
		else {
			if (A3) { WR32(A3 + 4, (uint32_t)gid); for (int g = 0; g < 5; g++) WR32(A3 + 8 + 4 * g, num[g]); put_str(A3 + 0x1c, gid < 0 ? x->title : "", 128); put_str(A3 + 0x9c, gid < 0 ? x->detail : "", 1024); }
			if (dat) { WR32(dat + 4, (uint32_t)gid); for (int g = 0; g < 5; g++) WR32(dat + 8 + 4 * g, unl[g]); WR32(dat + 0x1c, unl[0] * 100 / num[0]); }
		}
	}
	pthread_mutex_unlock(&lock); RET(r);
}
/* (ctx, handle, trophy id, SceNpTrophyDetails *, SceNpTrophyData *) -- a hidden trophy not yet unlocked keeps its texts */
static void h_trophy_info(Cpu *c) {
	pthread_mutex_lock(&lock);
	Ctx *x; uint32_t r = check_call(A0, A1, &x); const int32_t id = (int32_t)A2; const uint32_t dat = STK(0);
	if (!r && (id < 0 || id >= x->count || x->defs[id].grade == TROPHY_GRADE_UNKNOWN)) r = TROPHY_ERR_INVALID_TROPHY_ID;
	if (!r) {
		const TrophyDef *d = &x->defs[id]; const int unl = trophy_is_set(x->flags, id), veil = d->hidden && !unl;
		if (A3) { WR32(A3 + 4, (uint32_t)id); WR32(A3 + 8, (uint32_t)d->grade); WR32(A3 + 0xc, (uint32_t)d->gid); WR32(A3 + 0x10, (uint32_t)d->hidden);
			put_str(A3 + 0x14, veil ? "" : d->name, 128); put_str(A3 + 0x94, veil ? "" : d->detail, 1024); }
		if (dat) { WR32(dat + 4, (uint32_t)id); WR32(dat + 8, (uint32_t)unl); WR32(dat + 0xc, 0); WR64(dat + 0x10, unl ? x->ticks[id] : 0); }
	}
	pthread_mutex_unlock(&lock); RET(r);
}
/* an icon into (buffer, SceSize *size): a null buffer asks for the size */
static uint32_t icon(Ctx *x, const char *name, uint32_t buf, uint32_t size) {
	const uint8_t *e; size_t n;
	if (!size) return E_INVALID_ARGUMENT;
	if (!trp_find(x->trp, x->trp_len, name, &e, &n)) return E_ICON_NOT_FOUND;
	if (!buf) { WR32(size, (uint32_t)n); return 0; }
	if (RD32(size) < n) { WR32(size, (uint32_t)n); return E_INSUFFICIENT_BUFFER; }
	memcpy(G(buf), e, n); WR32(size, (uint32_t)n); return 0;
}
static void h_game_icon(Cpu *c) {   /* (ctx, handle, buffer, size *) */
	pthread_mutex_lock(&lock); Ctx *x; uint32_t r = check_call(A0, A1, &x);
	if (!r) r = icon(x, "ICON0.PNG", A2, A3);
	pthread_mutex_unlock(&lock); RET(r);
}
static void h_group_icon(Cpu *c) {   /* (ctx, handle, group id, buffer, size *) */
	pthread_mutex_lock(&lock); Ctx *x; uint32_t r = check_call(A0, A1, &x);
	if (!r) { char n[16]; snprintf(n, sizeof n, "GR%03d.PNG", (int32_t)A2); r = (int32_t)A2 < 0 ? icon(x, "ICON0.PNG", A3, STK(0)) : icon(x, n, A3, STK(0)); }
	pthread_mutex_unlock(&lock); RET(r);
}
static void h_trophy_icon(Cpu *c) {   /* (ctx, handle, trophy id, buffer, size *) */
	pthread_mutex_lock(&lock); Ctx *x; uint32_t r = check_call(A0, A1, &x);
	if (!r) { char n[16]; snprintf(n, sizeof n, "TROP%03d.PNG", (int32_t)A2); r = icon(x, n, A3, STK(0)); }
	pthread_mutex_unlock(&lock); RET(r);
}

const HleEntry hle_trophy[] = {
	{"sceNpTrophyInit", h_init}, {"sceNpTrophyTerm", h_term},
	{"sceNpTrophyCreateContext", h_create_context}, {"sceNpTrophyDestroyContext", h_destroy_context},
	{"sceNpTrophyCreateHandle", h_create_handle}, {"sceNpTrophyDestroyHandle", h_destroy_handle}, {"sceNpTrophyAbortHandle", h_abort_handle},
	{"sceNpTrophyUnlockTrophy", h_unlock}, {"sceNpTrophyGetTrophyUnlockState", h_unlock_state},
	{"sceNpTrophyGetGameInfo", h_game_info}, {"sceNpTrophyGetGroupInfo", h_group_info}, {"sceNpTrophyGetTrophyInfo", h_trophy_info},
	{"sceNpTrophyGetGameIcon", h_game_icon}, {"sceNpTrophyGetGroupIcon", h_group_icon}, {"sceNpTrophyGetTrophyIcon", h_trophy_icon},
	{0, 0},
};
