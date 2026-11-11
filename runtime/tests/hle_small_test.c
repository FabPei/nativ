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
	/* SceMotionSensorState: 64 bytes, accelerometer +0, gyro +12, timestamp +36, counter +40, hostTimestamp +48 */
	{ uint8_t r[64]; memset(r, 0xee, sizeof r); float acc[3] = {0, 0, -1.0f}, gy[3] = {0.5f, 0, 0}, f;
	  hle_motion_sensor_record(r, acc, gy, 1234u, 7u, 0x123456789ull);
	  uint32_t u; uint64_t q; int ok = HLE_MOTION_SENSOR_SIZE == 64;
	  memcpy(&f, r + 8, 4); ok &= f == -1.0f; memcpy(&f, r + 12, 4); ok &= f == 0.5f;
	  memcpy(&u, r + 36, 4); ok &= u == 1234u; memcpy(&u, r + 40, 4); ok &= u == 7u; memcpy(&q, r + 48, 8); ok &= q == 0x123456789ull;
	  ok &= r[24] == 0 && r[44] == 0 && r[63] == 0;
	  check("motion: SceMotionSensorState record (accel z -1 g, gyro, timestamps, counter; reserved 0)", ok); }
	/* SceCtrlData: 32-byte records, n samples back to back (an 8-sample read fills exactly 0x100 bytes) */
	check("ctrl: SceCtrlData is 32 bytes", HLE_CTRL_DATA_SIZE == 32);
	check("ctrl: sample 1 follows sample 0 at +0x20", hle_ctrl_data_addr(0x1000, 1) == 0x1020);
	check("ctrl: 8 samples end at +0x100 (no write past an 8-record buffer)", hle_ctrl_data_addr(0x1000, 7) + HLE_CTRL_DATA_SIZE == 0x1100);
	/* the "2" reads: the shoulders are L1/R1, never LTRIGGER/RTRIGGER */
	check("ctrl v2: L (0x100) is L1 (0x400)", hle_ctrl_v2_buttons(0x100) == 0x400);
	check("ctrl v2: R (0x200) is R1 (0x800)", hle_ctrl_v2_buttons(0x200) == 0x800);
	check("ctrl v2: both shoulders + cross", hle_ctrl_v2_buttons(0x4300) == 0x4c00);
	check("ctrl v2: every other button unchanged", hle_ctrl_v2_buttons(0xf0f9) == 0xf0f9);
	check("ctrl v2: L1/R1 already set stay set", hle_ctrl_v2_buttons(0xc00) == 0xc00);
	check("model: PS TV is SCE_KERNEL_MODEL_VITATV 0x20000", hle_kernel_model(1) == 0x20000u);
	/* touch samples: one per vblank edge, stamped with the edge (Vita3K touch_get) */
	check("touch: first Read gives one report", hle_touch_read_count(0, 100, 16) == 1);
	check("touch: Read again on the same edge gives one (the waited-for edge)", hle_touch_read_count(100, 100, 16) == 1);
	check("touch: Read 3 edges later gives 3", hle_touch_read_count(100, 103, 16) == 3);
	check("touch: Read 40 edges later is capped at nBufs 16", hle_touch_read_count(100, 140, 16) == 16);
	check("touch: nBufs 0 gives none", hle_touch_read_count(100, 103, 0) == 0);
	check("touch: reports oldest first, the last one the current edge", hle_touch_report_edge(103, 3, 0) == 101 && hle_touch_report_edge(103, 3, 2) == 103);
	/* the game's drain loop: peek(1); while (peek.edge != last read) { n = Read(16); last = report[n-1].edge; peek } --
	 * on one edge it ends after one Read (before: a stamp per call, never equal) */
	{ uint64_t edge = 500, prev = 0, last = 0; int reads = 0;
	  for (int guard = 0; guard < 100; guard++) {
		const uint64_t peek = hle_touch_report_edge(edge, 1, 0);
		if (peek == last) break;
		const uint32_t n = hle_touch_read_count(prev, edge, 16); prev = edge; reads++;
		last = hle_touch_report_edge(edge, n, n - 1);
	  }
	  check("touch: a drain-until-Peek loop ends after one Read", reads == 1 && last == edge); }

	{ int playing = 0;   /* NGS-PLAY-01: Play every update must not restart the voice */
	  check("ngs: Play on a voice at rest starts it (0, playing)", hle_ngs_voice_play(&playing) == 0 && playing == 1);
	  int restarted = 0; for (int u = 0; u < 100; u++) restarted += hle_ngs_voice_play(&playing) == 0;
	  check("ngs: Play on a playing voice, 100 updates: never restarted, SCE_NGS_ERROR", restarted == 0 && hle_ngs_voice_play(&playing) == 0x804a0001u && playing == 1);
	  playing = 0;   /* finished, keyed off without release, or killed */
	  check("ngs: once it stopped, Play starts it again", hle_ngs_voice_play(&playing) == 0 && playing == 1); }
	{ int running = 1, started = 0;   /* a master / reverb / bus voice: the runtime runs it from its creation */
	  check("ngs: the game's first Play on a master voice the runtime runs: 0", hle_ngs_voice_play_state(0, &running, &started) == 0 && started == 1);
	  check("ngs: its second Play: SCE_NGS_ERROR", hle_ngs_voice_play_state(0, &running, &started) == 0x804a0001u);
	  int pl = 1, st = 0;
	  check("ngs: a player voice still checks its own playing (SCE_NGS_ERROR)", hle_ngs_voice_play_state(1, &pl, &st) == 0x804a0001u && st == 0); }
	{ /* (PTW-NGS) a player's start buffer and an empty buffer that links on, in the shape Tearaway's FMOD queues
	   * (synthetic: 0x54-byte params, buffer 0 empty -> 1 with data -> 2 empty, end) */
	  uint8_t pb[0x54] = {0}; pb[79] = 1; pb[80] = 0; pb[81] = 1;   /* type ADPCM, reserved 0, start buffer 1 */
	  check("ngs: the start buffer is the s8 at +81 (Tearaway: 1)", hle_ngs_player_start((int8_t)pb[HLE_NGS_PLAYER_START_BUFFER]) == 1);
	  check("ngs: a start buffer out of 0..3 starts at 0", hle_ngs_player_start(-1) == 0 && hle_ngs_player_start(4) == 0 && hle_ngs_player_start(3) == 3);
	  check("ngs: an empty buffer 0 linking to 1 with 0x2170 bytes goes on with 1", hle_ngs_player_skip_empty(0, 1, 0x10000, 0x2170, 16) == 1);
	  check("ngs: an empty buffer whose next is -1, empty, too short or itself: the data ends",
	        hle_ngs_player_skip_empty(2, -1, 0, 0, 16) == -1 && hle_ngs_player_skip_empty(0, 1, 0x10000, 0, 16) == -1
	        && hle_ngs_player_skip_empty(0, 1, 0x10000, 8, 16) == -1 && hle_ngs_player_skip_empty(0, 1, 0, 0x2170, 16) == -1
	        && hle_ngs_player_skip_empty(1, 1, 0x10000, 0x2170, 16) == -1 && hle_ngs_player_skip_empty(0, 4, 0x10000, 0x2170, 16) == -1); }
	{ /* (PTW-CAM) a 30 fps camera read by a game at its own rate: one new frame per 33333 us, else ALREADY_READ */
	  uint64_t next = 0, last = 0, iv = hle_camera_frame_us(30); int fresh = 0, already = 0;
	  check("camera: 30 fps is a frame every 33333 us (0 fps: 30)", iv == 33333 && hle_camera_frame_us(0) == 33333 && hle_camera_frame_us(60) == 16666);
	  for (uint64_t t = 1000000; t < 1000000 + 1000000; t += 16667) {   /* a 60 Hz reader for one second */
	    if (next && hle_camera_wait_us(t, next)) already++; else { hle_camera_take(t, &next, &last, iv); fresh++; } }
	  check("camera: a 60 Hz reader gets 30 new frames a second, the other reads ALREADY_READ", fresh >= 29 && fresh <= 31 && already >= 29 && already <= 31);
	  check("camera: the frame taken carries its own time", last >= 1000000 && last < 2000000 && next == last + iv);
	  next = 5000000; hle_camera_take(9000000, &next, &last, iv);
	  check("camera: a reader late by seconds starts the grid again (no burst)", last == 9000000 && next == 9000000 + iv);
	  check("camera: a read 1 us before the frame is due waits for nothing", hle_camera_wait_us(999999, 1000000) == 0 && hle_camera_wait_us(900000, 1000000) == 100000); }
	{ /* (PTW-VIS) a deferred context's visibility test counts into the executing immediate context's buffer */
	  uint32_t a = 0; uint8_t f = 0;
	  check("vis: an immediate draw with the test on counts at its own buffer + 4*index", hle_vis_record(1, 0x1b88b000, 3, 0, 0, &a, &f) && a == 0x1b88b00c && f == 0);
	  a = 0; f = 0;
	  check("vis: a deferred draw (no buffer of its own, recording) keeps its counter for the execute", hle_vis_record(1, 0, 0, 0, 1, &a, &f) == 1);
	  hle_vis_execute(0x1b88b000, &a, &f);
	  check("vis: executed on the immediate context, it counts at that buffer (Tearaway: index 0, INCREMENT)", a == 0x1b88b000 && f == 0);
	  a = 0; f = 0; hle_vis_record(1, 0, 5, 1, 1, &a, &f); hle_vis_execute(0x1b889000, &a, &f);
	  check("vis: a deferred SET test keeps its op and index", a == 0x1b889014 && f == 1);
	  a = 0; f = 0; hle_vis_record(1, 0, 2, 0, 1, &a, &f); hle_vis_execute(0, &a, &f);
	  check("vis: executed where no buffer is set: not counted", a == 0 && f == 0);
	  a = 0; f = 0;
	  check("vis: test off, or no buffer and not recording: no counter", !hle_vis_record(0, 0x1b88b000, 1, 0, 0, &a, &f) && !hle_vis_record(1, 0, 1, 0, 0, &a, &f) && a == 0); }
	uint32_t len[HLE_AUDIO_PORTS] = {0};
	int ok = 1;
	for (int k = 0; k < HLE_AUDIO_PORTS; k++) { int s = hle_audio_port_free_slot(len); ok &= s == k; if (s >= 0) len[s] = 256; }
	check("audio: 8 opens take slots 0..7", ok);
	check("audio: a 9th open finds no slot (PORT_FULL)", hle_audio_port_free_slot(len) == -1);
	/* (AUTO-01 TW-30) RECOMP_TOUCH: front entries as before, @b entries on the back panel only */
	{ int x = -1, y = -1; const char *sc = "100-105:1280,770;200-300:960,500@b;250:10,20";
	  check("touch script: a front entry touches port 0", hle_touch_script(sc, 0, 103, &x, &y) && x == 1280 && y == 770);
	  check("touch script: a front entry leaves the back alone", !hle_touch_script(sc, 1, 103, &x, &y));
	  check("touch script: an @b entry touches port 1", hle_touch_script(sc, 1, 220, &x, &y) && x == 960 && y == 500);
	  check("touch script: an @b entry leaves the front alone", !hle_touch_script(sc, 0, 220, &x, &y));
	  check("touch script: front and back in one frame", hle_touch_script(sc, 0, 250, &x, &y) && x == 10 && y == 20 && hle_touch_script(sc, 1, 250, &x, &y) && x == 960);
	  check("touch script: outside every range, or none, nothing", !hle_touch_script(sc, 0, 99, &x, &y) && !hle_touch_script(sc, 1, 301, &x, &y) && !hle_touch_script(0, 0, 100, &x, &y)); }
	/* (AUTO-01 TW-27) RECOMP_MOTION: scripted accelerometer ranges, a /p shake flips x and y every p frames */
	{ float a[3] = {9, 9, 9}; const char *sc = "100-200:2.5,2.5,-1/4;300:0,-1,0";
	  check("motion script: before any range, nothing", !hle_motion_script(sc, 99, a) && a[0] == 9);
	  check("motion script: a shake's first p frames +x +y", hle_motion_script(sc, 103, a) && a[0] == 2.5f && a[1] == 2.5f && a[2] == -1.0f);
	  check("motion script: the next p frames -x -y, z kept", hle_motion_script(sc, 104, a) && a[0] == -2.5f && a[1] == -2.5f && a[2] == -1.0f);
	  check("motion script: a one-frame range without /p", hle_motion_script(sc, 300, a) && a[0] == 0 && a[1] == -1.0f && a[2] == 0);
	  check("motion script: between and after ranges, nothing", !hle_motion_script(sc, 250, a) && !hle_motion_script(sc, 301, a) && !hle_motion_script(0, 100, a)); }
	/* (AUTO-01 LBP-7) audio input: LittleBigPlanet opens a voice port (type 0, grain 256, 16 kHz) */
	check("audio in: a voice port, 256 samples at 16 kHz, opens", hle_audio_in_open_check(0, 256, 16000) == 0);
	check("audio in: raw ports at 16 and 48 kHz open", hle_audio_in_open_check(2, 256, 16000) == 0 && hle_audio_in_open_check(2, 768, 48000) == 0);
	check("audio in: a voice port at 48 kHz, type 1, grain 0: INVALID_PARAMETER",
	      hle_audio_in_open_check(0, 256, 48000) == HLE_AUDIO_IN_ERR_INVALID_PARAMETER && hle_audio_in_open_check(1, 256, 16000) == HLE_AUDIO_IN_ERR_INVALID_PARAMETER
	      && hle_audio_in_open_check(0, 0, 16000) == HLE_AUDIO_IN_ERR_INVALID_PARAMETER);
	check("audio in: one 256-sample grain at 16 kHz takes 16000 us", hle_audio_in_grain_us(256, 16000) == 16000u);
	int s = hle_audio_port_slot(len, 3);   /* release handle 3 = slot 2 */
	check("audio: handle 3 names slot 2", s == 2);
	if (s >= 0) len[s] = 0;
	check("audio: after release the next open reuses slot 2", hle_audio_port_free_slot(len) == 2);
	check("audio: releasing it again is INVALID_PORT (no slot)", hle_audio_port_slot(len, 3) == -1);
	check("audio: handle 0 and 9 name no port", hle_audio_port_slot(len, 0) == -1 && hle_audio_port_slot(len, 9) == -1);
	/* the open/release cycle a game runs per scene: 100 cycles never run out */
	ok = 1; for (int k = 0; k < 100; k++) { int f = hle_audio_port_free_slot(len); ok &= f >= 0; if (f >= 0) { len[f] = 256; len[f] = 0; } }
	check("audio: 100 open/release cycles never run out", ok);
	/* sceAudioOutOutput returns the samples registered (Vita3K: the port's length), not 0: a sound thread that checks
	 * for its grain of 512 quit after the first buffer */
	check("audio: Output with a buffer returns the port length (512)", hle_audio_output_result(512, 0x81000000u) == 512);
	check("audio: Output with a buffer returns the port length (256)", hle_audio_output_result(256, 0x81000000u) == 256);
	check("audio: Output with a NULL buffer (drain wait) returns 0", hle_audio_output_result(512, 0) == 0);

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
