/*
 * hub_scale.h - the launcher's UI scale (UI-01h): the larger of the display's own scale (Windows' 125 / 150 / 200 %) and
 * one that grows with the window's height in pixels -- 800 lines (a Steam Deck) and less are 1, 1080 is 1.35, a 4K TV's
 * 2160 is 2.7, at most 3 -- so a big screen across a room gets text that size, not 16 px on 2160 lines.
 * The in-game menu does the same with its own base (runtime/ui/ui_scale.h: 720 lines). Plain C, header-only:
 * launcher/tests/hub_scale_test.c checks it.
 */
#ifndef ATIV_HUB_SCALE_H
#define ATIV_HUB_SCALE_H
static inline float hub_ui_scale(float window_h_px, float display_scale) {
	float k = window_h_px / 800.0f;
	if (!(k > 1.0f)) k = 1.0f;
	if (k > 3.0f) k = 3.0f;
	if (!(display_scale > 0)) display_scale = 1.0f;
	return display_scale > k ? display_scale : k;
}
#endif
