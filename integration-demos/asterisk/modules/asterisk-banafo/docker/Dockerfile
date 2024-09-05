# Asterisk server Docker container
# author: Dimitar Kokov
# version: 0.3
# date: 2024-08-29

# docker image build -t ban-ast-mod .
#

FROM debian:bullseye-slim
#FROM bitnami/minideb:bullseye

ENV DEBIAN_FRONTEND noninteractive
ENV NAME ban-ast-mod
ENV apiKey "apiKey-from-banafo.ai"

#INSTALL DEPENDENCIES
RUN apt-get update && \
apt-get install -y wget gcc make git autoconf automake sudo apt-utils tzdata net-tools \
libedit-dev build-essential libssl-dev libncurses5-dev libnewt-dev libxml2-dev libsqlite3-dev uuid-dev libjansson-dev libsrtp2-dev \
libspeex-dev libspeexdsp-dev libcurl4 'libtool-bin|libtool' pkg-config


RUN git clone https://github.com/asterisk/asterisk.git /usr/src/asterisk
#RUN wget http://downloads.asterisk.org/pub/telephony/asterisk/asterisk-18-current.tar.gz -O /usr/src/asterisk.tar.gz

COPY . /usr/src/asterisk-banafo/

WORKDIR /ban-ast-mod
COPY ./docker/run-re6-core.sh /ban-ast-mod/
RUN chmod +x /ban-ast-mod/run-re6-core.sh

# asterisk
#RUN cd /usr/src/ && tar xvf asterisk.tar.gz && mv -v asterisk-18.* /usr/src/asterisk/
RUN cd /usr/src/asterisk/ && ./configure  && make && make install && ldconfig

# copy Banafo setup to the Asterisk
COPY ./docker/conf/* /etc/asterisk/

# Asterisk 21, opus module
RUN cd /usr/src/ && wget http://downloads.digium.com/pub/telephony/codec_opus/asterisk-21.0/x86-64/codec_opus-21.0-current-x86_64.tar.gz
RUN cd /usr/src/ && tar -zxf codec_opus-21.0-current-x86_64.tar.gz
RUN cd /usr/src/codec_opus-21.0_* && cp codec_opus.so /usr/lib/asterisk/modules/ && cp format_ogg_opus.so /usr/lib/asterisk/modules/ && \
cp codec_opus_config-en_US.xml /var/lib/asterisk/documentation/

# asterisk-banafo module
RUN cd /usr/src/asterisk-banafo/ && ./bootstrap
RUN cd /usr/src/asterisk-banafo/ && ./configure --with-asterisk=/usr/src/asterisk/ --prefix=/usr/ 
RUN cd /usr/src/asterisk-banafo/ && make && make install
RUN cp -v /usr/src/asterisk-banafo/res-speech-banafo/res_speech_banafo.conf /etc/asterisk/
RUN sed -i "s/apiKey-from-banafo.ai/${apiKey}/g" /etc/asterisk/res_speech_banafo.conf

#COPY ./docker/hosts /etc/

# Cleanup the image
RUN apt-get clean

CMD ["asterisk","-f"]

