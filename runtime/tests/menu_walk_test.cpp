/* menu_walk_test.cpp -- the in-game menu's pad flow (UI-03), walked with the pad alone: the menu opens on the tab in use;
 * D-pad down enters each page at its first row and visits every row in order (a list that stacks is walked through);
 * up from the first row comes back to the tab; L1 / R1 step the tabs; left / right change a choice and a slider; Circle
 * in a row goes back to the tab, Circle on the tabs closes. Every control kind the menu has is on the pages: an Off / On
 * toggle, a choice of three, a list that stacks (long names), a slider, a logarithmic slider, a button.
 * -DMENU_WALK_OLD=1: the same pages in the frame as it was (the sections in a sidebar on the left: menu_sidebar.h,
 * Circle closing the menu from anywhere) -- the owner's report (2026-11-09) -> FAILED.
 * Focus is read by label: ImGui's test-engine hooks (-DIMGUI_ENABLE_TEST_ENGINE) record each item's label.
 * Build (WSL; SDL3's headers only):
 *   c++ -O1 -g -w -std=c++17 -DIMGUI_ENABLE_TEST_ENGINE -I ../ui -I ../../third_party/imgui menu_walk_test.cpp \
 *       ../../third_party/imgui/imgui.cpp ../../third_party/imgui/imgui_draw.cpp ../../third_party/imgui/imgui_tables.cpp \
 *       ../../third_party/imgui/imgui_widgets.cpp -o /tmp/menu_walk_test && /tmp/menu_walk_test */
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "imgui.h"
#include "imgui_internal.h"
extern "C" const char *ui_tr(const char *id) { return !strcmp(id, "common.off") ? "Off" : !strcmp(id, "common.on") ? "On" : id; }
#include "ui_theme.h"
#ifndef MENU_WALK_OLD
#define MENU_WALK_OLD 0
#endif
#if MENU_WALK_OLD
#include "menu_sidebar.h"
#else
#include "menu_shell.h"
#endif

/* the test-engine hooks: each item's label by id */
static std::map<ImGuiID, std::string> g_labels;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext *, ImGuiID, const ImRect &, const ImGuiLastItemData *) {}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext *, ImGuiID id, const char *label, ImGuiItemStatusFlags) { if (label) g_labels[id] = label; }
void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID) { return nullptr; }

enum { T_N = 3 };
static const int order[T_N] = {0, 1, 2};
static const char *const tab_names[T_N] = {"Display", "Controls", "Sound"};
static const int rows_of[T_N] = {3, 3, 2};
static int g_section, g_choice = 0, g_list = 0, g_pick2 = 0; static bool g_full = false, g_vib = true;
static float g_scale = 1.0f, g_sens = 1.0f; static bool g_closed, g_focus_page; static int g_open_frames; static int g_pressed;
static ImGuiID g_tab_ids[T_N];

static void page(int t) {
	ui::rows_begin("rows");
	if (t == 0) {
		ui::row("Fullscreen##t0_r0"); ui::toggle("t0_r0", &g_full);
		ui::row("Mode"); { const char *o[3] = {"Fit##t0_r1", "Stretch##t0_r1", "Fill##t0_r1"}; ui::segmented(o, 3, &g_choice); }
		ui::row("Scale"); ImGui::SetNextItemWidth(-FLT_MIN); ui::slider_float("##t0_r2", &g_scale, 0.5f, 2.0f, "%.2f");
	} else if (t == 1) {
		ui::row("Controller"); { const char *o[4] = {"Automatic##t1_r0", "Xbox Wireless Controller (Xbox) number one##t1_r0",
		                                             "DualSense Wireless Controller (PlayStation) number two##t1_r0", "8BitDo Pro 2 (not connected) number three##t1_r0"}; ui::segmented(o, 4, &g_list); }
		ui::row("Reset"); if (ImGui::Button("Reset the mapping##t1_r1")) g_pressed++;
		ui::row("Vibration"); ui::toggle("t1_r2", &g_vib);
	} else {
		ui::row("Sensitivity"); ImGui::SetNextItemWidth(-FLT_MIN); ui::slider_float("##t2_r0", &g_sens, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ui::row("Output"); { const char *o[2] = {"Speakers##t2_r1", "Headphones##t2_r1"}; ui::segmented(o, 2, &g_pick2); }
	}
	ui::rows_end();
}
static void menu(void) {
	ImGui::SetNextWindowPos(ImVec2(20, 20)); ImGui::SetNextWindowSize(ImVec2(1240, 760));
	ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
	ImGuiWindow *menuw = ImGui::GetCurrentWindow();
	ImGui::SetCursorPosX(1100); ImGui::Button("Resume##hdr");
	const float foot = ImGui::GetFrameHeightWithSpacing() * 2;
	/* L1 / R1: the tabs in order, the new page's first row takes the focus (as overlay.cpp does) */
	int step = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ? -1 : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ? 1 : 0;
	if (step) { g_section = order[(g_section + step + T_N) % T_N]; g_focus_page = true; }
	ImGuiWindow *pagew = nullptr;
#if MENU_WALK_OLD
	const char *labels[T_N]; char lb[T_N][32]; for (int i = 0; i < T_N; i++) { snprintf(lb[i], 32, "%s", tab_names[i]); labels[i] = lb[i]; }
	menu_sidebar(&g_section, order, T_N, labels, -foot);
	ImGui::SameLine();
	ImGui::BeginChild("page", ImVec2(0, -foot), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
	pagew = ImGui::GetCurrentWindow();
	if (g_focus_page) { ImGui::SetKeyboardFocusHere(); g_focus_page = false; }
	page(g_section);
	ImGui::EndChild();
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) g_closed = true;   /* (as it was: Circle resumes the game from anywhere) */
	(void)menuw; (void)pagew;
#else
	menu_tabs(&g_section, order, T_N, tab_names, g_tab_ids);
	if (g_open_frames < 2) { ImGui::SetFocusID(g_tab_ids[menu_tab_place(g_section, order, T_N)], menuw); ImGui::SetNavCursorVisible(true); g_open_frames++; }   /* (the menu opens on the tab in use: its first two frames, ImGui puts the focus on the first item of a new window after the first) */
	ImGui::BeginChild("page", ImVec2(0, -foot), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
	pagew = ImGui::GetCurrentWindow();
	static bool tabs_had_focus;
	ui::tabs_down_enters(tabs_had_focus);   /* (as overlay.cpp: down from the tabs, the page's first control) */
	if (g_focus_page) { ui::focus_next_item(); g_focus_page = false; }
	page(g_section);
	ui::nav_keep_visible();
	ImGui::EndChild();
	if (menu_back(pagew, menuw, g_tab_ids[menu_tab_place(g_section, order, T_N)])) g_closed = true;
	tabs_had_focus = menu_tab_focused(g_tab_ids, T_N);
#endif
	ImGui::Button("Capture##foot");
	ImGui::End();
}
static void frame(void) {
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2(1280, 800); io.DeltaTime = 1.0f / 60.0f; io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	ImGui::NewFrame(); menu(); ImGui::Render();
}
static void press(ImGuiKey k) { ImGui::GetIO().AddKeyEvent(k, true); frame(); ImGui::GetIO().AddKeyEvent(k, false); frame(); frame(); }
static std::string focus(void) { auto it = g_labels.find(GImGui->NavId); return it == g_labels.end() ? std::string("-") : it->second; }
static bool at(const char *tok) { return focus().find(tok) != std::string::npos; }
static int row_of(const std::string &l) { const size_t p = l.find("_r"); return p == std::string::npos ? -1 : l[p + 2] - '0'; }
static int tab_of(const std::string &l) { const size_t p = l.find("##t"); return p == std::string::npos || l[p + 3] < '0' || l[p + 3] > '9' ? -1 : l[p + 3] - '0'; }

static int fails;
static void check(const char *what, bool ok) { printf("%-74s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) { fails++; printf("    (focus: %s)\n", focus().c_str()); } }

int main(void) {
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO(); io.IniFilename = nullptr; GImGui->TestEngineHookItems = true;   /* (the label hooks run) */
	unsigned char *px; int fw, fh; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
	frame(); frame(); frame();
	check("the menu opens on the tab in use", at("##tab0"));
	for (int t = 0; t < T_N; t++) {
		char w[96];
		if (t) {   /* the next tab: R1 */
			press(ImGuiKey_GamepadR1);
			snprintf(w, sizeof w, "R1: tab %d shown, its first row has the focus", t);
			check(w, g_section == t && tab_of(focus()) == t && row_of(focus()) == 0);
			press(ImGuiKey_GamepadDpadUp);
			snprintf(w, sizeof w, "tab %d: up from the first row comes back to its tab", t);
			char tok[16]; snprintf(tok, sizeof tok, "##tab%d", t);
			check(w, at(tok));
		}
		press(ImGuiKey_GamepadDpadDown);
		snprintf(w, sizeof w, "tab %d: down from the tab enters the first row", t);
		check(w, tab_of(focus()) == t && row_of(focus()) == 0);
		/* every row in order, a stacking list walked through on the way (at most 6 presses per row) */
		int seen = row_of(focus()) == 0 ? 1 : 0, last = row_of(focus()); bool order_ok = true;
		for (int i = 0; i < 6 * rows_of[t] && seen < rows_of[t]; i++) {
			press(ImGuiKey_GamepadDpadDown);
			const int r = row_of(focus());
			if (tab_of(focus()) != t || r < last) { order_ok = false; break; }
			if (r == last + 1) { seen++; last = r; }
		}
		snprintf(w, sizeof w, "tab %d: down visits its %d rows in order", t, rows_of[t]);
		check(w, order_ok && seen == rows_of[t]);
		/* back up to the tab */
		char tok[16]; snprintf(tok, sizeof tok, "##tab%d", t);
		for (int i = 0; i < 20 && !at("##tab"); i++) press(ImGuiKey_GamepadDpadUp);
		snprintf(w, sizeof w, "tab %d: up from the rows comes back to tab %d", t, t);
		check(w, at(tok));
	}
	/* left / right change values: tab 0's choice of three and its slider */
	press(ImGuiKey_GamepadL1); press(ImGuiKey_GamepadL1);   /* (back to tab 0: its first row) */
	press(ImGuiKey_GamepadDpadDown);   /* (row 1: the choice, on its value Fit) */
	const int c0 = g_choice; press(ImGuiKey_GamepadDpadRight);
	check("right on a choice picks the next value (Fit -> Stretch)", c0 == 0 && g_choice == 1);
	press(ImGuiKey_GamepadDpadDown);   /* (row 2: the slider) */
	const float s0 = g_scale; press(ImGuiKey_GamepadDpadRight);
	check("right on a slider raises it, without taking it first", g_scale > s0 && row_of(focus()) == 2);
	press(ImGuiKey_GamepadDpadUp);
	check("up from a changed choice's neighbour lands on its value (Stretch)", focus().find("Stretch##t0_r1") == 0);
	/* Circle: a row -> its tab; the tab -> closed */
	press(ImGuiKey_GamepadFaceRight);
	check("Circle in a row goes back to the tab in use, the menu stays", at("##tab0") && !g_closed);
	press(ImGuiKey_GamepadFaceRight);
	check("Circle on the tabs closes the menu", g_closed);
	ImGui::DestroyContext();
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
