/* ui_text.h — the menu's string catalog (U12a).
 *
 * ids + `ui_tr(id)`; English is the code's own text. The language: the `menu_lang` setting ("auto" = the system's
 * preference, "en", "de"). The language decision is pure (the setting plus a locale string) so the test can drive
 * it; the caller reads the setting and SDL_GetPreferredLocales. Every catalog string stays inside the font's
 * glyph range (Latin-1: ui_theme.h builds the font with GetGlyphRangesDefault) -- ui_text_test asserts it. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
const char *ui_tr(const char *id);                              /* the string in the language in effect; an
                                                                 * unknown id shows itself (a missing string is
                                                                 * visible, not silent) */
void ui_text_lang_load(const char *pref, const char *locale);   /* "menu_lang" + a locale like "de_DE" or "en-US" */
const char *ui_text_lang(void);                                 /* the language in effect: "en" or "de" */
int ui_text_count(void);
void ui_text_at(int i, const char **id, const char **en, const char **de);   /* the catalog, for the test */
#ifdef __cplusplus
}
#endif
