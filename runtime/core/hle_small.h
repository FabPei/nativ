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
#include <stdlib.h>
#include <string.h>

#define HLE_KERNEL_MODEL_VITA   0x10000u
#define HLE_KERNEL_MODEL_VITATV 0x20000u
static inline uint32_t hle_kernel_model(int pstv) { return pstv ? HLE_KERNEL_MODEL_VITATV : HLE_KERNEL_MODEL_VITA; }

/* SceCtrlData (sceCtrlPeek/ReadBufferPositive[2], ...Negative): timeStamp u64, buttons u32, lx ly rx ry, 12 analog-button
 * bytes, 4 reserved = 32 bytes; a read of n samples fills n consecutive records. A 64-byte stride wrote samples 4.. of an
 * 8-sample read past the game's buffer (Gravity Rush: over its saved registers, AUTO-01 2026-11-08) */
#define HLE_CTRL_DATA_SIZE 32u
static inline uint32_t hle_ctrl_data_addr(uint32_t buf, uint32_t i) { return buf + HLE_CTRL_DATA_SIZE * i; }
/* the "2" reads (sceCtrlPeek/ReadBufferPositive2) report the shoulder buttons as SCE_CTRL_L1/R1 (0x400/0x800), the other
 * reads as SCE_CTRL_LTRIGGER/RTRIGGER (0x100/0x200) -- Vita3K's is_v2 bindings, on every port. A game reading only the
 * "2" form tests L1/R1: with the v1 bits on its port 0, Soul Sacrifice Delta never saw L or R (PAD-LBRB 2026-11-13) */
static inline uint32_t hle_ctrl_v2_buttons(uint32_t b) {
	return (b & ~0x300u) | (b & 0x100u ? 0x400u : 0) | (b & 0x200u ? 0x800u : 0);
}

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
/* sceAudioOutOutput's result on an open port: the samples it registered -- the port's length for a buffer, 0 for a NULL
 * buffer (a wait for the output to drain). It returned 0 always; a sound thread that checks for its grain (one game's
 * `cmp r0, #0x200; bne exit`) quit after its first buffer, and with it the game's audio and its movie player */
static inline uint32_t hle_audio_output_result(uint32_t port_len, uint32_t buf) { return buf ? port_len : 0; }

/* (AUTO-01 LBP-7) audio input: sceAudioInOpenPort(type, grain, freq, param). Types: 0 VOICE (16 kHz), 2 RAW (16 or 48 kHz);
 * one port per type; the handle is 1-based like an output port's. sceAudioInInput(port, dest) hands one grain of 16-bit mono
 * samples per call and returns when the hardware would have captured it (grain / freq). Without one (a stub returning 0 at
 * once) LittleBigPlanet's 'Mic Thread' spun at its priority beside the job workers */
#define HLE_AUDIO_IN_ERR_INVALID_PORT      0x80260101u
#define HLE_AUDIO_IN_ERR_INVALID_POINTER   0x80260102u
#define HLE_AUDIO_IN_ERR_PORT_FULL         0x80260103u
#define HLE_AUDIO_IN_ERR_NOT_OPENED        0x80260104u
#define HLE_AUDIO_IN_ERR_INVALID_PARAMETER 0x80260106u
/* 0 when (type, grain, freq) can be opened, an error otherwise */
static inline uint32_t hle_audio_in_open_check(uint32_t type, uint32_t grain, uint32_t freq) {
	if (type != 0 && type != 2) return HLE_AUDIO_IN_ERR_INVALID_PARAMETER;
	if (type == 0 && freq != 16000) return HLE_AUDIO_IN_ERR_INVALID_PARAMETER;
	if (type == 2 && freq != 16000 && freq != 48000) return HLE_AUDIO_IN_ERR_INVALID_PARAMETER;
	if (grain == 0 || grain > 4096) return HLE_AUDIO_IN_ERR_INVALID_PARAMETER;
	return 0;
}
/* the microseconds one grain takes to capture */
static inline uint64_t hle_audio_in_grain_us(uint32_t grain, uint32_t freq) { return freq ? (uint64_t)grain * 1000000u / freq : 0; }

/* (AUTO-01 TW-27) RECOMP_MOTION="from-to:ax,ay,az[/p];...": the accelerometer (g, Vita axes) a headless run reports in
 * those frames -- the motion counterpart of RECOMP_TOUCH. With /p, x and y flip sign every p frames: a shake (Tearaway
 * asks "Shake the PlayStation Vita system" and waits). 1 with acc filled when a range covers frame, 0 otherwise */
static inline int hle_motion_script(const char *q, uint64_t frame, float acc[3]) {
	while (q && *q) {
		char *e; unsigned long a = strtoul(q, &e, 10), z = a; q = e;
		float v[3] = {0, 0, -1.0f}; unsigned long per = 0;
		if (*q == '-') { z = strtoul(q + 1, &e, 10); q = e; }
		if (*q == ':') { for (int i = 0; i < 3; i++) { v[i] = strtof(q + 1, &e); q = e; if (*q != ',') break; } }
		if (*q == '/') { per = strtoul(q + 1, &e, 10); q = e; }
		if (frame >= a && frame <= z) {
			const float sg = per && ((frame - a) / per) & 1 ? -1.0f : 1.0f;
			acc[0] = v[0] * sg; acc[1] = v[1] * sg; acc[2] = v[2];
			return 1;
		}
		if (*q == ';') q++; else break;
	}
	return 0;
}

/* (AUTO-01 TW-30) the scripted touch of a headless run, RECOMP_TOUCH="from-to:x,y[@b];...": an entry touches the front
 * panel (1920x1088), or with @b the back panel, in its own coordinates (0..1919 x 108..889, sceTouchGetPanelInfo).
 * Tearaway's first level asks "Tap & hold the rear touch pad"; the script reached the front only. 1 with x, y when an
 * entry for this port (0 front, 1 back) covers frame, the first such entry */
static inline int hle_touch_script(const char *q, uint32_t port, uint64_t frame, int *x, int *y) {
	while (q && *q) {
		char *e; unsigned long a = strtoul(q, &e, 10), z = a, px = 0, py = 0; uint32_t pp = 0; q = e;
		if (*q == '-') { z = strtoul(q + 1, &e, 10); q = e; }
		if (*q == ':') { px = strtoul(q + 1, &e, 10); q = e; }
		if (*q == ',') { py = strtoul(q + 1, &e, 10); q = e; }
		if (*q == '@') { pp = q[1] == 'b' || q[1] == '1'; q += q[1] ? 2 : 1; }
		if (pp == port && frame >= a && frame <= z) { *x = (int)px; *y = (int)py; return 1; }
		if (*q == ';') q++; else break;
	}
	return 0;
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
/* NGS-PLAY-01: sceNgsVoicePlay starts a voice from rest only. On a voice that plays -- also paused, or in its key-off
 * release -- it changes nothing and returns SCE_NGS_ERROR (Vita3K's VoiceScheduler::play: not AVAILABLE, not played).
 * Medieval Defenders calls Play on its streamed ATRAC9 music voice every system update; restarting it each time
 * (buffer 0, byte 0, fresh decoder) played the same first 512 decoded frames over and over: a loud ~94 Hz buzz.
 * 0 and *playing set: started (the caller resets the voice); HLE_NGS_ERROR: left as it was */
#define HLE_NGS_ERROR 0x804a0001u
static inline uint32_t hle_ngs_voice_play(int *playing) {
	if (*playing) return HLE_NGS_ERROR;
	*playing = 1; return 0;
}
/* which state that Play checks: a voice that plays data (a player) is playing until it ends or stops; a voice the
 * runtime runs from its creation so that it mixes (master, reverb, bus) is AVAILABLE to the game until the game's own
 * first Play -- that one succeeds, as on the console. Checking the runtime's running flag for those refused FMOD's
 * first Play on its master voice (SCE_NGS_ERROR), and one title's audio start-up failed into a crash report. */
static inline uint32_t hle_ngs_voice_play_state(int player_voice, int *playing, int *started) {
	return hle_ngs_voice_play(player_voice ? playing : started);
}
/* (PTW-NGS) SceNgsPlayerParams, 0x54 bytes (Vita3K ngs/modules/player.h): descriptor, four buffers of 12 bytes from +8
 * {data, bytes, s16 loops, s16 next}, +56 frequency, +60 scalar, +64 lead-in samples, +68 sample limit, +72 start bytes,
 * +76 channels, +77 channel map[2], +79 type, +80 reserved, +81 s8 start buffer. Nativ read the start buffer at +80:
 * Tearaway's FMOD voices start at buffer 1 (0 is empty), began on the empty one and ended at once -- no sound at all.
 * The voice's first buffer: 0..3, else 0 */
#define HLE_NGS_PLAYER_START_BUFFER 81
static inline uint32_t hle_ngs_player_start(int8_t sb) { return sb >= 0 && sb < 4 ? (uint32_t)sb : 0; }
/* a player buffer without data (or less than one frame): as the DSP (Vita3K's player), the voice goes on with the buffer
 * it links to when that one has data -- Tearaway's first voice starts on an empty buffer 0 linking to 1 -- else its
 * data ends. The buffer to go on with, or -1 */
static inline int hle_ngs_player_skip_empty(int cur, int16_t next, uint32_t next_data, uint32_t next_bytes, uint32_t frame_bytes) {
	return next >= 0 && next < 4 && next != cur && next_data && next_bytes >= frame_bytes ? next : -1;
}
/* (PTW-CAM) the camera's own frame rate (Vita3K camera.cpp Camera::read): a frame every 1/fps s. A read before the next one
 * waits for it (SceCameraRead.mode SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_ON 0), or with WAIT_NEXTFRAME_OFF (1) returns at once with
 * SCE_CAMERA_STATUS_IS_ALREADY_READ (1) and the last frame's number and timestamp, its buffers untouched. Nativ gave a new frame
 * on every read: a game reading faster than the camera saw a new picture each time, as fast as it read. (Tearaway's flickering
 * touch button was tested against this and is not caused by it.) The microseconds until the next frame (0: one is due) */
#define HLE_CAMERA_READ_WAIT_OFF 1
#define HLE_CAMERA_STATUS_ALREADY_READ 1
static inline uint64_t hle_camera_frame_us(uint32_t fps) { return 1000000u / (fps ? fps : 30); }
static inline uint64_t hle_camera_wait_us(uint64_t now, uint64_t next) {
	return next > now + 1 ? next - now : 0;
}
/* a frame taken at now: its timestamp in *last, the next one due an interval later (a reader late by more than a frame starts the
 * grid again at now, no burst of stale frames) */
static inline void hle_camera_take(uint64_t now, uint64_t *next, uint64_t *last, uint64_t interval) {
	if (!*next || now > *next + interval) *next = now;
	*last = *next; *next += interval;
}
/* (PTW-VIS) a draw's visibility counter. sceGxmSetVisibilityBuffer takes an immediate context only (Vita3K SceGxm.cpp:
 * a deferred one is SCE_GXM_ERROR_INVALID_VALUE); a deferred context sets the test's enable, index and op, and its command
 * list's draws count into the buffer of the immediate context that executes the list. Resolved at record time from the
 * draw's own context, a deferred draw had no buffer and was never counted: Tearaway's job workers draw a touch button
 * through such a list behind an INCREMENT test, read 0 samples, take it as hidden every other frame, and it flickered.
 * hle_vis_record: at the draw, 1 with *addr / *flags (bit 0 the SET op) when it carries a counter -- the address, or for a
 * draw being recorded without a buffer of its own the offset 4*index and HLE_VIS_DEFERRED; hle_vis_execute: at
 * ExecuteCommandList, a deferred counter against the executing context's buffer (none: not counted) */
#define HLE_VIS_DEFERRED 0x80u
static inline int hle_vis_record(int on, uint32_t own_buf, uint32_t index, int set, int recording, uint32_t *addr, uint8_t *flags) {
	if (on && own_buf) { *addr = own_buf + 4u * index; *flags = set ? 1 : 0; return 1; }
	if (on && recording) { *addr = 4u * index; *flags = (uint8_t)(HLE_VIS_DEFERRED | (set ? 1 : 0)); return 1; }
	return 0;
}
static inline void hle_vis_execute(uint32_t exec_buf, uint32_t *addr, uint8_t *flags) {
	if (!(*flags & HLE_VIS_DEFERRED)) return;
	*addr = exec_buf ? exec_buf + *addr : 0; *flags = exec_buf ? (uint8_t)(*flags & 1) : 0;
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
