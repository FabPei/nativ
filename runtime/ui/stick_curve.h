/*
 * stick_curve.h - a controller stick's two axes as the Vita's stick bytes, with the player's sensitivity and dead zone
 * (UI-04b; settings stick_l_sens / stick_l_dead, stick_r_sens / stick_r_dead -- the launcher's and the F10 menu's
 * Controls). The Vita reads a stick as two bytes, 0..255 with 128 the centre; SDL gives an axis as -32768..32767.
 *
 *   dead  a radial dead zone, a fraction of full tilt (0 none .. 0.9): a stick nearer its centre than that is centred,
 *         and the travel beyond it is stretched over the whole range (no jump at its edge);
 *   sens  a gain on the tilt beyond the dead zone (1 as it is): above 1 full tilt comes sooner, below 1 never quite.
 *
 * The result is clamped to 0..255 per axis. sens 1 and dead 0 (the defaults) give exactly what the runtime always did,
 * (axis + 32768) >> 8 -- byte for byte (runtime/tests/stick_curve_test.c checks every axis value). Header-only C.
 */
#ifndef RECOMP_STICK_CURVE_H
#define RECOMP_STICK_CURVE_H
#include <math.h>
#include <stdint.h>

static inline uint8_t stick_byte_raw(int axis) { return (uint8_t)((axis + 32768) >> 8); }

static inline void stick_curve(int ax, int ay, float sens, float dead, uint8_t *bx, uint8_t *by) {
	if (sens == 1.0f && dead <= 0.0f) { *bx = stick_byte_raw(ax); *by = stick_byte_raw(ay); return; }   /* (the defaults: as always) */
	if (dead < 0.0f) dead = 0.0f;
	if (dead > 0.9f) dead = 0.9f;
	if (!(sens > 0.0f)) sens = 1.0f;
	/* the tilt as the byte scale sees it: -128 .. +127.996, centre 0 (so 128 + f is the default's byte before its floor) */
	const float fx = (ax + 32768) / 256.0f - 128.0f, fy = (ay + 32768) / 256.0f - 128.0f;
	const float m = sqrtf(fx * fx + fy * fy) / 128.0f;   /* 0 .. ~1.41 (a square gate's corner) */
	float k = 0.0f;
	if (m > dead) k = ((m - dead) / (1.0f - dead)) * sens / m;   /* the new tilt over the old, the direction kept */
	float ox = 128.0f + fx * k, oy = 128.0f + fy * k;
	ox = ox < 0.0f ? 0.0f : ox > 255.0f ? 255.0f : ox;
	oy = oy < 0.0f ? 0.0f : oy > 255.0f ? 255.0f : oy;
	*bx = (uint8_t)floorf(ox); *by = (uint8_t)floorf(oy);
}
#endif
