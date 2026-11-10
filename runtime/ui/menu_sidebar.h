/*
 * menu_sidebar.h - the in-game menu's (F10) section list on its left: one entry per page, in the sidebar's order. Moved
 * here from overlay.cpp's build_ui so runtime/tests/menu_sidebar_test.cpp can drive it with a controller.
 * Each entry is a Selectable: the D-pad reaches it from the page (left) and moves along the list (up / down), Cross chooses
 * it, and the focused one shows ImGui's focus frame. They were InvisibleButtons, which ImGui's navigation never chose as a
 * target from the page -- the sections could be stepped only with L1 / R1 (a Steam Deck report, 2026-11-09).
 * C++ (Dear ImGui), header-only; the look from ui_theme.h.
 */
#ifndef RECOMP_MENU_SIDEBAR_H
#define RECOMP_MENU_SIDEBAR_H
#include "imgui.h"
#include "ui_theme.h"

/* the list inside the current window (a NavFlattened child of height h); section: the page shown (changed when an entry is
 * chosen); order / labels: the n entries' page ids and texts in the sidebar's order (labels indexed by page id) */
static inline void menu_sidebar(int *section, const int *order, int n, const char *const *labels, float h_child) {
	const float em = ImGui::GetFontSize();
	ImGui::BeginChild("nav", ImVec2(em * 11.5f, h_child), ImGuiChildFlags_NavFlattened);
	for (int k = 0; k < n; k++) {
		int i = order[k]; bool on = *section == i;
		ImVec2 p = ImGui::GetCursorScreenPos(); float h = ImGui::GetFrameHeight() * 1.15f, w = ImGui::GetContentRegionAvail().x;
		ImGui::PushID(i);
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		if (ImGui::Selectable("##sec", on, ImGuiSelectableFlags_NoPadWithHalfSpacing, ImVec2(w, h))) *section = i;   /* (its own fill hidden: the look is drawn below) */
		ImGui::PopStyleColor(3);
		bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		if (on || hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ui::u32(on ? ui::card_hi : ui::card), ImGui::GetStyle().FrameRounding);
		if (on) dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.22f), ImVec2(p.x + 3, p.y + h * 0.78f), ui::u32(ui::accent), 2);
		dl->AddText(ImVec2(p.x + em * 0.9f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), ui::u32(on ? ui::text : ui::muted), labels[i]);
		ImGui::PopID();
	}
	ImGui::EndChild();
}
#endif
