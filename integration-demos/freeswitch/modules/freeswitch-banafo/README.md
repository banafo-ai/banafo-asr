# freeswitch-banafo

This module works with [Banafo on-promise ASR server](https://banafo.ai/en/on-premise-asr) or with [Banafo Streaming API](https://banafo.ai/docs/api/#streaming).

<br>

## <u>compile and install</u>

preparing

```bash

git clone git@gitlab.int.securax.net:dimitar.kokov/freeswitch-banafo.git /usr/src/freeswitch-banafo

git clone https://github.com/signalwire/freeswitch /usr/src/freeswitch

cd /usr/src/freeswitch

sed -i '/src\/mod\/asr_tts\/mod_tts_commandline\/Makefile/a\\t\tsrc\/mod\/asr_tts\/mod_banafo_transcribe\/Makefile' configure.ac

add 'asr_tts/mod_banafo_transcribe' in modules.conf

cp -fvR ../freeswitch-banafo/mod_banafo_transcribe/ src/mod/asr_tts/mod_banafo_transcribe/

```

compile and install

```bash
cd /usr/src/freeswitch

./bootstrap.sh -j

./configure

make -j`nproc` && make install

```

<br>

## <u>config</u>

By default the **Banafo** module config file is not in the default FreeSWITCH config directory. You should copy it to the **'/usr/local/freeswitch/conf/autoload_configs/'**. 

```
cd /usr/src/freeswitch-banafo/mod_banafo_transcribe/

cp -v banafo_transcribe.conf.xml /usr/local/freeswitch/conf/autoload_configs/
```

<br>

**'freeswitch-banafo'** module configuration:
``` xml
<configuration name="banafo_transcribe.conf" description="Banafo Configuration">
  <system>
	  <param name="debug" value="0"/>
  </system>
  <profiles>
	<profile name="en">
		<!-- Banafo ASR server destination hostname -->
		<param name="host" value="localhost"/>

		<!-- Banafo ASR server destination port -->
		<param name="port" value="6006"/>

		<!-- ws,wss -->
		<param name="prot" value="ws"/>

		<!-- Banafo ASR server sample_rate -->
		<param name="sample_rate" value="16000"/>

		<!-- Callback URL -->
		<param name="callback_url" value="ws://127.0.0.1:8000"/>
	</profile>
		<profile name="en-US">
		<param name="host" value="app.banafo.ai"/>
		<param name="port" value="443"/>
		<param name="prot" value="wss"/>
		<param name="sample_rate" value="16000"/>
	</profile>
	<profile name="bg">
		<param name="host" value="localhost"/>
		<param name="port" value="6007"/>
		<param name="prot" value="wss"/>
		<param name="sample_rate" value="8000"/>
	</profile>
  </profiles>
</configuration>
```
<br>

---

<br>

* **freeswitch-banafo API** (call in FS-cli or in the some script) [1]
* **freeswitch-banafo APP** (call in dialplan) [2]

You can use **'uuid_banafo_transcribe'** as **FreeSWITCH API** [1]. See example in the **FS_CLI**. 

``` bash
freeswitch@banafo-fs-mod> uuid_banafo_transcribe b49d710e-e49f-4bb7-b077-43075835bca8 start en-US
```

There are examples how to use **'banafo_transcribe'** application [2] in the your FreeSWITCH dialplan:

```xml
 <extension name="local-ws">
    <condition field="destination_number" expression="^99996$">
      <action application="set" data="answer_delay=0"/>
      <action application="answer" />
      <action application="log" data="uuid: ${uuid}"/>
      <action application="set" data="lang=en"/>
      <action application="banafo_transcribe" data="${uuid} start ${lang}" />
      <action application="echo" />
      <action application="hangup" />
    </condition>
 </extension>

 <extension name="api-wss">
    <condition field="destination_number" expression="^99997$">
      <action application="set" data="answer_delay=0"/>
      <action application="answer" />
      <action application="log" data="uuid: ${uuid}"/>
      <action application="set" data="BANAFO_API_KEY=<apiKey from banafo.ai>"/>
      <action application="set" data="BANAFO_SPEECH_ENDPOINTS=true"/>
      <action application="set" data="lang=en-US"/>
      <action application="banafo_transcribe" data="${uuid} start ${lang}" />
      <action application="echo" />
      <action application="hangup" />
    </condition>
 </extension>

 <extension name="local-conf-room">
    <condition field="destination_number" expression="^99998$">
      <action application="set" data="answer_delay=0"/>
      <action application="answer" />
      <action application="log" data="uuid: ${uuid}"/>
      <action application="set" data="lang=en"/>
      <action application="banafo_transcribe" data="${uuid} start ${lang}" />
      <action application="conference" data="$1-${domain_name}@default"/>
      <action application="hangup" />
    </condition>
 </extension>

```

The **local-ws** extension is to connect to Banafo ASR server (on-premise-asr) and **'api-wss'** extention is to connect to https://banafo.ai as using streaming API.
For second case, first you should generate an apiKey in the Banafo API site. After that,you will change <apiKey from banafo.ai> with the your real apiKey.
In the FS-cli will execute **'reloadxml'**.

<br>

## <u>'freeswitch-banafo' in Docker container and tests</u>

You can test with Docker container. The need files you can find in **'./docker/'** subdir.

If you want to use the variant with **API**, you should insert your **apiKey** from https://banafo.ai in the Dockerfile.

```
cd docker/

vim Dockerfile

edit 'ENV apiKey "<apiKey from banafo.ai>"'
```
Change "\<apiKey from banafo.ai\>" with the your **apiKey**. 
For example: "1234-56789-ABCD-98765".

After that, build an image and create a container.

```
cd docker/

docker-compose up -d --build
```

You can check whether all is OK with follow commands:

```
docker ps -a

docker logs banafo-fs-mod
```

The first will show started containers but the second will show deffined container.
In this case with '**freeswitch'** and **'freeswitch-banafo'** module.

When you use **'banafo-fs-mod'** Docker container, you will have ready setup with test SIP accounts and a few extentions for testing. You won't have a Banafo on-premise server or ready Banafo account with apiKey in the https://banafo.ai site.

***Two SIP accounts for testing:***
```
SIP proxy: localhost or 127.0.0.1
SIP proxy port: 9096
SIP username: 18902
SIP password: QA*VoIP-Test_902
audio codecs: g711,opus,speex,etc
```

... and ..
```
SIP proxy: localhost or 127.0.0.1
SIP proxy port: 9096
SIP username: 18903
SIP password: QA*VoIP-Test_903
audio codecs: g711,opus,speex,etc
```

***Test extensions:***
* 99996 to call to Banafo on-premise server ( localhost )
* 99997 to call to Banafo API ( https://banafo.ai )
* 99998 to call a conference room and send audio stream to Banafo on-promise server