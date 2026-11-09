/*
 * export_progress.h - what "Export for Steam Deck..." shows while `vitarecomp export-appimage <title> <out>` runs, read from
 * its output in the launcher's log (EXPORT-01a's contract, in order):
 *   export: <the private-use notice>
 *   export 1/3: compiling ...   export 2/3: collecting ...   export 3/3: packing ...
 *   exported <name> -> <out> (<n> GB) ...
 * and, when it fails, a non-zero exit with one plain line saying why ("build it first", no toolchain, ...).
 * The launcher's own lines in the log (a "HH:MM:SS " stamp on each; "$ ", "@@ ", "-- ", "== " lines) are skipped.
 * Header-only: launcher/tests/export_progress_test.cpp checks it.
 */
#ifndef NATIV_EXPORT_PROGRESS_H
#define NATIV_EXPORT_PROGRESS_H
#include <cctype>
#include <cstdio>
#include <string>

struct ExportView {
	std::string notice;         /* the "export:" line's text */
	int step = 0;               /* the latest "export k/n" (0: none yet) */
	int steps = 3;              /* its n */
	std::string step_text[8];   /* each step's text, as printed (1-based: [1] .. [steps]) */
	std::string result;         /* the "exported ..." line */
	std::string out;            /* its path (between " -> " and " (") */
	std::string error;          /* failed: the last line of the command's own */
	int step_at[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   /* each step's start, seconds of the day from its line's stamp (-1: none) */
	int done_at = -1;           /* the "exported" line's */
	int last_at = -1;           /* the latest stamp in the log */
};
/* seconds from a to b on the launcher's clock (a stamp is the time of day: past midnight wraps); -1 when one is unknown */
static inline int export_secs(int a, int b) { return a < 0 || b < 0 ? -1 : (b - a + 86400) % 86400; }
/* 0:07, 43:30, 1:02:09 */
static inline std::string export_dur(int s) {
	char b[32];
	if (s < 0) return "";
	if (s >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
	return b;
}

static inline std::string export_unstamp(const std::string &l, int *at = nullptr) {   /* without the launcher's "HH:MM:SS " (its time in *at) */
	if (l.size() >= 9 && l[2] == ':' && l[5] == ':' && l[8] == ' ' && isdigit((unsigned char)l[0]) && isdigit((unsigned char)l[1])) {
		if (at) *at = ((l[0] - '0') * 10 + (l[1] - '0')) * 3600 + ((l[3] - '0') * 10 + (l[4] - '0')) * 60 + (l[6] - '0') * 10 + (l[7] - '0');
		return l.substr(9);
	}
	return l;
}
static inline bool export_launcher_line(const std::string &l) {
	return l.rfind("$ ", 0) == 0 || l.rfind("@@ ", 0) == 0 || l.rfind("-- ", 0) == 0 || l.rfind("== ", 0) == 0;
}

/* the view of `log` (the task's part of the launcher's log); failed: the command ended with an error */
static inline ExportView export_parse(const std::string &log, bool failed) {
	ExportView v;
	std::string last;   /* the command's latest line of its own */
	size_t a = 0;
	while (a < log.size()) {
		size_t e = log.find('\n', a); if (e == std::string::npos) e = log.size();
		int at = -1;
		std::string l = export_unstamp(log.substr(a, e - a), &at);
		a = e + 1;
		if (at >= 0) v.last_at = at;
		while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
		if (l.empty() || export_launcher_line(l)) continue;
		last = l;
		if (l.rfind("export: ", 0) == 0) { v.notice = l.substr(8); continue; }
		int k = 0, n = 0, used = 0;
		if (l.rfind("export ", 0) == 0 && sscanf(l.c_str() + 7, "%d/%d:%n", &k, &n, &used) == 2 && used > 0 && k >= 1 && n >= k && n < 8) {
			v.step = k; v.steps = n;
			std::string t = l.substr(7 + used); while (!t.empty() && t[0] == ' ') t.erase(0, 1);
			v.step_text[k] = t; if (v.step_at[k] < 0) v.step_at[k] = at;
			continue;
		}
		if (l.rfind("exported ", 0) == 0) {
			v.result = l; v.done_at = at;
			const size_t p = l.find(" -> ");
			if (p != std::string::npos) { const size_t q = l.find(" (", p + 4); v.out = l.substr(p + 4, q == std::string::npos ? std::string::npos : q - p - 4); }
		}
	}
	if (failed) v.error = last.empty() ? "The export stopped without saying why." : last;
	return v;
}

/* the folder of a path (for "Show in folder"): everything before its last / or \ */
static inline std::string export_folder(const std::string &p) {
	const size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? std::string(".") : p.substr(0, s == 0 ? 1 : s);
}
/* a file name for a game's AppImage: its name with only letters, digits, '-', '_' and '.' ("Soul Sacrifice Delta" ->
 * "Soul-Sacrifice-Delta.AppImage") */
static inline std::string export_file_name(const std::string &game) {
	std::string s;
	for (char c : game) {
		if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') s += c;
		else if (!s.empty() && s.back() != '-') s += '-';
	}
	while (!s.empty() && s.back() == '-') s.pop_back();
	return (s.empty() ? std::string("game") : s) + ".AppImage";
}
#endif
