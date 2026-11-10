/*
 * SceSmart, the Vita's camera AR library (SCE_SYSMODULE_SMART 0x39): learned image targets and scene mapping on the
 * rear camera's frames. Soul Sacrifice Delta's AR mode uses it: Init, a learned image target from its own dictionary,
 * SceneMapping Start, then every frame SceneMappingRun and a switch on the mapping state the run writes at result+8
 * (0 nothing yet, 1 initialising, 2 tracking with landmarks, 3 lost, 4 failed -> Reset/Stop/Start), counts from
 * GetInitializationPointInfo / GetLandmarkInfo, and on exit Stop, Unregister, Destroy, Release. It ignores every
 * SceSmart result; the state word and the two counts are what it reads.
 *
 * No public source documents the library (vita-headers: NIDs only; Vita3K: all unimplemented), so this follows the
 * game's own use. The host does no tracking (yet): this is an honest "nothing found" -- the mapping never leaves
 * state 0, no initialisation points, no landmarks -- while the calls keep their order (Init before use, Start before
 * Run, real target handles), so the mode shows the camera picture and finds nothing instead of reading stale
 * memory. A call out of order answers SMART_ERR_STATE and says so in the log: the firmware's error codes are not
 * public, so that value is Nativ's own, not the system's.
 */
#include <stdio.h>
#include <string.h>
#include "hle.h"
#include "overlay.h"

#define SMART_ERR_STATE 0x80000001u   /* Nativ's own (see above): a call out of order or with a bad argument */
#define SMART_STATE_OFF 8u            /* the mapping state in SceneMappingRun's result */
#define SMART_TARGETS 16

static struct { int inited, mapping; uint64_t runs; int target_used[SMART_TARGETS], target_registered[SMART_TARGETS]; int complaints; } smart;

static void smart_reject(Cpu *c, const char *call, const char *why) {
	if (smart.complaints++ < 32) fprintf(stderr, "smart: %s rejected -- %s\n", call, why);
	RET(SMART_ERR_STATE);
}
static int target_slot(uint32_t id) { return id >= 1 && id <= SMART_TARGETS && smart.target_used[id - 1] ? (int)id - 1 : -1; }

static void h_init(Cpu *c) {   /* (param *: {?, alloc callback, free callback} in Soul Sacrifice Delta) */
	if (!A0) { smart_reject(c, "sceSmartInit", "no parameter"); return; }
	if (smart.inited) { smart_reject(c, "sceSmartInit", "already initialised"); return; }
	memset(&smart, 0, sizeof smart);
	smart.inited = 1;
	fprintf(stderr, "smart: AR library ready -- this host does no image tracking: the camera picture shows, nothing is ever found\n");
	RET(0);
}
static void h_release(Cpu *c) {
	if (!smart.inited) { smart_reject(c, "sceSmartRelease", "not initialised"); return; }
	fprintf(stderr, "smart: released after %llu mapping runs\n", (unsigned long long)smart.runs);
	memset(&smart, 0, sizeof smart);
	RET(0);
}
static void h_create_learned(Cpu *c) {   /* (int *id, param *: the learned dictionary and a read callback) */
	if (!smart.inited) { smart_reject(c, "sceSmartCreateLearnedImageTarget", "not initialised"); return; }
	if (!A0 || !A1) { smart_reject(c, "sceSmartCreateLearnedImageTarget", "null argument"); return; }
	for (int i = 0; i < SMART_TARGETS; i++) if (!smart.target_used[i]) {
		smart.target_used[i] = 1; smart.target_registered[i] = 0;
		WR32(A0, (uint32_t)i + 1);
		RET(0); return;
	}
	smart_reject(c, "sceSmartCreateLearnedImageTarget", "no free target");
}
static void h_destroy(Cpu *c) {
	int s = target_slot(A0);
	if (s < 0) { smart_reject(c, "sceSmartDestroyTarget", "unknown target"); return; }
	smart.target_used[s] = smart.target_registered[s] = 0;
	RET(0);
}
static void h_sm_register(Cpu *c) {
	int s = target_slot(A0);
	if (!smart.inited || s < 0) { smart_reject(c, "sceSmartSceneMappingRegisterTarget", !smart.inited ? "not initialised" : "unknown target"); return; }
	smart.target_registered[s] = 1;
	RET(0);
}
static void h_sm_unregister(Cpu *c) {
	int s = target_slot(A0);
	if (s < 0 || !smart.target_registered[s]) { smart_reject(c, "sceSmartSceneMappingUnregisterTarget", s < 0 ? "unknown target" : "not registered"); return; }
	smart.target_registered[s] = 0;
	RET(0);
}
static void h_sm_start(Cpu *c) {   /* (mode) */
	if (!smart.inited) { smart_reject(c, "sceSmartSceneMappingStart", "not initialised"); return; }
	if (smart.mapping) { smart_reject(c, "sceSmartSceneMappingStart", "already started"); return; }
	smart.mapping = 1;
	static int said;
	if (!said) { said = 1; overlay_toast_titled("Camera AR", "The game is looking for its AR picture through the camera.\nNativ cannot recognise it yet: nothing will be found.", 6.0); }
	RET(0);
}
static void h_sm_stop(Cpu *c) {
	if (!smart.mapping) { smart_reject(c, "sceSmartSceneMappingStop", "not started"); return; }
	smart.mapping = 0;
	RET(0);
}
static void h_sm_reset(Cpu *c) {
	if (!smart.inited) { smart_reject(c, "sceSmartSceneMappingReset", "not initialised"); return; }
	RET(0);
}
static void h_sm_run(Cpu *c) {   /* (result *, image, frame/time 64-bit in r2:r3) */
	if (!smart.mapping) { smart_reject(c, "sceSmartSceneMappingRun", "not started"); return; }
	if (!A0) { smart_reject(c, "sceSmartSceneMappingRun", "no result"); return; }
	WR32(A0 + SMART_STATE_OFF, 0);   /* nothing found yet -- every frame, so no stale state survives */
	smart.runs++;
	RET(0);
}
static void h_sm_propagate(Cpu *c) {
	if (!smart.mapping) { smart_reject(c, "sceSmartSceneMappingPropagateResult", "not started"); return; }
	RET(0);
}
/* (buffer, max entries) -> entries written. The game stores the answer as a count, so even a call out of order
 * answers 0 entries, never a (negative) error */
static void h_sm_info(Cpu *c) {
	if (!smart.mapping && smart.complaints++ < 32) fprintf(stderr, "smart: point/landmark query while mapping is stopped -- 0 entries\n");
	RET(0);
}

const HleEntry hle_smart[] = {
	{"sceSmartInit", h_init}, {"sceSmartRelease", h_release},
	{"sceSmartCreateLearnedImageTarget", h_create_learned}, {"sceSmartDestroyTarget", h_destroy},
	{"sceSmartSceneMappingRegisterTarget", h_sm_register}, {"sceSmartSceneMappingUnregisterTarget", h_sm_unregister},
	{"sceSmartSceneMappingStart", h_sm_start}, {"sceSmartSceneMappingStop", h_sm_stop}, {"sceSmartSceneMappingReset", h_sm_reset},
	{"sceSmartSceneMappingRun", h_sm_run}, {"sceSmartSceneMappingPropagateResult", h_sm_propagate},
	{"sceSmartSceneMappingGetInitializationPointInfo", h_sm_info}, {"sceSmartSceneMappingGetLandmarkInfo", h_sm_info},
	{0, 0},
};
