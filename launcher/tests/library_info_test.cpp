/* library_info_test.cpp - launcher/library_info.h (UI-01l): SFO titles, the play-time file, the list's formats.
 * Build: c++ -std=c++17 -Wall -I .. library_info_test.cpp -o /tmp/library_info_test && /tmp/library_info_test */
#include <cstdio>
#include <string>
#include <vector>
#include "library_info.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* a synthetic SFO with the given (key, value) strings */
static std::vector<unsigned char> sfo(const std::vector<std::pair<std::string, std::string>> &kv) {
	std::string keys, data; std::vector<unsigned char> idx;
	auto p16 = [&](unsigned v) { idx.push_back(v & 255); idx.push_back(v >> 8 & 255); };
	auto p32 = [&](unsigned v) { for (int i = 0; i < 4; i++) idx.push_back(v >> (8 * i) & 255); };
	for (auto &e : kv) {
		p16((unsigned)keys.size()); p16(0x0204); p32((unsigned)e.second.size() + 1); p32((unsigned)e.second.size() + 1); p32((unsigned)data.size());
		keys += e.first; keys += '\0'; data += e.second; data += '\0';
	}
	std::vector<unsigned char> out = {0, 'P', 'S', 'F', 1, 1, 0, 0};
	const unsigned kt = 20 + (unsigned)idx.size(), dt = kt + (unsigned)keys.size();
	for (unsigned v : {kt, dt, (unsigned)kv.size()}) for (int i = 0; i < 4; i++) out.push_back(v >> (8 * i) & 255);
	out.insert(out.end(), idx.begin(), idx.end()); out.insert(out.end(), keys.begin(), keys.end()); out.insert(out.end(), data.begin(), data.end());
	return out;
}

int main(void) {
	/* SFO */
	auto s = sfo({{"APP_VER", "01.00"}, {"STITLE", "P4G"}, {"TITLE", "Persona 4 Golden"}, {"TITLE_ID", "PCSB00245"}});
	CHECK(sfo_title(s.data(), s.size()) == "Persona 4 Golden");
	auto s2 = sfo({{"TITLE", "Two\nLines"}});
	CHECK(sfo_title(s2.data(), s2.size()) == "Two Lines");
	auto s3 = sfo({{"TITLE_ID", "PCSB00245"}});
	CHECK(sfo_title(s3.data(), s3.size()) == "");                 /* (TITLE_ID is not TITLE) */
	const unsigned char junk[] = "not an sfo at all, just bytes";
	CHECK(sfo_title(junk, sizeof junk) == "");
	CHECK(sfo_title(s.data(), 24) == "");                          /* cut short */
	CHECK(sfo_title_file("/nonexistent/param.sfo") == "");
	/* play time */
	auto m = playtime_parse("PCSF00532\t1700000000\t3600\nPCSB00245\t1700000500\t60\nbroken line\n\tno id\t1\t2\n");
	CHECK(m.size() == 2 && m["PCSF00532"].seconds == 3600 && m["PCSB00245"].last == 1700000500);
	playtime_add(m, "PCSF00532", 1700009000, 1800);
	CHECK(m["PCSF00532"].seconds == 5400 && m["PCSF00532"].last == 1700009000);
	playtime_add(m, "PCSE00764", 1700001000, 0);                   /* (started, no time: last played, no time) */
	CHECK(m["PCSE00764"].last == 1700001000 && m["PCSE00764"].seconds == 0);
	CHECK(playtime_parse(playtime_format(m)).size() == 3 && playtime_parse(playtime_format(m))["PCSF00532"].seconds == 5400);
	/* formats */
	CHECK(fmt_size(442000000ull) == "422 MB");
	CHECK(fmt_size(1700000000ull) == "1.58 GB");
	CHECK(fmt_size(0) == "0 MB");
	CHECK(fmt_played(0) == "" && fmt_played(30) == "under a minute" && fmt_played(754) == "12 min");
	CHECK(fmt_played(45240) == "12 h 34 min" && fmt_played(3900) == "1 h 05 min");
	{	struct tm t = {}; t.tm_year = 126; t.tm_mon = 10; t.tm_mday = 8; t.tm_hour = 18; t.tm_min = 30; t.tm_isdst = -1;
		const long long now = (long long)mktime(&t);
		struct tm a = t; a.tm_hour = 16; a.tm_min = 17; const long long today = (long long)mktime(&a);
		struct tm y = t; y.tm_mday = 7; y.tm_hour = 23; const long long yday = (long long)mktime(&y);
		struct tm d = t; d.tm_mday = 5; const long long three = (long long)mktime(&d);
		struct tm o = t; o.tm_mon = 9; o.tm_mday = 30; const long long old = (long long)mktime(&o);
		CHECK(fmt_last(today, now) == "Today 16:17");
		CHECK(fmt_last(yday, now) == "Yesterday");
		CHECK(fmt_last(three, now) == "3 days ago");
		CHECK(fmt_last(old, now) == "2026-10-30");
		CHECK(fmt_last(0, now) == "");
	}
	printf("library_info: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
