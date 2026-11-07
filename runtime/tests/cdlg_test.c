/* cdlg_test.c -- HLE-LIB-01: the common dialogs' state machine (cdlg.h, see its header comment).
 * Fail-before is the runtime's record: sceNpMessageDialogGetStatus was polled 10000+ times answering 0 (NONE)
 * -- the dialog the game opened never reached FINISHED.
 *   cc -O1 -g -Wall -I ../core cdlg_test.c -o /tmp/cdlg_test && /tmp/cdlg_test
 */
#include <stdio.h>
#include "cdlg.h"

static int fails;
static void check(const char *what, int ok) { printf("%-62s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	CdlgState s[CDLG_N] = {{0}};
	int32_t r = -1;
	check("never opened: status NONE", cdlg_status(s, CDLG_NP_MESSAGE) == CDLG_STATUS_NONE);
	check("never opened: GetResult NOT_IN_USE", cdlg_result(s, CDLG_NP_MESSAGE, &r) == CDLG_ERR_NOT_IN_USE);
	check("never opened: Term NOT_IN_USE", cdlg_term(s, CDLG_NP_MESSAGE) == CDLG_ERR_NOT_IN_USE);
	check("never opened: Abort NOT_RUNNING", cdlg_abort(s, CDLG_NP_MESSAGE) == CDLG_ERR_NOT_RUNNING);

	check("NP message: Init ok", cdlg_init(s, CDLG_NP_MESSAGE) == 0);
	check("NP message: status FINISHED (the poll loop ends)", cdlg_status(s, CDLG_NP_MESSAGE) == CDLG_STATUS_FINISHED);
	check("NP message: second Init BUSY", cdlg_init(s, CDLG_NP_MESSAGE) == CDLG_ERR_BUSY);
	check("NP message: result USER_CANCELED (no PSN)", cdlg_result(s, CDLG_NP_MESSAGE, &r) == 0 && r == CDLG_RESULT_USER_CANCELED);
	check("NP message: Term ok, status NONE again", cdlg_term(s, CDLG_NP_MESSAGE) == 0 && cdlg_status(s, CDLG_NP_MESSAGE) == CDLG_STATUS_NONE);
	check("NP message: Init after Term ok", cdlg_init(s, CDLG_NP_MESSAGE) == 0);

	check("IME: Init ok, result OK", cdlg_init(s, CDLG_IME) == 0 && cdlg_result(s, CDLG_IME, &r) == 0 && r == CDLG_RESULT_OK);
	check("IME: kinds are independent (NP message still FINISHED)", cdlg_status(s, CDLG_NP_MESSAGE) == CDLG_STATUS_FINISHED);
	check("IME: Abort -> result ABORTED", cdlg_abort(s, CDLG_IME) == 0 && cdlg_result(s, CDLG_IME, &r) == 0 && r == CDLG_RESULT_ABORTED);
	check("IME: Term after Abort ok", cdlg_term(s, CDLG_IME) == 0);

	int ok = 1;
	for (int k = CDLG_NP_PROFILE; k < CDLG_N; k++) { ok &= cdlg_init(s, k) == 0 && cdlg_result(s, k, &r) == 0 && r == CDLG_RESULT_USER_CANCELED && cdlg_term(s, k) == 0; }
	check("profile/facebook/twitter/store: canceled, then terminated", ok);

	/* HLE-LIB-05: a kind the host shows runs until the player answers */
	check("IME card: init_running ok, status RUNNING", cdlg_init_running(s, CDLG_IME) == 0 && cdlg_status(s, CDLG_IME) == CDLG_STATUS_RUNNING);
	check("IME card: second Init BUSY", cdlg_init_running(s, CDLG_IME) == CDLG_ERR_BUSY);
	check("IME card: GetResult while running NOT_FINISHED", cdlg_result(s, CDLG_IME, &r) == CDLG_ERR_NOT_FINISHED);
	check("IME card: Term while running NOT_FINISHED", cdlg_term(s, CDLG_IME) == CDLG_ERR_NOT_FINISHED);
	cdlg_finish(s, CDLG_IME, CDLG_RESULT_USER_CANCELED);
	check("IME card: finish(cancel) -> FINISHED, USER_CANCELED", cdlg_status(s, CDLG_IME) == CDLG_STATUS_FINISHED && cdlg_result(s, CDLG_IME, &r) == 0 && r == CDLG_RESULT_USER_CANCELED);
	check("IME card: Term ok", cdlg_term(s, CDLG_IME) == 0);
	check("IME card: Abort while running -> ABORTED", cdlg_init_running(s, CDLG_IME) == 0 && cdlg_abort(s, CDLG_IME) == 0 && cdlg_result(s, CDLG_IME, &r) == 0 && r == CDLG_RESULT_ABORTED && cdlg_term(s, CDLG_IME) == 0);

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
