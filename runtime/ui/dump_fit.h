/*
 * RECOMP_OVERLAY_DUMP_SIZE=WxH (window.c's overlay_dump_maybe): the in-game menu dumped as it looks in a window of
 * that size. The game's frame is fitted into W x H first -- aspect kept, centred, nearest neighbour, opaque black
 * bars -- the way the window shows it in the "fit" aspect, then the menu is drawn over it at W x H. Header-only and
 * free of SDL so runtime/tests/dump_fit_test.c can check it.
 */
#ifndef RECOMP_DUMP_FIT_H
#define RECOMP_DUMP_FIT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { DUMP_SIZE_MIN = 16, DUMP_SIZE_MAX = 8192 };

/* "WxH" (or "WXH"), each DUMP_SIZE_MIN..DUMP_SIZE_MAX, nothing after it: 1 and *w, *h set; else 0, outputs untouched */
static inline int dump_size_parse(const char *s, uint32_t *w, uint32_t *h) {
	if (!s || *s < '0' || *s > '9') return 0;
	char *e; unsigned long a = strtoul(s, &e, 10);
	if (*e != 'x' && *e != 'X') return 0;
	const char *t = e + 1;
	if (*t < '0' || *t > '9') return 0;
	unsigned long b = strtoul(t, &e, 10);
	if (*e || a < DUMP_SIZE_MIN || a > DUMP_SIZE_MAX || b < DUMP_SIZE_MIN || b > DUMP_SIZE_MAX) return 0;
	*w = (uint32_t)a; *h = (uint32_t)b;
	return 1;
}

/* where a w x h picture lands in W x H, aspect kept and centred */
static inline void dump_fit_rect(uint32_t w, uint32_t h, uint32_t W, uint32_t H, uint32_t *ox, uint32_t *oy, uint32_t *dw, uint32_t *dh) {
	if ((uint64_t)W * h <= (uint64_t)H * w) { *dw = W; *dh = (uint32_t)((uint64_t)h * W / w); }   /* the width limits */
	else { *dh = H; *dw = (uint32_t)((uint64_t)w * H / h); }                                       /* the height limits */
	if (*dw < 1) *dw = 1;
	if (*dh < 1) *dh = 1;
	*ox = (W - *dw) / 2; *oy = (H - *dh) / 2;
}

/* src: w x h RGBA, tightly packed; dst: W x H RGBA, every pixel written */
static inline void dump_fit(const uint8_t *src, uint32_t w, uint32_t h, uint8_t *dst, uint32_t W, uint32_t H) {
	uint32_t ox, oy, dw, dh;
	dump_fit_rect(w, h, W, H, &ox, &oy, &dw, &dh);
	static const uint8_t black[4] = {0, 0, 0, 255};
	for (uint32_t y = 0; y < H; y++) {
		uint8_t *row = dst + 4u * W * y;
		if (y < oy || y >= oy + dh) { for (uint32_t x = 0; x < W; x++) memcpy(row + 4u * x, black, 4); continue; }
		const uint8_t *srow = src + 4u * w * (uint32_t)((uint64_t)(y - oy) * h / dh);
		for (uint32_t x = 0; x < W; x++) {
			if (x < ox || x >= ox + dw) memcpy(row + 4u * x, black, 4);
			else memcpy(row + 4u * x, srow + 4u * (uint32_t)((uint64_t)(x - ox) * w / dw), 4);
		}
	}
}
#endif
