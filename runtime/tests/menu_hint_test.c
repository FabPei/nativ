/* menu_hint_test.c - runtime/ui/menu_hint.h (UI-02a, HINT-01): the start hint names every way to open the menu -- the
 * keyboard's (and the mouse's), a controller's in its own names, a touch screen's -- always, the ways the player has now
 * first. The owner's reports: 2026-11-09 the hint said only "Move the mouse to the top, or press [F10] or Start+Select";
 * 2026-11-10 "es steht immer noch nur da, dass F10 das Menü öffnet ... auch sagen, wie Controller- oder Touchscreennutzer
 * das Menü öffnen" (UI-02a named only the devices there at the start: without a pad, no controller line). Built against
 * the UI-02a header (-DMENU_HINT_UI02A=1 with its folder first on the include path) the HINT-01 checks FAIL.
 * Build: cc -std=c99 -Wall -I ../ui -I <SDL3 include> menu_hint_test.c -o /tmp/menu_hint_test && /tmp/menu_hint_test */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_gamepad.h>
#include "menu_hint.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define HAS(s, x) (strstr((s), (x)) != NULL)
static const char *line(const char *b, int k, char *out) {   /* the k-th line (0-based) of b */
	for (int i = 0; i < k && b; i++) { b = strchr(b, '\n'); if (b) b++; }
	if (!b) { out[0] = 0; return out; }
	const char *e = strchr(b, '\n'); size_t n = e ? (size_t)(e - b) : strlen(b);
	memcpy(out, b, n); out[n] = 0; return out;
}

int main(void) {
	char b[224], l[128]; int n;
	/* a desktop without a pad or a touch screen: the keyboard first, then the controller's way (the common names) and touch */
	n = menu_hint_text(b, sizeof b, 1, 1, -1, 0, 0, 0);
	CHECK(n == 3);
	CHECK(HAS(line(b, 0, l), "Keyboard: [F10]") && HAS(l, "mouse"));
	CHECK(HAS(line(b, 1, l), "Controller: [Start] + [Select]"));   /* (UI-02a: no controller line without a pad) */
	CHECK(HAS(line(b, 2, l), "Touch: the tab at the top, then Game > Full menu"));   /* (UI-02a: no touch line without a touch screen) */
	/* an Xbox pad: its own names, second (keyboard there too) */
	n = menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_XBOXONE, 0, 0, 0);
	CHECK(n == 3 && HAS(line(b, 1, l), "Controller: [Menu] + [View]") && HAS(line(b, 2, l), "Touch"));
	menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_PS5, 0, 0, 0);
	CHECK(HAS(b, "Controller: [Options] + [Share]"));
	menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0, 0, 0);
	CHECK(HAS(b, "Controller: [+] + [-]"));
	/* the Steam Deck in Game Mode: its controller and its touch screen first, the keyboard's way last */
	n = menu_hint_text(b, sizeof b, 0, 0, SDL_GAMEPAD_TYPE_XBOX360, 1, 1, 0);
	CHECK(n == 3 && HAS(line(b, 0, l), "Controller: [Menu] + [View]") && HAS(line(b, 1, l), "Touch") && HAS(line(b, 2, l), "[F10]") && !HAS(b, "On-screen"));
	/* a tablet: touch first; the on-screen buttons' off switch */
	n = menu_hint_text(b, sizeof b, 0, 0, -1, 0, 1, 1);
	CHECK(n == 4 && HAS(line(b, 0, l), "Touch") && HAS(b, "Controller: [Start] + [Select]") && HAS(b, "[F10]") && HAS(line(b, 3, l), "On-screen buttons: off in the menu, Controls"));
	/* nothing detected: still all three (a keyboard SDL didn't report) */
	n = menu_hint_text(b, sizeof b, 0, 0, -1, 0, 0, 0);
	CHECK(n == 3 && HAS(b, "[F10]") && HAS(b, "Controller:") && HAS(b, "Touch:"));
	/* the most there is: fits the toast's body (224 bytes) */
	n = menu_hint_text(b, sizeof b, 1, 1, -1, 0, 1, 1);
	CHECK(n == 4 && strlen(b) < sizeof b - 1);
	n = menu_hint_text(b, sizeof b, 1, 1, SDL_GAMEPAD_TYPE_PS4, 1, 1, 0);
	CHECK(strlen(b) < sizeof b - 1);
	/* a short buffer: cut, still a string */
	char s[16]; menu_hint_text(s, sizeof s, 1, 1, SDL_GAMEPAD_TYPE_PS4, 0, 1, 0);
	CHECK(strlen(s) == sizeof s - 1);
#if !MENU_HINT_UI02A
	/* (HINT-01) the note when a controller is connected: its own buttons, or the common names */
	menu_hint_pad_line(l, sizeof l, SDL_GAMEPAD_TYPE_XBOXONE, 0); CHECK(!strcmp(l, "Controller: [Menu] + [View]"));
	menu_hint_pad_line(l, sizeof l, SDL_GAMEPAD_TYPE_PS4, 0); CHECK(!strcmp(l, "Controller: [Options] + [Share]"));
	menu_hint_pad_line(l, sizeof l, -1, 0); CHECK(!strcmp(l, "Controller: [Start] + [Select], or [Menu] + [View]"));
#endif
	printf("menu_hint: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
