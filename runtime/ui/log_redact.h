/*
 * LOG-REDACT: what a log or settings file says about the person before it leaves the computer (the launcher's Copy log /
 * Save log..., and pipeline/redact.py for `vitarecomp report`: the same rules).
 *  - the home folder, however it's written (C:\Users\<name>, C:/Users/<name>, any case; /home/<name>, /Users/<name>) -> ~
 *  - any other user folder on the machine (C:\Users\<someone>, \\?\C:\Users\... too) -> ~ as well
 *  - the identity keys (settings.ini: open_psid, online_id, np_account_id, np_*) -> their value "<removed>"
 * Header only, plain C: runtime/tests/log_redact_test.c checks it.
 */
#ifndef LOG_REDACT_H
#define LOG_REDACT_H
#include <stdlib.h>
#include <string.h>

static inline int log_redact_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int log_redact_sep(int c) { return c == '/' || c == '\\'; }
/* at s: "<drive>:<sep>users<sep><name>" or "<sep>home<sep><name>" / "<sep>Users<sep><name>"; returns its length, 0 for none */
static inline size_t log_redact_user_dir(const char *s, size_t n) {
	size_t i = 0;
	if (n >= 3 && ((s[0] | 32) >= 'a' && (s[0] | 32) <= 'z') && s[1] == ':' && log_redact_sep(s[2])) i = 3;
	else if (n >= 1 && log_redact_sep(s[0])) i = 1;
	else return 0;
	static const char *const roots[] = {"users", "home"};
	for (int r = 0; r < 2; r++) {
		size_t l = strlen(roots[r]), k = 0;
		if (i + l + 1 >= n) continue;
		for (; k < l && log_redact_lower((unsigned char)s[i + k]) == roots[r][k]; k++) {}
		if (k < l || !log_redact_sep(s[i + l])) continue;
		if (r == 1 && i == 3) continue;   /* (C:\home\... is no home folder) */
		size_t j = i + l + 1, start = j;
		while (j < n && !log_redact_sep(s[j]) && s[j] != '"' && s[j] != '\'' && s[j] != ' ' && s[j] != '\t' && s[j] != '\r' && s[j] != '\n' && s[j] != ';' && s[j] != ',') j++;
		if (j == start) return 0;
		{	/* Windows' shared folders are nobody's: C:\Users\Public, Default */
			static const char *const shared[] = {"public", "default", "all users"};
			for (int q = 0; q < 3; q++) { size_t ql = strlen(shared[q]), m = 0;
				if (j - start != ql) continue;
				for (; m < ql && log_redact_lower((unsigned char)s[start + m]) == shared[q][m]; m++) {}
				if (m == ql) return 0; }
		}
		return j;
	}
	return 0;
}
/* at the start of a line: "<key> = <value>" with an identity key; returns the value's offset in the line, 0 for none */
static inline size_t log_redact_identity(const char *s, size_t n) {
	size_t i = 0; while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
	static const char *const keys[] = {"open_psid", "online_id", "np_"};
	for (int k = 0; k < 3; k++) {
		size_t l = strlen(keys[k]);
		if (i + l > n || strncmp(s + i, keys[k], l)) continue;
		size_t j = i + l; if (k == 2) while (j < n && (s[j] == '_' || (s[j] >= 'a' && s[j] <= 'z'))) j++;
		while (j < n && (s[j] == ' ' || s[j] == '\t')) j++;
		if (j >= n || s[j] != '=') continue;
		j++; while (j < n && (s[j] == ' ' || s[j] == '\t')) j++;
		if (j >= n || s[j] == '\r' || s[j] == '\n') return 0;   /* (an empty value says nothing) */
		return j;
	}
	return 0;
}
/* the redacted copy of in[0..n) (malloc'd, NUL-terminated; *out_n its length), or NULL when out of memory */
static inline char *log_redact(const char *in, size_t n, size_t *out_n) {
	char *o = (char *)malloc(n + n / 4 + 64); size_t w = 0, cap = n + n / 4 + 64;
	if (!o) return NULL;
	int bol = 1;
	for (size_t i = 0; i < n; ) {
		if (w + 32 >= cap) { cap = cap * 2 + 64; char *g = (char *)realloc(o, cap); if (!g) { free(o); return NULL; } o = g; }
		if (bol) {
			size_t v = log_redact_identity(in + i, n - i);
			if (v) {
				memcpy(o + w, in + i, v); w += v;
				memcpy(o + w, "<removed>", 9); w += 9;
				i += v; while (i < n && in[i] != '\r' && in[i] != '\n') i++;
				bol = 0; continue;
			}
		}
		size_t u = log_redact_user_dir(in + i, n - i);
		if (u) {   /* a drive path: not inside a word; a /home or /Users path: only where a path starts (not C:\x\home\...) */
			const int p = i ? (unsigned char)in[i - 1] : ' ';
			const int ok = log_redact_sep(in[i]) ? (p == ' ' || p == '\t' || p == '\n' || p == '"' || p == '\'' || p == '=' || p == '(' || p == ':' || p == ',' || p == ';' || p == '[')
			                                     : !((p | 32) >= 'a' && (p | 32) <= 'z') && !(p >= '0' && p <= '9');
			if (ok) { o[w++] = '~'; i += u; bol = 0; continue; }
		}
		bol = in[i] == '\n';
		o[w++] = in[i++];
	}
	o[w] = 0; if (out_n) *out_n = w;
	return o;
}
#endif
