/* imedlg_test.c -- HLE-LIB-05: the IME dialog's UTF-16 <-> UTF-8 helpers and its shared state (imedlg.h/.c).
 * Fail-before: before HLE-LIB-05 there was no card -- an IME dialog only ever returned its initial text.
 *   cc -O1 -g -Wall -I ../core imedlg_test.c ../core/imedlg.c -o /tmp/imedlg_test -lpthread && /tmp/imedlg_test
 */
#include <stdio.h>
#include <string.h>
#include "imedlg.h"

static int fails;
static void check(const char *what, int ok) { printf("%-60s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	const uint16_t u16[] = {'N', 0x00E4, 'm', 0x20AC, 0xD83D, 0xDE00, 0};   /* N, a-umlaut, m, euro, a surrogate pair */
	char u8[32]; imedlg_utf16_to_utf8(u16, 16, u8, sizeof u8);
	check("utf16 -> utf8: ASCII, 2-, 3- and 4-byte forms", !strcmp(u8, "N\xC3\xA4m\xE2\x82\xAC\xF0\x9F\x98\x80"));
	uint16_t back[16]; const size_t n = imedlg_utf8_to_utf16(u8, back, 15);
	check("utf8 -> utf16: round trip (6 units, surrogates kept)", n == 6 && !memcmp(back, u16, 7 * 2));
	check("utf8 length counts code points (5)", imedlg_utf8_len(u8) == 5);
	uint16_t small[4]; check("utf8 -> utf16 stops at max (3 units + 0)", imedlg_utf8_to_utf16("abcdef", small, 3) == 3 && small[3] == 0);
	char tiny[4]; imedlg_utf16_to_utf8(u16, 16, tiny, sizeof tiny);
	check("utf16 -> utf8 never splits a character at the cap", !strcmp(tiny, "N\xC3\xA4"));

	char t[64]; int ok = -1; char title[32]; uint32_t mx; int seq;
	check("no UI: open refuses (the caller keeps its fallback)", imedlg_open("Name", "Player", 16) == 0);
	imedlg_set_ui_ready(1);
	check("UI: open ok, the card sees title/text/max", imedlg_open("Name", "Player", 16) && imedlg_view(title, sizeof title, t, sizeof t, &mx, &seq) && !strcmp(title, "Name") && !strcmp(t, "Player") && mx == 16);
	check("nothing to take before the answer", !imedlg_take(t, sizeof t, &ok));
	imedlg_answer(1, "Fabian");
	check("answered: the card no longer shows", !imedlg_view(title, sizeof title, t, sizeof t, &mx, &seq));
	check("take: the typed text, OK", imedlg_take(t, sizeof t, &ok) && ok == 1 && !strcmp(t, "Fabian"));
	check("taken once only", !imedlg_take(t, sizeof t, &ok));
	imedlg_open("Name", "Player", 16); imedlg_answer(0, "ignored");
	check("cancel: ok 0, the initial text kept", imedlg_take(t, sizeof t, &ok) && ok == 0 && !strcmp(t, "Player"));
	imedlg_open("Name", "x", 4); imedlg_close();
	check("close (Abort/Term): no card, nothing to take", !imedlg_view(title, sizeof title, t, sizeof t, &mx, &seq) && !imedlg_take(t, sizeof t, &ok));

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
