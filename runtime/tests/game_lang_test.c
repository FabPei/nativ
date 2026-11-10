/* game_lang_test.c -- GAME-LANG: the Vita system language a game sees (runtime/core/game_lang.h), alone:
 *
 * - every key of the setting game_lang names one Vita language (SceSystemParamLang), and the Vita's number works too;
 * - "auto" follows the host locale: de_AT -> German, en_GB -> English (UK), en_US -> English (US), pt_BR / pt_PT,
 *   zh_TW / zh_CN, nb -> Norwegian, a language the Vita doesn't have -> English (US);
 * - RECOMP_GAME_LANG wins over the setting, a scripted run (headless) with "auto" stays English (US).
 *
 * Fail-before: apputil.c answered 1 (English US) for every game and every setting; see system_lang_test.c for that
 * check on apputil.c itself. Build: README.md here.
 */
#include <stdio.h>
#include "../core/game_lang.h"

static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	int ids[20] = {0}, dup = 0;
	for (int i = 0; i < GAME_LANG_N; i++) { if (game_langs[i].id < 0 || game_langs[i].id > 19 || ids[game_langs[i].id]++) dup = 1; }
	check("20 languages, each Vita number 0..19 once", GAME_LANG_N == 20 && !dup);
	check("key de -> 4, en_gb -> 18, ja -> 0, zh_hant -> 10", game_lang_from_key("de") == 4 && game_lang_from_key("en_gb") == 18 &&
	      game_lang_from_key("ja") == 0 && game_lang_from_key("zh_hant") == 10);
	check("the Vita's number as the value: \"4\" -> 4, \"19\" -> 19", game_lang_from_key("4") == 4 && game_lang_from_key("19") == 19);
	check("auto, empty, an unknown key or number -> -1 (not a language)", game_lang_from_key("auto") == -1 && game_lang_from_key("") == -1 &&
	      game_lang_from_key("xx") == -1 && game_lang_from_key("20") == -1 && game_lang_from_key(NULL) == -1);
	check("locale de_AT -> German (4)", game_lang_from_locale("de", "AT") == 4);
	check("locale en_GB -> English UK (18), en_US and en alone -> English US (1)", game_lang_from_locale("en", "GB") == 18 &&
	      game_lang_from_locale("en", "US") == 1 && game_lang_from_locale("en", NULL) == 1);
	check("locale pt_BR -> 17, pt_PT -> 7", game_lang_from_locale("pt", "BR") == 17 && game_lang_from_locale("pt", "PT") == 7);
	check("locale zh_TW -> Traditional (10), zh_CN -> Simplified (11)", game_lang_from_locale("zh", "TW") == 10 && game_lang_from_locale("zh", "CN") == 11);
	check("locale nb / nn -> Norwegian (15), FR (upper case) -> French (2)", game_lang_from_locale("nb", "NO") == 15 &&
	      game_lang_from_locale("nn", NULL) == 15 && game_lang_from_locale("FR", "fr") == 2);
	check("a language the Vita lacks (cs) or none -> English US (1)", game_lang_from_locale("cs", "CZ") == 1 && game_lang_from_locale(NULL, NULL) == 1);
	check("setting de -> 4, whatever the locale", game_lang_resolve(NULL, "de", 0, "ja", "JP") == 4);
	check("setting auto -> the locale (fr_FR -> 2)", game_lang_resolve(NULL, "auto", 0, "fr", "FR") == 2);
	check("RECOMP_GAME_LANG wins over the setting (ja over de)", game_lang_resolve("ja", "de", 0, "fr", "FR") == 0);
	check("an unusable RECOMP_GAME_LANG is ignored (setting it)", game_lang_resolve("klingon", "it", 0, NULL, NULL) == 5);
	check("headless + auto -> English US, not the machine's locale", game_lang_resolve(NULL, "auto", 1, "de", "DE") == 1);
	check("headless + a chosen language -> that language", game_lang_resolve(NULL, "es", 1, "de", "DE") == 3);
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
