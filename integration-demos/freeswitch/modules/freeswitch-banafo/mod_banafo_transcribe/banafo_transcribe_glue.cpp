#include <switch.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <unordered_map>

#include "mod_banafo_transcribe.h"
#include "simple_buffer.h"
#include "parser.hpp"
#include "audio_pipe.hpp"

#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000  320 /*which means each 20ms frame as 320 bytes at 8 khz (1 channel only)*/
#define BANAFO_API_PATH "/api/v1/transcripts/streaming?"

namespace {
  static bool hasDefaultCredentials = false;
  //static const char* defaultApiKey = nullptr;
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 1, 5));
  static unsigned int idxCallCount = 0;
  static uint32_t playCount = 0;

  /* banafo model / tier defaults by language */
  struct LanguageInfo {
      std::string tier;
      std::string model;
  };

  static const std::unordered_map<std::string, LanguageInfo> languageLookupTable = {
      {"zh", {"base", "general"}},
      {"zh-CN", {"base", "general"}},
      {"zh-TW", {"base", "general"}},
      {"da", {"enhanced", "general"}},
      {"en", {"nova", "phonecall"}},
      {"en-US", {"nova", "phonecall"}},
      {"en-AU", {"nova", "general"}},
      {"en-GB", {"nova", "general"}},
      {"en-IN", {"nova", "general"}},
      {"en-NZ", {"nova", "general"}},
      {"nl", {"enhanced", "general"}},
      {"fr", {"enhanced", "general"}},
      {"fr-CA", {"base", "general"}},
      {"de", {"enhanced", "general"}},
      {"hi", {"enhanced", "general"}},
      {"hi-Latn", {"base", "general"}},
      {"id", {"base", "general"}},
      {"ja", {"enhanced", "general"}},
      {"ko", {"enhanced", "general"}},
      {"no", {"enhanced", "general"}},
      {"pl", {"enhanced", "general"}},
      {"pt", {"enhanced", "general"}},
      {"pt-BR", {"enhanced", "general"}},
      {"pt-PT", {"enhanced", "general"}},
      {"ru", {"base", "general"}},
      {"es", {"nova", "general"}},
      {"es-419", {"nova", "general"}},
      {"sv", {"enhanced", "general"}},
      {"ta", {"enhanced", "general"}},
      {"tr", {"base", "general"}},
      {"uk", {"base", "general"}}
  };

  static bool getLanguageInfo(const std::string& language, LanguageInfo& info) {
      auto it = languageLookupTable.find(language);
      if (it != languageLookupTable.end()) {
          info = it->second;
          return true;
      }
      return false;
  }

  static const char* emptyTranscript = "{\"alternatives\":[{\"transcript\":\"\",\"confidence\":0.0,\"words\":[]}]}";

  static void reaper(private_t *tech_pvt) {
    std::shared_ptr<banafo::AudioPipe> pAp;
    pAp.reset((banafo::AudioPipe *)tech_pvt->pAudioPipe);
    tech_pvt->pAudioPipe = nullptr;

    std::thread t([pAp, tech_pvt]{
      pAp->finish();
      pAp->waitForClose();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s (%u) got remote close\n", tech_pvt->sessionId, tech_pvt->id);
    });
    t.detach();
  }

  static void destroy_tech_pvt(private_t *tech_pvt) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->sessionId, tech_pvt->id);
    if (tech_pvt) {
      if (tech_pvt->pAudioPipe) {
        banafo::AudioPipe* p = (banafo::AudioPipe *) tech_pvt->pAudioPipe;
        delete p;
        tech_pvt->pAudioPipe = nullptr;
      }

      if (tech_pvt->pCallBack) {
        banafo::AudioPipe* cb = (banafo::AudioPipe *) tech_pvt->pCallBack;
        //delete cb; ??? why is it crashed ???
        tech_pvt->pCallBack = nullptr;
      }

      if (tech_pvt->resampler) {
          speex_resampler_destroy(tech_pvt->resampler);
          tech_pvt->resampler = NULL;
      }

      /*
      if (tech_pvt->vad) {
        switch_vad_destroy(&tech_pvt->vad);
        tech_pvt->vad = nullptr;
      }
      */
    }
  }

  std::string encodeURIComponent(std::string decoded)
  {

      std::ostringstream oss;
      std::regex r("[!'\\(\\)*-.0-9A-Za-z_~:]");

      for (char &c : decoded)
      {
          if (std::regex_match((std::string){c}, r))
          {
              oss << c;
          }
          else
          {
              oss << "%" << std::uppercase << std::hex << (0xff & c);
          }
      }
      return oss.str();
  }

  std::string& constructPath(switch_core_session_t* session, std::string& path, 
    int sampleRate, int channels, const char* language, int interim) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    const char *var ;
    std::ostringstream oss;
    //LanguageInfo info;

    if (var = switch_channel_get_variable(channel, "BANAFO_API_KEY")) {
      oss << BANAFO_API_PATH <<  "apiKey=";
      oss << var;
    } else {
      oss << "";
      path = oss.str();
      return path;
    }

    oss <<  "&languageCode=";
    oss <<  language;

    if (var = switch_channel_get_variable(channel, "BANAFO_SPEECH_ENDPOINTS")) {
      oss <<  "&endpoints=";
      oss <<  var;
    }

   path = oss.str();
   return path;
  }

  static void eventCallback(const char* sessionId, banafo::AudioPipe::NotifyEvent_t event, const char* message, bool finished) {
    switch_core_session_t* session = switch_core_session_locate(sessionId);
    if (session) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
      if (bug) {
        private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
        if (tech_pvt) {
          switch (event) {
            case banafo::AudioPipe::CONNECT_SUCCESS:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "connection successful\n");
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_SUCCESS, NULL, tech_pvt->bugname, finished);
            break;
            case banafo::AudioPipe::CONNECT_FAIL:
            {
              // first thing: we can no longer access the AudioPipe
              std::stringstream json;
              json << "{\"reason\":\"" << message << "\"}";
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_FAIL, (char *) json.str().c_str(), tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "connection failed: %s\n", message);
            }
            break;
            case banafo::AudioPipe::CONNECTION_DROPPED:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_DISCONNECT, NULL, tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection dropped from far end\n");
            break;
            case banafo::AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection closed gracefully\n");
            break;
            case banafo::AudioPipe::MESSAGE:
            {
              if( strstr(message, emptyTranscript)) {
                if(mod_banafo_transcribe_globals.debug) switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "empty banafo transcript\n");
              }
              else {
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_RESULTS, message, tech_pvt->bugname, finished);
                if(mod_banafo_transcribe_globals.debug) {
                  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "banafo message: %s\n", message);
                }
                std::string msg = message;
                std::string type;
                mod_banafo_transcribe_conn_t *bptr = tech_pvt->banafo_conn;
                result_mode_t result_mode = text;
                banafo::AudioPipe *pCallBack = static_cast<banafo::AudioPipe *>(tech_pvt->pCallBack);

                if (strcmp(msg.c_str(), "Done!") == 0) {
                  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "received 'Done!' by Banafo ASR server\n");
                }

                if (bptr == NULL) {
                  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "banafo_conn pointer is NULL\n");
                } else {
                  result_mode = bptr->result_mode;
                }

                if (pCallBack) {
                  // send transcriptions to the Banafo proxy (callback_url)
                  const char* text = msg.c_str();
                  pCallBack->bufferForSending(text);
                  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "send '%s' [%d] to the Banafo proxy!\n", text, strlen(text));
                }

                if (result_mode == text) {
                  cJSON* json = parse_json(session, msg, type) ;
                  if (json) {
                    cJSON* tmp_json = cJSON_GetObjectItem(json, "segment");
                    int segm_num = tmp_json->valueint;

                    tmp_json = cJSON_GetObjectItem(json, "type");
                    const char* type = tmp_json->valuestring;

                    tmp_json = cJSON_GetObjectItem(json, "text");
                    const char* txt = tmp_json->valuestring;

                    if((strlen(txt) > 0) && (strcmp(type,"final") == 0)) {
                      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE,
                                        "[TEXT],transcribe[%d]: %s [%d]\n",segm_num,txt,strlen(txt));
                    }
                    cJSON_Delete(json);
                  }
                } else if (result_mode == json) {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, 
                                        "[JSON],banafo message: %s\n", message);
                }
              }
            }
            break;
            default:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "got unexpected msg from banafo %d:%s\n", event, message);
              break;
          }
        }
      }
      switch_core_session_rwunlock(session);
    }
  }

  int cb_url_parse(cb_url_t *cb_ptr, char *cb_url) {
    char tmp[CB_URL_LEN];
    char *token = NULL, *host = NULL, *port = NULL, *path = NULL;
    strcpy(tmp, cb_url);

    token = strtok(tmp, CB_URL_DEL1);
    if (token != NULL) {
      if (strcmp(token, "ws:") == 0) {
        cb_ptr->cb_prot = ws;
      } else if (strcmp(token, "wss:") == 0) {
        cb_ptr->cb_prot = wss;
      }
    } else {
      return -1;
    }

    token = strtok(NULL, CB_URL_DEL1);
    if (token != NULL) {
      path = strtok(NULL, CB_URL_DEL1);
      if (path != NULL) {
        strcpy(cb_ptr->cb_path, path);
      }

      host = strtok(token, CB_URL_DEL2);
      if (host != NULL) {
        strcpy(cb_ptr->cb_host, host);
      }

      port = strtok(NULL, ":");
      if (port != NULL) {
        cb_ptr->cb_port = atoi(port);
      }
    } else {
      return -1;
    }

    return 0;
  }

  switch_status_t fork_data_init(private_t *tech_pvt, switch_core_session_t *session, 
    int sampling, int channels, char *lang, int interim, 
    char* bugname, responseHandler_t responseHandler) {

    int err;
    int desiredSampling;
    void *vptr = NULL;
    mod_banafo_transcribe_conn_t *ptr = NULL;
    switch_codec_implementation_t read_impl;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch_core_session_get_read_impl(session, &read_impl);

    if (mod_banafo_transcribe_globals.profiles != NULL) {
      vptr = switch_core_hash_find(mod_banafo_transcribe_globals.profiles,lang);
      if (vptr != NULL) {
        ptr = (mod_banafo_transcribe_conn_t *)vptr;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                          "host: %s, port: %d, prot: %d, result_mode: %d, sample_rate: %d, send_sample_rate: %d, callback_url: %s;\n",
                          ptr->banafo_asr_hostname, ptr->banafo_asr_port, ptr->prot, ptr->result_mode, ptr->sample_rate,
                          ptr->send_sample_rate, ptr->callback_url);
      } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"the profile is not found!");
        return SWITCH_STATUS_FALSE;
      }
    } else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"profiles pointer is NULL!");
      return SWITCH_STATUS_FALSE;
    }

    desiredSampling = ptr->sample_rate;
    memset(tech_pvt, 0, sizeof(private_t));
    tech_pvt->banafo_conn = ptr;

    std::string path;
    constructPath(session, path, desiredSampling, channels, lang, interim);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "path[%d]: %s \n", path.size(), path.c_str());

    strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    strncpy(tech_pvt->host, ptr->banafo_asr_hostname, MAX_WS_URL_LEN);
    tech_pvt->port = ptr->banafo_asr_port;
    strncpy(tech_pvt->path, path.c_str(), MAX_PATH_LEN);
    tech_pvt->sampling = desiredSampling;
    tech_pvt->responseHandler = responseHandler;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->buffer_overrun_notified = 0;
    tech_pvt->is_started = 0;

    /* is it correct for all codec variants ???? */
    size_t buflen = LWS_PRE + (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD * nAudioBufferSecs);

    const char* apiKey = "";

    banafo::AudioPipe* ap = new banafo::AudioPipe(tech_pvt->sessionId, tech_pvt->host, tech_pvt->port, ptr->prot,tech_pvt->path,
        buflen, read_impl.decoded_bytes_per_packet, apiKey, eventCallback);
    if (!ap) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe\n");
      return SWITCH_STATUS_FALSE;
    }

    tech_pvt->pAudioPipe = static_cast<void *>(ap);

    switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));

    if (tech_pvt->banafo_conn->callback_url != NULL) {
      cb_url_t cb_url = {0};
      if ( cb_url_parse(&cb_url, tech_pvt->banafo_conn->callback_url) == 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                          "cb_host: %s, cb_port: %d, cb_prot: %d, cb_path: %s;\n",
                          cb_url.cb_host, cb_url.cb_port, cb_url.cb_prot, cb_url.cb_path);

        banafo::AudioPipe* cb = new banafo::AudioPipe(tech_pvt->sessionId, cb_url.cb_host, cb_url.cb_port, cb_url.cb_prot, cb_url.cb_path,
            buflen, read_impl.decoded_bytes_per_packet, apiKey, eventCallback);
        if (!cb) {
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe (CallBack!)\n");
          return SWITCH_STATUS_FALSE;
        }

        tech_pvt->pCallBack = static_cast<void *>(cb);
      }
    }

    /*
      'desiredSampling' is from 'profile.sample_rate'(value from config)
      'sampling' is from 'codec.samples_per_seconds'
    */
    if (desiredSampling != sampling) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) resampling from %u to %u\n", tech_pvt->id, sampling, desiredSampling);
      tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
      if (0 != err) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
        return SWITCH_STATUS_FALSE;
      }
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) no resampling needed for this call\n", tech_pvt->id);
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_data_init\n", tech_pvt->id);

    return SWITCH_STATUS_SUCCESS;
  }

  void lws_logger(int level, const char *line) {
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;

    switch (level) {
      case LLL_ERR: llevel = SWITCH_LOG_ERROR; break;
      case LLL_WARN: llevel = SWITCH_LOG_WARNING; break;
      case LLL_NOTICE: llevel = SWITCH_LOG_NOTICE; break;
      case LLL_INFO: llevel = SWITCH_LOG_INFO; break;
      break;
    }
	  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "%s\n", line);
  }
}


extern "C" {
  int s16le_to_f32le(float *_float32,int16_t *_int16,char *_buf,size_t _buf_size) {
    int c;
    int samples = _buf_size/2;

    memcpy(_int16,_buf,_buf_size);
    for(c=0;c < samples;c++) {
      _float32[c] = ((float)_int16[c]) / 32768;
    }
    return c;
  }

  switch_status_t banafo_transcribe_init() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_banafo_transcribe: audio buffer (in secs):    %d secs\n", nAudioBufferSecs);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_banafo_transcribe: lws service threads:       %d\n", nServiceThreads);
 
    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE || LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;
    
    banafo::AudioPipe::initialize(nServiceThreads, logs, lws_logger);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AudioPipe::initialize completed\n");

		return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t banafo_transcribe_cleanup() {
    bool cleanup = false;
    cleanup = banafo::AudioPipe::deinitialize();
    if (cleanup == true) {
        return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }
	
  switch_status_t banafo_transcribe_session_init(switch_core_session_t *session, 
    responseHandler_t responseHandler, uint32_t channels, 
    char* lang, int interim, char* bugname, void **ppUserData)
  {
    int err;
    uint32_t samples_per_second;
    switch_codec_implementation_t read_impl;
    memset(&read_impl,0,sizeof(switch_codec_implementation_t));
    switch_core_session_get_read_impl(session, &read_impl);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
                    "codec: %s, samples_per_second: %d, samples_per_packet: %d, decoded_bytes_per_packet: %d\n", 
                    read_impl.iananame, read_impl.samples_per_second, read_impl.samples_per_packet, read_impl.decoded_bytes_per_packet);

    samples_per_second = !strcasecmp(read_impl.iananame, "g722") ? read_impl.actual_samples_per_second : read_impl.samples_per_second;

    // allocate per-session data structure
    private_t* tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));
    if (!tech_pvt) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
      return SWITCH_STATUS_FALSE;
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "channels: %d\n",channels);
    if (SWITCH_STATUS_SUCCESS != fork_data_init(tech_pvt, session, samples_per_second, channels, lang, interim, bugname, responseHandler)) {
      destroy_tech_pvt(tech_pvt);
      return SWITCH_STATUS_FALSE;
    }

    if (mod_banafo_transcribe_globals.debug) {
      char filename[256] = "";
      sprintf(filename, "/tmp/fs_debug_%s_%d_%d.pcm", read_impl.iananame, read_impl.samples_per_second, (int)time(NULL));
      tech_pvt->fp = fopen( filename,"w");
    }

    *ppUserData = tech_pvt;

    banafo::AudioPipe *pAudioPipe = static_cast<banafo::AudioPipe *>(tech_pvt->pAudioPipe);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connecting now\n");
    pAudioPipe->connect();
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection in progress\n");

    if (tech_pvt->pCallBack) {
      banafo::AudioPipe *pCallBack = static_cast<banafo::AudioPipe *>(tech_pvt->pCallBack);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "CallBack connecting now\n");
      pCallBack->connect();
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "CallBack connection in progress\n");
    }

    return SWITCH_STATUS_SUCCESS;
  }

	switch_status_t banafo_transcribe_session_stop(switch_core_session_t *session,int channelIsClosing, char* bugname) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "banafo_transcribe_session_stop: no bug - websocket conection already closed\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    uint32_t id = tech_pvt->id;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) banafo_transcribe_session_stop\n", id);

    if (!tech_pvt) return SWITCH_STATUS_FALSE;

    banafo::AudioPipe *pAudioPipe = static_cast<banafo::AudioPipe *>(tech_pvt->pAudioPipe);
    if (pAudioPipe) {
      if (tech_pvt->banafo_conn != NULL) {
        pAudioPipe->finish();
        pAudioPipe->close();
      }
    }

    banafo::AudioPipe *pCallBack = static_cast<banafo::AudioPipe *>(tech_pvt->pCallBack);
    if (pCallBack) {
        pCallBack->close();
    }

    if (tech_pvt->fp != NULL) fclose(tech_pvt->fp);

    // close connection and get final responses
    switch_mutex_lock(tech_pvt->mutex);
    switch_channel_set_private(channel, bugname, NULL);
    if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

    if (pAudioPipe) { 
      reaper(tech_pvt);
    } else {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "reaper() is not started\n");
    }
    destroy_tech_pvt(tech_pvt);
    switch_mutex_unlock(tech_pvt->mutex);
    switch_mutex_destroy(tech_pvt->mutex);
    tech_pvt->mutex = nullptr;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) banafo_transcribe_session_stop\n", id);
    return SWITCH_STATUS_SUCCESS;
  }

switch_bool_t banafo_transcribe_frame(switch_core_session_t *session, switch_media_bug_t *bug) {
    char *tmp_ptr;
    switch_frame_t frame = { 0 };
    uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];

    frame.data = data;
    frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);

    if (!tech_pvt) return SWITCH_TRUE;

    if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
      if (!tech_pvt->pAudioPipe) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }
      banafo::AudioPipe *pAudioPipe = static_cast<banafo::AudioPipe *>(tech_pvt->pAudioPipe);
      if (pAudioPipe->getLwsState() != banafo::AudioPipe::LWS_CLIENT_CONNECTED) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }

      pAudioPipe->lockAudioBuffer();
      size_t available = pAudioPipe->binarySpaceAvailable();
      if (NULL == tech_pvt->resampler) {
        while (true) {
          // check if buffer would be overwritten; dump packets if so
          if (available < pAudioPipe->binaryMinSpace()) {
            if (!tech_pvt->buffer_overrun_notified) {
              tech_pvt->buffer_overrun_notified = 1;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
            }
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets!\n", 
              tech_pvt->id);
            pAudioPipe->binaryWritePtrResetToZero();

            frame.data = pAudioPipe->binaryWritePtr();
            frame.buflen = available = pAudioPipe->binarySpaceAvailable();
          }

          switch_status_t rv = switch_core_media_bug_read(bug, &frame, SWITCH_TRUE);
          if (rv != SWITCH_STATUS_SUCCESS) break;
          if (frame.datalen) {
            int16_t _int16[SWITCH_RECOMMENDED_BUFFER_SIZE/2] = {0};
            float _float32[SWITCH_RECOMMENDED_BUFFER_SIZE/2] = {0};
            int c = s16le_to_f32le(_float32,_int16,(char *)frame.data,frame.datalen);

            if (tech_pvt->fp != NULL) fwrite(_float32,1,(sizeof(float)*c),tech_pvt->fp);
            tmp_ptr = pAudioPipe->binaryWritePtr();
            memcpy(tmp_ptr,_float32,(sizeof(float)*c));

            pAudioPipe->binaryWritePtrAdd((sizeof(float)*c));
            frame.buflen = available = pAudioPipe->binarySpaceAvailable();
          }
        }
      } else {
        spx_int16_t spx_data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        float _float32 = 0;

        if (!tech_pvt->is_started) {
          if (tech_pvt->banafo_conn != NULL) {
            if(tech_pvt->banafo_conn->send_sample_rate) {
              tmp_ptr = pAudioPipe->binaryWritePtr();
              int32_t sample_rate = tech_pvt->banafo_conn->sample_rate;
              char _sample_rate[32] = {0};
              memcpy(_sample_rate,&sample_rate,sizeof(sample_rate));
              memcpy(tmp_ptr,_sample_rate,sizeof(_sample_rate));
              tech_pvt->is_started = 1;
              pAudioPipe->binaryWritePtrAdd(sizeof(_sample_rate));
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "send sample rate: %d\n",sample_rate);
            }
          }
        }

        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
          if (frame.datalen) {
            spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE >> 1;
            spx_uint32_t in_len = frame.samples;

            speex_resampler_process_interleaved_int(tech_pvt->resampler, (const spx_int16_t *) frame.data, (spx_uint32_t *) &in_len, spx_data, &out_len);

            if (out_len > 0) {
              // bytes written = num samples * 2 * num channels
              size_t bytes_written = out_len << tech_pvt->channels;

              tmp_ptr = pAudioPipe->binaryWritePtr();
              for(int i=0;i<out_len;i++) {
                _float32 = ((float) spx_data[i]) / 32768;
                memcpy(tmp_ptr,&_float32,sizeof(_float32));
                tmp_ptr = tmp_ptr + sizeof(_float32);
              }
              if (tech_pvt->fp != NULL) fwrite(tmp_ptr,1,(bytes_written*2),tech_pvt->fp);

              pAudioPipe->binaryWritePtrAdd((bytes_written)*2);
              available = pAudioPipe->binarySpaceAvailable();
            }
            if (available < pAudioPipe->binaryMinSpace()) {
              if (!tech_pvt->buffer_overrun_notified) {
                tech_pvt->buffer_overrun_notified = 1;
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets!\n", 
                  tech_pvt->id);
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
              }
              break;
            }
          }
        }
      }

      pAudioPipe->unlockAudioBuffer();
      switch_mutex_unlock(tech_pvt->mutex);
    }
    return SWITCH_TRUE;
  }
}
