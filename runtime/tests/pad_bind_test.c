/* pad_bind_test.c — U11, PAD-MAP: the pure pad -> Vita mapping and its default table (every Vita and PS TV button).
 *
 * Build in WSL (literal flags -- the tool layer eats $VARS):
 *   cc -O1 -g -Wall -I ../ui -I $HOME/toolchains/ativ-win/SDL3/x86_64-w64-mingw32/include \
 *      pad_bind_test.c ../ui/pad_bind.c -o /tmp/pad_bind_test && /tmp/pad_bind_test
 */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_gamepad.h>
#include "pad_bind.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
	/* the default table: every Vita and PS TV button, bijective (PAD-MAP: 17 rows) */
	CHECK(PADB_N == 17);
	int m[PADB_N];
	for (int i = 0; i < PADB_N; i++) m[i] = pad_bind_default(i);
	CHECK(pad_bind_default(PADB_CROSS) == SDL_GAMEPAD_BUTTON_SOUTH);
	CHECK(pad_bind_default(PADB_CIRCLE) == SDL_GAMEPAD_BUTTON_EAST);
	CHECK(pad_bind_default(PADB_SQUARE) == SDL_GAMEPAD_BUTTON_WEST);
	CHECK(pad_bind_default(PADB_TRIANGLE) == SDL_GAMEPAD_BUTTON_NORTH);
	CHECK(pad_bind_default(PADB_START) == SDL_GAMEPAD_BUTTON_START);
	CHECK(pad_bind_default(PADB_SELECT) == SDL_GAMEPAD_BUTTON_BACK);
	CHECK(pad_bind_default(PADB_UP) == SDL_GAMEPAD_BUTTON_DPAD_UP);
	CHECK(pad_bind_default(PADB_DOWN) == SDL_GAMEPAD_BUTTON_DPAD_DOWN);
	CHECK(pad_bind_default(PADB_LEFT) == SDL_GAMEPAD_BUTTON_DPAD_LEFT);
	CHECK(pad_bind_default(PADB_RIGHT) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
	CHECK(pad_bind_default(PADB_L3) == SDL_GAMEPAD_BUTTON_LEFT_STICK);
	CHECK(pad_bind_default(PADB_R3) == SDL_GAMEPAD_BUTTON_RIGHT_STICK);
	/* the shoulders as before: L and R on the triggers, the L2 / R2 pair (the touch cursor) on the bumpers, PS on Guide */
	CHECK(pad_bind_default(PADB_L) == PAD_TRIGGER_LEFT && pad_bind_default(PADB_R) == PAD_TRIGGER_RIGHT);
	CHECK(pad_bind_default(PADB_L2) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && pad_bind_default(PADB_R2) == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
	CHECK(pad_bind_default(PADB_PS) == SDL_GAMEPAD_BUTTON_GUIDE);
	/* "shoulder_buttons" = bumpers: the two pairs' defaults swap, nothing else moves */
	CHECK(pad_bind_default_swapped(PADB_L, 1) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && pad_bind_default_swapped(PADB_R, 1) == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
	CHECK(pad_bind_default_swapped(PADB_L2, 1) == PAD_TRIGGER_LEFT && pad_bind_default_swapped(PADB_R2, 1) == PAD_TRIGGER_RIGHT);
	for (int i = 0; i < PADB_N; i++) if (i != PADB_L && i != PADB_R && i != PADB_L2 && i != PADB_R2) CHECK(pad_bind_default_swapped(i, 1) == pad_bind_default(i));
	for (int i = 0; i < PADB_N; i++) CHECK(pad_bind_default_swapped(i, 0) == pad_bind_default(i));
	CHECK(pad_bind_default(PADB_N) == -1 && pad_bind_default(-1) == -1);
	for (int i = 0; i < PADB_N; i++) CHECK(pad_bind_find(m, m[i]) == i);   /* each button reaches exactly its row */
	CHECK(pad_bind_find(m, SDL_GAMEPAD_BUTTON_GUIDE) == PADB_PS);
	CHECK(pad_bind_find(m, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == PADB_L2 && pad_bind_find(m, PAD_TRIGGER_RIGHT) == PADB_R);
	CHECK(pad_bind_find(m, SDL_GAMEPAD_BUTTON_MISC1) == -1);
	CHECK(pad_bind_find(m, -1) == -1 && pad_bind_find(NULL, SDL_GAMEPAD_BUTTON_SOUTH) == -1);
	CHECK((int)PAD_TRIGGER_LEFT >= (int)SDL_GAMEPAD_BUTTON_COUNT && PAD_TRIGGER_RIGHT == PAD_TRIGGER_LEFT + 1);   /* (never a real button) */

	/* a remap: Cross moves to East; South is then free */
	m[PADB_CROSS] = SDL_GAMEPAD_BUTTON_EAST;
	CHECK(pad_bind_find(m, SDL_GAMEPAD_BUTTON_EAST) == PADB_CROSS);
	CHECK(pad_bind_find(m, SDL_GAMEPAD_BUTTON_SOUTH) == -1);

	/* the settings-value names round-trip; a button the Vita has no use for has no name (the capture refuses it) */
	static const struct { const char *name; int button; } expect[] = {
		{"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"east", SDL_GAMEPAD_BUTTON_EAST},
		{"west", SDL_GAMEPAD_BUTTON_WEST}, {"north", SDL_GAMEPAD_BUTTON_NORTH},
		{"start", SDL_GAMEPAD_BUTTON_START}, {"back", SDL_GAMEPAD_BUTTON_BACK},
		{"dpad_up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"dpad_down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
		{"dpad_left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"dpad_right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
		{"left_stick", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"right_stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
		{"left_shoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"right_shoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
		{"left_trigger", PAD_TRIGGER_LEFT}, {"right_trigger", PAD_TRIGGER_RIGHT}, {"guide", SDL_GAMEPAD_BUTTON_GUIDE},
	};
	for (unsigned i = 0; i < sizeof expect / sizeof *expect; i++) {
		CHECK(pad_bind_name_button(expect[i].name) == expect[i].button);
		CHECK(pad_bind_button_name(expect[i].button) && !strcmp(pad_bind_button_name(expect[i].button), expect[i].name));
	}
	CHECK(pad_bind_name_button("bogus") == -1 && pad_bind_name_button(NULL) == -1);
	CHECK(pad_bind_button_name(SDL_GAMEPAD_BUTTON_MISC1) == NULL);

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
