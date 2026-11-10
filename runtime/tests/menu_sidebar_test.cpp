/* menu_sidebar_test.cpp -- the in-game menu's section list (runtime/ui/menu_sidebar.h) with a controller. A Steam Deck
 * report (owner, 2026-11-09): the sections on the left could be stepped only with L1 / R1, not reached with the D-pad and
 * chosen with Cross like the rest of the menu. The fixture builds the menu's shape -- a header with Resume, the sidebar,
 * the page with rows, the foot -- around the real sidebar, feeds the pad through the host's seam
 * (savedlg_ui_gamepad_button, as overlay_process_event does) and checks where the focus goes and what Cross chooses.
 * Build (WSL, the SDL3 headers only):
 *   c++ -O1 -g -w -std=c++17 -I ../ui -I ../core -I ../../third_party/imgui -I <SDL3 include> menu_sidebar_test.cpp \
 *       ../../third_party/imgui/imgui.cpp ../../third_party/imgui/imgui_draw.cpp ../../third_party/imgui/imgui_tables.cpp \
 *       ../../third_party/imgui/imgui_widgets.cpp -o /tmp/menu_sidebar_test -lpthread && /tmp/menu_sidebar_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL_gamepad.h>
#ifndef MENU_SIDEBAR_UNDER_TEST
#define MENU_SIDEBAR_UNDER_TEST "../ui/menu_sidebar.h"
#endif
#include MENU_SIDEBAR_UNDER_TEST

/* the host's pad feed (savedlg_ui.cpp's, reproduced: this test needs only the buttons) */
static void feed(int b, int down) {
	ImGuiIO &io = ImGui::GetIO(); io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	ImGuiKey k = b == SDL_GAMEPAD_BUTTON_SOUTH ? ImGuiKey_GamepadFaceDown : b == SDL_GAMEPAD_BUTTON_EAST ? ImGuiKey_GamepadFaceRight
	           : b == SDL_GAMEPAD_BUTTON_DPAD_UP ? ImGuiKey_GamepadDpadUp : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? ImGuiKey_GamepadDpadDown
	           : b == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? ImGuiKey_GamepadDpadLeft : ImGuiKey_GamepadDpadRight;
	io.AddKeyEvent(k, down != 0);
}

enum { T_A, T_B, T_C, T_D, T_E, T_N };
static const int order[T_N] = {T_A, T_C, T_D, T_B, T_E};
static const char *const labels[T_N] = {"Display", "Performance", "Game", "Controls", "Help"};
static int g_section = T_A;
static ImGuiID g_rows[T_N];      /* the sidebar entries' ids, by place */
static ImGuiID g_resume, g_page0;

static void menu(void) {
	ImGui::SetNextWindowPos(ImVec2(20, 20)); ImGui::SetNextWindowSize(ImVec2(900, 500));
	ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Button("Resume"); g_resume = ImGui::GetItemID();
	const float foot = ImGui::GetFrameHeightWithSpacing() * 2;
	menu_sidebar(&g_section, order, T_N, labels, -foot);
	ImGui::SameLine();
	ImGui::BeginChild("page", ImVec2(0, -foot), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
	for (int r = 0; r < 6; r++) { char b[32]; snprintf(b, sizeof b, "%s row %d", labels[g_section], r); ImGui::Button(b); if (!r) g_page0 = ImGui::GetItemID(); }
	ImGui::EndChild();
	ImGui::Button("Capture"); ImGui::SameLine(); ImGui::Button("Quit");
	ImGui::End();
}
static void frame(void) {
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2(960, 544); io.DeltaTime = 1.0f / 60.0f;
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	ImGui::NewFrame(); menu(); ImGui::Render();
}
static void press(int b) { feed(b, 1); frame(); feed(b, 0); frame(); frame(); }
/* the sidebar entry (place 0..n-1) the nav focus is on, -1 for none */
static int focused_place(void) {
	ImGuiContext &g = *GImGui;
	for (int k = 0; k < T_N; k++) if (g_rows[k] && g.NavId == g_rows[k]) return k;
	return -1;
}

static int fails;
static void check(const char *what, int ok) { printf("%-66s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO(); io.IniFilename = nullptr;
	unsigned char *px; int fw, fh; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
	frame(); frame(); frame();
	/* the entries' ids: "##menu" / "nav" child / PushID(page id) / "sec" */
	{	ImGuiWindow *m = ImGui::FindWindowByName("##menu");
		ImGuiWindow *nav = nullptr;
		for (ImGuiWindow *w : GImGui->Windows) if (w->ParentWindow == m && strstr(w->Name, "/nav_")) nav = w;
		check("the sidebar child exists", nav != nullptr);
		if (!nav) return 1;
		for (int k = 0; k < T_N; k++) g_rows[k] = ImHashStr("##sec", 0, ImHashData(&order[k], sizeof(int), nav->ID));   /* (PushID(int) then "sec", seeded by the child window) */
	}
	/* 1: from the page (where the menu's focus is), D-pad left reaches the sidebar */
	ImGui::SetFocusID(g_page0, ImGui::FindWindowByName("##menu")); GImGui->NavCursorVisible = true; frame();
	press(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
	const int at1 = focused_place();
	printf("   (after left: NavId %08x in %s)\n", GImGui->NavId, GImGui->NavWindow ? GImGui->NavWindow->Name : "-");
	check("1: D-pad left from the page lands on a sidebar entry", at1 >= 0);
	if (at1 >= 0) {
		/* 2: D-pad down / up move along the sidebar */
		press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
		const int at2 = focused_place();
		printf("   (after down: place %d -> %d, NavId %08x)\n", at1, at2, GImGui->NavId);
		check("2: D-pad down moves to the next section", at2 == at1 + 1);
		press(SDL_GAMEPAD_BUTTON_DPAD_DOWN); press(SDL_GAMEPAD_BUTTON_DPAD_UP);
		check("2: D-pad up moves back", focused_place() == at2);
		/* 3: Cross chooses the focused section */
		press(SDL_GAMEPAD_BUTTON_SOUTH);
		check("3: Cross shows the focused section's page", at2 >= 0 && g_section == order[at2]);
		/* 4: right goes into the page */
		press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
		check("4: D-pad right goes into the page", focused_place() < 0 && GImGui->NavId != 0);
	}
	ImGui::DestroyContext();
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
