/*
 * nativ: Project Nativ's launcher, the front door. One window shows the player's games -- every per-game pack
 * (packs/<title>/pack.toml) and every title already under dump/decrypted/ -- as cover-art cards with
 * what state each is in, and runs the pipeline's stages for them: add a game and its update from a player's own
 * copy (a PKG with its licence file, a game folder, or a whole folder searched for them), install the firmware
 * from Sony's update file once, build, play. Each card says which version the copy is and whether its game pack
 * recommends another (the pack's VERSIONS / UPDATE_NOTE: Killzone's 1.12 update brings Botzone and online play).
 * First run (or no firmware yet) opens a short setup: welcome, firmware (with Sony's official download page),
 * games, the online name, ready. The work itself is the existing command line (./vitarecomp): the hub runs it as a
 * child process, shows the step it is on, and keeps its whole output in the activity drawer -- so the command
 * line and the hub never disagree about how a step is done.
 *
 * SDL3 + Dear ImGui on SDL_GPU, the same stack as the in-game overlay (runtime/ui/overlay.cpp). Cover art is the
 * game's own sce_sys/pic0.png and icon0.png (SDL_LoadPNG). Nothing here touches a game's code or data beyond
 * reading param-like facts (the pack's NAME=, which files exist).
 *
 * ATIV_PREFS=<dir>: the launcher's own files and the shared settings there instead of the user's app folder (a test).
 * ATIV_DUMP=<dir>: the library (games, firmware, licences, builds, saves) there instead of the usual one, library_dir (a
 * fresh library to try setup on; the stages it runs inherit it). ATIV_SHOT=f.ppm (a verification aid, no visible window): renders the hub once into f.ppm and exits;
 * ATIV_SHOT_UI=library|setup:<0-4>|add|settings:<0-2>|game:<title>|more:<title>|sac|sac:again picks what is on screen, ATIV_SHOT_SIZE=WxH the size (1280x800),
 * ATIV_SHOT_SCAN=<rows> a folder search's results (vitarecomp scan's output) in the Add dialog,
 * ATIV_SHOT_LIBSCAN=<rows> the library folders' search (vitarecomp folders scan's output: "Found in" on the cards);
 * the library folders themselves are <prefs>/library_folders.tsv (ATIV_PREFS=<dir> for a test's).
 * ATIV_TASK=play:<title>|build:<title>|scan:<folder>|licences:<folder>|firmware:<file> (a verification aid, no window): that
 * task run as its button runs it, its log printed, the exit status its outcome; ATIV_TASK_CANCEL=<seconds>: Cancel then.
 * ATIV_SHOT_STEAM=<file> what Steam has from us (vitarecomp steam list's output) in a screenshot run.
 * ATIV_SHOT_FIT=<file> (with ATIV_SHOT): the fit check -- every item or text cut off or pushed off-screen in that frame, one line
 * each, into the file (empty: nothing cut off); ledger tools/ui_fitcheck.py runs it over every screen and size (UI-01b).
 * nativ --play <title>: that game started as Play starts it (its build brought up to date first), no launcher window --
 * what a Steam shortcut made by "Add to Steam" runs; its log in <prefs>/launcher.play.log, an error in a message box
 * (ATIV_PLAY_QUIET=1: on stderr instead, for a test).
 * The library's view (hub.ini): library_view = grid | list, library_size = 60..160 (percent, the cards' and icons' size).
 */
#include "imgui.h"
#include "imgui_internal.h"   /* (ATIV_SHOT_FIT: the item hooks and window state the fit check reads; build.sh builds ImGui with IMGUI_ENABLE_TEST_ENGINE for the launcher) */
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "ui_theme.h"   /* the look shared with the in-game overlay */
#include "hub_scale.h"   /* (UI-01h) the UI's scale from the window's height or the display's scale */
#include "file_browse.h"   /* (UI-01f) the own file / folder browser's listing */
#include "library_info.h"   /* (UI-01l) a game's own name, play time, size: the list's columns */
#include "pad_family.h"   /* (UI-01e) the controller in use and its buttons' names, shared with the in-game menu */
#include <ctime>
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <random>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/mount.h>
#include <sys/xattr.h>
#endif

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif
static std::string g_root = REPO_ROOT;   /* the repository (ATIV_ROOT overrides; else found from where the launcher is) */
static std::string g_dump;               /* the player's library: games, firmware, licences, builds, saves (library_dir) */
static const char *const SONY_FIRMWARE_PAGE = "https://www.playstation.com/en-us/support/hardware/psvita/system-software/";
/* a screenshot run (ATIV_SHOT) does nothing for real: a task, a file dialog or a page a button would start is only noted
 * here (ATIV_SHOT_NAV's report: what ATIV_SHOT_KEYS pressed into) */
static bool g_shot_mode;
static std::vector<std::string> g_shot_done;
static void open_url(const std::string &u) { if (g_shot_mode) { g_shot_done.push_back("open " + u); return; } SDL_OpenURL(u.c_str()); }

static bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }
#ifdef _WIN32   /* (paths are UTF-8 throughout: the launcher's manifest sets the process code page, launcher/nativ.manifest) */
static void make_dir(const std::string &p) { _mkdir(p.c_str()); }
static void set_env(const char *k, const std::string &v) { _putenv_s(k, v.c_str()); }
static std::wstring wide(const std::string &s) {
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w((size_t)n, L'\0'); if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}
#else
static void make_dir(const std::string &p) { mkdir(p.c_str(), 0755); }
static void set_env(const char *k, const std::string &v) { setenv(k, v.c_str(), 1); }
#endif
static std::string shown_args(const std::vector<std::string> &argv) {   /* a command for the log: words with spaces quoted */
	std::string s;
	for (const std::string &a : argv) { if (!s.empty()) s += ' '; s += a.empty() || a.find_first_of(" \t\"'") != std::string::npos ? "\"" + a + "\"" : a; }
	return s;
}
/* the stages' command line (pipeline/vitarecomp.py) on the repository's Python: its virtualenv, else one installed beside the
 * launcher (<root>/python: Windows' and the self-contained Linux install's), else the system's */
static std::string python_exe(void) {
#ifdef _WIN32
	for (const char *p : {"/.venv/Scripts/python.exe", "/python/python.exe"}) if (exists(g_root + p)) return g_root + p;
	return "python";
#else
	for (const char *p : {"/.venv/bin/python", "/python/bin/python3"}) if (exists(g_root + p)) return g_root + p;   /* (an install's: <root>/python) */
	return "python3";
#endif
}
static std::vector<std::string> vitarecomp(std::initializer_list<std::string> args) {
	std::vector<std::string> v{python_exe(), g_root + "/pipeline/vitarecomp.py"};
	v.insert(v.end(), args);
	return v;
}
static void open_folder(const std::string &p) {
#ifdef _WIN32
	std::string u = p; std::replace(u.begin(), u.end(), '\\', '/'); open_url("file:///" + u);
#else
	open_url("file://" + p);
#endif
}

/* ---- textures: the games' own PNGs, uploaded once */
struct Tex { SDL_GPUTexture *t = nullptr; int w = 0, h = 0; };
static SDL_GPUDevice *g_gpu;
static std::map<std::string, Tex> g_tex;
static Tex texture(const std::string &path) {
	auto it = g_tex.find(path);
	if (it != g_tex.end()) return it->second;
	Tex x;
	SDL_Surface *s = exists(path) ? SDL_LoadPNG(path.c_str()) : nullptr;
	SDL_Surface *r = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888) : nullptr;   /* bytes R, G, B, A */
	if (s) SDL_DestroySurface(s);
	if (r && g_gpu) {
		SDL_GPUTextureCreateInfo ci = {};
		ci.type = SDL_GPU_TEXTURETYPE_2D; ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		ci.width = (Uint32)r->w; ci.height = (Uint32)r->h; ci.layer_count_or_depth = 1; ci.num_levels = 1;
		SDL_GPUTransferBufferCreateInfo tc = {}; tc.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; tc.size = (Uint32)(r->w * r->h * 4);
		SDL_GPUTexture *t = SDL_CreateGPUTexture(g_gpu, &ci);
		SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g_gpu, &tc);
		if (t && tb) {
			uint8_t *dst = (uint8_t *)SDL_MapGPUTransferBuffer(g_gpu, tb, false);
			for (int y = 0; dst && y < r->h; y++) memcpy(dst + (size_t)y * r->w * 4, (uint8_t *)r->pixels + (size_t)y * r->pitch, (size_t)r->w * 4);
			SDL_UnmapGPUTransferBuffer(g_gpu, tb);
			SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g_gpu);
			SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
			SDL_GPUTextureTransferInfo src = {}; src.transfer_buffer = tb; src.pixels_per_row = (Uint32)r->w; src.rows_per_layer = (Uint32)r->h;
			SDL_GPUTextureRegion dr = {}; dr.texture = t; dr.w = (Uint32)r->w; dr.h = (Uint32)r->h; dr.d = 1;
			SDL_UploadToGPUTexture(cp, &src, &dr, false);
			SDL_EndGPUCopyPass(cp); SDL_SubmitGPUCommandBuffer(cb);
			x.t = t; x.w = r->w; x.h = r->h;
		} else if (t) SDL_ReleaseGPUTexture(g_gpu, t);
		if (tb) SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
	}
	if (r) SDL_DestroySurface(r);
	g_tex[path] = x;
	return x;
}
static ImTextureRef tex_ref(const Tex &t) { return ImTextureRef((ImTextureID)(intptr_t)t.t); }

/* the app icon on the window (and the Dock / taskbar): assets/brand's, macOS's drawn on its own inset grid */
static void window_icon(SDL_Window *win) {
#ifdef __APPLE__
	std::string png = g_root + "/assets/brand/png/icon-macos-512.png";
#else
	std::string png = g_root + "/assets/brand/png/icon-256.png";
#endif
	if (SDL_Surface *s = exists(png) ? SDL_LoadPNG(png.c_str()) : nullptr) { SDL_SetWindowIcon(win, s); SDL_DestroySurface(s); }
}

/* ---- the user's settings (the platform's per-user app folder, or ATIV_PREFS=<dir>: a test's): setup finished, the
 * drawer's state (hub.ini); a folder search's results (scan.tsv); the settings every game reads (settings.ini) */
static std::string pref_dir(void) {
	if (const char *d = getenv("ATIV_PREFS")) { make_dir(d); return std::string(d) + "/"; }
	char *p = SDL_GetPrefPath("ProjectNativ", "Nativ");
	std::string s = p ? p : "";
	SDL_free(p);
	/* (the name was Project Ativ: its folder's files are copied over once, before anything is read from the new one) */
	static bool moved;
	size_t at = s.rfind("ProjectNativ");
	if (!moved && at != std::string::npos) {
		moved = true;
		std::string old = s.substr(0, at) + "ProjectAtiv" + s.substr(at + 12);
		size_t leaf = old.rfind("Nativ");
		if (leaf != std::string::npos && leaf > at) old.replace(leaf, 5, "Ativ");
		for (const char *f : {"hub.ini", "settings.ini", "scan.tsv"})
			if (exists(old + f) && !exists(s + f)) {
				std::string body; if (FILE *in = fopen((old + f).c_str(), "rb")) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, in)) > 0) body.append(b, n); fclose(in); }
				if (FILE *out = fopen((s + f).c_str(), "wb")) { fwrite(body.data(), 1, body.size(), out); fclose(out); }
			}
	}
	return s;
}
static std::map<std::string, std::string> g_prefs;
/* the player's library folders (pipeline/folders.py keeps them: <prefs>/library_folders.tsv, one per line -- kind
 * (folder | vita3k), the folder, the folder searched in it: a Vita3K install's ux0) */
struct LibFolder { std::string kind, path, root; };
static std::vector<LibFolder> g_lib_folders;
static std::string prefs_path(void) { std::string d = pref_dir(); return d.empty() ? "" : d + "hub.ini"; }
static void prefs_load(void) {
	FILE *f = fopen(prefs_path().c_str(), "r"); if (!f) return;
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '='); if (!eq) continue;
		*eq = 0; std::string v = eq + 1; while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
		g_prefs[line] = v;
	}
	fclose(f);
}
static void prefs_set(const char *k, const std::string &v) {
	g_prefs[k] = v;
	std::string p = prefs_path(); if (p.empty()) return;
	FILE *f = fopen(p.c_str(), "w"); if (!f) return;
	for (auto &kv : g_prefs) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
	fclose(f);
}

/* ---- a child process: its output (stdout and stderr together) read through a pipe; killed with everything it started
 * (POSIX: its own process group; Windows: a job object holding the process tree) */
struct Proc {
#ifdef _WIN32
	HANDLE process = nullptr, job = nullptr, out = nullptr;
#else
	pid_t pid = 0; int out = -1;
#endif
};
#ifdef _WIN32
static std::wstring win_arg(const std::wstring &a) {   /* one argument, quoted the way CommandLineToArgvW reads it back */
	if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
	std::wstring q = L"\"";
	for (size_t i = 0;; i++) {
		size_t bs = 0; while (i < a.size() && a[i] == L'\\') { bs++; i++; }
		if (i == a.size()) { q.append(bs * 2, L'\\'); break; }
		if (a[i] == L'"') { q.append(bs * 2 + 1, L'\\'); q += L'"'; } else { q.append(bs, L'\\'); q += a[i]; }
	}
	return q + L"\"";
}
static bool proc_start(Proc &p, const std::vector<std::string> &argv, const std::vector<std::string> &env) {
	SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
	HANDLE rd, wr, nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
	if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	std::wstring cmd; for (const std::string &a : argv) { if (!cmd.empty()) cmd += L' '; cmd += win_arg(wide(a)); }
	std::wstring block;   /* the launcher's environment with the step's NAME=value settings over it */
	if (!env.empty()) {
		std::vector<std::wstring> vars;
		if (wchar_t *e = GetEnvironmentStringsW()) { for (wchar_t *v = e; *v; v += wcslen(v) + 1) vars.push_back(v); FreeEnvironmentStringsW(e); }
		for (const std::string &kv : env) {
			std::wstring w = wide(kv), k = w.substr(0, w.find(L'=') + 1);
			vars.erase(std::remove_if(vars.begin(), vars.end(), [&](const std::wstring &v) { return _wcsnicmp(v.c_str(), k.c_str(), k.size()) == 0; }), vars.end());
			vars.push_back(w);
		}
		for (const std::wstring &v : vars) { block += v; block += L'\0'; }
		block += L'\0';
	}
	STARTUPINFOW si = {}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
	PROCESS_INFORMATION pi = {};
	std::wstring dir = wide(g_root);
	BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
	                         env.empty() ? nullptr : &block[0], dir.c_str(), &si, &pi);
	CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
	if (!ok) { CloseHandle(rd); return false; }
	p.job = CreateJobObjectW(nullptr, nullptr);
	if (p.job) AssignProcessToJobObject(p.job, pi.hProcess);
	ResumeThread(pi.hThread); CloseHandle(pi.hThread);
	p.process = pi.hProcess; p.out = rd;
	return true;
}
static long proc_read(Proc &p, char *buf, size_t n) { DWORD got = 0; return ReadFile(p.out, buf, (DWORD)n, &got, nullptr) ? (long)got : -1; }
static int proc_wait(Proc &p) {   /* its exit code (a Cancel: 1) */
	WaitForSingleObject(p.process, INFINITE);
	DWORD code = 1; GetExitCodeProcess(p.process, &code);
	CloseHandle(p.out); CloseHandle(p.process);
	return (int)code;
}
static void proc_kill(Proc &p) { if (p.job) TerminateJobObject(p.job, 1); else if (p.process) TerminateProcess(p.process, 1); }
static void proc_done(Proc &p) { if (p.job) CloseHandle(p.job); p = Proc(); }
#else
extern char **environ;
static bool proc_start(Proc &p, const std::vector<std::string> &argv, const std::vector<std::string> &env) {
	/* everything the child needs made before fork: the launcher has threads, and a child of one may only exec */
	std::vector<char *> a; for (const std::string &s : argv) a.push_back((char *)s.c_str()); a.push_back(nullptr);
	std::vector<std::string> vars;
	for (char **e = environ; *e; e++) {
		bool over = false;
		for (const std::string &kv : env) over |= strncmp(*e, kv.c_str(), kv.find('=') + 1) == 0;
		if (!over) vars.push_back(*e);
	}
	vars.insert(vars.end(), env.begin(), env.end());
	std::vector<char *> envp; for (std::string &v : vars) envp.push_back(&v[0]); envp.push_back(nullptr);
	int fd[2];
	if (pipe(fd) != 0) return false;
	pid_t pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		dup2(fd[1], 1); dup2(fd[1], 2); close(fd[0]); close(fd[1]);
		if (chdir(g_root.c_str()) != 0) _exit(126);
		environ = envp.data();
		execvp(a[0], a.data());
		static const char msg[] = "can't run the command\n"; (void)!write(2, msg, sizeof msg - 1);
		_exit(127);
	}
	close(fd[1]);
	if (pid < 0) { close(fd[0]); return false; }
	setpgid(pid, pid);
	p.pid = pid; p.out = fd[0];
	return true;
}
static long proc_read(Proc &p, char *buf, size_t n) { return (long)read(p.out, buf, n); }
static int proc_wait(Proc &p) {   /* its exit code; killed by a signal: 128 + the signal */
	close(p.out);
	int st = 0; waitpid(p.pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
static void proc_kill(Proc &p) { if (p.pid > 0) kill(-p.pid, SIGTERM); }
static void proc_done(Proc &p) { p = Proc(); }
#endif

/* ---- one task at a time: its steps run in turn, their output into the log; Cancel kills the step running and runs no
 * more. A step announces itself with an "@@ <step>" line (the latest is the status). A failed step ends its group: group
 * 0 is the whole task; a folder search's chain gives each game its own (one game failing doesn't stop the next) */
struct Step {
	std::string say;                 /* "@@ " + this before it runs (a step with no command: only the line) */
	std::vector<std::string> argv;
	std::vector<std::string> env;    /* NAME=value over the launcher's environment */
	int group = 0;
};
/* ---- a newer Nativ: `vitarecomp update-check` in the background at start (it asks GitHub at most once a day, and only
 * an install whose dist named a release channel); its release, or nothing */
static struct { std::mutex m; std::string tag, page; bool dismissed = false; } g_update;
static void update_check_start(void) {
	std::thread([]() {
		Proc p; if (!proc_start(p, vitarecomp({"update-check"}), {})) return;
		std::string out; char b[512]; long n; while ((n = proc_read(p, b, sizeof b)) > 0) out.append(b, (size_t)n);
		proc_wait(p); proc_done(p);
		for (size_t a = 0; a < out.size(); ) {
			size_t e = out.find('\n', a); if (e == std::string::npos) e = out.size();
			char tag[64], page[512];
			if (sscanf(out.substr(a, e - a).c_str(), "update %63s %511s", tag, page) == 2 && !strncmp(page, "https://", 8)) {
				std::lock_guard<std::mutex> l(g_update.m); g_update.tag = tag; g_update.page = page;
			}
			a = e + 1;
		}
	}).detach();
}
/* ---- an install with files missing or cut short: the release's own list (.nativ-files, "size path" a line: dev/release.sh
 * writes it into the Windows zip, the one install a player unpacks), checked in the background at start. A player's Python
 * once lacked Lib\collections, an unpacking that stopped part way (2026-10-30): the firmware step died on a traceback */
static struct { std::mutex m; int bad = 0; std::string first; bool dismissed = false; } g_files;
static void files_check(void) {
	FILE *f = fopen((g_root + "/.nativ-files").c_str(), "rb"); if (!f) return;
	char line[2048]; int bad = 0; std::string first;
	while (fgets(line, sizeof line, f)) {
		char *sp = strchr(line, ' '); if (!sp) continue;
		*sp = 0; std::string rel(sp + 1); while (!rel.empty() && (rel.back() == '\n' || rel.back() == '\r')) rel.pop_back();
		struct stat st; const long long want = atoll(line);
		if (stat((g_root + "/" + rel).c_str(), &st) != 0 || (long long)st.st_size != want) { if (!bad++) first = rel; }
	}
	fclose(f);
	if (!bad) return;
	fprintf(stderr, "install: %d of the release's files missing or cut short (the first: %s)\n", bad, first.c_str());
#ifdef _WIN32
	std::replace(first.begin(), first.end(), '/', '\\');
#endif
	std::lock_guard<std::mutex> l(g_files.m); g_files.bad = bad; g_files.first = first;
}
static void files_check_start(void) { std::thread(files_check).detach(); }
static Step step(const std::string &say, std::vector<std::string> argv = {}, int group = 0) { Step s; s.say = say; s.argv = std::move(argv); s.group = group; return s; }
struct Task {
	std::string name, game, kind;
	std::vector<std::string> games;   /* the titles it works on: their cards show its step (game, or a search's chain's) */
	std::atomic<bool> running{false}, cancelled{false};
	bool ok = false;
	size_t log_from = 0;
	std::thread reader;
	std::mutex proc_lock; Proc proc;  /* the step running */
};
static Task g_task;
static std::mutex g_log_lock;
static std::string g_log;
static std::atomic<bool> g_refresh{false};   /* a task ended: the library's states may have changed */
static std::string g_failed;                  /* the last task that failed, for the banner (empty: none) */
static bool g_drawer;                         /* the activity drawer open (the whole log) */
static std::string g_log_file;                /* <prefs>/launcher.log: the log as each task ends, for a bug report (the launcher
                                               * before this one: launcher.prev.log) */
static bool g_log_bol = true;                 /* the log ends at a line's start: the next text gets its time */
static std::string g_step;                    /* the running task's latest "@@ step" (the status) */

/* every line of the log starts with the local time it arrived ("14:03:27 "), the steps' own output included (when the
 * launcher read it); an empty line gets none. A step's end says how long it took, and the task's end its whole time */
static std::string log_time(void) {
	time_t t = time(nullptr); struct tm tm;
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	char b[16]; strftime(b, sizeof b, "%H:%M:%S ", &tm);
	return b;
}
static std::string duration(Uint64 ms) {   /* 0:07, 16:43, 1:02:09 */
	unsigned long long s = ms / 1000; char b[32];
	if (s >= 3600) snprintf(b, sizeof b, "%llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
	else snprintf(b, sizeof b, "%llu:%02llu", s / 60, s % 60);
	return b;
}
static size_t log_stamp_len(const std::string &line) {   /* the "HH:MM:SS " its start carries (0: none) */
	return line.size() >= 9 && line[2] == ':' && line[5] == ':' && line[8] == ' ' && isdigit((unsigned char)line[0]) ? 9 : 0;
}
static void log_append(const char *s, size_t n) {
	std::lock_guard<std::mutex> l(g_log_lock);
	std::string now;
	for (size_t a = 0; a < n; ) {
		if (g_log_bol && s[a] != '\n' && s[a] != '\r') { if (now.empty()) now = log_time(); g_log += now; }
		const char *nl = (const char *)memchr(s + a, '\n', n - a);
		size_t e = nl ? (size_t)(nl - s) + 1 : n;
		g_log.append(s + a, e - a);
		g_log_bol = nl != nullptr;
		a = e;
	}
	if (g_log.size() > (1u << 20)) { size_t cut = g_log.size() - (1u << 19); g_log.erase(0, cut); g_task.log_from = g_task.log_from > cut ? g_task.log_from - cut : 0; }
}
static void log_append(const std::string &s) { log_append(s.data(), s.size()); }
static void log_line(const std::string &s) {   /* a line of the launcher's own, on a line of its own */
	bool bol; { std::lock_guard<std::mutex> l(g_log_lock); bol = g_log_bol; }
	log_append((bol ? "" : "\n") + s + "\n");
}
static void task_start(const std::string &name, const std::string &kind, const std::string &game, std::vector<Step> steps) {
	if (g_shot_mode) { g_shot_done.push_back("task " + name); return; }   /* (a screenshot run starts nothing) */
	if (g_task.running) return;
	if (g_task.reader.joinable()) g_task.reader.join();
	g_failed.clear();
	{ std::lock_guard<std::mutex> l(g_log_lock); g_step.clear(); }
	log_line("\n== " + name);
	{ std::lock_guard<std::mutex> l(g_log_lock); g_task.log_from = g_log.size(); }
	g_task.name = name; g_task.kind = kind; g_task.game = game; g_task.ok = false; g_task.cancelled = false; g_task.running = true;
	g_task.games.clear(); if (!game.empty()) g_task.games.push_back(game);
	g_task.reader = std::thread([steps = std::move(steps), name]() {
		std::vector<int> failed;   /* the groups that failed */
		int last = 0;
		const Uint64 task_t0 = SDL_GetTicks();
		for (const Step &s : steps) {
			if (g_task.cancelled) break;
			if (std::find(failed.begin(), failed.end(), s.group) != failed.end()) continue;
			if (!s.say.empty()) { { std::lock_guard<std::mutex> l(g_log_lock); g_step = s.say; } log_line("@@ " + s.say); }
			if (s.argv.empty()) continue;
			log_line("$ " + shown_args(s.argv));
			const Uint64 t0 = SDL_GetTicks();
			Proc p;
			{ std::lock_guard<std::mutex> l(g_task.proc_lock); if (g_task.cancelled || !proc_start(p, s.argv, s.env)) { if (!g_task.cancelled) log_append("can't start " + s.argv[0] + "\n"); last = 127; failed.push_back(s.group); if (s.group == 0) break; continue; } g_task.proc = p; }
			char buf[4096]; long n;
			while ((n = proc_read(p, buf, sizeof buf)) > 0) log_append(buf, (size_t)n);
			int code = proc_wait(p);
			{ std::lock_guard<std::mutex> l(g_task.proc_lock); proc_done(g_task.proc); }
			log_line("-- " + (s.say.empty() ? std::string() : s.say + ": ") + (g_task.cancelled ? "stopped after " : code ? "failed after " : "done in ") + duration(SDL_GetTicks() - t0));
			if (code != 0) { last = code; failed.push_back(s.group); if (s.group == 0) break; }
		}
		bool stopped = g_task.cancelled;
		g_task.ok = failed.empty() && !stopped;
		char end[96]; snprintf(end, sizeof end, "== %s (exit %d) in ", stopped ? "stopped" : "done", stopped ? -1 : last);
		log_line(end + duration(SDL_GetTicks() - task_t0));
		if (!g_task.ok && !stopped) g_failed = name;
		if (!g_log_file.empty()) {
			std::string copy; { std::lock_guard<std::mutex> l(g_log_lock); copy = g_log; }
			if (FILE *f = fopen(g_log_file.c_str(), "wb")) { fwrite(copy.data(), 1, copy.size(), f); fclose(f); }
		}
		g_task.running = false;
		g_refresh = true;
	});
}
static void task_cancel(void) {
	if (!g_task.running) return;
	std::lock_guard<std::mutex> l(g_task.proc_lock);
	g_task.cancelled = true;
	proc_kill(g_task.proc);
}
static std::string task_status(void) {   /* the latest "@@ step" of the running task, else its last line of output (no time) */
	std::lock_guard<std::mutex> l(g_log_lock);
	if (!g_step.empty()) return g_step;
	size_t from = g_task.log_from < g_log.size() ? g_task.log_from : g_log.size();
	size_t e = g_log.size(); while (e > from && (g_log[e - 1] == '\n' || g_log[e - 1] == '\r' || g_log[e - 1] == ' ')) e--;
	size_t b = g_log.rfind('\n', e ? e - 1 : 0); b = b == std::string::npos || b < from ? from : b + 1;
	std::string s = e > b ? g_log.substr(b, e - b) : "";
	s.erase(0, log_stamp_len(s));
	if (s.size() > 90) s = s.substr(0, 87) + "...";
	return s;
}

/* ---- the library: packs + whatever is under dump/decrypted */
struct Game {
	std::string id, name, elf = "eboot.bin.elf", base = "game";   /* base: the pack's BASE= (the host binary is <base>_host) */
	std::string version;      /* this copy's (dump/decrypted/<id>/version, vitarecomp import's); "-": the pack's one build, no number recorded */
	std::vector<std::pair<std::string, std::string>> versions;   /* the pack's versions: (version, recommended | supported) */
	std::string update_note;  /* the pack's UPDATE_NOTE: what its recommended version brings */
	std::string cover, icon;  /* the copy's own art (art_paths): "" when it has none */
	std::string region;       /* the pack's REGION: the release it plays (US) */
	bool pack = false, imported = false, built = false;
	std::string missing;      /* a folder its copy reads in place (vitarecomp add --in-place: app0.links) that isn't there: its drive unplugged */
};
static std::string recommended_version(const Game &g) {
	for (auto &v : g.versions) if (v.second == "recommended") return v.first;
	return "";
}
static std::string played_versions(const Game &g) {   /* "1.00, 1.12" (the numbers recorded) */
	std::string s;
	for (auto &v : g.versions) if (v.first != "-") s += (s.empty() ? "" : ", ") + v.first;
	return s;
}
static std::vector<Game> g_games;
static bool g_firmware;
static std::string g_firmware_version;

/* a game pack (packs/<title>/pack.toml; the format: pipeline/packconf.py), as far as the launcher reads it: the top-level
 * settings (KEY = "text" or a number) and each [versions."<version>"] table's status, in the pack's order */
struct Pack { std::map<std::string, std::string> top; std::vector<std::pair<std::string, std::string>> versions; };
static Pack pack_read(const std::string &path) {
	Pack p; std::string text, table;
	if (FILE *f = fopen(path.c_str(), "rb")) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) text.append(b, n); fclose(f); }
	for (size_t at = 0; at < text.size();) {
		size_t end = text.find('\n', at); if (end == std::string::npos) end = text.size();
		const std::string l = text.substr(at, end - at); at = end + 1;
		size_t s = l.find_first_not_of(" \t\r"); if (s == std::string::npos || l[s] == '#') continue;
		if (l[s] == '[') {
			size_t e = l.rfind(']'); table = e != std::string::npos && e > s ? l.substr(s + 1, e - s - 1) : "";
			if (table.size() > 11 && table.compare(0, 10, "versions.\"") == 0 && table.back() == '"') p.versions.push_back({table.substr(10, table.size() - 11), "supported"});
			continue;
		}
		size_t eq = l.find('=', s), v = eq == std::string::npos ? eq : l.find_first_not_of(" \t", eq + 1);
		if (eq == std::string::npos || eq == s || v == std::string::npos) continue;
		const std::string key = l.substr(s, l.find_last_not_of(" \t", eq - 1) - s + 1);
		std::string val;
		if (l[v] == '"') { for (v++; v < l.size() && l[v] != '"'; v++) { if (l[v] == '\\' && v + 1 < l.size()) v++; val += l[v]; } }
		else { size_t e = l.find_first_of(" \t#\r", v); val = l.substr(v, e == std::string::npos ? std::string::npos : e - v); }
		if (table.empty()) p.top[key] = val;
		else if (key == "status" && table.compare(0, 9, "versions.") == 0 && !p.versions.empty()) p.versions.back().second = val;
	}
	return p;
}
static std::string pack_value(const Pack &p, const char *key) { auto it = p.top.find(key); return it == p.top.end() ? "" : it->second; }
static std::string first_line(const std::string &path) {
	FILE *f = fopen(path.c_str(), "r"); if (!f) return "";
	char line[256] = ""; if (!fgets(line, sizeof line, f)) line[0] = 0;
	fclose(f);
	std::string v = line; while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
	return v;
}
static std::vector<std::string> split_ws(const std::string &v) {   /* "A B  C" -> {A, B, C} */
	std::vector<std::string> out; std::string cur;
	for (char c : v) { if (c == ' ' || c == '	' || c == ',') { if (!cur.empty()) out.push_back(cur), cur.clear(); } else cur += c; }
	if (!cur.empty()) out.push_back(cur);
	return out;
}
static bool title_like(const char *s) {   /* PCSA00029: four letters, five digits */
	if (strlen(s) != 9) return false;
	for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return false;
	for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return false;
	return true;
}
static void art_paths(Game &g);
static void count_licences(void);
static void lib_load(void);
static std::map<std::string, std::string> g_other_of;   /* a pack's OTHER_TITLES: another release's id -> the pack that plays it */
static void library_scan(void) {
	count_licences();
	for (auto it = g_tex.begin(); it != g_tex.end();) it = it->second.t ? std::next(it) : g_tex.erase(it);   /* (retry art not found before) */
	std::map<std::string, Game> by;
	std::map<std::string, std::string> &other_of = g_other_of; other_of.clear();
	std::string packs = g_root + "/packs";
	if (DIR *d = opendir(packs.c_str())) {
		while (dirent *e = readdir(d)) {
			std::string n = e->d_name;
			const std::string path = packs + "/" + n + "/pack.toml";
			if (n.size() != 9 || !title_like(n.c_str()) || !exists(path)) continue;
			const Pack p = pack_read(path);
			Game &g = by[n];
			g.id = n; g.pack = true;
			g.name = pack_value(p, "NAME");
			std::string elf = pack_value(p, "ELF"); if (!elf.empty()) g.elf = elf;
			std::string base = pack_value(p, "BASE"); if (!base.empty()) g.base = base;
			g.versions = p.versions;
			g.update_note = pack_value(p, "UPDATE_NOTE");
			g.region = pack_value(p, "REGION");
			for (const std::string &o : split_ws(pack_value(p, "OTHER_TITLES"))) other_of[o] = n;
		}
		closedir(d);
	}
	std::string dumps = g_dump + "/decrypted";
	if (DIR *d = opendir(dumps.c_str())) {
		while (dirent *e = readdir(d)) {
			if (!title_like(e->d_name)) continue;
			std::string w = dumps + "/" + e->d_name;
			if (!exists(w + "/elf")) continue;   /* a scratch or analysis folder, not an imported game */
			Game &g = by[e->d_name]; g.id = e->d_name;
			/* a copy without its own pack plays with the pack listing it in OTHER_TITLES, as the pipeline does
			 * (packconf.pack_for): that pack's name, main program and versions, the copy's own title id */
			auto o = other_of.find(g.id);
			if (!g.pack && o != other_of.end() && by.count(o->second)) { const std::string id = g.id; g = by[o->second]; g.id = id; }
		}
		closedir(d);
	}
	g_games.clear();
	for (auto &kv : by) {
		Game g = kv.second;
		std::string w = dumps + "/" + g.id;
		g.imported = exists(w + "/elf/" + g.elf);
#ifdef _WIN32
		g.built = exists(w + "/recomp/" + g.base + "_host.exe");
#else
		g.built = exists(w + "/recomp/" + g.base + "_host");
#endif
		g.version = first_line(w + "/version");
		for (const char *lf : {"/app0.links", "/patch0.links"})
			if (FILE *f = g.missing.empty() ? fopen((w + lf).c_str(), "r") : nullptr) {
				char line[4096];
				while (g.missing.empty() && fgets(line, sizeof line, f)) { std::string t = line; while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back(); if (!t.empty() && !exists(t)) g.missing = t; }
				fclose(f);
			}
		if (g.imported) art_paths(g);
		if (g.name.empty() && g.imported) g.name = sfo_title_file(w + "/recomp/fs/app0/sce_sys/param.sfo");   /* (no game pack: the game's own title, not its id) */
		if (g.name.empty()) g.name = g.id;
		g_games.push_back(g);
	}
	/* a console dump's modules, or a firmware update file's from setup (dump/firmware/<version>/) */
	g_firmware = exists(dumps + "/us/libgxm_es4.suprx.elf"); g_firmware_version.clear();
	std::string fw = g_dump + "/firmware";
	if (DIR *d = opendir(fw.c_str())) {   /* the newest update file installed */
		while (dirent *e = readdir(d))
			if (e->d_name[0] != '.' && exists(fw + "/" + e->d_name + "/os0/us/libgxm_es4.suprx.elf") && std::string(e->d_name) > g_firmware_version) { g_firmware = true; g_firmware_version = e->d_name; }
		closedir(d);
	}
	if (g_firmware && g_firmware_version.empty()) g_firmware_version = "from a console dump";
	lib_load();
}
/* ---- a card's art, from the copy's own sce_sys: pic0.png (960x544, the game's background picture); else its LiveArea's
 * background (template.xml <livearea-background><image>, 840x500); else its LiveArea gate's start image (280x158).
 * (Nothing of a game ships with Nativ: a game not added yet keeps its typeset placeholder.) */
static std::string read_file(const std::string &path) {
	std::string s; FILE *f = fopen(path.c_str(), "rb"); if (!f) return s;
	char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0 && s.size() < (1u << 20)) s.append(buf, n);
	fclose(f);
	return s;
}
static std::string tag_text(const std::string &x, const char *tag, size_t from = 0) {   /* the text right after <tag ...>, trimmed */
	size_t a = x.find(tag, from); if (a == std::string::npos) return "";
	a = x.find('>', a); if (a == std::string::npos) return "";
	size_t b = x.find('<', ++a); std::string v = x.substr(a, b == std::string::npos ? std::string::npos : b - a);
	size_t s0 = v.find_first_not_of(" \t\r\n"), s1 = v.find_last_not_of(" \t\r\n");
	return s0 == std::string::npos ? "" : v.substr(s0, s1 - s0 + 1);
}
static void art_paths(Game &g) {
	std::string sys = g_dump + "/decrypted/" + g.id + "/recomp/fs/app0/sce_sys/", la = sys + "livearea/contents/";
	g.icon = exists(sys + "icon0.png") ? sys + "icon0.png" : "";
	g.cover.clear();
	if (exists(sys + "pic0.png")) { g.cover = sys + "pic0.png"; return; }
	std::string x = read_file(la + "template.xml");
	size_t bg = x.find("<livearea-background");
	for (const std::string &v : {bg == std::string::npos ? std::string() : tag_text(x, "<image", bg), tag_text(x, "<startup-image")})
		if (!v.empty() && v.find('/') == std::string::npos && exists(la + v)) { g.cover = la + v; return; }
}

/* ---- the native file dialogs (SDL3): their callback lands the chosen path in a field */
static std::mutex g_pick_lock;
static std::string *g_pick_target;
static std::string g_pick_result; static bool g_pick_done;
static void SDLCALL picked(void *, const char *const *files, int) {
	std::lock_guard<std::mutex> l(g_pick_lock);
	if (files && files[0]) { g_pick_result = files[0]; g_pick_done = true; }
}
static bool use_in_app_browser(void);
static void browse_open(std::string *target, int kind);
static void pick_file(SDL_Window *w, std::string *target, const SDL_DialogFileFilter *f, int nf, int kind) { if (use_in_app_browser()) { browse_open(target, kind); return; } if (g_shot_mode) { g_shot_done.push_back("pick a file"); return; } g_pick_target = target; SDL_ShowOpenFileDialog(picked, nullptr, w, f, nf, nullptr, false); }
static void pick_folder(SDL_Window *w, std::string *target) { if (use_in_app_browser()) { browse_open(target, BROWSE_FOLDER); return; } if (g_shot_mode) { g_shot_done.push_back("pick a folder"); return; } g_pick_target = target; SDL_ShowOpenFolderDialog(picked, nullptr, w, nullptr, false); }
/* what each file dialog shows first. A game file is a PKG (vitadecrypt.game reads a file only by a PKG's magic; zip/vpk
 * are not read); a licence is a .rif or a work.bin; the firmware is Sony's .PUP. All files last, for a renamed one */
static const SDL_DialogFileFilter k_pick_pkg[] = {{"PS Vita game or update (*.pkg)", "pkg"}, {"All files (*.*)", "*"}};
static const SDL_DialogFileFilter k_pick_licence[] = {{"Licence file (*.rif, work.bin)", "rif;bin"}, {"All files (*.*)", "*"}};
static const SDL_DialogFileFilter k_pick_pup[] = {{"PS Vita system update (*.PUP)", "pup"}, {"All files (*.*)", "*"}};
static void pick_pkg(SDL_Window *w, std::string *target) { pick_file(w, target, k_pick_pkg, 2, BROWSE_PKG); }
static void pick_licence(SDL_Window *w, std::string *target) { pick_file(w, target, k_pick_licence, 2, BROWSE_LICENCE); }
static void pick_pup(SDL_Window *w, std::string *target) { pick_file(w, target, k_pick_pup, 2, BROWSE_PUP); }
static void pick_poll(void) {
	std::lock_guard<std::mutex> l(g_pick_lock);
	if (g_pick_done && g_pick_target) *g_pick_target = g_pick_result;
	g_pick_done = false;
}
static void osk_open(std::string *target, const char *label);
static const std::string *g_osk_done;   /* (the field the on-screen keyboard just wrote: its caller saves it, input_done) */
static bool input_done(const std::string &s) {   /* a field's edit finished: typed and left, or the keyboard's Done */
	if (g_osk_done == &s) { g_osk_done = nullptr; return true; }
	return ImGui::IsItemDeactivatedAfterEdit();
}
static bool input_text(const char *label, std::string &s, float w = -1, const char *hint = nullptr) {   /* ImGui::InputText over a std::string */
	if (use_in_app_browser()) {   /* (UI-01g: a controller or a finger: the field is a button that opens the on-screen keyboard) */
		const ImGuiStyle &st = ImGui::GetStyle();
		const ImVec2 size = ImGui::CalcItemSize(ImVec2(w, 0), ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
		std::string shown = s.empty() ? std::string(hint ? hint : "") : s;
		const float room = size.x - st.FramePadding.x * 2;
		if (room > 0 && ImGui::CalcTextSize(shown.c_str()).x > room) {   /* (a long path: its end, which names the folder) */
			while (shown.size() > 1 && ImGui::CalcTextSize(("..." + shown).c_str()).x > room) shown.erase(0, 1);
			shown = "..." + shown;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Button, st.Colors[ImGuiCol_FrameBg]); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, st.Colors[ImGuiCol_FrameBgHovered]);
		if (s.empty()) ImGui::PushStyleColor(ImGuiCol_Text, st.Colors[ImGuiCol_TextDisabled]);
		const bool open = ImGui::Button((shown + "###" + label).c_str(), size);
		if (s.empty()) ImGui::PopStyleColor();
		ImGui::PopStyleColor(2); ImGui::PopStyleVar();
		if (open) osk_open(&s, label);
		return false;
	}
	char buf[2048]; snprintf(buf, sizeof buf, "%s", s.c_str());
	ImGui::SetNextItemWidth(w);
	bool ch = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf) : ImGui::InputText(label, buf, sizeof buf);
	if (ch) s = buf;
	return ch;
}
static bool path_row(const char *id, std::string &s, const char *hint) {   /* a path field with Browse... beside it: true when Browse is pressed */
	float bw = ImGui::CalcTextSize("Browse…").x + ImGui::GetStyle().FramePadding.x * 2;
	input_text(id, s, -bw - ImGui::GetStyle().ItemSpacing.x, hint);
	ImGui::SameLine();
	ImGui::PushID(id); bool r = ImGui::Button("Browse…"); ImGui::PopID();
	return r;
}

/* ---- the stages, as commands */
static void build_steps(std::vector<Step> &steps, const std::string &id, int group = 0) {   /* analyse, recompile, build: each its own "@@" step */
	steps.push_back(step("Step 1 of 3: analysing the game", vitarecomp({"analyze", id}), group));
	steps.push_back(step("Step 2 of 3: recompiling it to C", vitarecomp({"recompile", id}), group));
	steps.push_back(step("Step 3 of 3: compiling the native program", vitarecomp({"build", id}), group));
	steps.push_back(step("Ready to play", {}, group));
}
static void start_build(const Game &g) { std::vector<Step> s; build_steps(s, g.id); task_start("Building " + g.name, "build", g.id, s); }
static bool needs_online_name(const Game &g);   /* (below: an online game, and no name of the player's own yet) */
static std::string g_name_game;                  /* the game waiting on the online name prompt (online_name_modal) */
static void start_play_now(const Game &g);
static void start_play(const Game &g) {
	if (needs_online_name(g)) { g_name_game = g.id; return; }
	start_play_now(g);
}
static void start_play_now(const Game &g) {   /* (ATIV_LAUNCHER: the game's menu offers "Back to the library"; the launcher comes back to the front when it ends) */
	/* ready first: a build older than these tools (Nativ was updated) is brought up to date before the game starts, so an
	 * update reaches every game; when it's current that's a moment */
	Step r = step("Checking " + g.name + "'s build is up to date", vitarecomp({"ready", g.id}));
	Step s = step("Running " + g.name, vitarecomp({"run", g.id})); s.env.push_back("ATIV_LAUNCHER=1");
	task_start("Playing " + g.name, "play", g.id, {r, s});
}

/* ---- a controller's L1 / R1 (or Ctrl+Tab, Ctrl+Shift+Tab) step a row of tabs: -1, +1, or 0 -- not while a control is held */
static bool g_osk_open_any(void);
static int tab_step(void) {
	if (ImGui::IsAnyItemActive() || g_osk_open_any()) return 0;
	const bool ctrl = ImGui::GetIO().KeyCtrl, shift = ImGui::GetIO().KeyShift;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Tab, false))) return -1;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Tab, false))) return 1;
	return 0;
}
/* ---- nothing cut off (UI-01b): rows of buttons wrap, dialogs fit the window */
/* ---- touch (UI-01d): finger-sized targets, swipe to scroll, information a tap away. g_touch: the last pointer was a touch
 * screen (touch_style sets it each frame; the SDL backend tags touch-driven pointer events) */
static bool g_touch;
static float touch_target(void) { return ImGui::GetFontSize() * 2.75f; }   /* (44 px at the UI's scale: a fingertip) */
static bool small_button(const char *label) { return g_touch ? ImGui::Button(label) : ImGui::SmallButton(label); }
static bool link_button(const char *label) { return g_touch ? ImGui::Button(label) : ImGui::TextLink(label); }
static bool menu_item(const char *label, const char *hint = nullptr) {   /* a menu's entry; by touch one a finger tall, its hint in the label */
	if (!g_touch) return ImGui::MenuItem(label, hint);
	std::string l = hint ? std::string(label) + "  (" + hint + ")" : std::string(label);
	return ImGui::Selectable(l.c_str(), false, 0, ImVec2(0, touch_target()));
}
/* ---- button hints (UI-01e): "A Select  B Back" in the controller's own names -- shown while a controller or the keyboard
 * moves the focus, never for a mouse or a finger (they have the buttons themselves). The family: the first pad's SDL type,
 * the Deck from Steam's SteamDeck=1 (pad_family.h); ATIV_SHOT_PAD=ps|xbox|deck|switch sets it in a screenshot run */
static int g_pad_fam = PAD_FAM_PS;
static int pad_family_now(void) {
	if (const char *e = getenv("ATIV_SHOT_PAD"))
		return !strcmp(e, "xbox") ? PAD_FAM_XBOX : !strcmp(e, "deck") ? PAD_FAM_DECK : !strcmp(e, "switch") ? PAD_FAM_SWITCH : PAD_FAM_PS;
	int n = 0; SDL_JoystickID *ids = SDL_GetGamepads(&n);
	const int type = ids && n > 0 ? (int)SDL_GetGamepadTypeForID(ids[0]) : -1;
	SDL_free(ids);
	const char *deck = getenv("SteamDeck");
	return pad_family(type, deck && !strcmp(deck, "1"));
}
static bool hints_keyboard(void) { return GImGui->NavInputSource == ImGuiInputSource_Keyboard; }
static bool hints_visible(void) {
	const ImGuiContext &g = *GImGui;
	return !g_touch && g.NavCursorVisible && (g.NavInputSource == ImGuiInputSource_Gamepad || g.NavInputSource == ImGuiInputSource_Keyboard);
}
/* one button as a badge: a round one for a face button (PlayStation's drawn as its shape), a pill for a key or a shoulder */
static float badge_w(const char *name) {
	const float h = ImGui::GetTextLineHeight();
	const bool round = strlen(name) == 1 || (g_pad_fam == PAD_FAM_PS && !hints_keyboard() && (!strcmp(name, "Cross") || !strcmp(name, "Circle") || !strcmp(name, "Square") || !strcmp(name, "Triangle")));
	return round ? h * 1.3f : ImGui::CalcTextSize(name).x + h * 0.9f;
}
static void badge_draw(ImDrawList *dl, ImVec2 p, const char *name) {
	const float h = ImGui::GetTextLineHeight(), w = badge_w(name), r = h * 0.62f;
	const ImVec2 c(p.x + w * 0.5f, p.y + h * 0.5f);
	const ImU32 fill = ui::u32(ui::card_hi), edge = ui::u32(ui::border), ink = ui::u32(ui::text);
	const bool ps = g_pad_fam == PAD_FAM_PS && !hints_keyboard();
	if (w <= h * 1.31f) {
		dl->AddCircleFilled(c, r, fill); dl->AddCircle(c, r, edge, 0, 1.0f);
		const float s = r * 0.48f, t = std::max(1.5f, h * 0.09f);
		if (ps && !strcmp(name, "Cross")) { dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ink, t); dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), ink, t); }
		else if (ps && !strcmp(name, "Circle")) dl->AddCircle(c, s * 1.1f, ink, 0, t);
		else if (ps && !strcmp(name, "Square")) dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ink, 0, 0, t);
		else if (ps && !strcmp(name, "Triangle")) dl->AddTriangle(ImVec2(c.x, c.y - s * 1.1f), ImVec2(c.x + s * 1.1f, c.y + s * 0.8f), ImVec2(c.x - s * 1.1f, c.y + s * 0.8f), ink, t);
		else { const ImVec2 ts = ImGui::CalcTextSize(name); dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ink, name); }
	} else {
		dl->AddRectFilled(ImVec2(p.x, c.y - r), ImVec2(p.x + w, c.y + r), fill, r); dl->AddRect(ImVec2(p.x, c.y - r), ImVec2(p.x + w, c.y + r), edge, r);
		const ImVec2 ts = ImGui::CalcTextSize(name); dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ink, name);
	}
}
/* "<select> Select  <back> Back": as one row, its width first (a place that has room for it) */
static std::vector<std::pair<std::string, std::string>> hint_items(bool back) {
	const PadNames *n = pad_names_of(g_pad_fam); const bool kb = hints_keyboard();
	std::vector<std::pair<std::string, std::string>> v{{kb ? "Enter" : n->select, "Select"}};
	if (back) v.push_back({kb ? "Esc" : n->back, "Back"});
	return v;
}
static float hints_width(bool back) {
	const float h = ImGui::GetTextLineHeight(); float w = 0;
	for (auto &it : hint_items(back)) w += (w > 0 ? h * 1.1f : 0) + badge_w(it.first.c_str()) + h * 0.35f + ImGui::CalcTextSize(it.second.c_str()).x;
	return w;
}
static void hints_draw(bool back) {   /* at the cursor, one line; a Dummy holds its place */
	const float h = ImGui::GetTextLineHeight(), base = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
	ImDrawList *dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos(); p.y += base; const float x0 = p.x;
	bool first = true;
	for (auto &it : hint_items(back)) {
		if (!first) p.x += h * 1.1f;
		first = false;
		badge_draw(dl, p, it.first.c_str()); p.x += badge_w(it.first.c_str()) + h * 0.35f;
		dl->AddText(p, ui::u32(ui::muted), it.second.c_str()); p.x += ImGui::CalcTextSize(it.second.c_str()).x;
	}
	ImGui::Dummy(ImVec2(p.x - x0, h));
}
static void dialog_hints(void) {   /* a dialog's foot: the hints on a line of their own under its buttons */
	if (!hints_visible()) return;
	ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.15f));
	hints_draw(true);
}
static float button_w(const char *label) { return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2; }
static void same_line_fits(float next_w) {   /* SameLine only when the next item, next_w wide, still fits on this line; else it starts the next one */
	const ImVec2 at = ImGui::GetCursorScreenPos();   /* (after an item: the next line's start, so this plus the room left is the row's right edge) */
	const float right = at.x + ImGui::GetContentRegionAvail().x;
	if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + next_w <= right + 0.5f) ImGui::SameLine();
}
/* a dialog (BeginPopupModal) at most 90 % of the window's height: the content between dialog_body_begin() and
 * dialog_body_end() scrolls when it's taller than the room left, and what follows (the dialog's buttons) stays in view
 * below it. (Its width already is at most 94 % of the window: each dialog's SetNextWindowSize. A size constraint on the
 * dialog itself as well gave every dialog a needless scrollbar, 2026-11-08.) */
static void dialog_body_begin(void) {
	const ImGuiViewport *vp = ImGui::GetMainViewport(); const ImGuiStyle &s = ImGui::GetStyle(); const float em = ImGui::GetFontSize();
	const float foot = ImGui::GetFrameHeightWithSpacing() * (vp->WorkSize.x < 44 * em ? 2 : 1) + em * 0.4f
	                 + (hints_visible() ? ImGui::GetTextLineHeightWithSpacing() + em * 0.15f : 0);   /* (the hint line under the buttons) */   /* (a narrow window: room for the buttons on two lines) */
	float maxh = vp->WorkSize.y * 0.90f - s.WindowPadding.y * 2 - (ImGui::GetCursorPosY() - s.WindowPadding.y) - foot;
	if (maxh < ImGui::GetFrameHeight() * 2) maxh = ImGui::GetFrameHeight() * 2;
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, maxh));
	/* (a little room each side, the text where it was: a glyph that reaches past its line's start -- a j's hook -- isn't cut) */
	const float pad = em * 0.25f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() - pad);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, 0));
	ImGui::BeginChild("##body", ImVec2(ImGui::GetContentRegionAvail().x + pad, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
	ImGui::PopStyleVar();
}
/* B on a controller, or Esc: the open dialog's Back / Cancel / Close / Not now. Not when that press was taken by ImGui to
 * end an edit (a field typed in, a slider held) or to close a menu over the dialog: those were busy the frame before */
static bool g_ui_busy_prev;
static bool dialog_back(void) {
	/* (NoPopupHierarchy: only the topmost dialog takes it -- ImGui counts a dialog opened inside this one, the browser, the
	 * keyboard, a Change, as this one's child, and B there closed both) */
	if (g_ui_busy_prev || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy)) return false;
	return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}
static void dialog_body_end(void) { ImGui::EndChild(); ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.2f)); }
/* ---- the launcher's own file and folder browser (UI-01f): a controller or a finger can't use the system's file dialog, so a
 * pick made with either opens this one instead (a mouse keeps the system's). Places first (drives, home, Downloads,
 * Vita3K, the library folders, on Linux the drives and SD cards under /run/media), then a folder's subfolders and the
 * files this pick wants (file_browse.h). A / tap opens a folder or picks a file, B goes up a folder (closes at the
 * places), "Use this folder" picks the folder shown. It opens inside the dialog that asked for it (a dialog opened
 * beside it would close that one). The last folder used is where the next pick starts (hub.ini browse_last) */
static std::string vita3k_default_dir(void);
static struct { bool open = false, appearing = false, top = false; std::string *target = nullptr; int kind = BROWSE_FOLDER; std::string dir; std::vector<BrowseEntry> items; } g_browse;
static bool use_in_app_browser(void) {
	return g_touch || (GImGui->NavCursorVisible && GImGui->NavInputSource == ImGuiInputSource_Gamepad) || (getenv("ATIV_BROWSER") && *getenv("ATIV_BROWSER") == '1');
}
static void browse_go(const std::string &dir) { g_browse.dir = dir; g_browse.items = dir.empty() ? std::vector<BrowseEntry>() : browse_list(dir, g_browse.kind); g_browse.appearing = true; }
static bool is_dir(const std::string &p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
static void browse_open(std::string *target, int kind) {
	g_browse.open = true; g_browse.target = target; g_browse.kind = kind; g_browse.top = GImGui->OpenPopupStack.empty();
	std::string start = target && !target->empty() ? (kind == BROWSE_FOLDER ? *target : browse_parent(*target)) : g_prefs["browse_last"];
	browse_go(is_dir(start) ? start : std::string());
}
static std::vector<std::pair<std::string, std::string>> browse_places(void) {   /* (label, folder) */
	std::vector<std::pair<std::string, std::string>> v;
	auto add = [&](const std::string &label, std::string path) { for (char &c : path) if (c == '\\') c = '/'; if (is_dir(path)) v.push_back({label, path}); };
	if (!g_prefs["browse_last"].empty()) add("Last used: " + g_prefs["browse_last"], g_prefs["browse_last"]);
#ifdef _WIN32
	const DWORD drives = GetLogicalDrives();
	for (int i = 2; i < 26; i++) if (drives & (1u << i)) { const std::string d = std::string(1, (char)('A' + i)) + ":/"; v.push_back({"Drive " + d.substr(0, 2), d}); }
	const char *home = getenv("USERPROFILE");
#else
	const char *home = getenv("HOME"); const char *user = getenv("USER");
	if (user) { add("Drives and SD cards", std::string("/run/media/") + user); add("Drives", std::string("/media/") + user); }
	add("This computer", "/");
#endif
	if (home) { const std::string h = home; add("Home", h); for (const char *sub : {"Downloads", "Documents", "Desktop"}) add(sub, h + "/" + sub); }
	const std::string v3k = vita3k_default_dir(); if (!v3k.empty()) add("Vita3K", v3k);
	for (const LibFolder &f : g_lib_folders) add("Library folder: " + f.path, f.path);
	return v;
}
static void browser_modal(void) {
	if (!g_browse.open) return;
	const ImGuiViewport *vp = ImGui::GetMainViewport(); const float em = ImGui::GetFontSize();
	if (!ImGui::IsPopupOpen("##browse")) ImGui::OpenPopup("##browse");
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 44 * em ? vp->WorkSize.x * 0.94f : 40 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("##browse", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	static const char *const titles[] = {"Choose a folder", "Choose the game's PKG", "Choose the licence file", "Choose the system update file"};
	ui::heading(titles[g_browse.kind >= 0 && g_browse.kind <= 3 ? g_browse.kind : 0], em * 1.4f);
	ImGui::PushTextWrapPos(0); ui::muted_wrapped(g_browse.dir.empty() ? "Places" : g_browse.dir.c_str()); ImGui::PopTextWrapPos();
	dialog_body_begin();
	bool close = false, going = false; std::string go, pick;
	const float rowh = g_touch ? touch_target() : ImGui::GetFrameHeight();
	int idx = 0;
	auto row = [&](const std::string &label) {   /* one entry, finger-tall by touch; the first takes the focus when a folder opens */
		ImGui::PushID(idx);
		const bool r = ImGui::Selectable(("   " + label).c_str(), false, 0, ImVec2(0, rowh));   /* (a little in from the row's edge) */
		if (idx == 0 && g_browse.appearing && GImGui->NavCursorVisible) ImGui::SetKeyboardFocusHere(-1);
		ImGui::PopID(); idx++;
		return r;
	};
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));   /* (a row's name in its middle) */
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));   /* (rows exactly the list's size: a Selectable reaches half the spacing past each side; the rows are a button tall instead) */
	if (g_browse.dir.empty()) {
		for (auto &pl : browse_places()) if (row(pl.first)) { go = pl.second; going = true; }
	} else {
		if (row(".. (up one folder)")) { go = browse_parent(g_browse.dir); going = true; }
		for (const BrowseEntry &e : g_browse.items)
			if (row(e.dir ? e.name + "/" : e.name)) { if (e.dir) { go = browse_join(g_browse.dir, e.name); going = true; } else pick = browse_join(g_browse.dir, e.name); }
		if (g_browse.items.empty()) ui::muted_wrapped(g_browse.kind == BROWSE_FOLDER ? "No folders in here." : "Nothing in here to pick: no folders, and no file of the kind wanted.");
	}
	ImGui::PopStyleVar(2);
	dialog_body_end();
	if (g_browse.kind == BROWSE_FOLDER && !g_browse.dir.empty()) {
		if (ui::primary_button("Use this folder") || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) pick = g_browse.dir;   /* (Menu / Options: the screen's main action) */
		same_line_fits(button_w("Cancel"));
	}
	if (ImGui::Button("Cancel")) close = true;
	if (dialog_back()) { if (g_browse.dir.empty()) close = true; else { go = browse_parent(g_browse.dir); going = true; } }   /* (B: up a folder) */
	dialog_hints();
	if (!pick.empty()) {
		if (g_browse.target) *g_browse.target = pick;
		prefs_set("browse_last", g_browse.kind == BROWSE_FOLDER ? pick : browse_parent(pick));
		if (g_shot_mode) g_shot_done.push_back("picked " + pick);
		close = true;
	}
	if (going) browse_go(go); else g_browse.appearing = false;
	if (close) { g_browse.open = false; ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}
/* ---- the on-screen keyboard (UI-01g): a controller or a finger types a path, an address or a name with it. Keys for what
 * those need -- digits, letters, : / \ . - _ and space; Shift for capitals. A / tap types a key, Y deletes, L1 is Shift,
 * R1 a space, Menu / Options or Done keeps the text, B or Cancel leaves the field as it was. A keyboard plugged in types
 * into it too. It opens inside the dialog that asked (like the browser) */
static struct { bool open = false, top = false, shift = false, appearing = false; std::string *target = nullptr; std::string text, title; } g_osk;
static const char *osk_title(const char *label) {
	static const std::pair<const char *, const char *> t[] = {{"##scanpath", "The folder to search"}, {"##addpath", "The game's PKG or folder"}, {"##lic", "The licence file"},
		{"##pup", "The system update file"}, {"##vaddr", "VitaShell's address"}, {"##oid", "Your online name"}, {"##name_oid", "Your online name"},
		{"##setup_oid", "Your online name"}, {"##srv", "The private server"}};
	for (auto &e : t) if (!strcmp(label, e.first)) return e.second;
	return "Type";
}
static void osk_open(std::string *target, const char *label) {
	g_osk.open = true; g_osk.top = GImGui->OpenPopupStack.empty(); g_osk.shift = false; g_osk.appearing = true;
	g_osk.target = target; g_osk.text = *target; g_osk.title = osk_title(label);
}
static void osk_modal(void) {
	if (!g_osk.open) return;
	const ImGuiViewport *vp = ImGui::GetMainViewport(); const float em = ImGui::GetFontSize();
	if (!ImGui::IsPopupOpen("##osk")) ImGui::OpenPopup("##osk");
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 44 * em ? vp->WorkSize.x * 0.96f : 40 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.2f * em, 1.1f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("##osk", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	ui::heading(g_osk.title.c_str(), em * 1.25f);
	{	/* the text so far, its end in view, a caret after it */
		const ImGuiStyle &st = ImGui::GetStyle(); const float fw = ImGui::GetContentRegionAvail().x, room = fw - st.FramePadding.x * 2 - em;
		std::string shown = g_osk.text;
		if (ImGui::CalcTextSize(shown.c_str()).x > room) { while (shown.size() > 1 && ImGui::CalcTextSize(("..." + shown).c_str()).x > room) shown.erase(0, 1); shown = "..." + shown; }
		const ImVec2 p = ImGui::GetCursorScreenPos(), sz(fw, ImGui::GetFrameHeight());
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ui::u32(ui::bg), st.FrameRounding); dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), ui::u32(ui::border), st.FrameRounding);
		const ImVec2 tp(p.x + st.FramePadding.x, p.y + st.FramePadding.y);
		dl->AddText(tp, ui::u32(ui::text), shown.c_str());
		const float cx = tp.x + ImGui::CalcTextSize(shown.c_str()).x + 1;
		dl->AddLine(ImVec2(cx, tp.y), ImVec2(cx, tp.y + ImGui::GetTextLineHeight()), ui::u32(ui::accent), 2.0f);
		ImGui::Dummy(sz);
	}
	ImGui::Dummy(ImVec2(0, em * 0.2f));
	/* the keys: ten to a row, each a tenth of the width (by touch a finger tall: the frame's touch padding) */
	const float sp = ImGui::GetStyle().ItemSpacing.x * 0.5f, kw = (ImGui::GetContentRegionAvail().x - sp * 9) / 10;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp, sp));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));   /* (a key's label centred in it: a narrow window's keys have no room for side padding) */
	static const char *const rows[] = {"1234567890", "qwertyuiop", "asdfghjkl:", "zxcvbnm.-_"};
	int typed = -1; bool del = false, space = false, shift = false;
	for (int r = 0; r < 4; r++)
		for (int i = 0; i < 10; i++) {
			if (i) ImGui::SameLine();
			char k = rows[r][i]; if (g_osk.shift && isalpha((unsigned char)k)) k = (char)toupper((unsigned char)k);
			char lb[16]; snprintf(lb, sizeof lb, "%c##k%d%d", k, r, i);
			if (ImGui::Button(lb, ImVec2(kw, 0))) typed = (unsigned char)k;
			if (r == 0 && i == 0 && g_osk.appearing && GImGui->NavCursorVisible) ImGui::SetKeyboardFocusHere(-1);
		}
	if (ImGui::Button(g_osk.shift ? "SHIFT##osk" : "Shift##osk", ImVec2(kw * 2 + sp, 0))) shift = true;
	ImGui::SameLine(); if (ImGui::Button("/##osk", ImVec2(kw, 0))) typed = '/';
	ImGui::SameLine(); if (ImGui::Button("\\##osk", ImVec2(kw, 0))) typed = '\\';
	ImGui::SameLine(); if (ImGui::Button("Space##osk", ImVec2(kw * 4 + sp * 3, 0))) space = true;
	ImGui::SameLine(); if (ImGui::Button("Delete##osk", ImVec2(kw * 2 + sp, 0))) del = true;
	ImGui::PopStyleVar(2);
	ImGui::Dummy(ImVec2(0, em * 0.3f));
	bool done = ui::primary_button("Done##osk"), cancel = false;
	same_line_fits(button_w("Cancel")); if (ImGui::Button("Cancel##osk")) cancel = true;
	/* a controller's shortcuts, and a keyboard plugged in */
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) del = true;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) shift = true;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) space = true;
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) done = true;
	for (ImWchar c : GImGui->IO.InputQueueCharacters) if (c >= 32 && c < 127) g_osk.text += (char)c;
	if (dialog_back()) cancel = true;
	if (hints_visible()) {
		dialog_hints();
		const PadNames *n = pad_names_of(g_pad_fam);
		if (!hints_keyboard()) { char line[160]; snprintf(line, sizeof line, "%s Delete    %s Shift    %s Space    %s Done", n->north, n->l, n->r, n->menu); ImGui::TextDisabled("%s", line); }
	}
	if (typed >= 0) { g_osk.text += (char)typed; if (g_osk.shift && isalpha(typed)) g_osk.shift = false; }
	if (space) g_osk.text += ' ';
	if (del && !g_osk.text.empty()) g_osk.text.pop_back();
	if (shift) g_osk.shift = !g_osk.shift;
	g_osk.appearing = false;
	if (done) {
		if (g_osk.target) { *g_osk.target = g_osk.text; g_osk_done = g_osk.target; }
		if (g_shot_mode) g_shot_done.push_back("typed " + g_osk.title + "=" + g_osk.text);
	}
	if (done || cancel) { g_osk.open = false; ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}
static bool g_bind_open(void);
static bool g_osk_open_any(void) { return g_osk.open || g_browse.open || g_bind_open(); }
static void child_dialogs(void) { browser_modal(); osk_modal(); }   /* (the browser and the keyboard, inside the dialog that asked) */
static void tab_hint(void) {   /* (while a controller or the keyboard moves the focus: the buttons that step the tabs) */
	if (!hints_visible()) return;
	const PadNames *n = pad_names_of(g_pad_fam);
	const std::string s = hints_keyboard() ? std::string("Ctrl+Tab") : std::string(n->l) + " / " + n->r;
	same_line_fits(ImGui::CalcTextSize(s.c_str()).x); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", s.c_str());
}

/* ---- shared pieces of the setup and the dialogs */
static int g_add_kind = 0;   /* 0 a folder to search, 1 a PKG file, 2 a game folder */
static std::string g_add_path, g_add_license, g_fw_path, g_scan_path;

/* "Where's my licence?": a PKG carries none (PSN gives each owner their own, the PKG is the same for everyone); the
 * owner's Vita has it -- a game installed from a PKG keeps it in its folder, sce_sys/package/work.bin; a game bought
 * and installed the usual way gets one written by the NoNpDrm plugin when started, ux0:nonpdrm/license/app/.
 * Searching a folder that holds the licence file finds it by its content id. */
static void center_next_popup(void);
static float popup_wrap(float ems) {   /* a popup's text width: ems wide, or what a narrow window leaves (its padding off both sides) */
	const float room = ImGui::GetMainViewport()->WorkSize.x * 0.94f - ImGui::GetStyle().WindowPadding.x * 2;
	return std::min(ImGui::GetFontSize() * ems, room);
}
/* a line of text with more to say (a path, a version note). With a mouse the note is its tooltip; touch and a controller have
 * no hover, so a tap, a click or A on it (it takes the focus like a button) shows the note in a small popup. Underlined:
 * there's more. By touch its target is a finger tall without moving the lines around it */
static void info_text(const char *id, const ImVec4 &col, const std::string &text, const std::string &note) {
	ImGuiWindow *w = ImGui::GetCurrentWindow(); if (w->SkipItems) return;
	const ImGuiID iid = w->GetID(id);
	const float base = w->DC.CurrLineTextBaseOffset;   /* (beside a button or a checkbox: on their text's line, not above it) */
	const ImVec2 p(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + base), ts = ImGui::CalcTextSize(text.c_str());
	const float extra = g_touch ? std::max(0.0f, (touch_target() - ts.y) * 0.5f) : 0;
	const ImRect bb(ImVec2(p.x, p.y - extra), ImVec2(p.x + ts.x, p.y + ts.y + extra));
	ImGui::ItemSize(ts, base);
	if (!ImGui::ItemAdd(bb, iid)) return;
	bool hov = false, held = false;
	const bool pressed = ImGui::ButtonBehavior(bb, iid, &hov, &held);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddText(p, ImGui::GetColorU32(col), text.c_str());
	dl->AddLine(ImVec2(p.x, p.y + ts.y), ImVec2(p.x + ts.x, p.y + ts.y), ui::u32(col, hov ? 0.9f : 0.35f));
	ImGui::RenderNavCursor(bb, iid);
	if (hov && !g_touch && !ImGui::IsPopupOpen(id)) ImGui::SetTooltip("%s", note.c_str());
	if (pressed) ImGui::OpenPopup(id);
	if (ImGui::BeginPopup(id)) { ImGui::PushTextWrapPos(popup_wrap(28)); ImGui::TextUnformatted(note.c_str()); ImGui::PopTextWrapPos(); ImGui::EndPopup(); }
}
static void licence_help_popup(void) {
	center_next_popup();
	if (!ImGui::BeginPopup("licence_help")) return;
	const float em = ImGui::GetFontSize();
	ImGui::PushTextWrapPos(popup_wrap(30));
	ImGui::TextUnformatted("Where's my licence?");
	ui::muted_wrapped("A PKG never carries one: PSN keeps a licence for each owner, and the PKG is the same file for everyone. Your Vita has yours:");
	ImGui::Bullet(); ImGui::TextWrapped("A game installed from its PKG: its folder holds the licence, ux0:app/<title>/sce_sys/package/work.bin. Copy the whole game folder and nothing else is needed.");
	ImGui::Bullet(); ImGui::TextWrapped("A game bought and installed the usual way: with the NoNpDrm plugin on, start the game once. Its licence is then saved as a .rif file in ux0:nonpdrm/license/app/<title>/.");
	ImGui::Bullet(); ImGui::TextWrapped("Copy that file (or the whole ux0:nonpdrm folder) next to your PKG and search the folder: Nativ matches each licence to its game.");
	ui::muted_wrapped("Easiest: Licences, From my Vita… fetches them all at once over Wi-Fi. Nativ keeps your licences on this computer only.");
	ImGui::PopTextWrapPos();
	ImGui::EndPopup();
}
static void licence_help_link(void) { if (link_button("Where's my licence?")) ImGui::OpenPopup("licence_help"); licence_help_popup(); }

/* ---- the player's licences: how many are kept, and three ways in -- the Vita itself (VitaShell's FTP), a folder (a
 * copied ux0:nonpdrm, licence files kept anywhere), a Vita3K install's -- each once; a folder search keeps the ones it
 * finds too. Fetched or imported, every later PKG, update and add-on of those games is added without asking. */
static int g_licences;   /* kept, counted at each library scan */
static std::string g_vita_addr, g_lic_folder;
static bool g_open_vita_popup;   /* (ATIV_SHOT_UI=add:vita: the popup open in a screenshot) */
static std::string vita3k_licences(void) {   /* a kept Vita3K folder's ux0/license, else Vita3K's where it keeps it by default (SDL's pref path Vita3K/Vita3K), "" none */
	for (const LibFolder &f : g_lib_folders) if (f.kind == "vita3k" && exists(f.root + "/license")) return f.root + "/license";
	const char *home = getenv("HOME"), *appdata = getenv("APPDATA"), *xdg = getenv("XDG_DATA_HOME");
	std::vector<std::string> bases;
	if (home) { bases.push_back(std::string(home) + "/Library/Application Support/Vita3K/Vita3K"); bases.push_back((xdg ? std::string(xdg) : std::string(home) + "/.local/share") + "/Vita3K/Vita3K"); }
	if (appdata) bases.push_back(std::string(appdata) + "/Vita3K/Vita3K");
	for (const std::string &b : bases) for (const char *sub : {"/ux0/license", "/fs/ux0/license"}) if (exists(b + sub)) return b + sub;
	return "";
}
static void count_licences(void) {
	g_licences = 0;
	if (DIR *d = opendir((g_dump + "/licenses").c_str())) { while (dirent *e = readdir(d)) { size_t n = strlen(e->d_name); g_licences += n > 4 && !strcmp(e->d_name + n - 4, ".rif"); } closedir(d); }
}
static void center_next_popup(void) { const ImGuiViewport *vp = ImGui::GetMainViewport(); ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); }
static void vita_licences_popup(void) {
	center_next_popup();
	if (!ImGui::BeginPopup("vita_licences")) return;
	const float em = ImGui::GetFontSize();
	ImGui::PushTextWrapPos(popup_wrap(28));
	ImGui::TextUnformatted("Get your licences from your Vita");
	ui::muted_wrapped("Your Vita and this computer on the same network. Nativ reads only licence files, nothing else.");
	ImGui::TextWrapped("1. On the Vita, open VitaShell and press SELECT. It shows an address like ftp://192.168.0.50:1337.");
	ImGui::TextWrapped("2. Type that address here:");
	if (g_vita_addr.empty()) g_vita_addr = g_prefs["vita_address"];
	input_text("##vaddr", g_vita_addr, std::min(em * 16, popup_wrap(28)), "192.168.0.50:1337");
	same_line_fits(button_w("Get licences"));
	ImGui::BeginDisabled(g_vita_addr.empty() || g_task.running);
	if (ui::primary_button("Get licences")) {
		std::string a = g_vita_addr; if (a.rfind("ftp://", 0) == 0) a = a.substr(6); while (!a.empty() && a.back() == '/') a.pop_back();
		prefs_set("vita_address", a);
		task_start("Getting licences from the Vita", "licences", "", {step("Reading the Vita", vitarecomp({"licenses", "vita", a}))});
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ui::muted_wrapped("A game bought and installed the usual way has a licence only the Vita can use: start it once with the NoNpDrm plugin on, then get licences again.");
	ImGui::PopTextWrapPos();
	ImGui::EndPopup();
}
static void licences_row(SDL_Window *win) {
	ImGui::PushID("licences");
	ImGui::AlignTextToFramePadding();   /* (the line's text level with its buttons' labels: a finger-tall button sat lower than it) */
	{	const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = ImGui::GetTextLineHeight(), r = h * 0.22f, off = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + off + h * 0.5f), r, ui::u32(g_licences ? ui::ready : ui::border));
		ImGui::Dummy(ImVec2(r * 2, h)); ImGui::SameLine(0, h * 0.35f);
	}
	ImGui::Text("Licences: %d kept", g_licences); same_line_fits(button_w("From my Vita…"));
	ImGui::BeginDisabled(g_task.running);
	if (small_button("From my Vita…") || g_open_vita_popup) { ImGui::OpenPopup("vita_licences"); g_open_vita_popup = false; }
	same_line_fits(button_w("From a folder…")); if (small_button("From a folder…")) { g_lic_folder.clear(); pick_folder(win, &g_lic_folder); }
	std::string v3k = vita3k_licences();
	if (!v3k.empty()) { same_line_fits(button_w("From Vita3K")); if (small_button("From Vita3K")) task_start("Importing Vita3K's licences", "licences", "", {step("", vitarecomp({"licenses", "vita3k", v3k}))}); }
	ImGui::EndDisabled();
	if (!g_lic_folder.empty() && !g_task.running) {   /* (a folder picked: its licences in) */
		task_start("Importing licences", "licences", "", {step("", vitarecomp({"licenses", "import", g_lic_folder}))});
		g_lic_folder.clear();
	}
	vita_licences_popup();
	ImGui::PopID();
}

/* a folder search's finds (vitarecomp scan: pipeline/packs.py's rows) -- games, updates, add-ons, firmware files, each
 * with what its game pack says of that version -- picked for adding in one go */
struct Found { std::string kind, form, id, version, status, licence, name, path, licence_file, content_id, note, source; bool pick = false; };
/* the player's licences, kept once (pipeline/licenses.py: <dump>/licenses/<content id>.rif): add finds a copy's
 * there by itself, so a row whose licence is kept -- by this search, or fetched from the Vita meanwhile -- is ready */
static std::string kept_licence(const std::string &cid) { std::string p = g_dump + "/licenses/" + cid + ".rif"; return !cid.empty() && cid != "-" && exists(p) ? p : ""; }
static std::vector<Found> g_found;
static bool g_found_ready;   /* a search finished: its rows (maybe none) are in g_found */
static std::string scan_file(void) { std::string d = pref_dir(); return d.empty() ? g_dump + "/.scan.tsv" : d + "scan.tsv"; }
static bool playable_status(const std::string &st) { return st == "recommended" || st == "supported" || st == "check"; }
static void scan_parse(const std::string &path, std::vector<Found> &out) {   /* (rows of a search, or of the library folders' search) */
	out.clear();
	FILE *f = fopen(path.c_str(), "r"); if (!f) return;
	char line[4096];
	while (fgets(line, sizeof line, f)) {
		std::vector<std::string> c(1);
		for (const char *q = line; *q && *q != '\n' && *q != '\r'; q++) { if (*q == '\t') c.emplace_back(); else c.back() += *q; }
		if (c.size() < 8) continue;
		Found x; x.kind = c[0]; x.form = c[1]; x.id = c[2]; x.version = c[3]; x.status = c[4]; x.licence = c[5]; x.name = c[6]; x.path = c[7];
		if (c.size() > 8) x.licence_file = c[8];   /* (licence "file": a kept one, or one found elsewhere in the folder) */
		if (c.size() > 9) x.content_id = c[9];
		if (c.size() > 10) x.note = c[10];   /* (a copy Nativ can't play as it is: which release and version it does) */
		if (c.size() > 11) x.source = c[11];   /* (vitarecomp folders scan: the library folder it was found in) */
		/* picked to start with: what plays and needs nothing typed in; the firmware when none is installed (the newest file) */
		if (x.kind == "game" || x.kind == "update") {
			bool dup = false; for (const Found &y : out) dup |= y.pick && y.kind == x.kind && y.id == x.id;
			x.pick = !dup && playable_status(x.status);   /* (one still needing its licence is ticked, its field showing; it's added once it has one) */
		} else if (x.kind == "firmware" && x.status == "firmware" && !g_firmware) {
			for (Found &y : out) if (y.kind == "firmware") y.pick = false;   /* (rows come sorted by version) */
			x.pick = true;
		}
		out.push_back(x);
	}
	fclose(f);
}
static void scan_load(const std::string &path) { scan_parse(path, g_found); g_found_ready = true; }
static bool found_ready(const Found &x, const std::vector<Found> &rows = g_found) {   /* it has what adding it needs */
	if (x.kind == "firmware") return true;
	if (!kept_licence(x.content_id).empty()) return true;
	if (x.licence == "rif" || (x.kind == "game" && x.licence == "no")) return !x.licence_file.empty();
	if (x.kind == "update" && x.licence == "no") {   /* it decrypts with its game's licence: the game is being added with one */
		for (const Found &y : rows) if (y.pick && y.kind == "game" && y.id == x.id && found_ready(y, rows)) return true;
		return false;
	}
	return true;
}
/* the firmware first, then per game: the game and its update kept, imported, built once. rows: a search's (g_found) or the
 * library folders' (only: that one title, a card's Add); a copy from a library folder on another drive is read in place
 * when the player chose so (hub.ini library_in_place) */
static std::vector<Step> found_steps(int *n, std::vector<std::string> *titles, const std::vector<Found> &rows = g_found, const std::string &only = "") {
	std::vector<Step> steps;   /* (each its own group: one failing doesn't stop the rest) */
	int group = 0;
	*n = 0;
	for (const Found &x : rows)
		if (only.empty() && x.pick && x.kind == "firmware") { steps.push_back(step("Installing the firmware " + x.version, vitarecomp({"firmware", x.path}), ++group)); ++*n; break; }
	std::vector<std::string> ids;
	for (const Found &x : rows) if (x.pick && (x.kind == "game" || x.kind == "update") && (only.empty() || x.id == only) && std::find(ids.begin(), ids.end(), x.id) == ids.end()) ids.push_back(x.id);
	if (titles) *titles = ids;
	for (const std::string &id : ids) {
		std::vector<const Found *> items;
		for (const char *k : {"game", "update"})
			for (const Found &x : rows) if (x.pick && x.id == id && x.kind == k && found_ready(x, rows)) { items.push_back(&x); break; }
		if (items.empty()) continue;
		bool pack = false;
		for (const Found &x : rows) pack |= x.id == id && x.status != "nopack";
		++group;
		for (const Found *xp : items) {
			const Found &x = *xp;
			std::vector<std::string> argv = vitarecomp({"add", x.path});
			if (!x.licence_file.empty()) argv.insert(argv.end(), {"--license", x.licence_file});
			argv.push_back("--no-build");
			if (!x.source.empty() && g_prefs["library_in_place"] == "1") argv.push_back("--in-place");
			steps.push_back(step(x.kind == "update" ? "Adding its update " + x.version : "Adding " + x.name + " " + x.version, argv, group));
			++*n;
		}
		steps.push_back(step("Checking its version", vitarecomp({"import", id}), group));   /* (the game with its update layered over it) */
		if (pack) build_steps(steps, id, group);
	}
	return steps;
}
static void found_status(const Found &x) {   /* what its game pack says of it, as a dot and a line */
	struct S { const char *key, *text; int tone; };   /* tone: 0 good, 1 a caution, 2 neutral */
	static const S st[] = {
		{"recommended", "Recommended version", 0}, {"supported", "Supported version", 0}, {"check", "Checked against its game pack when added", 2},
		{"unsupported", "This version isn't supported yet", 1}, {"nopack", "No game pack yet: experimental", 1},
		{"otherregion", "This region's release isn't supported yet", 1},
		{"added", "Already in your library", 2}, {"licensed", "Nothing to add: the game gets its add-ons", 2},
		{"installed", "Already installed", 2}, {"firmware", "Not installed yet", 0}};
	for (const S &e : st) if (x.status == e.key) {
		ui::dot(e.tone == 0 ? ui::ready : e.tone == 1 ? ui::warn : ui::border);
		ImGui::TextDisabled("%s", e.text);
		if (!x.note.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::PushStyleColor(ImGuiCol_Text, ui::muted); ImGui::TextWrapped("%s.", x.note.c_str()); ImGui::PopStyleColor(); ImGui::PopTextWrapPos(); }
		return;
	}
	ui::dot(ui::border); ImGui::TextDisabled("%s", x.status.c_str());
}
static void found_list(void) {
	const float em = ImGui::GetFontSize();
	if (g_found.empty()) { ui::muted_wrapped("Nothing found there: no game or update folders (with sce_sys/param.sfo), PKGs or firmware update files (.PUP)."); return; }
	float row_h = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
	ImGui::BeginChild("found", ImVec2(0, std::min(em * 17.0f, row_h * g_found.size() + em)), ImGuiChildFlags_Borders);
	for (size_t i = 0; i < g_found.size(); i++) {
		Found &x = g_found[i];
		ImGui::PushID((int)i);
		if (i) ImGui::Separator();
		bool can = x.kind == "game" || x.kind == "update" || (x.kind == "firmware" && x.status == "firmware");
		ImGui::BeginDisabled(!can || g_task.running);
		if (ImGui::Checkbox("##pick", &x.pick) && x.pick)   /* one copy of a game (or of its update) at a time */
			for (Found &y : g_found) if (&y != &x && y.kind == x.kind && y.id == x.id && x.kind != "firmware") y.pick = false;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginGroup();
		const char *kind = x.kind == "game" ? "Game" : x.kind == "update" ? "Update" : x.kind == "addon" ? "Add-on" : "Firmware file";
		ImGui::TextUnformatted(x.name.c_str()); ImGui::SameLine();
		ImGui::TextDisabled("%s%s%s%s", kind, x.version.empty() ? "" : " ", x.version.c_str(), x.form == "pkg" ? " (PKG)" : "");
		found_status(x);
		if (x.pick && (x.licence == "rif" || (x.kind == "game" && x.licence == "no")) && kept_licence(x.content_id).empty()) {
			ui::dot(ui::warn); ImGui::TextDisabled("Needs its licence file (.rif or work.bin).");
			licence_help_link();
			if (path_row("##licfile", x.licence_file, "Its licence file (.rif or work.bin)")) pick_licence(nullptr, &x.licence_file);
		} else if (x.pick && x.kind == "update" && x.licence == "no" && !found_ready(x)) {
			ui::dot(ui::warn); ImGui::TextDisabled("Add its game too: an update decrypts with its game's licence");
		}
		ImGui::PushFont(nullptr, em * 0.85f); ImGui::PushStyleColor(ImGuiCol_Text, ui::muted);
		ImGui::TextUnformatted(x.path.c_str());
		if (!kept_licence(x.content_id).empty() && (x.kind == "game" || x.kind == "update")) ImGui::TextUnformatted("Licence: kept");
		else if (x.licence == "file") ImGui::Text("Licence: %s", x.licence_file.c_str());
		ImGui::PopStyleColor(); ImGui::PopFont();
		ImGui::EndGroup();
		ImGui::PopID();
	}
	ImGui::EndChild();
}
/* ---- the library folders: searched on start (hub.ini library_rescan, on unless "0") and on demand; what they hold shows
 * on the cards ("Found in <folder>", Add). Nothing in a folder is changed (pipeline/folders.py) */
static std::vector<Found> g_lib_found;   /* the last search of the library folders */
static std::string lib_folders_file(void) { std::string d = pref_dir(); return d.empty() ? "" : d + "library_folders.tsv"; }
static std::string lib_scan_file(void) { std::string d = pref_dir(); return d.empty() ? g_dump + "/.library_scan.tsv" : d + "library_scan.tsv"; }
static void lib_load(void) {
	g_lib_folders.clear();
	if (FILE *f = fopen(lib_folders_file().c_str(), "r")) {
		char line[4096];
		while (fgets(line, sizeof line, f)) {
			std::vector<std::string> c(1);
			for (const char *q = line; *q && *q != '\n' && *q != '\r'; q++) { if (*q == '\t') c.emplace_back(); else c.back() += *q; }
			if (c.size() >= 2 && (c[0] == "folder" || c[0] == "vita3k") && !c[1].empty()) g_lib_folders.push_back({c[0], c[1], c.size() > 2 && !c[2].empty() ? c[2] : c[1]});
		}
		fclose(f);
	}
	scan_parse(lib_scan_file(), g_lib_found);
	g_lib_found.erase(std::remove_if(g_lib_found.begin(), g_lib_found.end(), [](const Found &x) {   /* (a folder forgotten since: its finds go) */
		for (const LibFolder &f : g_lib_folders) if (f.path == x.source) return false;
		return true; }), g_lib_found.end());
}
static std::vector<Step> lib_scan_steps(void) { return {step("Looking in your library folders", vitarecomp({"folders", "scan", "--out", lib_scan_file()}))}; }
static void lib_add_folder(const std::string &path, bool vita3k) {
	std::vector<std::string> a = vitarecomp({"folders", "add"}); if (vita3k) a.push_back("--vita3k"); a.push_back(path);
	std::vector<Step> s = {step("Keeping the folder", a)}; for (Step &x : lib_scan_steps()) s.push_back(x);
	task_start(vita3k ? "Adding a Vita3K folder" : "Adding a library folder", "libscan", "", s);
}
static void lib_remove_folder(const std::string &path) {   /* (only the list: the folder and what was added from it stay) */
	std::string out;
	for (const LibFolder &f : g_lib_folders) if (f.path != path) out += f.kind + "\t" + f.path + "\t" + f.root + "\n";
	if (FILE *f = fopen(lib_folders_file().c_str(), "wb")) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
	lib_load();
}
static std::string leaf_name(const std::string &p) {   /* "D:/Games/Vita3K" -> "Vita3K" */
	std::string s = p; while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
	size_t at = s.find_last_of("/\\"); return at == std::string::npos ? s : s.substr(at + 1);
}
static const Found *lib_find(const Game &g) {   /* a playable copy of this card's game in a library folder */
	for (const Found &x : g_lib_found)
		if (x.kind == "game" && x.pick && playable_status(x.status)) {
			auto o = g_other_of.find(x.id);
			if (x.id == g.id || (o != g_other_of.end() && o->second == g.id)) return &x;
		}
	return nullptr;
}
static std::string vita3k_default_dir(void) {   /* Vita3K's data folder at its usual place (SDL's pref path Vita3K/Vita3K), "" none */
	const char *home = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME"), *appdata = getenv("APPDATA");
	std::vector<std::string> bases;
	if (home) { bases.push_back(std::string(home) + "/Library/Application Support/Vita3K/Vita3K"); bases.push_back((xdg ? std::string(xdg) : std::string(home) + "/.local/share") + "/Vita3K/Vita3K"); }
	if (appdata) bases.push_back(std::string(appdata) + "/Vita3K/Vita3K");
	for (const std::string &b : bases) for (const char *sub : {"/ux0", "/fs/ux0"}) if (exists(b + sub)) return b;
	return "";
}
static bool g_search_keep = true;   /* the search form's "Keep this folder": searched again on each start */
static bool search_form(SDL_Window *win) {   /* a folder searched for games, updates and the firmware file; true when adding started */
	ui::muted_wrapped("A folder to look through, every subfolder too: a copy of your Vita's ux0: (its app, patch and addcont folders) or a downloads folder. Games, their updates and the firmware file are found and checked against the game packs, which play the games' US releases at the versions listed.");
	ImGui::Spacing();
	if (path_row("##scanpath", g_scan_path, "Choose a folder to search")) pick_folder(win, &g_scan_path);
	const bool searching = g_task.running && g_task.kind == "scan";
	ImGui::BeginDisabled(g_scan_path.empty() || g_task.running);
	bool go = g_found_ready ? ImGui::Button("Search again") : ui::primary_button("Search");
	ImGui::EndDisabled();
	same_line_fits(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Keep this folder").x); ImGui::Checkbox("Keep this folder", &g_search_keep);
	same_line_fits(ImGui::CalcTextSize("What's kept?").x); info_text("##keepinfo", ui::muted, "What's kept?", "Kept as a library folder (Settings, Library): Nativ looks in it again each time it starts,\nand shows what it finds there on the game's card. Nothing in the folder is changed.");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kept as a library folder (Settings, Library): Nativ looks in it again each time it starts,\nand shows what it finds there on the game's card. Nothing in the folder is changed.");
	if (go) {
		g_found.clear(); g_found_ready = false;
		std::vector<Step> s = {step("Searching", vitarecomp({"scan", g_scan_path, "--import-licences", "--out", scan_file()}))};
		if (g_search_keep) s.push_back(step("Keeping the folder", vitarecomp({"folders", "add", g_scan_path})));
		task_start("Searching a folder", "scan", "", s);
	}
	if (searching) { ImGui::SameLine(); ui::spinner(ui::accent); ImGui::TextDisabled("Searching…"); }
	if (!g_found_ready) return false;
	ImGui::Spacing();
	found_list();
	int n = 0; std::vector<std::string> titles; std::vector<Step> steps = found_steps(&n, &titles);
	ImGui::Spacing();
	ImGui::BeginDisabled(n == 0 || g_task.running);
	std::string label = n ? "Add " + std::to_string(n) + (n == 1 ? " item" : " items") : "Add";
	bool started = ui::primary_button(label.c_str());
	ImGui::EndDisabled();
	int waiting = 0; for (const Found &x : g_found) waiting += x.pick && (x.kind == "game" || x.kind == "update") && !found_ready(x);
	if (waiting) { ImGui::SameLine(); ImGui::TextDisabled("%d waiting for a licence", waiting); }
	if (started) {
		task_start(n == 1 ? "Adding from the folder" : "Adding " + std::to_string(n) + " items", "add", "", steps);
		g_task.games = titles;   /* (their cards show the chain's step) */
		g_found.clear(); g_found_ready = false;
	}
	return started;
}
static bool add_form(SDL_Window *win) {   /* a player's own copy -> decrypted, imported, analysed, recompiled, built; true when started */
	static const char *const kinds[] = {"Search a folder", "A PKG file", "A game folder"};
	ui::segmented(kinds, 3, &g_add_kind); tab_hint();
	g_add_kind = (g_add_kind + tab_step() + 3) % 3;
	ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.15f));
	if (g_add_kind == 0) return search_form(win);
	const bool pkg_kind = g_add_kind == 1;
	ui::muted_wrapped(pkg_kind ? "The .pkg of a game you own, with its licence file: the .rif from your Vita. Or its update's PKG, once the game is added (it uses the game's licence)."
	                           : "A game folder copied from your Vita (ux0:app/<title>: it carries its licence, sce_sys/package/work.bin), or one already decrypted; or its update (ux0:patch/<title>), once the game is added.");
	licence_help_link();
	ImGui::Spacing();
	if (path_row("##addpath", g_add_path, pkg_kind ? "Choose the .pkg file" : "Choose the game folder")) {
		if (pkg_kind) pick_pkg(win, &g_add_path); else pick_folder(win, &g_add_path);
	}
	if (path_row("##lic", g_add_license, pkg_kind ? "Its licence file (.rif or work.bin)" : "Its licence file, if the folder has none (.rif or work.bin)")) pick_licence(win, &g_add_license);
	ImGui::Spacing();
	bool started = false;
	ImGui::BeginDisabled(g_add_path.empty() || g_task.running);
	if (ui::primary_button("Add this game")) {
		std::vector<std::string> argv = vitarecomp({"add", g_add_path});
		if (!g_add_license.empty()) argv.insert(argv.end(), {"--license", g_add_license});
		task_start("Adding a game", "add", "", {step("Decrypting and importing your copy", argv), step("Added")});
		g_add_path.clear(); g_add_license.clear();
		started = true;
	}
	ImGui::EndDisabled();
	return started;
}
static void task_line(void) {   /* the running task, inline: spinner, its step, Cancel; or the last failure */
	if (g_task.running) {
		ui::spinner(ui::accent);
		ImGui::TextUnformatted(g_task.name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("%s", task_status().c_str());
	} else if (!g_failed.empty()) {
		ui::dot(ui::error); ImGui::TextColored(ui::error, "%s didn’t finish.", g_failed.c_str()); ImGui::SameLine();
		if (link_button("Show the log")) g_drawer = true;
	}
}

/* ---- settings (runtime/core/settings.h): the shared file every game reads -- display, controls, sound, online ID -- beside this
 * launcher's own files, and each game's own (its options, and where it keeps a value of its own). A game reads its own
 * file first, then the shared one; a change in any game's menu changes the shared file. The launcher's Settings page
 * edits the shared file, a card's Game settings its game's. Each change is read-modify-written at once (a running game
 * merges its own changes the same way). */
struct Ini { std::vector<std::pair<std::string, std::string>> kv; };
static Ini ini_load(const std::string &path) {
	Ini x; FILE *f = fopen(path.c_str(), "r"); if (!f) return x;
	char line[512];
	while (fgets(line, sizeof line, f)) {
		std::string l = line; size_t eq = l.find('=');
		size_t b = l.find_first_not_of(" \t"); if (b == std::string::npos || l[b] == '#' || l[b] == ';' || eq == std::string::npos) continue;
		auto trim = [](std::string v) { size_t a = v.find_first_not_of(" \t\r\n"), e = v.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : v.substr(a, e - a + 1); };
		x.kv.push_back({trim(l.substr(0, eq)), trim(l.substr(eq + 1))});
	}
	fclose(f);
	return x;
}
static const std::string *ini_find(const Ini &x, const std::string &k) { for (auto &e : x.kv) if (e.first == k) return &e.second; return nullptr; }
static void ini_set(Ini &x, const std::string &k, const std::string &v) { for (auto &e : x.kv) if (e.first == k) { e.second = v; return; } x.kv.push_back({k, v}); }
static void ini_erase(Ini &x, const std::string &k) { x.kv.erase(std::remove_if(x.kv.begin(), x.kv.end(), [&](const std::pair<std::string, std::string> &e) { return e.first == k; }), x.kv.end()); }
static void ini_save(const Ini &x, const std::string &path, const char *header) {
	std::string tmp = path + ".tmp"; FILE *f = fopen(tmp.c_str(), "w"); if (!f) return;
	fprintf(f, "%s\n", header);
	for (auto &e : x.kv) fprintf(f, "%s = %s\n", e.first.c_str(), e.second.c_str());
	if (fclose(f) == 0) {
#ifdef _WIN32
		MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);   /* (rename refuses an existing file on Windows) */
#else
		rename(tmp.c_str(), path.c_str());
#endif
	}
}
static const char *const SHARED_HEADER = "# Project Nativ's settings for every game (the launcher's Settings, and each game's menu; runtime/core/settings.h)";
static const char *const GAME_HEADER = "# vitarecomp settings for this game: its own options, and where it differs from the shared settings (runtime/core/settings.h)";
static bool game_key(const std::string &k) {   /* (settings.c's game_key: kept in the game's own file) */
	for (const char *g : {"fov", "view_distance", "scenery_detail", "character_detail", "private_server", "net_redirect", "menu_hint_shown", "controller_layout"}) if (k == g) return true;
	return false;
}
/* the shared keys' defaults where a game reads no value (runtime/core/settings.h; the consumers' own) --
 * one table now in runtime/ui/setting_defaults.h (UI QUEUE U7), shared with the in-game menu's "default" marks */
#include "setting_defaults.h"
#include "frame_rates.h"   /* the frame-rate rules: a game's allowed rates (its pack's RECOMP_FRAME_RATES) */
/* keys only some games read (an online one, a camera one, one with ambient occlusion): a game without the key never chose */
static bool some_games_key(const std::string &k) { return k == "online_id" || k == "online" || k == "np_account_id" || k == "np_age" || k == "camera" || k == "ssao" || k == "ssao_strength"; }
static std::string shared_path(void) { std::string d = pref_dir(); return d.empty() ? "" : d + "settings.ini"; }
static std::string game_settings_path(const std::string &id) { return g_dump + "/decrypted/" + id + "/recomp/settings.ini"; }
static std::string fmt_g(double v) { char b[32]; snprintf(b, sizeof b, "%g", v); return b; }

/* the starting point on a new machine: the look most players want from a PC version -- fullscreen, sharp for this display,
 * the full width of the screen, 60 fps, the mouse aiming 1:1, softer shadows (the games' own menus, and Settings, change it) */
static int recommended_scale(void) {
	const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
	float h = m ? m->h * (m->pixel_density > 0 ? m->pixel_density : 1.0f) : 1080;
	int k = (int)lroundf(h / 544.0f); return k < 2 ? 2 : k > 4 ? 4 : k;
}
static void recommended_into(Ini &x) {
	ini_set(x, "fullscreen", "1"); ini_set(x, "render_scale", std::to_string(recommended_scale())); ini_set(x, "aspect_ratio", "window");
	ini_set(x, "frame_rate", "60"); ini_set(x, "raw_look", "1"); ini_set(x, "shadow_filter", "2");
}
/* once, when there is no shared file yet: it starts as what this library's games agree on (each key: the value most of
 * their files hold; a tie, the most recently changed file's), or the recommended set on a new install. Then, every
 * start, a game's file drops a shared key holding the same value as the shared file -- no change in what it plays with,
 * and what's left in it is what differs (its Game settings list those) */
static void settings_migrate(void) {
	std::string sp = shared_path(); if (sp.empty()) return;
	struct F { std::string path; Ini x; time_t mtime; };
	std::vector<F> files;
	if (DIR *d = opendir((g_dump + "/decrypted").c_str())) {
		while (dirent *e = readdir(d)) {
			if (e->d_name[0] == '.') continue;
			std::string p = g_dump + "/decrypted/" + e->d_name + "/recomp/settings.ini"; struct stat st;
			if (stat(p.c_str(), &st) == 0) files.push_back({p, ini_load(p), st.st_mtime});
		}
		closedir(d);
	}
	/* (a shared file holding only np_* keys is a game's account record -- a game ran before the launcher ever did -- not
	 * anyone's choices: it starts like a new one, those keys kept) */
	Ini before = exists(sp) ? ini_load(sp) : Ini();
	bool chosen = false; for (auto &e : before.kv) chosen |= e.first.rfind("np_", 0) != 0;
	if (!chosen) {
		/* a game whose file lacks a key plays with its default: a vote for leaving the key out (but for keys only some
		 * games read) -- so one game's choice stays that game's, and what most games chose becomes everyone's */
		Ini sh; std::map<std::string, std::map<std::string, std::pair<int, time_t>>> votes;
		const std::string ABSENT = std::string("\x01") + "absent";
		for (const F &f : files) for (auto &e : f.x.kv) if (!game_key(e.first) && e.first.rfind("bind_", 0) != 0) {
			auto &v = votes[e.first][e.second]; v.first++; if (f.mtime > v.second) v.second = f.mtime;
		}
		for (auto &k : votes) if (!some_games_key(k.first))
			for (const F &f : files) if (!ini_find(f.x, k.first)) { auto &v = k.second[ABSENT]; v.first++; if (f.mtime > v.second) v.second = f.mtime; }
		for (auto &k : votes) {
			const std::string *best = nullptr; std::pair<int, time_t> bv{0, 0};
			for (auto &v : k.second) if (v.second > bv) { bv = v.second; best = &v.first; }
			if (best && *best != ABSENT) ini_set(sh, k.first, *best);
		}
		bool voted = false; for (auto &k : votes) voted |= k.first.rfind("np_", 0) != 0;   /* (account keys alone: no one's choices) */
		if (!voted) recommended_into(sh);
		for (auto &e : before.kv) ini_set(sh, e.first, e.second);
		ini_save(sh, sp, SHARED_HEADER);
		fprintf(stderr, "settings: the shared file started %s (%s)\n", !voted ? "with the recommended set" : "from this library's games", sp.c_str());
	}
	Ini sh = ini_load(sp);
	for (F &f : files) {
		size_t before = f.x.kv.size();
		f.x.kv.erase(std::remove_if(f.x.kv.begin(), f.x.kv.end(), [&](const std::pair<std::string, std::string> &e) {
			if (game_key(e.first)) return false;
			const std::string *v = ini_find(sh, e.first); const char *d = setting_default(e.first);   /* (the value it would read without it) */
			return v ? *v == e.second : d && atof(d) == atof(e.second.c_str()) && (isdigit((unsigned char)d[0]) || e.second == d); }), f.x.kv.end());
		if (f.x.kv.size() != before) ini_save(f.x, f.path, GAME_HEADER);
	}
}

/* the Settings page's controls: each a settings row (ui_theme.h) over one key of an Ini, written at once */
struct SetTarget { Ini *x; std::string path; const char *header; };
static void target_write(const SetTarget &t, const std::string &k, const std::string &v) {   /* read-modify-write: a running game's changes stay */
	ini_set(*t.x, k, v);
	Ini disk = ini_load(t.path); ini_set(disk, k, v); ini_save(disk, t.path, t.header);
}
static std::string tget(const SetTarget &t, const char *k, const char *def) { const std::string *v = ini_find(*t.x, k); return v ? *v : def; }
static void seg_row(const SetTarget &t, const char *label, const char *expl, const char *key, const char *def, std::initializer_list<std::pair<const char *, const char *>> opts) {
	std::string cur = tget(t, key, def);
	ui::row(label, expl, cur == def ? "default" : nullptr);   /* (the in-game menu's "default" mark: U7) */
	ImGui::PushID(key);
	std::vector<const char *> names; int v = 0, i = 0;
	for (auto &o : opts) { if (cur == o.first) v = i; names.push_back(o.second); i++; }
	if (ui::segmented(names.data(), (int)names.size(), &v)) target_write(t, key, opts.begin()[v].first);
	ImGui::PopID();
}
static void toggle_row(const SetTarget &t, const char *label, const char *expl, const char *key, bool def) {
	bool on = atoi(tget(t, key, def ? "1" : "0").c_str()) != 0;
	ui::row(label, expl, on == def ? "default" : nullptr);
	if (ui::toggle(key, &on)) target_write(t, key, on ? "1" : "0");
}
static void slider_row(const SetTarget &t, const char *label, const char *expl, const char *key, double def, float lo, float hi, const char *fmt, float shown = 1, ImGuiSliderFlags fl = 0) {
	float v = (float)atof(tget(t, key, fmt_g(def).c_str()).c_str()) * shown;
	ui::row(label, expl, fabsf(v - (float)def * shown) < 0.0005f * (shown > 1 ? shown : 1) ? "default" : nullptr);
	ImGui::SetNextItemWidth(-FLT_MIN);
	std::string id = std::string("##") + key;
	if (ImGui::SliderFloat(id.c_str(), &v, lo, hi, fmt, fl)) ini_set(*t.x, key, fmt_g(v / shown));
	if (ImGui::IsItemDeactivatedAfterEdit()) target_write(t, key, tget(t, key, ""));   /* (written once the drag ends) */
}
static bool g_settings_open; static int g_settings_tab; static Ini g_shared;
static bool g_setup_online = true;   /* setup's Play online, written with the name */
static bool g_settings_writable = true;   /* (false in a screenshot run without ATIV_PREFS: it touches no one's settings) */

/* the online name (runtime/net/np.c np_online_id makes the same): others see "ntv_" and the player's name, up to 10
 * letters, digits, '-' or '_' -- or, with none chosen, "ntv_" and a tag of six from this install's account ID (settings
 * np_account_id: random, made once, here when no game has made it yet) */
/* online_status.txt beside the shared settings (runtime/net/online/status.c): a game playing online rewrites it every 5 s.
 * "" when there is none */
static std::string online_status_line(void) {
	std::string d = pref_dir(); if (d.empty()) return "";
	Ini st = ini_load(d + "online_status.txt");
	const std::string *tm = ini_find(st, "time"); if (!tm) return "";
	const long long ago = (long long)time(nullptr) - atoll(tm->c_str());
	auto num = [&](const char *k) { const std::string *v = ini_find(st, k); return v ? atoi(v->c_str()) : 0; };
	const std::string *g = ini_find(st, "game"); const std::string game = g && !g->empty() ? *g : "a game";
	const int linked = num("linked"), rooms = num("rooms_heard") + num("rooms_mine");
	char b[256];
	if (ago >= 0 && ago < 15) snprintf(b, sizeof b, "Online now in %s: %s, %d %s open.", game.c_str(), linked ? (std::to_string(linked) + (linked == 1 ? " other copy linked" : " other copies linked")).c_str() : "looking for other players", rooms, rooms == 1 ? "room" : "rooms");
	else {
		const long long m = ago / 60;
		const std::string when = m < 1 ? "a moment ago" : m < 60 ? std::to_string(m) + " min ago" : m < 48 * 60 ? std::to_string(m / 60) + " h ago" : std::to_string(m / 1440) + " days ago";
		snprintf(b, sizeof b, "Last online %s, in %s%s.", when.c_str(), game.c_str(), linked ? (", linked with " + std::to_string(linked) + (linked == 1 ? " other copy" : " other copies")).c_str() : "");
	}
	return b;
}
static const char *ONLINE_EXPL = "Games' online modes (Killzone: Mercenary, Unit 13) with other Project Nativ players, on your network and over the internet. They see your online name and your computer's IP address, as in any peer-to-peer game.";
static bool online_name_ok(const std::string &n) {
	return n.size() <= 10 && std::all_of(n.begin(), n.end(), [](char c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; });
}
static std::string online_name(const Ini &shared) {   /* the chosen name, without a tag a user may have typed: "ntv_", or the ones before, "nativ_" and Project Ativ's "ativ_" */
	const std::string *v = ini_find(shared, "online_id"); std::string n = v ? *v : "";
	for (const char *tag : {"ntv_", "nativ_", "ativ_"}) if (!strncasecmp(n.c_str(), tag, strlen(tag))) return n.substr(strlen(tag));
	return n;
}
static bool game_option(const Game &g, const char *name);
static bool needs_online_name(const Game &g) {   /* asked once: an online game, online on, and the player hasn't named themselves */
	if (g_prefs["online_name_asked"] == "1" || !game_option(g, "online")) return false;
	const Ini sh = ini_load(shared_path());
	const std::string *on = ini_find(sh, "online");
	return !(on && atoi(on->c_str()) == 0) && online_name(sh).empty();
}
static std::string online_tag(Ini &shared) {
	const std::string *a = ini_find(shared, "np_account_id");
	uint64_t v = a ? strtoull(a->c_str(), nullptr, 16) : 0;
	if (!v) {
		std::random_device rd; v = ((uint64_t)rd() << 32 | rd()) | 1;
		char b[24]; snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
		if (g_settings_writable) target_write(SetTarget{&shared, shared_path(), SHARED_HEADER}, "np_account_id", b);
	}
	static const char al[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* (no 0/O, 1/I lookalikes) */
	std::string tag; for (int n = 0; n < 6; n++, v >>= 5) tag += al[v & 31];
	return tag;
}
/* Settings, Library: the library folders (kept, searched again, removed), and how a copy from one is added */
static std::string g_lib_pick, g_lib_pick_v3k;   /* a folder just picked to keep */
static void pref_toggle_row(const char *label, const char *expl, const char *key, bool def) {   /* (hub.ini: the launcher's own, no game reads it) */
	ui::row(label, expl);
	auto it = g_prefs.find(key); bool on = it == g_prefs.end() || it->second.empty() ? def : it->second != "0";
	if (ui::toggle(key, &on)) prefs_set(key, on ? "1" : "0");
}
static void library_tab(void) {
	const float em = ImGui::GetFontSize();
	if (!g_lib_pick.empty() && !g_task.running) { lib_add_folder(g_lib_pick, false); g_lib_pick.clear(); }
	if (!g_lib_pick_v3k.empty() && !g_task.running) { lib_add_folder(g_lib_pick_v3k, true); g_lib_pick_v3k.clear(); }
	ImGui::PushTextWrapPos(0);
	ImGui::TextUnformatted("Library folders");
	ui::muted_wrapped("Folders Nativ looks through for your games, their updates, add-ons and licences: a copy of your Vita's ux0:, a folder of dumps, or Vita3K's. A game found there shows on its card, ready to add. Nothing in these folders is ever changed.");
	ImGui::Dummy(ImVec2(0, em * 0.2f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::surface);
	ImGui::BeginChild("libfolders", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
	if (g_lib_folders.empty()) ImGui::TextDisabled("No folders yet.");
	bool v3k_kept = false;
	for (size_t i = 0; i < g_lib_folders.size(); i++) {
		const LibFolder f = g_lib_folders[i];
		v3k_kept |= f.kind == "vita3k";
		ImGui::PushID((int)i);
		const bool here = exists(f.root);
		int n = 0; for (const Found &x : g_lib_found) n += x.source == f.path && (x.kind == "game" || x.kind == "update" || x.kind == "addon");
		ui::dot(here ? ui::ready : ui::warn);
		ImGui::TextUnformatted(f.path.c_str());
		ImGui::SameLine(); ImGui::TextDisabled("%s%s", f.kind == "vita3k" ? "Vita3K  ·  " : "", !here ? "not connected" : (std::to_string(n) + (n == 1 ? " copy" : " copies")).c_str());
		const float rw = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - rw);
		ImGui::BeginDisabled(g_task.running);
		if (small_button("Remove")) lib_remove_folder(f.path);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Only from this list: the folder, and games already added from it, stay.");
		ImGui::PopID();
	}
	ImGui::EndChild(); ImGui::PopStyleColor();
	const bool looking = g_task.running && g_task.kind == "libscan";
	ImGui::BeginDisabled(g_task.running);
	if (ImGui::Button("Add a folder…")) pick_folder(nullptr, &g_lib_pick);
	same_line_fits(button_w("Add a Vita3K folder…")); if (ImGui::Button("Add a Vita3K folder…")) pick_folder(nullptr, &g_lib_pick_v3k);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Vita3K's own folder (where Vita3K.exe and config.yml are, a portable install too),\nits data folder, or its ux0: its games, updates, add-ons and licences are found.");
	same_line_fits(button_w("Look now")); ImGui::BeginDisabled(g_lib_folders.empty());
	if (ImGui::Button("Look now")) task_start("Looking in your library folders", "libscan", "", lib_scan_steps());
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	if (looking) { ImGui::SameLine(); ui::spinner(ui::accent); ImGui::TextDisabled("Looking…"); }
	const std::string v3k = v3k_kept ? "" : vita3k_default_dir();
	if (!v3k.empty()) {
		ImGui::Dummy(ImVec2(0, em * 0.2f));
		ui::dot(ui::accent); ImGui::TextWrapped("Vita3K is installed here: %s", v3k.c_str());
		ImGui::BeginDisabled(g_task.running);
		if (ui::primary_button("Use Vita3K's games and licences")) lib_add_folder(v3k, true);
		ImGui::EndDisabled();
	}
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, em * 0.4f));
	if (ui::rows_begin("library")) {
		pref_toggle_row("Look for new copies on start", "Each time Nativ starts, the library folders are searched again (in the background).", "library_rescan", true);
		pref_toggle_row("Read in place from other drives", "A game found on another drive than your library is copied in full when added. On: its data stays where it is and is read from there (Vita3K's games, already decrypted) -- no second copy, but that drive has to be connected to play. On the same drive nothing is ever copied twice.", "library_in_place", false);
		ui::rows_end();
	}
}
static SDL_Window *g_win;   /* (the launcher's window: About's fullscreen) */
static void about_tab(void) {   /* (UI-01i) the version, the launcher's own window, and Quit -- the in-game menu's Help has its About too */
	const float em = ImGui::GetFontSize();
	ImGui::PushTextWrapPos(0);
	{ ImVec2 p = ImGui::GetCursorScreenPos(); const float h = em * 1.6f; ImGui::Dummy(ImVec2(ui::wordmark(ImGui::GetWindowDrawList(), p, h), h)); }
	const std::string v = first_line(g_root + "/version.txt");
	ImGui::TextUnformatted(v.empty() ? "A development checkout (no version.txt)" : ("Version " + v).c_str());
	ui::muted_wrapped("Your PS Vita games, rebuilt natively: no Sony or game code ships with Nativ; your copies, licences and saves stay on this computer.");
	ImGui::Dummy(ImVec2(0, em * 0.4f));
	if (ui::rows_begin("about")) {
		ui::row("Launcher in fullscreen", "This window filling the screen; F11 switches it too. For a TV, or the launcher as a Steam entry (--fullscreen).", g_prefs["launcher_fullscreen"] == "1" ? nullptr : "default");
		bool fs = g_prefs["launcher_fullscreen"] == "1";
		if (ui::toggle("launcher_fs", &fs)) { prefs_set("launcher_fullscreen", fs ? "1" : "0"); if (g_win && !g_shot_mode) SDL_SetWindowFullscreen(g_win, fs); }
		ui::row("Quit", "Closes the launcher. A build or game started from it stops.");
		if (ImGui::Button("Quit Project Nativ")) { if (g_shot_mode) g_shot_done.push_back("quit"); else { SDL_Event q = {}; q.type = SDL_EVENT_QUIT; SDL_PushEvent(&q); } }
		ui::rows_end();
	}
	ImGui::PopTextWrapPos();
}
/* ---- Controls > Mapping (UI-01m): which button or key does which of the Vita's -- for the controller and for keyboard
 * and mouse, the same settings keys the game's own menu (F10, Controls) writes (runtime/ui/window.c: pad_bind_<x> with
 * "south" / "east" / ... names, "" the default, "none" unbound; bind_<x> with SDL key names), and the same rule: a button
 * another action had is taken from it. Change: press the new button or key (a controller's press is taken for
 * 6 seconds; Esc leaves a key as it was), or pick it from the list (touch and a mouse can map a controller too).
 * Touch: the on-screen buttons (UI-01n: "osb", "osb_size", the same keys as the game's menu) and the game's touch rules */
struct BindRow { const char *key, *label, *def; };
static const BindRow k_pad_rows[] = {   /* (window.c pad_binds, in its order; def: pad_bind.c's defaults) */
	{"pad_bind_cross", "Cross", "south"}, {"pad_bind_circle", "Circle", "east"}, {"pad_bind_square", "Square", "west"}, {"pad_bind_triangle", "Triangle", "north"},
	{"pad_bind_start", "Start", "start"}, {"pad_bind_select", "Select", "back"}, {"pad_bind_up", "D-pad up", "dpad_up"}, {"pad_bind_down", "D-pad down", "dpad_down"},
	{"pad_bind_left", "D-pad left", "dpad_left"}, {"pad_bind_right", "D-pad right", "dpad_right"}, {"pad_bind_l3", "L3", "left_stick"}, {"pad_bind_r3", "R3", "right_stick"}};
static const BindRow k_key_rows[] = {   /* (window.c kb_binds, in its order, with its defaults) */
	{"bind_move_up", "Move forward (left stick)", "W"}, {"bind_move_down", "Move back", "S"}, {"bind_move_left", "Move left", "A"}, {"bind_move_right", "Move right", "D"},
	{"bind_cross", "Cross", "Space"}, {"bind_circle", "Circle", "C"}, {"bind_square", "Square", "F"}, {"bind_triangle", "Triangle", "E"},
	{"bind_l", "L (also the right mouse button)", "Q"}, {"bind_r", "R (also the left mouse button)", "R"}, {"bind_start", "Start", "Return"}, {"bind_select", "Select", "Tab"},
	{"bind_up", "D-pad up", "Up"}, {"bind_down", "D-pad down", "Down"}, {"bind_left", "D-pad left", "Left"}, {"bind_right", "D-pad right", "Right"}};
static const char *const k_pad_names[] = {"south", "east", "west", "north", "start", "back", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "left_stick", "right_stick"};
static std::string pad_button_label(const std::string &name) {   /* "south" -> the pad's own name (Cross / A / B on a Switch pad) */
	const PadNames *n = pad_names_of(g_pad_fam);
	if (name == "south") return n->select; if (name == "east") return n->back; if (name == "west") return n->west; if (name == "north") return n->north;
	if (name == "start") return n->menu; if (name == "back") return n->view;
	if (name == "dpad_up") return "D-pad up"; if (name == "dpad_down") return "D-pad down"; if (name == "dpad_left") return "D-pad left"; if (name == "dpad_right") return "D-pad right";
	if (name == "left_stick") return "Left stick (press)"; if (name == "right_stick") return "Right stick (press)";
	return "(none)";
}
static std::string bind_value(const SetTarget &t, const BindRow &r, bool pad) {   /* what the game reads for this row */
	const std::string v = tget(t, r.key, "");
	if (pad) return v.empty() ? r.def : v;
	return v.empty() ? r.def : v;
}
static void bind_write(const SetTarget &t, bool pad, int row, const std::string &value) {   /* the game's rule: the button or key another row had is taken from it */
	const BindRow *rows = pad ? k_pad_rows : k_key_rows; const int n = pad ? 12 : 16;
	for (int j = 0; j < n; j++) if (j != row && bind_value(t, rows[j], pad) == value) target_write(t, rows[j].key, pad ? "none" : "");
	target_write(t, rows[row].key, value);
}
static struct { bool open = false, pad = true; int row = 0; double until = 0; } g_bind;   /* the Change dialog */
static int g_map_type;   /* 0 controller, 1 keyboard & mouse, 2 touch */
static const char *imgui_key_sdl_name(ImGuiKey k) {   /* a pressed key as SDL names it (window.c reads SDL_GetKeyFromName) */
	static char b[8];
	if (k >= ImGuiKey_A && k <= ImGuiKey_Z) { b[0] = (char)('A' + (k - ImGuiKey_A)); b[1] = 0; return b; }
	if (k >= ImGuiKey_0 && k <= ImGuiKey_9) { b[0] = (char)('0' + (k - ImGuiKey_0)); b[1] = 0; return b; }
	switch (k) {
	case ImGuiKey_Space: return "Space"; case ImGuiKey_Enter: return "Return"; case ImGuiKey_Tab: return "Tab"; case ImGuiKey_Backspace: return "Backspace";
	case ImGuiKey_UpArrow: return "Up"; case ImGuiKey_DownArrow: return "Down"; case ImGuiKey_LeftArrow: return "Left"; case ImGuiKey_RightArrow: return "Right";
	case ImGuiKey_LeftShift: return "Left Shift"; case ImGuiKey_RightShift: return "Right Shift"; case ImGuiKey_LeftCtrl: return "Left Ctrl"; case ImGuiKey_RightCtrl: return "Right Ctrl";
	case ImGuiKey_LeftAlt: return "Left Alt"; case ImGuiKey_CapsLock: return "CapsLock";
	case ImGuiKey_Comma: return ","; case ImGuiKey_Period: return "."; case ImGuiKey_Minus: return "-"; case ImGuiKey_Semicolon: return ";";
	default: return nullptr;
	}
}
static void bind_modal(const SetTarget &t) {   /* (inside Settings) */
	if (!g_bind.open) return;
	const ImGuiViewport *vp = ImGui::GetMainViewport(); const float em = ImGui::GetFontSize();
	if (!ImGui::IsPopupOpen("##bind")) ImGui::OpenPopup("##bind");
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 40 * em ? vp->WorkSize.x * 0.94f : 34 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("##bind", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	const BindRow &r = (g_bind.pad ? k_pad_rows : k_key_rows)[g_bind.row];
	ui::heading((std::string(r.label) + (g_bind.pad ? ": which button?" : ": which key?")).c_str(), em * 1.3f);
	const double left = g_bind.until - ImGui::GetTime();
	ImGui::PushTextWrapPos(0);
	if (g_bind.pad) { char s[96]; snprintf(s, sizeof s, "Press the button on the controller now (%d s), or pick it below.", (int)ceil(left > 0 ? left : 0)); ImGui::TextUnformatted(s); }
	else ImGui::TextUnformatted("Press the key now, or pick it below. Esc leaves it as it was.");
	ui::muted_wrapped(("Now: " + (g_bind.pad ? pad_button_label(bind_value(t, r, true)) : bind_value(t, r, false))).c_str());
	ui::muted_wrapped(g_bind.pad ? "A button another action had is taken from it." : "A key another action had is taken from it.");
	ImGui::PopTextWrapPos();
	std::string chosen; bool cancel = false;
	/* the pick list: by touch or with a mouse (no controller focus: a controller's press is the answer) */
	ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * 0.5f, ImGui::GetStyle().ItemSpacing.y * 0.5f));
	if (g_bind.pad) {
		for (const char *nm : k_pad_names) { const std::string l = pad_button_label(nm) + "##pick" + nm; same_line_fits(button_w(l.c_str())); if (ImGui::Button(l.c_str())) chosen = nm; }
	} else {
		static const char *const keys[] = {"W", "A", "S", "D", "Q", "E", "R", "F", "C", "Space", "Return", "Tab", "Up", "Down", "Left", "Right", "Left Shift", "Left Ctrl", "1", "2", "3", "4"};
		for (const char *k : keys) { const std::string l = std::string(k) + "##pick"; same_line_fits(button_w(l.c_str())); if (ImGui::Button(l.c_str())) chosen = k; }
	}
	ImGui::PopStyleVar();
	ImGui::Dummy(ImVec2(0, em * 0.3f));
	if (ImGui::Button("Cancel##bind")) cancel = true;
	ImGui::PopItemFlag();
	/* the press itself */
	if (g_bind.pad) {
		static const std::pair<ImGuiKey, const char *> pads[] = {{ImGuiKey_GamepadFaceDown, "south"}, {ImGuiKey_GamepadFaceRight, "east"}, {ImGuiKey_GamepadFaceLeft, "west"},
			{ImGuiKey_GamepadFaceUp, "north"}, {ImGuiKey_GamepadStart, "start"}, {ImGuiKey_GamepadBack, "back"}, {ImGuiKey_GamepadDpadUp, "dpad_up"},
			{ImGuiKey_GamepadDpadDown, "dpad_down"}, {ImGuiKey_GamepadDpadLeft, "dpad_left"}, {ImGuiKey_GamepadDpadRight, "dpad_right"},
			{ImGuiKey_GamepadL3, "left_stick"}, {ImGuiKey_GamepadR3, "right_stick"}};
		for (auto &pk : pads) if (ImGui::IsKeyPressed(pk.first, false)) chosen = pk.second;
		if (left <= 0) cancel = true;
	} else {
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cancel = true;
		else for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && chosen.empty(); k++)
			if (ImGui::IsKeyPressed((ImGuiKey)k, false)) if (const char *nm = imgui_key_sdl_name((ImGuiKey)k)) chosen = nm;
	}
	if (!chosen.empty()) {
		if (g_settings_writable) bind_write(t, g_bind.pad, g_bind.row, chosen);
		if (g_shot_mode) g_shot_done.push_back(std::string("bound ") + r.key + "=" + chosen);
	}
	if (!chosen.empty() || cancel) { g_bind.open = false; ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}
static bool g_bind_open(void) { return g_bind.open; }
static void mapping_rows(const SetTarget &t) {   /* (Controls tab, after its rows) */
	const float em = ImGui::GetFontSize();
	ui::row("Mapping for", "Each way of playing has its own mapping; the game's own menu (F10, Controls) changes the same.");
	{	static const char *const types[] = {"Controller##mt", "Keyboard & mouse##mt", "Touch##mt"}; ui::segmented(types, 3, &g_map_type); }
	ui::rows_end();
	ImGui::Dummy(ImVec2(0, em * 0.2f));
	if (g_map_type == 2) {   /* (UI-01n) the on-screen buttons: when they show, how big; the rest of the screen is the Vita's front touch screen */
		if (ui::rows_begin("map_touch")) {
			seg_row(t, "On-screen buttons", "For playing by touch alone: the Vita's buttons and sticks on the screen. Automatic shows them once you touch the screen and hides them when you use a controller or the keyboard.",
			        "osb", "auto", {{"auto", "Automatic"}, {"on", "Always"}, {"off", "Off"}});
			slider_row(t, "Button size", "Bigger buttons are easier to hit; smaller ones cover less of the game.", "osb_size", 1.0, 0.75f, 1.5f, "%.2f×");
			ui::rows_end();
		}
		ImGui::Dummy(ImVec2(0, em * 0.2f));
		ImGui::PushTextWrapPos(0);
		ui::muted_wrapped("Where they sit: L and R in the top corners, the D-pad and the left stick on the left, the face buttons and the right stick on the right, Select and Start at the bottom. A touch anywhere else is the Vita's front touch screen; the rear touch pad: hold the shoulder pair that isn't L and R and move a stick, or Alt and the mouse (see Touch cursor speed above).");
		ImGui::PopTextWrapPos();
		ui::rows_begin("map_rest");
		return;
	}
	const bool pad = g_map_type == 0;
	const BindRow *rows = pad ? k_pad_rows : k_key_rows; const int n = pad ? 12 : 16;
	if (ui::rows_begin(pad ? "map_pad" : "map_keys")) {
		for (int i = 0; i < n; i++) {
			const std::string v = bind_value(t, rows[i], pad);
			ui::row(rows[i].label, nullptr, v == rows[i].def ? "default" : nullptr);
			const std::string shown = (pad ? pad_button_label(v) : (v.empty() ? std::string("(none)") : v)) + "##map" + rows[i].key;
			if (ImGui::Button(shown.c_str(), ImVec2(-FLT_MIN, 0))) { g_bind.open = true; g_bind.pad = pad; g_bind.row = i; g_bind.until = ImGui::GetTime() + 6.0; }
		}
		ui::rows_end();
	}
	if (ImGui::Button(pad ? "Reset the controller mapping" : "Reset the keyboard mapping")) {
		for (int i = 0; i < n; i++) if (g_settings_writable) target_write(t, rows[i].key, pad ? "" : rows[i].def);
	}
	ui::rows_begin("map_rest");
}
static void settings_modal(void) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	if (g_settings_open && !ImGui::IsPopupOpen("Settings")) { ImGui::OpenPopup("Settings"); g_shared = ini_load(shared_path()); }
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 50 * em ? vp->WorkSize.x * 0.94f : 46 * em, vp->WorkSize.y * 0.86f), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	ui::heading("Settings", em * 1.4f);
	static const char *const tabs[] = {"Display##st", "Controls##st", "Sound & online##st", "Library##st", "About##st"};
	ui::segmented(tabs, 5, &g_settings_tab); tab_hint();
	g_settings_tab = (g_settings_tab + tab_step() + 5) % 5;
	ImGui::Dummy(ImVec2(0, em * 0.2f));
	SetTarget t{&g_shared, shared_path(), SHARED_HEADER};
	ImGui::BeginChild("settings_rows", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - em * 0.4f - (hints_visible() ? ImGui::GetTextLineHeightWithSpacing() + em * 0.15f : 0)), ImGuiChildFlags_NavFlattened);
	if (g_settings_tab <= 2) {   /* (what these settings are: at the top of the rows, scrolling with them -- a small window has no room for it above) */
		ImGui::PushTextWrapPos(0);
		ui::muted_wrapped("For every game. A game reads them when it starts; its own menu (F10) changes them too, for every game. A game's own options, and any value it keeps for itself, are on its page (its card's ...).");
		ImGui::PopTextWrapPos(); ImGui::Dummy(ImVec2(0, em * 0.2f));
	}
	if (g_settings_tab == 3) library_tab();
	else if (g_settings_tab == 4) about_tab();
	else if (ui::rows_begin("shared")) {
		if (g_settings_tab == 0) {
			ui::row("Resolution", "The game drawn at a multiple of the Vita's 960 × 544. Higher is sharper; 4× and up want a fast GPU.");
			{	ImGui::PushID("render_scale");
				const char *const names[6] = {"1×", "2×", "3×", "4×", "5×", "6×"}; int v = atoi(tget(t, "render_scale", "1").c_str()) - 1; if (v < 0) v = 0; if (v > 5) v = 5;
				if (ui::segmented(names, 6, &v)) target_write(t, "render_scale", std::to_string(v + 1));
				int r = recommended_scale(); if (r != v + 1) { char l[48]; snprintf(l, sizeof l, "%d× suits this display", r); if (link_button(l)) target_write(t, "render_scale", std::to_string(r)); }
				ImGui::PopID(); }
			toggle_row(t, "Fullscreen", "Also kept when you use the window's own fullscreen button.", "fullscreen", false);
			seg_row(t, "Widescreen", "Shows more of the game at the sides, in games that support it.", "aspect_ratio", "native", {{"native", "Off"}, {"16:10", "16:10"}, {"3:2", "3:2"}, {"21:9", "21:9"}, {"32:9", "32:9"}, {"window", "Window"}});
			seg_row(t, "HUD", "With widescreen on: the HUD at the screen's edges, in a 16:9 frame in the middle, or stretched.", "hud_layout", "edges", {{"edges", "Edges"}, {"16:9", "16:9"}, {"stretch", "Stretched"}});
			seg_row(t, "Picture fit", "How the picture fills the window.", "aspect", "fit", {{"fit", "Fit"}, {"stretch", "Stretch"}, {"fill", "Fill"}});
			seg_row(t, "Frame rate", "Above 30 is experimental: parts of a game timed in fixed 1/30 s steps can run fast. A game runs at the highest rate it allows up to this one (its Game settings show which).", "frame_rate", "30", {{"30", "30"}, {"60", "60"}, {"120", "120"}});
			toggle_row(t, "Anti-aliasing", "Smooths jagged edges (FXAA).", "post_aa", true);
			slider_row(t, "Sharpening", "Brings back detail lost to scaling and anti-aliasing.", "sharpen", 0.35, 0.0f, 100.0f, "%.0f%%", 100.0f);
			seg_row(t, "Texture filtering", "Keeps textures crisp when seen at an angle.", "anisotropy", "16", {{"1", "Off"}, {"2", "2×"}, {"4", "4×"}, {"8", "8×"}, {"16", "16×"}});
			seg_row(t, "Shadow edges", "How soft the edges of shadows are.", "shadow_filter", "1", {{"0", "Original"}, {"1", "Soft"}, {"2", "Softer"}});
			toggle_row(t, "Full-resolution world", "Games that draw their 3D world below the screen's resolution draw it at the full one. Sharper, at a GPU cost.", "full_res_world", true);
			toggle_row(t, "Ambient occlusion", "Soft shadows where surfaces meet, in games that support it.", "ssao", true);
			slider_row(t, "Occlusion strength", nullptr, "ssao_strength", 1.0, 0.0f, 200.0f, "%.0f%%", 100.0f);
			seg_row(t, "On-screen stats", "Frame rate and timings over the game. F1 cycles them.", "osd", "0", {{"0", "Off"}, {"1", "FPS"}, {"2", "Full"}});
		} else if (g_settings_tab == 1) {
			seg_row(t, "Play with", "Automatic follows the last device you touch.", "input_mode", "auto", {{"auto", "Automatic"}, {"pad", "Controller"}, {"kbm", "Keyboard & mouse"}});
			toggle_row(t, "Mouse look", "The mouse turns the camera and aims; left button R, right button L.", "mouse_look", true);
			seg_row(t, "Mouse aim", "Direct: the game turns exactly as far as the mouse moves (Uncharted, Unit 13, Killzone). Stick: the mouse moves the right stick. F8 switches in a game.", "raw_look", "1", {{"1", "Direct"}, {"0", "Stick"}});
			slider_row(t, "Mouse sensitivity", nullptr, "mouse_sens", 1.0, 0.1f, 5.0f, "%.2f×", 1, ImGuiSliderFlags_Logarithmic);
			toggle_row(t, "Invert vertical look", nullptr, "mouse_invert", false);
			seg_row(t, "L and R buttons", "The shoulder pair that is the game's L and R (aim and fire in a shooter). The other pair holds the touch cursor, or is L2 and R2 in a PS TV layout.", "shoulder_buttons", "triggers", {{"triggers", "Triggers (L2 / R2)"}, {"bumpers", "Bumpers (L1 / R1)"}});
			slider_row(t, "Touch cursor speed", "Hold the shoulder pair that isn't L and R, and move a stick: the cursor is a finger on the touch screen.", "touch_cursor_speed", 1.0, 0.25f, 3.0f, "%.2f×");
			toggle_row(t, "Gyro", "A controller's gyro as the Vita's motion sensor.", "gyro", true);
			seg_row(t, "A level controller is a Vita held", "Upright, as in your hands, or flat on a table.", "gyro_pose", "upright", {{"upright", "Upright"}, {"flat", "Flat"}});
			slider_row(t, "Gyro sensitivity", nullptr, "gyro_sens", 1.0, 0.25f, 3.0f, "%.2f×");
			mapping_rows(t);
		} else {
			slider_row(t, "Volume", nullptr, "volume", 1.0, 0.0f, 100.0f, "%.0f%%", 100.0f);
			toggle_row(t, "Play online", ONLINE_EXPL, "online", true);
			{ const std::string st = online_status_line(); if (!st.empty()) ui::muted_wrapped(st.c_str()); }
			ui::row("Online name", "Others see ntv_ and your name, so players know you're on PC: up to 10 letters, digits, '-' or '_'.");
			{	static std::string oid; static bool init; if (!init || !ImGui::IsPopupOpen("Settings")) { oid = online_name(*t.x); init = true; }
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("ntv_"); ImGui::SameLine(0, ImGui::GetFontSize() * 0.3f);   /* (room for the field's focus frame: it covered the "_") */
				input_text("##oid", oid, -FLT_MIN);
				const bool ok = online_name_ok(oid);
				if (input_done(oid) && ok) target_write(t, "online_id", oid);
				if (!ok) ui::muted_wrapped("Not saved: up to 10 letters, digits, '-' or '_'");
				else if (oid.empty()) ui::muted_wrapped(("Empty: you play as ntv_" + online_tag(*t.x) + ", a tag made for this computer.").c_str());
			}
		}
		ui::rows_end();
	}
	ImGui::EndChild();
	if (ImGui::Button("Use the recommended set")) { Ini r; recommended_into(r); for (auto &e : r.kv) target_write(t, e.first, e.second); }
	if (g_touch || GImGui->NavCursorVisible) { char rs[160]; snprintf(rs, sizeof rs, "Fullscreen, %d× resolution, widescreen to the window, 60 fps, direct mouse aim, softer shadows", recommended_scale()); ImGui::SameLine(); info_text("##recinfo", ui::muted, "What it sets", rs); }
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fullscreen, %d× resolution, widescreen to the window, 60 fps, direct mouse aim, softer shadows", recommended_scale());
	ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Done").x - ImGui::GetStyle().FramePadding.x * 2);
	if (ui::primary_button("Done") || dialog_back()) { g_settings_open = false; ImGui::CloseCurrentPopup(); }
	dialog_hints();
	child_dialogs();   /* (Library: Add a folder; the online name) */
	bind_modal(SetTarget{&g_shared, shared_path(), SHARED_HEADER});
	ImGui::EndPopup();
}

/* a game's own settings: its options (the pack's RECOMP_GAME_OPTIONS -- the ones its hooks honour) and the values it keeps
 * apart from the shared ones, each of which can go back to the shared value */
static std::string g_game_settings;   /* the game whose dialog is open ("": none) */
static Ini g_game_ini;
static bool game_option(const Game &g, const char *name) {
	std::string env = pack_value(pack_read(g_root + "/packs/" + g.id + "/pack.toml"), "RUN_ENV") + " ";
	size_t a = env.find("RECOMP_GAME_OPTIONS="); if (a == std::string::npos) return false;
	std::string list = "," + env.substr(a + 20, env.find(' ', a) - a - 20) + ",";
	return list.find(std::string(",") + name + ",") != std::string::npos;
}
/* the frame rates a game allows (its pack's RUN_ENV RECOMP_FRAME_RATES, as the game itself reads it: frame_rates.h) */
static int game_frame_rates(const Game &g) {
	std::string env = pack_value(pack_read(g_root + "/packs/" + g.id + "/pack.toml"), "RUN_ENV") + " ";
	size_t a = env.find("RECOMP_FRAME_RATES="); if (a == std::string::npos) return fr_allowed(nullptr);
	return fr_allowed(env.substr(a + 19, env.find(' ', a) - a - 19).c_str());
}
static const char *setting_name(const std::string &k) {
	static const std::pair<const char *, const char *> names[] = {{"render_scale", "Resolution"}, {"fullscreen", "Fullscreen"}, {"aspect_ratio", "Widescreen"},
		{"hud_layout", "HUD"}, {"aspect", "Picture fit"}, {"frame_rate", "Frame rate"}, {"post_aa", "Anti-aliasing"}, {"sharpen", "Sharpening"},
		{"anisotropy", "Texture filtering"}, {"shadow_filter", "Shadow edges"}, {"full_res_world", "Full-resolution world"}, {"ssao", "Ambient occlusion"},
		{"ssao_strength", "Occlusion strength"}, {"osd", "On-screen stats"}, {"input_mode", "Play with"}, {"mouse_look", "Mouse look"}, {"raw_look", "Mouse aim"},
		{"mouse_sens", "Mouse sensitivity"}, {"mouse_invert", "Invert vertical look"}, {"shoulder_buttons", "L and R buttons"}, {"volume", "Volume"}, {"online_id", "Online name"}, {"online", "Play online"}, {"gyro", "Gyro"},
		{"gyro_sens", "Gyro sensitivity"}, {"camera", "Rear camera"}};
	for (auto &n : names) if (k == n.first) return n.second;
	return nullptr;
}
static void game_settings_modal(void) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	const Game *g = nullptr; for (const Game &x : g_games) if (x.id == g_game_settings) g = &x;
	if (!g_game_settings.empty() && !ImGui::IsPopupOpen("Game settings")) { ImGui::OpenPopup("Game settings"); g_game_ini = ini_load(game_settings_path(g_game_settings)); }
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 44 * em ? vp->WorkSize.x * 0.94f : 40 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("Game settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	if (!g) { g_game_settings.clear(); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
	ui::heading((g->name + " settings").c_str(), em * 1.4f);
	dialog_body_begin();
	SetTarget t{&g_game_ini, game_settings_path(g->id), GAME_HEADER};
	bool any = false;
	if (ui::rows_begin("game_own")) {
		{	/* the frame rate, among only the rates this game allows; kept for this game (its own file wins over the shared one) */
			any = true;
			const int allowed = game_frame_rates(*g); int l[3], n = fr_list(allowed, l);
			Ini sh = ini_load(shared_path()); const std::string *shv = ini_find(sh, "frame_rate");
			const int cur = fr_effective(atoi(tget(t, "frame_rate", shv ? shv->c_str() : "30").c_str()), allowed);
			std::string rates; for (int i = 0; i < n; i++) rates += (i ? ", " : "") + std::to_string(l[i]);
			std::string expl = "This game allows " + rates + " fps. Above 30 is experimental.";
			ui::row("Frame rate", expl.c_str());
			ImGui::PushID("frame_rate_game");
			static const char *const all[3] = {"30", "60", "120"}; const char *names[3]; int v = 0;
			for (int i = 0; i < n; i++) { names[i] = all[l[i] >= 120 ? 2 : l[i] >= 60 ? 1 : 0]; if (l[i] == cur) v = i; }
			if (ui::segmented(names, n, &v)) target_write(t, "frame_rate", std::to_string(l[v]));
			ImGui::PopID();
		}
		if (game_option(*g, "fov")) { any = true; slider_row(t, "Field of view", "A wider view. 1.00× is the game's own.", "fov", 1.0, 0.75f, 1.5f, "%.2f×"); }
		if (game_option(*g, "view_distance")) { any = true; slider_row(t, "View distance", "How far fading scenery reaches. 1.00× is the game's own.", "view_distance", 1.0, 1.0f, 3.0f, "%.2f×"); }
		if (game_option(*g, "scenery_detail")) { any = true; slider_row(t, "Scenery detail", "How far small scenery details stay visible. 1.00× is the game's own.", "scenery_detail", 1.0, 1.0f, 8.0f, "%.1f×"); }
		if (game_option(*g, "character_detail")) { any = true; slider_row(t, "Character detail", "How far characters and props keep their full detail. 1.00× is the game's own.", "character_detail", 1.0, 1.0f, 8.0f, "%.1f×"); }
		if (game_option(*g, "private_server")) {   /* (a game whose online needs its makers' servers: Killzone; Unit 13 needs none) */
			any = true;
			ui::row("Private server", "The community server that stands in for the game's own (host, or host:port). Empty: the game's own, long offline.");
			static std::string srv; static std::string for_id; if (for_id != g->id) { srv = tget(t, "private_server", ""); for_id = g->id; }
			input_text("##srv", srv, -FLT_MIN);
			if (input_done(srv)) target_write(t, "private_server", srv);
		}
		ui::rows_end();
	}
	if (!any) ui::muted_wrapped("This game has no options of its own yet.");
	/* what it keeps apart from the shared settings */
	Ini sh = ini_load(shared_path());
	std::vector<std::pair<std::string, std::string>> own;
	for (auto &e : g_game_ini.kv) if (!game_key(e.first) && setting_name(e.first)) own.push_back(e);
	ImGui::Dummy(ImVec2(0, em * 0.5f));
	if (own.empty()) ui::muted_wrapped("Everything else follows the shared Settings.");
	else {
		ImGui::TextUnformatted("Kept for this game only");
		ui::muted_wrapped("Values this game uses instead of the shared Settings (from before settings were shared, or set here).");
		for (auto &e : own) {
			ImGui::PushID(e.first.c_str());
			const std::string *sv = ini_find(sh, e.first);
			ImGui::Bullet(); ImGui::Text("%s: %s", setting_name(e.first), e.second.c_str());
			ImGui::SameLine(); ImGui::TextDisabled("(shared: %s)", sv ? sv->c_str() : setting_default(e.first) ? setting_default(e.first) : "default");
			ImGui::SameLine(); if (small_button("Use shared")) { ini_erase(g_game_ini, e.first); Ini disk = ini_load(t.path); ini_erase(disk, e.first); ini_save(disk, t.path, GAME_HEADER); }
			ImGui::PopID();
		}
	}
	dialog_body_end();
	if (ui::primary_button("Done") || dialog_back()) { g_game_settings.clear(); ImGui::CloseCurrentPopup(); }
	dialog_hints();
	child_dialogs();
	ImGui::EndPopup();
}

/* ---- Windows' Smart App Control. When it's on, a program that isn't signed and that Windows doesn't already know gets a
 * verdict from Microsoft's cloud, one per file (by its hash), and the verdict sticks. Every game Nativ builds is new to it
 * (made on this PC, from the player's own copy), and now and then one is refused: on one PC (2026-10-26) Shovel Knight's
 * build was, while Killzone's, the launcher, and the same Shovel Knight code with one byte more all started. A rebuild is
 * a new file (the linker stamps the time), checked afresh; turning Smart App Control off is the way left after that, as
 * Windows has no exception for one program. The dialog opens when Windows refused a game: vitarecomp's SAC_BLOCKED line */
static bool g_sac_open, g_sac_rebuilt;   /* (rebuilt: the player already rebuilt it once and it was refused again) */
static std::string g_sac_game;            /* the game Windows refused */
/* ---- the online name, before the first online game: what other players will see (the same field as Settings' Online name
 * and setup's). Asked once; Play with it empty keeps the tag made for this computer */
static void online_name_modal(void) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	static std::string name;
	if (!g_name_game.empty() && !ImGui::IsPopupOpen("Online name")) { name.clear(); ImGui::OpenPopup("Online name"); }
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 40 * em ? vp->WorkSize.x * 0.94f : 32 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("Online name", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	const Game *g = nullptr; for (const Game &x : g_games) if (x.id == g_name_game) g = &x;
	if (!g) { g_name_game.clear(); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
	ui::heading("Your online name", em * 1.4f);
	dialog_body_begin();
	ImGui::PushTextWrapPos(0);
	ImGui::TextUnformatted((g->name + " plays online with other Project Nativ players. Choose the name they'll see: it shows as ntv_ and your name.").c_str());
	ImGui::Dummy(ImVec2(0, em * 0.3f));
	ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("ntv_"); ImGui::SameLine(0, ImGui::GetFontSize() * 0.3f);   /* (room for the field's focus frame: it covered the "_") */
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	input_text("##name_oid", name, em * 12, "your name");
	const bool ok = online_name_ok(name);
	if (!ok) ui::muted_wrapped("Up to 10 letters, digits, '-' or '_'.");
	else if (name.empty()) ui::muted_wrapped(("Left empty, you play as ntv_" + online_tag(g_shared) + ", a tag made for this computer.").c_str());
	ui::muted_wrapped("You can change it any time in Settings.");
	ImGui::PopTextWrapPos();
	dialog_body_end();
	ImGui::BeginDisabled(!ok);
	if (ui::primary_button("Play") || (ok && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
		if (!name.empty() && g_settings_writable) target_write(SetTarget{&g_shared, shared_path(), SHARED_HEADER}, "online_id", name);
		prefs_set("online_name_asked", "1");
		const Game game = *g; g_name_game.clear(); ImGui::CloseCurrentPopup(); ImGui::EndDisabled(); ImGui::EndPopup();
		start_play_now(game);
		return;
	}
	ImGui::EndDisabled();
	same_line_fits(button_w("Not now")); if (ImGui::Button("Not now") || dialog_back()) { g_name_game.clear(); ImGui::CloseCurrentPopup(); }
	dialog_hints();
	child_dialogs();
	ImGui::EndPopup();
}
static void sac_modal(void) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	if (g_sac_open && !ImGui::IsPopupOpen("Smart App Control")) ImGui::OpenPopup("Smart App Control");
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 44 * em ? vp->WorkSize.x * 0.94f : 36 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("Smart App Control", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	const Game *g = nullptr; for (const Game &x : g_games) if (x.id == g_sac_game) g = &x;
	ui::heading(("Windows blocked " + (g ? g->name : g_sac_game)).c_str(), em * 1.4f);
	dialog_body_begin();
	ImGui::PushTextWrapPos(0);
	ImGui::TextUnformatted(g_sac_rebuilt ? "Windows' Smart App Control refused the rebuilt game too." : "Windows' Smart App Control refused to start it.");
	ImGui::Dummy(ImVec2(0, em * 0.3f));
	ImGui::TextUnformatted("Smart App Control checks each program Windows doesn't know with Microsoft's cloud, and every game Nativ builds is "
	                       "new to it, because it's built on this PC from your own copy. Its verdict is made once for each file, and now and "
	                       "then it refuses one. A rebuild makes a new file that Windows checks afresh, and that usually starts.");
	ImGui::Dummy(ImVec2(0, em * 0.3f));
	ImGui::TextUnformatted("If it's refused again, turn Smart App Control off: in Windows Security, open App & browser control, then Smart App "
	                       "Control settings, and choose Off. Windows has no way to allow a single program.");
	ImGui::Dummy(ImVec2(0, em * 0.2f));
	ui::muted_wrapped("On most versions of Windows 11, Smart App Control can't be turned back on without reinstalling Windows.");
	ImGui::PopTextWrapPos();
	dialog_body_end();
	auto rebuild = [&]() {   /* (a relink: the same code as a new file, then the game, as Play does) */
		Step b = step("Rebuilding " + g->name + " as a new file for Windows to check", vitarecomp({"build", g->id}));
		Step r = step("Running " + g->name, vitarecomp({"run", g->id})); r.env.push_back("ATIV_LAUNCHER=1");
		task_start("Playing " + g->name, "play", g->id, {b, r});
		g_sac_rebuilt = true; g_sac_open = false; ImGui::CloseCurrentPopup();
	};
	auto settings = [&]() { open_url("windowsdefender://smartapp"); };
	ImGui::BeginDisabled(!g || g_task.running);
	if (g_sac_rebuilt ? ImGui::Button("Rebuild and try again") : ui::primary_button("Rebuild and try again")) rebuild();
	ImGui::EndDisabled();
	same_line_fits(button_w("Open Smart App Control settings"));
	if (g_sac_rebuilt ? ui::primary_button("Open Smart App Control settings") : ImGui::Button("Open Smart App Control settings")) settings();
	same_line_fits(button_w("Close")); if (ImGui::Button("Close") || dialog_back()) { g_sac_open = g_sac_rebuilt = false; ImGui::CloseCurrentPopup(); }
	dialog_hints();
	ImGui::EndPopup();
}

/* ---- setup: welcome, firmware, games, ready (first run, or whenever the firmware is missing) */
static bool g_setup_open;
static int g_setup_step;
static void stepper(int step) {
	static const char *const names[] = {"Welcome", "Firmware", "Your games", "Online", "Ready"};
	ImDrawList *dl = ImGui::GetWindowDrawList();
	float h = ImGui::GetTextLineHeight(), r = h * 0.5f;
	float all = 0; for (int i = 0; i < 5; i++) all += r * 2 + h * 0.35f + ImGui::CalcTextSize(names[i]).x + (i ? h * 0.9f : 0);
	const bool compact = all > ImGui::GetContentRegionAvail().x;   /* (a narrow window: the numbers, and only the current step's name) */
	for (int i = 0; i < 5; i++) {
		if (i) ImGui::SameLine(0, compact ? h * 0.35f : h * 0.9f);
		ImVec2 p = ImGui::GetCursorScreenPos();
		bool done = i == 1 ? g_firmware : i < step, cur = i == step;   /* (firmware: done only when installed, skipped or not) */
		ImVec4 c = cur ? ui::accent : done ? ui::ready : ui::border;
		dl->AddCircleFilled(ImVec2(p.x + r, p.y + r), r, ui::u32(c));
		char n[4]; snprintf(n, sizeof n, "%d", i + 1);
		ImVec2 ts = ImGui::CalcTextSize(n);
		dl->AddText(ImVec2(p.x + r - ts.x * 0.5f, p.y + r - ts.y * 0.5f), ui::u32(cur || done ? ui::accent_ink : ui::muted), n);
		ImGui::Dummy(ImVec2(r * 2, h));
		if (compact && !cur) continue;
		ImGui::SameLine(0, h * 0.35f);
		ImGui::PushStyleColor(ImGuiCol_Text, cur ? ui::text : ui::muted); ImGui::TextUnformatted(names[i]); ImGui::PopStyleColor();
	}
}
static void setup_modal(SDL_Window *win) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	float w = vp->WorkSize.x < 44 * em ? vp->WorkSize.x * 0.94f : 40 * em;
	ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (g_setup_open && !ImGui::IsPopupOpen("Setup")) ImGui::OpenPopup("Setup");
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.8f * em, 1.5f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("Setup", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	stepper(g_setup_step);
	ImGui::Dummy(ImVec2(0, em * 0.6f));
	auto finish = [&]() { prefs_set("setup_done", "1"); g_setup_open = false; ImGui::CloseCurrentPopup(); };
	static std::string s3_name; static bool s3_loaded;   /* (the online name step's field) */
	int have = 0; for (const Game &g : g_games) have += g.imported;
	/* the step's content scrolls when the window is too small for it; its buttons, below, stay in view */
	dialog_body_begin();
	ImGui::PushTextWrapPos(0);
	switch (g_setup_step) {
	case 0:
		{	/* the logo: the Vita, its screen dark, the name beneath (assets/brand/logo.svg) */
			ImDrawList *dl = ImGui::GetWindowDrawList(); const float avail = ImGui::GetContentRegionAvail().x, lw = std::min(em * 12, avail * 0.55f);
			ImVec2 p = ImGui::GetCursorScreenPos(); p.x += (avail - lw) * 0.5f;
			ui::vita(dl, p, lw, ui::u32(ui::text), ui::u32(ImVec4(0.051f, 0.059f, 0.075f, 1)));
			ui::wordmark(dl, ImVec2(p.x + lw * 0.25f, p.y + lw * 310 / 600), lw / 6);
			ImGui::Dummy(ImVec2(lw, lw * 420 / 600 + em * 0.4f));
		}
		ui::heading("Welcome to Project Nativ", em * 1.55f);
		ImGui::TextWrapped("Nativ rebuilds the PS Vita games you own as native programs for this computer — sharper, wider and faster than the console could run them.");
		ImGui::Dummy(ImVec2(0, em * 0.3f));
		ImGui::TextWrapped("It ships no Sony or game code. You bring two things, and they stay on this computer:");
		ImGui::Bullet(); ImGui::TextWrapped("The PS Vita system software (firmware) update file — free, from Sony's own website.");
		ImGui::Bullet(); ImGui::TextWrapped("Your own copies of your games and their updates: game folders copied from your Vita, or PKGs, or a whole folder to search.");
		ImGui::Bullet(); ImGui::TextWrapped("Their licences: a game folder from your Vita carries its own. For PKGs, Nativ fetches yours from your Vita over Wi-Fi, once.");
		ui::muted_wrapped("Each game plays as one exact release: the US version, at the versions its card lists (Killzone: 1.00 or its 1.12 update). Other regions and versions are recognised and named, but not supported yet.");
		break;
	case 1:
		ui::heading("Firmware", em * 1.55f);
		if (g_firmware) {
			ui::dot(ui::ready); ImGui::TextWrapped("Installed (%s). Every game builds against it — nothing more to do here.", g_firmware_version.c_str());
		} else {
			ImGui::TextWrapped("Download the PS Vita system software update file from Sony's official page, then choose it below. Its system libraries are decrypted once and every game builds against them.");
			ImGui::Dummy(ImVec2(0, em * 0.2f));
			if (ImGui::Button("Open Sony's download page")) open_url(SONY_FIRMWARE_PAGE);
			same_line_fits(ImGui::CalcTextSize("playstation.com/.../psvita/system-software").x); if (link_button("playstation.com/.../psvita/system-software")) open_url(SONY_FIRMWARE_PAGE);
			ui::muted_wrapped("On that page, download the update file (PSVUPDAT.PUP or PSP2UPDAT.PUP, version 3.74). Any recent version works.");
			ImGui::Dummy(ImVec2(0, em * 0.2f));
			if (path_row("##pup", g_fw_path, "Choose the update file (.PUP)")) pick_pup(win, &g_fw_path);
			ImGui::BeginDisabled(g_fw_path.empty() || g_task.running);
			if (ui::primary_button("Install firmware")) task_start("Installing the firmware", "firmware", "", {step("Decrypting the system update", vitarecomp({"firmware", g_fw_path})), step("Installed")});
			ImGui::EndDisabled();
		}
		ImGui::Dummy(ImVec2(0, em * 0.2f)); task_line();
		if (g_task.running) { ImGui::Dummy(ImVec2(0, em * 0.3f)); ui::muted_wrapped("It carries on in the background: continue whenever you like."); }
		break;
	case 2:
		ui::heading("Your games", em * 1.55f);
		ImGui::TextWrapped("Add the games you own, with their updates. Each one is decrypted and checked against its game pack, which says which versions it plays. Your licences are kept once: every later PKG, update or add-on of those games just adds.");
		ImGui::Dummy(ImVec2(0, em * 0.2f));
		add_form(win);
		ImGui::Dummy(ImVec2(0, em * 0.2f)); task_line();
		ImGui::Dummy(ImVec2(0, em * 0.2f)); licences_row(win);
		if (have) {
			ImGui::Dummy(ImVec2(0, em * 0.3f));
			ImGui::TextDisabled("In your library:");
			for (const Game &g : g_games) if (g.imported) { ImGui::Bullet(); ImGui::TextWrapped("%s", g.name.c_str()); }
		}
		if (g_task.running) { ImGui::Dummy(ImVec2(0, em * 0.3f)); ui::muted_wrapped("Adding and building carry on in the background: continue whenever you like."); }
		break;
	case 3: {   /* the name other players see (the same field as Settings' Online name) */
		if (!s3_loaded) { g_shared = ini_load(shared_path()); s3_name = online_name(g_shared); s3_loaded = true; }
		ui::heading("Your online name", em * 1.55f);
		ImGui::TextWrapped("The name other players see when you play a game online. It shows as ntv_ and your name, so they know you're playing on PC.");
		ImGui::Dummy(ImVec2(0, em * 0.2f));
		ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("ntv_"); ImGui::SameLine(0, ImGui::GetFontSize() * 0.3f);   /* (room for the field's focus frame: it covered the "_") */
		input_text("##setup_oid", s3_name, std::min(em * 12, ImGui::GetContentRegionAvail().x), "your name");
		const bool ok = online_name_ok(s3_name);
		if (!ok) ui::muted_wrapped("Up to 10 letters, digits, '-' or '_'.");
		else if (s3_name.empty()) ui::muted_wrapped(("Left empty, you play as ntv_" + online_tag(g_shared) + ", a tag made for this computer.").c_str());
		ui::muted_wrapped("You can change it any time in Settings.");
		ImGui::Dummy(ImVec2(0, em * 0.5f));
		{	static bool online = true; static bool online_loaded;
			if (!online_loaded) { const std::string *v = ini_find(g_shared, "online"); online = !v || atoi(v->c_str()) != 0; online_loaded = true; }
			ui::toggle("setup_online", &online); ImGui::SameLine(); ImGui::TextUnformatted("Play online");
			ui::muted_wrapped(ONLINE_EXPL);
			g_setup_online = online;
		}
		break;
	}
	default:
		ui::heading("You’re set", em * 1.55f);
		ImGui::TextWrapped("Each game is built into a native program the first time — a few minutes per game — and starts straight away after that.");
		ImGui::Bullet(); ImGui::TextWrapped("In a game, F10 (or the PS button) opens the menu: display, controls, frame rate, field of view.");
		ImGui::Bullet(); ImGui::TextWrapped("Settings, at the top right, holds the display, controls and sound for every game; a card's … menu has that game's own options.");
		ImGui::Bullet(); ImGui::TextWrapped("Setup is at the top right whenever you need it again.");
		if (g_task.running) { ImGui::Dummy(ImVec2(0, em * 0.3f)); ui::muted_wrapped("Still working on your games: the bar at the bottom of the library shows how far it's got."); }
		break;
	}
	ImGui::PopTextWrapPos();
	dialog_body_end();
	/* the step's buttons: always in view */
	switch (g_setup_step) {
	case 0:
		if (ui::primary_button("Get started")) g_setup_step = 1;
		same_line_fits(button_w("Set up later")); if (ImGui::Button("Set up later") || dialog_back()) finish();
		break;
	case 1:
		if (g_firmware || g_task.running) { if (ui::primary_button("Continue")) g_setup_step = 2; }
		else if (ImGui::Button("Skip for now")) g_setup_step = 2;
		same_line_fits(button_w("Back")); if (ImGui::Button("Back") || dialog_back()) g_setup_step = 0;
		break;
	case 2:
		if (have || g_task.running ? ui::primary_button("Continue") : ImGui::Button("Skip for now")) g_setup_step = 3;   /* (never waits on a running task: the bar at the bottom shows it) */
		same_line_fits(button_w("Back")); if (ImGui::Button("Back") || dialog_back()) g_setup_step = 1;
		break;
	case 3:
		ImGui::BeginDisabled(!online_name_ok(s3_name));
		if (ui::primary_button("Continue")) {
			if (g_settings_writable) { SetTarget st{&g_shared, shared_path(), SHARED_HEADER}; target_write(st, "online_id", s3_name); target_write(st, "online", g_setup_online ? "1" : "0"); }
			g_setup_step = 4;
		}
		ImGui::EndDisabled();
		same_line_fits(button_w("Back")); if (ImGui::Button("Back") || dialog_back()) g_setup_step = 2;
		break;
	default:
		if (ui::primary_button("Go to my library")) finish();
		same_line_fits(button_w("Back")); if (ImGui::Button("Back") || dialog_back()) g_setup_step = 3;
		break;
	}
	dialog_hints();
	child_dialogs();   /* (Firmware, Your games, the online name) */
	ImGui::EndPopup();
}

static bool g_add_open;   /* the Add a game dialog */

/* ---- Steam: a game as a Steam shortcut (pipeline/steam.py: "<this launcher> --play <title>" in every Steam user's
 * shortcuts.vdf, tagged Project Nativ). What Steam has from us is read at start and after each change (vitarecomp steam
 * list). Steam keeps its list in memory while it runs, so a change waits for it to be closed: the dialog says so */
static struct { std::mutex m; bool have = false; std::set<std::string> in; } g_steam;
static void steam_parse(const std::string &out) {
	bool have = false; std::set<std::string> in;
	for (size_t a = 0; a < out.size(); ) {
		size_t e = out.find('\n', a); if (e == std::string::npos) e = out.size();
		std::string l = out.substr(a, e - a); while (!l.empty() && l.back() == '\r') l.pop_back();
		if (l.rfind("steam ", 0) == 0) have = l != "steam -";
		else if (l.rfind("in ", 0) == 0) in.insert(l.substr(3, l.find(' ', 3) - 3));
		a = e + 1;
	}
	std::lock_guard<std::mutex> lk(g_steam.m); g_steam.have = have; g_steam.in = in;
}
static void steam_refresh(void) {
	std::thread([]() {
		Proc p; if (!proc_start(p, vitarecomp({"steam", "list"}), {})) return;
		std::string out; char b[512]; long n; while ((n = proc_read(p, b, sizeof b)) > 0) out.append(b, (size_t)n);
		proc_wait(p); proc_done(p);
		steam_parse(out);
		std::string dir = "-", run = "0"; size_t n_in = 0;   /* one line in the log: what was found (for a question about it) */
		for (size_t a = 0; a < out.size(); ) {
			size_t e = out.find('\n', a); if (e == std::string::npos) e = out.size();
			std::string l = out.substr(a, e - a); while (!l.empty() && l.back() == '\r') l.pop_back();
			if (l.rfind("steam ", 0) == 0) dir = l.substr(6); else if (l.rfind("running ", 0) == 0) run = l.substr(8); else if (l.rfind("in ", 0) == 0) n_in++;
			a = e + 1;
		}
		log_line(dir == "-" ? std::string("Steam: not found on this computer") :
		         "Steam: " + dir + (run == "1" ? ", running" : ", not running") + ", " + std::to_string(n_in) + " game(s) added from here");
	}).detach();
}
static std::string self_exe(void) {   /* this launcher, as Steam is to start it (in an AppImage: the AppImage; "": not known) */
#ifdef _WIN32
	wchar_t w[MAX_PATH * 4]; DWORD n = GetModuleFileNameW(nullptr, w, MAX_PATH * 4);
	if (!n || n >= MAX_PATH * 4) return "";
	int m = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, nullptr, 0, nullptr, nullptr);
	std::string s((size_t)m, '\0'); WideCharToMultiByte(CP_UTF8, 0, w, (int)n, &s[0], m, nullptr, nullptr);
	return s;
#elif defined(__APPLE__)
	return "";   /* (not offered on the Mac yet) */
#else
	if (const char *a = getenv("APPIMAGE")) if (*a) return a;
	char b[4096]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
	return n > 0 ? std::string(b, (size_t)n) : "";
#endif
}
static std::vector<Step> g_steam_steps; static std::string g_steam_name, g_steam_game;   /* the last change, for Try again */
static bool g_steam_closed_open;   /* the "close Steam first" dialog */
static bool steam_addable(const Game &g);
static void steam_task(const Game &g, bool add) {
	if (add && !steam_addable(g)) return;   /* (never a shortcut to a game that isn't built) */
	std::vector<std::string> v{python_exe(), g_root + "/pipeline/vitarecomp.py", "steam", add ? "add" : "remove", g.id};
	if (add) {
		v.insert(v.end(), {"--exe", self_exe(), "--name", g.name});
		if (!g.icon.empty()) v.insert(v.end(), {"--icon", g.icon});
		if (!g.cover.empty()) v.insert(v.end(), {"--cover", g.cover});
	}
	g_steam_steps = {step(add ? "Adding it to Steam" : "Removing it from Steam", v)};
	g_steam_name = (add ? "Adding " : "Removing ") + g.name + (add ? " to Steam" : " from Steam"); g_steam_game = g.id;
	task_start(g_steam_name, "steam", g.id, g_steam_steps);
}
/* (UI-01j) Steam can't be closed by the player in the Deck's Game Mode -- it is the screen. "Restart Steam and add it" lets
 * steam.py ask Steam to quit, make the change once it has, and start it again (--restart; in Game Mode Steam's session
 * starts it by itself). Started from Steam, the launcher would go with it: there the change runs on its own, outside
 * Steam's processes (systemd-run --user, else a new session), its output in <prefs>/steam-restart.log */
static bool started_by_steam(void) {
	const char *deck = getenv("SteamDeck"), *gid = getenv("SteamGameId"), *aid = getenv("SteamAppId");
	return (deck && !strcmp(deck, "1")) || (gid && *gid) || (aid && *aid);
}
static bool g_steam_restarting;   /* (the detached change is under way: the dialog says so) */
static void steam_restart_change(void) {
	std::vector<Step> s = g_steam_steps;
	for (Step &st : s) if (!st.argv.empty()) st.argv.push_back("--restart");
	if (g_shot_mode) { g_shot_done.push_back("steam restart " + std::string(started_by_steam() ? "detached" : "task")); return; }
#ifndef _WIN32
	if (started_by_steam() && !s.empty() && !s[0].argv.empty()) {
		std::vector<std::string> argv = s[0].argv;
		if (system("command -v systemd-run >/dev/null 2>&1") == 0) argv.insert(argv.begin(), {"systemd-run", "--user", "--collect", "--quiet"});
		const std::string logf = pref_dir() + "steam-restart.log";
		std::vector<char *> a; for (std::string &x : argv) a.push_back(&x[0]); a.push_back(nullptr);
		pid_t pid = fork();
		if (pid == 0) {   /* a new session, its output into the log: it outlives Steam's quitting and the launcher's */
			setsid();
			int fd = open(logf.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
			if (chdir(g_root.c_str()) != 0) _exit(126);
			execvp(a[0], a.data());
			_exit(127);
		}
		log_line(pid > 0 ? "Steam restarts to take the change (" + logf + ")" : std::string("couldn't start the change"));
		g_steam_restarting = pid > 0;
		return;
	}
#endif
	task_start(g_steam_name, "steam", g_steam_game, s);
}
static void steam_closed_modal(void) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	if (g_steam_closed_open && !ImGui::IsPopupOpen("Close Steam")) ImGui::OpenPopup("Close Steam");
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 44 * em ? vp->WorkSize.x * 0.94f : 32 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("Close Steam", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	ui::heading("Close Steam first", em * 1.4f);
	dialog_body_begin();
	ImGui::PushTextWrapPos(0);
	ImGui::TextUnformatted("Steam is running. It keeps its list of games in memory and writes it back when it closes, so a change made now would be lost.");
	ImGui::Dummy(ImVec2(0, em * 0.3f));
	ImGui::TextUnformatted(started_by_steam() ? "Nativ can restart Steam to make the change: Steam quits for a moment, the game is added, and Steam starts again with it."
	                                          : "Let Nativ restart Steam to make the change (Steam quits for a moment and starts again), or close Steam yourself (Steam menu, Exit) and try again.");
	if (g_steam_restarting) { ImGui::Dummy(ImVec2(0, em * 0.2f)); ui::muted_wrapped("Steam is restarting: the change is made while it's closed."); }
	ImGui::PopTextWrapPos();
	dialog_body_end();
	ImGui::BeginDisabled(g_task.running || g_steam_restarting);
	const bool adding = g_steam_name.rfind("Adding ", 0) == 0;
	if (ui::primary_button(adding ? "Restart Steam and add it" : "Restart Steam and remove it")) { steam_restart_change(); if (!g_steam_restarting) { g_steam_closed_open = false; ImGui::CloseCurrentPopup(); } }
	same_line_fits(button_w("Try again"));
	if (ImGui::Button("Try again")) { g_steam_closed_open = false; ImGui::CloseCurrentPopup(); task_start(g_steam_name, "steam", g_steam_game, g_steam_steps); }
	ImGui::EndDisabled();
	same_line_fits(button_w("Not now")); if (ImGui::Button("Not now") || dialog_back()) { g_steam_closed_open = false; ImGui::CloseCurrentPopup(); }
	dialog_hints();
	ImGui::EndPopup();
}

/* ---- a game's state, its title-id line and its actions: on its card and on its row of the list */
/* ---- what the list shows beside a game (UI-01l): when it was last played and for how long -- <prefs>/playtime.tsv, written
 * when a game started from here or from a Steam shortcut ends (the time it ran, not its build check) -- and how much
 * room it takes: its folder in the library, counted in the background; a file shared with another copy (Vita3K's, on
 * the same drive: a hard link) counts as shared, data read in place from another drive not at all */
static std::map<std::string, PlayTime> g_playtime;
static std::string playtime_path(void) { const std::string d = pref_dir(); return d.empty() ? "" : d + "playtime.tsv"; }
static void playtime_load(void) { g_playtime = playtime_parse(read_file(playtime_path())); }
static void playtime_record(const std::string &id, long long started, long long seconds) {
	playtime_load(); playtime_add(g_playtime, id, started, seconds);
	const std::string p = playtime_path(); if (p.empty() || !g_settings_writable) return;
	const std::string t = playtime_format(g_playtime);
	if (FILE *f = fopen((p + ".tmp").c_str(), "wb")) { fwrite(t.data(), 1, t.size(), f); fclose(f);
#ifdef _WIN32
		MoveFileExA((p + ".tmp").c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
		rename((p + ".tmp").c_str(), p.c_str());
#endif
	}
}
static struct { std::mutex m; std::map<std::string, std::pair<uint64_t, uint64_t>> sizes; std::atomic<bool> busy{false}, again{false}; } g_size;   /* id -> (own, shared) bytes */
#ifdef _WIN32
static std::string utf8_of(const std::wstring &w) {
	const int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)m, '\0'); if (m) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], m, nullptr, nullptr);
	return s;
}
#endif
static void size_walk(const std::string &dir, uint64_t &own, uint64_t &shared, int depth) {
	if (depth > 40) return;
#ifdef _WIN32
	WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(wide(dir + "/*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::wstring n = fd.cFileName;
		if (n == L"." || n == L".." || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;   /* (a junction: data read in place elsewhere) */
		const std::string path = dir + "/" + utf8_of(n);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { size_walk(path, own, shared, depth + 1); continue; }
		const uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
		DWORD links = 1;
		HANDLE f = CreateFileW(wide(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (f != INVALID_HANDLE_VALUE) { BY_HANDLE_FILE_INFORMATION bi; if (GetFileInformationByHandle(f, &bi)) links = bi.nNumberOfLinks; CloseHandle(f); }
		(links > 1 ? shared : own) += sz;
	} while (FindNextFileW(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir.c_str()); if (!d) return;
	while (dirent *e = readdir(d)) {
		const std::string n = e->d_name; if (n == "." || n == "..") continue;
		const std::string path = dir + "/" + n; struct stat st;
		if (lstat(path.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
		if (S_ISDIR(st.st_mode)) size_walk(path, own, shared, depth + 1); else (st.st_nlink > 1 ? shared : own) += (uint64_t)st.st_size;
	}
	closedir(d);
#endif
}
static void size_refresh(void) {   /* every added game's size, in the background (again when asked while it runs) */
	if (g_shot_mode) return;   /* (a screenshot run: no sizes, the same picture each time) */
	if (g_size.busy.exchange(true)) { g_size.again = true; return; }
	std::vector<std::string> ids; for (const Game &g : g_games) if (g.imported) ids.push_back(g.id);
	const std::string dump = g_dump;
	std::thread([ids, dump]() {
		do {
			g_size.again = false;
			for (const std::string &id : ids) { uint64_t own = 0, sh = 0; size_walk(dump + "/decrypted/" + id, own, sh, 0); std::lock_guard<std::mutex> l(g_size.m); g_size.sizes[id] = {own, sh}; }
		} while (g_size.again.exchange(false));
		g_size.busy = false;
	}).detach();
}
static uint64_t size_of(const Game &g, uint64_t *shared = nullptr, bool *known = nullptr) {
	std::lock_guard<std::mutex> l(g_size.m); auto it = g_size.sizes.find(g.id);
	if (known) *known = it != g_size.sizes.end();
	if (it == g_size.sizes.end()) return 0;
	if (shared) *shared = it->second.second;
	return it->second.first + it->second.second;
}
static std::string stat_last(const Game &g) { auto it = g_playtime.find(g.id); const std::string s = it == g_playtime.end() ? "" : fmt_last(it->second.last, (long long)time(nullptr)); return !s.empty() ? s : g.imported ? "Never" : "-"; }
static std::string stat_time(const Game &g) { auto it = g_playtime.find(g.id); const std::string s = it == g_playtime.end() ? "" : fmt_played(it->second.seconds); return !s.empty() ? s : g.imported ? "Never played" : "-"; }
static std::string stat_size(const Game &g) {
	if (!g.imported) return "-";
	uint64_t sh = 0; bool known = false; const uint64_t all = size_of(g, &sh, &known);
	if (!known) return "...";
	return sh ? fmt_size(all) + " (" + fmt_size(sh) + " shared)" : fmt_size(all);
}
static bool g_list_stats;
static long long g_run_started;   /* (a game's run from here: when it began, 0 none) */   /* (the list's table has the three columns: wide enough) */
static void game_state(const Game &g, const Found *lf, bool mine) {
	if (mine) { ui::spinner(ui::accent); ImGui::TextDisabled("%s", task_status().c_str()); }
	else if (g.imported && !g.missing.empty()) {
		ui::dot(ui::warn); info_text("##drive", ui::text, "Its game data's drive isn't connected",
		                             "This copy reads its data where it was found:\n" + g.missing + "\nConnect that drive again, or add the copy again with Settings, Library, \"Read in place\" off.");
	}
	else if (g.built) { ui::dot(ui::ready); ImGui::TextUnformatted("Ready to play"); }
	else if (g.imported) { ui::dot(ui::warn); ImGui::TextUnformatted("Needs a one-time build"); }
	else if (lf) {
		ui::dot(ui::accent); info_text("##found", ui::text, "Found in " + leaf_name(lf->source), lf->name + " " + lf->version + "\n" + lf->path);
	}
	else { ui::dot(ui::border); ImGui::TextDisabled("Not added yet"); }
}
static std::string id_line_text(const Game &g, std::string *update) {   /* the id line's text, and the update it names ("": none) */
	std::string line = g.id, rec = recommended_version(g), vs = played_versions(g);
	if (!g.pack) line += "  -  experimental (no game pack)";
	else if (g.imported && !g.version.empty() && g.version != "-") line += "  ·  " + (g.region.empty() ? "" : g.region + " ") + g.version;
	else if (!g.imported && !vs.empty()) line += "  ·  plays " + (g.region.empty() ? "" : g.region + " ") + vs;
	{ std::lock_guard<std::mutex> lk(g_steam.m); if (g_steam.in.count(g.id)) line += "  ·  In Steam"; }
	if (update) *update = g.imported && !rec.empty() && g.version != rec ? rec : "";
	return line;
}
static float id_line_height(const Game &g, float wrap) {   /* (its height at this width: the cards of a row reserve the tallest, so their buttons line up) */
	std::string upd, line = id_line_text(g, &upd);
	if (!upd.empty()) line += "  Update " + upd + " available";
	return ImGui::CalcTextSize(line.c_str(), nullptr, false, wrap).y;
}
static void game_id_line(const Game &g) {   /* the title id and the version: this copy's, else what the pack plays; In Steam; a chip when the pack recommends another */
	std::string rec, line = id_line_text(g, &rec);
	ImGui::TextDisabled("%s", line.c_str());
	if (!rec.empty()) {
		ImGui::SameLine();   /* (one unwrapped line of text, not a chip: every card's lines keep one height) */
		/* ("Update 1.12" beside the version read as the copy's own: a 1.00 copy was taken for 1.12) */
		info_text("##update", ui::warn, "Update " + rec + " available",
		          (g.update_note.empty() ? "Version " + rec + " is recommended" : g.update_note) + " (this copy is " + g.version + ").\nAdd it with + Add game: its update folder (ux0:patch/" + g.id + ") or PKG,\nor search the folder you copied from your Vita.");
	}
}
static std::string g_game_page;   /* (UI-01i) the game whose page is open ("": none; ATIV_SHOT_UI=more:<title> opens it in a screenshot run) */
/* Add to Steam only for a game that is Ready to play (its built program is there, its data reachable): a shortcut to a
 * game never built would only fail in Steam. Remove from Steam is always offered, so an old shortcut can go. */
static bool steam_addable(const Game &g) { return g.built && (!g.imported || g.missing.empty()); }
/* the first press of a direction lands on the first game ready to play (else the first game): its main button is the
 * library's default focus (hub_ui picks the game each frame) */
static std::string g_focus_game;
static bool g_focus_placed;   /* (once a launcher run: then ImGui keeps the focus where the player moves it, across dialogs too) */
/* the library scrolls between a pinned header and a pinned bar: the D-pad reaches the header only from the top of the list
 * and the bar only from its end, so up / down in between move through the games (and scroll), not out to them */
static float g_lib_scroll, g_lib_scroll_max;   /* (the library's scroll, the frame before) */
static std::map<ImGuiID, std::string> g_nav_game;   /* (ATIV_SHOT_NAV: which game a focused button belongs to) */
static const Found *lib_find(const Game &g);
/* the library's order (UI-01l): by state in sections -- ready to play, needs a build, found in your folders, supported but
 * not added (folded until asked) -- or one list by name, last played, time played or size (hub.ini library_sort,
 * library_desc, library_show_all) */
static std::string g_lib_sort; static bool g_lib_desc, g_lib_show_all, g_lib_sort_loaded;
struct LibSection { const char *title; std::vector<const Game *> games; bool folded = false; };
static std::vector<LibSection> library_sections(void) {
	if (!g_lib_sort_loaded) { g_lib_sort_loaded = true; g_lib_sort = g_prefs["library_sort"]; if (g_lib_sort.empty()) g_lib_sort = "state"; g_lib_desc = g_prefs["library_desc"] == "1"; g_lib_show_all = g_prefs["library_show_all"] == "1"; }
	std::vector<LibSection> v;
	if (g_lib_sort == "state") {
		LibSection s[4] = {{"Ready to play", {}}, {"Needs a one-time build", {}}, {"Found in your folders, ready to add", {}}, {"Supported, not added yet", {}}};
		for (const Game &g : g_games) s[g.built ? 0 : g.imported ? 1 : lib_find(g) ? 2 : 3].games.push_back(&g);
		s[3].folded = !g_lib_show_all;
		for (auto &x : s) if (!x.games.empty()) v.push_back(x);
		return v;
	}
	LibSection all{nullptr, {}}; for (const Game &g : g_games) all.games.push_back(&g);
	auto key_num = [](const Game &g) -> long long {
		if (g_lib_sort == "last") { auto it = g_playtime.find(g.id); return it == g_playtime.end() ? 0 : it->second.last; }
		if (g_lib_sort == "time") { auto it = g_playtime.find(g.id); return it == g_playtime.end() ? 0 : it->second.seconds; }
		return (long long)size_of(g);
	};
	std::stable_sort(all.games.begin(), all.games.end(), [&](const Game *a, const Game *b) {
		if (g_lib_sort == "name") { const std::string x = browse_lower(a->name), y = browse_lower(b->name); return g_lib_desc ? y < x : x < y; }
		const long long x = key_num(*a), y = key_num(*b); return g_lib_desc ? x < y : x > y;   /* (the most first, as a list of what was played is read) */
	});
	v.push_back(all);
	return v;
}
static void first_game_focus(const Game &g) {
	if (g_shot_mode) g_nav_game[ImGui::GetItemID()] = g.id;
	if (g.id != g_focus_game) return;
	ImGui::SetItemDefaultFocus();
	if (g_focus_placed || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || GImGui->NavId) return;
	g_focus_placed = true;   /* as a click would: this button has the focus, its frame shows with the first press of a direction */
	ImGui::FocusWindow(ImGui::GetCurrentWindow()); ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
	ImGui::SetNavCursorVisible((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_HasGamepad) != 0);   /* (a controller connected: its frame shows at once, so A plays; with a mouse it shows with the first arrow key) */
}
static void game_actions(const Game &g, const Found *lf, float bw) {   /* the one primary action, the "more" menu beside it */
	const bool busy = g_task.running;
	const float em = ImGui::GetFontSize(), more = ImGui::GetFrameHeight();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImGui::BeginDisabled(busy);
	if (g.built) { if (ui::primary_button("Play", ImVec2(bw - more - ImGui::GetStyle().ItemSpacing.x, 0))) start_play(g); first_game_focus(g); ImGui::SameLine(); }
	else if (g.imported) { if (ui::primary_button("Build", ImVec2(bw - more - ImGui::GetStyle().ItemSpacing.x, 0))) start_build(g); first_game_focus(g); ImGui::SameLine(); }
	else if (lf) {   /* from its library folder, as a search's Add does; a licence still missing: the search's list, to give it */
		if (ui::primary_button("Add", ImVec2(bw - more - ImGui::GetStyle().ItemSpacing.x, 0))) {
			if (found_ready(*lf, g_lib_found)) {
				int n = 0; std::vector<std::string> titles; std::vector<Step> s = found_steps(&n, &titles, g_lib_found, lf->id);
				task_start("Adding " + g.name, "add", g.id, s);
				for (const std::string &t : titles) if (t != g.id) g_task.games.push_back(t);
			} else {
				g_found.clear(); for (const Found &x : g_lib_found) if (x.id == lf->id) g_found.push_back(x);
				g_found_ready = true; g_add_kind = 0; g_scan_path = lf->source; g_add_open = true;
			}
		}
		first_game_focus(g);
		ImGui::SameLine();
	}
	else {   /* (a small card: the short label) */
		const float lw = bw - more - ImGui::GetStyle().ItemSpacing.x;
		const char *label = button_w("Add your copy…") <= lw ? "Add your copy…" : button_w("Add copy") <= lw ? "Add copy" : "Add";
		if (ImGui::Button(label, ImVec2(lw, 0))) g_add_open = true;
		first_game_focus(g);
		ImGui::SameLine();
	}
	ImGui::EndDisabled();
	{	/* the "more" button: three dots, drawn (the font may have no ellipsis glyph) */
		if (ImGui::Button("##more", ImVec2(more, 0))) g_game_page = g.id;   /* (UI-01i: the game's page, every action on it with its explanation) */
		if (g_shot_mode) g_nav_game[ImGui::GetItemID()] = g.id;
		ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(), m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
		for (int k = -1; k <= 1; k++) dl->AddCircleFilled(ImVec2(m.x + k * em * 0.32f, m.y), em * 0.09f, ui::u32(ui::text, 0.8f));
	}
}
/* ---- a game's row of the list view: its icon, name, title-id line, state, actions */
static void game_row(const Game &g, float ic) {
	const float em = ImGui::GetFontSize(), padv = em * 0.35f;
	const bool mine = g_task.running && std::find(g_task.games.begin(), g_task.games.end(), g.id) != g_task.games.end();
	const Found *lf = g.imported ? nullptr : lib_find(g);
	ImGui::PushID(g.id.c_str());
	ImGui::TableNextRow(0, ic + padv * 2);
	ImGui::TableSetColumnIndex(0);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padv);
	{	Tex icon = g.icon.empty() ? Tex() : texture(g.icon); ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList *dl = ImGui::GetWindowDrawList();
		if (icon.t) dl->AddImageRounded(tex_ref(icon), p, ImVec2(p.x + ic, p.y + ic), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ic * 0.16f);
		else dl->AddRectFilled(p, ImVec2(p.x + ic, p.y + ic), ui::u32(ui::card_hi), ic * 0.16f);
		ImGui::Dummy(ImVec2(ic, ic));
	}
	ImGui::SameLine(0, em * 0.8f);
	{	const float th = ImGui::GetTextLineHeightWithSpacing() * 2;   /* (name and id line, centred on the icon) */
		if (ic > th) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ic - th) * 0.5f);
		ImGui::BeginGroup(); ImGui::PushTextWrapPos(0);
		ImGui::TextUnformatted(g.name.c_str()); game_id_line(g);
		ImGui::PopTextWrapPos(); ImGui::EndGroup();
	}
	const float mid = padv + (ic - ImGui::GetFrameHeight()) * 0.5f;
	ImGui::TableSetColumnIndex(1); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padv + (ic - ImGui::GetTextLineHeight()) * 0.5f); game_state(g, lf, mine);
	int col = 2;
	if (g_list_stats) {   /* last played, time played, size */
		const std::string st[3] = {stat_last(g), stat_time(g), stat_size(g)};
		for (int c = 0; c < 3; c++) { ImGui::TableSetColumnIndex(col++); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padv + (ic - ImGui::GetTextLineHeight()) * 0.5f); ImGui::PushTextWrapPos(0); ImGui::TextWrapped("%s", st[c].c_str()); ImGui::PopTextWrapPos(); }
	}
	ImGui::TableSetColumnIndex(col); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + mid); game_actions(g, lf, ImGui::GetContentRegionAvail().x);
	ImGui::PopID();
}

/* ---- a game's row of the list in a narrow window: the icon and the name beside it, then its lines and actions under them */
static void game_row_stacked(const Game &g, float ic) {
	const float em = ImGui::GetFontSize();
	const bool mine = g_task.running && std::find(g_task.games.begin(), g_task.games.end(), g.id) != g_task.games.end();
	const Found *lf = g.imported ? nullptr : lib_find(g);
	ImGui::PushID(g.id.c_str());
	{	Tex icon = g.icon.empty() ? Tex() : texture(g.icon); ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList *dl = ImGui::GetWindowDrawList();
		if (icon.t) dl->AddImageRounded(tex_ref(icon), p, ImVec2(p.x + ic, p.y + ic), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ic * 0.16f);
		else dl->AddRectFilled(p, ImVec2(p.x + ic, p.y + ic), ui::u32(ui::card_hi), ic * 0.16f);
		ImGui::Dummy(ImVec2(ic, ic));
	}
	ImGui::SameLine(0, em * 0.8f);
	ImGui::BeginGroup(); ImGui::PushTextWrapPos(0);
	ImGui::TextWrapped("%s", g.name.c_str()); game_id_line(g); game_state(g, lf, mine);
	if (g.imported) ImGui::TextDisabled("Last played: %s  ·  Time played: %s  ·  Size: %s", stat_last(g).c_str(), stat_time(g).c_str(), stat_size(g).c_str());
	ImGui::PopTextWrapPos(); ImGui::EndGroup();
	game_actions(g, lf, ImGui::GetContentRegionAvail().x);
	ImGui::Dummy(ImVec2(0, em * 0.2f)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, em * 0.2f));
	ImGui::PopID();
}

/* ---- a game's card: its cover (pic0), icon, name, state, one primary action */
static void game_card(const Game &g, float w, float name_h, float id_h) {
	const float em = ImGui::GetFontSize(), pad = em * 0.9f;
	const float img_h = w * 544.0f / 960.0f;
	const bool mine = g_task.running && std::find(g_task.games.begin(), g_task.games.end(), g.id) != g_task.games.end();
	ImGui::PushID(g.id.c_str());
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::card); ImGui::PushStyleColor(ImGuiCol_Border, ui::border);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("card", ImVec2(w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float rounding = ImGui::GetStyle().ChildRounding;
	Tex cover = g.cover.empty() ? Tex() : texture(g.cover), icon = g.icon.empty() ? Tex() : texture(g.icon);
	if (cover.t) {   /* filled, not stretched: a LiveArea background is 840x500, the card 960x544 */
		ImVec2 uv0(0, 0), uv1(1, 1); float box = w / img_h, ia = (float)cover.w / (float)cover.h;
		if (ia > box) { uv0.x = (1 - box / ia) * 0.5f; uv1.x = 1 - uv0.x; } else { uv0.y = (1 - ia / box) * 0.5f; uv1.y = 1 - uv0.y; }
		dl->AddImageRounded(tex_ref(cover), p, ImVec2(p.x + w, p.y + img_h), uv0, uv1, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersTop);
	}
	else {   /* no art in this copy: a quiet panel with the title set large */
		dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + img_h), ui::u32(ui::card_hi), ui::u32(ui::card_hi), ui::u32(ui::surface), ui::u32(ui::surface));
		/* (as large as fits: every word whole on its line, the name inside the picture -- a narrow card set it smaller) */
		const float wrap = std::max(w - em * 3, em * 2);
		float fs = em * 1.9f; ImVec2 ts;
		for (;; fs *= 0.9f) {
			ImGui::PushFont(nullptr, fs);
			float widest = 0;
			for (size_t a = 0; a < g.name.size(); ) { size_t e = g.name.find(' ', a); if (e == std::string::npos) e = g.name.size(); widest = std::max(widest, ImGui::CalcTextSize(g.name.c_str() + a, g.name.c_str() + e).x); a = e + 1; }
			ts = ImGui::CalcTextSize(g.name.c_str(), nullptr, false, wrap);
			if ((widest <= wrap && ts.y <= img_h * 0.86f) || fs < em * 0.8f) break;
			ImGui::PopFont();
		}
		dl->AddText(nullptr, 0, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (img_h - ts.y) * 0.5f), ui::u32(ui::muted, 0.75f), g.name.c_str(), nullptr, wrap);
		ImGui::PopFont();
	}
	if (icon.t) {   /* the icon over the cover's bottom-left corner */
		float s = em * 3.0f; ImVec2 a(p.x + pad, p.y + img_h - s * 0.6f);
		dl->AddRectFilled(ImVec2(a.x - 2, a.y - 2), ImVec2(a.x + s + 2, a.y + s + 2), ui::u32(ui::card), em * 0.5f);
		dl->AddImageRounded(tex_ref(icon), a, ImVec2(a.x + s, a.y + s), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, em * 0.45f);
	}
	ImGui::Dummy(ImVec2(w, img_h + em * 2.0f));   /* (room for the icon on every card, so they line up) */
	/* the text block, one height on every card: name (two lines at most), state, title ID */
	float y0 = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(pad);
	ImGui::PushTextWrapPos(w - pad);
	ImGui::PushFont(nullptr, em * 1.15f); ImGui::TextWrapped("%s", g.name.c_str()); ImGui::PopFont();
	ImGui::SetCursorPosY(y0 + name_h + ImGui::GetStyle().ItemSpacing.y);
	ImGui::SetCursorPosX(pad);
	const Found *lf = g.imported ? nullptr : lib_find(g);
	game_state(g, lf, mine);
	ImGui::SetCursorPosX(pad);
	const float y_id = ImGui::GetCursorPosY();
	game_id_line(g);
	ImGui::SetCursorPosY(y_id + id_h + ImGui::GetStyle().ItemSpacing.y);   /* (every card's id line one height: the buttons of a row line up) */
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, em * 0.2f));
	ImGui::SetCursorPosX(pad);
	game_actions(g, lf, w - pad * 2);
	ImGui::Dummy(ImVec2(0, pad * 0.4f));
	ImGui::EndChild();
	ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
	ImGui::PopID();
}

/* ---- a game's page (UI-01i): what the card's "..." opens -- its picture, what it is and where it stands (version, region,
 * Steam, played, size, notes in plain sight) and every action as a full-size button with what it does written under it
 * (no hover needed: the explanations were tooltips). B or Back closes it */
static void game_page_modal(void) {
	if (g_game_page.empty()) return;
	const Game *gp = nullptr; for (const Game &x : g_games) if (x.id == g_game_page) gp = &x;
	if (!gp) { g_game_page.clear(); return; }
	const Game &g = *gp; const bool busy = g_task.running;
	const ImGuiViewport *vp = ImGui::GetMainViewport(); const float em = ImGui::GetFontSize();
	if (!ImGui::IsPopupOpen("##gamepage")) ImGui::OpenPopup("##gamepage");
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x < 48 * em ? vp->WorkSize.x * 0.94f : 44 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (!ImGui::BeginPopupModal("##gamepage", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) { ImGui::PopStyleVar(2); return; }
	ImGui::PopStyleVar(2);
	ImGui::PushTextWrapPos(0); ui::heading(g.name.c_str(), em * 1.4f); ImGui::PopTextWrapPos();
	bool close = false;
	dialog_body_begin();
	ImGui::PushTextWrapPos(0);
	{	/* the picture, as wide as fits up to half the page */
		Tex cover = g.cover.empty() ? Tex() : texture(g.cover);
		if (cover.t) {
			const float w = std::min(ImGui::GetContentRegionAvail().x, em * 20), h = w * 544.0f / 960.0f;
			float ia = (float)cover.w / (float)cover.h, box = w / h; ImVec2 uv0(0, 0), uv1(1, 1);
			if (ia > box) { uv0.x = (1 - box / ia) * 0.5f; uv1.x = 1 - uv0.x; } else { uv0.y = (1 - ia / box) * 0.5f; uv1.y = 1 - uv0.y; }
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddImageRounded(tex_ref(cover), p, ImVec2(p.x + w, p.y + h), uv0, uv1, IM_COL32_WHITE, em * 0.5f);
			ImGui::Dummy(ImVec2(w, h));
		}
	}
	const Found *lf = g.imported ? nullptr : lib_find(g);
	const bool mine = g_task.running && std::find(g_task.games.begin(), g_task.games.end(), g.id) != g_task.games.end();
	game_state(g, lf, mine);
	game_id_line(g);
	if (g.imported) ImGui::TextDisabled("Last played: %s  ·  Time played: %s  ·  Size: %s", stat_last(g).c_str(), stat_time(g).c_str(), stat_size(g).c_str());
	{ std::lock_guard<std::mutex> lk(g_steam.m); if (g_steam.have) ImGui::TextDisabled("%s", g_steam.in.count(g.id) ? "In your Steam library" : "Not in your Steam library"); }
	if (!g.pack) ui::muted_wrapped("Experimental: no game pack plays this game yet, so it may not run correctly.");
	ImGui::Dummy(ImVec2(0, em * 0.4f));
	/* the actions: a full-width button each, what it does under it */
	auto action = [&](const char *label, const char *expl, bool primary, bool enabled) {
		ImGui::BeginDisabled(!enabled);
		const bool r = primary ? ui::primary_button(label, ImVec2(-FLT_MIN, 0)) : ImGui::Button(label, ImVec2(-FLT_MIN, 0));
		ImGui::EndDisabled();
		if (expl && *expl) ui::muted_wrapped(expl);
		ImGui::Dummy(ImVec2(0, em * 0.15f));
		return r && enabled;
	};
	if (g.built) { if (action("Play", "Starts the game (its build is brought up to date first).", true, !busy)) { start_play(g); close = true; } }
	else if (g.imported) { if (action("Build", "A one-time build of the game from your copy: a few minutes. After it, Play starts it straight away.", true, !busy)) { start_build(g); close = true; } }
	else if (lf) { if (action("Add", ("From " + leaf_name(lf->source) + ", where your library folders found it.").c_str(), true, !busy)) { g_game_page.clear(); ImGui::CloseCurrentPopup(); ImGui::PopTextWrapPos(); dialog_body_end(); ImGui::EndPopup();
		if (found_ready(*lf, g_lib_found)) { int n = 0; std::vector<std::string> titles; std::vector<Step> s = found_steps(&n, &titles, g_lib_found, lf->id); task_start("Adding " + g.name, "add", g.id, s); }
		else { g_found.clear(); for (const Found &x : g_lib_found) if (x.id == lf->id) g_found.push_back(x); g_found_ready = true; g_add_kind = 0; g_scan_path = lf->source; g_add_open = true; }
		return; } }
	else { if (action("Add your copy", "A game folder from your Vita, a PKG with its licence, or a folder to search.", true, !busy)) { g_add_open = true; close = true; } }
	{	bool have, in; { std::lock_guard<std::mutex> lk(g_steam.m); have = g_steam.have; in = g_steam.in.count(g.id) > 0; }
		if (have && !self_exe().empty()) {
			const bool ok = steam_addable(g);
			if (in) { if (action("Remove from Steam", "Takes only this game's shortcut out of your Steam library. Steam needs to be closed for it.", false, !busy)) { steam_task(g, false); close = true; } }
			else if (action("Add to Steam", ok ? "A shortcut in your Steam library that starts the game directly. Steam needs to be closed for it; the game shows up the next time Steam starts."
			                                    : "Build the game first: a game can go into Steam once it is ready to play.", false, ok && !busy)) { steam_task(g, true); close = true; }
		}
	}
	if (g.imported && action("Game settings", "This game's own options (field of view, view distance, ...) and the values it keeps apart from the shared Settings.", false, true)) {
		make_dir(g_dump + "/decrypted/" + g.id + "/recomp"); g_game_settings = g.id; close = true;
	}
	if (g.built && action("Rebuild", "Builds the game again from your copy: a new file, as after an update of Nativ.", false, !busy)) { start_build(g); close = true; }
	if (g.imported && action("Show its folder", "Opens the game's folder in the library: its build, its logs.", false, true)) open_folder(g_dump + "/decrypted/" + g.id);
	if (action("Report a problem", "One zip of this game's logs, your settings and the pictures from your last F5, shown in its folder: send it with a description of what happened. No game files, saves or keys.", false, !busy)) {
		task_start("Making a bug report for " + g.name, "report", g.id, {step("Collecting the logs and settings", vitarecomp({"report", g.id}))}); close = true;
	}
	ImGui::PopTextWrapPos();
	dialog_body_end();
	if (ImGui::Button("Back") || dialog_back()) close = true;
	dialog_hints();
	if (close) { g_game_page.clear(); ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}

/* ---- the log, copied or saved whole (the activity bar's Copy log / Save log...): for a bug report or a question */
static std::mutex g_log_note_lock;
static std::string g_log_note, g_log_note_key; static Uint64 g_log_note_until;   /* "Copied" / "Saved" on its button for a moment */
static std::string g_log_to_save;                                 /* the log as it was when Save log was pressed */
static void log_note(const char *key, const std::string &n) { std::lock_guard<std::mutex> l(g_log_note_lock); g_log_note_key = key; g_log_note = n; g_log_note_until = SDL_GetTicks() + 2500; }
static std::string log_copy(void) { std::lock_guard<std::mutex> l(g_log_lock); return g_log; }
static void SDLCALL log_saved(void *, const char *const *files, int) {   /* (the dialog's callback: on its own thread) */
	if (!files || !files[0]) return;   /* (cancelled, or the dialog failed) */
	std::string path = files[0];
	size_t slash = path.find_last_of("/\\"), dot = path.rfind('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) path += ".log";
	FILE *f = fopen(path.c_str(), "wb");
	bool ok = f && fwrite(g_log_to_save.data(), 1, g_log_to_save.size(), f) == g_log_to_save.size();
	if (f && fclose(f) != 0) ok = false;
	log_note("save", ok ? "Saved" : "Not saved");
}
static void log_save(SDL_Window *win) {
	static const SDL_DialogFileFilter f[] = {{"Log file (*.log)", "log"}, {"Text file (*.txt)", "txt"}, {"All files (*.*)", "*"}};
	g_log_to_save = log_copy();
	char name[64]; time_t t = time(nullptr); struct tm tm;
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	strftime(name, sizeof name, "nativ-launcher-%Y%m%d-%H%M%S.log", &tm);
	const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
	std::string where = docs ? std::string(docs) + name : std::string(name);
	if (use_in_app_browser()) {   /* (a controller or a finger: no save dialog to answer -- into Documents, and the button says so) */
		if (g_shot_mode) { g_shot_done.push_back("save the log to " + where); return; }
		FILE *f = fopen(where.c_str(), "wb"); bool ok = f && fwrite(g_log_to_save.data(), 1, g_log_to_save.size(), f) == g_log_to_save.size();
		if (f && fclose(f) != 0) ok = false;
		log_note("save", ok ? "Saved to Documents" : "Not saved");
		return;
	}
	if (g_shot_mode) { g_shot_done.push_back("save the log"); return; }
	SDL_ShowSaveFileDialog(log_saved, nullptr, win, f, 3, where.c_str());
}
static bool log_button(const char *label, const char *id) {   /* its label, or the note for a moment after it was used */
	std::string shown = label;
	{ std::lock_guard<std::mutex> l(g_log_note_lock); if (SDL_GetTicks() < g_log_note_until && !g_log_note.empty() && g_log_note_key == id) shown = g_log_note; }
	return ImGui::Button((shown + "###" + id).c_str());
}

/* ---- the window */
static int g_style_gen;   /* (counts ui::apply calls: a new scale means a new base style) */
static float g_ui_scale = 1.0f;
static void apply_scale(float scale) {   /* the whole look at this scale, if it changed */
	if (fabsf(scale - g_ui_scale) < 0.01f && g_style_gen) return;
	g_ui_scale = scale; ui::apply(16.0f * scale, 1.0f); g_style_gen++;   /* (the font size itself, not FontScaleDpi: PushFont(nullptr, em * x) passes sizes already in em, which FontScaleDpi scaled a second time -- headings were scale^2 large; the in-game menu does it this way too) */
}
static void apply_window_scale(SDL_Window *win) {   /* the larger of the display's scale and the window height's (hub_scale.h) */
	int w = 0, h = 0; SDL_GetWindowSize(win, &w, &h);
	apply_scale(hub_ui_scale((float)h, SDL_GetWindowDisplayScale(win)));
}
static void touch_style(void) {   /* a touch screen in use: finger-sized targets, until a mouse moves again */
	ImGuiStyle &st = ImGui::GetStyle(); static ImGuiStyle base; static int seen_gen = -1;
	if (seen_gen != g_style_gen) { seen_gen = g_style_gen; base = st; g_touch = false; }   /* (a new scale: this style is the new base; touch sizes again from it) */
	const bool t = ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;   /* (the SDL backend tags touch-driven pointer events) */
	if (!t) {
		if (g_touch) { st.FramePadding = base.FramePadding; st.GrabMinSize = base.GrabMinSize; st.ScrollbarSize = base.ScrollbarSize; st.ItemSpacing = base.ItemSpacing; }
		base = st; g_touch = false; return;
	}
	if (!g_touch) {
		const float em = ImGui::GetFontSize();
		st.FramePadding = ImVec2(base.FramePadding.x * 1.4f, std::max(base.FramePadding.y, (em * 2.75f - em) * 0.5f));   /* (a frame 44 px tall at scale 1) */
		st.GrabMinSize = base.GrabMinSize * 2; st.ScrollbarSize = base.ScrollbarSize * 1.6f; st.ItemSpacing = ImVec2(base.ItemSpacing.x, base.ItemSpacing.y * 1.2f);
	}
	g_touch = true;
}
/* swipe to scroll (Dear ImGui has none: a finger could only drag the scrollbar): a touch that moves up or down more than
 * ~0.6 em scrolls the nearest scrolling area under it (the games, the settings rows, a dialog's content -- never what's
 * behind a dialog); the button it started on isn't pressed; the list carries on after the finger lifts and slows down */
static struct { bool down = false, swiping = false; ImVec2 start; ImGuiWindow *win = nullptr; float vel = 0; } g_swipe;
static ImGuiWindow *scroll_target(ImGuiWindow *w) {
	for (; w; w = w->ParentWindow) { if (w->ScrollMax.y > 0.5f) return w; if (w->Flags & ImGuiWindowFlags_Popup) break; }
	return nullptr;
}
static void touch_scroll(void) {
	ImGuiIO &io = ImGui::GetIO(); ImGuiContext &g = *GImGui; const float em = ImGui::GetFontSize();
	if (g_touch && io.MouseClicked[0]) { g_swipe.down = true; g_swipe.swiping = false; g_swipe.start = io.MousePos; g_swipe.win = scroll_target(g.HoveredWindow); g_swipe.vel = 0; }
	if (g_swipe.down && io.MouseDown[0]) {
		const float dx = io.MousePos.x - g_swipe.start.x, dy = io.MousePos.y - g_swipe.start.y;
		float move = io.MouseDelta.y;
		if (!g_swipe.swiping && g_swipe.win && fabsf(dy) > em * 0.6f && fabsf(dy) > fabsf(dx)) { g_swipe.swiping = true; move = dy; }   /* (the way so far, at once) */
		if (g_swipe.swiping) {
			ImGui::ClearActiveID();   /* (a swipe, not a press of the button it started on) */
			ImGui::SetScrollY(g_swipe.win, g_swipe.win->Scroll.y - move);
			g_swipe.vel = g_swipe.vel * 0.5f - io.MouseDelta.y * 0.5f;
		}
	} else if (g_swipe.down) { g_swipe.down = false; if (!g_swipe.swiping) g_swipe.vel = 0; }
	if (!g_swipe.down && g_swipe.win && fabsf(g_swipe.vel) > 0.5f) {
		ImGui::SetScrollY(g_swipe.win, g_swipe.win->Scroll.y + g_swipe.vel);
		g_swipe.vel *= 0.94f;
	}
}
static void hub_ui(SDL_Window *win) {
	touch_style();
	g_pad_fam = pad_family_now();
	touch_scroll();
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float em = ImGui::GetFontSize();
	ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar(2);
	const bool busy = g_task.running, narrow = vp->WorkSize.x < 44 * em;

	ImGui::PushItemFlag(ImGuiItemFlags_NoNav, g_lib_scroll > 0.5f);   /* (the header: from the top of the list) */
	/* header: the logo (the Vita, its screen lit as the app icon's, and the wordmark), the setup state, Add game, the setup gear */
	{	ImDrawList *dl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = em * 1.6f, vh = h * 0.9f, vw = vh * 600 / 280;
		ui::vita(dl, ImVec2(p.x, p.y + (h - vh) * 0.5f), vw, ui::u32(ui::text), ui::u32(ui::accent), 30, 1);
		float ww = ui::wordmark(dl, ImVec2(p.x + vw + em * 0.6f, p.y), h);
		ImGui::Dummy(ImVec2(vw + em * 0.6f + ww, h));
	}
	if (!narrow) { ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + em * 0.45f); ImGui::TextDisabled("your PS Vita games, rebuilt natively"); }
	float right = ImGui::GetWindowContentRegionMax().x;
	float bw = ImGui::CalcTextSize("+ Add game").x + ImGui::CalcTextSize("Settings").x + ImGui::CalcTextSize("Setup").x + ImGui::GetStyle().FramePadding.x * 6 + ImGui::GetStyle().ItemSpacing.x * 2;
	std::string fw = g_firmware ? "Firmware " + g_firmware_version : "Firmware missing";
	float pw = ImGui::CalcTextSize(fw.c_str()).x + em * 1.6f;
	if (!narrow) ImGui::SameLine(right - bw - pw - ImGui::GetStyle().ItemSpacing.x * 2); else ImGui::Dummy(ImVec2(0, 0));
	{	/* the firmware pill: green when installed, amber (and opens setup) when not */
		ImVec2 p = ImGui::GetCursorScreenPos(); float h = ImGui::GetFrameHeight();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + pw, p.y + h), ui::u32(g_firmware ? ui::ready : ui::warn, 0.14f), h * 0.5f);
		ImDrawList *dl = ImGui::GetWindowDrawList(); float lh = ImGui::GetTextLineHeight();
		dl->AddCircleFilled(ImVec2(p.x + em * 0.6f + lh * 0.22f, p.y + h * 0.5f), lh * 0.22f, ui::u32(g_firmware ? ui::ready : ui::warn));
		dl->AddText(ImVec2(p.x + em * 0.6f + lh * 0.8f, p.y + (h - lh) * 0.5f), ui::u32(ui::text), fw.c_str());
		if (ImGui::InvisibleButton("fwpill", ImVec2(pw, h)) && !g_firmware) { g_setup_step = 1; g_setup_open = true; }
		if (ImGui::IsItemHovered() && !g_firmware) ImGui::SetTooltip("Install the firmware");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(busy);
	if (ui::primary_button("+ Add game")) g_add_open = true;
	ImGui::EndDisabled();
	ImGui::SameLine(); if (ImGui::Button("Settings")) g_settings_open = true;
	ImGui::SameLine(); if (ImGui::Button("Setup")) { g_setup_step = 0; g_setup_open = true; }
	ImGui::Dummy(ImVec2(0, em * 0.3f));

	ImGui::PopItemFlag();
	g_focus_game.clear();   /* (the first game shown) */
	const std::vector<LibSection> sections = library_sections();
	for (const LibSection &s : sections) if (!s.folded && !s.games.empty()) { g_focus_game = s.games[0]->id; break; }
	/* the library: a grid of cards, sized to the window */
	static float bar_seen;   /* (the height of the bar as drawn the frame before: the library takes exactly the rest) */
	const float bar_h = bar_seen > 0 ? bar_seen : ImGui::GetFrameHeightWithSpacing() + em * 0.8f, drawer_h = g_drawer ? ImGui::GetContentRegionAvail().y * 0.4f : 0;
	ImGui::BeginChild("library", ImVec2(0, ImGui::GetContentRegionAvail().y - bar_h - drawer_h), ImGuiChildFlags_NavFlattened);   /* (the D-pad goes straight to the games: no "enter the list" first) */
	int nimp = 0; for (const Game &g : g_games) nimp += g.imported;
	ImGui::PushFont(nullptr, em * 1.15f); ImGui::TextUnformatted("Library"); ImGui::PopFont();
	ImGui::SameLine(); ImGui::TextDisabled("%d of %zu added", nimp, g_games.size());
	static int view = -1, size = 100;   /* the library's view (hub.ini library_view, library_size): grid 0 | list 1, percent */
	if (view < 0) {
		view = g_prefs["library_view"] == "list";
		auto it = g_prefs.find("library_size"); size = it == g_prefs.end() ? 100 : atoi(it->second.c_str());
		size = size < 60 ? 60 : size > 160 ? 160 : size;
	}
	{	/* the bar of controls: Sort by (the same again turns the order round), Size - / + (a controller and a finger can
		 * press them; a slider was slow with one and fiddly with the other), Grid / List -- a row that wraps */
		ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Sort by");
		static const char *const sorts[][2] = {{"state", "State"}, {"name", "Name"}, {"last", "Last played"}, {"time", "Time played"}, {"size", "Size"}};
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * 0.25f, ImGui::GetStyle().ItemSpacing.y));
		for (auto &s : sorts) {
			const bool on = g_lib_sort == s[0];
			std::string label = s[1]; if (on && strcmp(s[0], "state")) label += g_lib_desc ? "  (reversed)" : "";
			label += std::string("##sort") + s[0];
			same_line_fits(button_w(label.c_str()));
			ImGui::PushStyleColor(ImGuiCol_Button, on ? ui::with_alpha(ui::accent, 0.18f) : ui::card); ImGui::PushStyleColor(ImGuiCol_Text, on ? ui::accent : ui::muted);
			if (ImGui::Button(label.c_str())) {
				if (on) g_lib_desc = !g_lib_desc; else { g_lib_sort = s[0]; g_lib_desc = false; }
				if (g_settings_writable) { prefs_set("library_sort", g_lib_sort); prefs_set("library_desc", g_lib_desc ? "1" : "0"); }
			}
			ImGui::PopStyleColor(2);
		}
		ImGui::PopStyleVar();
		const float sizew = ImGui::CalcTextSize("Size").x + button_w("-") * 2 + ImGui::CalcTextSize("160%").x + ImGui::GetStyle().ItemSpacing.x * 4;
		same_line_fits(sizew + em);
		ImGui::Dummy(ImVec2(em * 0.5f, 0)); ImGui::SameLine();
		ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Size"); ImGui::SameLine();
		ImGui::BeginDisabled(size <= 60); if (ImGui::Button("-##libsize", ImVec2(std::max(ImGui::GetFrameHeight(), button_w("+")), 0))) { size -= 20; if (g_settings_writable) prefs_set("library_size", std::to_string(size)); } ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::Text("%d%%", size); ImGui::SameLine();
		ImGui::BeginDisabled(size >= 160); if (ImGui::Button("+##libsize", ImVec2(std::max(ImGui::GetFrameHeight(), button_w("+")), 0))) { size += 20; if (g_settings_writable) prefs_set("library_size", std::to_string(size)); } ImGui::EndDisabled();
		size = size < 60 ? 60 : size > 160 ? 160 : size;
		const char *const views[] = {"Grid", "List"};
		same_line_fits(button_w("Grid") + button_w("List") + em);
		ImGui::Dummy(ImVec2(em * 0.5f, 0)); ImGui::SameLine();
		int v = view;
		if (ui::segmented(views, 2, &v) && v != view) { view = v; if (g_settings_writable) prefs_set("library_view", view ? "list" : "grid"); }
	}
	const float toolbar_end = ImGui::GetCursorScreenPos().y;   /* (below the Sort / Size bar) */
	const float zoom = size / 100.0f;
	if (!g_firmware) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ui::warn.x, ui::warn.y, ui::warn.z, 0.10f));
		ImGui::BeginChild("fwnote", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
		ui::dot(ui::warn); ImGui::TextWrapped("The firmware isn't installed yet. Games build against Sony's system software — it’s a free download from Sony.");
		if (ui::primary_button("Set up the firmware")) { g_setup_step = 1; g_setup_open = true; }
		ImGui::EndChild(); ImGui::PopStyleColor();
	}
	{	int bad = 0; std::string first; { std::lock_guard<std::mutex> l(g_files.m); if (!g_files.dismissed) { bad = g_files.bad; first = g_files.first; } }
		if (bad) {   /* the install is missing files (files_check_start) */
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ui::warn.x, ui::warn.y, ui::warn.z, 0.10f));
			ImGui::BeginChild("filesnote", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
			ui::dot(ui::warn); ImGui::TextWrapped("Project Nativ is missing %d of its files (the first: %s), so adding games and building them will fail. Unpack the whole zip again into an empty folder, and wait until it has finished. If files go missing again, your antivirus may be removing them.", bad, first.c_str());
			if (ImGui::Button("Not now##files")) { std::lock_guard<std::mutex> l(g_files.m); g_files.dismissed = true; }
			ImGui::EndChild(); ImGui::PopStyleColor();
		}
	}
	{	std::string tag, page; { std::lock_guard<std::mutex> l(g_update.m); if (!g_update.dismissed) { tag = g_update.tag; page = g_update.page; } }
		if (!tag.empty()) {   /* a newer release: its page, where the download is */
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ui::accent.x, ui::accent.y, ui::accent.z, 0.10f));
			ImGui::BeginChild("updnote", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
#ifdef __APPLE__
			ui::dot(ui::accent); ImGui::TextWrapped("Project Nativ %s is out. Download it and drag it into Applications, replacing this one: your games, saves and settings stay as they are.", tag.c_str());
#else
			ui::dot(ui::accent); ImGui::TextWrapped("Project Nativ %s is out. Download it and replace this folder with it: your games, saves and settings stay as they are.", tag.c_str());
#endif
			if (ui::primary_button("Download")) open_url(page);
			ImGui::SameLine(); if (ImGui::Button("Not now")) { std::lock_guard<std::mutex> l(g_update.m); g_update.dismissed = true; }
			ImGui::EndChild(); ImGui::PopStyleColor();
		}
	}
	if (g_games.empty()) {
		ImGui::Dummy(ImVec2(0, em * 2));
		ImGui::TextDisabled("No games yet."); ImGui::TextWrapped("Add your own copy of a game — a PKG with its licence file, or a game folder — with + Add game.");
	}
	/* every card the same text heights (all games, so sections line up too) */
	const float gap = em * 1.0f, min_w = em * 15.5f * zoom, avail_w = ImGui::GetContentRegionAvail().x;
	int cols = (int)((avail_w + gap) / (min_w + gap)); if (cols < 1) cols = 1;
	float cw = (avail_w - gap * (cols - 1)) / cols; if (cw > em * 24 * zoom) cw = em * 24 * zoom;   /* (a wide window gets more columns, not giant cards) */
	float name_h = 0, id_h = 0;
	ImGui::PushFont(nullptr, em * 1.15f);
	for (const Game &g : g_games) { float hh = ImGui::CalcTextSize(g.name.c_str(), nullptr, false, cw - em * 1.8f).y; if (hh > name_h) name_h = hh; }
	ImGui::PopFont();
	for (const Game &g : g_games) id_h = std::max(id_h, id_line_height(g, cw - em * 1.8f));
	const bool narrow_list = avail_w < em * 36;
	g_list_stats = avail_w >= em * 58;   /* (the three columns only where they have room; else the stacked rows show them) */
	int si = 0;
	for (const LibSection &sec : sections) {
		ImGui::PushID(si++);
		if (sec.title) {   /* the section's heading and its count */
			ImGui::Dummy(ImVec2(0, em * 0.3f));
			ImGui::TextUnformatted(sec.title); ImGui::SameLine(); ImGui::TextDisabled("%zu", sec.games.size());
		}
		if (sec.folded) {
			char lb[64]; snprintf(lb, sizeof lb, "Show the %zu supported games", sec.games.size());
			if (ImGui::Button(lb)) { g_lib_show_all = true; if (g_settings_writable) prefs_set("library_show_all", "1"); }
			ImGui::PopID(); continue;
		}
		if (view == 1 && narrow_list) {   /* the list in a narrow window: each game's lines stacked, nothing squeezed into columns */
			ImGui::Dummy(ImVec2(0, em * 0.2f));
			for (const Game *g : sec.games) game_row_stacked(*g, em * 2.6f * zoom);
		} else if (view == 1) {   /* the list: one row a game, its icon at the chosen size */
			ImGui::Dummy(ImVec2(0, em * 0.2f));
			if (ImGui::BeginTable("gamelist", g_list_stats ? 6 : 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
				ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 2.0f);
				if (g_list_stats) {
					ImGui::TableSetupColumn("Last played", ImGuiTableColumnFlags_WidthStretch, 1.3f);
					ImGui::TableSetupColumn("Time played", ImGuiTableColumnFlags_WidthStretch, 1.3f);
					ImGui::TableSetupColumn("Size on disk", ImGuiTableColumnFlags_WidthStretch, 1.5f);
				}
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, em * 11.5f);
				if (g_list_stats) {   /* (the columns' names, the actions' unnamed) */
					ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
					for (int c = 0; c < 5; c++) { ImGui::TableSetColumnIndex(c); ImGui::TextDisabled("%s", ImGui::TableGetColumnName(c)); }
				}
				for (const Game *g : sec.games) game_row(*g, em * 2.6f * zoom);
				ImGui::EndTable();
			}
		} else {   /* the grid: cards at the chosen size, as many columns as fit */
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
			for (size_t i = 0; i < sec.games.size(); i++) {
				if (i % cols) ImGui::SameLine();
				ImGui::BeginGroup(); game_card(*sec.games[i], cw, name_h, id_h); ImGui::EndGroup();
			}
			ImGui::PopStyleVar();
		}
		ImGui::PopID();
	}
	{	/* the focus on the Sort / Size bar: the list all the way up (nav scrolls just enough to show the bar; the header is
		 * reached from the very top) */
		ImGuiWindow *lw = ImGui::GetCurrentWindow(); ImGuiContext &g = *GImGui;
		if (g.NavWindow == lw && g.NavId && ImGui::GetScrollY() > 0 && ImGui::WindowRectRelToAbs(lw, lw->NavRectRel[0]).Min.y < toolbar_end) ImGui::SetScrollY(0);
	}
	g_lib_scroll = ImGui::GetScrollY(); g_lib_scroll_max = ImGui::GetScrollMaxY();
	ImGui::EndChild();

	/* the activity bar: what's running (its step), the last failure, the log on demand */
	const float bar_y0 = ImGui::GetCursorPosY();
	ImGui::Separator();
	ImGui::PushItemFlag(ImGuiItemFlags_NoNav, g_lib_scroll < g_lib_scroll_max - 0.5f);   /* (the bar: from the end of the list) */
	ImGui::Dummy(ImVec2(0, em * 0.1f));
	if (busy || !g_failed.empty()) task_line(); else { ui::dot(ui::border); ImGui::TextDisabled("Nothing running"); }
	{
		const ImGuiStyle &sty = ImGui::GetStyle();
		float lw = ImGui::CalcTextSize(g_drawer ? "Hide log" : "Show log").x + sty.FramePadding.x * 2, cw2 = busy ? ImGui::CalcTextSize("Cancel").x + sty.FramePadding.x * 2 + sty.ItemSpacing.x : 0;
		const float cpw = ImGui::CalcTextSize("Copy log").x + ImGui::CalcTextSize("Save log...").x + sty.FramePadding.x * 4 + sty.ItemSpacing.x * 2;
		const float right = ImGui::GetWindowContentRegionMax().x - lw - cw2 - cpw;
		if (hints_visible()) {   /* (the library's hint: A on a game's button; it has no Back) */
			ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
			if (ImGui::GetCursorPosX() + hints_width(false) + sty.ItemSpacing.x * 2 <= right) { ImGui::AlignTextToFramePadding(); hints_draw(false); }
			else ImGui::Dummy(ImVec2(0, 0));
		}
		ImGui::SameLine(right);
	}
	{	bool empty; { std::lock_guard<std::mutex> l(g_log_lock); empty = g_log.empty(); }
		ImGui::BeginDisabled(empty);
		if (log_button("Copy log", "copy")) { SDL_SetClipboardText(log_copy().c_str()); log_note("copy", "Copied"); }
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(empty ? "Nothing in the log yet" : "Copies the whole launcher log, to paste into a bug report or a message");
		ImGui::SameLine();
		if (log_button("Save log...", "save")) log_save(win);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(empty ? "Nothing in the log yet" : "Saves the whole launcher log as a file");
		ImGui::EndDisabled();
		ImGui::SameLine();
	}
	if (busy) { if (ImGui::Button("Cancel")) task_cancel(); ImGui::SameLine(); }
	if (ImGui::Button(g_drawer ? "Hide log" : "Show log")) g_drawer = !g_drawer;
	ImGui::PopItemFlag();
	bar_seen = ImGui::GetCursorPosY() - bar_y0;
	if (g_drawer) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::surface);
		ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
		{ std::lock_guard<std::mutex> l(g_log_lock); ImGui::PushStyleColor(ImGuiCol_Text, ui::muted); ImGui::TextUnformatted(g_log.c_str(), g_log.c_str() + g_log.size()); ImGui::PopStyleColor(); }
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);   /* follow the end unless scrolled up */
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	/* Add a game, as its own dialog */
	if (g_add_open && !ImGui::IsPopupOpen("Add a game")) ImGui::OpenPopup("Add a game");
	ImGui::SetNextWindowSize(ImVec2(narrow ? vp->WorkSize.x * 0.94f : 38 * em, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.6f * em, 1.3f * em)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::BeginPopupModal("Add a game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
		ImGui::PopStyleVar(2);
		ui::heading("Add a game", em * 1.4f);
		dialog_body_begin();
		ImGui::PushTextWrapPos(0);
		const bool started = add_form(win);
		ImGui::Dummy(ImVec2(0, em * 0.3f)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, em * 0.1f)); licences_row(win);
		ImGui::PopTextWrapPos();
		dialog_body_end();
		if (started || ImGui::Button("Cancel") || dialog_back()) { g_add_open = false; ImGui::CloseCurrentPopup(); }
		dialog_hints();
		child_dialogs();   /* (Browse..., licences From a folder..., the paths, the Vita's address) */
		ImGui::EndPopup();
	} else ImGui::PopStyleVar(2);
	sac_modal();
	steam_closed_modal();
	online_name_modal();
	setup_modal(win);
	settings_modal();
	game_settings_modal();
	game_page_modal();
	if (g_browse.top) browser_modal();   /* (one opened with no dialog around it) */
	if (g_osk.top) osk_modal();
	{	/* (a press of B this frame that ImGui used to end an edit or close a menu: no dialog takes it next frame either) */
		ImGuiContext &g = *GImGui; bool menu = false;
		for (const ImGuiPopupData &p : g.OpenPopupStack) menu |= p.Window && !(p.Window->Flags & ImGuiWindowFlags_Modal);
		g_ui_busy_prev = ImGui::IsAnyItemActive() || menu;
	}
	ImGui::End();
}

/* ---- ATIV_SHOT_FIT=<file>: the fit check (UI-01b). In a screenshot run's last frame every item is recorded through Dear
 * ImGui's item hooks (IMGUI_ENABLE_TEST_ENGINE: they cost one flag test per item when off); after the frame each is checked
 * against what its window shows. Reported, one line each: an item past its window's left or right edge, or above / below
 * it with no scrolling parent to bring it into view; a framed item (a button, a field) whose label is wider than its box; a
 * window larger than the screen; a window's content wider than the window; text drawn across a clip edge (a cut line, a
 * table cell's text cut by its column). Nothing on screen may be cut off at any size: the report's lines are the bugs. */
#ifdef IMGUI_ENABLE_TEST_ENGINE
static bool g_fit_on;
struct FitItem { ImGuiID id; ImRect bb; ImGuiWindow *w; };
static std::vector<FitItem> g_fit_items;
static std::vector<std::string> g_fit;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *) { if (g_fit_on) g_fit_items.push_back({id, bb, ctx->CurrentWindow}); }
void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID) { return nullptr; }
static std::string fit_name(const ImGuiWindow *w) {   /* "##hub/library_1A2B3C4D/card_..." -> "hub/library/card" */
	std::string s, n = w ? w->Name : "?";
	for (size_t i = 0; i < n.size(); ) {
		if (n[i] == '#') { i++; continue; }
		if (n[i] == '_' && i + 9 <= n.size() && std::all_of(n.begin() + i + 1, n.begin() + i + 9, [](char c) { return isxdigit((unsigned char)c) != 0; })) { i += 9; continue; }
		s += n[i++];
	}
	return s;
}
static void fit_add(const std::string &line) { if (std::find(g_fit.begin(), g_fit.end(), line) == g_fit.end()) g_fit.push_back(line); }
static bool fit_scrolls_y(const ImGuiWindow *w) { for (; w; w = w->ParentWindow) if (w->ScrollMax.y > 0.5f) return true; return false; }
static bool g_nav_on;                          /* (ATIV_SHOT_KEYS / ATIV_SHOT_NAV: the items' labels, to name the one with the focus) */
static std::map<ImGuiID, std::string> g_nav_labels;
void ImGuiTestEngineHook_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags) {
	if ((g_nav_on || g_fit_on) && label) g_nav_labels[id] = label;
	if (!g_fit_on || !label || g_fit_items.empty() || g_fit_items.back().id != id) return;
	const char *end = strstr(label, "##"); std::string vis(label, end ? (size_t)(end - label) : strlen(label));
	if (vis.empty() || vis.find('/') != std::string::npos) return;   /* (a child window's name is no label) */
	const FitItem &it = g_fit_items.back();
	const ImGuiStyle &st = ctx->Style;
	const float tw = ImGui::CalcTextSize(vis.c_str()).x, bw = it.bb.GetWidth();
	const bool framed = it.bb.GetHeight() > ImGui::GetTextLineHeight() + 1.0f && !(it.w->Flags & ImGuiWindowFlags_Popup && !(it.w->Flags & ImGuiWindowFlags_Modal));   /* (a button, a field: its label sits inside the frame's padding; a menu's entry has none) */
	if (tw > (framed ? bw - 2 * st.FramePadding.x : bw) + 1.0f)
		fit_add("label-cut " + fit_name(it.w) + " \"" + vis + "\" text " + std::to_string((int)tw) + " box " + std::to_string((int)bw));
}
static void fit_check(void) {
	ImGuiContext &g = *GImGui;
	const ImRect screen(ImVec2(0, 0), g.IO.DisplaySize);
	for (const FitItem &it : g_fit_items) {
		/* (ImGui's own parts of a window -- the window itself, its scrollbars, resize borders and corners -- sit on its edge
		 * by design; ImGui's fallback "Debug" window is never shown) */
		ImGuiWindow *w = it.w;
		if (w->IsFallbackWindow || it.id == w->ID || it.id == ImGui::GetWindowScrollbarID(w, ImGuiAxis_X) || it.id == ImGui::GetWindowScrollbarID(w, ImGuiAxis_Y)) continue;
		bool own = false;
		for (int n = 0; n < 4 && !own; n++) own = it.id == ImGui::GetWindowResizeCornerID(w, n) || it.id == ImGui::GetWindowResizeBorderID(w, (ImGuiDir)n);
		if (own) continue;
		/* a finger needs a target about 44 px tall (x the UI's scale): in touch mode, every tappable item (UI-01d) */
		if (g.IO.MouseSource == ImGuiMouseSource_TouchScreen && !(w->Flags & ImGuiWindowFlags_ChildWindow && it.id == w->ChildId) && it.bb.GetHeight() < ImGui::GetFontSize() * 2.75f - 0.5f) {
			auto lb = g_nav_labels.find(it.id);
			fit_add("target-small " + fit_name(w) + " \"" + (lb != g_nav_labels.end() ? lb->second : std::string("?")) + "\" h " + std::to_string((int)it.bb.GetHeight()));
		}
		/* (a menu's entries span the menu's padding by design: in a menu or popup the edge is the window's own) */
		const ImRect c = (w->Flags & ImGuiWindowFlags_Popup) && !(w->Flags & ImGuiWindowFlags_Modal) ? w->Rect() : w->InnerClipRect; const float e = 1.5f;
		const bool out_y = it.bb.Max.y > c.Max.y + e || it.bb.Min.y < c.Min.y - e;
		if (out_y && fit_scrolls_y(it.w)) continue;   /* (scrolled out of view: scrolling brings it back) */
		if ((w->Flags & ImGuiWindowFlags_Popup) && !(w->Flags & ImGuiWindowFlags_Modal)) continue;   /* (a menu's entries span its padding and edges by design; their labels are checked as labels) */
		if (it.bb.Max.x > c.Max.x + e || it.bb.Min.x < c.Min.x - e)
			fit_add("item-past-edge " + fit_name(it.w) + " x " + std::to_string((int)it.bb.Min.x) + ".." + std::to_string((int)it.bb.Max.x) + " visible " + std::to_string((int)c.Min.x) + ".." + std::to_string((int)c.Max.x));
		else if (out_y)
			fit_add("item-off-screen " + fit_name(it.w) + " y " + std::to_string((int)it.bb.Min.y) + ".." + std::to_string((int)it.bb.Max.y) + " visible " + std::to_string((int)c.Min.y) + ".." + std::to_string((int)c.Max.y));
	}
	ImFontAtlas *atlas = g.IO.Fonts; const ImVec2 white = atlas->TexUvWhitePixel;
	for (ImGuiWindow *w : g.Windows) {
		if (!w->WasActive || w->Hidden || w->IsFallbackWindow) continue;
		if (!(w->Flags & ImGuiWindowFlags_ChildWindow) && (w->Rect().Max.x > screen.Max.x + 1 || w->Rect().Max.y > screen.Max.y + 1 || w->Pos.x < -1 || w->Pos.y < -1))
			fit_add("window-larger-than-screen " + fit_name(w) + " " + std::to_string((int)w->Size.x) + "x" + std::to_string((int)w->Size.y));
		/* content reaching past what the window shows (not merely into its padding) */
		if (!w->ScrollbarX && w->DC.CursorStartPos.x + w->ContentSize.x > w->InnerClipRect.Max.x + 1.5f)
			fit_add("content-wider " + fit_name(w) + " content to " + std::to_string((int)(w->DC.CursorStartPos.x + w->ContentSize.x)) + " visible to " + std::to_string((int)w->InnerClipRect.Max.x));
		/* text drawn across its clip rectangle: a cut line, a cell's text cut by its column. Only glyphs count (the font
		 * atlas's texture, not its white pixel): panels, covers and frames drawn edge to edge are decoration, not content */
		const ImDrawList *dl = w->DrawList;
		for (const ImDrawCmd &cmd : dl->CmdBuffer) {
			if (cmd.UserCallback || !cmd.ElemCount || cmd.TexRef._TexData != atlas->TexData) continue;
			bool cut_x = false, cut_y = false; ImVec2 at(0, 0);
			for (unsigned i = cmd.IdxOffset; i < cmd.IdxOffset + cmd.ElemCount && i < (unsigned)dl->IdxBuffer.Size; i++) {
				const ImDrawVert &v = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[i]];
				if (fabsf(v.uv.x - white.x) < 1e-6f && fabsf(v.uv.y - white.y) < 1e-6f) continue;
				{	/* (nor the atlas's baked lines: thick strokes -- the focus frame -- are drawn with them, not a glyph) */
					bool line = false;
					for (int lw = 0; lw <= IM_DRAWLIST_TEX_LINES_WIDTH_MAX && !line; lw++) { const ImVec4 &u = atlas->TexUvLines[lw]; line = v.uv.x >= std::min(u.x, u.z) - 1e-6f && v.uv.x <= std::max(u.x, u.z) + 1e-6f && v.uv.y >= std::min(u.y, u.w) - 1e-6f && v.uv.y <= std::max(u.y, u.w) + 1e-6f; }
					if (line) continue;
				}
				const ImVec2 p = v.pos;
				const bool x = p.x > cmd.ClipRect.z + 1.5f || p.x < cmd.ClipRect.x - 1.5f;
				if (x && !cut_x) at = p;
				cut_x |= x;
				cut_y |= p.y > cmd.ClipRect.w + 1.5f || p.y < cmd.ClipRect.y - 1.5f;
			}
			if (cut_x) fit_add("drawn-across-edge " + fit_name(w) + " clip x " + std::to_string((int)cmd.ClipRect.x) + ".." + std::to_string((int)cmd.ClipRect.z) + " glyph at " + std::to_string((int)at.x) + "," + std::to_string((int)at.y));
			if (cut_y && !fit_scrolls_y(w)) fit_add("drawn-below-edge " + fit_name(w) + " clip y " + std::to_string((int)cmd.ClipRect.y) + ".." + std::to_string((int)cmd.ClipRect.w));
		}
	}
}
#endif

/* ---- ATIV_SHOT: one frame into a file, no visible window */
static int shot(SDL_Window *win, const char *path, int w, int h) {
	SDL_GPUTextureCreateInfo ci = {};
	ci.type = SDL_GPU_TEXTURETYPE_2D; ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ci.width = (Uint32)w; ci.height = (Uint32)h; ci.layer_count_or_depth = 1; ci.num_levels = 1;
	SDL_GPUTexture *t = SDL_CreateGPUTexture(g_gpu, &ci);
	SDL_GPUTransferBufferCreateInfo dc = {}; dc.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; dc.size = (Uint32)(w * h * 4);
	SDL_GPUTransferBuffer *down = SDL_CreateGPUTransferBuffer(g_gpu, &dc);
	if (!t || !down) { fprintf(stderr, "ATIV_SHOT: out of GPU memory\n"); return 1; }
	/* ATIV_SHOT_KEYS=down,right*6,a,b: controller buttons (up down left right a b x y l1 r1 start; esc is the Escape key),
	 * each pressed for a frame once the screen has settled, then a few frames to act; ATIV_SHOT_NAV=<file>: what came of
	 * them -- the item with the focus, the dialogs and menus open, and what a button would have done (g_shot_done) */
	/* touch: tap:x:y, swipe:x1:y1:x2:y2 (pixels; a touch-screen pointer, the swipe over 8 frames); ATIV_SHOT_TOUCH=1: the
	 * whole run in touch mode (the touch screen the last pointer used: finger-sized targets) */
	struct Press { int kind; ImGuiKey key; float x1, y1, x2, y2; };   /* (kind: 0 a button, 1 a tap, 2 a swipe) */
	std::vector<Press> keys;
	if (const char *ks = getenv("ATIV_SHOT_KEYS")) {
		static const std::pair<const char *, ImGuiKey> names[] = {{"up", ImGuiKey_GamepadDpadUp}, {"down", ImGuiKey_GamepadDpadDown}, {"left", ImGuiKey_GamepadDpadLeft},
			{"right", ImGuiKey_GamepadDpadRight}, {"a", ImGuiKey_GamepadFaceDown}, {"b", ImGuiKey_GamepadFaceRight}, {"x", ImGuiKey_GamepadFaceLeft}, {"y", ImGuiKey_GamepadFaceUp},
			{"l1", ImGuiKey_GamepadL1}, {"r1", ImGuiKey_GamepadR1}, {"start", ImGuiKey_GamepadStart}, {"esc", ImGuiKey_Escape},
			{"kup", ImGuiKey_UpArrow}, {"kdown", ImGuiKey_DownArrow}, {"kleft", ImGuiKey_LeftArrow}, {"kright", ImGuiKey_RightArrow}, {"enter", ImGuiKey_Enter}};   /* (k...: the keyboard's arrows) */
		std::string s = ks;
		for (size_t a = 0; a <= s.size(); ) {
			size_t e = s.find(',', a); if (e == std::string::npos) e = s.size();
			std::string tok = s.substr(a, e - a); int n = 1; size_t st = tok.find('*');
			if (st != std::string::npos) { n = atoi(tok.c_str() + st + 1); tok = tok.substr(0, st); }
			bool known = false; Press pr = {0, ImGuiKey_None, 0, 0, 0, 0};
			if (sscanf(tok.c_str(), "swipe:%f:%f:%f:%f", &pr.x1, &pr.y1, &pr.x2, &pr.y2) == 4) { pr.kind = 2; known = true; }
			else if (sscanf(tok.c_str(), "tap:%f:%f", &pr.x1, &pr.y1) == 2) { pr.kind = 1; known = true; }
			else for (auto &kv : names) if (tok == kv.first) { pr.key = kv.second; known = true; }
			if (known) for (int i = 0; i < n; i++) keys.push_back(pr);
			if (!known && !tok.empty()) { fprintf(stderr, "ATIV_SHOT_KEYS: no button \"%s\"\n", tok.c_str()); return 1; }
			a = e + 1;
		}
	}
	const bool touch_run = getenv("ATIV_SHOT_TOUCH") && atoi(getenv("ATIV_SHOT_TOUCH"));
	bool any_swipe = false; for (const Press &pr : keys) any_swipe |= pr.kind == 2;
	const int settle = 40, per_key = 12, frames = settle + (int)keys.size() * per_key + (keys.empty() ? 0 : any_swipe ? 60 : 4);
	const char *nav_path = getenv("ATIV_SHOT_NAV");
	for (int f = 0; f < frames; f++) {   /* ImGui sizes auto-sized windows on the first frames; a modal's dim fades in */
		ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)w, (float)h); io.DisplayFramebufferScale = ImVec2(1, 1); io.DeltaTime = 1.0f / 60;
		if (touch_run) {   /* (every frame: the SDL backend may report the pointer as a mouse again; a finger rests where it last was) */
			static ImVec2 last(-1, -1);   /* (a real position off the UI: ImGui takes the source only from a valid one) */
			io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen); io.AddMousePosEvent(last.x, last.y);
			last = io.MousePos.x > -FLT_MAX ? io.MousePos : last;
		}
		if (!keys.empty()) {   /* (a controller "connected": the SDL backend clears this when it finds none) */
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
			const int k = (f - settle) / per_key, at = (f - settle) % per_key;
			if (f >= settle && k < (int)keys.size()) {
				const Press &pr = keys[k];
				if (pr.kind == 0) { if (at < 2) io.AddKeyEvent(pr.key, at == 0); }
				else if (pr.kind == 1 || at <= 9) {
					const float t = pr.kind == 2 ? std::min(at, 8) / 8.0f : 0;
					io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
					io.AddMousePosEvent(pr.x1 + (pr.x2 - pr.x1) * t, pr.y1 + (pr.y2 - pr.y1) * t);
					if (at == 0) { io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen); io.AddMouseButtonEvent(0, true); }
					if ((pr.kind == 1 && at == 2) || (pr.kind == 2 && at == 9)) { io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen); io.AddMouseButtonEvent(0, false); }
				}
			}
		}
#ifdef IMGUI_ENABLE_TEST_ENGINE
		const char *fit_path = getenv("ATIV_SHOT_FIT");
		g_fit_on = fit_path && *fit_path && f == frames - 1;
		g_nav_on = nav_path && *nav_path;
		GImGui->TestEngineHookItems = g_fit_on || g_nav_on;
#endif
		ImGui::NewFrame(); hub_ui(win); ImGui::Render();
#ifdef IMGUI_ENABLE_TEST_ENGINE
		if (g_fit_on) {
			fit_check(); g_fit_on = false; GImGui->TestEngineHookItems = false;
			if (FILE *fr = fopen(fit_path, "wb")) { for (const std::string &l : g_fit) fprintf(fr, "%s\n", l.c_str()); fclose(fr); }
			fprintf(stderr, "ATIV_SHOT_FIT: %zu\n", g_fit.size());
		}
		if (g_nav_on && f == frames - 1) {
			ImGuiContext &g = *GImGui;
			std::string focus = "focus ", open = "open", rect;
			if (g.NavWindow && g.NavId) {
				const ImRect r = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[0]);   /* (where it is on screen: a test taps it) */
				rect = "focusrect " + std::to_string((int)r.Min.x) + " " + std::to_string((int)r.Min.y) + " " + std::to_string((int)r.Max.x) + " " + std::to_string((int)r.Max.y);
				auto it = g_nav_labels.find(g.NavId);
				focus += fit_name(g.NavWindow) + " | " + (it != g_nav_labels.end() ? it->second : std::string("?"));
				auto gm = g_nav_game.find(g.NavId); if (gm != g_nav_game.end()) focus += " | " + gm->second;
			} else focus += "none";
			for (const ImGuiPopupData &p : g.OpenPopupStack) open += " " + (p.Window ? fit_name(p.Window) : std::string("?"));
			if (g.OpenPopupStack.empty()) open += " none";
			if (FILE *fn = fopen(nav_path, "wb")) {
				fprintf(fn, "%s\n%s\n", focus.c_str(), open.c_str());
				if (!rect.empty()) fprintf(fn, "%s\n", rect.c_str());
				fprintf(fn, "pointer %d %d %s over %s\n", (int)io.MousePos.x, (int)io.MousePos.y, io.MouseSource == ImGuiMouseSource_TouchScreen ? "touch" : "mouse", g.HoveredWindow ? fit_name(g.HoveredWindow).c_str() : "-");
				for (const std::string &d : g_shot_done) fprintf(fn, "did %s\n", d.c_str());
				if (g_browse.open) fprintf(fn, "browse %s (%zu)\n", g_browse.dir.empty() ? "places" : g_browse.dir.c_str(), g_browse.items.size());
				for (ImGuiWindow *sw : g.Windows) if (sw->WasActive && !sw->Hidden && sw->ScrollMax.y > 0.5f) fprintf(fn, "scroll %s %d/%d\n", fit_name(sw).c_str(), (int)sw->Scroll.y, (int)sw->ScrollMax.y);
				fclose(fn);
			}
			fprintf(stderr, "ATIV_SHOT_NAV: %s; %s\n", focus.c_str(), open.c_str());
		}
#endif
		ImDrawData *dd = ImGui::GetDrawData();
		SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
		ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
		SDL_GPUColorTargetInfo ti = {}; ti.texture = t; ti.clear_color = SDL_FColor{ui::bg.x, ui::bg.y, ui::bg.z, 1}; ti.load_op = SDL_GPU_LOADOP_CLEAR; ti.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ti, 1, nullptr);
		ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp);
		SDL_EndGPURenderPass(rp);
		if (f == frames - 1) {
			SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
			SDL_GPUTextureRegion src = {}; src.texture = t; src.w = (Uint32)w; src.h = (Uint32)h; src.d = 1;
			SDL_GPUTextureTransferInfo dst = {}; dst.transfer_buffer = down; dst.pixels_per_row = (Uint32)w; dst.rows_per_layer = (Uint32)h;
			SDL_DownloadFromGPUTexture(cp, &src, &dst);
			SDL_EndGPUCopyPass(cp);
		}
		SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
		SDL_WaitForGPUFences(g_gpu, true, &fence, 1); SDL_ReleaseGPUFence(g_gpu, fence);
	}
	const uint8_t *px = (const uint8_t *)SDL_MapGPUTransferBuffer(g_gpu, down, false);
	FILE *f = fopen(path, "wb");
	if (!f || !px) { fprintf(stderr, "ATIV_SHOT: can't write %s\n", path); return 1; }
	fprintf(f, "P6 %d %d 255\n", w, h);
	for (int i = 0; i < w * h; i++) fwrite(px + 4 * i, 1, 3, f);
	fclose(f); SDL_UnmapGPUTransferBuffer(g_gpu, down);
	fprintf(stderr, "ATIV_SHOT: %s\n", path);
	return 0;
}

/* the repository (or an install of it): ATIV_ROOT, else the one the launcher sits in -- launcher/nativ, or beside it
 * at the top -- wherever it was moved, else where it was built */
static std::string find_root(void) {
	if (const char *r = getenv("ATIV_ROOT")) if (*r) return r;
	if (const char *b = SDL_GetBasePath())
		for (const char *up : {"..", "."}) {
			std::string r = std::string(b) + up;
#ifdef _WIN32
			char full[MAX_PATH * 4]; if (_fullpath(full, r.c_str(), sizeof full)) r = full;
#else
			if (char *full = realpath(r.c_str(), nullptr)) { r = full; free(full); }
#endif
			while (r.size() > 1 && (r.back() == '/' || r.back() == '\\')) r.pop_back();
			if (exists(r + "/pipeline/vitarecomp.py")) return r;
		}
	return REPO_ROOT;
}

/* the player's library (pipeline/datadir.py makes the same choice; the stages get this one as ATIV_DUMP): ATIV_DUMP, else
 * <root>/dump when it exists (a checkout, an install from before), else the platform's per-user data folder -- local, not
 * roaming, on Windows: a game is gigabytes -- so an update replaces the install without touching what the player made */
static std::string library_dir(void) {
	if (const char *d = getenv("ATIV_DUMP")) if (*d) return d;
	if (exists(g_root + "/dump")) return g_root + "/dump";
	const char *home = getenv("HOME"); std::string base;
#ifdef _WIN32
	const char *la = getenv("LOCALAPPDATA"), *up = getenv("USERPROFILE");
	base = la && *la ? la : std::string(up ? up : ".") + "/AppData/Local";
#elif defined(__APPLE__)
	base = std::string(home ? home : ".") + "/Library/Application Support";
#else
	const char *xdg = getenv("XDG_DATA_HOME");
	base = xdg && *xdg ? xdg : std::string(home ? home : ".") + "/.local/share";
#endif
	(void)home;
	return base + "/ProjectNativ/Nativ/library";
}

static int task_cli(const std::string &t) {   /* ATIV_TASK */
	size_t c = t.find(':'); std::string kind = t.substr(0, c), arg = c == std::string::npos ? "" : t.substr(c + 1);
	library_scan();
	const Game *g = nullptr; for (const Game &x : g_games) if (x.id == arg) g = &x;
	if ((kind == "play" || kind == "build") && !g) { fprintf(stderr, "ATIV_TASK: no game %s in the library\n", arg.c_str()); return 2; }
	if (kind == "play") start_play(*g);
	else if (kind == "build") start_build(*g);
	else if (kind == "scan") task_start("Searching a folder", "scan", "", {step("Searching", vitarecomp({"scan", arg, "--out", scan_file()}))});
	else if (kind == "licences") task_start("Importing licences", "licences", "", {step("", vitarecomp({"licenses", "import", arg}))});
	else if (kind == "firmware") task_start("Installing the firmware", "firmware", "", {step("Decrypting the system update", vitarecomp({"firmware", arg})), step("Installed")});
	else { fprintf(stderr, "ATIV_TASK: play|build|scan|licences|firmware\n"); return 2; }
	const char *cancel = getenv("ATIV_TASK_CANCEL");
	Uint64 t0 = SDL_GetTicks();
	while (g_task.running) {
		SDL_Delay(50);
		if (cancel && SDL_GetTicks() - t0 > (Uint64)(atof(cancel) * 1000)) { task_cancel(); cancel = nullptr; }
	}
	g_task.reader.join();
#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);   /* (the log as the steps wrote it: no second \r before each \n) */
#endif
	fwrite(g_log.data(), 1, g_log.size(), stdout);
	if (kind == "scan" && g_task.ok) { scan_load(scan_file()); printf("(%zu found)\n", g_found.size()); }
	return g_task.ok ? 0 : 1;
}

static int play_cli(const std::string &id) {   /* nativ --play <title>: the game as Play starts it, no launcher window */
	library_scan();
	const Game *g = nullptr; for (const Game &x : g_games) if (x.id == id) g = &x;
	std::string err;
	if (!g || !g->imported) err = id + " isn't in your Project Nativ library. Open Project Nativ and add the game again (or remove its shortcut from Steam there).";
	else {
		Step r = step("Checking " + g->name + "'s build is up to date", vitarecomp({"ready", g->id}));
		Step s = step("Running " + g->name, vitarecomp({"run", g->id}));
		const std::string name = g->name;
		task_start("Playing " + name + " (from a shortcut)", "play", id, {r, s});
		Uint64 run_t0 = 0;
		while (g_task.running) {
			SDL_Delay(100);
			if (!run_t0) { std::lock_guard<std::mutex> l(g_log_lock); if (g_step == s.say) run_t0 = SDL_GetTicks(); }
		}
		g_task.reader.join();
		if (run_t0) { const long long secs = (long long)((SDL_GetTicks() - run_t0) / 1000); playtime_record(id, (long long)time(nullptr) - secs, secs); }
		/* (a game that played a while and ended with an error code is not reported: only one that didn't start) */
		if (!g_task.ok && (!run_t0 || SDL_GetTicks() - run_t0 < 15000))
			err = name + " couldn't start. Open Project Nativ and press Play on its card to see what happened (Show log).";
	}
	if (!err.empty()) {
		if (getenv("ATIV_PLAY_QUIET")) fprintf(stderr, "%s\n", err.c_str());   /* (a test: no box to wait on) */
		else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Project Nativ", err.c_str(), nullptr);
	}
	return err.empty() ? 0 : 1;
}

#ifdef __APPLE__
/* the Mac app's own checks, before its window: where it runs from (a temporary copy macOS makes of a downloaded app
 * opened where it landed, or a read-only disk image: it belongs in Applications), the download's quarantine on its own
 * files (cleared once the player has opened it), and Apple's Command Line Tools (every game build needs their compiler
 * and SDK: Apple's to install, not ours to ship). False: it said why and the launcher stops. */
static bool mac_preflight(void) {
	struct statfs fs;
	const bool in_place = g_root.find("/AppTranslocation/") == std::string::npos && !(statfs(g_root.c_str(), &fs) == 0 && (fs.f_flags & MNT_RDONLY));
	if (!in_place) {
		const SDL_MessageBoxButtonData b[2] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Open Applications"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
		const SDL_MessageBoxData m = {SDL_MESSAGEBOX_INFORMATION, nullptr, "Project Nativ", "Drag Project Nativ into your Applications folder first, then open it from there.", 2, b, nullptr};
		int hit = 0; SDL_ShowMessageBox(&m, &hit);
		if (hit == 1) SDL_OpenURL("file:///Applications");
		return false;
	}
	const size_t c = g_root.rfind("/Contents/Resources");   /* (inside the app: its bundle) */
	const std::string app = c != std::string::npos ? g_root.substr(0, c) : g_root;
	if (getxattr(app.c_str(), "com.apple.quarantine", nullptr, 0, 0, 0) >= 0) (void)!system(("xattr -dr com.apple.quarantine '" + app + "' 2>/dev/null").c_str());
	if (system("xcode-select -p >/dev/null 2>&1") != 0) {
		(void)!system("xcode-select --install >/dev/null 2>&1");
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Project Nativ", "Project Nativ builds each game on your Mac with Apple's free Command Line Tools.\n\nInstall them from the window that just opened, then open Project Nativ again.", nullptr);
		return false;
	}
	return true;
}
#endif

int main(int argc, char **argv) {
	g_root = find_root();
	if (!exists(g_root + "/pipeline/vitarecomp.py")) {   /* (the launcher apart from its folder: run from inside the zip, or copied out alone) */
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Project Nativ", "Project Nativ can't find the rest of its files.\n\nIf you opened it from inside the zip, extract the whole zip first (on Windows: right-click it, Extract All), then open Project Nativ from the extracted folder.", nullptr);
		return 1;
	}
	g_dump = library_dir();
	set_env("ATIV_DUMP", g_dump);   /* (the stages it runs: the same library whatever their own rule finds) */
	const char *shot_path = getenv("ATIV_SHOT");
	g_shot_mode = shot_path != nullptr;
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif
#ifdef __APPLE__
	if (!shot_path && !getenv("ATIV_TASK") && !mac_preflight()) return 0;
#endif
	if (!getenv("PYTHONPYCACHEPREFIX")) set_env("PYTHONPYCACHEPREFIX", pref_dir() + "pycache");   /* (the stages' compiled Python beside the settings: an install is never written) */
	if (!getenv("PYTHONUTF8")) set_env("PYTHONUTF8", "1");   /* (the stages' Python in UTF-8 mode: Windows' cp1252 default crashed an add on a game title outside it) */
#if !defined(_WIN32) && !defined(__APPLE__)
	if (!getenv("ZIG_GLOBAL_CACHE_DIR")) {   /* (Linux's compiler's own cache, kept per user: an AppImage's install can't be written, and it outlives an update) */
		const char *x = getenv("XDG_CACHE_HOME"), *h = getenv("HOME");
		set_env("ZIG_GLOBAL_CACHE_DIR", (x && *x ? std::string(x) : std::string(h ? h : "/tmp") + "/.cache") + "/ProjectNativ/zig");
	}
#endif
	if (const char *t = getenv("ATIV_TASK")) return task_cli(t);   /* (after the caches above: a task's steps run as the window's do) */
	if (argc >= 3 && !strcmp(argv[1], "--play")) {   /* (a Steam shortcut: the game, no window) */
		prefs_load();
		set_env("RECOMP_SETTINGS_SHARED", shared_path());
		const std::string d = pref_dir(); if (!d.empty()) g_log_file = d + "launcher.play.log";
		return play_cli(argv[2]);
	}
	if (!shot_path) {   /* (the last launcher's log kept beside this one's) */
		const std::string d = pref_dir();
		if (!d.empty()) { g_log_file = d + "launcher.log"; remove((d + "launcher.prev.log").c_str()); rename(g_log_file.c_str(), (d + "launcher.prev.log").c_str()); }
	}
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	int sw = 1280, sh = 800;
	if (const char *s = getenv("ATIV_SHOT_SIZE")) sscanf(s, "%dx%d", &sw, &sh);
	const float start_scale = shot_path ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());   /* the window's opening size only; the UI's scale comes from the window */
	SDL_Window *win = g_win = shot_path ? SDL_CreateWindow("Project Nativ", sw, sh, SDL_WINDOW_HIDDEN)
	                            : SDL_CreateWindow("Project Nativ", (int)(1180 * start_scale), (int)(760 * start_scale), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	g_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
	if (!win || !g_gpu || (!shot_path && !SDL_ClaimWindowForGPUDevice(g_gpu, win))) { fprintf(stderr, "window/GPU: %s\n", SDL_GetError()); return 1; }
	if (!shot_path) SDL_SetGPUSwapchainParameters(g_gpu, win, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
	SDL_SetWindowMinimumSize(win, 420, 480);
	window_icon(win);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;   /* a controller (or the arrows) moves through everything; L1/R1 the tabs */
	ui::load_system_font(io);
	if (shot_path) apply_scale(hub_ui_scale((float)sh, 1.0f));   /* (a screenshot run: the size it renders at, display scale 1) */
	else apply_window_scale(win);   /* the display the window is on, not the primary one, and the window's height */
	ImGui_ImplSDL3_InitForSDLGPU(win);
	ImGui_ImplSDLGPU3_InitInfo info = {};
	info.Device = g_gpu; info.ColorTargetFormat = shot_path ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM : SDL_GetGPUSwapchainTextureFormat(g_gpu, win); info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
	ImGui_ImplSDLGPU3_Init(&info);

	prefs_load();
	g_settings_writable = !shot_path || getenv("ATIV_PREFS");
	if (g_settings_writable) settings_migrate();   /* (a screenshot run touches no one's settings but a test folder's) */
	set_env("RECOMP_SETTINGS_SHARED", shared_path());   /* the games it starts read the same shared file (runtime/core/main.c) */
	library_scan();
	g_setup_open = g_prefs["setup_done"] != "1" || !g_firmware;   /* first run, or the firmware still missing */
	if (!g_firmware && g_prefs["setup_done"] == "1") g_setup_step = 1;
	if (shot_path) {
		std::string ui_sel = getenv("ATIV_SHOT_UI") ? getenv("ATIV_SHOT_UI") : "library";
		g_setup_open = ui_sel.rfind("setup", 0) == 0;
		if (g_setup_open) g_setup_step = ui_sel.size() > 6 ? atoi(ui_sel.c_str() + 6) : 0;
		g_add_open = ui_sel == "add" || ui_sel == "add:vita"; g_open_vita_popup = ui_sel == "add:vita";
		if (const char *f = getenv("ATIV_SHOT_SCAN")) { scan_load(f); g_add_kind = 0; g_scan_path = "/Volumes/Vita/ux0"; }
		if (const char *f = getenv("ATIV_SHOT_LIBSCAN")) { scan_parse(f, g_lib_found); }
		if (const char *f = getenv("ATIV_SHOT_STEAM")) steam_parse(read_file(f));
		g_drawer = ui_sel == "log";
		g_sac_open = ui_sel.rfind("sac", 0) == 0; g_sac_game = "PCSE00640"; g_sac_rebuilt = ui_sel == "sac:again";
		g_settings_open = ui_sel.rfind("settings", 0) == 0; if (g_settings_open && ui_sel.size() > 9) g_settings_tab = atoi(ui_sel.c_str() + 9);
		if (ui_sel.rfind("game:", 0) == 0) g_game_settings = ui_sel.substr(5);
		if (ui_sel.rfind("more:", 0) == 0) g_game_page = ui_sel.substr(5);
		if (ui_sel.rfind("name:", 0) == 0) g_name_game = ui_sel.substr(5);
		if (ui_sel == "map:touch") { g_settings_open = true; g_settings_tab = 1; g_map_type = 2; }   /* (UI-01n: Settings > Controls, the touch mapping) */
		else if (ui_sel.rfind("map:", 0) == 0) {   /* (map:pad|key:<row>: Settings > Controls with that row's Change dialog open) */
			g_settings_open = true; g_settings_tab = 1; g_map_type = ui_sel.compare(4, 3, "pad") == 0 ? 0 : 1;
			g_bind.open = true; g_bind.pad = g_map_type == 0; g_bind.row = atoi(ui_sel.c_str() + 8); g_bind.until = 6.0;   /* (a screenshot run's clock starts at 0: the real 6 seconds) */
		}
		if (ui_sel == "steamclosed") { g_steam_closed_open = true; g_steam_name = "Adding Soul Sacrifice Delta to Steam"; g_steam_game = "PCSF00532"; g_steam_steps = {step("Adding it to Steam", vitarecomp({"steam", "add", "PCSF00532"}))}; }
		if (ui_sel == "osk") { static std::string sample = "D:/Vita backup/ux0"; osk_open(&sample, "##scanpath"); g_osk.top = true; }
		if (ui_sel.rfind("browse:", 0) == 0) {   /* (browse:<kind>:<dir>: the own browser, opened at <dir>) */
			static std::string picked_here;
			const std::string rest = ui_sel.substr(7); const size_t c = rest.find(':');
			const std::string k = rest.substr(0, c);
			browse_open(&picked_here, k == "pkg" ? BROWSE_PKG : k == "licence" ? BROWSE_LICENCE : k == "pup" ? BROWSE_PUP : BROWSE_FOLDER);
			g_browse.top = true; browse_go(c == std::string::npos ? std::string() : rest.substr(c + 1));
		}
		if (getenv("ATIV_SHOT_NOFW") && *getenv("ATIV_SHOT_NOFW")) { g_firmware = false; g_firmware_version.clear(); }
		if (const char *u = getenv("ATIV_SHOT_UPDATE")) { g_update.tag = u; g_update.page = "https://example.invalid/"; }   /* (the notice, without asking anyone) */
		files_check();   /* (an install's own check, as at start: what a damaged copy shows) */
		int r = shot(win, shot_path, sw, sh);
		SDL_WaitForGPUIdle(g_gpu);
		ImGui_ImplSDL3_Shutdown(); ImGui_ImplSDLGPU3_Shutdown(); ImGui::DestroyContext();
		SDL_DestroyGPUDevice(g_gpu); SDL_DestroyWindow(win); SDL_Quit();
		return r;
	}

	update_check_start(); files_check_start(); steam_refresh();
	playtime_load(); size_refresh();
	/* fullscreen (UI-01h): --fullscreen (a Steam entry for the launcher itself), or as last time (hub.ini launcher_fullscreen);
	 * F11 switches it and keeps the choice */
	{	bool fs = g_prefs["launcher_fullscreen"] == "1";
		for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--fullscreen")) fs = true;
		if (fs) SDL_SetWindowFullscreen(win, true);
	}
	bool quit = false;
	while (!quit) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			ImGui_ImplSDL3_ProcessEvent(&e);
			if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(win))) quit = true;
			if ((e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED || e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) && e.window.windowID == SDL_GetWindowID(win))
				apply_window_scale(win);   /* (apply resets the style first, so this is safe; touch_style rebuilds its sizes from it) */
			if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11 && !e.key.repeat) {
				const bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0;
				SDL_SetWindowFullscreen(win, fs); prefs_set("launcher_fullscreen", fs ? "1" : "0");
			}
		}
		if (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }
		pick_poll();
		{	static bool lib_looked;   /* the library folders searched once at start, when nothing else runs (setup first) */
			if (!lib_looked && !g_task.running && !g_setup_open) {
				lib_looked = true;
				auto it = g_prefs.find("library_rescan");
				if (!g_lib_folders.empty() && (it == g_prefs.end() || it->second != "0")) task_start("Looking in your library folders", "libscan", "", lib_scan_steps());
			}
		}
		{	std::string step; { std::lock_guard<std::mutex> l(g_log_lock); step = g_step; }   /* (the game's own run began: its play time from here) */
			if (g_task.running && g_task.kind == "play" && !g_run_started && step.rfind("Running ", 0) == 0) g_run_started = (long long)time(nullptr);
		}
		static Uint64 last_scan;
		if (g_task.running && SDL_GetTicks() - last_scan > 1500) { library_scan(); last_scan = SDL_GetTicks(); }   /* (a long add: its card follows) */
		if (g_refresh.exchange(false)) {
			if (g_task.kind == "play") { SDL_RestoreWindow(win); SDL_RaiseWindow(win); }   /* (the game closed: back to the library) */
			if (g_task.kind == "play" && g_run_started) { playtime_record(g_task.game, g_run_started, (long long)time(nullptr) - g_run_started); }
			g_run_started = 0;
			if (g_task.kind != "play") size_refresh();
			library_scan();
			if (g_task.kind == "scan" && g_task.ok) scan_load(scan_file());
			if (g_task.kind == "report" && g_task.ok) open_folder(g_dump + "/reports");
			if (g_task.kind == "steam") {   /* what Steam has from us, read again; Steam was running: the dialog to close it */
				steam_refresh();
				std::lock_guard<std::mutex> l(g_log_lock);
				if (!g_task.ok && g_log.find("Steam is running", g_task.log_from) != std::string::npos) g_steam_closed_open = true;
			}
			if (g_task.kind == "play") {   /* Windows refused the game (vitarecomp's SAC_BLOCKED line): the Smart App Control dialog */
				bool blocked;
				{ std::lock_guard<std::mutex> l(g_log_lock); blocked = !g_task.ok && g_log.find("Smart App Control is on", g_task.log_from) != std::string::npos; }
				if (blocked && g_sac_game != g_task.game) g_sac_rebuilt = false;   /* (another game: its first refusal) */
				if (blocked) { g_sac_game = g_task.game; g_sac_open = true; } else g_sac_rebuilt = false;
			}
		}
		ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
		hub_ui(win);
		ImGui::Render();
		ImDrawData *dd = ImGui::GetDrawData();
		SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
		SDL_GPUTexture *swap = nullptr;
		if (cmd && SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &swap, nullptr, nullptr) && swap && dd->DisplaySize.x > 0) {
			ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
			SDL_GPUColorTargetInfo ti = {};
			ti.texture = swap; ti.clear_color = SDL_FColor{ui::bg.x, ui::bg.y, ui::bg.z, 1}; ti.load_op = SDL_GPU_LOADOP_CLEAR; ti.store_op = SDL_GPU_STOREOP_STORE;
			SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ti, 1, nullptr);
			ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp);
			SDL_EndGPURenderPass(rp);
		}
		if (cmd) SDL_SubmitGPUCommandBuffer(cmd);
	}
	task_cancel();
	if (g_task.reader.joinable()) g_task.reader.join();
	SDL_WaitForGPUIdle(g_gpu);
	for (auto &kv : g_tex) if (kv.second.t) SDL_ReleaseGPUTexture(g_gpu, kv.second.t);
	ImGui_ImplSDL3_Shutdown(); ImGui_ImplSDLGPU3_Shutdown(); ImGui::DestroyContext();
	SDL_ReleaseWindowFromGPUDevice(g_gpu, win); SDL_DestroyGPUDevice(g_gpu); SDL_DestroyWindow(win); SDL_Quit();
	return 0;
}
