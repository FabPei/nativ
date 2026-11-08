/*
 * HLE-LIB-06: the pure parts of the last offline system functions the analysed games import (the ledger's hle-coverage
 * inventory, section 6). Header-only, no guest memory, so runtime/tests/hle_offline_test.c checks the contracts alone.
 * Contracts from the vitasdk headers (livearea.h, libime.h, apputil.h, appmgr.h, rtc.h) and, where those are silent,
 * Vita3K (read for semantics: SceJpegUser.cpp sceJpegGetOutputInfo, motion_input.cpp RotateYaw, SceAppMgr.cpp
 * _sceAppMgrLoadExec's error codes); no code copied.
 *   - SceLiveAreaUtil: the host has no LiveArea screen. Sync replacements/updates are checked as the console checks
 *     them and kept as a per-frame revision table (an update counts the frame's revision up); nothing asynchronous is
 *     ever pending, so GetStatus is "no request" (0x80104001); a frame the game never wrote is "unavailable".
 *   - SceAppUtil: no background download ever ran (a zeroed status).
 *   - SceAppMgr LoadExec: the console replaces the process by another SELF in app0. One pack is one recompiled program,
 *     so an existing SELF fails loudly (NOEXEC); a missing one is the console's INVALID_SELF_PATH.
 *   - SceMotion RotateYaw: a turn about the vertical axis applied to the reported orientation (Vita3K: the rotation
 *     (0, 0, -sin(r/2), cos(r/2)) multiplied on the left); sceMotionReset clears it.
 *   - SceJpeg GetOutputInfo: the frame header (SOF) gives size and sampling; sizes and pitches are the decoders' own
 *     layout (jpeg.c), so what GetOutputInfo promises is what sceJpegDecodeMJpeg* then write.
 *   - SceIme (the in-game keyboard, not the dialog): open/close state and the events sceImeUpdate hands the game's
 *     handler -- OPEN once, then the answer: UPDATE_TEXT + PRESS_ENTER (OK) or PRESS_CLOSE (cancel).
 */
#ifndef RECOMP_HLE_OFFLINE_H
#define RECOMP_HLE_OFFLINE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/* ---- SceLiveAreaUtil */
#define HLE_LA_ERR_NO_REQUEST   0x80104001u   /* no asynchronous operation pending */
#define HLE_LA_ERR_PARAM        0x80104002u
#define HLE_LA_ERR_PARSE        0x80104004u   /* the frame XML could not be parsed */
#define HLE_LA_ERR_XML_TOO_LONG 0x80104009u   /* over 10239 bytes */
#define HLE_LA_ERR_UNAVAILABLE  0x8010400Au   /* the LiveArea object or frame is unavailable */
#define HLE_LA_FRAMES 32
typedef struct { char id[32]; uint64_t rev; } HleLaFrame;
typedef struct { HleLaFrame f[HLE_LA_FRAMES]; int n; } HleLiveArea;
static inline int hle_la_path_ok(const char *p) { return p && strlen(p) <= 255; }
/* the frame id of a frame XML document (<frame id="x"> or id='x'): 1 and id, else 0 */
static inline int hle_la_frame_id(const char *xml, size_t len, char *id, size_t n) {
	for (size_t i = 0; i + 6 <= len; i++) {
		if (memcmp(xml + i, "<frame", 6) || (i + 6 < len && xml[i + 6] != ' ' && xml[i + 6] != '\t' && xml[i + 6] != '\n' && xml[i + 6] != '\r')) continue;
		for (size_t j = i + 6; j + 3 < len && xml[j] != '>'; j++) {
			if (memcmp(xml + j, "id", 2) || (xml[j - 1] != ' ' && xml[j - 1] != '\t' && xml[j - 1] != '\n' && xml[j - 1] != '\r')) continue;
			size_t k = j + 2; while (k < len && (xml[k] == ' ' || xml[k] == '\t')) k++;
			if (k >= len || xml[k] != '=') continue;
			k++; while (k < len && (xml[k] == ' ' || xml[k] == '\t')) k++;
			if (k >= len || (xml[k] != '"' && xml[k] != '\'')) return 0;
			const char q = xml[k++]; size_t m = 0;
			while (k < len && xml[k] != q) { if (m + 1 >= n) return 0; id[m++] = xml[k++]; }
			if (k >= len || !m) return 0;
			id[m] = 0; return 1;
		}
		return 0;
	}
	return 0;
}
static inline HleLaFrame *hle_la_find(HleLiveArea *la, const char *id) {
	for (int i = 0; i < la->n; i++) if (!strcmp(la->f[i].id, id)) return &la->f[i];
	return 0;
}
/* sceLiveAreaReplaceAllSync(contents_path): every frame is the new data's -- the table starts over */
static inline uint32_t hle_la_replace_all(HleLiveArea *la, const char *path) {
	if (!hle_la_path_ok(path)) return HLE_LA_ERR_PARAM;
	la->n = 0; return 0;
}
/* sceLiveAreaUpdateFrameSync(format_version, frame_xml, frame_xml_length, contents_path, target_type) */
static inline uint32_t hle_la_update_frame(HleLiveArea *la, const char *fmt, const char *xml, int32_t len, const char *path, uint32_t target) {
	if (!fmt || strcmp(fmt, "01.00") || !xml || !hle_la_path_ok(path) || target > 1) return HLE_LA_ERR_PARAM;
	const size_t n = len < 0 ? strlen(xml) : (size_t)len;
	if (n > 10239) return HLE_LA_ERR_XML_TOO_LONG;
	char id[32];
	if (!hle_la_frame_id(xml, n, id, sizeof id)) return HLE_LA_ERR_PARSE;
	HleLaFrame *f = hle_la_find(la, id);
	if (!f && la->n < HLE_LA_FRAMES) { f = &la->f[la->n++]; memcpy(f->id, id, sizeof id); f->rev = 0; }
	if (f) f->rev++;
	return 0;
}
/* sceLiveAreaGetFrameRevision(frame_id, revision) */
static inline uint32_t hle_la_frame_revision(HleLiveArea *la, const char *id, int have_out, uint64_t *rev) {
	if (!id || !*id || !have_out) return HLE_LA_ERR_PARAM;
	const HleLaFrame *f = hle_la_find(la, id);
	if (!f) return HLE_LA_ERR_UNAVAILABLE;
	*rev = f->rev; return 0;
}

/* ---- SceAppUtil sceAppUtilBgdlGetStatus(SceAppUtilBgdlStatus *): 0x2c bytes, type 0 and no add-ons either way */
#define HLE_APPUTIL_ERR_PARAMETER 0x80100600u
#define HLE_BGDL_STATUS_SIZE 0x2c

/* ---- SceAppMgr sceAppMgrLoadExec(path, argv, optParam) */
#define HLE_APPMGR_ERR_NULL_POINTER     0x80802016u
#define HLE_APPMGR_ERR_INVALID          0x8080201Au
#define HLE_APPMGR_ERR_TOO_LONG_ARGV    0x8080201Du
#define HLE_APPMGR_ERR_INVALID_SELF_PATH 0x8080201Eu
#define HLE_APPMGR_ERR_NOEXEC           0x8080201Fu
static inline uint32_t hle_loadexec_check(const char *path, int has_opt, size_t argv_bytes, int self_exists) {
	if (!path) return HLE_APPMGR_ERR_NULL_POINTER;
	if (has_opt) return HLE_APPMGR_ERR_INVALID;
	if (strncmp(path, "app0:", 5) || !self_exists) return HLE_APPMGR_ERR_INVALID_SELF_PATH;
	if (argv_bytes > 1024) return HLE_APPMGR_ERR_TOO_LONG_ARGV;
	return HLE_APPMGR_ERR_NOEXEC;   /* the console would now replace the process: the host has this one program only */
}

/* ---- SceMotion: quaternions as the Vita stores them, x, y, z, w */
static inline void hle_quat_mul(const float a[4], const float b[4], float o[4]) {
	const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	const float n = sqrtf(x * x + y * y + z * z + w * w);
	if (n < 1e-6f) { o[0] = o[1] = o[2] = 0; o[3] = 1; return; }
	o[0] = x / n; o[1] = y / n; o[2] = z / n; o[3] = w / n;
}
/* sceMotionRotateYaw(radians): the accumulated turn yaw (x, y, z, w) turned by radians more */
static inline void hle_motion_rotate_yaw(float yaw[4], float radians) {
	const float r[4] = {0, 0, -sinf(radians / 2), cosf(radians / 2)};
	float o[4]; hle_quat_mul(r, yaw, o); memcpy(yaw, o, sizeof o);
}

/* ---- SceJpeg */
#define HLE_JPEG_ERR_DECODE       0x80650009u
#define HLE_JPEG_ERR_PTR          0x80650010u
#define HLE_JPEG_ERR_COLORSPACE   0x80650013u   /* UNSUPPORT_COLORSPACE */
#define HLE_JPEG_ERR_SAMPLING     0x80650016u   /* UNSUPPORT_SAMPLING */
#define HLE_JPEG_ERR_COLOR_FORMAT 0x80650050u   /* INVALID_COLOR_FORMAT */
#define HLE_JPEG_ERR_DOWNSCALE    0x80650060u   /* UNSUPPORT_DOWNSCALE */
#define HLE_JPEG_CS_GRAYSCALE 0x10101u
#define HLE_JPEG_CS_YUV422    0x20201u
#define HLE_JPEG_CS_YUV420    0x20202u
static inline int hle_align_up(int v, int a) { return (v + a - 1) / a * a; }
/* the decoded size and plane pitches for a decode mode (bits 0-2 the DHT/sampling mode: 0, 1 standard 4:2:0 / 4:2:2,
 * 2, 3 any sampling; bits 4-6 the downscale 1/2, 1/4, 1/8): the luma pitch is the image rounded up to the sampling's
 * MCU and divided by the downscale, the chroma planes half of it per subsampled direction, 16-aligned in the standard
 * modes (jpeg.c's decoders write this layout) */
typedef struct { int w, h, cw, ch, py, pyh, pc, pch, hs, vs; } HleJpegLayout;
static inline void hle_jpeg_layout(int w, int h, int sampling, int mode, HleJpegLayout *l) {
	const int ratio = (mode & 0x70) == 0x10 ? 2 : (mode & 0x70) == 0x20 ? 4 : (mode & 0x70) == 0x40 ? 8 : 1;
	const int standard = (mode & 7) < 2;
	l->hs = (sampling >> 4) >= 2 ? 2 : 1; l->vs = (sampling & 15) >= 2 ? 2 : 1;
	const int bw = 8 * l->hs, bh = 8 * l->vs;
	l->w = (w + ratio - 1) / ratio; l->h = (h + ratio - 1) / ratio;
	l->py = hle_align_up(hle_align_up(w, bw) / ratio, bw); l->pyh = hle_align_up(hle_align_up(h, bh) / ratio, standard ? 8 : 1);
	l->pc = l->py / l->hs; l->pch = l->pyh / l->vs;
	if (standard && (l->hs == 2)) l->pc = hle_align_up(l->pc, 16);
	l->cw = (l->w + l->hs - 1) / l->hs; l->ch = (l->h + l->vs - 1) / l->vs;
}
/* the frame header of a JPEG (SOF0..SOF15 but DHT/JPG/DAC): 1 with width, height, components and component 1's
 * sampling factors (0x22 4:2:0, 0x21 4:2:2, 0x11 4:4:4), 0 when there is none before the scan or the data ends */
static inline int hle_jpeg_sof(const uint8_t *p, size_t n, int *w, int *h, int *ncomp, int *sampling) {
	if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 0;
	for (size_t i = 2; i + 1 < n;) {
		if (p[i] != 0xFF) return 0;
		const uint8_t m = p[i + 1];
		if (m == 0xFF) { i++; continue; }
		if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
		if (m == 0xD9 || m == 0xDA || i + 4 > n) return 0;
		const size_t len = (size_t)p[i + 2] << 8 | p[i + 3];
		if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
			if (len < 8 || i + 2 + len > n || i + 12 > n) return 0;
			*h = p[i + 5] << 8 | p[i + 6]; *w = p[i + 7] << 8 | p[i + 8]; *ncomp = p[i + 9];
			*sampling = *ncomp >= 1 ? p[i + 11] : 0x11;
			return *w > 0 && *h > 0 && *ncomp > 0;
		}
		i += 2 + len;
	}
	return 0;
}
/* SceJpegOutputInfo (0x34 bytes): color space, u16 width, u16 height, output size, temp buffer size, coef buffer
 * size, 4 pitches {x, y} */
typedef struct { uint32_t color_space; uint16_t width, height; uint32_t output_size, temp_size, coef_size; uint32_t pitch[4][2]; } HleJpegOutputInfo;
/* sceJpegGetOutputInfo(jpeg, size, format (-1 no colour conversion, 0 RGBA8888, 4 BGRA8888), decodeMode, out) */
static inline uint32_t hle_jpeg_output_info(const uint8_t *p, size_t n, int32_t format, int mode, HleJpegOutputInfo *o) {
	if (!p || !n || !o) return HLE_JPEG_ERR_PTR;
	if (format != -1 && format != 0 && format != 4) return HLE_JPEG_ERR_COLOR_FORMAT;
	int w, h, nc, s;
	if (!hle_jpeg_sof(p, n, &w, &h, &nc, &s)) return HLE_JPEG_ERR_DECODE;
	memset(o, 0, sizeof *o);
	o->color_space = nc == 1 ? HLE_JPEG_CS_GRAYSCALE : 0x20000u | (uint32_t)(s >> 4 & 15) << 8 | (uint32_t)(s & 15);
	const int standard = (mode & 7) < 2;
	if (standard && nc == 1) return HLE_JPEG_ERR_COLORSPACE;
	if (standard && o->color_space != HLE_JPEG_CS_YUV420 && o->color_space != HLE_JPEG_CS_YUV422) return HLE_JPEG_ERR_SAMPLING;
	if (!standard && format != -1) return HLE_JPEG_ERR_DOWNSCALE;
	HleJpegLayout l; hle_jpeg_layout(w, h, nc == 1 ? 0x11 : s, mode, &l);
	o->width = (uint16_t)l.w; o->height = (uint16_t)l.h;
	o->pitch[0][0] = (uint32_t)l.py; o->pitch[0][1] = (uint32_t)l.pyh;
	if (nc > 1) for (int k = 1; k < 3; k++) { o->pitch[k][0] = (uint32_t)l.pc; o->pitch[k][1] = (uint32_t)l.pch; }
	uint32_t total = 0; for (int k = 0; k < 4; k++) total += o->pitch[k][0] * o->pitch[k][1];
	if (format != -1) {
		o->pitch[0][0] *= 4; for (int k = 1; k < 4; k++) o->pitch[k][0] = o->pitch[k][1] = 0;
		o->temp_size = (uint32_t)hle_align_up((int)total, 256);
		o->output_size = (uint32_t)hle_align_up((int)(o->pitch[0][0] * o->pitch[0][1]), 256);
	} else o->output_size = (uint32_t)hle_align_up((int)total, 256);
	return 0;   /* coef_size 0: the host's decoders need no coefficient buffer */
}

/* ---- SceIme (libime.h) */
#define HLE_IME_ERR_ALREADY_OPENED   0x80100700u
#define HLE_IME_ERR_NOT_OPENED       0x80100701u
#define HLE_IME_ERR_INVALID_POINTER  0x80100702u
#define HLE_IME_ERR_INVALID_TYPE     0x80100714u
#define HLE_IME_ERR_INVALID_OPTION   0x80100715u
#define HLE_IME_ERR_INVALID_WORK     0x80100716u
#define HLE_IME_ERR_INVALID_HANDLER  0x80100718u
#define HLE_IME_ERR_INVALID_MAX_TEXT_LENGTH   0x80100719u
#define HLE_IME_ERR_INVALID_INPUT_TEXT_BUFFER 0x8010071au
#define HLE_IME_ERR_INVALID_ENTER_LABEL       0x8010071cu
#define HLE_IME_EVENT_OPEN        0
#define HLE_IME_EVENT_UPDATE_TEXT 1
#define HLE_IME_EVENT_PRESS_CLOSE 4
#define HLE_IME_EVENT_PRESS_ENTER 5
#define HLE_IME_MAX_TEXT_LENGTH 2048
#define HLE_IME_EVENT_SIZE 0x2c
/* the fields of a SceImeParam (0x40 bytes) the checks read: type +0x14, option +0x18, work +0x1c, arg +0x20,
 * handler +0x24, filter +0x28, initialText +0x2c, maxTextLength +0x30, inputTextBuffer +0x34, enterLabel +0x38 */
typedef struct { uint32_t type, option, work, handler, max_len, input_buf; uint8_t enter_label; } HleImeParam;
/* the console's order of these checks is not known; a game's valid parameters pass every order */
static inline uint32_t hle_ime_check(const HleImeParam *p) {
	if (!p) return HLE_IME_ERR_INVALID_POINTER;
	if (!p->handler) return HLE_IME_ERR_INVALID_HANDLER;
	if (!p->work) return HLE_IME_ERR_INVALID_WORK;
	if (!p->input_buf) return HLE_IME_ERR_INVALID_INPUT_TEXT_BUFFER;
	if (!p->max_len || p->max_len > HLE_IME_MAX_TEXT_LENGTH) return HLE_IME_ERR_INVALID_MAX_TEXT_LENGTH;
	if (p->type > 5) return HLE_IME_ERR_INVALID_TYPE;
	if (p->option & ~7u) return HLE_IME_ERR_INVALID_OPTION;
	if (p->enter_label > 3) return HLE_IME_ERR_INVALID_ENTER_LABEL;
	return 0;
}
typedef struct { int open, sent_open, done; } HleIme;
static inline uint32_t hle_ime_open(HleIme *s, const HleImeParam *p) {
	if (s->open) return HLE_IME_ERR_ALREADY_OPENED;
	const uint32_t e = hle_ime_check(p);
	if (e) return e;
	s->open = 1; s->sent_open = 0; s->done = 0; return 0;
}
static inline uint32_t hle_ime_close(HleIme *s) { if (!s->open) return HLE_IME_ERR_NOT_OPENED; s->open = 0; return 0; }
/* the events one sceImeUpdate hands the handler (answer: -1 none yet, 0 cancelled, 1 entered): OPEN on the first
 * update, the answer from the next one on, once. The caller answers NOT_OPENED while closed */
static inline int hle_ime_events(HleIme *s, int answer, uint32_t ids[3]) {
	int n = 0;
	if (!s->open) return 0;
	if (!s->sent_open) { ids[n++] = HLE_IME_EVENT_OPEN; s->sent_open = 1; return n; }
	if (!s->done && answer >= 0) {
		if (answer) { ids[n++] = HLE_IME_EVENT_UPDATE_TEXT; ids[n++] = HLE_IME_EVENT_PRESS_ENTER; }
		else ids[n++] = HLE_IME_EVENT_PRESS_CLOSE;
		s->done = 1;
	}
	return n;
}
/* a SceImeEventData (0x2c bytes: u32 id, then the 0x28-byte union) in little-endian bytes. UPDATE_TEXT carries a
 * SceImeEditText: preeditIndex, preeditLength 0, caretIndex, str (the game's inputTextBuffer), editIndex 0,
 * editLengthChange; the caret and preedit index stand after the text (len units). OPEN carries the keyboard's
 * rectangle: the host's card is not drawn into the game's screen, so it is empty */
static inline void hle_put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static inline void hle_ime_event_bytes(uint8_t b[HLE_IME_EVENT_SIZE], uint32_t id, uint32_t len, uint32_t str, int32_t change) {
	memset(b, 0, HLE_IME_EVENT_SIZE);
	hle_put32(b, id);
	if (id == HLE_IME_EVENT_UPDATE_TEXT) { hle_put32(b + 4, len); hle_put32(b + 12, len); hle_put32(b + 16, str); hle_put32(b + 24, (uint32_t)change); }
}
#endif
