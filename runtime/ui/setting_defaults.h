#pragma once
/* the shared settings keys' defaults where a game reads no value (runtime/core/settings.h's keys; the consumers' own).
 * One table for both consumers (UI QUEUE U7 moved it out of launcher/hub.cpp): the launcher's per-game settings
 * ("(shared: …)" when a game file holds no value) and the in-game menu's "default" marks on rows (overlay.cpp).
 * The values must not change -- the launcher's display depends on them. */
#include <cstring>
#include <string>
inline const char *setting_default(const char *k) {
	static const struct { const char *k, *v; } d[] = {
		{"render_scale", "1"}, {"fullscreen", "0"}, {"aspect_ratio", "native"}, {"hud_layout", "edges"},
		{"aspect", "fit"}, {"frame_rate", "30"}, {"post_aa", "1"}, {"sharpen", "0.35"}, {"anisotropy", "16"},
		{"shadow_filter", "1"}, {"full_res_world", "1"}, {"ssao", "1"}, {"ssao_strength", "1"}, {"osd", "0"},
		{"input_mode", "auto"}, {"mouse_look", "1"}, {"raw_look", "1"}, {"mouse_sens", "1"},
		{"mouse_invert", "0"}, {"shoulder_buttons", "triggers"}, {"touch_cursor_speed", "1"}, {"osb", "auto"}, {"osb_size", "1"}, {"gyro", "1"},
		{"gyro_pose", "upright"}, {"gyro_sens", "1"}, {"stick_l_sens", "1"}, {"stick_l_dead", "0"}, {"stick_r_sens", "1"}, {"stick_r_dead", "0"}, {"volume", "1"}, {"online_id", ""}, {"online", "1"},
		{"camera", "light"}, {"save_dialog", "ask"}, {"audio_sync_trim_ms", "0"}, {"audio_buffers", "1"}, {"game_lang", "auto"}};
	for (const auto &e : d) if (!strcmp(e.k, k)) return e.v;
	return nullptr;
}
inline const char *setting_default(const std::string &k) { return setting_default(k.c_str()); }
