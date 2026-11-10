/* touch_controls_test.c - runtime/ui/touch_controls.h (UI-01n): the on-screen buttons' layout at the window sizes that
 * matter (the Deck, 720p, 1080p, a small window, a portrait one), what a finger presses, the sticks, the setting.
 * Build:
 *   cc -std=c99 -Wall -I ../ui touch_controls_test.c -lm -o /tmp/touch_controls_test && /tmp/touch_controls_test */
#include <stdio.h>
#include "touch_controls.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int overlap(const OsbCtrl *a, const OsbCtrl *b) {
	const float e = 0.01f;   /* (the face diamond's boxes meet corner to corner: rounding) */
	return a->x + e < b->x + b->w && b->x + e < a->x + a->w && a->y + e < b->y + b->h && b->y + e < a->y + a->h;
}
static float cxof(const OsbCtrl *c) { return c->x + c->w * 0.5f; }
static float cyof(const OsbCtrl *c) { return c->y + c->h * 0.5f; }

static void layout_checks(float w, float h, float size) {
	OsbLayout L; osb_layout(&L, w, h, size);
	CHECK(L.unit >= 44.0f || L.unit >= h / 9.0f - 0.01f);   /* a finger's target, unless the window is too small for it */
	for (int i = 0; i < OSB_N; i++) {
		const OsbCtrl *c = &L.c[i];
		CHECK(c->x >= 0 && c->y >= 0 && c->x + c->w <= w + 0.01f && c->y + c->h <= h + 0.01f);   /* on screen */
		for (int j = i + 1; j < OSB_N; j++) if (overlap(c, &L.c[j])) { printf("  %gx%g: controls %d and %d overlap\n", w, h, i, j); CHECK(0); }
		/* each control's centre hits itself (the D-pad's centre is its dead zone, but still the D-pad) */
		CHECK(osb_hit(&L, cxof(c), cyof(c)) == i);
	}
	/* the left column on the left half, the right one on the right half */
	CHECK(cxof(&L.c[OSB_DPAD]) < w * 0.5f && cxof(&L.c[OSB_LSTICK]) < w * 0.5f && cxof(&L.c[OSB_L]) < w * 0.5f);
	CHECK(cxof(&L.c[OSB_CROSS]) > w * 0.5f && cxof(&L.c[OSB_RSTICK]) > w * 0.5f && cxof(&L.c[OSB_R]) > w * 0.5f);
	/* the screen's middle is the game's (front touch) */
	CHECK(osb_hit(&L, w * 0.5f, h * 0.4f) == -1);
}

/* the rule of UI-01n, as it was (no pad argument): the fail-before of UI-02e */
static int osb_visible_ui01n(int mode, int touch_last) { return mode == OSB_ON || (mode == OSB_AUTO && touch_last); }

int main(void) {
	CHECK(OSB_BIT_CROSS == 0x4000 && OSB_BIT_START == 0x8 && OSB_BIT_L == 0x100 && OSB_BIT_UP == 0x10);   /* (sceCtrl's values) */
	const float sizes[][2] = {{1280, 800}, {1280, 720}, {1920, 1080}, {960, 544}, {640, 400}, {2560, 1600}, {800, 1280}};
	for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
		for (float s = 0.75f; s <= 1.5f; s += 0.25f) layout_checks(sizes[i][0], sizes[i][1], s);

	OsbLayout L; osb_layout(&L, 1280, 800, 1.0f);
	CHECK(L.unit > 67.9f && L.unit < 68.1f);   /* the Deck: 800 * 0.085 */
	/* the face buttons in the Vita's diamond: Triangle up, Cross down, Square left, Circle right */
	CHECK(cyof(&L.c[OSB_TRIANGLE]) < cyof(&L.c[OSB_CROSS]) && cxof(&L.c[OSB_SQUARE]) < cxof(&L.c[OSB_CIRCLE]));
	CHECK(osb_bits(&L, OSB_CROSS, cxof(&L.c[OSB_CROSS]), cyof(&L.c[OSB_CROSS])) == OSB_BIT_CROSS);
	CHECK(osb_bits(&L, OSB_START, 0, 0) == OSB_BIT_START);
	CHECK(osb_bits(&L, OSB_LSTICK, 0, 0) == 0);
	CHECK(osb_bits(&L, -1, 0, 0) == 0);
	/* a little outside a face button still presses it */
	{	const OsbCtrl *c = &L.c[OSB_CIRCLE];
		CHECK(osb_hit(&L, c->x + c->w + L.unit * 0.1f, cyof(c)) == OSB_CIRCLE); }
	/* the D-pad: eight ways, a dead centre */
	{	const OsbCtrl *d = &L.c[OSB_DPAD]; const float x = cxof(d), y = cyof(d), u = L.unit;
		CHECK(osb_bits(&L, OSB_DPAD, x, y) == 0);
		CHECK(osb_bits(&L, OSB_DPAD, x, y - u) == OSB_BIT_UP);
		CHECK(osb_bits(&L, OSB_DPAD, x, y + u) == OSB_BIT_DOWN);
		CHECK(osb_bits(&L, OSB_DPAD, x - u, y) == OSB_BIT_LEFT);
		CHECK(osb_bits(&L, OSB_DPAD, x + u, y) == OSB_BIT_RIGHT);
		CHECK(osb_bits(&L, OSB_DPAD, x + u, y - u) == (OSB_BIT_UP | OSB_BIT_RIGHT));
		CHECK(osb_bits(&L, OSB_DPAD, x - u, y + u) == (OSB_BIT_DOWN | OSB_BIT_LEFT));
		CHECK(osb_bits(&L, OSB_DPAD, x + u, y - u * 0.2f) == OSB_BIT_RIGHT); }   /* (mostly right: right only) */
	/* the sticks */
	{	const OsbCtrl *s = &L.c[OSB_LSTICK]; unsigned char sx, sy; const float r = s->w * 0.5f;
		osb_stick(s, cxof(s), cyof(s), &sx, &sy); CHECK(sx == 128 && sy == 128);
		osb_stick(s, cxof(s) + r, cyof(s), &sx, &sy); CHECK(sx == 255 && sy == 128);
		osb_stick(s, cxof(s), cyof(s) - r, &sx, &sy); CHECK(sx == 128 && sy == 1);
		osb_stick(s, cxof(s) - 5 * r, cyof(s), &sx, &sy); CHECK(sx == 1 && sy == 128);   /* beyond the ring: full tilt */
		osb_stick(s, cxof(s) + r * 0.5f, cyof(s) + r * 0.5f, &sx, &sy); CHECK(sx > 185 && sx < 195 && sy > 185 && sy < 195); }
	/* size: bigger, clamped */
	{	OsbLayout B; osb_layout(&B, 1280, 800, 1.5f); CHECK(B.unit > L.unit && B.unit <= 800 / 9.0f + 0.01f);
		OsbLayout C; osb_layout(&C, 1280, 800, 9.0f); CHECK(C.unit == B.unit);
		OsbLayout D; osb_layout(&D, 1280, 800, 0.0f); CHECK(D.unit == L.unit); }   /* (unset: the default) */
	/* the setting and when they show */
	CHECK(osb_mode_parse("on") == OSB_ON && osb_mode_parse("off") == OSB_OFF && osb_mode_parse("auto") == OSB_AUTO);
	CHECK(osb_mode_parse(0) == OSB_AUTO && osb_mode_parse("junk") == OSB_AUTO);
	CHECK(osb_visible(OSB_ON, 0, 0) && !osb_visible(OSB_OFF, 1, 0) && osb_visible(OSB_AUTO, 1, 0) && !osb_visible(OSB_AUTO, 0, 0));
	/* (UI-02e) a touch with a controller connected (a Steam Deck): auto keeps them away -- before, "touch_last" alone
	 * showed them (the rule as it was, kept here as the fail-before: it says shown) */
	{	CHECK(osb_visible_ui01n(OSB_AUTO, 1) == 1);   /* (the old rule: a touch with a pad connected -> shown: the defect) */
		CHECK(!osb_visible(OSB_AUTO, 1, 1)); }
	CHECK(osb_visible(OSB_ON, 0, 1));    /* (on: shown with a pad too) */
	CHECK(!osb_visible(OSB_OFF, 1, 1));
	CHECK(osb_visible(OSB_AUTO, 1, 0));  /* (the pad unplugged, then a touch: shown again) */

	printf("touch_controls: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
