/*
 * library_info.h - what the library's list shows beside a game (UI-01l): its own name from its param.sfo (a game with no
 * game pack showed its title id), when it was last played and for how long (the launcher keeps <prefs>/playtime.tsv:
 * "<title>\t<last start, epoch s>\t<seconds played>" a line), and how much room it takes. Header-only C++;
 * launcher/tests/library_info_test.cpp checks it.
 */
#ifndef ATIV_LIBRARY_INFO_H
#define ATIV_LIBRARY_INFO_H
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

/* PARAM.SFO (the Vita's own "PSF" format): its TITLE, "" when the data isn't one or has none */
static inline std::string sfo_title(const unsigned char *d, size_t n) {
	auto u16 = [&](size_t o) { return o + 2 <= n ? (unsigned)d[o] | (unsigned)d[o + 1] << 8 : 0u; };
	auto u32 = [&](size_t o) { return o + 4 <= n ? (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24 : 0u; };
	if (n < 20 || memcmp(d, "\0PSF", 4) != 0) return "";
	const uint32_t keys = u32(8), data = u32(12), count = u32(16);
	for (uint32_t i = 0; i < count && 20 + (size_t)i * 16 + 16 <= n; i++) {
		const size_t e = 20 + (size_t)i * 16;
		const size_t ko = keys + u16(e), dl = u32(e + 4), dof = data + u32(e + 12);
		if (ko >= n) continue;
		if (strncmp((const char *)d + ko, "TITLE", n - ko) == 0 && (size_t)strnlen((const char *)d + ko, n - ko) == 5 && dof + dl <= n) {
			std::string t((const char *)d + dof, dl);
			while (!t.empty() && t.back() == '\0') t.pop_back();
			for (char &c : t) if (c == '\n' || c == '\r') c = ' ';   /* (a two-line title: one line on a card) */
			return t;
		}
	}
	return "";
}
static inline std::string sfo_title_file(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb"); if (!f) return "";
	unsigned char buf[65536]; const size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
	return sfo_title(buf, n);
}

struct PlayTime { long long last = 0, seconds = 0; };
static inline std::map<std::string, PlayTime> playtime_parse(const std::string &text) {
	std::map<std::string, PlayTime> m;
	size_t a = 0;
	while (a < text.size()) {
		size_t e = text.find('\n', a); if (e == std::string::npos) e = text.size();
		const std::string l = text.substr(a, e - a); a = e + 1;
		const size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
		if (t2 == std::string::npos || t1 == 0) continue;
		PlayTime p; p.last = atoll(l.c_str() + t1 + 1); p.seconds = atoll(l.c_str() + t2 + 1);
		m[l.substr(0, t1)] = p;
	}
	return m;
}
static inline std::string playtime_format(const std::map<std::string, PlayTime> &m) {
	std::string s; char b[96];
	for (auto &kv : m) { snprintf(b, sizeof b, "\t%lld\t%lld\n", kv.second.last, kv.second.seconds); s += kv.first + b; }
	return s;
}
static inline void playtime_add(std::map<std::string, PlayTime> &m, const std::string &id, long long started, long long seconds) {
	PlayTime &p = m[id];
	if (started > p.last) p.last = started;
	if (seconds > 0) p.seconds += seconds;
}

/* "421 MB", "1.58 GB" */
static inline std::string fmt_size(uint64_t bytes) {
	char b[32];
	if (bytes >= 1000ull * 1024 * 1024) snprintf(b, sizeof b, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
	else if (bytes >= 1024ull * 1024) snprintf(b, sizeof b, "%.0f MB", bytes / (1024.0 * 1024));
	else if (bytes > 0) snprintf(b, sizeof b, "%.0f KB", bytes / 1024.0 + 0.5);
	else snprintf(b, sizeof b, "0 MB");
	return b;
}
/* "under a minute", "12 min", "3 h 05 min" */
static inline std::string fmt_played(long long s) {
	char b[32];
	if (s <= 0) return "";
	if (s < 60) return "under a minute";
	if (s < 3600) { snprintf(b, sizeof b, "%lld min", s / 60); return b; }
	snprintf(b, sizeof b, "%lld h %02lld min", s / 3600, s / 60 % 60);
	return b;
}
/* "Today 16:17", "Yesterday", "3 days ago", "2026-10-30" (local time) */
static inline std::string fmt_last(long long then, long long now) {
	if (then <= 0) return "";
	time_t t = (time_t)then, n = (time_t)now; struct tm a, b;
#ifdef _WIN32
	localtime_s(&a, &t); localtime_s(&b, &n);
#else
	localtime_r(&t, &a); localtime_r(&n, &b);
#endif
	struct tm a0 = a, b0 = b; a0.tm_hour = b0.tm_hour = 12; a0.tm_min = b0.tm_min = a0.tm_sec = b0.tm_sec = 0; a0.tm_isdst = b0.tm_isdst = -1;
	const long days = (long)((mktime(&b0) - mktime(&a0) + 43200) / 86400);   /* (calendar days apart, noon to noon) */
	char s[32];
	if (days <= 0) { strftime(s, sizeof s, "Today %H:%M", &a); return s; }
	if (days == 1) return "Yesterday";
	if (days < 7) { snprintf(s, sizeof s, "%ld days ago", days); return s; }
	strftime(s, sizeof s, "%Y-%m-%d", &a);
	return s;
}
#endif
