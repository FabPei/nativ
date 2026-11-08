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
	/* SceCtrlData: 32-byte records, n samples back to back (an 8-sample read fills exactly 0x100 bytes) */
	check("ctrl: SceCtrlData is 32 bytes", HLE_CTRL_DATA_SIZE == 32);
	check("ctrl: sample 1 follows sample 0 at +0x20", hle_ctrl_data_addr(0x1000, 1) == 0x1020);
	check("ctrl: 8 samples end at +0x100 (no write past an 8-record buffer)", hle_ctrl_data_addr(0x1000, 7) + HLE_CTRL_DATA_SIZE == 0x1100);
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

	/* HLE-LIB-03: a synthetic PARAM.SFO with one string and one integer */
	uint8_t sfo[128] = {0}; uint32_t v = 0;
	memcpy(sfo, "\0PSF", 4); sfo[4] = 1; sfo[1 * 0 + 8] = 0x34; sfo[0xc] = 0x50; sfo[0x10] = 2;   /* keys at 0x34, data at 0x50 */
	sfo[0x14] = 0; sfo[0x16] = 0x04; sfo[0x17] = 0x02; sfo[0x18] = 4; sfo[0x20] = 0;          /* TITLE (utf-8), data +0 */
	sfo[0x24] = 6; sfo[0x26] = 0x04; sfo[0x27] = 0x04; sfo[0x28] = 4; sfo[0x2c] = 4; sfo[0x30] = 4;   /* SAVEDATA_MAX_SIZE (u32), data +4 */
	memcpy(sfo + 0x34, "TITLE\0SAVEDATA_MAX_SIZE", 24); memcpy(sfo + 0x50, "abc\0", 4); sfo[0x54] = 0x00; sfo[0x55] = 0x04;   /* 1024 */
	check("sfo: SAVEDATA_MAX_SIZE read as u32 1024", hle_sfo_int(sfo, sizeof sfo, "SAVEDATA_MAX_SIZE", &v) && v == 1024);
	check("sfo: a string key is no integer", !hle_sfo_int(sfo, sizeof sfo, "TITLE", &v));
	check("sfo: a missing key / bad magic", !hle_sfo_int(sfo, sizeof sfo, "NOPE", &v) && !hle_sfo_int((const uint8_t *)"xxxxxxxxxxxxxxxxxxxxxxxx", 24, "TITLE", &v));

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
