/*
 * HLE-LIB-01: the common dialogs' shared state machine (SceCommonDialog), pure and header-only so
 * runtime/tests/cdlg_test.c checks it. Every dialog kind runs the console's contract (vitasdk common_dialog.h):
 *   Init      a kind already in use -> SCE_COMMON_DIALOG_ERROR_BUSY; else it runs (here: it finishes at once,
 *             the host answers for the player) with the kind's answer as its result
 *   GetStatus NONE before Init and after Term, FINISHED while it waits for Term
 *   Abort     a running/finished kind ends with RESULT_ABORTED; not in use -> NOT_RUNNING
 *   Close     (the message dialog) the same as Abort but with the kind's own result kept
 *   GetResult not in use -> NOT_IN_USE; else the result
 *   Term      not in use -> NOT_IN_USE; else back to NONE
 * The answers: the IME dialog OK (the initial text is entered as typed), every PSN, social or store dialog
 * USER_CANCELED -- there is no PSN, Twitter, Facebook or store behind the host, and a cancel is the console's
 * answer when the player closes such a dialog. Before HLE-LIB-01 these kinds were placeholders: Init returned 0,
 * GetStatus 0 (NONE) forever, so a game that waits for FINISHED waited for ever.
 */
#ifndef RECOMP_CDLG_H
#define RECOMP_CDLG_H
#include <stdint.h>

enum { CDLG_STATUS_NONE = 0, CDLG_STATUS_RUNNING = 1, CDLG_STATUS_FINISHED = 2 };
enum { CDLG_RESULT_OK = 0, CDLG_RESULT_USER_CANCELED = 1, CDLG_RESULT_ABORTED = 2 };
#define CDLG_ERR_BUSY        0x80020401u
#define CDLG_ERR_NOT_RUNNING 0x80020404u
#define CDLG_ERR_NOT_IN_USE  0x80020411u
#define CDLG_ERR_NOT_FINISHED 0x80020410u

enum { CDLG_IME, CDLG_NP_MESSAGE, CDLG_NP_PROFILE, CDLG_NP_SNS_FACEBOOK, CDLG_TW, CDLG_STORE_CHECKOUT, CDLG_N };
typedef struct { int status; int32_t result; } CdlgState;

static inline int32_t cdlg_answer(int kind) { return kind == CDLG_IME ? CDLG_RESULT_OK : CDLG_RESULT_USER_CANCELED; }
static inline uint32_t cdlg_init(CdlgState *s, int kind) {
	if (s[kind].status != CDLG_STATUS_NONE) return CDLG_ERR_BUSY;
	s[kind].status = CDLG_STATUS_FINISHED; s[kind].result = cdlg_answer(kind);
	return 0;
}
static inline int cdlg_status(const CdlgState *s, int kind) { return s[kind].status; }
/* (HLE-LIB-05) a kind the host shows to the player (the IME card): it runs until cdlg_finish */
static inline uint32_t cdlg_init_running(CdlgState *s, int kind) {
	if (s[kind].status != CDLG_STATUS_NONE) return CDLG_ERR_BUSY;
	s[kind].status = CDLG_STATUS_RUNNING; s[kind].result = 0;
	return 0;
}
static inline void cdlg_finish(CdlgState *s, int kind, int32_t result) {
	if (s[kind].status == CDLG_STATUS_RUNNING) { s[kind].status = CDLG_STATUS_FINISHED; s[kind].result = result; }
}
static inline uint32_t cdlg_abort(CdlgState *s, int kind) {
	if (s[kind].status == CDLG_STATUS_NONE) return CDLG_ERR_NOT_RUNNING;
	s[kind].status = CDLG_STATUS_FINISHED; s[kind].result = CDLG_RESULT_ABORTED;
	return 0;
}
static inline uint32_t cdlg_result(const CdlgState *s, int kind, int32_t *out) {
	if (s[kind].status == CDLG_STATUS_NONE) return CDLG_ERR_NOT_IN_USE;
	if (s[kind].status == CDLG_STATUS_RUNNING) return CDLG_ERR_NOT_FINISHED;   /* (HLE-LIB-05) */
	*out = s[kind].result; return 0;
}
static inline uint32_t cdlg_term(CdlgState *s, int kind) {
	if (s[kind].status == CDLG_STATUS_NONE) return CDLG_ERR_NOT_IN_USE;
	if (s[kind].status == CDLG_STATUS_RUNNING) return CDLG_ERR_NOT_FINISHED;   /* (HLE-LIB-05) */
	s[kind].status = CDLG_STATUS_NONE; s[kind].result = 0;
	return 0;
}
#endif
