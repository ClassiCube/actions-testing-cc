#include "Audio.h"
#include "String.h"
#include "Logger.h"
#include "Funcs.h"
#include "Errors.h"
#include "Utils.h"
#include "Platform.h"

void Audio_ApplyVolume(cc_int16* samples, int count, int volume) {
	int i;

	for (i = 0; i < (count & ~0x07); i += 8, samples += 8) {
		samples[0] = (samples[0] * volume / 100);
		samples[1] = (samples[1] * volume / 100);
		samples[2] = (samples[2] * volume / 100);
		samples[3] = (samples[3] * volume / 100);

		samples[4] = (samples[4] * volume / 100);
		samples[5] = (samples[5] * volume / 100);
		samples[6] = (samples[6] * volume / 100);
		samples[7] = (samples[7] * volume / 100);
	}

	for (; i < count; i++, samples++) {
		samples[0] = (samples[0] * volume / 100);
	}
}


void Audio_Warn(cc_result res, const char* action) {
	Logger_Warn(res, action, Audio_DescribeError);
}

/* Common/Base methods */
static void AudioBase_Clear(struct AudioContext* ctx);
static cc_bool AudioBase_AdjustSound(struct AudioContext* ctx, struct AudioData* data);
static void AudioBase_AllocChunks(int size, void** chunks, int numChunks);
static void AudioBase_FreeChunks(void** chunks, int numChunks);

/* achieve higher speed by playing samples at higher sample rate */
#define Audio_AdjustSampleRate(data) ((data->sampleRate * data->rate) / 100)

#if defined CC_BUILD_OPENAL
/*########################################################################################################################*
*------------------------------------------------------OpenAL backend-----------------------------------------------------*
*#########################################################################################################################*/
/* Simpler to just include subset of OpenAL actually use here instead of including */
/* === BEGIN OPENAL HEADERS === */
#if defined _WIN32
#define APIENTRY __cdecl
#else
#define APIENTRY
#endif
#define AL_NONE              0
#define AL_GAIN              0x100A
#define AL_SOURCE_STATE      0x1010
#define AL_PLAYING           0x1012
#define AL_BUFFERS_QUEUED    0x1015
#define AL_BUFFERS_PROCESSED 0x1016
#define AL_FORMAT_MONO16     0x1101
#define AL_FORMAT_STEREO16   0x1103

#define AL_INVALID_NAME      0xA001
#define AL_INVALID_ENUM      0xA002
#define AL_INVALID_VALUE     0xA003
#define AL_INVALID_OPERATION 0xA004
#define AL_OUT_OF_MEMORY     0xA005

typedef char ALboolean;
typedef int ALint;
typedef unsigned int ALuint;
typedef int ALsizei;
typedef int ALenum;

/* Apologies for the ugly dynamic symbol definitions here */
static ALenum (APIENTRY *_alGetError)(void);
static void   (APIENTRY *_alGenSources)(ALsizei n, ALuint* sources);
static void   (APIENTRY *_alDeleteSources)(ALsizei n, const ALuint* sources);
static void   (APIENTRY *_alGetSourcei)(ALuint source, ALenum param, ALint* value);
static void   (APIENTRY *_alSourcef)(ALuint source, ALenum param, float value);
static void   (APIENTRY *_alSourcePlay)(ALuint source); 
static void   (APIENTRY *_alSourceStop)(ALuint source);
static void   (APIENTRY *_alSourceQueueBuffers)(ALuint source, ALsizei nb, const ALuint* buffers);
static void   (APIENTRY *_alSourceUnqueueBuffers)(ALuint source, ALsizei nb, ALuint* buffers);
static void   (APIENTRY *_alGenBuffers)(ALsizei n, ALuint* buffers); 
static void   (APIENTRY *_alDeleteBuffers)(ALsizei n, const ALuint* buffers);
static void   (APIENTRY *_alBufferData)(ALuint buffer, ALenum format, const void* data, ALsizei size, ALsizei freq);

static void   (APIENTRY *_alDistanceModel)(ALenum distanceModel);
static void*     (APIENTRY *_alcCreateContext)(void* device, const ALint* attrlist);
static ALboolean (APIENTRY *_alcMakeContextCurrent)(void* context);
static void      (APIENTRY *_alcDestroyContext)(void* context);
static void*     (APIENTRY *_alcOpenDevice)(const char* devicename);
static ALboolean (APIENTRY *_alcCloseDevice)(void* device);
static ALenum    (APIENTRY *_alcGetError)(void* device);
/* === END OPENAL HEADERS === */

struct AudioContext {
	ALuint source;
	ALuint buffers[AUDIO_MAX_BUFFERS];
	ALuint freeIDs[AUDIO_MAX_BUFFERS];
	int count, free, sampleRate;
	ALenum format;
};
#define AUDIO_COMMON_ALLOC

static void* audio_device;
static void* audio_context;

#if defined CC_BUILD_WIN
static const cc_string alLib = String_FromConst("openal32.dll");
#elif defined CC_BUILD_MACOS
static const cc_string alLib = String_FromConst("/System/Library/Frameworks/OpenAL.framework/Versions/A/OpenAL");
#elif defined CC_BUILD_IOS
static const cc_string alLib = String_FromConst("/System/Library/Frameworks/OpenAL.framework/OpenAL");
#elif defined CC_BUILD_NETBSD
static const cc_string alLib = String_FromConst("/usr/pkg/lib/libopenal.so");
#elif defined CC_BUILD_BSD
static const cc_string alLib = String_FromConst("libopenal.so");
#else
static const cc_string alLib = String_FromConst("libopenal.so.1");
#endif

static cc_bool LoadALFuncs(void) {
	static const struct DynamicLibSym funcs[] = {
		DynamicLib_Sym(alcCreateContext),  DynamicLib_Sym(alcMakeContextCurrent),
		DynamicLib_Sym(alcDestroyContext), DynamicLib_Sym(alcOpenDevice),
		DynamicLib_Sym(alcCloseDevice),    DynamicLib_Sym(alcGetError),

		DynamicLib_Sym(alGetError),        
		DynamicLib_Sym(alGenSources),      DynamicLib_Sym(alDeleteSources),
		DynamicLib_Sym(alGetSourcei),      DynamicLib_Sym(alSourcef),
		DynamicLib_Sym(alSourcePlay),      DynamicLib_Sym(alSourceStop),
		DynamicLib_Sym(alSourceQueueBuffers), DynamicLib_Sym(alSourceUnqueueBuffers),
		DynamicLib_Sym(alGenBuffers),      DynamicLib_Sym(alDeleteBuffers),
		DynamicLib_Sym(alBufferData),      DynamicLib_Sym(alDistanceModel)
	};
	void* lib;
	
	return DynamicLib_LoadAll(&alLib, funcs, Array_Elems(funcs), &lib);
}

static cc_result CreateALContext(void) {
	ALenum err;
	audio_device = _alcOpenDevice(NULL);
	if ((err = _alcGetError(audio_device))) return err;
	if (!audio_device)  return AL_ERR_INIT_DEVICE;

	audio_context = _alcCreateContext(audio_device, NULL);
	if ((err = _alcGetError(audio_device))) return err;
	if (!audio_context) return AL_ERR_INIT_CONTEXT;

	_alcMakeContextCurrent(audio_context);
	return _alcGetError(audio_device);
}

cc_bool AudioBackend_Init(void) {
	static const cc_string msg = String_FromConst("Failed to init OpenAL. No audio will play.");
	cc_result res;
	if (audio_device) return true;
	if (!LoadALFuncs()) { Logger_WarnFunc(&msg); return false; }

	res = CreateALContext();
	if (res) { Audio_Warn(res, "initing OpenAL"); return false; }
	return true;
}

void AudioBackend_Tick(void) { }

void AudioBackend_Free(void) {
	if (!audio_device) return;
	_alcMakeContextCurrent(NULL);

	if (audio_context) _alcDestroyContext(audio_context);
	if (audio_device)  _alcCloseDevice(audio_device);

	audio_context = NULL;
	audio_device  = NULL;
}

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	ALenum i, err;
	_alDistanceModel(AL_NONE);
	ctx->source = 0;
	ctx->count  = buffers;
	
	_alGetError(); /* Reset error state */
	_alGenSources(1, &ctx->source);
	if ((err = _alGetError())) return err;

	_alGenBuffers(buffers, ctx->buffers);
	if ((err = _alGetError())) return err;

	for (i = 0; i < buffers; i++) {
		ctx->freeIDs[i] = ctx->buffers[i];
	}
	ctx->free = buffers;
	return 0;
}

static void Audio_Stop(struct AudioContext* ctx) {
	_alSourceStop(ctx->source);
}

static void Audio_Reset(struct AudioContext* ctx) {
	_alDeleteSources(1,          &ctx->source);
	_alDeleteBuffers(ctx->count, ctx->buffers);
	ctx->source = 0;
}

static void ClearFree(struct AudioContext* ctx) {
	int i;
	for (i = 0; i < AUDIO_MAX_BUFFERS; i++) {
		ctx->freeIDs[i] = 0;
	}
	ctx->free = 0;
}

void Audio_Close(struct AudioContext* ctx) {
	if (ctx->source) {
		Audio_Stop(ctx);
		Audio_Reset(ctx);
		_alGetError(); /* Reset error state */
	}
	ClearFree(ctx);
	ctx->count = 0;
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	ctx->sampleRate = sampleRate;

	if (channels == 1) {
		ctx->format = AL_FORMAT_MONO16;
	} else if (channels == 2) {
		ctx->format = AL_FORMAT_STEREO16;
	} else {
		return ERR_INVALID_ARGUMENT;
	}
	return 0;
}

cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 size) {
	ALuint buffer;
	ALenum err;
	
	if (!ctx->free) return ERR_INVALID_ARGUMENT;
	buffer = ctx->freeIDs[--ctx->free];
	_alGetError(); /* Reset error state */

	_alBufferData(buffer, ctx->format, chunk, size, ctx->sampleRate);
	if ((err = _alGetError())) return err;
	_alSourceQueueBuffers(ctx->source, 1, &buffer);
	if ((err = _alGetError())) return err;
	return 0;
}

cc_result Audio_Play(struct AudioContext* ctx) {
	_alSourcePlay(ctx->source);
	return _alGetError();
}

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	ALint processed = 0;
	ALuint buffer;
	ALenum err;

	*inUse = 0;
	if (!ctx->source) return 0;

	_alGetError(); /* Reset error state */
	_alGetSourcei(ctx->source, AL_BUFFERS_PROCESSED, &processed);
	if ((err = _alGetError())) return err;

	if (processed > 0) {
		_alSourceUnqueueBuffers(ctx->source, 1, &buffer);
		if ((err = _alGetError())) return err;

		ctx->freeIDs[ctx->free++] = buffer;
	}
	*inUse = ctx->count - ctx->free; return 0;
}

cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	/* Channels/Sample rate is per buffer, not a per source property */
	return true;
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	cc_result res;
	
	data->sampleRate = Audio_AdjustSampleRate(data);
	_alSourcef(ctx->source, AL_GAIN, data->volume / 100.0f);
	_alGetError(); /* Reset error state */

	if ((res = Audio_SetFormat(ctx, data->channels, data->sampleRate))) return res;
	if ((res = Audio_QueueChunk(ctx, data->data,     data->size)))      return res;
	if ((res = Audio_Play(ctx))) return res;
	return 0;
}

static const char* GetError(cc_result res) {
	switch (res) {
	case AL_ERR_INIT_CONTEXT:  return "Failed to init OpenAL context";
	case AL_ERR_INIT_DEVICE:   return "Failed to init OpenAL device";
	case AL_INVALID_NAME:      return "Invalid parameter name";
	case AL_INVALID_ENUM:      return "Invalid parameter";
	case AL_INVALID_VALUE:     return "Invalid parameter value";
	case AL_INVALID_OPERATION: return "Invalid operation";
	case AL_OUT_OF_MEMORY:     return "OpenAL out of memory";
	}
	return NULL;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	const char* err = GetError(res);
	if (err) String_AppendConst(dst, err);
	return err != NULL;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	AudioBase_AllocChunks(size, chunks, numChunks);
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	AudioBase_FreeChunks(chunks, numChunks);
}
#elif defined CC_BUILD_WINMM
/*########################################################################################################################*
*------------------------------------------------------WinMM backend------------------------------------------------------*
*#########################################################################################################################*/
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOMCX
#define NOIME
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>

/* === BEGIN mmsyscom.h === */
#define CALLBACK_NULL  0x00000000l
typedef UINT           MMRESULT;
#define WINMMAPI       DECLSPEC_IMPORT
#define MMSYSERR_BADDEVICEID 2
/* === BEGIN mmeapi.h === */
typedef struct WAVEHDR_ {
	LPSTR       lpData;
	DWORD       dwBufferLength;
	DWORD       dwBytesRecorded;
	DWORD_PTR   dwUser;
	DWORD       dwFlags;
	DWORD       dwLoops;
	struct WAVEHDR_* lpNext;
	DWORD_PTR   reserved;
} WAVEHDR;

typedef struct WAVEFORMATEX_ {
	WORD  wFormatTag;
	WORD  nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD  nBlockAlign;
	WORD  wBitsPerSample;
	WORD  cbSize;
} WAVEFORMATEX;
typedef void* HWAVEOUT;

#define WAVE_MAPPER     ((UINT)-1)
#define WAVE_FORMAT_PCM 1
#define WHDR_DONE       0x00000001
#define WHDR_PREPARED   0x00000002

WINMMAPI MMRESULT WINAPI waveOutOpen(HWAVEOUT* phwo, UINT deviceID, const WAVEFORMATEX* fmt, DWORD_PTR callback, DWORD_PTR instance, DWORD flags);
WINMMAPI MMRESULT WINAPI waveOutClose(HWAVEOUT hwo);
WINMMAPI MMRESULT WINAPI waveOutPrepareHeader(HWAVEOUT hwo, WAVEHDR* hdr, UINT hdrSize);
WINMMAPI MMRESULT WINAPI waveOutUnprepareHeader(HWAVEOUT hwo, WAVEHDR* hdr, UINT hdrSize);
WINMMAPI MMRESULT WINAPI waveOutWrite(HWAVEOUT hwo, WAVEHDR* hdr, UINT hdrSize);
WINMMAPI MMRESULT WINAPI waveOutReset(HWAVEOUT hwo);
WINMMAPI MMRESULT WINAPI waveOutGetErrorTextA(MMRESULT err, LPSTR text, UINT textLen);
WINMMAPI UINT     WINAPI waveOutGetNumDevs(void);
/* === END mmeapi.h === */

struct AudioContext {
	HWAVEOUT handle;
	WAVEHDR headers[AUDIO_MAX_BUFFERS];
	int count, channels, sampleRate;
	cc_uint32 _tmpSize; void* _tmpData;
};
#define AUDIO_COMMON_VOLUME
#define AUDIO_COMMON_ALLOC

cc_bool AudioBackend_Init(void) { return true; }
void AudioBackend_Tick(void) { }
void AudioBackend_Free(void) { }

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	int i;
	for (i = 0; i < buffers; i++) {
		ctx->headers[i].dwFlags = WHDR_DONE;
	}
	ctx->count = buffers;
	return 0;
}

static void Audio_Stop(struct AudioContext* ctx) {
	waveOutReset(ctx->handle);
}

static cc_result Audio_Reset(struct AudioContext* ctx) {
	cc_result res;
	if (!ctx->handle) return 0;

	res = waveOutClose(ctx->handle);
	ctx->handle = NULL;
	return res;
}

void Audio_Close(struct AudioContext* ctx) {
	int inUse;
	if (ctx->handle) {
		Audio_Stop(ctx);
		Audio_Poll(ctx, &inUse); /* unprepare buffers */
		Audio_Reset(ctx);
	}
	AudioBase_Clear(ctx);
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	WAVEFORMATEX fmt;
	cc_result res;
	int sampleSize;

	if (ctx->channels == channels && ctx->sampleRate == sampleRate) return 0;
	ctx->channels   = channels;
	ctx->sampleRate = sampleRate;
	
	sampleSize = channels * 2; /* 16 bits per sample / 8 */
	if ((res = Audio_Reset(ctx))) return res;

	fmt.wFormatTag      = WAVE_FORMAT_PCM;
	fmt.nChannels       = channels;
	fmt.nSamplesPerSec  = sampleRate;
	fmt.nAvgBytesPerSec = sampleRate * sampleSize;
	fmt.nBlockAlign     = sampleSize;
	fmt.wBitsPerSample  = 16;
	fmt.cbSize          = 0;
	
	res = waveOutOpen(&ctx->handle, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
	/* Show a better error message when no audio output devices connected than */
	/*  "A device ID has been used that is out of range for your system" */
	if (res == MMSYSERR_BADDEVICEID && waveOutGetNumDevs() == 0)
		return ERR_NO_AUDIO_OUTPUT;
	return res;
}

cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 dataSize) {
	cc_result res = 0;
	WAVEHDR* hdr;
	int i;

	for (i = 0; i < ctx->count; i++) {
		hdr = &ctx->headers[i];
		if (!(hdr->dwFlags & WHDR_DONE)) continue;

		Mem_Set(hdr, 0, sizeof(WAVEHDR));
		hdr->lpData         = (LPSTR)chunk;
		hdr->dwBufferLength = dataSize;
		hdr->dwLoops        = 1;
		
		if ((res = waveOutPrepareHeader(ctx->handle, hdr, sizeof(WAVEHDR)))) return res;
		if ((res = waveOutWrite(ctx->handle, hdr, sizeof(WAVEHDR))))         return res;
		return 0;
	}
	/* tried to queue data without polling for free buffers first */
	return ERR_INVALID_ARGUMENT;
}

cc_result Audio_Play(struct AudioContext* ctx) { return 0; }

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	cc_result res = 0;
	WAVEHDR* hdr;
	int i, count = 0;

	for (i = 0; i < ctx->count; i++) {
		hdr = &ctx->headers[i];
		if (!(hdr->dwFlags & WHDR_DONE)) { count++; continue; }
	
		if (!(hdr->dwFlags & WHDR_PREPARED)) continue;
		/* unprepare this WAVEHDR so it can be reused */
		res = waveOutUnprepareHeader(ctx->handle, hdr, sizeof(WAVEHDR));
		if (res) break;
	}

	*inUse = count; return res;
}


cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	int channels   = data->channels;
	int sampleRate = Audio_AdjustSampleRate(data);
	return !ctx->channels || (ctx->channels == channels && ctx->sampleRate == sampleRate);
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	cc_bool ok = AudioBase_AdjustSound(ctx, data);
	cc_result res;
	if (!ok) return ERR_OUT_OF_MEMORY; 
	data->sampleRate = Audio_AdjustSampleRate(data);

	if ((res = Audio_SetFormat(ctx, data->channels, data->sampleRate))) return res;
	if ((res = Audio_QueueChunk(ctx, data->data,    data->size)))       return res;
	return 0;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	char buffer[NATIVE_STR_LEN] = { 0 };
	waveOutGetErrorTextA(res, buffer, NATIVE_STR_LEN);

	if (!buffer[0]) return false;
	String_AppendConst(dst, buffer);
	return true;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	AudioBase_AllocChunks(size, chunks, numChunks);
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	AudioBase_FreeChunks(chunks, numChunks);
}
#elif defined CC_BUILD_OPENSLES
/*########################################################################################################################*
*----------------------------------------------------OpenSL ES backend----------------------------------------------------*
*#########################################################################################################################*/
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
static SLObjectItf slEngineObject;
static SLEngineItf slEngineEngine;
static SLObjectItf slOutputObject;

struct AudioContext {
	int count, channels, sampleRate;
	SLObjectItf playerObject;
	SLPlayItf   playerPlayer;
	SLBufferQueueItf playerQueue;
	SLPlaybackRateItf playerRate;
	cc_uint32 _tmpSize; void* _tmpData;
};
#define AUDIO_COMMON_VOLUME
#define AUDIO_COMMON_ALLOC

static SLresult (SLAPIENTRY *_slCreateEngine)(SLObjectItf* engine, SLuint32 numOptions, const SLEngineOption* engineOptions,
							SLuint32 numInterfaces, const SLInterfaceID* interfaceIds, const SLboolean* interfaceRequired);
static SLInterfaceID* _SL_IID_NULL;
static SLInterfaceID* _SL_IID_PLAY;
static SLInterfaceID* _SL_IID_ENGINE;
static SLInterfaceID* _SL_IID_BUFFERQUEUE;
static SLInterfaceID* _SL_IID_PLAYBACKRATE;
static const cc_string slLib = String_FromConst("libOpenSLES.so");

static cc_bool LoadSLFuncs(void) {
	static const struct DynamicLibSym funcs[] = {
		DynamicLib_Sym(slCreateEngine),     DynamicLib_Sym(SL_IID_NULL),
		DynamicLib_Sym(SL_IID_PLAY),        DynamicLib_Sym(SL_IID_ENGINE),
		DynamicLib_Sym(SL_IID_BUFFERQUEUE), DynamicLib_Sym(SL_IID_PLAYBACKRATE)
	};
	void* lib;
	
	return DynamicLib_LoadAll(&slLib, funcs, Array_Elems(funcs), &lib);
}

cc_bool AudioBackend_Init(void) {
	static const cc_string msg = String_FromConst("Failed to init OpenSLES. No audio will play.");
	SLInterfaceID ids[1];
	SLboolean req[1];
	SLresult res;

	if (slEngineObject) return true;
	if (!LoadSLFuncs()) { Logger_WarnFunc(&msg); return false; }
	
	/* mixer doesn't use any effects */
	ids[0] = *_SL_IID_NULL; 
	req[0] = SL_BOOLEAN_FALSE;
	
	res = _slCreateEngine(&slEngineObject, 0, NULL, 0, NULL, NULL);
	if (res) { Audio_Warn(res, "creating OpenSL ES engine"); return false; }

	res = (*slEngineObject)->Realize(slEngineObject, SL_BOOLEAN_FALSE);
	if (res) { Audio_Warn(res, "realising OpenSL ES engine"); return false; }

	res = (*slEngineObject)->GetInterface(slEngineObject, *_SL_IID_ENGINE, &slEngineEngine);
	if (res) { Audio_Warn(res, "initing OpenSL ES engine"); return false; }

	res = (*slEngineEngine)->CreateOutputMix(slEngineEngine, &slOutputObject, 1, ids, req);
	if (res) { Audio_Warn(res, "creating OpenSL ES mixer"); return false; }

	res = (*slOutputObject)->Realize(slOutputObject, SL_BOOLEAN_FALSE);
	if (res) { Audio_Warn(res, "realising OpenSL ES mixer"); return false; }

	return true;
}

void AudioBackend_Tick(void) { }

void AudioBackend_Free(void) {
	if (slOutputObject) {
		(*slOutputObject)->Destroy(slOutputObject);
		slOutputObject = NULL;
	}
	if (slEngineObject) {
		(*slEngineObject)->Destroy(slEngineObject);
		slEngineObject = NULL;
		slEngineEngine = NULL;
	}
}

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	ctx->count = buffers;
	return 0;
}

static void Audio_Stop(struct AudioContext* ctx) {
	if (!ctx->playerPlayer) return;

	(*ctx->playerQueue)->Clear(ctx->playerQueue);
	(*ctx->playerPlayer)->SetPlayState(ctx->playerPlayer, SL_PLAYSTATE_STOPPED);
}

static void Audio_Reset(struct AudioContext* ctx) {
	SLObjectItf playerObject = ctx->playerObject;
	if (!playerObject) return;

	(*playerObject)->Destroy(playerObject);
	ctx->playerObject = NULL;
	ctx->playerPlayer = NULL;
	ctx->playerQueue  = NULL;
	ctx->playerRate   = NULL;
}

void Audio_Close(struct AudioContext* ctx) {
	Audio_Stop(ctx);
	Audio_Reset(ctx);
	AudioBase_Clear(ctx);
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	SLDataLocator_AndroidSimpleBufferQueue input;
	SLDataLocator_OutputMix output;
	SLObjectItf playerObject;
	SLDataFormat_PCM fmt;
	SLInterfaceID ids[3];
	SLboolean req[3];
	SLDataSource src;
	SLDataSink dst;
	cc_result res;

	if (ctx->channels == channels && ctx->sampleRate == sampleRate) return 0;
	ctx->channels   = channels;
	ctx->sampleRate = sampleRate;
	Audio_Reset(ctx);

	fmt.formatType     = SL_DATAFORMAT_PCM;
	fmt.numChannels    = channels;
	fmt.samplesPerSec  = sampleRate * 1000;
	fmt.bitsPerSample  = SL_PCMSAMPLEFORMAT_FIXED_16;
	fmt.containerSize  = SL_PCMSAMPLEFORMAT_FIXED_16;
	fmt.channelMask    = 0;
	fmt.endianness     = SL_BYTEORDER_LITTLEENDIAN;

	input.locatorType  = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
	input.numBuffers   = ctx->count;
	output.locatorType = SL_DATALOCATOR_OUTPUTMIX;
	output.outputMix   = slOutputObject;

	src.pLocator = &input;
	src.pFormat  = &fmt;
	dst.pLocator = &output;
	dst.pFormat  = NULL;

	ids[0] = *_SL_IID_BUFFERQUEUE;  req[0] = SL_BOOLEAN_TRUE;
	ids[1] = *_SL_IID_PLAY;         req[1] = SL_BOOLEAN_TRUE;
	ids[2] = *_SL_IID_PLAYBACKRATE; req[2] = SL_BOOLEAN_TRUE;

	res = (*slEngineEngine)->CreateAudioPlayer(slEngineEngine, &playerObject, &src, &dst, 3, ids, req);
	ctx->playerObject = playerObject;
	if (res) return res;

	if ((res = (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE)))                               return res;
	if ((res = (*playerObject)->GetInterface(playerObject, *_SL_IID_PLAY,         &ctx->playerPlayer))) return res;
	if ((res = (*playerObject)->GetInterface(playerObject, *_SL_IID_BUFFERQUEUE,  &ctx->playerQueue)))  return res;
	if ((res = (*playerObject)->GetInterface(playerObject, *_SL_IID_PLAYBACKRATE, &ctx->playerRate)))   return res;
	return 0;
}

cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 size) {
	return (*ctx->playerQueue)->Enqueue(ctx->playerQueue, chunk, size);
}

cc_result Audio_Pause(struct AudioContext* ctx) {
	return (*ctx->playerPlayer)->SetPlayState(ctx->playerPlayer, SL_PLAYSTATE_PAUSED);
}

cc_result Audio_Play(struct AudioContext* ctx) {
	return (*ctx->playerPlayer)->SetPlayState(ctx->playerPlayer, SL_PLAYSTATE_PLAYING);
}

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	SLBufferQueueState state = { 0 };
	cc_result res = 0;

	if (ctx->playerQueue) {
		res = (*ctx->playerQueue)->GetState(ctx->playerQueue, &state);	
	}
	*inUse  = state.count;
	return res;
}

cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	return !ctx->channels || (ctx->channels == data->channels && ctx->sampleRate == data->sampleRate);
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	cc_bool ok = AudioBase_AdjustSound(ctx, data);
	cc_result res;
	if (!ok) return ERR_OUT_OF_MEMORY; 

	if ((res = Audio_SetFormat(ctx, data->channels, data->sampleRate))) return res;
	/* rate is in milli, so 1000 = normal rate */
	if ((res = (*ctx->playerRate)->SetRate(ctx->playerRate, data->rate * 10))) return res;

	if ((res = Audio_QueueChunk(ctx, data->data, data->size))) return res;
	if ((res = Audio_Play(ctx))) return res;
	return 0;
}

static const char* GetError(cc_result res) {
	switch (res) {
	case SL_RESULT_PRECONDITIONS_VIOLATED: return "Preconditions violated";
	case SL_RESULT_PARAMETER_INVALID:   return "Invalid parameter";
	case SL_RESULT_MEMORY_FAILURE:      return "Memory failure";
	case SL_RESULT_RESOURCE_ERROR:      return "Resource error";
	case SL_RESULT_RESOURCE_LOST:       return "Resource lost";
	case SL_RESULT_IO_ERROR:            return "I/O error";
	case SL_RESULT_BUFFER_INSUFFICIENT: return "Insufficient buffer";
	case SL_RESULT_CONTENT_CORRUPTED:   return "Content corrupted";
	case SL_RESULT_CONTENT_UNSUPPORTED: return "Content unsupported";
	case SL_RESULT_CONTENT_NOT_FOUND:   return "Content not found";
	case SL_RESULT_PERMISSION_DENIED:   return "Permission denied";
	case SL_RESULT_FEATURE_UNSUPPORTED: return "Feature unsupported";
	case SL_RESULT_INTERNAL_ERROR:      return "Internal error";
	case SL_RESULT_UNKNOWN_ERROR:       return "Unknown error";
	case SL_RESULT_OPERATION_ABORTED:   return "Operation aborted";
	case SL_RESULT_CONTROL_LOST:        return "Control lost";
	}
	return NULL;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	const char* err = GetError(res);
	if (err) String_AppendConst(dst, err);
	return err != NULL;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	AudioBase_AllocChunks(size, chunks, numChunks);
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	AudioBase_FreeChunks(chunks, numChunks);
}
#elif defined CC_BUILD_3DS
/*########################################################################################################################*
*-------------------------------------------------------3DS backend-------------------------------------------------------*
*#########################################################################################################################*/
#include <3ds.h>
struct AudioContext {
	int chanID, count;
	ndspWaveBuf bufs[AUDIO_MAX_BUFFERS];
	int sampleRate;
	cc_bool stereo;
};
static int channelIDs;

cc_bool AudioBackend_Init(void) {
	int result = ndspInit();
	Platform_Log1("NDSP_INIT: %i", &result);
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	return result == 0; 
}

void AudioBackend_Tick(void) { }

void AudioBackend_Free(void) { }

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	int chanID = -1;
	
	for (int i = 0; i < 24; i++)
	{
		// channel in use
		if (channelIDs & (1 << i)) continue;
		
		chanID = i; break;
	}
	if (chanID == -1) return ERR_INVALID_ARGUMENT;
	
	channelIDs |= (1 << chanID);
	ctx->count  = buffers;
	ctx->chanID = chanID;

	ndspChnSetInterp(ctx->chanID, NDSP_INTERP_LINEAR);
	return 0;
}

void Audio_Close(struct AudioContext* ctx) {
	if (ctx->count) {
		ndspChnWaveBufClear(ctx->chanID);
		channelIDs &= ~(1 << ctx->chanID);
	}
	ctx->count = 0;
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	ctx->stereo = (channels == 2);
	int fmt = ctx->stereo ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
	
	ndspChnSetFormat(ctx->chanID, fmt);
	ndspChnSetRate(ctx->chanID, sampleRate);
	return 0;
}

cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 dataSize) {
	ndspWaveBuf* buf;

	// DSP audio buffers must be aligned to a multiple of 0x80, according to the example code I could find.
	if (((uintptr_t)chunk & 0x7F) != 0) {
		Platform_Log1("Audio_QueueData: tried to queue buffer with non-aligned audio buffer 0x%x\n", &chunk);
	}
	if ((dataSize & 0x7F) != 0) {
		Platform_Log1("Audio_QueueData: unaligned audio data size 0x%x\n", &dataSize);
	}

	for (int i = 0; i < ctx->count; i++) 
	{
		buf = &ctx->bufs[i];
		//Platform_Log2("QUEUE_CHUNK: %i = %i", &ctx->chanID, &buf->status);
		if (buf->status == NDSP_WBUF_QUEUED || buf->status == NDSP_WBUF_PLAYING)
			continue;

		buf->data_pcm16 = chunk;
		buf->nsamples   = dataSize / (sizeof(cc_int16) * (ctx->stereo ? 2 : 1));
		//Platform_Log1("PLAYING ON: %i", &ctx->chanID);
		DSP_FlushDataCache(buf->data_pcm16, dataSize);
		ndspChnWaveBufAdd(ctx->chanID, buf);
		return 0;
	}
	// tried to queue data without polling for free buffers first
	return ERR_INVALID_ARGUMENT;
}

cc_result Audio_Play(struct AudioContext* ctx) { return 0; }

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	ndspWaveBuf* buf;
	int count = 0;

	for (int i = 0; i < ctx->count; i++) 
	{
		buf = &ctx->bufs[i];
		//Platform_Log2("CHECK_CHUNK: %i = %i", &ctx->chanID, &buf->status);
		if (buf->status == NDSP_WBUF_QUEUED || buf->status == NDSP_WBUF_PLAYING) { 
			count++; continue; 
		}
	}

	*inUse = count; 
	return 0;
}


cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	return true;
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	float mix[12] = { 0 };
 	mix[0] = data->volume / 100.0f;
 	mix[1] = data->volume / 100.0f;
 	
 	ndspChnSetMix(ctx->chanID, mix);
	data->sampleRate = Audio_AdjustSampleRate(data);
	cc_result res;

	if ((res = Audio_SetFormat(ctx, data->channels, data->sampleRate))) return res;
	if ((res = Audio_QueueChunk(ctx, data->data,    data->size)))       return res;
	return 0;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	return false;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	size = (size + 0x7F) & ~0x7F;  // round up to nearest multiple of 0x80
	cc_uint8* dst = linearAlloc(size * numChunks);
	for (int i = 0; i < numChunks; i++) {
		chunks[i] = dst ? (dst + size * i) : NULL;
	}
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	linearFree(chunks[0]);
}
#elif defined CC_BUILD_SWITCH
/*########################################################################################################################*
*-----------------------------------------------------Switch backend------------------------------------------------------*
*#########################################################################################################################*/
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>

struct AudioContext {
	int chanID, count;
	AudioDriverWaveBuf bufs[AUDIO_MAX_BUFFERS];
	int channels, sampleRate;
};
struct AudioMemPools {
	void* chunk;
	int mpid;
};

static int channelIDs;
static struct AudioMemPools audioPools[64];
AudioDriver drv;
bool switchAudio = false;
void* audrv_mutex;

cc_bool AudioBackend_Init(void) {
	if (switchAudio) return true;
	switchAudio = true;

	if (!audrv_mutex) audrv_mutex = Mutex_Create();

	Mem_Set(audioPools, 0, sizeof(audioPools));

	static const AudioRendererConfig arConfig =
    {
        .output_rate     = AudioRendererOutputRate_48kHz,
        .num_voices      = 24,
        .num_effects     = 0,
        .num_sinks       = 1,
        .num_mix_objs    = 1,
        .num_mix_buffers = 2,
    };

	audrenInitialize(&arConfig);
	audrvCreate(&drv, &arConfig, 2);

	static const u8 sink_channels[] = { 0, 1 };
	/*int sink =*/ audrvDeviceSinkAdd(&drv, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

	audrvUpdate(&drv);

	Result res = audrenStartAudioRenderer();
	
	return R_SUCCEEDED(res); 
}

void AudioBackend_Tick(void) {
	Mutex_Lock(audrv_mutex);
	if (switchAudio) audrvUpdate(&drv);
	Mutex_Unlock(audrv_mutex);
}

void AudioBackend_Free(void) {
	for (int i = 0; i < 24; i++) {
		audrvVoiceStop(&drv, i);
	}
	audrvUpdate(&drv);
}

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	int chanID = -1;
	
	for (int i = 0; i < 24; i++)
	{
		// channel in use
		if (channelIDs & (1 << i)) continue;
		
		chanID = i; break;
	}
	if (chanID == -1) return ERR_INVALID_ARGUMENT;
	
	channelIDs |= (1 << chanID);
	ctx->count  = buffers;
	ctx->chanID = chanID;
	return 0;
}

void Audio_Close(struct AudioContext* ctx) {
	if (ctx->count) {
		audrvVoiceStop(&drv, ctx->chanID);
		channelIDs &= ~(1 << ctx->chanID);
	}
	ctx->count = 0;
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	ctx->channels   = channels;
	ctx->sampleRate = sampleRate;

	audrvVoiceStop(&drv, ctx->chanID);
	audrvVoiceInit(&drv, ctx->chanID, ctx->channels, PcmFormat_Int16, ctx->sampleRate);
	audrvVoiceSetDestinationMix(&drv, ctx->chanID, AUDREN_FINAL_MIX_ID);

	if (channels == 1) {
		// mono
		audrvVoiceSetMixFactor(&drv, ctx->chanID, 1.0f, 0, 0);
		audrvVoiceSetMixFactor(&drv, ctx->chanID, 1.0f, 0, 1);
	}
	else {
		// stereo
		audrvVoiceSetMixFactor(&drv, ctx->chanID, 1.0f, 0, 0);
		audrvVoiceSetMixFactor(&drv, ctx->chanID, 0.0f, 0, 1);
		audrvVoiceSetMixFactor(&drv, ctx->chanID, 0.0f, 1, 0);
		audrvVoiceSetMixFactor(&drv, ctx->chanID, 1.0f, 1, 1);
	}

	return 0;
}

cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 dataSize) {
	AudioDriverWaveBuf* buf;

	// Audio buffers must be aligned to a multiple of 0x1000, according to libnx example code
	if (((uintptr_t)chunk & 0xFFF) != 0) {
		Platform_Log1("Audio_QueueData: tried to queue buffer with non-aligned audio buffer 0x%x\n", &chunk);
	}
	if ((dataSize & 0xFFF) != 0) {
		Platform_Log1("Audio_QueueData: unaligned audio data size 0x%x\n", &dataSize);
	}


	for (int i = 0; i < ctx->count; i++) 
	{
		buf = &ctx->bufs[i];
		int state = buf->state;
		cc_uint32 size = dataSize;
		cc_uint32 endOffset = dataSize / (sizeof(cc_int16) * ((ctx->channels == 2) ? 2 : 1));

		if (state == AudioDriverWaveBufState_Queued || state == AudioDriverWaveBufState_Playing || state == AudioDriverWaveBufState_Waiting)
			continue;

		buf->data_pcm16 = chunk;
		buf->size       = size;
		buf->start_sample_offset = 0;
		buf->end_sample_offset = endOffset;

		Mutex_Lock(audrv_mutex);
		audrvVoiceAddWaveBuf(&drv, ctx->chanID, buf);
		Mutex_Unlock(audrv_mutex);

		return 0;
	}

	// tried to queue data without polling for free buffers first
	return ERR_INVALID_ARGUMENT;
}

cc_result Audio_Play(struct AudioContext* ctx) {
	audrvVoiceStart(&drv, ctx->chanID);
	return 0;
}

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	AudioDriverWaveBuf* buf;
	int count = 0;

	for (int i = 0; i < ctx->count; i++) 
	{
		buf = &ctx->bufs[i];
		if (buf->state == AudioDriverWaveBufState_Queued || buf->state == AudioDriverWaveBufState_Playing || buf->state == AudioDriverWaveBufState_Waiting) {
			count++; continue;
		}
	}

	*inUse = count;
	return 0;
}


cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	return true;
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	data->sampleRate = Audio_AdjustSampleRate(data);
	cc_result res;

	if ((res = Audio_SetFormat(ctx, data->channels, data->sampleRate))) return res;
	if ((res = Audio_QueueChunk(ctx, data->data,    data->size)))       return res;

	audrvVoiceSetVolume(&drv, ctx->chanID, data->volume/100.f);
	if ((res = Audio_Play(ctx))) return res;

	return 0;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	return false;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	size = (size + 0xFFF) & ~0xFFF;  // round up to nearest multiple of 0x1000
	void* dst = aligned_alloc(0x1000, size*numChunks);

	for (int i = 0; i < numChunks; i++) {
		chunks[i] = dst + size * i;

		int mpid = audrvMemPoolAdd(&drv, dst + size * i, size);
		audrvMemPoolAttach(&drv, mpid);

		for (int j = 0; j < 64; j++) {
			if (audioPools[j].chunk != NULL) continue;
			audioPools[j].chunk = dst;
			audioPools[j].mpid = mpid;
			break;
		}
	}
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	// remove memory pool from audren
	for (int i=0; i<numChunks; i++) {
		for (int j=0; j<64; j++) {
			if (audioPools[j].chunk == chunks[0]) {
				audrvMemPoolDetach(&drv, audioPools[j].mpid);
				audrvMemPoolRemove(&drv, audioPools[j].mpid);
				Mem_Set(&audioPools[j], 0, sizeof(struct AudioMemPools));
				break;
			}
		}
	}

	free(chunks[0]);
}
#elif defined CC_BUILD_DREAMCAST
/*########################################################################################################################*
*----------------------------------------------------Dreamcast backend----------------------------------------------------*
*#########################################################################################################################*/
#include <kos.h>
/* TODO needs way more testing, especially with sounds */

struct AudioBuffer {
	int available;
	int bytesLeft;
	void* samples;
};

struct AudioContext {
	int bufHead, channels;
	snd_stream_hnd_t hnd;
	struct AudioBuffer bufs[AUDIO_MAX_BUFFERS];
	int count, sampleRate;
};

cc_bool AudioBackend_Init(void) {
	return snd_stream_init() == 0;
}

void AudioBackend_Tick(void) {
    // TODO is this really threadsafe with music? should this be done in Audio_Poll instead?
    for (int i = 0; i < SND_STREAM_MAX; i++)
        snd_stream_poll(i);
}

void AudioBackend_Free(void) { 
	snd_stream_shutdown();
}

static void* AudioCallback(snd_stream_hnd_t hnd, int smp_req, int *smp_recv) {
	struct AudioContext* ctx = snd_stream_get_userdata(hnd);
	struct AudioBuffer* buf  = &ctx->bufs[ctx->bufHead]; 
	
	int samples = min(buf->bytesLeft, smp_req);
	*smp_recv   = samples;
	void* ptr   = buf->samples;
	
	buf->samples   += samples;
	buf->bytesLeft -= samples;
	
	if (buf->bytesLeft == 0) {
		ctx->bufHead   = (ctx->bufHead + 1) % ctx->count;
		buf->samples   = NULL;
		buf->available = true;

        // special case to fix sounds looping
        if (samples == 0 && ptr == NULL) *smp_recv = smp_req;
	}
	return ptr;
}

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	ctx->hnd = snd_stream_alloc(AudioCallback, SND_STREAM_BUFFER_MAX);
	if (ctx->hnd == SND_STREAM_INVALID) return ERR_NOT_SUPPORTED;
	snd_stream_set_userdata(ctx->hnd, ctx);
	
	Mem_Set(ctx->bufs, 0, sizeof(ctx->bufs));
	for (int i = 0; i < buffers; i++) {
		ctx->bufs[i].available = true;
	}
	
	ctx->count   = buffers;
	ctx->bufHead = 0;
	return 0;
}

void Audio_Close(struct AudioContext* ctx) {
	if (ctx->count) {
		snd_stream_stop(ctx->hnd);
		snd_stream_destroy(ctx->hnd);
	}
	
	ctx->hnd   = SND_STREAM_INVALID;
	ctx->count = 0;
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	ctx->channels   = channels;
	ctx->sampleRate = sampleRate;
	return 0;
}

cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 dataSize) {
	struct AudioBuffer* buf;

	for (int i = 0; i < ctx->count; i++) 
	{
		buf = &ctx->bufs[i];
		if (!buf->available) continue;

		buf->samples   = chunk;
		buf->bytesLeft = dataSize;
		buf->available = false;
		return 0;
	}
	// tried to queue data without polling for free buffers first
	return ERR_INVALID_ARGUMENT;
}

cc_result Audio_Play(struct AudioContext* ctx) {
	snd_stream_start(ctx->hnd, ctx->sampleRate, ctx->channels == 2);
	return 0; 
}

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	struct AudioBuffer* buf;
	int count = 0;

	for (int i = 0; i < ctx->count; i++)
	{
		buf = &ctx->bufs[i];
		if (!buf->available) count++;
	}

	*inUse = count;
	return 0;
}

cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	return true;
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	snd_stream_volume(ctx->hnd, data->volume);
	data->sampleRate = Audio_AdjustSampleRate(data);
	cc_result res;

	if ((res = Audio_SetFormat(ctx, data->channels, data->sampleRate))) return res;
	if ((res = Audio_QueueChunk(ctx, data->data,    data->size)))       return res;
	if ((res = Audio_Play(ctx))) return res;
	return 0;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	return false;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	size = (size + 0x1F) & ~0x1F;  // round up to nearest multiple of 32
	void* dst = memalign(32, size * numChunks);

	for (int i = 0; i < numChunks; i++) {
		chunks[i] = dst + size * i;
	}
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	free(chunks[0]);
}

#elif defined CC_BUILD_WEBAUDIO
/*########################################################################################################################*
*-----------------------------------------------------WebAudio backend----------------------------------------------------*
*#########################################################################################################################*/
struct AudioContext { int contextID, count; };
#define AUDIO_COMMON_ALLOC

extern int  interop_InitAudio(void);
extern int  interop_AudioCreate(void);
extern void interop_AudioClose(int contextID);
extern int  interop_AudioPlay(int contextID, const void* name, int volume, int rate);
extern int  interop_AudioPoll(int contetID, int* inUse);
extern int  interop_AudioDescribe(int res, char* buffer, int bufferLen);

cc_bool AudioBackend_Init(void) {
	cc_result res = interop_InitAudio();
	if (res) { Audio_Warn(res, "initing WebAudio context"); return false; }
	return true;
}

void AudioBackend_Tick(void) { }
void AudioBackend_Free(void) { }

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	ctx->count     = buffers;
	ctx->contextID = interop_AudioCreate();
	return 0;
}

void Audio_Close(struct AudioContext* ctx) {
	if (ctx->contextID) interop_AudioClose(ctx->contextID);
	ctx->contextID = 0;
	ctx->count     = 0;
}

cc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate) {
	return ERR_NOT_SUPPORTED;
}
cc_result Audio_QueueChunk(struct AudioContext* ctx, void* chunk, cc_uint32 size) {
	return ERR_NOT_SUPPORTED;
}
cc_result Audio_Play(struct AudioContext* ctx) { return ERR_NOT_SUPPORTED; }

cc_result Audio_Poll(struct AudioContext* ctx, int* inUse) {
	return interop_AudioPoll(ctx->contextID, inUse);
}

cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) {
	/* Channels/Sample rate is per buffer, not a per source property */
	return true;
}

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	return interop_AudioPlay(ctx->contextID, data->data, data->volume, data->rate);
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) {
	char buffer[NATIVE_STR_LEN];
	int len = interop_AudioDescribe(res, buffer, NATIVE_STR_LEN);

	String_AppendUtf8(dst, buffer, len);
	return len > 0;
}

void Audio_AllocChunks(cc_uint32 size, void** chunks, int numChunks) {
	AudioBase_AllocChunks(size, chunks, numChunks);
}

void Audio_FreeChunks(void** chunks, int numChunks) {
	AudioBase_FreeChunks(chunks, numChunks);
}
#else
/*########################################################################################################################*
*----------------------------------------------------Null/Empty backend---------------------------------------------------*
*#########################################################################################################################*/
struct AudioContext { int count; };

cc_bool AudioBackend_Init(void) { return false; }
void    AudioBackend_Tick(void) { }
void    AudioBackend_Free(void) { }

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	return ERR_NOT_SUPPORTED;
}

void Audio_Close(struct AudioContext* ctx) { }

cc_bool Audio_FastPlay(struct AudioContext* ctx, struct AudioData* data) { return true; }

cc_result Audio_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	return ERR_NOT_SUPPORTED;
}

cc_bool Audio_DescribeError(cc_result res, cc_string* dst) { return false; }
#endif


/*########################################################################################################################*
*---------------------------------------------------Common backend code---------------------------------------------------*
*#########################################################################################################################*/

#ifdef AUDIO_COMMON_VOLUME
static void AudioBase_Clear(struct AudioContext* ctx) {
	ctx->count      = 0;
	ctx->channels   = 0;
	ctx->sampleRate = 0;
	Mem_Free(ctx->_tmpData);
	ctx->_tmpData = NULL;
	ctx->_tmpSize = 0;
}

static cc_bool AudioBase_AdjustSound(struct AudioContext* ctx, struct AudioData* data) {
	void* audio;
	if (data->volume >= 100) return true;

	/* copy to temp buffer to apply volume */
	if (ctx->_tmpSize < data->size) {
		/* TODO: check if we can realloc NULL without a problem */
		if (ctx->_tmpData) {
			audio = Mem_TryRealloc(ctx->_tmpData, data->size, 1);
		} else {
			audio = Mem_TryAlloc(data->size, 1);
		}

		if (!data) return false;
		ctx->_tmpData = audio;
		ctx->_tmpSize = data->size;
	}

	audio = ctx->_tmpData;
	Mem_Copy(audio, data->data, data->size);
	Audio_ApplyVolume((cc_int16*)audio, data->size / 2, data->volume);
	data->data = audio;
	return true;
}
#endif

#ifdef AUDIO_COMMON_ALLOC
static void AudioBase_AllocChunks(int size, void** chunks, int numChunks) {
	cc_uint8* dst = (cc_uint8*)Mem_TryAlloc(numChunks, size);
	int i;
	
	for (i = 0; i < numChunks; i++) 
	{
		chunks[i] = dst ? (dst + size * i) : NULL;
	}
}

static void AudioBase_FreeChunks(void** chunks, int numChunks) {
	Mem_Free(chunks[0]);
}
#endif


/*########################################################################################################################*
*---------------------------------------------------Audio context code----------------------------------------------------*
*#########################################################################################################################*/
struct AudioContext music_ctx;
#define POOL_MAX_CONTEXTS 8
static struct AudioContext context_pool[POOL_MAX_CONTEXTS];

#ifndef CC_BUILD_NOSOUNDS
cc_result AudioPool_Play(struct AudioData* data) {
	struct AudioContext* ctx;
	int inUse, i;
	cc_result res;

	/* Try to play on a context that doesn't need to be recreated */
	for (i = 0; i < POOL_MAX_CONTEXTS; i++) {
		ctx = &context_pool[i];
		if (!ctx->count && (res = Audio_Init(ctx, 1))) return res;

		if ((res = Audio_Poll(ctx, &inUse))) return res;
		if (inUse > 0) continue;
		
		if (!Audio_FastPlay(ctx, data)) continue;
		return Audio_PlayData(ctx, data);
	}

	/* Try again with all contexts, even if need to recreate one (expensive) */
	for (i = 0; i < POOL_MAX_CONTEXTS; i++) {
		ctx = &context_pool[i];
		res = Audio_Poll(ctx, &inUse);

		if (res) return res;
		if (inUse > 0) continue;

		return Audio_PlayData(ctx, data);
	}
	return 0;
}

void AudioPool_Close(void) {
	int i;
	for (i = 0; i < POOL_MAX_CONTEXTS; i++) {
		Audio_Close(&context_pool[i]);
	}
}
#endif
