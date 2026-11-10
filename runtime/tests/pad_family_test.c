/* pad_family_test.c - runtime/ui/pad_family.h (UI-01e): the controller family from SDL's gamepad type and the Deck, and the
 * names its hints use. SDL's own constants, so the header's numbers can't drift from SDL's enum.
 * Build (no SDL library needed, its header only):
 *   cc -std=c99 -Wall -I ../ui -I <SDL3 include dir> pad_family_test.c -o /tmp/pad_family_test && /tmp/pad_family_test */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_gamepad.h>
#include "pad_family.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define SAME(a, b) CHECK(strcmp((a), (b)) == 0)

int main(void) {
	/* families */
	CHECK(pad_family(SDL_GAMEPAD_TYPE_XBOX360, 0) == PAD_FAM_XBOX);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_XBOXONE, 0) == PAD_FAM_XBOX);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_STANDARD, 0) == PAD_FAM_XBOX);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_PS3, 0) == PAD_FAM_PS);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_PS4, 0) == PAD_FAM_PS);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_PS5, 0) == PAD_FAM_PS);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0) == PAD_FAM_SWITCH);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT, 0) == PAD_FAM_SWITCH);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT, 0) == PAD_FAM_SWITCH);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR, 0) == PAD_FAM_SWITCH);
	CHECK(pad_family(SDL_GAMEPAD_TYPE_UNKNOWN, 0) == PAD_FAM_PS);
	CHECK(pad_family(-1, 0) == PAD_FAM_PS);   /* no pad: the Vita's own names */
	/* the Deck in Game Mode: Steam's virtual Xbox pad, but its buttons are the Deck's */
	CHECK(pad_family(SDL_GAMEPAD_TYPE_XBOX360, 1) == PAD_FAM_DECK);
	CHECK(pad_family(-1, 1) == PAD_FAM_DECK);
	/* names: select is the south button, back the east one */
	SAME(pad_names_of(PAD_FAM_PS)->select, "Cross"); SAME(pad_names_of(PAD_FAM_PS)->back, "Circle");
	SAME(pad_names_of(PAD_FAM_PS)->l, "L1"); SAME(pad_names_of(PAD_FAM_PS)->menu, "Options");
	SAME(pad_names_of(PAD_FAM_XBOX)->select, "A"); SAME(pad_names_of(PAD_FAM_XBOX)->back, "B");
	SAME(pad_names_of(PAD_FAM_XBOX)->l, "LB"); SAME(pad_names_of(PAD_FAM_XBOX)->r, "RB");
	SAME(pad_names_of(PAD_FAM_DECK)->select, "A"); SAME(pad_names_of(PAD_FAM_DECK)->back, "B");
	SAME(pad_names_of(PAD_FAM_DECK)->l, "L1"); SAME(pad_names_of(PAD_FAM_DECK)->r, "R1");
	SAME(pad_names_of(PAD_FAM_SWITCH)->select, "B"); SAME(pad_names_of(PAD_FAM_SWITCH)->back, "A");   /* (printed B south, A east) */
	SAME(pad_names_of(PAD_FAM_SWITCH)->l, "L"); SAME(pad_names_of(PAD_FAM_SWITCH)->menu, "+");
	SAME(pad_names_of(99)->select, "Cross");   /* (out of range: the PlayStation names) */
	/* every name Latin-1 (the in-game font's range) */
	for (int f = 0; f < PAD_FAM_N; f++) {
		const PadNames *n = pad_names_of(f);
		const char *all[] = {n->select, n->back, n->west, n->north, n->l, n->r, n->menu, n->view};
		for (int i = 0; i < 8; i++) for (const unsigned char *c = (const unsigned char *)all[i]; *c; c++) CHECK(*c < 0x80);
	}
	printf("pad_family: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
