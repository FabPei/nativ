/* ui_text_test.c — U12a: the string catalog, the language decision and the font's glyph range check.
 *
 * Build in WSL (literal flags -- the tool layer eats $VARS):
 *   cc -O1 -g -Wall -I ../ui ui_text_test.c ../ui/ui_text.c -o /tmp/ui_text_test && /tmp/ui_text_test
 */
#include <stdio.h>
#include <string.h>
#include "ui_text.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
	/* the language decision: an explicit preference wins; otherwise the locale's prefix */
	ui_text_lang_load("de", "en-US"); CHECK(!strcmp(ui_text_lang(), "de"));
	ui_text_lang_load("en", "de-DE"); CHECK(!strcmp(ui_text_lang(), "en"));
	ui_text_lang_load("auto", "de-DE"); CHECK(!strcmp(ui_text_lang(), "de"));
	ui_text_lang_load("auto", "de"); CHECK(!strcmp(ui_text_lang(), "de"));
	ui_text_lang_load("auto", "de_AT"); CHECK(!strcmp(ui_text_lang(), "de"));
	ui_text_lang_load("auto", "en-US"); CHECK(!strcmp(ui_text_lang(), "en"));
	ui_text_lang_load("auto", "fr-FR"); CHECK(!strcmp(ui_text_lang(), "en"));
	ui_text_lang_load("auto", NULL); CHECK(!strcmp(ui_text_lang(), "en"));
	ui_text_lang_load(NULL, "de-AT"); CHECK(!strcmp(ui_text_lang(), "de"));
	ui_text_lang_load("bogus", "de"); CHECK(!strcmp(ui_text_lang(), "de"));   /* an unknown preference: the locale */
	ui_text_lang_load("bogus", NULL); CHECK(!strcmp(ui_text_lang(), "en"));

	/* lookups: English and German; an unknown id shows itself */
	ui_text_lang_load("en", NULL);
	CHECK(!strcmp(ui_tr("foot.capture"), "Capture frame (F12)"));
	CHECK(!strcmp(ui_tr("foot.save_failed"), "Settings could not be saved"));
	CHECK(!strcmp(ui_tr("nope.not_here"), "nope.not_here"));
	ui_text_lang_load("de", NULL);
	CHECK(!strcmp(ui_tr("foot.capture"), "Bild aufnehmen (F12)"));
	CHECK(!strcmp(ui_tr("foot.save_failed"), "Einstellungen konnten nicht gespeichert werden"));
	CHECK(!strcmp(ui_tr("nope.not_here"), "nope.not_here"));

	/* the glyph range check: the font is built with GetGlyphRangesDefault (Basic Latin + Latin-1), so every
	 * catalog character must decode below U+0100 -- a Japanese or Cyrillic string fails here */
	CHECK(ui_text_count() > 0);
	for (int i = 0; i < ui_text_count(); i++) {
		const char *id, *en, *de;
		ui_text_at(i, &id, &en, &de);
		CHECK(id && *id);
		for (const char *s = en; s && *s; ) {
			unsigned cp = (unsigned char)*s;
			if (cp < 0x80) s++;
			else if ((cp & 0xE0) == 0xC0 && s[1]) { cp = ((cp & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F); s += 2; }
			else { CHECK(0); break; }   /* a 3+-byte sequence is outside Latin-1 by construction */
			CHECK(cp < 0x100);
		}
		for (const char *s = de; s && *s; ) {
			unsigned cp = (unsigned char)*s;
			if (cp < 0x80) s++;
			else if ((cp & 0xE0) == 0xC0 && s[1]) { cp = ((cp & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F); s += 2; }
			else { CHECK(0); break; }
			CHECK(cp < 0x100);
		}
	}

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
