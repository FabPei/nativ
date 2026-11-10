/*
 * Project Nativ's look, shared by the launcher (launcher/hub.cpp) and the in-game overlay (host/overlay.cpp): one
 * palette (a dark slate ground, warm off-white text, the amber the in-game badges use as the accent), the
 * system's own UI face, the logo (drawn, so it's sharp at any size: assets/brand/make.py's geometry), and the
 * few controls both draw the same way -- the one primary button a view offers, a segmented choice, status
 * dots, and settings rows (a label with a line of explanation on the left, its control on the right).
 * Header-only C++ over Dear ImGui.
 */
#ifndef ATIV_UI_THEME_H
#define ATIV_UI_THEME_H
#include "imgui.h"
#include "imgui_internal.h"   /* (UI-03: SetFocusID, the nav state) */
#include "ui_text.h"   /* (U12b) ui_tr for the toggle's Off/On */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <sys/stat.h>

namespace ui {
inline const ImVec4 bg(0.071f, 0.078f, 0.098f, 1), surface(0.102f, 0.114f, 0.141f, 1), card(0.125f, 0.141f, 0.173f, 1),
	card_hi(0.149f, 0.168f, 0.204f, 1), border(0.173f, 0.192f, 0.231f, 1), text(0.914f, 0.902f, 0.875f, 1), muted(0.565f, 0.592f, 0.639f, 1),
	accent(0.941f, 0.706f, 0.298f, 1), accent_hi(0.961f, 0.773f, 0.424f, 1), accent_ink(0.10f, 0.08f, 0.04f, 1),
	ready(0.345f, 0.761f, 0.545f, 1), warn(0.910f, 0.639f, 0.239f, 1), error(0.898f, 0.376f, 0.353f, 1);
inline ImVec4 with_alpha(const ImVec4 &c, float a) { return ImVec4(c.x, c.y, c.z, c.w * a); }
inline ImU32 u32(const ImVec4 &c, float a = 1) { return ImGui::GetColorU32(with_alpha(c, a)); }

/* the style: sizes in points at font_size, scaled by the display's content scale */
inline void apply(float font_size, float scale) {
	ImGuiStyle &st = ImGui::GetStyle();
	ImGui::StyleColorsDark();
	st.FontSizeBase = font_size; st.FontScaleDpi = scale;
	const float k = font_size / 16.0f;
	st.WindowPadding = ImVec2(22 * k, 18 * k); st.FramePadding = ImVec2(14 * k, 7 * k); st.ItemSpacing = ImVec2(12 * k, 9 * k);
	st.ItemInnerSpacing = ImVec2(8 * k, 6 * k); st.CellPadding = ImVec2(10 * k, 7 * k); st.SeparatorTextPadding = ImVec2(0, 6 * k);
	st.WindowRounding = 12 * k; st.ChildRounding = 10 * k; st.FrameRounding = 7 * k; st.PopupRounding = 10 * k; st.GrabRounding = 7 * k;
	st.ScrollbarRounding = 7 * k; st.TabRounding = 7 * k; st.GrabMinSize = 14 * k; st.ScrollbarSize = 11 * k;
	st.WindowBorderSize = 1; st.ChildBorderSize = 1; st.PopupBorderSize = 1; st.FrameBorderSize = 0; st.TabBorderSize = 0;
	ImVec4 *c = st.Colors;
	c[ImGuiCol_Text] = text; c[ImGuiCol_TextDisabled] = muted; c[ImGuiCol_WindowBg] = bg; c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_PopupBg] = surface; c[ImGuiCol_Border] = border; c[ImGuiCol_FrameBg] = card; c[ImGuiCol_FrameBgHovered] = card_hi;
	c[ImGuiCol_FrameBgActive] = card_hi; c[ImGuiCol_Button] = card_hi; c[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.22f, 0.27f, 1);
	c[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.26f, 0.32f, 1); c[ImGuiCol_Header] = card_hi; c[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.22f, 0.27f, 1);
	c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.26f, 0.32f, 1); c[ImGuiCol_Separator] = border; c[ImGuiCol_SeparatorHovered] = border;
	c[ImGuiCol_CheckMark] = accent; c[ImGuiCol_SliderGrab] = accent; c[ImGuiCol_SliderGrabActive] = accent_hi; c[ImGuiCol_TextLink] = accent;
	c[ImGuiCol_TitleBg] = surface; c[ImGuiCol_TitleBgActive] = surface; c[ImGuiCol_MenuBarBg] = surface; c[ImGuiCol_Tab] = card;
	c[ImGuiCol_TabHovered] = card_hi; c[ImGuiCol_TabSelected] = card_hi; c[ImGuiCol_TabSelectedOverline] = accent;
	c[ImGuiCol_TableHeaderBg] = card; c[ImGuiCol_TableBorderLight] = border; c[ImGuiCol_TableBorderStrong] = border;
	c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.72f); c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ScrollbarGrab] = card_hi; c[ImGuiCol_NavCursor] = accent; c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
	st.ScaleAllSizes(scale);
}
/* the system's own UI face, read at run time (nothing shipped): San Francisco, Segoe UI, a common Linux sans;
 * ImGui's built-in vector font when none is there. ImGui 1.92's fonts size themselves: one face, any size */
inline ImFont *load_system_font(ImGuiIO &io) {
	static const char *const faces[] = {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc", "C:\\Windows\\Fonts\\segoeui.ttf",
		"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/cantarell/Cantarell-VF.otf", 0};
	ImFont *font = nullptr;
	for (int i = 0; faces[i] && !font; i++) { struct stat st; if (stat(faces[i], &st) == 0) font = io.Fonts->AddFontFromFileTTF(faces[i], 0, nullptr, io.Fonts->GetGlyphRangesDefault()); }   /* (U12a: the default ranges -- Basic Latin + Latin-1 -- are the catalog's contract: English and German both; ui_text_test asserts every string stays in them) */
	if (!font) font = io.Fonts->AddFontDefaultVector();
	/* (UI-05) names the catalog doesn't write -- a game's title, a device's name -- may be Chinese, Japanese or Korean: the
	 * system's CJK faces merged in behind it (ImGui 1.92 loads each glyph as it is first drawn, from the first face that has
	 * it). One face per script group, the first one found; nothing shipped. None there: text_drawable() says so. */
	static const char *const han[] = {"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\YuGothR.ttc", "C:\\Windows\\Fonts\\msjh.ttc",
		"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc",
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJKsc-Regular.otf", 0};
	static const char *const hangul[] = {"C:\\Windows\\Fonts\\malgun.ttf", "/System/Library/Fonts/AppleSDGothicNeo.ttc", 0};   /* (a Noto CJK face above has Hangul already) */
	const char *picked = nullptr;
	if (const char *e = getenv("ATIV_SHOT_NO_CJK")) if (*e == '1') return font;   /* (a screenshot run as on a system without them: the title-id fallback) */
	for (const char *const *grp : {han, hangul})
		for (int i = 0; grp[i]; i++) {
			struct stat st; if (stat(grp[i], &st) != 0) continue;
			if (grp == hangul && picked && strstr(picked, "Noto")) break;
			ImFontConfig cfg; cfg.MergeMode = true;
			if (io.Fonts->AddFontFromFileTTF(grp[i], 0, &cfg, nullptr)) { if (grp == han) picked = grp[i]; break; }
		}
	return font;
}
/* (UI-05) can the face draw every character of s (UTF-8)? A name it can't is better shown another way (the launcher: the
 * game's title id) than as boxes / question marks */
inline bool text_drawable(const char *s) {
	ImGuiIO &io = ImGui::GetIO();
	if (io.Fonts->Fonts.Size == 0) return true;
	ImFont *f = io.Fonts->Fonts[0];
	for (const char *p = s; *p;) {
		unsigned int c = 0; const int n = ImTextCharFromUtf8(&c, p, nullptr);
		if (n <= 0 || c == IM_UNICODE_CODEPOINT_INVALID || c > IM_UNICODE_CODEPOINT_MAX) return false;
		p += n;
		if (c >= 0x80 && !f->IsGlyphInFont((ImWchar)c)) return false;
	}
	return true;
}

inline bool primary_button(const char *label, ImVec2 size = ImVec2(0, 0)) {   /* the one amber button a view offers */
	ImGui::PushStyleColor(ImGuiCol_Button, accent); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_hi);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent); ImGui::PushStyleColor(ImGuiCol_Text, accent_ink);
	bool r = ImGui::Button(label, size);
	ImGui::PopStyleColor(4);
	return r;
}
/* a choice of n as one joined control; true when it changes. ids: labels may carry "##" suffixes.
 * (2026-11-07: wraps to the next line when the row would overflow the column -- six widescreen
 * segments clipped at 1280x800's narrower settings column before this.) */
/* (UI-03) one pad flow for every choice: up / down reach a choice ON its current value (a focus arriving from another row
 * is moved there -- it never lands on, nor picks, a neighbour by where it came from), left / right (or up / down
 * through a list that stacks) pick the value as they move, Cross on one picks it too; a mouse or a finger click as
 * before. The focus a group had last frame is kept in ImGui's state storage, keyed by the group's first label. */
/* (UI-04: this rule now is only the tabs' -- a row of tabs is where you are, not a value; every value is ui::segmented below) */
inline bool tabs_row(const char *const *labels, int n, int *v) {
	bool ch = false;
	ImGuiContext &g = *GImGui;
	const ImGuiID key = ImGui::GetID(labels[0]) ^ 0x5e6d0001u;
	ImGuiStorage *st = ImGui::GetStateStorage();
	const int prev = st->GetInt(key, -1);
	const bool pad_nav = g.NavCursorVisible && (g.NavInputSource == ImGuiInputSource_Gamepad || g.NavInputSource == ImGuiInputSource_Keyboard);
	int fi = -1; ImGuiID cur_id = 0;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * 0.25f, 0));
	const float sp = ImGui::GetStyle().ItemSpacing.x, avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
	float xnext = x0;   /* where the next button would start on this line (after Button() the cursor is at the next line's start, so track it) */
	/* (2026-11-10) a list with every value on a line of its own (a device list) keeps up / down through its values; a row of
	 * values -- also one that wraps part of it to a second line -- is one D-pad stop: left / right pick, up / down leave */
	bool one_per_line = n > 1;
	{	float xs = x0;
		for (int i = 0; i < n; i++) {
			const float w = ImGui::CalcTextSize(labels[i]).x + 2.0f * ImGui::GetStyle().FramePadding.x;
			const bool same = i && (xs + w <= x0 + avail + 0.5f);
			if (same) one_per_line = false;
			xs = (same ? xs : x0) + w + sp;
		}
	}
	const bool row_stop = pad_nav && !one_per_line;
	for (int i = 0; i < n; i++) {
		ImVec2 ts = ImGui::CalcTextSize(labels[i]);
		float w = ts.x + 2.0f * ImGui::GetStyle().FramePadding.x;
		bool same = i && (xnext + w <= x0 + avail + 0.5f);
		if (same) ImGui::SameLine();
		float placed = same ? xnext : x0;
		bool on = *v == i;
		ImGui::PushStyleColor(ImGuiCol_Button, on ? with_alpha(accent, 0.18f) : card);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? with_alpha(accent, 0.24f) : card_hi);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(accent, 0.30f));
		ImGui::PushStyleColor(ImGuiCol_Text, on ? accent : muted);
		/* (2026-11-10, NIGHT-02) only the current value is a D-pad stop: up / down always leave the choice, also when it
		 * stacks over two lines (a wrapped "Controller | Keyboard & mouse / Touch" took down onto Touch, picked it, and the
		 * drawn controller below became unreachable); left / right pick (below). A click or a tap reaches every segment. */
		const bool nonav = row_stop && !on;
		if (nonav) ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
		if (ImGui::Button(labels[i]) && !on) { *v = i; ch = true; }
		if (nonav) ImGui::PopItemFlag();
		if (ImGui::IsItemFocused()) fi = i;
		if (i == *v) cur_id = ImGui::GetItemID();
		ImGui::PopStyleColor(4);
		xnext = placed + w + sp;
	}
	ImGui::PopStyleVar();
	int keep = fi;
	const bool following = prev == -2 && cur_id && row_stop;   /* (the frame after a left / right pick: the focus follows the value) */
	if (following) { ImGui::SetFocusID(cur_id, ImGui::GetCurrentWindow()); ImGui::SetNavCursorVisible(true); keep = *v; }   /* (SetFocusID hides the nav cursor: the next press would no longer count as a pad press) */
	else if (fi >= 0 && pad_nav && !ch) {
		if (prev < 0) { if (fi != *v && cur_id) { ImGui::SetFocusID(cur_id, ImGui::GetCurrentWindow()); keep = *v; } }   /* (arrived: onto the value) */
		else if (fi != prev && fi != *v) { *v = fi; ch = true; }   /* (moved within: that value) */
	}
	if (row_stop && !ch && (following || (fi >= 0 && fi == *v))) {   /* left / right on the value: the next / previous one (no wrap), the focus stays on the choice -- also in the frame the focus is still following (a quick second press is not lost) */
		int dir = 0;
		if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dir = -1;
		if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dir = 1;
		if (dir && *v + dir >= 0 && *v + dir < n) { *v += dir; ch = true; keep = -2; ImGui::NavMoveRequestCancel(); }
		else if (dir) ImGui::NavMoveRequestCancel();   /* (at an end: stay, not off to a neighbour row) */
	}
	st->SetInt(key, keep);
	return ch;
}

/* (UI-04) a value changes only when the player says so (the owner, 2026-11-10: "wieso muss ich mit controller nicht die
 * Auswahl der Optionen mit X o.ä. bestätigen? ... ich navigiere ja durch das Menü"). With the pad (or the arrow keys):
 * moving never changes a value -- a choice, a list or a slider is ONE stop, on its current value; Cross (Enter, Space)
 * opens it for editing: the D-pad then picks (all four ways), Cross takes the pick, Circle (Esc) leaves the old
 * value; an Off / On switch flips on Cross. A mouse click or a finger picks directly, as before.
 * While editing, the control is ImGui's active item (an id of its own): ImGui's D-pad navigation stops (all four
 * directions "used" by it), ImGui's own Circle / Esc only ends it, and every "an item was busy last frame" check the
 * menus have (B goes up a level, L1 / R1 step the tabs) leaves that press to the edit. */
struct edit_t { ImGuiID id = 0; int cand = 0; float orig = 0; };
inline edit_t g_edit;
inline int g_value_focus_frame = -1;   /* the frame a value control last had the focus (the foot's hints: "Cross Change") */
inline bool editing(void) { return g_edit.id && GImGui->ActiveId == g_edit.id; }
/* (UI-05) scrolling has ONE rule: the list follows the focus only in a frame the pad / the keys moved it (ImGui's nav moved
 * it, or a control put it on an edit's pick / back on its value) -- never because of the wheel, a swipe or a click: a
 * focused item scrolled away by the wheel pulled the list back each frame (the owner: "Wieso kann man manchmal im
 * Launcher nicht scrollen?") */
inline int g_focus_set_frame = -100;
inline bool nav_just_moved(void) { return GImGui->NavJustMovedToId != 0 || g_focus_set_frame >= ImGui::GetFrameCount() - 1; }
inline void focus_set(ImGuiID id, ImGuiWindow *win) { ImGui::SetFocusID(id, win); ImGui::SetNavCursorVisible(true); g_focus_set_frame = ImGui::GetFrameCount(); }
inline bool value_focused(void) { return g_value_focus_frame == ImGui::GetFrameCount(); }
inline bool nav_by_pad(void) {   /* the focus is moved by the pad or the keys (not the mouse, not a finger) */
	const ImGuiContext &g = *GImGui;
	return g.NavCursorVisible && (g.NavInputSource == ImGuiInputSource_Gamepad || g.NavInputSource == ImGuiInputSource_Keyboard);
}
inline void edit_begin(ImGuiID id) {
	ImGuiContext &g = *GImGui; const ImGuiInputSource src = g.NavInputSource;
	g_edit.id = id;
	ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
	g.ActiveIdSource = src; g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_COUNT) - 1;
	ImGui::SetNavCursorVisible(true);
}
inline void edit_keep(void) { ImGui::KeepAliveID(g_edit.id); GImGui->ActiveIdUsingNavDirMask = (1 << ImGuiDir_COUNT) - 1; }
inline void edit_end(void) { if (GImGui->ActiveId == g_edit.id) ImGui::ClearActiveID(); g_edit.id = 0; }
inline bool edit_ok_pressed(void) {
	return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)
	    || ImGui::IsKeyPressed(ImGuiKey_Space, false);
}
inline int edit_step_pressed(bool vertical_too) {   /* -1 / +1 (held: repeats) */
	int d = 0;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) d = -1;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) d = 1;
	if (vertical_too) {
		if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) || ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) d = -1;
		if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) || ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) d = 1;
	}
	return d;
}
/* left / right on a value that isn't being edited: nothing (not off to whatever lies that way) */
inline void sideways_stays(void) {
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)
	    || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) ImGui::NavMoveRequestCancel();
}
inline void edit_frame(ImVec2 a, ImVec2 b) {   /* the mark of a control being edited: an amber frame around it */
	const float k = ImGui::GetFontSize() * 0.18f;
	ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - k, a.y - k), ImVec2(b.x + k, b.y + k), u32(accent), ImGui::GetStyle().FrameRounding + k, 0, ImMax(2.0f, k * 0.8f));
}
/* a choice of n as one joined control (wrapping to more lines, down to one value a line: a list); true when *v changes.
 * ids: labels may carry "##" suffixes. switch_: an Off / On pair -- Cross flips it, no edit (ui::toggle).
 * (2026-11-07: wraps to the next line when the row would overflow the column -- six widescreen segments clipped at
 * 1280x800's narrower settings column before this.) */
inline bool segmented(const char *const *labels, int n, int *v, bool switch_ = false) {
	ImGuiContext &g = *GImGui;
	bool ch = false;
	const ImGuiID eid = ImGui::GetID(labels[0]) ^ 0x5e6d0004u;
	bool ed = g_edit.id == eid, refocus = false;
	if (ed && g.ActiveId != eid) { g_edit.id = 0; ed = false; refocus = g.ActiveId == 0; }   /* (Circle / Esc, or a click elsewhere: the value stays as it was) */
	const bool pad = nav_by_pad() || ed;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * 0.25f, 0));
	const float sp = ImGui::GetStyle().ItemSpacing.x, avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
	if (ed) {   /* (read before drawing: this frame shows the pick) */
		edit_keep();
		const int d = edit_step_pressed(true);   /* (every direction: up / left the one before, down / right the next -- a choice that wraps reads on) */
		if (d && g_edit.cand + d >= 0 && g_edit.cand + d < n) g_edit.cand += d;
		if (edit_ok_pressed()) {
			if (g_edit.cand != *v) { *v = g_edit.cand; ch = true; }
			edit_end(); ed = false;
		}
	}
	const int cand = ed ? g_edit.cand : *v;
	float xnext = x0;
	int fi = -1; ImGuiID cur_id = 0, cand_id = 0; ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
	bool nav_press = false; int clicked = -1;
	ImGuiWindow *win = ImGui::GetCurrentWindow();
	for (int i = 0; i < n; i++) {
		const float w = ImGui::CalcTextSize(labels[i], nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
		const bool same = i && (xnext + w <= x0 + avail + 0.5f);
		if (same) ImGui::SameLine();
		const float placed = same ? xnext : x0;
		const bool on = *v == i, pick = ed && cand == i;
		ImGui::PushStyleColor(ImGuiCol_Button, pick ? accent : on ? with_alpha(accent, 0.18f) : card);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pick ? accent_hi : on ? with_alpha(accent, 0.24f) : card_hi);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, pick ? accent : with_alpha(accent, 0.30f));
		ImGui::PushStyleColor(ImGuiCol_Text, pick ? accent_ink : on ? accent : muted);
		/* only the current value is a stop for the pad: the choice is one row, also a list or one that wraps (UI-03, NIGHT-02) */
		const bool nonav = pad && !ed && !on;
		if (nonav) ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
		if (ImGui::Button(labels[i])) {
			if (g.ActiveId == ImGui::GetItemID() && g.ActiveIdSource != ImGuiInputSource_Mouse) nav_press = true;   /* (Cross / Enter: pressed by the nav) */
			else clicked = i;   /* (a click or a tap: that value) */
		}
		if (nonav) ImGui::PopItemFlag();
		if (ImGui::IsItemFocused()) fi = i;
		if (on) cur_id = ImGui::GetItemID();
		if (i == cand) cand_id = ImGui::GetItemID();
		/* the pad's focus: on the pick while editing; back on the value after an edit */
		if ((ed && i == cand && g.NavId != cand_id) || (!ed && refocus && on && g.NavId != cur_id)) focus_set(ImGui::GetItemID(), win);
		mn = ImMin(mn, ImGui::GetItemRectMin()); mx = ImMax(mx, ImGui::GetItemRectMax());
		ImGui::PopStyleColor(4);
		xnext = placed + w + sp;
	}
	ImGui::PopStyleVar();
	if (clicked >= 0) {
		if (ed) { edit_end(); ed = false; }
		if (clicked != *v) { *v = clicked; ch = true; }
	} else if (nav_press && !ch) {
		if (switch_) { *v = *v ? 0 : 1; ch = true; }   /* (an Off / On switch: Cross flips it; the focus moves onto the new value next frame) */
		else { g_edit.cand = *v; g_edit.orig = (float)*v; edit_begin(eid); ed = true; }
	}
	if (ed) edit_frame(mn, mx);   /* the mark of the edit */
	else if (pad && cur_id && !ch && fi >= 0 && fi != *v) focus_set(cur_id, win);   /* (a focus that came in sideways, or stayed on a flipped switch's old value: onto the value) */
	if (fi >= 0 || ed) {
		g_value_focus_frame = ImGui::GetFrameCount();
		if (!ed && pad) sideways_stays();
	}
	return ch;
}
/* (UI-03) in a scrolling list of rows (call it last, inside its window): the focused control is kept in view with a margin
 * of rows below and above it, so a D-pad press always moves to the next one -- with the next one out of view, ImGui
 * spent the press scrolling to it first, and the player pressed twice (the walk test's "stalls"). (UI-05: only in a frame
 * the focus moved by the pad / the keys -- nav_just_moved -- so the wheel and a swipe scroll freely) */
inline void nav_keep_visible(float margin_rows = 2.5f) {
	ImGuiContext &g = *GImGui; ImGuiWindow *w = ImGui::GetCurrentWindow();
	if (!g.NavId || g.NavWindow != w || !g.NavCursorVisible || !nav_just_moved()) return;
	const ImRect r = ImGui::WindowRectRelToAbs(w, w->NavRectRel[0]);
	const float m = ImGui::GetFrameHeightWithSpacing() * margin_rows, top = w->InnerClipRect.Min.y, bot = w->InnerClipRect.Max.y;
	if (r.Max.y + m > bot) ImGui::SetScrollY(w, ImMin(w->ScrollMax.y, w->Scroll.y + (r.Max.y + m - bot)));
	else if (r.Min.y - m < top) ImGui::SetScrollY(w, ImMax(0.0f, w->Scroll.y - (top - (r.Min.y - m))));
}
/* (UI-03) the focus to the next control submitted, as SetKeyboardFocusHere() but without taking it: ImGui's own call
 * activates what it lands on, and a slider landed on that way opened for typing (B then only left the typing) */
inline void focus_next_item(void) {
	ImGuiContext &g = *GImGui;
	ImGui::SetNavWindow(g.CurrentWindow);
	ImGui::NavMoveRequestSubmit(ImGuiDir_None, ImGuiDir_Down, ImGuiNavMoveFlags_IsTabbing | ImGuiNavMoveFlags_FocusApi, ImGuiScrollFlags_KeepVisibleEdgeX | ImGuiScrollFlags_KeepVisibleEdgeY);
	g.NavTabbingDir = 1; g.NavTabbingCounter = 1;
}
/* (UI-03) down from the tabs enters the rows at their first control, whatever is in view (at the top of a list, before its
 * first item): ImGui's nearest-below rule took a page whose first control was out of view straight to the foot */
inline void tabs_down_enters(bool tab_had_focus) {
	if (tab_had_focus && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false) || ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))) focus_next_item();
}
/* (UI-04) a slider: one stop for the pad, Cross opens it for editing -- left / right (the arrow keys) step it by a twentieth of
 * its range (a logarithmic one by 10 %), the game sees each step; Cross keeps the value, Circle (Esc) puts the old one
 * back. A mouse or a finger drags it as before. edit_done(): the frame an edit by the pad ended (kept or put back) -- the
 * launcher writes its file then, as at the end of a drag. */
inline bool g_edit_done = false;
inline bool edit_done(void) { return g_edit_done; }
inline bool slider_float(const char *label, float *v, float lo, float hi, const char *fmt = "%.3f", ImGuiSliderFlags fl = 0) {
	ImGuiContext &g = *GImGui;
	bool ch = false;
	const ImGuiID sid = ImGui::GetID(label), eid = sid ^ 0x5e6d0005u;
	bool ed = g_edit.id == eid;
	g_edit_done = false;
	if (ed && g.ActiveId != eid) {   /* Circle / Esc (or a click elsewhere): the old value back -- not when the mouse took the slider itself */
		if (g.ActiveId != sid && *v != g_edit.orig) { *v = g_edit.orig; ch = true; }
		g_edit.id = 0; ed = false; g_edit_done = true;
	}
	if (ed) {
		edit_keep();
		if (const int d = edit_step_pressed(false)) {
			float nv = (fl & ImGuiSliderFlags_Logarithmic) && *v > 0 ? *v * (d > 0 ? 1.1f : 1.0f / 1.1f) : *v + d * (hi - lo) / 20.0f;
			nv = nv < lo ? lo : nv > hi ? hi : nv;
			if (nv != *v) { *v = nv; ch = true; }
		}
		if (edit_ok_pressed()) { edit_end(); ed = false; g_edit_done = true; }
	}
	const float before = *v;
	const bool pad = nav_by_pad() || ed;
	ch |= ImGui::SliderFloat(label, v, lo, hi, fmt, fl);
	if (!ed && pad && g.ActiveId == sid && g.ActiveIdSource != ImGuiInputSource_Mouse) {   /* Cross (Enter) took it: an edit instead of ImGui's own */
		if (*v != before) { *v = before; ch = false; }
		g_edit.orig = before; edit_begin(eid); ed = true;
	}
	if (ed) edit_frame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	if (ImGui::IsItemFocused() || ed) {
		g_value_focus_frame = ImGui::GetFrameCount();
		if (!ed && pad) sideways_stays();
	}
	return ch;
}
inline bool toggle(const char *id, bool *on) {   /* Off / On as a segmented pair (U12b: the catalog's words) */
	char a[64], b[64]; snprintf(a, sizeof a, "%s##%s", ui_tr("common.off"), id); snprintf(b, sizeof b, "%s##%s", ui_tr("common.on"), id);
	const char *const ls[2] = {a, b};
	int v = *on ? 1 : 0;
	if (!segmented(ls, 2, &v, true)) return false;
	*on = v != 0;
	return true;
}
inline void heading(const char *s, float size) { ImGui::PushFont(nullptr, size); ImGui::TextUnformatted(s); ImGui::PopFont(); }
inline void muted_wrapped(const char *s) { ImGui::PushStyleColor(ImGuiCol_Text, muted); ImGui::TextWrapped("%s", s); ImGui::PopStyleColor(); }
inline void dot(const ImVec4 &c) {   /* a status dot, centred on the text line */
	ImVec2 p = ImGui::GetCursorScreenPos(); float h = ImGui::GetTextLineHeight(), r = h * 0.22f;
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, u32(c));
	ImGui::Dummy(ImVec2(r * 2, h)); ImGui::SameLine(0, h * 0.35f);
}
inline void spinner(const ImVec4 &c) {
	ImVec2 p = ImGui::GetCursorScreenPos(); float h = ImGui::GetTextLineHeight(), r = h * 0.36f, t = (float)ImGui::GetTime() * 6.0f;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->PathArcTo(ImVec2(p.x + r, p.y + h * 0.5f), r, t, t + 4.4f, 20); dl->PathStroke(u32(c), 0, r * 0.32f);
	ImGui::Dummy(ImVec2(r * 2, h)); ImGui::SameLine(0, h * 0.4f);
}
inline void chip(const char *s, const ImVec4 &c) {   /* a small rounded label, tinted */
	ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(s); float px = ImGui::GetFontSize() * 0.55f, py = ImGui::GetFontSize() * 0.18f;
	ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ts.x + px * 2, p.y + ts.y + py * 2), u32(c, 0.16f), ts.y);
	ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + px, p.y + py), u32(c), s);
	ImGui::Dummy(ImVec2(ts.x + px * 2, ts.y + py * 2));
}

/* settings rows: rows_begin, then per setting row(label, explanation) and its control(s), then rows_end.
 * (U10) two_pane: a wide window shows the rows left and the hovered row's help right (build_ui decides; the rows
 * then carry no inline explanation). help_label/help_text are what the right pane shows -- the page's first row's
 * help until a row is hovered or its control focused, then sticky. */
inline bool two_pane = false;
inline const char *help_label = nullptr, *help_text = nullptr;
inline const char *r_prev_label = nullptr, *r_prev_help = nullptr;
inline float r_y0 = 0, r_x0 = 0, r_x1 = 0;
inline void row_finish(void) {   /* the row above is complete: does it want the right pane? */
	if (!two_pane || !r_prev_label) return;
	if (ImGui::IsMouseHoveringRect(ImVec2(r_x0, r_y0), ImVec2(r_x1, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().CellPadding.y))
	    || ImGui::IsItemFocused()) {   /* (the last item is that row's control) */
		help_label = r_prev_label; help_text = r_prev_help;
	}
	r_prev_label = nullptr;
}
inline bool rows_begin(const char *id) {
	r_x0 = ImGui::GetCursorScreenPos().x; r_x1 = r_x0 + ImGui::GetContentRegionAvail().x;
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) return false;
	ImGui::TableSetupColumn("setting", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 1.15f);
	return true;
}
inline void row(const char *label, const char *explanation = nullptr, const char *note = nullptr) {
	row_finish();   /* (U10) */
	ImGui::TableNextRow(0, ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2);
	r_y0 = ImGui::GetCursorScreenPos().y;
	ImGui::TableNextColumn();
	ImGui::TextUnformatted(label);
	if (note && *note) {   /* (U7: a small muted mark -- "default", "next scene", "after restart") */
		ImGui::SameLine(0, ImGui::GetFontSize() * 0.35f);
		ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.82f);
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		ImGui::TextUnformatted(note);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	if (explanation && *explanation) {
		if (two_pane) { if (!help_label) { help_label = label; help_text = explanation; } }
		else { ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.88f); muted_wrapped(explanation); ImGui::PopFont(); }
	}
	ImGui::TableNextColumn();
	r_prev_label = label; r_prev_help = explanation;
}
inline void rows_end(void) { row_finish(); ImGui::EndTable(); }
/* (U10) the right pane's content: the hovered row's label and its help */
inline void help_pane(void) {
	if (help_label) { ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.1f); ImGui::TextWrapped("%s", help_label); ImGui::PopFont(); ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.35f)); }
	if (help_text) { ImGui::PushStyleColor(ImGuiCol_Text, muted); ImGui::TextWrapped("%s", help_text); ImGui::PopStyleColor(); }
}

/* the logo, drawn (assets/brand/make.py has the same geometry as SVG). wordmark: "nativ" in lowercase monoline, the n
 * in the text colour and "ativ" (Vita, backwards) in amber, `height` px tall (its units: x -6..282, y 14..110), 3x as
 * wide; returns the width */
inline float wordmark(ImDrawList *dl, ImVec2 at, float height, ImU32 n = 0, ImU32 ativ = 0) {
	if (!n) n = u32(text);
	if (!ativ) ativ = u32(accent);
	const float s = height / 96.0f, w = 14 * s;
	auto P = [&](float x, float y) { return ImVec2(at.x + (x + 6) * s, at.y + (y - 14) * s); };
	auto cap = [&](float x, float y, ImU32 c) { dl->AddCircleFilled(P(x, y), w * 0.5f, c); };
	auto line = [&](float x0, float y0, float x1, float y1, ImU32 c) { dl->AddLine(P(x0, y0), P(x1, y1), c, w); cap(x0, y0, c); cap(x1, y1, c); };
	line(4, 46, 4, 100, n);                                                                  /* n */
	dl->PathLineTo(P(4, 72)); dl->PathBezierCubicCurveTo(P(4, 54), P(14, 46), P(26, 46)); dl->PathBezierCubicCurveTo(P(38, 46), P(48, 54), P(48, 72));
	dl->PathLineTo(P(48, 100)); dl->PathStroke(n, 0, w); cap(48, 100, n);
	dl->AddCircle(P(101, 73), 27 * s, ativ, 0, w); line(128, 46, 128, 100, ativ);            /* a */
	dl->PathLineTo(P(161, 24)); dl->PathLineTo(P(161, 84)); dl->PathBezierQuadraticCurveTo(P(161, 100), P(177, 100));   /* t */
	dl->PathStroke(ativ, 0, w); cap(161, 24, ativ); cap(177, 100, ativ); line(149, 46, 175, 46, ativ);
	line(203, 46, 203, 100, ativ); dl->AddCircleFilled(P(203, 25), 8.4f * s, ativ);         /* i */
	dl->PathLineTo(P(225, 46)); dl->PathLineTo(P(242, 92)); dl->PathBezierQuadraticCurveTo(P(247, 104), P(252, 92)); dl->PathLineTo(P(269, 46));   /* v */
	dl->PathStroke(ativ, 0, w); cap(225, 46, ativ); cap(269, 46, ativ);
	return 288 * s;
}
/* the Vita in outline, `width` px wide (its units 600 x 280): the screen filled `screen`; controls 0 none, 1 the
 * sticks as dots (small sizes), 2 d-pad left, face buttons right, the two sticks; returns the height */
inline float vita(ImDrawList *dl, ImVec2 at, float width, ImU32 line, ImU32 screen, float body_w = 14, int controls = 2) {
	const float s = width / 600.0f;
	auto P = [&](float x, float y) { return ImVec2(at.x + x * s, at.y + y * s); };
	dl->AddRectFilled(P(125, 41), P(475, 239), screen, 12 * s);
	dl->AddRect(P(8, 8), P(592, 272), line, 132 * s, 0, body_w * s);
	if (controls == 1) { dl->AddCircleFilled(P(70, 140), 22 * s, line); dl->AddCircleFilled(P(530, 140), 22 * s, line); }
	if (controls == 2) {
		const float k = 12 * s;
		dl->AddLine(P(50, 108), P(82, 108), line, k); dl->AddLine(P(66, 92), P(66, 124), line, k);
		for (ImVec2 c : {P(50, 108), P(82, 108), P(66, 92), P(66, 124)}) dl->AddCircleFilled(c, k * 0.5f, line);
		dl->AddCircle(P(74, 190), 14 * s, line, 0, 10 * s); dl->AddCircle(P(526, 190), 14 * s, line, 0, 10 * s);
		for (ImVec2 c : {P(534, 90), P(516, 108), P(552, 108), P(534, 126)}) dl->AddCircleFilled(c, 8 * s, line);
	}
	return 280 * s;
}
/* the wordmark as a layout item, `height` px tall, on the current line */
inline void wordmark_item(float height) {
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(wordmark(ImGui::GetWindowDrawList(), p, height), height));
}
}   /* namespace ui */
#endif
