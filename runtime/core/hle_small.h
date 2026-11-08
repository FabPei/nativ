/*
 * HLE-PORT-01: the small, pure parts of a few system calls that games polled or called as placeholders
 * (docs: the ledger's hle-coverage inventory). Header-only, no guest memory, so runtime/tests/hle_small_test.c
 * checks the contracts on their own:
 *   - sceKernelGetModel: SCE_KERNEL_MODEL_VITA 0x10000, SCE_KERNEL_MODEL_VITATV 0x20000 (vitasdk
 *     psp2common/kernel/sysmem.h; the placeholder answered 0, no model at all)
 *   - the audio output ports (display.c): 8 slots, a slot is in use while its length is non-zero; Open takes the
 *     first free slot (handle = slot + 1), Release frees it (SCE_AUDIO_OUT_ERROR_INVALID_PORT for a bad or free
 *     handle). Before, Release was a placeholder: a game that opens and closes ports ran out after 8 opens
 *     (SCE_AUDIO_OUT_ERROR_PORT_FULL) and went silent.
 *   - NGS patches' source output slot (ngs.c): a SceNgsPatchSetupInfo's source_output_sub_index -1 means "the
 *     first free slot of that output"; sceNgsVoiceGetOutputPatch finds the active patch on (voice, output, slot).
 */
#ifndef RECOMP_HLE_SMALL_H
#define RECOMP_HLE_SMALL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define HLE_KERNEL_MODEL_VITA   0x10000u
#define HLE_KERNEL_MODEL_VITATV 0x20000u
static inline uint32_t hle_kernel_model(int pstv) { return pstv ? HLE_KERNEL_MODEL_VITATV : HLE_KERNEL_MODEL_VITA; }

/* SceCtrlData (sceCtrlPeek/ReadBufferPositive[2], ...Negative): timeStamp u64, buttons u32, lx ly rx ry, 12 analog-button
 * bytes, 4 reserved = 32 bytes; a read of n samples fills n consecutive records. A 64-byte stride wrote samples 4.. of an
 * 8-sample read past the game's buffer (Gravity Rush: over its saved registers, AUTO-01 2026-11-08) */
#define HLE_CTRL_DATA_SIZE 32u
static inline uint32_t hle_ctrl_data_addr(uint32_t buf, uint32_t i) { return buf + HLE_CTRL_DATA_SIZE * i; }

/* SceMotionSensorState (sceMotionGetSensorState fills numRecords of them): accelerometer xyz (g) +0, gyro xyz (rad/s) +12,
 * reserved +24, timestamp (us, 32 bits) +36, counter +40, reserved +44, hostTimestamp (us) +48, reserved +56: 64 bytes.
 * Left unfilled (the import was a stub), Tearaway read an accelerometer of 0 -- no gravity direction -- and oriented its
 * world from it: its whole picture degenerated into contour rings (AUTO-01 TW-6) */
#define HLE_MOTION_SENSOR_SIZE 64u
static inline void hle_motion_sensor_record(uint8_t out[64], const float acc[3], const float gyro[3], uint32_t ts, uint32_t counter, uint64_t host_ts) {
	memset(out, 0, 64);
	memcpy(out, acc, 12); memcpy(out + 12, gyro, 12);
	memcpy(out + 36, &ts, 4); memcpy(out + 40, &counter, 4); memcpy(out + 48, &host_ts, 8);
}

#define HLE_AUDIO_PORTS 8
#define HLE_AUDIO_ERR_INVALID_PORT 0x80260003u
#define HLE_AUDIO_ERR_PORT_FULL    0x80260005u
/* the first free slot (len[i] == 0), or -1 */
static inline int hle_audio_port_free_slot(const uint32_t len[HLE_AUDIO_PORTS]) {
	for (int i = 0; i < HLE_AUDIO_PORTS; i++) if (!len[i]) return i;
	return -1;
}
/* a handle (1-based) to its slot, or -1 when it names no open port */
static inline int hle_audio_port_slot(const uint32_t len[HLE_AUDIO_PORTS], uint32_t handle) {
	const uint32_t i = handle - 1;
	return i < HLE_AUDIO_PORTS && len[i] ? (int)i : -1;
}

/* NGS: one patch's source side, as the host keeps it */
typedef struct { uint32_t src; int32_t out, sub; int active; } HleNgsPatchSrc;
/* the slot a new patch from (src, out) takes: the requested one, or with -1 the lowest one no active patch uses */
static inline int32_t hle_ngs_patch_sub(const HleNgsPatchSrc *p, int n, uint32_t src, int32_t out, int32_t want) {
	if (want >= 0) return want;
	for (int32_t s = 0; s < 64; s++) {
		int used = 0;
		for (int i = 0; i < n && !used; i++) used = p[i].active && p[i].src == src && p[i].out == out && p[i].sub == s;
		if (!used) return s;
	}
	return 0;
}
/* HLE-LIB-03: an integer of a PARAM.SFO (little-endian: "\0PSF" +0, key table +8, data table +0xc, entry count +0x10;
 * 16-byte index entries from +0x14: key offset u16, format u16 (0x0404 = u32), length u32, max u32, data offset u32).
 * 1 and *v when the key holds an integer, else 0 */
static inline uint32_t hle_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int hle_sfo_int(const uint8_t *b, size_t n, const char *key, uint32_t *v) {
	if (n < 0x14 || hle_le32(b) != 0x46535000u) return 0;
	const uint32_t kt = hle_le32(b + 8), dt = hle_le32(b + 0xc), cnt = hle_le32(b + 0x10);
	for (uint32_t i = 0; i < cnt && 0x14 + 16 * (size_t)i + 16 <= n; i++) {
		const uint8_t *e = b + 0x14 + 16 * i;
		const uint32_t ko = kt + (e[0] | (uint32_t)e[1] << 8), fmt = e[2] | (uint32_t)e[3] << 8, doff = dt + hle_le32(e + 12);
		if (ko >= n || doff + 4 > n) continue;
		size_t kl = 0; while (ko + kl < n && b[ko + kl]) kl++;
		if (kl == strlen(key) && !memcmp(b + ko, key, kl) && fmt == 0x0404) { *v = hle_le32(b + doff); return 1; }
	}
	return 0;
}
/* the active patch on (src, out, sub): its index, or -1 */
static inline int hle_ngs_patch_find(const HleNgsPatchSrc *p, int n, uint32_t src, int32_t out, int32_t sub) {
	for (int i = 0; i < n; i++) if (p[i].active && p[i].src == src && p[i].out == out && p[i].sub == sub) return i;
	return -1;
}
#endif
