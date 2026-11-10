/* dump_fit.h: the size parser and the fit-scaler behind RECOMP_OVERLAY_DUMP_SIZE (window.c's menu dump at any
 * window size). No SDL, no game: build from this folder with
 *     cc -O1 -g -Wall -I ../ui dump_fit_test.c -o /tmp/dump_fit_test && /tmp/dump_fit_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dump_fit.h"

static int fails, oks;
#define CHECK(c, ...) do { if (c) oks++; else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t px(const uint8_t *img, uint32_t w, uint32_t x, uint32_t y) {
	const uint8_t *p = img + 4u * (w * y + x); return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(void) {
	uint32_t w, h;
	/* the parser: "WxH" (or "WXH"), each 16..8192; anything else refused and the outputs untouched */
	CHECK(dump_size_parse("1920x1080", &w, &h) && w == 1920 && h == 1080, "1920x1080 -> %ux%u", w, h);
	CHECK(dump_size_parse("960X544", &w, &h) && w == 960 && h == 544, "960X544");
	w = h = 7;
	CHECK(!dump_size_parse(NULL, &w, &h) && w == 7 && h == 7, "NULL refused");
	CHECK(!dump_size_parse("", &w, &h), "empty refused");
	CHECK(!dump_size_parse("1920", &w, &h), "no x refused");
	CHECK(!dump_size_parse("1920x", &w, &h), "no height refused");
	CHECK(!dump_size_parse("8x8", &w, &h), "below 16 refused");
	CHECK(!dump_size_parse("9000x100", &w, &h), "above 8192 refused");
	CHECK(!dump_size_parse("100x100junk", &w, &h), "trailing text refused");
	CHECK(!dump_size_parse("-100x100", &w, &h) && w == 7, "negative refused");

	/* the fitted rectangle: aspect kept, centred */
	uint32_t ox, oy, dw, dh;
	dump_fit_rect(960, 544, 1920, 1088, &ox, &oy, &dw, &dh);
	CHECK(ox == 0 && oy == 0 && dw == 1920 && dh == 1088, "exact 2x: %u %u %u %u", ox, oy, dw, dh);
	dump_fit_rect(960, 544, 1920, 1080, &ox, &oy, &dw, &dh);   /* height limits: 1080/544 */
	CHECK(dh == 1080 && dw == 1905 && ox == 7 && oy == 0, "1920x1080: %u %u %u %u", ox, oy, dw, dh);
	dump_fit_rect(960, 544, 1280, 800, &ox, &oy, &dw, &dh);    /* width limits: 1280/960 */
	CHECK(dw == 1280 && dh == 725 && ox == 0 && oy == 37, "1280x800: %u %u %u %u", ox, oy, dw, dh);

	/* the scaler: a 2x2 source (four colours) into 4x4 is each colour as a 2x2 block; into 6x4 the same with
	 * one black column each side */
	uint8_t src[16];
	const uint32_t c[4] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu};
	for (int i = 0; i < 4; i++) memcpy(src + 4 * i, &c[i], 4);
	uint8_t *d = malloc(4 * 6 * 4);
	dump_fit(src, 2, 2, d, 4, 4);
	CHECK(px(d, 4, 0, 0) == c[0] && px(d, 4, 1, 1) == c[0] && px(d, 4, 2, 0) == c[1] && px(d, 4, 3, 1) == c[1] &&
	      px(d, 4, 0, 2) == c[2] && px(d, 4, 1, 3) == c[2] && px(d, 4, 2, 2) == c[3] && px(d, 4, 3, 3) == c[3], "2x2 -> 4x4 blocks");
	memset(d, 0x55, 4 * 6 * 4);
	dump_fit(src, 2, 2, d, 6, 4);
	CHECK(px(d, 6, 0, 0) == 0xff000000u && px(d, 6, 5, 3) == 0xff000000u, "6x4: bars are opaque black (%08x %08x)", px(d, 6, 0, 0), px(d, 6, 5, 3));
	CHECK(px(d, 6, 1, 0) == c[0] && px(d, 6, 4, 3) == c[3], "6x4: picture between the bars");
	/* same size: a plain copy */
	dump_fit(src, 2, 2, d, 2, 2);
	CHECK(!memcmp(d, src, 16), "same size copies");
	free(d);

	if (fails) { printf("%d failed, %d ok\n", fails, oks); return 1; }
	printf("all passed (%d ok)\n", oks);
	return 0;
}
