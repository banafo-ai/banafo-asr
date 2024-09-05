MAINTAINERCLEANFILES          = Makefile.in

AM_CPPFLAGS                   = -I$(top_srcdir)/include $(ASTERISK_INCLUDES)
AM_CFLAGS                     = -DAST_MODULE_SELF_SYM="__internal_res_speech_banafo"

moddir                        = $(ASTERISK_MODDIR)
mod_LTLIBRARIES               = res_speech_banafo.la

res_speech_banafo_la_SOURCES = res_speech_banafo.c
res_speech_banafo_la_LDFLAGS = -avoid-version -no-undefined -module
res_speech_banafo_la_LIBADD  = $(banafo_LIBS) -lspeexdsp

load: 
	asterisk -rx "module load res_speech_banafo.so"

unload: 
	asterisk -rx "module unload res_speech_banafo.so"
