#ifndef __MOD_BANAFO_TRANSCRIBE_H__
#define __MOD_BANAFO_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "banafo_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "banafo_transcribe::transcription"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "banafo_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_VAD_DETECTED "banafo_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_CONNECT_SUCCESS "banafo_transcribe::connect"
#define TRANSCRIBE_EVENT_CONNECT_FAIL    "banafo_transcribe::connect_failed"
#define TRANSCRIBE_EVENT_BUFFER_OVERRUN  "banafo_transcribe::buffer_overrun"
#define TRANSCRIBE_EVENT_DISCONNECT      "banafo_transcribe::disconnect"

#define MAX_LANG (12)
#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (4096)
#define MAX_BUG_LEN (64)

#define CB_URL_DEL1 "/"
#define CB_URL_DEL2 ":"
#define CB_HOST_LEN 255
#define CB_PATH_LEN 255
#define CB_URL_LEN 1024

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished);

typedef enum prot {
  ws=1,
  wss
} prot_t;

typedef enum result_mode {
  text,
  json
} result_mode_t;

typedef struct mod_banafo_transcribe_globals_s {
  switch_memory_pool_t *pool;
  switch_hash_t *profiles;
  uint8_t debug;
} mod_banafo_transcribe_globals_t;

typedef struct cb_url_s {
	char cb_host[CB_HOST_LEN];
	unsigned short cb_port;
	prot_t cb_prot;
	char cb_path[CB_PATH_LEN];
} cb_url_t;

typedef struct mod_banafo_transcribe_conn_s {
  switch_memory_pool_t *pool;

  prot_t prot;
  uint16_t sample_rate;
  char *banafo_asr_hostname;
  char *callback_url;
  uint16_t banafo_asr_port;
  result_mode_t result_mode;
  uint8_t send_sample_rate;
} mod_banafo_transcribe_conn_t;

struct private_data {
	switch_mutex_t *mutex;
	char sessionId[MAX_SESSION_ID];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  void *pAudioPipe;
  void *pCallBack;
  int ws_state;
  char host[MAX_WS_URL_LEN];
  unsigned int port;
  char path[MAX_PATH_LEN];
  char bugname[MAX_BUG_LEN+1];
  int sampling;
  int  channels;
  unsigned int id;
  int buffer_overrun_notified:1;
  int is_finished:1;
  int is_started:1; // ???
  FILE *fp; // ???
  mod_banafo_transcribe_conn_t *banafo_conn;
};

typedef struct private_data private_t;

extern mod_banafo_transcribe_globals_t mod_banafo_transcribe_globals;

#endif