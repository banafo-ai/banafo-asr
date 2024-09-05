# asterisk-banafo

``` bash
git clone https://github.com/banafo-ai/banafo-asr.git

cd banafo-asr/integration-demos/asterisk/modules/asterisk-banafo/
```

## <u>Compile, install and load the 'asterisk-banafo' module</u>

When you will use **Banafo API**, you can be inserted your apiKey in the **Dockerfile** first:
```
ENV apiKey "apiKey-from-banafo.ai"
```

Replace **"apiKey-from-banafo.ai"** with your own from the https://banafo.ai !

You can be used docker container with following commands :
```
cd docker

vim Dockerfile (replace apiKey)

docker-compose up -d --build
```

If you don't want to use Docker container you could use compile and install commands.

See steps to compile and install **'res_speech_banafo'** module.

``` bash
./bootstrap
./configure --with-asterisk=/usr/src/asterisk/ --prefix=/usr/
make
make install
```
In this example,the Asterisk version is **'21.4.2'**.

If you want to use other,then should change asterisk version and the same path.


*modules.conf :*
```
load = res_speech.so
load = res_http_websocket.so
load = res_speech_banafo.so
```


If you would like to load (manualy) banafo module from Asterisk CLI
```
module load res_speech_banafo.so
```

If you would like to unload (manualy) banafo module from Asterisk CLI
```
module unload res_speech_banafo.so
```

## <u>Using of 'asterisk-banafo' in the Asterisk dialplan</u>


Example in the **'extension.conf'**
```
; to Banafo API GW with ENG models
exten => _8883,1,NoOp()
exten => _8883,n,Answer()
exten => _8883,n,SpeechCreate(ban-en-api)
exten => _8883,n,SpeechBackground(hello-world)
exten => _8883,n,Verbose(0,${SPEECH_TEXT(0)})
exten => _8883,n,Hangup()

; to Banafo ASR server with ENG models
exten => _8884,1,NoOp()
exten => _8884,n,Answer()
exten => _8884,n,SpeechCreate(ban-en)
exten => _8884,n,SpeechBackground(hello-world)
exten => _8884,n,Verbose(0,${SPEECH_TEXT(0)})
exten => _8884,n,Hangup()

; to Banafo ASR server with BG models
exten => _8885,1,NoOp()
exten => _8885,n,Answer()
exten => _8885,n,SpeechCreate(ban-bg)
exten => _8885,n,SpeechBackground(hello-world)
exten => _8885,n,Verbose(0,${SPEECH_TEXT(0)})
exten => _8885,n,Hangup()

```

## <u>Banafo ASR server</u>

The command for server starting is very easy :

```
./online-websocket-server --key=<KEY>
```

Replace **KEY** with your own from Banafo !

[Banafo ASR server/On premise ASR , link ](https://banafo.ai/en/on-premise-asr)

## <u>'asterisk-banafo' module setup</u>

/etc/asterisk/res_speech_banafo.conf

```
[general]
debug = no

[ban-en]
url = ws://localhost:6006
sample_rate = 16000
callback_url = ws://127.0.0.1:8000

[ban-bg]
url = ws://localhost:6007

[ban-en-api]
url = wss://app.banafo.ai/api/v1/transcripts/streaming
apiKey = api-key-from-banafo-ai
lang = en-US
;endpoints = false
```

The params **sample_rate** and **callback_url** are options.
If you need to use different **sample_rate** (by default it's 16000),
then you can use this option. Also when you want to send (forward) a result to the some callback server, then you should insert **callback_url**.

## <u>'asterisk-banafo' module tests </u>

1. download and install [**Zoiper5**](https://www.zoiper.com/en/voip-softphone/download/current).

2. setup SIP account with follow params:
* SIP server : 127.0.0.1:5297
* SIP username : 1107
* SIP password : TestQA*1107
* audio codecs : g711, opus or gsm

3. can dial follow extensions:
* 8883 -> call to banafo.ai/api-streeming
* 8884 -> call to Banafo ASR localserver, ENG model
* 8885 -> call to Banafo ASR localserver, BG model

You can be used follow command to show a current Asterisk log file :
```
docker logs -f  banafo-ast-mod
```