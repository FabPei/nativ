/* menu_pad_test.cpp -- U12c: the full menu (F10) answers the whole controller, not just the save dialog's keys.
 * The host feeds every pad event routed to the overlay through one seam (savedlg_ui_gamepad_button/axis,
 * savedlg_ui.cpp); the menu needs more of the pad than the save dialog did: L1 / R1 step the pages, Triangle /
 * Square are ImGui's nav menu / text keys, the triggers and the right stick exist on every pad, and Start /
 * Back reach ImGui as well. The failure this pins (owner, U12b look 2026-11-07: "the whole menu is not
 * controllable by controller"): the feed mapped only Cross, Circle, the D-pad and the left stick, and the SDL3
 * backend's own frame (ImGui_ImplSDL3_NewFrame -> UpdateGamepads) cleared ImGuiBackendFlags_HasGamepad every
 * frame while its private pad list was empty -- which wipes every gamepad key (imgui.cpp UpdateKeyboardInputs)
 * and turns gamepad navigation off (nav_gamepad_active needs the flag).
 *
 * Case 1: each SDL button reaches its ImGui key (fail-before: L1, R1, Triangle, Square, Start, Back, L3, R3
 *         were dropped). Case 2: the triggers and the right stick reach theirs. Case 3: savedlg_ui_gamepad_frame
 *         re-marks the flag after the backend's frame cleared it, so a held key survives into NewFrame.
 * Case 4: with the flag and NavEnableGamepad, a D-pad press moves the nav focus between two buttons.
 *
 * Includes savedlg_ui.cpp (and apputil.c, which it needs) directly, as savedlg_pad_test.cpp does.
 * Build and run: see README.md here.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef APPUTIL_UNDER_TEST
#define APPUTIL_UNDER_TEST "../core/apputil.c"
#endif
#ifndef SAVEDLG_UI_UNDER_TEST
#define SAVEDLG_UI_UNDER_TEST "../ui/savedlg_ui.cpp"
#endif
#include APPUTIL_UNDER_TEST
#include "../core/imedlg.c"   /* (HLE-LIB-05: apputil.c's IME dialog shares its state) */
#include SAVEDLG_UI_UNDER_TEST
#include <SDL3/SDL_gamepad.h>

uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }
uint32_t guest_alloc(uint32_t size, uint32_t align) { (void)align; static uint32_t heap = 0x81000000; uint32_t a = heap; heap += size; return a; }
void gwatch_read_open(uint32_t addr, uint32_t len) { (void)addr; (void)len; }
int net_online(void) { return 0; }
const char *net_online_needs(void) { return ""; }
int overlay_draws_hud(void) { return 0; }
void overlay_toast_titled(const char *title, const char *body, double seconds) { (void)title; (void)body; (void)seconds; }
void plat_random(void *buf, size_t n) { memset(buf, 0x5a, n); }
const char *settings_str(const char *key, const char *def, char *buf, int bufsize) { (void)key; (void)buf; (void)bufsize; return def; }
void settings_set_str(const char *key, const char *v) { (void)key; (void)v; }
void settings_set_own(const char *key, const char *v) { (void)key; (void)v; }
int settings_save(void) { return 1; }

#ifndef HAVE_GAMEPAD_FRAME
#define HAVE_GAMEPAD_FRAME 1   /* -DHAVE_GAMEPAD_FRAME=0: compile against a savedlg_ui.cpp from before U12c */
#endif

static int fails;
static void check(const char *what, int ok) {
	printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) fails++;
}

/* what the host does per frame: the backend's frame clears the flag (no pads of its own), then the host's
 * hook re-marks it while SDL reports a pad */
static void backend_frame(int pad_connected) {
	ImGuiIO &io = ImGui::GetIO();
	io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;   /* ImGui_ImplSDL3_UpdateGamepads with an empty list */
#if HAVE_GAMEPAD_FRAME
	savedlg_ui_gamepad_frame(pad_connected);
#else
	(void)pad_connected;
#endif
}
static bool g_b0, g_b1;
static bool g_focus1;
static void frame(void (*build)(void) = nullptr) {
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2(960, 544);
	io.DeltaTime = 1.0f / 60.0f;
	backend_frame(1);
	ImGui::NewFrame();
	if (build) build();
	ImGui::Render();
}
static void two_buttons(void) {
	ImGui::SetNextWindowPos(ImVec2(10, 10)); ImGui::SetNextWindowSize(ImVec2(300, 200));
	ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	g_b0 = ImGui::Button("first");
	g_b1 = ImGui::Button("second");
	g_focus1 = ImGui::IsItemFocused();
	ImGui::End();
}

int main(void) {
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	unsigned char *px; int fw, fh; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
	frame();

	/* 1: every button */
	static const struct { int b; ImGuiKey k; const char *n; } btn[] = {
		{SDL_GAMEPAD_BUTTON_SOUTH, ImGuiKey_GamepadFaceDown, "Cross"}, {SDL_GAMEPAD_BUTTON_EAST, ImGuiKey_GamepadFaceRight, "Circle"},
		{SDL_GAMEPAD_BUTTON_NORTH, ImGuiKey_GamepadFaceUp, "Triangle"}, {SDL_GAMEPAD_BUTTON_WEST, ImGuiKey_GamepadFaceLeft, "Square"},
		{SDL_GAMEPAD_BUTTON_DPAD_UP, ImGuiKey_GamepadDpadUp, "D-pad up"}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, ImGuiKey_GamepadDpadLeft, "D-pad left"},
		{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ImGuiKey_GamepadL1, "L1"}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ImGuiKey_GamepadR1, "R1"},
		{SDL_GAMEPAD_BUTTON_START, ImGuiKey_GamepadStart, "Start"}, {SDL_GAMEPAD_BUTTON_BACK, ImGuiKey_GamepadBack, "Back/Select"},
		{SDL_GAMEPAD_BUTTON_LEFT_STICK, ImGuiKey_GamepadL3, "L3"}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, ImGuiKey_GamepadR3, "R3"},
	};
	for (auto &e : btn) {
		savedlg_ui_gamepad_button(e.b, 1); frame();
		char what[80]; snprintf(what, sizeof what, "1: %s reaches its ImGui key", e.n);
		const bool down = ImGui::IsKeyDown(e.k);
		savedlg_ui_gamepad_button(e.b, 0); frame();
		check(what, down && !ImGui::IsKeyDown(e.k));
	}
	/* 2: triggers and the right stick */
	static const struct { int a, v; ImGuiKey k; const char *n; } ax[] = {
		{SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000, ImGuiKey_GamepadL2, "L2"}, {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000, ImGuiKey_GamepadR2, "R2"},
		{SDL_GAMEPAD_AXIS_RIGHTX, 30000, ImGuiKey_GamepadRStickRight, "right stick right"}, {SDL_GAMEPAD_AXIS_RIGHTY, -30000, ImGuiKey_GamepadRStickUp, "right stick up"},
		{SDL_GAMEPAD_AXIS_LEFTY, 30000, ImGuiKey_GamepadLStickDown, "left stick down"},
	};
	for (auto &e : ax) {
		savedlg_ui_gamepad_axis(e.a, e.v); frame();
		char what[80]; snprintf(what, sizeof what, "2: %s reaches its ImGui key", e.n);
		const bool down = ImGui::IsKeyDown(e.k);
		savedlg_ui_gamepad_axis(e.a, 0); frame();
		check(what, down && !ImGui::IsKeyDown(e.k));
	}
	/* 3: a held key survives the backend's per-frame flag clear */
	savedlg_ui_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, 1);
	frame(); frame(); frame();
	check("3: held D-pad survives three backend frames", ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown));
	savedlg_ui_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0); frame();
	/* 4: gamepad nav moves the focus (first -> second) and Cross activates it */
	frame(two_buttons); frame(two_buttons);
	savedlg_ui_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, 1); frame(two_buttons);
	savedlg_ui_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0); frame(two_buttons); frame(two_buttons);
	check("4: D-pad down moves the nav focus to the second button", g_focus1);
	savedlg_ui_gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH, 1); frame(two_buttons);
	bool hit = g_b1;
	savedlg_ui_gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH, 0); frame(two_buttons); hit = hit || g_b1;
	check("4: Cross activates the focused button", hit);

	ImGui::DestroyContext();
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
