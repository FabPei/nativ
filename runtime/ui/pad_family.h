/*
 * pad_family.h - which controller is in use, and what its buttons are called (UI-01e). One table for the launcher's hints
 * and the in-game menu's foot, so a hint names the button printed on the pad in the player's hands.
 *
 * Family: from SDL's gamepad type, and the Steam Deck from Steam's own SteamDeck=1 (in Game Mode the Deck's controls
 * reach a program as Steam's virtual Xbox pad, so the type alone would say Xbox). Names: the button Dear ImGui's
 * navigation takes as "select" is the south face button (FaceDown) and "back" the east one (FaceRight) on every pad --
 * on a Switch pad the south button is printed B and the east one A, so there the hints read B to select, A to go back.
 * Shoulders: L1/R1 on PlayStation pads and the Deck, LB/RB on Xbox pads, L/R on Switch pads.
 * Latin-1 only (the in-game font's glyph range): PlayStation's symbols as words; the launcher draws them as shapes.
 * Plain C, header-only: runtime/tests/pad_family_test.c checks it.
 */
#ifndef RECOMP_PAD_FAMILY_H
#define RECOMP_PAD_FAMILY_H

enum { PAD_FAM_PS, PAD_FAM_XBOX, PAD_FAM_DECK, PAD_FAM_SWITCH, PAD_FAM_N };

typedef struct {
	const char *select;   /* south face button: Dear ImGui's activate */
	const char *back;     /* east face button: cancel / back */
	const char *west, *north;
	const char *l, *r;    /* the shoulder pair that steps sections */
	const char *menu;     /* Start / Options / Menu / + */
	const char *view;     /* Select / Share / View / - */
} PadNames;

/* the family of a pad of SDL gamepad type `type` (SDL_GamepadType's value); deck: Steam says this is a Steam Deck. With no
 * pad (type < 0) the PlayStation names: the Vita's own */
static inline int pad_family(int type, int deck) {
	if (deck) return PAD_FAM_DECK;
	switch (type) {
	case 2: case 3: return PAD_FAM_XBOX;                 /* SDL_GAMEPAD_TYPE_XBOX360, _XBOXONE */
	case 7: case 8: case 9: case 10: return PAD_FAM_SWITCH;   /* SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, _JOYCON_LEFT / _RIGHT / _PAIR */
	case 1: return PAD_FAM_XBOX;                         /* SDL_GAMEPAD_TYPE_STANDARD: a generic pad, laid out as an Xbox pad */
	default: return PAD_FAM_PS;                          /* PS3 / PS4 / PS5, unknown, none */
	}
}

static inline const PadNames *pad_names_of(int fam) {
	static const PadNames names[PAD_FAM_N] = {
		{"Cross", "Circle", "Square", "Triangle", "L1", "R1", "Options", "Share"},
		{"A", "B", "X", "Y", "LB", "RB", "Menu", "View"},
		{"A", "B", "X", "Y", "L1", "R1", "Menu", "View"},
		{"B", "A", "Y", "X", "L", "R", "+", "-"},
	};
	return &names[fam >= 0 && fam < PAD_FAM_N ? fam : PAD_FAM_PS];
}
#endif
