/* hle_offline_glue_test.c -- HLE-LIB-06: apputil.c's handlers for the in-game IME, LiveArea, background download,
 * store, BGM port and LoadExec, called through the hle_apputil table by name with arguments in guest memory (the pure
 * contracts are hle_offline_test.c). The game's IME handler is the guest_call shim below: it records each event and,
 * like a game, may close the keyboard from inside the handler.
 * Fail-before: every one of these names is missing from hle_apputil (call() answers 0xdeadbeef: the generic stub's 0).
 *   cc -O1 -g -Wall -D_GNU_SOURCE $INC -I $SDL3_INC hle_offline_glue_test.c -o /tmp/hle_offline_glue_test -lpthread -lm \
 *     && /tmp/hle_offline_glue_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include "../core/hle_offline.h"
#ifndef APPUTIL_UNDER_TEST
#define APPUTIL_UNDER_TEST "../core/apputil.c"
#endif
#include APPUTIL_UNDER_TEST   /* (-DAPPUTIL_UNDER_TEST='"<older apputil.c>"': the fail-before run) */
#include "../core/imedlg.c"

/* ---- apputil.c expects these (sysmodule_test.c's shims, with real guest memory) */
uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }
uint32_t guest_alloc(uint32_t size, uint32_t align) { static uint32_t heap = 0x81800000; heap = (heap + align - 1) / align * align; uint32_t a = heap; heap += size; return a; }
void gwatch_read_open(uint32_t addr, uint32_t len) { (void)addr; (void)len; }
int net_online(void) { return 0; }
const char *net_online_needs(void) { return ""; }
int overlay_draws_hud(void) { return 0; }
void overlay_toast_titled(const char *title, const char *body, double seconds) { (void)title; (void)body; (void)seconds; }
void plat_random(void *buf, size_t n) { memset(buf, 0x5a, n); }
const char *settings_str(const char *key, const char *def, char *buf, int bufsize) { (void)key; (void)buf; (void)bufsize; return def; }
void settings_set_str(const char *key, const char *v) { (void)key; (void)v; }
void settings_set_own(const char *key, const char *v) { (void)key; (void)v; }
int settings_save(void) { return 1; }

/* the game's IME handler: records (fn, arg, id, the event's caret/str/change); close_in_handler: closes on ENTER */
#define HANDLER 0x81100001u
#define ARG 0x1234u
static uint32_t ev_ids[16], ev_caret[16], ev_str[16], ev_change[16], ev_arg; static int n_ev, close_in_handler;
static uint32_t call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t s0);
uint32_t guest_call(Cpu *parent, uint32_t fn, uint32_t a0, uint32_t a1) {
	(void)parent;
	if (fn != HANDLER || n_ev >= 16) return 0;
	ev_arg = a0; ev_ids[n_ev] = RD32(a1); ev_caret[n_ev] = RD32(a1 + 12); ev_str[n_ev] = RD32(a1 + 16); ev_change[n_ev] = RD32(a1 + 24); n_ev++;
	if (close_in_handler && RD32(a1) == HLE_IME_EVENT_PRESS_ENTER) call("sceImeClose", 0, 0, 0, 0, 0);
	return 0;
}

static int fails;
static void check(const char *what, int ok) { printf("%-66s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static uint32_t call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t s0) {
	for (const HleEntry *e = hle_apputil; e->name; e++)
		if (!strcmp(e->name, name)) {
			Cpu cpu = {0}; cpu.r[0] = a0; cpu.r[1] = a1; cpu.r[2] = a2; cpu.r[3] = a3; cpu.r[13] = 0x82000000u; WR32(0x82000000u, s0);
			e->fn(&cpu); return cpu.r[0];
		}
	return 0xdeadbeefu;
}
static uint32_t gstr(uint32_t a, const char *s) { memcpy(G(a), s, strlen(s) + 1); return a; }
static uint32_t gstr16(uint32_t a, const char *s) { size_t i = 0; for (; s[i]; i++) WR16(a + 2 * (uint32_t)i, (uint8_t)s[i]); WR16(a + 2 * (uint32_t)i, 0); return a; }
static int buf_is(uint32_t a, const char *s) { size_t i = 0; for (; s[i]; i++) if (RD16(a + 2 * (uint32_t)i) != (uint8_t)s[i]) return 0; return RD16(a + 2 * (uint32_t)i) == 0; }
/* a SceImeParam at p: work, arg, handler, initial text, max length, text buffer */
#define PARAM 0x81000000u
#define WORK 0x81010000u
#define INIT 0x81020000u
#define TEXT 0x81030000u
static void ime_param(uint32_t max) {
	memset(G(PARAM), 0, 0x40);
	WR32(PARAM + 0x1c, WORK); WR32(PARAM + 0x20, ARG); WR32(PARAM + 0x24, HANDLER); WR32(PARAM + 0x2c, gstr16(INIT, "Ash")); WR32(PARAM + 0x30, max); WR32(PARAM + 0x34, TEXT);
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }

	/* ---- the in-game IME, no window (headless): the initial text counts as typed */
	check("ime: Update before Open is NOT_OPENED (Delta's save-dialog poll)", call("sceImeUpdate", 0, 0, 0, 0, 0) == HLE_IME_ERR_NOT_OPENED);
	check("ime: Close before Open is NOT_OPENED", call("sceImeClose", 0, 0, 0, 0, 0) == HLE_IME_ERR_NOT_OPENED);
	ime_param(16); WR32(PARAM + 0x24, 0);
	check("ime: Open without a handler is INVALID_HANDLER", call("sceImeOpen", PARAM, 0, 0, 0, 0) == HLE_IME_ERR_INVALID_HANDLER);
	ime_param(16);
	check("ime: Open -> 0", call("sceImeOpen", PARAM, 0, 0, 0, 0) == 0);
	check("ime: a second Open is ALREADY_OPENED", call("sceImeOpen", PARAM, 0, 0, 0, 0) == HLE_IME_ERR_ALREADY_OPENED);
	check("ime: Update 1 -> 0, one OPEN event with the game's arg", call("sceImeUpdate", 0, 0, 0, 0, 0) == 0 && n_ev == 1 && ev_ids[0] == HLE_IME_EVENT_OPEN && ev_arg == ARG);
	check("ime: Update 2 -> UPDATE_TEXT, PRESS_ENTER", call("sceImeUpdate", 0, 0, 0, 0, 0) == 0 && n_ev == 3 && ev_ids[1] == HLE_IME_EVENT_UPDATE_TEXT && ev_ids[2] == HLE_IME_EVENT_PRESS_ENTER);
	check("ime: ... the text \"Ash\" in inputTextBuffer, caret 3, change 0", buf_is(TEXT, "Ash") && ev_caret[1] == 3 && ev_str[1] == TEXT && ev_change[1] == 0);
	check("ime: Update 3 -> no more events", call("sceImeUpdate", 0, 0, 0, 0, 0) == 0 && n_ev == 3);
	check("ime: Close -> 0, Update is NOT_OPENED again", call("sceImeClose", 0, 0, 0, 0, 0) == 0 && call("sceImeUpdate", 0, 0, 0, 0, 0) == HLE_IME_ERR_NOT_OPENED);

	/* ---- with a window: the card answers; the handler closes on ENTER */
	imedlg_set_ui_ready(1); n_ev = 0; close_in_handler = 1;
	ime_param(4);
	check("ime(card): Open -> 0, the card shows the initial text", call("sceImeOpen", PARAM, 0, 0, 0, 0) == 0);
	{ char t[64], tt[64]; uint32_t mx; int seq; check("ime(card): ... card open with \"Ash\", max 4", imedlg_view(tt, sizeof tt, t, sizeof t, &mx, &seq) && !strcmp(t, "Ash") && mx == 4); }
	call("sceImeUpdate", 0, 0, 0, 0, 0); call("sceImeUpdate", 0, 0, 0, 0, 0);
	check("ime(card): OPEN, then nothing while the player types", n_ev == 1 && ev_ids[0] == HLE_IME_EVENT_OPEN);
	imedlg_answer(1, "Ashes");   /* five characters, max 4 */
	check("ime(card): answered -> UPDATE_TEXT + ENTER, text cut to 4 units", call("sceImeUpdate", 0, 0, 0, 0, 0) == 0 && n_ev == 3 && ev_ids[2] == HLE_IME_EVENT_PRESS_ENTER && buf_is(TEXT, "Ashe") && ev_caret[1] == 4 && ev_change[1] == 1);
	check("ime(card): the handler's own Close took effect", call("sceImeUpdate", 0, 0, 0, 0, 0) == HLE_IME_ERR_NOT_OPENED);
	n_ev = 0; close_in_handler = 0; ime_param(16);
	call("sceImeOpen", PARAM, 0, 0, 0, 0); call("sceImeUpdate", 0, 0, 0, 0, 0);
	imedlg_answer(0, 0);
	check("ime(card): cancelled -> PRESS_CLOSE, the buffer untouched", call("sceImeUpdate", 0, 0, 0, 0, 0) == 0 && n_ev == 2 && ev_ids[1] == HLE_IME_EVENT_PRESS_CLOSE && buf_is(TEXT, "Ashe"));
	check("ime(card): Close closes the card", call("sceImeClose", 0, 0, 0, 0, 0) == 0 && !imedlg_view((char[8]){0}, 8, (char[8]){0}, 8, &(uint32_t){0}, &(int){0}));
	imedlg_set_ui_ready(0);

	/* ---- LiveArea */
	const uint32_t s = 0x81040000u, rev = 0x81041000u;
	check("livearea: GetStatus is NO_REQUEST (0x80104001)", call("sceLiveAreaGetStatus", 0, 0, 0, 0, 0) == HLE_LA_ERR_NO_REQUEST);
	check("livearea: ReplaceAllSync(NULL) PARAM, (app0:...) 0", call("sceLiveAreaReplaceAllSync", 0, 0, 0, 0, 0) == HLE_LA_ERR_PARAM && call("sceLiveAreaReplaceAllSync", gstr(s, "app0:sce_sys/livearea/contents"), 0, 0, 0, 0) == 0);
	gstr(s, "01.00"); gstr(s + 0x10, "<frame id='frame2'><liveitem/></frame>"); gstr(s + 0x100, "app0:");
	check("livearea: UpdateFrameSync (target on the stack) -> 0", call("sceLiveAreaUpdateFrameSync", s, s + 0x10, 0xffffffffu, s + 0x100, 0) == 0);
	check("livearea: UpdateFrameSync target 7 (stack) -> PARAM", call("sceLiveAreaUpdateFrameSync", s, s + 0x10, 0xffffffffu, s + 0x100, 7) == HLE_LA_ERR_PARAM);
	WR64(rev, 0xffffffffffffffffull);
	check("livearea: GetFrameRevision(frame2) -> 0, revision 1 written", call("sceLiveAreaGetFrameRevision", gstr(s + 0x200, "frame2"), rev, 0, 0, 0) == 0 && RD64(rev) == 1);
	check("livearea: GetFrameRevision(frame9) UNAVAILABLE, (frame2, NULL) PARAM", call("sceLiveAreaGetFrameRevision", gstr(s + 0x200, "frame9"), rev, 0, 0, 0) == HLE_LA_ERR_UNAVAILABLE && call("sceLiveAreaGetFrameRevision", gstr(s + 0x200, "frame2"), 0, 0, 0, 0) == HLE_LA_ERR_PARAM);

	/* ---- AppUtil / AppMgr */
	memset(G(s), 0xee, 0x40);
	check("bgdl: GetStatus(NULL) PARAMETER; (buf) 0 and 0x2c bytes zeroed", call("sceAppUtilBgdlGetStatus", 0, 0, 0, 0, 0) == HLE_APPUTIL_ERR_PARAMETER && call("sceAppUtilBgdlGetStatus", s, 0, 0, 0, 0) == 0 && RD32(s) == 0 && RD32(s + 0x28) == 0 && RD8(s + 0x2c) == 0xee);
	WR32(s, 1); WR32(s + 4, gstr(s + 0x100, "EP0000-TEST00000_00-0000000000000000"));
	check("store: Browse(NULL) PARAMETER; (param) 0", call("sceAppUtilStoreBrowse", 0, 0, 0, 0, 0) == HLE_APPUTIL_ERR_PARAMETER && call("sceAppUtilStoreBrowse", s, 0, 0, 0, 0) == 0);
	check("bgm: AcquireBgmPort -> 0", call("sceAppMgrAcquireBgmPort", 0, 0, 0, 0, 0) == 0);
	char dir[] = "/tmp/hle06fsXXXXXX"; if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
	char p[256]; snprintf(p, sizeof p, "%s/app0", dir); mkdir(p, 0755); snprintf(p, sizeof p, "%s/app0/other.self", dir); FILE *f = fopen(p, "wb"); if (f) { fputs("x", f); fclose(f); }
	setenv("RECOMP_FS", dir, 1);
	WR32(s + 0x300, 0);   /* argv: { NULL } */
	check("loadexec: a SELF app0 doesn't ship is INVALID_SELF_PATH", call("sceAppMgrLoadExec", gstr(s, "app0:cardgame.self"), s + 0x300, 0, 0, 0) == HLE_APPMGR_ERR_INVALID_SELF_PATH);
	check("loadexec: an existing SELF is NOEXEC (loud)", call("sceAppMgrLoadExec", gstr(s, "app0:other.self"), s + 0x300, 0, 0, 0) == HLE_APPMGR_ERR_NOEXEC);
	check("loadexec: with an option parameter is INVALID; NULL path NULL_POINTER", call("sceAppMgrLoadExec", gstr(s, "app0:other.self"), s + 0x300, s + 0x400, 0, 0) == HLE_APPMGR_ERR_INVALID && call("sceAppMgrLoadExec", 0, 0, 0, 0, 0) == HLE_APPMGR_ERR_NULL_POINTER);
	remove(p); snprintf(p, sizeof p, "%s/app0", dir); rmdir(p); rmdir(dir);

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
