/* pad_bind.h — the pad -> Vita button bindings, pure (U11).
 *
 * The one canonical default table (which pad button presses which Vita button) and the settings-value names.
 * window.c owns the settings keys and the Vita button bits; this module owns the mapping so the test can drive
 * it without a window. All functions are pure: no state, no SDL calls (the SDL_GamepadButton values only). */
#pragma once
enum { PADB_CROSS, PADB_CIRCLE, PADB_SQUARE, PADB_TRIANGLE, PADB_START, PADB_SELECT,
       PADB_UP, PADB_DOWN, PADB_LEFT, PADB_RIGHT, PADB_L3, PADB_R3, PADB_N };
int pad_bind_default(int padb);                       /* the built-in pad button for a Vita button, -1 out of range */
int pad_bind_find(const int map[PADB_N], int button); /* which Vita button this pad button is bound to, -1 none */
int pad_bind_name_button(const char *name);           /* "south" -> its SDL_GamepadButton, -1 unknown */
const char *pad_bind_button_name(int button);         /* SDL_GamepadButton -> "south"; NULL for a button that is
                                                       * not one of the twelve (Guide, the shoulders -- reserved) */
