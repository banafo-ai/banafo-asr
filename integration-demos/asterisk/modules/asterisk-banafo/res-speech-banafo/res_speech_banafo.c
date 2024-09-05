/*
 * Asterisk -- An open source telephony toolkit.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * Please follow coding guidelines 
 * https://docs.asterisk.org/Development/Policies-and-Procedures/Coding-Guidelines/
 * 
 * === Modifications made by Banafo Ltd, Copyright 2024 ===
 * This module contains modifications and original code written by Banafo Ltd.
 * Changes include:
 * - Original configuration for Banafo audio format and result format handling.
 * - Custom resampling logic.
 * - Callback implementations for enhanced audio processing.
 *
 * This module also incorporates code from other Asterisk modules, including:
 * - Code based on 'res_speech_vosk' (https://github.com/alphacep/vosk-asterisk).
 * - Concepts and ideas adapted from 'res_speech', 'res_aeap', and 'res_speech_aeap'.
 *
/* Asterisk includes. */
#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#define AST_MODULE "res_speech_banafo"
#include <asterisk/module.h>
#include <asterisk/config.h>
#include "asterisk/utils.h"

#include <asterisk/frame.h>
#include <asterisk/speech.h>
#include <asterisk/format_cache.h>
#include <asterisk/json.h>

#include <asterisk/http_websocket.h>

#include <speex/speex_resampler.h>

#define BANAFO_ENGINE_CONFIG	"res_speech_banafo.conf"
#define BANAFO_BUF_SIZE			6400

#define BANAFO_RESAMPLE_QUALITY	2
#define BANAFO_CHANNELS			1
#define BANAFO_SAMPLE_RATE		16000

#define TLS		"wss"
#define noTLS	"ws"

struct ban_speech_s {
	/* Name of the speech object to be used for logging */
	char						*name;
	/* Speex resampler */
	SpeexResamplerState			*resampler;
	/* Websocket connection to Banafo ASR */
	struct	ast_websocket		*ws;
	/* Webscoket connection to callback */
	struct	ast_websocket		*cb_ws;
	/* TLS setup */
	struct ast_tls_config		*tls_cfg;
	/* Buffer for frames */
	char						buf[BANAFO_BUF_SIZE];
	int							offset;
	char						*last_result;
	FILE 						*fp;
};

struct ban_engine_s {
	/* Websocket url*/
	char						*ws_url;
	/* */
	char						*c_url;
	/* callback url - send the result to it */
	char						*cb_url;
	/* ws or wss */
	char						*ws_type;
	/* Banafo API-KEY */
	char						*api_key;
	/* Language */
	char 						*lang;
	/* sample rate to Banafo */
	unsigned int				sample_rate;
	/* */
	char						*endpoints;
	/* Speech engine name */
	char						*name;
	/* pointer for current speech engine */
	struct ast_speech_engine 	*engine;
	/* pointer to next speech engine */
	struct ban_engine_s 		*next;
};

struct ban_globals_s {
	struct ban_engine_s *engines;
	unsigned short 	debug;
};

typedef struct ban_speech_s ban_speech_t;
typedef struct ban_engine_s ban_engine_t;
typedef struct ban_globals_s ban_globals_t;

static ban_globals_t ban_globals;

static ban_engine_t *ban_engine_find_eng(const char *name);
char *ban_engine_compose_api_wss_url(ban_engine_t *eng);

static int ban_recog_create(struct ast_speech *speech, struct ast_format *format)
{
	int err;
	char *url;
	ban_engine_t *engine;
	ban_speech_t *ban_speech;
	struct ast_tls_config *tls_cfg;
	enum ast_websocket_result result, cb_result;

	ban_speech = ast_calloc(1, sizeof(ban_speech_t));
	ban_speech->name = speech->engine->name;
	speech->data = ban_speech;

	int f_sample_rate = ast_format_get_sample_rate(format);

	engine = ban_engine_find_eng(ban_speech->name);
	if (engine == NULL) {
		ast_log(LOG_ERROR, "The SPEECH engine [%s] wasn't found!\n", ban_speech->name);
		ast_free(speech->data);
		return -1;
	}

	if (engine->sample_rate == 0) {
		engine->sample_rate = BANAFO_SAMPLE_RATE;
	}

	if (engine->c_url != NULL) {
		url = engine->c_url;
	} else if (engine->ws_url != NULL) {
		url = engine->ws_url;
	} else {
		ast_log(LOG_ERROR, "It doesn't have URL!\n");
		ast_free(speech->data);
		return -1;
	}

	if (strcmp(engine->ws_type,TLS) == 0) {
		tls_cfg = ast_calloc(1,sizeof(struct ast_tls_config));
		int i = ast_ssl_setup(tls_cfg);

		tls_cfg->flags.flags |= AST_SSL_DONT_VERIFY_SERVER;
		tls_cfg->flags.flags |= AST_SSL_IGNORE_COMMON_NAME;
		tls_cfg->flags.flags |= AST_SSL_SERVER_CIPHER_ORDER;

		ast_log(LOG_DEBUG, "SSL Setup result: %d, flags: %d\n", i, tls_cfg->flags);
		ban_speech->tls_cfg = tls_cfg;
	} else if (strcmp(engine->ws_type, noTLS) == 0) {
		tls_cfg = NULL;
		ban_speech->tls_cfg = NULL;
		ast_log(LOG_DEBUG, "No SSL Setup\n");
	} else {
		ast_log(LOG_ERROR, "ERROR!!!\nUnknown format\nCheck your url in the configuration!\n");
		ast_free(speech->data);
		return -1;
	}

	ban_speech->ws = ast_websocket_client_create(url, engine->ws_type, tls_cfg, &result);
	if (!ban_speech->ws) {
		ast_log(LOG_ERROR, "Can't create websocket to '%s'\n", url);
		ast_free(speech->data);
		return -1;
	}

	if (f_sample_rate != engine->sample_rate) {
		ban_speech->resampler = speex_resampler_init(BANAFO_CHANNELS, f_sample_rate, engine->sample_rate, BANAFO_RESAMPLE_QUALITY, &err);
		if (0 != err) {
			ast_log(LOG_ERROR, "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
			return -1;
		}
		ast_log(LOG_NOTICE, "resampling: %d -> %d", f_sample_rate, engine->sample_rate);
	} else {
		ast_log(LOG_NOTICE, "no resampling\n");
	}

	if (engine->cb_url != NULL) {
		ban_speech->cb_ws = ast_websocket_client_create(engine->cb_url, "ws", NULL, &cb_result);
		if (!ban_speech->cb_ws) {
			ast_log(LOG_ERROR, "Can't create websocket to '%s' (callback_url)\n", engine->cb_url);
		}
	}

	if (ban_globals.debug) {
		ban_speech->fp = fopen("/tmp/ast_debug_audio.pcm","w");
	}

	ast_log(LOG_NOTICE, "(%s) Created speech resource result %d\n", ban_speech->name, result);
}

static int ban_recog_destroy(struct ast_speech *speech)
{
	int res_len;
	char *res;
	ban_speech_t *ban_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Destroy speech resource\n",ban_speech->name);

	memset(ban_speech->buf,0,BANAFO_BUF_SIZE);
	memcpy(ban_speech->buf,"Done", 4);

	if (ban_speech->ws) {
		ast_websocket_write(ban_speech->ws, AST_WEBSOCKET_OPCODE_TEXT, ban_speech->buf, 4);

		if (ast_websocket_wait_for_input(ban_speech->ws, 2000) > 0) {
			res_len = ast_websocket_read_string(ban_speech->ws, &res);
			if (res_len >= 0) {
				ast_log(LOG_DEBUG, "WS[%d]: %s\n", res_len, res);
			} else {
				ast_log(LOG_ERROR, "(%s) Got error result %d\n", ban_speech->name, res_len);
			}
		} else {
			ast_log(LOG_DEBUG, "no wait\n");
		}

		if (ban_speech->tls_cfg != NULL) {
			ast_ssl_teardown(ban_speech->tls_cfg);
			ast_free(ban_speech->tls_cfg);
		}

		int fd = ast_websocket_fd(ban_speech->ws);
		if (fd > 0) {
			ast_websocket_close(ban_speech->ws, 1000);
			shutdown(fd, SHUT_RDWR);
			ast_websocket_unref(ban_speech->ws);
		}
	}

	if (ban_speech->cb_ws) {
		int cb_fd = ast_websocket_fd(ban_speech->cb_ws);
		if (cb_fd > 0) {
			ast_websocket_close(ban_speech->cb_ws, 1000);
			shutdown(cb_fd, SHUT_RDWR);
			ast_websocket_unref(ban_speech->cb_ws);
		}
	}

	if (ban_speech->resampler) {
		speex_resampler_destroy(ban_speech->resampler);
		ban_speech->resampler = NULL;
	}

	if (ban_speech->last_result) {
		ast_free(ban_speech->last_result);
	}
	ast_free(ban_speech);

	return 0;
}

static int ban_recog_load_grammar(struct ast_speech *speech, const char *grammar_name, const char *grammar_path)
{
	return 0;
}

static int ban_recog_unload_grammar(struct ast_speech *speech, const char *grammar_name)
{
	return 0;
}

static int ban_recog_activate_grammar(struct ast_speech *speech, const char *grammar_name)
{
	return 0;
}

static int ban_recog_deactivate_grammar(struct ast_speech *speech, const char *grammar_name)
{
	return 0;
}

static int ban_recog_write(struct ast_speech *speech, void *data, int len)
{
	char *res;
	int c,int16_len,fl32_len,res_len;
	ban_speech_t *ban_speech = speech->data;

	int16_t _int16[BANAFO_BUF_SIZE] = {0};
	float _float32[BANAFO_BUF_SIZE] = {0};
	spx_int16_t spx_data[BANAFO_BUF_SIZE] = {0};

	int16_len = (len/2);
	fl32_len = (len*2);

	if (ban_speech->resampler == NULL) {
		ast_assert (ban_speech->offset + f32_len < BANAFO_BUF_SIZE);

		memcpy(_int16 , data, len);

		for(c=0;c < int16_len;c++) {
			_float32[c] = ((float)_int16[c]) / 32768;
		}

		memcpy(ban_speech->buf + ban_speech->offset, _float32, fl32_len);
		ban_speech->offset += fl32_len;
	} else {
		ast_assert (ban_speech->offset + (f32_len*2) < BANAFO_BUF_SIZE);

		float s_float32 = 0;
		spx_uint32_t out_len = BANAFO_BUF_SIZE >> 1;

		speex_resampler_process_interleaved_int(ban_speech->resampler, (const spx_int16_t *) data, (spx_uint32_t *) &int16_len, spx_data, &out_len);
		if (out_len > 0) {
			// bytes written = num samples * 2 * num channels
			size_t bytes_written = out_len << BANAFO_CHANNELS;
			char *tmp_ptr = ban_speech->buf + ban_speech->offset;
			for(int i=0;i<out_len;i++) {
				s_float32 = ((float) spx_data[i]) / 32768;
				memcpy(tmp_ptr, &s_float32, sizeof(s_float32));
				tmp_ptr = tmp_ptr + sizeof(s_float32);
			}
			ban_speech->offset += (bytes_written*2);
		}
	}

	if (ban_speech->offset == BANAFO_BUF_SIZE) {
		if ((ban_globals.debug) && (ban_speech->fp != NULL)) {
			fwrite(ban_speech->buf,1,BANAFO_BUF_SIZE,ban_speech->fp);
		}
		ast_websocket_write(ban_speech->ws, AST_WEBSOCKET_OPCODE_BINARY, ban_speech->buf, BANAFO_BUF_SIZE);
		memset(ban_speech->buf,0,BANAFO_BUF_SIZE);
		ban_speech->offset = 0;
	}

	if (ast_websocket_wait_for_input(ban_speech->ws, 0) > 0) {
		res_len = ast_websocket_read_string(ban_speech->ws, &res);
		if (res_len >= 0) {
			if (ban_globals.debug) {
				ast_log(LOG_DEBUG, "(%s) Got result: '%s'\n", ban_speech->name, res);
			}

			/* send to callback if it's exist as param in the config */
			if (ban_speech->cb_ws != NULL) {
				ast_websocket_write(ban_speech->cb_ws, AST_WEBSOCKET_OPCODE_TEXT, res, res_len);
			}

			struct ast_json_error err;
			struct ast_json *res_json = ast_json_load_string(res, &err);
			if (res_json != NULL) {
				const char *text = ast_json_object_string_get(res_json, "text");
				const char *type = ast_json_object_string_get(res_json, "type");
				if ((type != NULL) && (strcmp(type,"final")==0)) {
					ast_log(LOG_NOTICE, "(%s) Banafo recognition result: %s\n", ban_speech->name, type);
					if ((text != NULL) && (strlen(text) > 0)) {
						ast_log(LOG_NOTICE, "(%s) Recognition result: %s\n", ban_speech->name, text);
						/*ast_free(ban_speech->last_result);
						ban_speech->last_result = ast_strdup(text);
						ast_speech_change_state(speech, AST_SPEECH_STATE_DONE); */
					}
				}
			} else {
				ast_log(LOG_ERROR, "(%s) JSON parse error: %s\n", ban_speech->name, err.text);
			}
			ast_json_free(res_json);
		} else {
			ast_log(LOG_NOTICE, "(%s) Got error result %d\n", ban_speech->name, res_len);
		}
	}

	return 0;
}

static int ban_recog_dtmf(struct ast_speech *speech, const char *dtmf)
{
	ban_speech_t *ban_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Signal DTMF %s\n",ban_speech->name,dtmf);
	return 0;
}

static int ban_recog_start(struct ast_speech *speech)
{
	ban_speech_t *ban_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Start recognition\n",ban_speech->name);
	ast_speech_change_state(speech, AST_SPEECH_STATE_READY);
	return 0;
}

static int ban_recog_change(struct ast_speech *speech, const char *name, const char *value)
{
	ban_speech_t *ban_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Change setting name: %s value:%s\n",ban_speech->name,name,value);
	return 0;
}

static int ban_recog_get_settings(struct ast_speech *speech, const char *name, char *buf, size_t len)
{
	ban_speech_t *ban_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Get settings name: %s\n",ban_speech->name,name);
	return -1;
}

static int ban_recog_change_results_type(struct ast_speech *speech,enum ast_speech_results_type results_type)
{
	return -1;
}

struct ast_speech_result* ban_recog_get(struct ast_speech *speech)
{
	struct ast_speech_result *speech_result;

	ban_speech_t *ban_speech = speech->data;
	speech_result = ast_calloc(sizeof(struct ast_speech_result), 1);
	speech_result->text = ast_strdup(ban_speech->last_result);
	speech_result->score = 100;

	ast_set_flag(speech, AST_SPEECH_HAVE_RESULTS);
	return speech_result;
}

static ban_engine_t *ban_engine_find_eng(const char *name)
{
	ban_engine_t *tmp_eng = ban_globals.engines;
	while ( tmp_eng != NULL) {
		if (strcmp(tmp_eng->name,name) == 0) {
			return tmp_eng;
		}
		tmp_eng = tmp_eng->next;
	}

	return NULL;
}

char *ban_engine_compose_api_wss_url(ban_engine_t *eng)
{
	size_t url_len = 0;
	char *composed_url = NULL;

	if (eng->endpoints == NULL) {
		eng->endpoints = ast_strdup("true");
	}

	/* All lenghts of variables + 34 bytes ('?','=','=',api param,lang param + endpoints param+ 1 byte for string ending '\0') */
	url_len = strlen(eng->ws_url) + strlen(eng->api_key) + strlen(eng->lang) + strlen(eng->endpoints) + 34 ;

	composed_url = ast_calloc(1,url_len);

	if (composed_url != NULL) {
		sprintf(composed_url,"%s?apiKey=%s&languageCode=%s&endpoints=%s",
				eng->ws_url, eng->api_key, eng->lang, eng->endpoints);
	}

	return composed_url;
}

char *ban_engine_separate_url(char *url)
{
	char *buf = ast_strdup(url);

	if (buf != NULL) {
		return ast_strsep(&buf, ':', 0);
	}

	return NULL;
}

void ban_engine_destroy(ban_engine_t *eng)
{
	if (eng != NULL) {
		if (eng->name != NULL) ast_free(eng->name);
		if (eng->ws_url != NULL) ast_free(eng->ws_url);
		if (eng->c_url != NULL) ast_free(eng->c_url);
		if (eng->engine != NULL) ast_free(eng->engine);
		if (eng->ws_type != NULL) ast_free(eng->ws_type);
		if (eng->api_key != NULL) ast_free(eng->api_key);
		if (eng->lang != NULL) ast_free(eng->lang);
		if (eng->endpoints != NULL) ast_free(eng->endpoints);
		if (eng->cb_url != NULL) ast_free(eng->cb_url);
	}
}

static int ban_engine_config_load()
{
	ban_engine_t *tmp_eng = NULL,*tmp = NULL;
	const char *value = NULL,*var = NULL, *cat = NULL;
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(BANAFO_ENGINE_CONFIG, config_flags);
	if(!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", BANAFO_ENGINE_CONFIG);
		return -1;
	}

	ban_globals.engines = NULL;
	ban_globals.debug = 0;

	if((value = ast_variable_retrieve(cfg, "general", "debug")) != NULL) {
		ast_log(LOG_NOTICE, "general.debug=%s\n", value);
		if(strcmp(value,"yes")==0) { 
			ban_globals.debug = 1;
		}
	}

	cat = ast_category_browse(cfg, "general");
	while (cat != NULL) {
		tmp_eng = ast_calloc(1,sizeof(ban_engine_t));

		if((var = ast_variable_retrieve(cfg, cat, "url")) != NULL) {
			if (ban_globals.debug) ast_log(LOG_DEBUG, "%s.url=%s\n", cat, var);
			if (strlen(var) > 0) {
				tmp_eng->ws_url = ast_strdup(var);
			} else {
				ast_log(LOG_ERROR, "Empty URL in SPEECH engine [%s]!!!\n", cat);
				ban_engine_destroy(tmp_eng);
				goto _next;
			}

			tmp_eng->ws_type = ban_engine_separate_url(tmp_eng->ws_url);
			if (tmp_eng->ws_type != NULL) {
				if ((strcmp(tmp_eng->ws_type,TLS) != 0) && (strcmp(tmp_eng->ws_type,noTLS) != 0)) {
					ast_log(LOG_ERROR, "Unknown format (support ws:// or wss://), your url is : %s !\n", tmp_eng->ws_url);
					ban_engine_destroy(tmp_eng);
					goto _next;
				}
			} else {
				ast_log(LOG_ERROR, "Can't recognize the link type (ws:// or wss://)!!!\n");
				ban_engine_destroy(tmp_eng);
				goto _next;
			}
		}

		if((var = ast_variable_retrieve(cfg, cat, "callback_url")) != NULL) {
			if (ban_globals.debug) ast_log(LOG_DEBUG, "%s.cb_url=%s\n", cat, var);
			if (strlen(var) > 0) {
				tmp_eng->cb_url = ast_strdup(var);
			}
		}

		if((var = ast_variable_retrieve(cfg, cat, "apiKey")) != NULL) {
			if (ban_globals.debug) ast_log(LOG_DEBUG, "%s.apiKey=%s\n", cat, var);
			if (strlen(var) > 0) {
				tmp_eng->api_key = ast_strdup(var);
			}
		}

		if((var = ast_variable_retrieve(cfg, cat, "sample_rate")) != NULL) {
			if (ban_globals.debug) ast_log(LOG_DEBUG, "%s.sample_rate=%s\n", cat, var);
			if (strlen(var) > 0) {
				tmp_eng->sample_rate = atoi(var);
			}
		}

		if((var = ast_variable_retrieve(cfg, cat, "lang")) != NULL) {
			if (ban_globals.debug) ast_log(LOG_DEBUG, "%s.lang=%s\n", cat, var);
			if (strlen(var) > 0) {
				tmp_eng->lang = ast_strdup(var);
			}
		}

		if((var = ast_variable_retrieve(cfg, cat, "endpoints")) != NULL) {
			if (ban_globals.debug) ast_log(LOG_DEBUG, "%s.endpoints=%s\n", cat, var);
			if (strlen(var) > 0) {
				tmp_eng->endpoints = ast_strdup(var);
			}
		}

		if ((tmp_eng->ws_url != NULL)&&(tmp_eng->api_key != NULL)&&(tmp_eng->lang != NULL)) {
			tmp_eng->c_url = ban_engine_compose_api_wss_url(tmp_eng);
			if (tmp_eng->c_url != NULL) {
				ast_log(LOG_NOTICE, "c_url = %s\n", tmp_eng->c_url);
			}
		}

		if (tmp_eng->ws_url == NULL) {
			ast_log(LOG_ERROR, "The SPEECH engine [%s] doesn't have a URL!\nIt didn't add to the SPEECH engine list!\n", cat);
			ban_engine_destroy(tmp_eng);
			goto _next;
		}

		tmp_eng->name = ast_strdup(cat);

		if (ban_globals.engines == NULL) {
			ban_globals.engines = tmp_eng;
			tmp = tmp_eng;
		} else {
			tmp->next = tmp_eng;
			tmp = tmp->next;
		}

_next:
		cat = ast_category_browse(cfg, cat);
	}

	ast_config_destroy(cfg);
	return 0;
}

static struct ast_speech_engine *ban_recog_speech_engine_alloc(const char *name)
{
	struct ast_speech_engine *engine;

	engine = ast_calloc(1,sizeof(struct ast_speech_engine));
	if (engine == NULL) {
		return  NULL;
	}

	engine->name = ast_strdup(name);
	engine->create = ban_recog_create;
	engine->destroy = ban_recog_destroy;
	engine->write = ban_recog_write;
	engine->dtmf = ban_recog_dtmf;
	engine->start = ban_recog_start;
	engine->change = ban_recog_change;
	engine->get_setting = ban_recog_get_settings;
	engine->change_results_type = ban_recog_change_results_type;
	engine->get = ban_recog_get;

	engine->formats = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if(!engine->formats) {
		ast_log(LOG_ERROR, "Failed to alloc media format capabilities\n");
		ast_free(engine);
		return NULL;
	}

	/*	ast_format_slin is SLIN/8000, 
		ast_format_slin16 is SLIN/16000 
		- second is by default for BANAFO ! */
	ast_format_cap_append(engine->formats, ast_format_slin16, 0);

	return engine;
}

static int ban_recog_load_engine(ban_engine_t *ban_engine_ptr)
{
	if (ban_engine_ptr == NULL) {
		ast_log(LOG_ERROR, "Don't have BANAFO configuration!!!\n");
		return -1;
	}

	ast_log(LOG_NOTICE, "load banafo engine: %s\n", ban_engine_ptr->name);
	if (ban_engine_ptr->name == NULL) {
		ast_log(LOG_ERROR, "didn't recognize banafo engine name ('[]',it's empty in the config file)!\n");
		return -1;
	}

	ban_engine_ptr->engine = ban_recog_speech_engine_alloc(ban_engine_ptr->name);
	if ( ban_engine_ptr->engine == NULL) {
		ast_log(LOG_ERROR, "Failed to allocate banafo engine mem: %s\n", ban_engine_ptr->name);
		return -1;
	}

	if(ast_speech_register(ban_engine_ptr->engine)) {
		ast_log(LOG_ERROR, "Failed to register banafo engine: %s\n", ban_engine_ptr->name);
		return -1;
	}

	return 0;
}

static int load_module(void)
{
	ban_engine_t *tmp_eng;
	struct ast_speech_engine *engine;

	ast_log(LOG_NOTICE, "Load res_speech_banafo module\n");

	/* Load engine configuration */
	ban_engine_config_load();

	tmp_eng = ban_globals.engines;
	if (tmp_eng == NULL) {
		ast_log(LOG_ERROR, "Can't load banafo module\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	while (tmp_eng != NULL) {
		engine = ast_speech_find_engine(tmp_eng->name);
		if (engine != NULL) {
			ast_log(LOG_DEBUG, "re-load engine: %s\n", tmp_eng->name);
			ast_speech_unregister2(engine->name);
		}
		ban_recog_load_engine(tmp_eng);
		ast_log(LOG_DEBUG, "load engine: %s\n", tmp_eng->name);
		tmp_eng = tmp_eng->next;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

/** \brief Unload module */
static int unload_module(void)
{
	ban_engine_t *tmp_eng,*tmp;

	tmp_eng = ban_globals.engines;
	while (tmp_eng != NULL) {
		tmp = tmp_eng;
		ast_log(LOG_DEBUG, "unregister: '%s'\n", tmp->name);
		if (ast_speech_unregister(tmp->name)) {
			ast_log(LOG_ERROR, "Failed to unregister '%s'\n", tmp->name);
		}
		tmp_eng = tmp_eng->next;
		ban_engine_destroy(tmp);
		tmp->next = NULL;
		ast_free(tmp);
	}

	ban_globals.engines = NULL;

	ast_log(LOG_NOTICE, "Unload res_speech_banafo module\n");

	return 0;
}

static int reload_module(void)
{
	unload_module();
	load_module();
}

//AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Banafo Speech Engine");
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Banafo Speech Engine",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_speech,res_http_websocket",
);
