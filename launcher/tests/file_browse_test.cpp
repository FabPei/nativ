/* file_browse_test.cpp - launcher/file_browse.h (UI-01f) on a folder tree of its own.
 * Build: c++ -std=c++17 -Wall -I .. file_browse_test.cpp -o /tmp/file_browse_test && /tmp/file_browse_test */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include "file_browse.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void touch(const std::string &p) { if (FILE *f = fopen(p.c_str(), "wb")) fclose(f); }
static std::string names(const std::vector<BrowseEntry> &v) { std::string s; for (auto &e : v) s += (s.empty() ? "" : ",") + e.name + (e.dir ? "/" : ""); return s; }

int main(void) {
	char tmpl[] = "/tmp/browse_testXXXXXX"; std::string t = mkdtemp(tmpl);
	mkdir((t + "/ux0").c_str(), 0755); mkdir((t + "/Apps").c_str(), 0755); mkdir((t + "/.cache").c_str(), 0755); mkdir((t + "/$RECYCLE.BIN").c_str(), 0755);
	touch(t + "/game.pkg"); touch(t + "/Update.PKG"); touch(t + "/licence.rif"); touch(t + "/work.bin"); touch(t + "/PSVUPDAT.PUP");
	touch(t + "/notes.txt"); touch(t + "/.hidden.pkg");
	/* each kind: folders first (no case), then its files; dot- and $-names never */
	CHECK(names(browse_list(t, BROWSE_FOLDER)) == "Apps/,ux0/");
	CHECK(names(browse_list(t, BROWSE_PKG)) == "Apps/,ux0/,game.pkg,Update.PKG");
	CHECK(names(browse_list(t, BROWSE_LICENCE)) == "Apps/,ux0/,licence.rif,work.bin");
	CHECK(names(browse_list(t, BROWSE_PUP)) == "Apps/,ux0/,PSVUPDAT.PUP");
	CHECK(browse_list(t + "/nothing-here", BROWSE_PKG).empty());
	/* parents and joins: drives, the root, trailing slashes, backslashes */
	CHECK(browse_parent("C:/Games/Vita") == "C:/Games");
	CHECK(browse_parent("C:/Games") == "C:/");
	CHECK(browse_parent("C:/") == "");
	CHECK(browse_parent("C:") == "");
	CHECK(browse_parent("C:\\Users\\me\\") == "C:/Users");
	CHECK(browse_parent("/home/deck/") == "/home");
	CHECK(browse_parent("/home") == "/");
	CHECK(browse_parent("/") == "");
	CHECK(browse_parent("/run/media/deck/SD") == "/run/media/deck");
	CHECK(browse_join("C:/", "Games") == "C:/Games");
	CHECK(browse_join("/home/deck", "Games") == "/home/deck/Games");
	CHECK(browse_wants(BROWSE_LICENCE, "WORK.BIN") && !browse_wants(BROWSE_FOLDER, "game.pkg") && !browse_wants(BROWSE_PKG, "pkg"));
	printf("file_browse: %d checks, %d failed\n", checks, fails);
	std::string rm = "rm -rf '" + t + "'"; if (system(rm.c_str())) {}
	return fails != 0;
}
