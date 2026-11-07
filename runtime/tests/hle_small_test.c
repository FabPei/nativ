/* hle_small_test.c -- HLE-PORT-01: the contracts of hle_small.h (see its header comment).
 * Fail-before is the runtime's own record: these imports were logged as "not implemented -- returns 0"
 * (sceKernelGetModel polled 10000+ times; sceAudioOutReleasePort by three titles), so a game got model 0
 * and its released ports stayed taken.
 *   cc -O1 -g -Wall -I ../core hle_small_test.c -o /tmp/hle_small_test && /tmp/hle_small_test
 */
#include <stdio.h>
#include "hle_small.h"

static int fails;
static void check(const char *what, int ok) { printf("%-60s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	check("model: handheld is SCE_KERNEL_MODEL_VITA 0x10000", hle_kernel_model(0) == 0x10000u);
	check("model: PS TV is SCE_KERNEL_MODEL_VITATV 0x20000", hle_kernel_model(1) == 0x20000u);

	uint32_t len[HLE_AUDIO_PORTS] = {0};
	int ok = 1;
	for (int k = 0; k < HLE_AUDIO_PORTS; k++) { int s = hle_audio_port_free_slot(len); ok &= s == k; if (s >= 0) len[s] = 256; }
	check("audio: 8 opens take slots 0..7", ok);
	check("audio: a 9th open finds no slot (PORT_FULL)", hle_audio_port_free_slot(len) == -1);
	int s = hle_audio_port_slot(len, 3);   /* release handle 3 = slot 2 */
	check("audio: handle 3 names slot 2", s == 2);
	if (s >= 0) len[s] = 0;
	check("audio: after release the next open reuses slot 2", hle_audio_port_free_slot(len) == 2);
	check("audio: releasing it again is INVALID_PORT (no slot)", hle_audio_port_slot(len, 3) == -1);
	check("audio: handle 0 and 9 name no port", hle_audio_port_slot(len, 0) == -1 && hle_audio_port_slot(len, 9) == -1);
	/* the open/release cycle a game runs per scene: 100 cycles never run out */
	ok = 1; for (int k = 0; k < 100; k++) { int f = hle_audio_port_free_slot(len); ok &= f >= 0; if (f >= 0) { len[f] = 256; len[f] = 0; } }
	check("audio: 100 open/release cycles never run out", ok);

	HleNgsPatchSrc p[4] = {{0}};
	int n = 0;
	p[n] = (HleNgsPatchSrc){0x70, 0, hle_ngs_patch_sub(p, n, 0x70, 0, -1), 1}; n++;
	p[n] = (HleNgsPatchSrc){0x70, 0, hle_ngs_patch_sub(p, n, 0x70, 0, -1), 1}; n++;
	p[n] = (HleNgsPatchSrc){0x70, 1, hle_ngs_patch_sub(p, n, 0x70, 1, -1), 1}; n++;
	check("ngs: -1 takes the first free slot per output (0, 1; other output 0)", p[0].sub == 0 && p[1].sub == 1 && p[2].sub == 0);
	check("ngs: find (voice, out 0, slot 1) is the second patch", hle_ngs_patch_find(p, n, 0x70, 0, 1) == 1);
	p[0].active = 0;
	check("ngs: a removed patch is not found", hle_ngs_patch_find(p, n, 0x70, 0, 0) == -1);
	check("ngs: its slot is free again for -1", hle_ngs_patch_sub(p, n, 0x70, 0, -1) == 0);
	check("ngs: an explicit slot is kept", hle_ngs_patch_sub(p, n, 0x70, 0, 5) == 5);

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
