package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"time"

	"github.com/go-audio/audio"
	"github.com/go-audio/wav"
	"github.com/gorilla/websocket"
)

/*
 * A websocket client for streaming-websocket-server
 *
 * Usage:
 *     ./streaming-client-file \
 *       --addr localhost \
 *       --port 6006 \
 *       --seconds-per-message 0.1 \
 *       --samples-per-message 8000 \
 *       /path/to/foo.wav
 *
 * (Note: You have to first start the server before starting the client)
 */

var addr = flag.String("addr", "localhost", "Address of the server")
var port = flag.String("port", "6006", "Port of the server")
var samples_per_message = flag.Int("samples-per-message", 8_000, "Number of samples per message")
var seconds_per_message = flag.Float64("seconds-per-message", 0.1, "We will simulate that the duration of two messages is of this value")

func write(conn *websocket.Conn, wav_decoder *wav.Decoder, audio_buffer *audio.IntBuffer) {
	time.Sleep(time.Second * time.Duration(*seconds_per_message))
	for {
		w, err := conn.NextWriter(websocket.BinaryMessage)

		if err != nil {
			log.Fatal(err)
		}

		defer w.Close()

		read_b, err := wav_decoder.PCMBuffer(audio_buffer)

		if err != nil {
			log.Fatal(err)
		}

		if read_b == 0 {
			doneWriter, _ := conn.NextWriter(websocket.TextMessage)

			doneWriter.Write([]byte("Done"))
			doneWriter.Close()
			break
		}

		audio_f32 := audio_buffer.AsFloat32Buffer().Data

		binary.Write(w, binary.LittleEndian, audio_f32)
	}
}

func read(conn *websocket.Conn) {
	for {
		t, r, err := conn.NextReader()

		if err != nil {
			if websocket.IsCloseError(err, websocket.CloseNormalClosure) {
				log.Println("Finished")
				break
			} else {
				log.Fatal(err)
			}
		}

		if t != websocket.TextMessage {
			log.Fatal("Expected text message")
		}

		res_bytes := &bytes.Buffer{}

		io.Copy(res_bytes, r)

		message := res_bytes.String()

		if message == "Done!" {
			conn.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.CloseNormalClosure, "Done"), time.Now().Add(time.Second*10))
		}

		log.Println(res_bytes.String())
	}
}

func main() {
	flag.Parse()
	file_paths := flag.Args()

	if len(file_paths) == 0 {
		log.Fatal("No file paths provided")
	}

	file, err := os.Open(file_paths[0])

	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	wav_decoder := wav.NewDecoder(file)

	if !wav_decoder.IsValidFile() {
		log.Fatal("Invalid file")
	}

	wav_decoder.FwdToPCM()

	if wav_decoder.SampleRate != 16000 {
		log.Fatal("Only 16 kHz sample rate supported")
	}

	if wav_decoder.NumChans != 1 {
		log.Fatal("Only mono audio files are supported")
	}

	url := fmt.Sprintf("ws://%s:%s", *addr, *port)
	client := &websocket.Dialer{}

	log.Printf("Connecting to %s", url)

	conn, res, err := client.Dial(url, nil)

	if err != nil {
		log.Println(*res)
		log.Fatal(err)
	}

	audio_buffer := &audio.IntBuffer{
		Data: make([]int, *samples_per_message),
	}

	go write(conn, wav_decoder, audio_buffer)
	read(conn)
}
