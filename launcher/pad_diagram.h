/*
 * pad_diagram.h - the drawn controller of the launcher's Controls > Mapping (UI-02c): where each physical button sits on
 * the pad of each family (pad_family.h: PlayStation, Xbox, Steam Deck, Switch), in a box 1 wide and PAD_DIAG_H tall,
 * and which Vita buttons each physical one carries (the inverse of the pad_bind_* rows). A physical button with two Vita
 * buttons on it is a conflict (the game keeps one of them only); a Vita button on none is not on the controller at all.
 * Header-only, no ImGui: launcher/tests/pad_diagram_test.cpp checks it.
 */
#ifndef NATIV_PAD_DIAGRAM_H
#define NATIV_PAD_DIAGRAM_H
#include <string>
#include <vector>
#include "pad_family.h"

#define PAD_DIAG_H 0.62f

/* the physical buttons the mapping can use, by their settings names (window.c's pad_bind_* values) */
enum { PD_SOUTH, PD_EAST, PD_WEST, PD_NORTH, PD_START, PD_BACK, PD_UP, PD_DOWN, PD_LEFT, PD_RIGHT, PD_LSTICK, PD_RSTICK,
       PD_LB, PD_RB, PD_LT, PD_RT, PD_GUIDE, PD_N };   /* (PAD-MAP: the bumpers, the triggers and Guide too) */
static const char *const pd_names[PD_N] = {"south", "east", "west", "north", "start", "back", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "left_stick", "right_stick",
	"left_shoulder", "right_shoulder", "left_trigger", "right_trigger", "guide"};

struct PadSpot { float x, y, r; };   /* centre and radius, in the diagram box (x 0..1, y 0..PAD_DIAG_H) */

/* where the physical buttons sit, per family: PlayStation pads have the D-pad above the left stick and the sticks side by
 * side below; Xbox and Switch pads have the left stick above the D-pad and the right stick low on the right; the Deck has
 * the D-pad and the face buttons high, the sticks under them, Menu / View at the top inside */
static inline PadSpot pad_spot(int fam, int b) {
	const float face_r = 0.034f, dpad_r = 0.032f, stick_r = 0.055f, small_r = 0.026f;
	float fx = 0.76f, fy = 0.24f, dx = 0.24f, dy = 0.24f, lsx = 0.36f, lsy = 0.44f, rsx = 0.64f, rsy = 0.44f, sy = 0.15f, ssx = 0.06f;
	if (fam == PAD_FAM_XBOX || fam == PAD_FAM_SWITCH) { dx = 0.36f; dy = 0.42f; lsx = 0.24f; lsy = 0.24f; rsx = 0.64f; rsy = 0.42f; sy = 0.24f; }
	if (fam == PAD_FAM_DECK) { fx = 0.84f; fy = 0.22f; dx = 0.16f; dy = 0.22f; lsx = 0.30f; lsy = 0.44f; rsx = 0.70f; rsy = 0.44f; sy = 0.12f; ssx = 0.20f; }
	const float o = 0.068f;   /* the face diamond's and the D-pad's spread */
	switch (b) {
	case PD_SOUTH: return {fx, fy + o, face_r};
	case PD_EAST: return {fx + o, fy, face_r};
	case PD_WEST: return {fx - o, fy, face_r};
	case PD_NORTH: return {fx, fy - o, face_r};
	case PD_UP: return {dx, dy - o, dpad_r};
	case PD_DOWN: return {dx, dy + o, dpad_r};
	case PD_LEFT: return {dx - o, dy, dpad_r};
	case PD_RIGHT: return {dx + o, dy, dpad_r};
	case PD_LSTICK: return {lsx, lsy, stick_r};
	case PD_RSTICK: return {rsx, rsy, stick_r};
	case PD_BACK: return {0.5f - ssx, sy, small_r};
	case PD_START: return {0.5f + ssx, sy, small_r};
	/* along the top edge, outside in: trigger, bumper | bumper, trigger; Guide in the middle (a PlayStation pad's below
	 * Share / Options, an Xbox / Switch pad's above View / Menu, the Deck's under its menu buttons) */
	case PD_LT: return {0.17f, 0.036f, small_r};
	case PD_LB: return {0.28f, 0.036f, small_r};
	case PD_RB: return {0.72f, 0.036f, small_r};
	case PD_RT: return {0.83f, 0.036f, small_r};
	case PD_GUIDE: return {0.5f, fam == PAD_FAM_XBOX || fam == PAD_FAM_SWITCH ? 0.13f : fam == PAD_FAM_DECK ? 0.30f : 0.26f, small_r};
	default: return {0.5f, 0.3f, 0.0f};
	}
}

/* the physical button a binding value names (-1: none, "none" or an unknown name) */
static inline int pad_diag_index(const std::string &v) {
	for (int b = 0; b < PD_N; b++) if (v == pd_names[b]) return b;
	return -1;
}
/* the inverse: for each physical button, the Vita rows (indices into the binding rows) on it; values[i]: row i's value */
static inline std::vector<std::vector<int>> pad_diag_on(const std::vector<std::string> &values) {
	std::vector<std::vector<int>> on(PD_N);
	for (size_t i = 0; i < values.size(); i++) { const int b = pad_diag_index(values[i]); if (b >= 0) on[b].push_back((int)i); }
	return on;
}
/* the rows on no physical button */
static inline std::vector<int> pad_diag_unplaced(const std::vector<std::string> &values) {
	std::vector<int> v;
	for (size_t i = 0; i < values.size(); i++) if (pad_diag_index(values[i]) < 0) v.push_back((int)i);
	return v;
}
#endif
