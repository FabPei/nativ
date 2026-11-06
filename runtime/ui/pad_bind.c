/* pad_bind.c — the pure pad -> Vita button mapping (U11). See pad_bind.h. */
#include <string.h>
#include <SDL3/SDL_gamepad.h>
#include "pad_bind.h"

static const int defaults[PADB_N] = {   /* the twelve, in PADB_ order */
	SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
	SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
	SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
	SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
};
int pad_bind_default(int padb) { return padb >= 0 && padb < PADB_N ? defaults[padb] : -1; }
int pad_bind_find(const int map[PADB_N], int button) {
	if (!map || button < 0) return -1;
	for (int i = 0; i < PADB_N; i++) if (map[i] == button) return i;   /* (a hand-edited duplicate: the first wins) */
	return -1;
}
static const struct { const char *name; int button; } names[] = {
	{"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"east", SDL_GAMEPAD_BUTTON_EAST},
	{"west", SDL_GAMEPAD_BUTTON_WEST}, {"north", SDL_GAMEPAD_BUTTON_NORTH},
	{"start", SDL_GAMEPAD_BUTTON_START}, {"back", SDL_GAMEPAD_BUTTON_BACK},
	{"dpad_up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"dpad_down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
	{"dpad_left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"dpad_right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
	{"left_stick", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"right_stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
};
int pad_bind_name_button(const char *name) {
	if (!name) return -1;
	for (unsigned i = 0; i < sizeof names / sizeof *names; i++) if (!strcmp(name, names[i].name)) return names[i].button;
	return -1;
}
const char *pad_bind_button_name(int button) {
	for (unsigned i = 0; i < sizeof names / sizeof *names; i++) if (names[i].button == button) return names[i].name;
	return NULL;
}
