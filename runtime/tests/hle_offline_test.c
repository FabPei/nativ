/* hle_offline_test.c -- HLE-LIB-06: the contracts of hle_offline.h (see its header comment).
 * Fail-before: the helpers did not exist and the imports were placeholders ("not implemented -- returns 0"): LiveArea
 * GetStatus 0 read as "the pending operation succeeded", GetFrameRevision/Bgdl/GetOutputInfo wrote nothing, LoadExec
 * reported a successful launch that never happened, sceImeOpen "opened" a keyboard that never sent an event.
 *   cc -O1 -g -Wall -I ../core hle_offline_test.c -o /tmp/hle_offline_test -lm && /tmp/hle_offline_test
 */
#include <stdio.h>
#include "hle_offline.h"

static int fails;
static void check(const char *what, int ok) { printf("%-66s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }
static uint32_t le32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* a synthetic JPEG header: SOI, an APP0 segment, SOF0 (w x h, ncomp components, component 1 sampling), EOI */
static size_t mkjpeg(uint8_t *b, int w, int h, int ncomp, int sampling) {
	size_t n = 0;
	b[n++] = 0xFF; b[n++] = 0xD8;
	b[n++] = 0xFF; b[n++] = 0xE0; b[n++] = 0; b[n++] = 6; b[n++] = 'J'; b[n++] = 'F'; b[n++] = 'I'; b[n++] = 0;
	b[n++] = 0xFF; b[n++] = 0xC0; const int len = 8 + 3 * ncomp; b[n++] = (uint8_t)(len >> 8); b[n++] = (uint8_t)len;
	b[n++] = 8; b[n++] = (uint8_t)(h >> 8); b[n++] = (uint8_t)h; b[n++] = (uint8_t)(w >> 8); b[n++] = (uint8_t)w; b[n++] = (uint8_t)ncomp;
	for (int k = 0; k < ncomp; k++) { b[n++] = (uint8_t)(k + 1); b[n++] = (uint8_t)(k ? 0x11 : sampling); b[n++] = (uint8_t)(k ? 1 : 0); }
	b[n++] = 0xFF; b[n++] = 0xD9;
	return n;
}

int main(void) {
	/* ---- LiveArea */
	HleLiveArea la = {0}; uint64_t rev = 99;
	const char *xml2 = "<?xml version=\"1.0\"?><frame id=\"frame2\"><liveitem><text><str>x</str></text></liveitem></frame>";
	const char *xml4 = "<frame id='frame4' multi='o'><liveitem/></frame>";
	char longpath[300]; memset(longpath, 'a', 299); longpath[299] = 0;
	check("livearea: ReplaceAllSync(NULL) / 299-char path is PARAM", hle_la_replace_all(&la, 0) == HLE_LA_ERR_PARAM && hle_la_replace_all(&la, longpath) == HLE_LA_ERR_PARAM);
	check("livearea: ReplaceAllSync(app0:...) is 0", hle_la_replace_all(&la, "app0:sce_sys/livearea/contents") == 0);
	check("livearea: a frame never written is UNAVAILABLE", hle_la_frame_revision(&la, "frame2", 1, &rev) == HLE_LA_ERR_UNAVAILABLE);
	check("livearea: GetFrameRevision(\"\"/NULL/no out) is PARAM", hle_la_frame_revision(&la, "", 1, &rev) == HLE_LA_ERR_PARAM && hle_la_frame_revision(&la, 0, 1, &rev) == HLE_LA_ERR_PARAM && hle_la_frame_revision(&la, "frame2", 0, &rev) == HLE_LA_ERR_PARAM);
	check("livearea: UpdateFrameSync double-quoted id, length -1 is 0", hle_la_update_frame(&la, "01.00", xml2, -1, "savedata0:", 0) == 0);
	check("livearea: ... frame2 revision 1", hle_la_frame_revision(&la, "frame2", 1, &rev) == 0 && rev == 1);
	check("livearea: single-quoted id, explicit length, retail target", hle_la_update_frame(&la, "01.00", xml4, (int32_t)strlen(xml4), "app0:", 1) == 0);
	check("livearea: frame2 again -> revision 2, frame4 stays 1", hle_la_update_frame(&la, "01.00", xml2, -1, "app0:", 0) == 0 && hle_la_frame_revision(&la, "frame2", 1, &rev) == 0 && rev == 2 && hle_la_frame_revision(&la, "frame4", 1, &rev) == 0 && rev == 1);
	check("livearea: format \"01.01\" / NULL xml / target 2 are PARAM", hle_la_update_frame(&la, "01.01", xml2, -1, "app0:", 0) == HLE_LA_ERR_PARAM && hle_la_update_frame(&la, "01.00", 0, -1, "app0:", 0) == HLE_LA_ERR_PARAM && hle_la_update_frame(&la, "01.00", xml2, -1, "app0:", 2) == HLE_LA_ERR_PARAM);
	check("livearea: XML of 10240 bytes is XML_TOO_LONG", hle_la_update_frame(&la, "01.00", xml2, 10240, "app0:", 0) == HLE_LA_ERR_XML_TOO_LONG);
	check("livearea: no <frame id> / <framex id> is PARSE", hle_la_update_frame(&la, "01.00", "<livearea/>", -1, "app0:", 0) == HLE_LA_ERR_PARSE && hle_la_update_frame(&la, "01.00", "<framex id=\"a\"/>", -1, "app0:", 0) == HLE_LA_ERR_PARSE);
	check("livearea: ReplaceAllSync starts the table over", hle_la_replace_all(&la, "app0:x") == 0 && hle_la_frame_revision(&la, "frame4", 1, &rev) == HLE_LA_ERR_UNAVAILABLE);

	/* ---- LoadExec */
	check("loadexec: NULL path is NULL_POINTER", hle_loadexec_check(0, 0, 0, 0) == HLE_APPMGR_ERR_NULL_POINTER);
	check("loadexec: an option parameter is INVALID", hle_loadexec_check("app0:a.self", 1, 0, 1) == HLE_APPMGR_ERR_INVALID);
	check("loadexec: a SELF outside app0 / missing is INVALID_SELF_PATH", hle_loadexec_check("ux0:a.self", 0, 0, 1) == HLE_APPMGR_ERR_INVALID_SELF_PATH && hle_loadexec_check("app0:a.self", 0, 0, 0) == HLE_APPMGR_ERR_INVALID_SELF_PATH);
	check("loadexec: argv over 1024 bytes is TOO_LONG_ARGV", hle_loadexec_check("app0:a.self", 0, 1025, 1) == HLE_APPMGR_ERR_TOO_LONG_ARGV);
	check("loadexec: an existing SELF is NOEXEC (one program per pack)", hle_loadexec_check("app0:a.self", 0, 10, 1) == HLE_APPMGR_ERR_NOEXEC);

	/* ---- Motion RotateYaw */
	float yaw[4] = {0, 0, 0, 1};
	hle_motion_rotate_yaw(yaw, (float)M_PI / 2);
	check("motion: +90 deg -> (0, 0, -sin 45, cos 45)", near(yaw[0], 0) && near(yaw[1], 0) && near(yaw[2], -0.70710678f) && near(yaw[3], 0.70710678f));
	hle_motion_rotate_yaw(yaw, (float)M_PI / 2);
	check("motion: two +90 deg turns are one 180 deg turn (0, 0, -1, 0)", near(yaw[2], -1) && near(yaw[3], 0));
	hle_motion_rotate_yaw(yaw, -(float)M_PI);
	check("motion: -180 deg turns back to identity", near(fabsf(yaw[3]), 1) && near(yaw[2], 0));
	float q[4] = {0.70710678f, 0, 0, 0.70710678f}, o[4], y90[4] = {0, 0, 0, 1};   /* the device pitched 90 deg about x */
	hle_motion_rotate_yaw(y90, (float)M_PI / 2); hle_quat_mul(y90, q, o);
	check("motion: yaw on the left keeps a pitched pose unit length", near(o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3], 1));
	check("motion: ... and equals (0.5, -0.5, -0.5, 0.5)", near(o[0], 0.5f) && near(o[1], -0.5f) && near(o[2], -0.5f) && near(o[3], 0.5f));

	/* ---- Jpeg GetOutputInfo */
	uint8_t j[64]; HleJpegOutputInfo oi; int w, h, nc, s;
	size_t jn = mkjpeg(j, 960, 544, 3, 0x22);
	check("jpeg: SOF0 read: 960x544, 3 components, 4:2:0", hle_jpeg_sof(j, jn, &w, &h, &nc, &s) && w == 960 && h == 544 && nc == 3 && s == 0x22);
	check("jpeg: RGBA 960x544 4:2:0 -> 0", hle_jpeg_output_info(j, jn, 0, 0, &oi) == 0);
	check("jpeg: ... colour space YUV420, 960x544", oi.color_space == HLE_JPEG_CS_YUV420 && oi.width == 960 && oi.height == 544);
	check("jpeg: ... pitch0 {3840, 544}, no chroma pitches", oi.pitch[0][0] == 3840 && oi.pitch[0][1] == 544 && !oi.pitch[1][0] && !oi.pitch[2][0]);
	check("jpeg: ... output 2088960, temp 783360 (Y + 2 x 480x272)", oi.output_size == 2088960 && oi.temp_size == 783360);
	check("jpeg: YCbCr (-1) 1/4 scale: 240x136, pitches 240/128 x 136/68", hle_jpeg_output_info(j, jn, -1, 0x20, &oi) == 0 && oi.width == 240 && oi.height == 136 && oi.pitch[0][0] == 240 && oi.pitch[0][1] == 136 && oi.pitch[1][0] == 128 && oi.pitch[1][1] == 68 && oi.temp_size == 0);
	check("jpeg: ... output = align(240x136 + 2 x 128x68, 256) = 50176", oi.output_size == 50176);
	jn = mkjpeg(j, 100, 50, 3, 0x21);
	check("jpeg: 4:2:2 100x50 -> YUV422, pitch 112 x 56", hle_jpeg_output_info(j, jn, -1, 0, &oi) == 0 && oi.color_space == HLE_JPEG_CS_YUV422 && oi.pitch[0][0] == 112 && oi.pitch[0][1] == 56);
	jn = mkjpeg(j, 64, 64, 3, 0x11);
	check("jpeg: 4:4:4 in a standard mode is UNSUPPORT_SAMPLING", hle_jpeg_output_info(j, jn, -1, 0, &oi) == HLE_JPEG_ERR_SAMPLING && oi.color_space == 0x20101u);
	check("jpeg: 4:4:4 in mode 2 (any sampling), no CSC -> 0", hle_jpeg_output_info(j, jn, -1, 2, &oi) == 0);
	check("jpeg: mode 2 with RGBA is UNSUPPORT_DOWNSCALE", hle_jpeg_output_info(j, jn, 0, 2, &oi) == HLE_JPEG_ERR_DOWNSCALE);
	jn = mkjpeg(j, 64, 64, 1, 0x11);
	check("jpeg: greyscale standard is UNSUPPORT_COLORSPACE", hle_jpeg_output_info(j, jn, -1, 0, &oi) == HLE_JPEG_ERR_COLORSPACE);
	check("jpeg: greyscale mode 3 -> GRAYSCALE, one plane", hle_jpeg_output_info(j, jn, -1, 3, &oi) == 0 && oi.color_space == HLE_JPEG_CS_GRAYSCALE && oi.pitch[0][0] == 64 && !oi.pitch[1][0]);
	check("jpeg: format 1 is INVALID_COLOR_FORMAT; NULL / size 0 is INVALID_POINTER", hle_jpeg_output_info(j, jn, 1, 0, &oi) == HLE_JPEG_ERR_COLOR_FORMAT && hle_jpeg_output_info(0, 10, 0, 0, &oi) == HLE_JPEG_ERR_PTR && hle_jpeg_output_info(j, 0, 0, 0, &oi) == HLE_JPEG_ERR_PTR);
	check("jpeg: no SOI / no SOF before EOI is DECODE_ERROR", hle_jpeg_output_info((const uint8_t *)"abcdefgh", 8, 0, 0, &oi) == HLE_JPEG_ERR_DECODE && hle_jpeg_output_info((const uint8_t *)"\xFF\xD8\xFF\xD9", 4, 0, 0, &oi) == HLE_JPEG_ERR_DECODE);
	HleJpegLayout l; hle_jpeg_layout(960, 544, 0x22, 0, &l);
	check("jpeg: the decoders' layout: RGBA bytes = output size", (uint32_t)(4 * l.py * l.pyh) == 2088960u);

	/* ---- IME (in-game keyboard) */
	HleIme ime = {0}; uint32_t ids[3]; int n;
	HleImeParam p = {0, 0, 0x81000000u, 0x81001000u, 64, 0x81002000u, 0};
	HleImeParam bad = p;
	check("ime: Close before Open is NOT_OPENED", hle_ime_close(&ime) == HLE_IME_ERR_NOT_OPENED);
	check("ime: no events while closed", hle_ime_events(&ime, 1, ids) == 0);
	bad.handler = 0; check("ime: no handler is INVALID_HANDLER", hle_ime_open(&ime, &bad) == HLE_IME_ERR_INVALID_HANDLER && !ime.open);
	bad = p; bad.input_buf = 0; check("ime: no text buffer is INVALID_INPUT_TEXT_BUFFER", hle_ime_open(&ime, &bad) == HLE_IME_ERR_INVALID_INPUT_TEXT_BUFFER);
	bad = p; bad.max_len = 2049; check("ime: max length 2049 / 0 is INVALID_MAX_TEXT_LENGTH", hle_ime_open(&ime, &bad) == HLE_IME_ERR_INVALID_MAX_TEXT_LENGTH && (bad.max_len = 0, hle_ime_open(&ime, &bad)) == HLE_IME_ERR_INVALID_MAX_TEXT_LENGTH);
	bad = p; bad.work = 0; check("ime: no work buffer is INVALID_WORK", hle_ime_open(&ime, &bad) == HLE_IME_ERR_INVALID_WORK);
	bad = p; bad.type = 6; check("ime: type 6 / option 8 / enter label 4 rejected", hle_ime_open(&ime, &bad) == HLE_IME_ERR_INVALID_TYPE && (bad = p, bad.option = 8, hle_ime_open(&ime, &bad)) == HLE_IME_ERR_INVALID_OPTION && (bad = p, bad.enter_label = 4, hle_ime_open(&ime, &bad)) == HLE_IME_ERR_INVALID_ENTER_LABEL);
	check("ime: NULL param is INVALID_POINTER", hle_ime_open(&ime, 0) == HLE_IME_ERR_INVALID_POINTER);
	check("ime: valid Open -> 0, a second is ALREADY_OPENED", hle_ime_open(&ime, &p) == 0 && hle_ime_open(&ime, &p) == HLE_IME_ERR_ALREADY_OPENED);
	n = hle_ime_events(&ime, 1, ids);
	check("ime: the first Update sends OPEN only (even with an answer ready)", n == 1 && ids[0] == HLE_IME_EVENT_OPEN);
	check("ime: no answer yet -> no event", hle_ime_events(&ime, -1, ids) == 0);
	n = hle_ime_events(&ime, 1, ids);
	check("ime: entered -> UPDATE_TEXT then PRESS_ENTER", n == 2 && ids[0] == HLE_IME_EVENT_UPDATE_TEXT && ids[1] == HLE_IME_EVENT_PRESS_ENTER);
	check("ime: the answer is delivered once", hle_ime_events(&ime, 1, ids) == 0);
	check("ime: Close -> 0, then Close is NOT_OPENED", hle_ime_close(&ime) == 0 && hle_ime_close(&ime) == HLE_IME_ERR_NOT_OPENED);
	hle_ime_open(&ime, &p); hle_ime_events(&ime, -1, ids); n = hle_ime_events(&ime, 0, ids);
	check("ime: reopened, cancelled -> PRESS_CLOSE", n == 1 && ids[0] == HLE_IME_EVENT_PRESS_CLOSE);
	uint8_t ev[HLE_IME_EVENT_SIZE];
	hle_ime_event_bytes(ev, HLE_IME_EVENT_UPDATE_TEXT, 5, 0x81002000u, 3);
	check("ime: UPDATE_TEXT bytes: id 1, preedit/caret 5, str, change 3", le32(ev) == 1 && le32(ev + 4) == 5 && le32(ev + 8) == 0 && le32(ev + 12) == 5 && le32(ev + 16) == 0x81002000u && le32(ev + 20) == 0 && le32(ev + 24) == 3);
	hle_ime_event_bytes(ev, HLE_IME_EVENT_OPEN, 5, 0x81002000u, 3);
	check("ime: OPEN bytes: id 0, an empty rectangle", le32(ev) == 0 && le32(ev + 4) == 0 && le32(ev + 12) == 0 && le32(ev + 16) == 0);

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
