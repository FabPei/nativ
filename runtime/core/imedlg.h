/*
 * HLE-LIB-05: the IME dialog's text entry, shared between the game side (apputil.c: sceImeDialog*) and the host's
 * card (runtime/ui/imedlg_ui.cpp, drawn by the overlay). The game side opens it with the dialog's title, initial text
 * and length limit (UTF-8 here; the guest's UTF-16 converted with the helpers below), the card answers it, the game
 * side takes the answer on its next GetStatus. imedlg_ui_ready: an overlay with a window can show the card -- without
 * one (headless runs, tests) the dialog keeps HLE-LIB-01's answer (the initial text, OK).
 * The UTF-16 <-> UTF-8 helpers are pure (runtime/tests/imedlg_test.c).
 */
#ifndef RECOMP_IMEDLG_H
#define RECOMP_IMEDLG_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define IMEDLG_TEXT_MAX 2048   /* UTF-8 bytes, terminator included */

void imedlg_set_ui_ready(int ready);   /* the overlay: it can draw the card */
int imedlg_ui_ready(void);
/* the game side */
int imedlg_open(const char *title, const char *text, uint32_t max_chars);   /* 0: no card (no UI) */
int imedlg_take(char *text, size_t n, int *ok);   /* 1 once the player answered (then closed) */
void imedlg_close(void);                          /* Abort / Term */
/* the card */
int imedlg_view(char *title, size_t tn, char *text, size_t n, uint32_t *max_chars, int *seq);   /* 1 while open and unanswered */
void imedlg_answer(int ok, const char *text);

/* UTF-16 (n units or up to a 0) -> UTF-8 into out (cap bytes, always terminated); returns the bytes written */
static inline size_t imedlg_utf16_to_utf8(const uint16_t *s, size_t n, char *out, size_t cap) {
	size_t k = 0;
	if (!cap) return 0;
	for (size_t i = 0; i < n && s[i]; i++) {
		uint32_t c = s[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
		else if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
		uint8_t b[4]; size_t l;
		if (c < 0x80) { b[0] = (uint8_t)c; l = 1; }
		else if (c < 0x800) { b[0] = (uint8_t)(0xC0 | c >> 6); b[1] = (uint8_t)(0x80 | (c & 0x3F)); l = 2; }
		else if (c < 0x10000) { b[0] = (uint8_t)(0xE0 | c >> 12); b[1] = (uint8_t)(0x80 | (c >> 6 & 0x3F)); b[2] = (uint8_t)(0x80 | (c & 0x3F)); l = 3; }
		else { b[0] = (uint8_t)(0xF0 | c >> 18); b[1] = (uint8_t)(0x80 | (c >> 12 & 0x3F)); b[2] = (uint8_t)(0x80 | (c >> 6 & 0x3F)); b[3] = (uint8_t)(0x80 | (c & 0x3F)); l = 4; }
		if (k + l + 1 > cap) break;
		for (size_t j = 0; j < l; j++) out[k++] = (char)b[j];
	}
	out[k] = 0; return k;
}
/* UTF-8 -> UTF-16 into out (at most max units, then a 0: out holds max + 1); returns the units written */
static inline size_t imedlg_utf8_to_utf16(const char *s, uint16_t *out, size_t max) {
	size_t k = 0; const uint8_t *p = (const uint8_t *)s;
	while (*p && k < max) {
		uint32_t c; int l;
		if (*p < 0x80) { c = *p; l = 1; }
		else if ((*p & 0xE0) == 0xC0) { c = *p & 0x1F; l = 2; }
		else if ((*p & 0xF0) == 0xE0) { c = *p & 0x0F; l = 3; }
		else if ((*p & 0xF8) == 0xF0) { c = *p & 0x07; l = 4; }
		else { p++; continue; }
		int okc = 1; for (int j = 1; j < l; j++) { if ((p[j] & 0xC0) != 0x80) { okc = 0; break; } c = c << 6 | (p[j] & 0x3F); }
		if (!okc) { p++; out[k++] = 0xFFFD; continue; }
		p += l;
		if (c >= 0x10000) { if (k + 2 > max) break; c -= 0x10000; out[k++] = (uint16_t)(0xD800 | c >> 10); out[k++] = (uint16_t)(0xDC00 | (c & 0x3FF)); }
		else out[k++] = (uint16_t)c;
	}
	out[k] = 0; return k;
}
/* the characters (code points) of a UTF-8 string */
static inline size_t imedlg_utf8_len(const char *s) { size_t n = 0; for (const uint8_t *p = (const uint8_t *)s; *p; p++) if ((*p & 0xC0) != 0x80) n++; return n; }

#ifdef __cplusplus
}
#endif
#endif
