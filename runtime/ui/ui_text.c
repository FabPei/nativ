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
	/* shared marks (the U7 row marks) */
	{"mark.default", "default", "Standard"},
	{"mark.next_scene", "next scene", "nächste Szene"},
	{"mark.after_restart", "after restart", "nach Neustart"},
	{"common.off", "Off", "Aus"},
	{"common.on", "On", "Ein"},
	/* the menu chrome: nav, header, foot */
	{"nav.display", "Display", "Anzeige"},
	{"nav.performance", "Performance", "Leistung"},
	{"nav.game", "Game", "Spiel"},
	{"nav.controls", "Controls", "Steuerung"},
	{"nav.help", "Help", "Hilfe"},
	{"blurb.display", "Resolution, picture and image quality", "Auflösung, Bild und Bildqualität"},
	{"blurb.performance", "Frame rate and on-screen stats", "Bildrate und Statistiken auf dem Bildschirm"},
	{"blurb.game", "Frame rate, view and detail options, volume", "Bildrate, Sicht und Detailoptionen, Lautstärke"},
	{"blurb.controls", "Keyboard, mouse, controller, gyro and camera", "Tastatur, Maus, Controller, Gyroskop und Kamera"},
	{"blurb.help", "Shortcuts and about", "Tastenkürzel und Über"},
	{"header.paused", "Paused", "Pausiert"},
	{"header.resume", "Resume", "Weiter"},
	{"foot.capture", "Capture frame (F12)", "Bild aufnehmen (F12)"},
	{"foot.hint", "Esc, F10 or the PS button closes this menu", "Esc, F10 oder die PS-Taste schließt dieses Menü"},
	{"foot.hint_pad", "L1 / R1: sections   Esc, F10 or the PS button closes this menu", "L1 / R1: Seiten   Esc, F10 oder die PS-Taste schließt dieses Menü"},
	{"foot.save_failed", "Settings could not be saved", "Einstellungen konnten nicht gespeichert werden"},
	{"foot.quit_game", "Quit game", "Spiel beenden"},
	{"foot.quit_launcher", "Back to the library", "Zurück zur Bibliothek"},
	{"help.about.body", "Project Nativ: this PS Vita game, statically recompiled to native code, running on this computer's own CPU and GPU.",
	 "Project Nativ: Dieses PS-Vita-Spiel, statisch in nativen Code übersetzt, läuft auf der CPU und GPU dieses Computers."},
	/* the Performance page */
	{"perf.osd", "On-screen stats", "Bildschirm-Statistiken"},
	{"perf.osd_help", "Frame rate and timings drawn over the game. F1 cycles them.", "Bildrate und Zeiten über dem Spiel. F1 schaltet sie um."},
	{"perf.osd_full", "Full", "Voll"},
	{"perf.fps_now", "Frame rate now", "Bildrate jetzt"},
	/* the Display page */
	{"display.resolution", "Resolution", "Auflösung"},
	{"display.resolution_help", "The game drawn at a multiple of the Vita's 960 × 544. Higher is sharper; 4× and up want a fast GPU.",
	 "Das Spiel in einem Vielfachen der 960 × 544 der Vita. Höher ist schärfer; 4× und mehr will eine schnelle GPU."},
	{"display.match_window", "Match the window (%d×)", "Fenster angleichen (%d×)"},
	{"display.fit", "Picture fit", "Bildanpassung"},
	{"display.fit_help", "How the picture fills the window.", "Wie das Bild das Fenster füllt."},
	{"aspect.fit", "Fit", "Einpassen"},
	{"aspect.stretch", "Stretch", "Strecken"},
	{"aspect.fill", "Fill", "Füllen"},
	{"display.widescreen", "Widescreen", "Breitbild"},
	{"display.widescreen_help", "Shows more of the game at the sides.", "Zeigt mehr vom Spiel an den Rändern."},
	{"display.widescreen_none", "This game has no widescreen support yet.", "Dieses Spiel unterstützt noch kein Breitbild."},
	{"display.ar_window", "Window", "Fenster"},
	{"display.hud", "HUD", "HUD"},
	{"display.hud_help", "With widescreen on: the HUD at its own shape, at the screen's edges or in a 16:9 frame in the middle; or stretched to the width.",
	 "Bei Breitbild: das HUD in eigener Form, an den Rändern des Bildschirms oder in einem 16:9-Rahmen in der Mitte; oder auf die Breite gestreckt."},
	{"display.hud_edges", "Edges", "Ränder"},
	{"display.hud_stretched", "Stretched", "Gestreckt"},
	{"display.full_res", "Full-resolution world", "Welt in voller Auflösung"},
	{"display.full_res_help", "The game draws its 3D world below the screen's resolution; on, at the full resolution. Sharper, at a GPU cost.",
	 "Das Spiel zeichnet seine 3D-Welt unter der Auflösung des Bildschirms; an, in voller Auflösung. Schärfer, auf Kosten der GPU."},
	{"display.ssao", "Ambient occlusion", "Umgebungsverdeckung"},
	{"display.ssao_help", "Soft shadows where surfaces meet: corners, crevices, under objects.", "Weiche Schatten, wo Flächen aufeinandertreffen: Ecken, Ritzen, unter Objekten."},
	{"display.ssao_strength", "Occlusion strength", "Stärke der Verdeckung"},
	{"display.fullscreen", "Fullscreen", "Vollbild"},
	{"display.presentation", "Presentation", "Bildausgabe"},
	{"display.presentation_help", "How frames reach the screen. Vsync waits for the screen's refresh: no tearing. Mailbox and Immediate do not wait -- less input lag, the frame rate paces itself -- and Immediate can tear. Applies after restart.",
	 "Wie die Bilder zum Bildschirm kommen. Vsync wartet auf den Bildschirmtakt: kein Reißen. Mailbox und Sofort warten nicht -- weniger Eingabeverzögerung, die Bildrate taktet sich selbst -- und Sofort kann reißen. Gilt nach dem Neustart."},
	{"display.pm_immediate", "Immediate", "Sofort"},
	{"display.window_size", "Window size", "Fenstergröße"},
	{"display.window_size_help", "The window at a multiple of the Vita's 960 × 544.", "Das Fenster in einem Vielfachen der 960 × 544 der Vita."},
	{"display.aa", "Anti-aliasing", "Kantenglättung"},
	{"display.aa_fsr_help", "Smooths jagged edges. FXAA: a light pass on the finished frame. FSR 3.1 (experimental): AMD's temporal anti-aliasing on the 3D world, steadier and cleaner, at a GPU cost; some surfaces may still shimmer (F4 compares it live).",
	 "Glättet gezackte Kanten. FXAA: ein leichter Durchlauf über das fertige Bild. FSR 3.1 (experimentell): AMDs zeitliche Kantenglättung für die 3D-Welt, ruhiger und sauberer, auf Kosten der GPU; manche Flächen können noch flimmern (F4 vergleicht live)."},
	{"display.aa_help", "Smooths jagged edges (FXAA).", "Glättet gezackte Kanten (FXAA)."},
	{"display.aa_fsr", "FSR 3.1 (experimental)", "FSR 3.1 (experimentell)"},
	{"display.sharpening", "Sharpening", "Schärfung"},
	{"display.sharpening_help", "Brings back detail lost to scaling and anti-aliasing.", "Holt durch Skalierung und Kantenglättung verlorenes Detail zurück."},
	{"display.aniso", "Texture filtering", "Texturfilterung"},
	{"display.aniso_help", "Keeps textures crisp when seen at an angle.", "Hält Texturen scharf, wenn man sie schräg sieht."},
	{"display.shadow", "Shadow edges", "Schattenränder"},
	{"display.shadow_help", "How soft the edges of shadows are.", "Wie weich die Ränder der Schatten sind."},
	{"display.sf_original", "Original", "Original"},
	{"display.sf_soft", "Soft", "Weich"},
	{"display.sf_softer", "Softer", "Weicher"},
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
