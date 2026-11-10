/* savedlg_ui.h -- the save-data dialog's UI (savedlg_ui.cpp): builds the dialog's ImGui step and takes the
 * player's answers (apputil.c through savedlg.h). The pad's buttons and left stick are fed into ImGui's key
 * state through here (SAVE-DLG-01).
 */
#ifndef RECOMP_SAVEDLG_UI_H
#define RECOMP_SAVEDLG_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* draw + handle the current dialog step (savedlg_view/savedlg_answer) */
void savedlg_ui_build(float w, float hgt);

/* one pad button / axis from the host's own pad (window.c's SDL gamepad): becomes the matching ImGui gamepad
 * key, which the dialog reads like any other key (sdl_button/sdl_axis: SDL_GAMEPAD_BUTTON_* / SDL_GAMEPAD_AXIS_*) */
void savedlg_ui_gamepad_button(int sdl_button, int down);
void savedlg_ui_gamepad_axis(int sdl_axis, int value);
void savedlg_ui_gamepad_frame(int pad_connected);   /* (U12c) after the backend frame, before ImGui::NewFrame */
/* the host stops (or starts) routing the keys and the pad here -- the menu or a dialog closed (or opened): every key and
 * button is let go in ImGui's state. A release that went to the game instead would otherwise leave its key held here,
 * and the next dialog step would never arm (a Steam Deck report: Cross on the menu's Resume, then the save dialog) */
void savedlg_ui_input_lost(void);

#ifdef __cplusplus
}
#endif
#endif
