/* export_progress_test.cpp - launcher/export_progress.h (EXPORT-01a's launcher side): the export's lines as the launcher's
 * log holds them (stamped, with the launcher's own lines between), success and failure.
 * Build: c++ -std=c++17 -Wall -I .. export_progress_test.cpp -o /tmp/export_progress_test && /tmp/export_progress_test */
#include <cstdio>
#include <string>
#include "export_progress.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
	const std::string head = "10:00:00 \n10:00:00 == Exporting Soul Sacrifice Delta for Steam Deck\n10:00:00 @@ Exporting for Steam Deck\n"
	                         "10:00:00 $ python pipeline/vitarecomp.py export-appimage PCSF00532 D:/out/Soul.AppImage\n";
	{	ExportView v = export_parse(head, false);
		CHECK(v.step == 0 && v.notice.empty() && v.result.empty() && v.error.empty()); }
	const std::string mid = head + "10:00:01 export: a private file for your own device -- do not share it\r\n10:00:01 export 1/3: compiling for znver2\n";
	{	ExportView v = export_parse(mid, false);
		CHECK(v.notice == "a private file for your own device -- do not share it");   /* (\r and the stamp gone) */
		CHECK(v.step == 1 && v.steps == 3 && v.step_text[1] == "compiling for znver2"); }
	const std::string done = mid + "10:02:00 some compiler chatter\n10:05:00 export 2/3: collecting the game's files\n10:06:00 export 3/3: packing\n"
	                         "10:07:00 exported Soul Sacrifice Delta -> D:/out/Soul.AppImage (3.2 GB): copy it to the Deck\n"
	                         "10:07:00 -- Exporting for Steam Deck: done in 7:00\n10:07:00 == done (exit 0) in 7:00\n";
	{	ExportView v = export_parse(done, false);
		CHECK(v.step == 3 && v.step_text[2] == "collecting the game's files" && v.step_text[3] == "packing");
		CHECK(v.result == "exported Soul Sacrifice Delta -> D:/out/Soul.AppImage (3.2 GB): copy it to the Deck");
		CHECK(v.out == "D:/out/Soul.AppImage");
		CHECK(v.error.empty());
		CHECK(v.step_at[1] == 10 * 3600 + 1 && v.step_at[2] == 10 * 3600 + 5 * 60 && v.step_at[3] == 10 * 3600 + 6 * 60 && v.done_at == 10 * 3600 + 7 * 60);
		CHECK(export_secs(v.step_at[1], v.step_at[2]) == 299 && export_dur(export_secs(v.step_at[1], v.done_at)) == "6:59"); }
	CHECK(export_secs(23 * 3600 + 59 * 60, 60) == 120);   /* (past midnight) */
	CHECK(export_secs(-1, 5) == -1 && export_dur(-1).empty());
	CHECK(export_dur(2610) == "43:30" && export_dur(3729) == "1:02:09" && export_dur(7) == "0:07");
	{	/* a failure: its one plain line, the launcher's end lines skipped */
		const std::string bad = head + "10:00:01 export: notice\n10:00:01 build it first: the game's code is older than this checkout\n"
		                        "10:00:01 -- Exporting for Steam Deck: failed after 0:01\n10:00:01 == done (exit 1) in 0:01\n";
		ExportView v = export_parse(bad, true);
		CHECK(v.error == "build it first: the game's code is older than this checkout");
		CHECK(v.step == 0 && v.result.empty()); }
	{	ExportView v = export_parse(head + "10:00:00 -- x: failed after 0:00\n", true);
		CHECK(v.error == "The export stopped without saying why."); }
	{	/* not a step line: "export 9/3", "exporting" */
		ExportView v = export_parse("export 4/3: no\nexporting things\nexport x/3: no\n", false);
		CHECK(v.step == 0); }
	{	ExportView v = export_parse("exported A -> /home/deck/Games/A.AppImage\n", false);
		CHECK(v.out == "/home/deck/Games/A.AppImage"); }   /* (no size part) */
	CHECK(export_folder("D:/out/Soul.AppImage") == "D:/out");
	CHECK(export_folder("C:\\Users\\me\\A.AppImage") == "C:\\Users\\me");
	CHECK(export_folder("/A.AppImage") == "/");
	CHECK(export_folder("A.AppImage") == ".");
	CHECK(export_file_name("Soul Sacrifice Delta") == "Soul-Sacrifice-Delta.AppImage");
	CHECK(export_file_name("Uncharted: Golden Abyss") == "Uncharted-Golden-Abyss.AppImage");
	CHECK(export_file_name("???") == "game.AppImage");
	printf("export_progress: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
