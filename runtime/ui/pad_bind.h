/* pad_bind.h — the pad -> Vita button bindings, pure (U11; PAD-MAP: every Vita and PS TV button).
 *
 * The one canonical default table (which pad button presses which Vita button) and the settings-value names.
 * window.c owns the settings keys and the Vita button bits; this module owns the mapping so the test can drive
 * it without a window. All functions are pure: no state, no SDL calls (the SDL_GamepadButton values only). */
#pragma once
enum { PADB_CROSS, PADB_CIRCLE, PADB_SQUARE, PADB_TRIANGLE, PADB_START, PADB_SELECT,
       PADB_UP, PADB_DOWN, PADB_LEFT, PADB_RIGHT, PADB_L3, PADB_R3,
       PADB_L, PADB_R,     /* the Vita's L and R (a PS TV's L1 and R1) */
       PADB_L2, PADB_R2,   /* a PS TV's L2 and R2; on a handheld, held, the touch cursor (left: the front panel, right: the rear) */
       PADB_PS,            /* the PS button: the menu */
       PADB_N };
/* the triggers as buttons (pressed past ~37%, window.c): numbers no SDL_GamepadButton has */
enum { PAD_TRIGGER_LEFT = 100, PAD_TRIGGER_RIGHT = 101 };
int pad_bind_default(int padb);                       /* the built-in pad button for a Vita button, -1 out of range */
int pad_bind_default_swapped(int padb, int swapped);  /* the same, with "shoulder_buttons" = bumpers (swapped): L / R on
                                                       * the bumpers, L2 / R2 (the touch cursor) on the triggers */
int pad_bind_find(const int map[PADB_N], int button); /* which Vita button this pad button is bound to, -1 none */
int pad_bind_name_button(const char *name);           /* "south" -> its SDL_GamepadButton, -1 unknown */
const char *pad_bind_button_name(int button);         /* SDL_GamepadButton (or PAD_TRIGGER_*) -> "south"; NULL for a
                                                       * button the mapping doesn't use (the paddles, Misc, the touchpad's click) */
