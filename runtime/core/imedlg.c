/* HLE-LIB-05: the IME dialog's shared state (imedlg.h) */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "imedlg.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int ui_ready, open_, answered, ok_, seq_;
static uint32_t max_;
static char title_[512], text_[IMEDLG_TEXT_MAX];

void imedlg_set_ui_ready(int r) { pthread_mutex_lock(&lock); ui_ready = r; pthread_mutex_unlock(&lock); }
int imedlg_ui_ready(void) { pthread_mutex_lock(&lock); int r = ui_ready; pthread_mutex_unlock(&lock); return r; }
int imedlg_open(const char *title, const char *text, uint32_t max_chars) {
	pthread_mutex_lock(&lock);
	const int r = ui_ready;
	if (r) {
		snprintf(title_, sizeof title_, "%s", title ? title : ""); snprintf(text_, sizeof text_, "%s", text ? text : "");
		max_ = max_chars; open_ = 1; answered = 0; ok_ = 0; seq_++;
	}
	pthread_mutex_unlock(&lock);
	return r;
}
int imedlg_take(char *text, size_t n, int *ok) {
	pthread_mutex_lock(&lock);
	const int r = open_ && answered;
	if (r) { snprintf(text, n, "%s", text_); *ok = ok_; open_ = 0; answered = 0; }
	pthread_mutex_unlock(&lock);
	return r;
}
void imedlg_close(void) { pthread_mutex_lock(&lock); open_ = 0; answered = 0; pthread_mutex_unlock(&lock); }
int imedlg_view(char *title, size_t tn, char *text, size_t n, uint32_t *max_chars, int *seq) {
	pthread_mutex_lock(&lock);
	const int r = open_ && !answered;
	if (r) { snprintf(title, tn, "%s", title_); snprintf(text, n, "%s", text_); *max_chars = max_; *seq = seq_; }
	pthread_mutex_unlock(&lock);
	return r;
}
void imedlg_answer(int ok, const char *text) {
	pthread_mutex_lock(&lock);
	if (open_ && !answered) { answered = 1; ok_ = ok; if (ok && text) snprintf(text_, sizeof text_, "%s", text); }
	pthread_mutex_unlock(&lock);
}
