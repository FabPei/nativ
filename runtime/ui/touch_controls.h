/*
 * touch_controls.h - on-screen buttons for playing by touch alone (UI-01n): where they sit, what a finger on them presses,
 * and when they show. One layout for window.c (which turns fingers into the Vita's buttons and sticks) and overlay.cpp
 * (which draws them), both from the window's size in points, so a finger always presses what is drawn under it.
 *
 * Layout (landscape, the Vita's own arrangement): L and R in the top corners; the D-pad on the left above the left stick;
 * the face buttons on the right above the right stick; Select and Start at the bottom centre. The unit is a fraction of the
 * window's short side (times "osb_size"), never under 44 points (a finger's target), and capped so the columns fit the
 * height. Fingers that land elsewhere stay the Vita's front touch screen.
 *
 * Setting "osb": "auto" (default: they show once a finger touches the screen, and hide when a controller or the keyboard
 * is used), "on" (always), "off" (never).
 * Plain C, header-only: runtime/tests/touch_controls_test.c checks it.
 */
#ifndef RECOMP_TOUCH_CONTROLS_H
#define RECOMP_TOUCH_CONTROLS_H

#include <math.h>
#include <string.h>

/* the Vita's button bits (sceCtrl's SCE_CTRL_*: hardware values, the same as window.c's) */
enum { OSB_BIT_SELECT = 0x1, OSB_BIT_START = 0x8, OSB_BIT_UP = 0x10, OSB_BIT_RIGHT = 0x20, OSB_BIT_DOWN = 0x40, OSB_BIT_LEFT = 0x80,
       OSB_BIT_L = 0x100, OSB_BIT_R = 0x200, OSB_BIT_TRIANGLE = 0x1000, OSB_BIT_CIRCLE = 0x2000, OSB_BIT_CROSS = 0x4000, OSB_BIT_SQUARE = 0x8000 };

enum { OSB_DPAD, OSB_TRIANGLE, OSB_CIRCLE, OSB_CROSS, OSB_SQUARE, OSB_L, OSB_R, OSB_SELECT, OSB_START, OSB_LSTICK, OSB_RSTICK, OSB_N };
enum { OSB_OFF, OSB_AUTO, OSB_ON };

typedef struct {
	float x, y, w, h;   /* the box, window points */
	int round;          /* drawn and hit as a circle (the box's inscribed one) */
	unsigned bit;       /* the button it presses (0: the D-pad, by where the finger is; the sticks) */
	const char *label;  /* Latin-1 */
} OsbCtrl;
typedef struct { OsbCtrl c[OSB_N]; float unit; } OsbLayout;

static inline float osb_minf(float a, float b) { return a < b ? a : b; }
static inline float osb_maxf(float a, float b) { return a > b ? a : b; }

/* the layout for a window of w x h points; size: "osb_size" (0.75..1.5, 1 the default) */
static inline void osb_layout(OsbLayout *L, float w, float h, float size) {
	if (!(size >= 0.75f)) size = size > 0 ? 0.75f : 1.0f;
	if (size > 1.5f) size = 1.5f;
	float u = osb_minf(w, h) * 0.085f * size;
	u = osb_maxf(u, 44.0f);
	u = osb_minf(u, osb_minf(h / 9.0f, w / 14.0f));   /* the columns fit: margin, L, gap, D-pad (3), gap, stick (3), margin */
	const float m = u * 0.4f;
	memset(L, 0, sizeof *L);
	L->unit = u;
	OsbCtrl *c = L->c;
	c[OSB_L] = (OsbCtrl){m, m, 2.0f * u, u, 0, OSB_BIT_L, "L"};
	c[OSB_R] = (OsbCtrl){w - m - 2.0f * u, m, 2.0f * u, u, 0, OSB_BIT_R, "R"};
	/* the sticks: bottom corners, 3 units across */
	c[OSB_LSTICK] = (OsbCtrl){m, h - m - 3.0f * u, 3.0f * u, 3.0f * u, 1, 0, ""};
	c[OSB_RSTICK] = (OsbCtrl){w - m - 3.0f * u, h - m - 3.0f * u, 3.0f * u, 3.0f * u, 1, 0, ""};
	/* the D-pad and the face buttons above them, half a unit apart */
	const float cy = h - m - 3.0f * u - 0.5f * u - 1.5f * u;
	c[OSB_DPAD] = (OsbCtrl){m, cy - 1.5f * u, 3.0f * u, 3.0f * u, 0, 0, ""};
	const float fx = w - m - 1.5f * u;   /* the face diamond's centre */
	c[OSB_TRIANGLE] = (OsbCtrl){fx - 0.5f * u, cy - 1.5f * u, u, u, 1, OSB_BIT_TRIANGLE, "Triangle"};
	c[OSB_CROSS]    = (OsbCtrl){fx - 0.5f * u, cy + 0.5f * u, u, u, 1, OSB_BIT_CROSS, "Cross"};
	c[OSB_SQUARE]   = (OsbCtrl){fx - 1.5f * u, cy - 0.5f * u, u, u, 1, OSB_BIT_SQUARE, "Square"};
	c[OSB_CIRCLE]   = (OsbCtrl){fx + 0.5f * u, cy - 0.5f * u, u, u, 1, OSB_BIT_CIRCLE, "Circle"};
	/* Select and Start: pills at the bottom centre */
	c[OSB_SELECT] = (OsbCtrl){w * 0.5f - 2.1f * u, h - m - 0.8f * u, 2.0f * u, 0.8f * u, 0, OSB_BIT_SELECT, "Select"};
	c[OSB_START]  = (OsbCtrl){w * 0.5f + 0.1f * u, h - m - 0.8f * u, 2.0f * u, 0.8f * u, 0, OSB_BIT_START, "Start"};
}

static inline int osb_inside(const OsbCtrl *c, float x, float y, float grow) {
	if (c->round) {
		const float r = c->w * 0.5f + grow, dx = x - (c->x + c->w * 0.5f), dy = y - (c->y + c->h * 0.5f);
		return dx * dx + dy * dy <= r * r;
	}
	return x >= c->x - grow && x < c->x + c->w + grow && y >= c->y - grow && y < c->y + c->h + grow;
}

/* the control under a finger at x, y (window points), -1 for none: the face buttons first (they are small; a quarter unit
 * of slack each, not reaching a neighbour), then the rest */
static inline int osb_hit(const OsbLayout *L, float x, float y) {
	for (int i = OSB_TRIANGLE; i <= OSB_SQUARE; i++) if (osb_inside(&L->c[i], x, y, L->unit * 0.2f)) return i;
	for (int i = 0; i < OSB_N; i++) if (i < OSB_TRIANGLE || i > OSB_SQUARE) if (osb_inside(&L->c[i], x, y, 0)) return i;
	return -1;
}

/* the buttons a finger on control i at x, y presses: the D-pad by direction from its centre (eight ways, nothing within a
 * third of a unit of the centre), a button its bit, a stick nothing */
static inline unsigned osb_bits(const OsbLayout *L, int i, float x, float y) {
	if (i < 0 || i >= OSB_N) return 0;
	const OsbCtrl *c = &L->c[i];
	if (i != OSB_DPAD) return c->bit;
	const float dx = x - (c->x + c->w * 0.5f), dy = y - (c->y + c->h * 0.5f);
	if (dx * dx + dy * dy < (L->unit / 3.0f) * (L->unit / 3.0f)) return 0;
	const float a = atan2f(-dy, dx) * 57.29578f;   /* degrees, 0 right, 90 up */
	unsigned b = 0;
	if (a > 22.5f && a < 157.5f) b |= OSB_BIT_UP;
	if (a < -22.5f && a > -157.5f) b |= OSB_BIT_DOWN;
	if (a > -67.5f && a < 67.5f) b |= OSB_BIT_RIGHT;
	if (a > 112.5f || a < -112.5f) b |= OSB_BIT_LEFT;
	return b;
}

/* a finger at x, y on stick control c: the stick's x, y (0..255, 128 centred), full tilt at the ring's edge (beyond it,
 * the same direction at full tilt) */
static inline void osb_stick(const OsbCtrl *c, float x, float y, unsigned char *sx, unsigned char *sy) {
	const float r = c->w * 0.5f;
	float dx = (x - (c->x + r)) / r, dy = (y - (c->y + r)) / r;
	const float d = sqrtf(dx * dx + dy * dy);
	if (d > 1.0f) { dx /= d; dy /= d; }
	int vx = (int)lroundf(128.0f + dx * 127.5f), vy = (int)lroundf(128.0f + dy * 127.5f);
	*sx = (unsigned char)(vx < 0 ? 0 : vx > 255 ? 255 : vx);
	*sy = (unsigned char)(vy < 0 ? 0 : vy > 255 ? 255 : vy);
}

/* the "osb" setting's value */
static inline int osb_mode_parse(const char *s) {
	if (s && !strcmp(s, "on")) return OSB_ON;
	if (s && !strcmp(s, "off")) return OSB_OFF;
	return OSB_AUTO;
}

/* shown now: "on" always, "off" never; "auto" while the last thing used was the touch screen (touch_last: 1 a finger
 * landed after the last controller / keyboard input) */
static inline int osb_visible(int mode, int touch_last) { return mode == OSB_ON || (mode == OSB_AUTO && touch_last); }

#endif
