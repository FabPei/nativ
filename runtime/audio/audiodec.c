/*
 * SceAudiodecUser: the system audio decoder a game feeds its own compressed streams to (music, movie
 * soundtracks) and whose PCM it then plays itself through SceAudioOut. Implemented here for ATRAC9 with
 * the LibAtrac9 already vendored for the NGS player (third_party/libatrac9, MIT). Other codec types
 * (MP3, AAC, CELP) fail with SCE_AUDIODEC_ERROR_INVALID_TYPE and a log line, so a game's own error path
 * runs; before this file every call was an unresolved import returning 0 with nothing decoded, which a
 * game reads as success and plays silence.
 *
 * The contract (the console's, as Vita3K's SceAudiodecUser implements it): SceAudiodecCtrl is 0x28 bytes
 * {size, handle, pEs, inputEsSize, maxEsSize, pPcm, outputPcmSize, maxPcmSize, wordLength, pInfo};
 * SceAudiodecInfo is {size, then the codec's union} -- for ATRAC9 {configData (4 bytes), ch, bitRate,
 * samplingRate, superFrameSize, framesInSuperFrame}. CreateDecoder reads the configuration word, fills
 * the rest of the info and the ctrl's maxEsSize/maxPcmSize and hands out a handle. Decode decodes one
 * ATRAC9 frame from pEs to pPcm (16-bit interleaved) and reports the bytes taken (inputEsSize, the
 * superframe's padding counted with its last frame) and the bytes written (outputPcmSize). ClearContext
 * drops the decoder's history (a seek or loop restart). RECOMP_AUDIODEC_LOG=1 logs the lifecycle.
 */
#include "hle.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/libatrac9/libatrac9.h"

#define SCE_AUDIODEC_ERROR_API_FAIL 0x807F0000u
#define SCE_AUDIODEC_ERROR_INVALID_TYPE 0x807F0001u
#define SCE_AUDIODEC_ERROR_NOT_INITIALIZED 0x807F0005u
#define SCE_AUDIODEC_ERROR_ALL_HANDLES_IN_USE 0x807F0007u
#define SCE_AUDIODEC_ERROR_INVALID_PTR 0x807F0008u
#define SCE_AUDIODEC_ERROR_INVALID_HANDLE 0x807F0009u
#define SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE 0x807F000Au
#define SCE_AUDIODEC_ERROR_INVALID_SIZE 0x807F000Du
#define SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG 0x807F2000u
#define SCE_AUDIODEC_TYPE_AT9 0x1003u
#define SCE_AUDIODEC_AT9_MAX_ES_SIZE 1024u

/* SceAudiodecCtrl field offsets */
enum { C_SIZE = 0, C_HANDLE = 4, C_ES = 8, C_ES_USED = 12, C_ES_MAX = 16, C_PCM = 20, C_PCM_GIVEN = 24, C_PCM_MAX = 28, C_WORD = 32, C_INFO = 36, CTRL_SIZE = 0x28 };
/* SceAudiodecInfo (ATRAC9) field offsets */
enum { I_CONFIG = 4, I_CH = 8, I_BITRATE = 12, I_RATE = 16, I_SF_SIZE = 20, I_SF_FRAMES = 24 };

#define MAX_DECODERS 32
typedef struct {
	int used;
	void *at9;
	unsigned char config[4];
	Atrac9CodecInfo ci;
	int sf_frame, sf_left;   /* the frame within the superframe; its bytes not yet taken */
	short pcm[256 * 8];      /* one frame: at most 256 samples x 8 channels (LibAtrac9's limits) */
} Decoder;
static Decoder decs[MAX_DECODERS];
static int at9_initialised;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;

static int ad_log(void) { static int v = -1; if (v < 0) v = getenv("RECOMP_AUDIODEC_LOG") != 0; return v; }

static int at9_open(Decoder *d) {
	d->at9 = Atrac9GetHandle();
	if (!d->at9) return -1;
	if (Atrac9InitDecoder(d->at9, d->config)) { Atrac9ReleaseHandle(d->at9); d->at9 = 0; return -1; }
	Atrac9GetCodecInfo(d->at9, &d->ci);
	d->sf_frame = 0; d->sf_left = d->ci.superframeSize;
	return 0;
}

static void h_init(Cpu *c) {
	uint32_t type = A0;
	if (type != SCE_AUDIODEC_TYPE_AT9) {
		fprintf(stderr, "    audiodec: codec type %#x not implemented (ATRAC9 only) -- INVALID_TYPE\n", type);
		RET(SCE_AUDIODEC_ERROR_INVALID_TYPE); return;
	}
	pthread_mutex_lock(&mx); at9_initialised = 1; pthread_mutex_unlock(&mx);
	if (ad_log()) fprintf(stderr, "audiodec: InitLibrary ATRAC9\n");
	RET(0);
}
static void h_term(Cpu *c) {
	if (A0 != SCE_AUDIODEC_TYPE_AT9) { RET(SCE_AUDIODEC_ERROR_INVALID_TYPE); return; }
	pthread_mutex_lock(&mx); at9_initialised = 0; pthread_mutex_unlock(&mx);
	RET(0);
}

/* the ctrl's checks shared by every call: a pointer, the right size */
static uint32_t ctrl_check(uint32_t ctrl) {
	if (!ctrl) return SCE_AUDIODEC_ERROR_INVALID_PTR;
	if (RD32(ctrl + C_SIZE) != CTRL_SIZE) return SCE_AUDIODEC_ERROR_INVALID_SIZE;
	return 0;
}
static Decoder *by_handle(uint32_t ctrl) {   /* (under mx) */
	uint32_t h = RD32(ctrl + C_HANDLE);
	return h >= 1 && h <= MAX_DECODERS && decs[h - 1].used ? &decs[h - 1] : 0;
}

static void h_create(Cpu *c) {
	uint32_t ctrl = A0, type = A1, e = ctrl_check(ctrl);
	if (e) { RET(e); return; }
	if (type != SCE_AUDIODEC_TYPE_AT9) {
		fprintf(stderr, "    audiodec: CreateDecoder for codec type %#x not implemented (ATRAC9 only) -- INVALID_TYPE\n", type);
		RET(SCE_AUDIODEC_ERROR_INVALID_TYPE); return;
	}
	uint32_t info = RD32(ctrl + C_INFO);
	if (!info) { RET(SCE_AUDIODEC_ERROR_INVALID_PTR); return; }
	pthread_mutex_lock(&mx);
	if (!at9_initialised) { pthread_mutex_unlock(&mx); RET(SCE_AUDIODEC_ERROR_NOT_INITIALIZED); return; }
	int i = 0; while (i < MAX_DECODERS && decs[i].used) i++;
	if (i == MAX_DECODERS) { pthread_mutex_unlock(&mx); RET(SCE_AUDIODEC_ERROR_ALL_HANDLES_IN_USE); return; }
	Decoder *d = &decs[i];
	memset(d, 0, sizeof *d);
	memcpy(d->config, G(info + I_CONFIG), 4);
	if (at9_open(d)) {
		pthread_mutex_unlock(&mx);
		fprintf(stderr, "    audiodec: ATRAC9 configuration %02x %02x %02x %02x rejected\n", d->config[0], d->config[1], d->config[2], d->config[3]);
		RET(SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG); return;
	}
	d->used = 1;
	Atrac9CodecInfo ci = d->ci;
	pthread_mutex_unlock(&mx);
	/* the bit rate: a superframe's bits, superframes per second */
	uint32_t bitrate = (uint32_t)((uint64_t)ci.superframeSize * 8u * (uint32_t)ci.samplingRate / ((uint32_t)ci.frameSamples * (uint32_t)ci.framesInSuperframe));
	WR32(info + I_CH, ci.channels); WR32(info + I_BITRATE, bitrate); WR32(info + I_RATE, ci.samplingRate);
	WR32(info + I_SF_SIZE, ci.superframeSize); WR32(info + I_SF_FRAMES, ci.framesInSuperframe);
	WR32(ctrl + C_HANDLE, i + 1);
	WR32(ctrl + C_ES_MAX, (uint32_t)ci.superframeSize < SCE_AUDIODEC_AT9_MAX_ES_SIZE ? (uint32_t)ci.superframeSize : SCE_AUDIODEC_AT9_MAX_ES_SIZE);
	WR32(ctrl + C_PCM_MAX, (uint32_t)(ci.frameSamples * ci.channels * 2));
	if (ad_log()) fprintf(stderr, "audiodec: decoder %d: %d ch, %d Hz, superframe %d bytes / %d frames of %d samples, word length %u\n",
		i + 1, ci.channels, ci.samplingRate, ci.superframeSize, ci.framesInSuperframe, ci.frameSamples, RD32(ctrl + C_WORD));
	RET(0);
}

static void h_delete(Cpu *c) {
	uint32_t ctrl = A0, e = ctrl_check(ctrl);
	if (e) { RET(e); return; }
	pthread_mutex_lock(&mx);
	Decoder *d = by_handle(ctrl);
	if (!d) { pthread_mutex_unlock(&mx); RET(SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE); return; }
	if (d->at9) Atrac9ReleaseHandle(d->at9);
	memset(d, 0, sizeof *d);
	pthread_mutex_unlock(&mx);
	RET(0);
}

static void h_clear(Cpu *c) {
	uint32_t ctrl = A0, e = ctrl_check(ctrl);
	if (e) { RET(e); return; }
	if (!RD32(ctrl + C_HANDLE)) { RET(SCE_AUDIODEC_ERROR_INVALID_HANDLE); return; }
	pthread_mutex_lock(&mx);
	Decoder *d = by_handle(ctrl);
	if (!d) { pthread_mutex_unlock(&mx); RET(SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE); return; }
	/* a fresh decoder for the same configuration: no overlap history, at a superframe's start */
	Atrac9ReleaseHandle(d->at9); d->at9 = 0;
	int bad = at9_open(d);
	pthread_mutex_unlock(&mx);
	RET(bad ? SCE_AUDIODEC_ERROR_API_FAIL : 0);
}

/* nframes ATRAC9 frames from pEs, one after another, their PCM one after another at pPcm */
static uint32_t decode_frames(uint32_t ctrl, uint32_t nframes) {
	uint32_t e = ctrl_check(ctrl);
	if (e) return e;
	uint32_t es = RD32(ctrl + C_ES), pcm = RD32(ctrl + C_PCM);
	if (!es || !pcm) return SCE_AUDIODEC_ERROR_INVALID_PTR;
	pthread_mutex_lock(&mx);
	Decoder *d = by_handle(ctrl);
	if (!d) { pthread_mutex_unlock(&mx); return SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE; }
	uint32_t used_total = 0, given_total = 0, frame_bytes = (uint32_t)(d->ci.frameSamples * d->ci.channels * 2);
	for (uint32_t f = 0; f < nframes; f++) {
		int used = 0;
		int r = Atrac9Decode(d->at9, G(es + used_total), d->pcm, &used);
		if (r) {
			pthread_mutex_unlock(&mx);
			static int said; if (said++ < 4) fprintf(stderr, "    audiodec: ATRAC9 frame rejected (%d) at es %#x\n", r, es + used_total);
			WR32(ctrl + C_ES_USED, used_total); WR32(ctrl + C_PCM_GIVEN, given_total);
			return SCE_AUDIODEC_ERROR_API_FAIL;
		}
		uint32_t take = (uint32_t)used;
		d->sf_left -= used;
		if (++d->sf_frame == d->ci.framesInSuperframe) {   /* the superframe's padding goes with its last frame */
			if (d->sf_left > 0) take += (uint32_t)d->sf_left;
			d->sf_frame = 0; d->sf_left = d->ci.superframeSize;
		}
		memcpy(G(pcm + given_total), d->pcm, frame_bytes);
		used_total += take; given_total += frame_bytes;
	}
	pthread_mutex_unlock(&mx);
	WR32(ctrl + C_ES_USED, used_total); WR32(ctrl + C_PCM_GIVEN, given_total);
	return 0;
}
static void h_decode(Cpu *c) { RET(decode_frames(A0, 1)); }
static void h_decode_n(Cpu *c) { RET(decode_frames(A0, A1)); }

static void h_internal_error(Cpu *c) {
	uint32_t ctrl = A0, out = A1, e = ctrl_check(ctrl);
	if (e) { RET(e); return; }
	if (!out) { RET(SCE_AUDIODEC_ERROR_INVALID_PTR); return; }
	WR32(out, 0);
	RET(0);
}

const HleEntry hle_audiodec[] = {
	{"sceAudiodecInitLibrary", h_init}, {"sceAudiodecTermLibrary", h_term},
	{"sceAudiodecCreateDecoder", h_create}, {"sceAudiodecDeleteDecoder", h_delete},
	{"sceAudiodecDecode", h_decode}, {"sceAudiodecDecodeNFrames", h_decode_n},
	{"sceAudiodecClearContext", h_clear}, {"sceAudiodecGetInternalError", h_internal_error},
	{0, 0},
};
