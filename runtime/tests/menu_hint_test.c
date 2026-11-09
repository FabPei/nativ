/* menu_hint_test.c - runtime/ui/menu_hint.h (UI-02a): the start hint names every way to open the menu the player has --
 * and only those. The owner's report (2026-11-09): the hint said only "Move the mouse to the top, or press [F10] or
 * Start+Select", nothing for a touch screen and the PlayStation names on every controller.
 * Build: cc -std=c99 -Wall -I ../ui -I <SDL3 include> menu_hint_test.c -o /tmp/menu_hint_test && /tmp/menu_hint_test */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_gamepad.h>
#include "menu_hint.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define HAS(s, x) (strstr((s), (x)) != NULL)

int main(void) {
	/* the hint as it was: one text for everyone (fail-before: what the old text lacks) */
	const char *old = "Move the mouse to the top, or press [F10] or Start+Select";
	CHECK(!HAS(old, "Touch") && !HAS(old, "[Menu] + [View]"));   /* (true: the defect -- no touch, the Xbox pad's names missing) */
	char b[224]; int n;
	/* keyboard + mouse only (a desktop without a pad): no controller, no touch line */
	n = menu_hint_text(b, sizeof b, 1, 1, -1, 0, 0);
	CHECK(n == 1 && HAS(b, "[F10]") && HAS(b, "mouse") && !HAS(b, "Controller") && !HAS(b, "Touch"));
	/* an Xbox pad: its own names */
	n = menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_XBOXONE, 0, 0);
	CHECK(n == 2 && HAS(b, "Controller: [Menu] + [View]") && HAS(b, "\nController"));
	/* a DualSense */
	menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_PS5, 0, 0);
	CHECK(HAS(b, "[Options] + [Share]"));
	/* a Switch pad */
	menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0, 0);
	CHECK(HAS(b, "[+] + [-]"));
	/* the Steam Deck in Game Mode: Steam's virtual Xbox pad, the Deck's names, its touch screen; no keyboard or mouse */
	n = menu_hint_text(b, sizeof b, 0, 0, SDL_GAMEPAD_TYPE_XBOX360, 1, 1);
	CHECK(n == 2 && !HAS(b, "[F10]") && HAS(b, "Controller: [Menu] + [View]") && HAS(b, "Touch: the tab at the top"));
	/* a tablet: touch only */
	n = menu_hint_text(b, sizeof b, 0, 0, -1, 0, 1);
	CHECK(n == 1 && HAS(b, "Touch") && !HAS(b, "Controller"));
	/* nothing detected: F10 all the same (a keyboard SDL didn't report) */
	n = menu_hint_text(b, sizeof b, 0, 0, -1, 0, 0);
	CHECK(n == 1 && HAS(b, "[F10]"));
	/* everything: fits the toast's body (224 bytes) */
	n = menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_PS4, 0, 1);
	CHECK(n == 3 && strlen(b) < sizeof b - 1);
	/* a short buffer: cut, still a string */
	char s[16]; menu_hint_text(s, sizeof s, 1, 1, SDL_GAMEPAD_TYPE_PS4, 0, 1);
	CHECK(strlen(s) == sizeof s - 1);
	printf("menu_hint: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
