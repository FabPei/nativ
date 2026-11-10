/*
 * menu_shell.h - the in-game menu's (F10) frame around its pages (UI-03): the sections as tabs ACROSS THE TOP, the page's
 * rows under them, one pad flow for all of it. L1 / R1 step the tabs; left / right on the tabs pick one (ui::tabs_row's
 * rule); D-pad down from the tabs enters the page's rows, up from the first row comes back to the tab in use; Circle in
 * the page goes up one level to that tab, Circle on the tabs closes the menu. It replaced a sidebar on the left, which
 * the D-pad reached from the page only sideways and into the page at whichever row sat level with the entry (the owner,
 * 2026-11-09: "Es ist immer noch nicht sauber möglich von der Sectionauswahl (links) in die Settings (rechts) mit dem
 * steuerkreuz zukommen"). C++ (Dear ImGui), header-only: runtime/tests/menu_walk_test.cpp walks it with the pad.
 */
#ifndef RECOMP_MENU_SHELL_H
#define RECOMP_MENU_SHELL_H
#include <cstdio>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui_theme.h"

enum { MENU_TABS_MAX = 8 };
/* the place (0..n-1) of page id `section` in the tabs' order */
static inline int menu_tab_place(int section, const int *order, int n) { for (int k = 0; k < n; k++) if (order[k] == section) return k; return 0; }
/* the tabs, in the current window: order / labels as the pages' ids and texts (labels indexed by page id); ids[k]: each tab's
 * item id (for the focus: menu_back, and the menu's first frame) */
static inline void menu_tabs(int *section, const int *order, int n, const char *const *labels, ImGuiID *ids) {
	char lb[MENU_TABS_MAX][96]; const char *p[MENU_TABS_MAX];
	if (n > MENU_TABS_MAX) n = MENU_TABS_MAX;
	for (int k = 0; k < n; k++) { snprintf(lb[k], sizeof lb[k], "%s##tab%d", labels[order[k]], k); p[k] = lb[k]; ids[k] = ImGui::GetID(lb[k]); }
	int v = menu_tab_place(*section, order, n);
	if (ui::tabs_row(p, n, &v)) *section = order[v];
}
/* (the tabs had the focus at the end of the last frame: tabs_down_enters' argument for the page) */
static inline bool menu_tab_focused(const ImGuiID *ids, int n) { const ImGuiID nav = GImGui->NavId; for (int k = 0; k < n; k++) if (ids[k] == nav) return true; return false; }
/* Circle: up one level -- from anywhere in the page (its window or a window inside it) to the tab in use, which takes the
 * focus; on the tabs it asks to close (returns 1). menu: the window the tabs are in. */
static inline int menu_back(ImGuiWindow *page, ImGuiWindow *menu, ImGuiID tab_id) {
	if (!ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) return 0;
	ImGuiContext &g = *GImGui;
	if (page && g.NavWindow && ImGui::IsWindowChildOf(g.NavWindow, page, false)) {
		ImGui::SetFocusID(tab_id, menu); ImGui::SetNavCursorVisible(true);
		return 0;
	}
	return 1;
}
#endif
