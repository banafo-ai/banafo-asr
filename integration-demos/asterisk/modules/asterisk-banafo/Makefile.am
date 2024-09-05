AUTOMAKE_OPTIONS     = foreign
MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure
ACLOCAL_AMFLAGS      = -I m4

SUBDIRS              = res-speech-banafo

EXTRA_DIST           = bootstrap res-speech-banafo/res_speech_banafo.conf

install-data-local:
	test -d $(DESTDIR)$(ASTERISK_CONF_DIR) || $(mkinstalldirs) $(DESTDIR)$(ASTERISK_CONF_DIR)
	test -f $(DESTDIR)$(ASTERISK_CONF_DIR)/res_speech_banafo.conf || $(INSTALL) -m 644 $(top_srcdir)/res-speech-banafo/res_speech_banafo.conf $(DESTDIR)$(ASTERISK_CONF_DIR)
