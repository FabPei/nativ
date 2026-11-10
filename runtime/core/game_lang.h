/*
 * GAME-LANG: the language a game reads as the Vita's system language (sceAppUtilSystemParamGetInt, id 1 =
 * SCE_SYSTEM_PARAM_ID_LANG). Most games pick their text and voice language from it once, at boot.
 *
 * The setting game_lang (settings.h) holds one of the keys below, or "auto" (the default): the host's preferred
 * locale (SDL_GetPreferredLocales), English (US) when it matches no Vita language. RECOMP_GAME_LANG (a key or the
 * Vita's number) wins over the setting; a scripted run (RECOMP_HEADLESS) with "auto" stays English (US), so the gate's
 * frames don't depend on the machine's locale.
 *
 * The numbers are SceSystemParamLang (vita-headers psp2/system_param.h). Header only: no SDL, no guest memory --
 * apputil.c passes the host locale in; runtime/tests/game_lang_test.c checks it alone.
 */
#ifndef GAME_LANG_H
#define GAME_LANG_H
#include <stdlib.h>
#include <string.h>

typedef struct { const char *key; int id; const char *name; } GameLang;
/* the menu's order: English first, the European languages, then the rest (names drawable in the menu's Latin-1 font) */
static const GameLang game_langs[] = {
	{"en_us", 1, "English (US)"}, {"en_gb", 18, "English (UK)"}, {"de", 4, "Deutsch"}, {"fr", 2, "Fran\xc3\xa7" "ais"},
	{"es", 3, "Espa\xc3\xb1ol"}, {"it", 5, "Italiano"}, {"nl", 6, "Nederlands"}, {"pt", 7, "Portugu\xc3\xaas"},
	{"pt_br", 17, "Portugu\xc3\xaas (Brasil)"}, {"pl", 16, "Polski"}, {"fi", 12, "Suomi"}, {"sv", 13, "Svenska"},
	{"da", 14, "Dansk"}, {"no", 15, "Norsk"}, {"tr", 19, "T\xc3\xbcrk\xc3\xa7" "e"}, {"ru", 8, "Russian"},
	{"ja", 0, "Japanese"}, {"ko", 9, "Korean"}, {"zh_hans", 11, "Chinese (Simplified)"}, {"zh_hant", 10, "Chinese (Traditional)"},
};
#define GAME_LANG_N ((int)(sizeof game_langs / sizeof game_langs[0]))
#define GAME_LANG_DEFAULT 1   /* English (US) */

/* a key ("de") or the Vita's number ("4") -> the number; -1 when it is neither */
static inline int game_lang_from_key(const char *k) {
	if (!k || !*k) return -1;
	for (int i = 0; i < GAME_LANG_N; i++) if (!strcmp(k, game_langs[i].key)) return game_langs[i].id;
	char *end; long n = strtol(k, &end, 10);
	if (!*end) for (int i = 0; i < GAME_LANG_N; i++) if (game_langs[i].id == n) return (int)n;
	return -1;
}
static inline int game_lang_eq(const char *a, const char *b) {   /* ASCII, case-insensitive */
	if (!a || !b) return 0;
	for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; }
	return !*a && !*b;
}
/* a host locale (SDL: language "de", country "AT" or NULL) -> the Vita's number; GAME_LANG_DEFAULT when none fits */
static inline int game_lang_from_locale(const char *lang, const char *country) {
	if (!lang || !*lang) return GAME_LANG_DEFAULT;
	const char *c = country ? country : "";
	if (game_lang_eq(lang, "en")) {
		static const char *const uk[] = {"GB", "IE", "AU", "NZ", "IN", "ZA", "SG", "HK", "MT"};
		for (unsigned i = 0; i < sizeof uk / sizeof uk[0]; i++) if (game_lang_eq(c, uk[i])) return 18;
		return 1;
	}
	if (game_lang_eq(lang, "pt")) return game_lang_eq(c, "BR") ? 17 : 7;
	if (game_lang_eq(lang, "zh")) return game_lang_eq(c, "TW") || game_lang_eq(c, "HK") || game_lang_eq(c, "MO") ? 10 : 11;
	if (game_lang_eq(lang, "nb") || game_lang_eq(lang, "nn") || game_lang_eq(lang, "no")) return 15;
	static const struct { const char *l; int id; } one[] = {{"ja", 0}, {"fr", 2}, {"es", 3}, {"de", 4}, {"it", 5}, {"nl", 6},
		{"ru", 8}, {"ko", 9}, {"fi", 12}, {"sv", 13}, {"da", 14}, {"pl", 16}, {"tr", 19}};
	for (unsigned i = 0; i < sizeof one / sizeof one[0]; i++) if (game_lang_eq(lang, one[i].l)) return one[i].id;
	return GAME_LANG_DEFAULT;
}
/* the language a game sees: env (RECOMP_GAME_LANG) > setting > "auto" (headless: English (US); else the locale) */
static inline int game_lang_resolve(const char *env, const char *setting, int headless, const char *lang, const char *country) {
	int v = game_lang_from_key(env);
	if (v >= 0) return v;
	v = game_lang_from_key(setting);
	if (v >= 0) return v;
	return headless ? GAME_LANG_DEFAULT : game_lang_from_locale(lang, country);
}
#endif
