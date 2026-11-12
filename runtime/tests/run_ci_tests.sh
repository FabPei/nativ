#!/bin/bash
# The runtime tests that need no game, no window, no SDL and no GPU: each is built with the line README.md gives for it
# and run; any compile error or non-zero exit fails the run. Used by .github/workflows/release.yml on Linux; runs the
# same on any Linux or WSL shell:  bash runtime/tests/run_ci_tests.sh
# Left out: tests that need SDL3 (camera, dlg_term, sysmodule, vcount, pad_bind, menu_pad, savedlg_pad), macOS-only
# mtl_archive_test, and tests without a documented build line.
set -u
cd "$(dirname "$0")"
INC="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CF="-O1 -g -Wall -Wno-unused-function"
fail=0

run() {  # run <name> <compile args...>
	local name="$1"; shift
	if ! cc "$@" -o "$OUT/$name" >"$OUT/$name.cc.log" 2>&1; then
		echo "FAIL $name (compile)"; sed 's/^/    /' "$OUT/$name.cc.log" | tail -20; fail=$((fail + 1)); return
	fi
	if "$OUT/$name" >"$OUT/$name.log" 2>&1; then
		echo "ok   $name: $(tail -1 "$OUT/$name.log")"
	else
		echo "FAIL $name (exit $?)"; sed 's/^/    /' "$OUT/$name.log" | tail -30; fail=$((fail + 1))
	fi
}

run kernel_test       $CF -D_GNU_SOURCE $INC kernel_test.c -lpthread -lm
run timer_test        $CF -D_GNU_SOURCE $INC timer_test.c ../debug/waitstats.c -lpthread -lm
run input_rec_test    $CF $INC input_rec_test.c ../gfx/input_rec.c
run gesture_test      $CF $INC -I .. gesture_test.c -lm -lpthread
run jpeg_test         $CF $INC jpeg_test.c
run metrics_test      -O1 -g -Wall -Wno-unused-function -I ../core -I ../platform metrics_test.c ../platform/platform_posix.c -lpthread -ldl
run stubret_test      -O1 -g -Wall $INC stubret_test.c
run view_swz_test     -O1 -g -Wall -I ../gfx view_swz_test.c
run audiodec_test     -O1 -g -Wall -D_GNU_SOURCE $INC audiodec_test.c ../../third_party/libatrac9/*.c -lpthread -lm
run codec_engine_test $CF $INC codec_engine_test.c -lpthread
run audio_pace_test   -O1 -g -Wall audio_pace_test.c
run hle_small_test    -O1 -g -Wall -I ../core hle_small_test.c
run dump_fit_test     -O1 -g -Wall -I ../ui dump_fit_test.c
run intro_skip_test   -O1 -g -Wall -I ../ui intro_skip_test.c
run kb_bind_test      -O1 -g -Wall -I ../ui kb_bind_test.c -lm
run pad_rows_test     -O1 -g -Wall -I ../ui pad_rows_test.c ../ui/ui_text.c -lm
run stick_curve_test  -O1 -g -Wall -I ../ui stick_curve_test.c -lm
run gaps_test         -O1 -g -w -D_GNU_SOURCE $INC gaps_test.c -lm -lpthread -Wl,--unresolved-symbols=ignore-all   # GAPS-01 (usse.c's guest-memory helpers stay unresolved: decode_one never reaches them)
run overlay_scale_test -O1 -g -Wall -I ../ui overlay_scale_test.c -lm
run trophy_test       -O1 -g -Wall -I ../core trophy_test.c
run game_lang_test    -O1 -g -Wall -I ../core game_lang_test.c
run target_scale_test -O1 -g -Wall -I ../gfx target_scale_test.c
run log_redact_test   -O1 -g -Wall -I ../ui log_redact_test.c
run ui_text_test      -O1 -g -Wall -I ../ui ui_text_test.c ../ui/ui_text.c
run settings_reset_test -O1 -g -Wall -I ../core settings_reset_test.c ../core/settings.c -lpthread
run settings_save_test -O1 -g -Wall -I ../core settings_save_test.c ../core/settings.c -lpthread
run sysinfo_test      -O1 -g -Wall -I ../ui sysinfo_test.c
run cdlg_test         -O1 -g -Wall -I ../core cdlg_test.c
run imedlg_test       -O1 -g -Wall -I ../core imedlg_test.c ../core/imedlg.c -lpthread

if [ "$fail" -ne 0 ]; then echo "$fail test(s) failed"; exit 1; fi
echo "all runtime tests passed"
