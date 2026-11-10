/*
 * Game modules loaded on demand (MODLOAD-01): sceKernelLoadStartModule / sceKernelStopUnloadModule of a module the
 * game ships in app0 (outside sce_module/, which loads at start), e.g. "app0:bin/<name>.self".
 *
 * Translated code can't be relocated at run time, so the pipeline recompiles each such module ahead of time at a fixed
 * base of its own (pack LOAD_MODULES) and links its code into the binary; RECOMP_LOAD_MODULES names its image:
 * "<app0-relative path>=<image file>;...". Loading a module here does what the console's loader does with it:
 * fresh copies of its segments (so a reload starts from the initial .data), its exports registered, the import
 * cache cleared (an import of the main program now reaches it; locale variants export the same names, the newest
 * load wins), module_start(args, argp) run, its result in *status, a module id returned. Unloading runs module_stop
 * and takes its exports away again (its address range stays reserved for it).
 *
 * module_start / module_stop run on the calling thread (guest_call4: they take arguments; the start-up modules'
 * kernel_run_thread takes none).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "hle.h"

/* main.c's loader (a test supplies its own): an image's segments and exports in; its module_start / module_stop
 * (0: none) and the address range it covers out; 0 on success */
int image_load_module(const char *img, uint32_t *start, uint32_t *stop, uint32_t *lo, uint32_t *hi);
void image_drop_exports(uint32_t lo, uint32_t hi);   /* exports inside [lo, hi) no longer resolve */
void imports_reset(void);                            /* the next call of every import resolves again */

#define MODLOAD_MAX 128
#define E_NOENT 0x80010002u                 /* SCE_ERROR_ERRNO_ENOENT: no module image for that path */
#define E_MODULEMGR_NOENT 0x8002D01Cu       /* SCE_KERNEL_ERROR_MODULEMGR_NOENT: not a loaded module id */
#define E_MODULEMGR_START_FAILED 0x8002D000u
#define PRELOADED_ID 0x40030001u            /* the id of a sce_module/ module MODULES loaded at start (stop/unload: 0) */

static struct { char path[128]; char img[512]; uint32_t start, stop, lo, hi; int loaded; } mods[MODLOAD_MAX];
static int nmods = -1;

/* "app0:bin/god.self", "app0:/bin/God.self", "bin/god.self" -> "bin/god.self" (lower case, forward slashes) */
static void norm(char *out, size_t n, const char *p) {
	if (!strncmp(p, "app0:", 5)) p += 5;
	while (*p == '/' || *p == '\\') p++;
	size_t i = 0;
	for (; *p && i + 1 < n; p++) out[i++] = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
	out[i] = 0;
}

static void table(void) {
	if (nmods >= 0) return;
	nmods = 0;
	const char *e = getenv("RECOMP_LOAD_MODULES");
	if (!e) return;
	char *s = strdup(e), *save = 0;
	for (char *it = strtok_r(s, ";", &save); it && nmods < MODLOAD_MAX; it = strtok_r(0, ";", &save)) {
		char *eq = strchr(it, '=');
		if (!eq) continue;
		*eq = 0;
		norm(mods[nmods].path, sizeof mods[nmods].path, it);
		snprintf(mods[nmods].img, sizeof mods[nmods].img, "%s", eq + 1);
		nmods++;
	}
	free(s);
}

static uint32_t modid(int i) { return 0x40020001u + 2u * (uint32_t)i; }
static int index_of(uint32_t id) {
	int i = (int)((id - 0x40020001u) / 2u);
	return id >= 0x40020001u && (id - 0x40020001u) % 2u == 0 && i < nmods && mods[i].loaded ? i : -1;
}

static char *guest_str(uint32_t a, char *buf, size_t n) {
	size_t i = 0;
	if (a) for (; i + 1 < n && RD8(a + i); i++) buf[i] = (char)RD8(a + i);
	buf[i] = 0;
	return buf;
}

/* SceUID sceKernelLoadStartModule(const char *path, SceSize args, void *argp, int flags, SceKernelLMOption *opt, int *status) */
static void h_load_start_module(Cpu *c) {
	table();
	char raw[256], key[128];
	norm(key, sizeof key, guest_str(A0, raw, sizeof raw));
	uint32_t args = A1, argp = A2, status = STK(1);
	int i = 0;
	while (i < nmods && strcmp(mods[i].path, key)) i++;
	if (i == nmods) {
		if (!strncmp(key, "sce_module/", 11)) {   /* the game's start-up modules: MODULES loaded and started them before the game */
			fprintf(stderr, "    modload: %s: loaded with the program (MODULES), id %#x\n", raw, PRELOADED_ID);
			if (status) WR32(status, 0);
			RET(PRELOADED_ID); return;
		}
		fprintf(stderr, "    modload: %s: no module image (pack LOAD_MODULES) -> %#x\n", raw, E_NOENT); RET(E_NOENT); return;
	}
	if (!mods[i].loaded) {
		if (image_load_module(mods[i].img, &mods[i].start, &mods[i].stop, &mods[i].lo, &mods[i].hi)) {
			fprintf(stderr, "    modload: %s: image %s unreadable\n", raw, mods[i].img); RET(E_MODULEMGR_START_FAILED); return;
		}
		mods[i].loaded = 1;
		imports_reset();
	}
	uint32_t r = mods[i].start ? guest_call4(c, mods[i].start, args, argp, 0, 0) : 0;
	fprintf(stderr, "    modload: %s loaded at %#x-%#x, module_start -> %#x, id %#x\n", raw, mods[i].lo, mods[i].hi, r, modid(i));
	if (status) WR32(status, r);
	RET(modid(i));
}

/* int sceKernelStopUnloadModule(SceUID modid, SceSize args, void *argp, int flags, SceKernelULMOption *opt, int *status) */
static void h_stop_unload_module(Cpu *c) {
	table();
	int i = index_of(A0);
	uint32_t args = A1, argp = A2, status = STK(1);
	if (A0 == PRELOADED_ID) { if (status) WR32(status, 0); RET(0); return; }
	if (i < 0) { RET(E_MODULEMGR_NOENT); return; }
	uint32_t r = mods[i].stop ? guest_call4(c, mods[i].stop, args, argp, 0, 0) : 0;
	image_drop_exports(mods[i].lo, mods[i].hi);
	mods[i].loaded = 0;
	imports_reset();
	fprintf(stderr, "    modload: %s unloaded, module_stop -> %#x\n", mods[i].path, r);
	if (status) WR32(status, r);
	RET(0);
}

const HleEntry hle_modload[] = {
	{"sceKernelLoadStartModule", h_load_start_module}, {"sceKernelStopUnloadModule", h_stop_unload_module},
	{0, 0},
};
