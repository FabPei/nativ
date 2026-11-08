/*
 * file_browse.h - the launcher's own file and folder browser's listing (UI-01f): what a folder holds that the player can
 * pick, for a controller or a finger (the system's file dialog takes neither). Header-only C++, POSIX dirent / stat
 * (mingw has both); launcher/tests/file_browse_test.cpp checks it on a folder tree of its own.
 *
 * Kinds: a folder (only folders shown), a PKG (folders and *.pkg), a licence (folders, *.rif and work.bin), the firmware
 * (folders and *.pup). Names are compared without case, folders first, then files; names starting with a dot and
 * Windows' $-prefixed system folders are left out. Paths use '/' (Windows takes them), a drive's root is "C:/".
 */
#ifndef ATIV_FILE_BROWSE_H
#define ATIV_FILE_BROWSE_H
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

enum { BROWSE_FOLDER, BROWSE_PKG, BROWSE_LICENCE, BROWSE_PUP };
struct BrowseEntry { std::string name; bool dir; };

static inline std::string browse_lower(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }
static inline bool browse_ends(const std::string &low, const char *ext) { const size_t n = strlen(ext); return low.size() > n && low.compare(low.size() - n, n, ext) == 0; }
static inline bool browse_wants(int kind, const std::string &name) {   /* a file this kind of pick shows */
	const std::string l = browse_lower(name);
	switch (kind) {
	case BROWSE_PKG: return browse_ends(l, ".pkg");
	case BROWSE_LICENCE: return browse_ends(l, ".rif") || l == "work.bin";
	case BROWSE_PUP: return browse_ends(l, ".pup");
	default: return false;
	}
}
static inline std::string browse_join(const std::string &dir, const std::string &name) {
	if (dir.empty()) return name;
	return dir.back() == '/' || dir.back() == '\\' ? dir + name : dir + "/" + name;
}
/* the folder above (C:/Games -> C:/, C:/ -> "": the places; /home/deck -> /home, / -> "") */
static inline std::string browse_parent(std::string p) {
	for (char &c : p) if (c == '\\') c = '/';
	while (p.size() > 1 && p.back() == '/' && !(p.size() == 3 && p[1] == ':')) p.pop_back();
	if (p == "/" || (p.size() == 3 && p[1] == ':' && p[2] == '/') || (p.size() == 2 && p[1] == ':')) return "";
	const size_t s = p.find_last_of('/');
	if (s == std::string::npos) return "";
	if (s == 0) return "/";
	if (s == 2 && p[1] == ':') return p.substr(0, 3);
	return p.substr(0, s);
}
static inline std::vector<BrowseEntry> browse_list(const std::string &dir, int kind) {
	std::vector<BrowseEntry> v;
	if (DIR *d = opendir(dir.c_str())) {
		while (dirent *e = readdir(d)) {
			const std::string n = e->d_name;
			if (n.empty() || n[0] == '.' || n[0] == '$') continue;
			struct stat st; if (stat(browse_join(dir, n).c_str(), &st) != 0) continue;
			const bool isdir = S_ISDIR(st.st_mode);
			if (isdir || browse_wants(kind, n)) v.push_back({n, isdir});
		}
		closedir(d);
	}
	std::sort(v.begin(), v.end(), [](const BrowseEntry &a, const BrowseEntry &b) { return a.dir != b.dir ? a.dir : browse_lower(a.name) < browse_lower(b.name); });
	return v;
}
#endif
