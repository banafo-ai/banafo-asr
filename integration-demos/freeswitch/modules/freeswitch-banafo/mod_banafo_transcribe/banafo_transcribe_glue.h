#ifndef __BANAFO_GLUE_H__
#define __BANAFO_GLUE_H__

switch_status_t banafo_transcribe_init();
switch_status_t banafo_transcribe_cleanup();
switch_status_t banafo_transcribe_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t channels, char* lang, int interim, char* bugname, void **ppUserData);
switch_status_t banafo_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing, char* bugname);
switch_bool_t banafo_transcribe_frame(switch_core_session_t *session, switch_media_bug_t *bug);


#endif