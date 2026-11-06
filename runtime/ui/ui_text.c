/* ui_text.c — the menu's string catalog (U12a). See ui_text.h.
 *
 * The seed below is real menu text (the foot and the Help page's About); U12b moves the overlay's strings in page
 * by page and keeps the German with them. */
#include <string.h>
#include "ui_text.h"

static int lang_de;
const char *ui_text_lang(void) { return lang_de ? "de" : "en"; }
void ui_text_lang_load(const char *pref, const char *locale) {
	if (pref && !strcmp(pref, "de")) lang_de = 1;
	else if (pref && !strcmp(pref, "en")) lang_de = 0;
	else lang_de = locale && locale[0] == 'd' && locale[1] == 'e'
	     && (locale[2] == 0 || locale[2] == '-' || locale[2] == '_');   /* auto (or an unknown preference): the locale */
}
typedef struct { const char *id, *en, *de; } Str;
static const Str catalog[] = {
	{"foot.capture", "Capture frame (F12)", "Bild aufnehmen (F12)"},
	{"foot.hint", "Esc, F10 or the PS button closes this menu", "Esc, F10 oder die PS-Taste schließt dieses Menü"},
	{"foot.save_failed", "Settings could not be saved", "Einstellungen konnten nicht gespeichert werden"},
	{"help.about.body", "Project Nativ: this PS Vita game, statically recompiled to native code, running on this computer's own CPU and GPU.",
	 "Project Nativ: Dieses PS-Vita-Spiel, statisch in nativen Code übersetzt, läuft auf der CPU und GPU dieses Computers."},
};
enum { N = (int)(sizeof catalog / sizeof *catalog) };
const char *ui_tr(const char *id) {
	for (int i = 0; i < N; i++) if (!strcmp(id, catalog[i].id)) {
		const char *s = lang_de ? catalog[i].de : catalog[i].en;
		return s && *s ? s : catalog[i].en;
	}
	return id;
}
int ui_text_count(void) { return N; }
void ui_text_at(int i, const char **id, const char **en, const char **de) {
	if (i < 0 || i >= N) { if (id) *id = ""; if (en) *en = ""; if (de) *de = ""; return; }
	if (id) *id = catalog[i].id;
	if (en) *en = catalog[i].en;
	if (de) *de = catalog[i].de;
}
