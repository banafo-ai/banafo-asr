package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"sync"
	"time"

	"github.com/go-audio/wav"
	"github.com/gorilla/websocket"
)

/*
 * A websocket client for banafo-pre-recorded-server
 *
 * This file shows how to transcribe multiple
 * files in parallel. We create a separate connection for transcribing each file.
 *
 * Usage:
 *     ./pre-recorded-client-parallel \
 *       --addr localhost \
 *       --port 6006 \
 *       /path/to/foo.wav \
 *       /path/to/bar.wav \
 *       /path/to/16kHz.wav \
 *       /path/to/8kHz.wav
 *
 * (Note: You have to first start the server before starting the client)
 */

var addr = flag.String("addr", "localhost", "Address of the server")
var port = flag.String("port", "6006", "Port of the server")

func read_message(conn *websocket.Conn, i int) {
	t, reader, err := conn.NextReader()

	if err != nil {
		log.Fatal(err)
	}

	if t != websocket.TextMessage {
		log.Fatal("Expected text message")
	}

	res_bytes := &bytes.Buffer{}

	io.Copy(res_bytes, reader)

	log.Println(res_bytes.String())
}

func send_audio(conn *websocket.Conn, chunk []byte) {
	limit := 1_000_000
	for i := 0; i < len(chunk); i += limit {
		end := i + limit
		if end > len(chunk) {
			end = len(chunk)
		}
		writer, err := conn.NextWriter(websocket.BinaryMessage)

		if err != nil {
			log.Fatal(err)
		}

		writer.Write(chunk[i:end])
		writer.Close()
	}
}

func send_recv(conn *websocket.Conn, file_path *string, i *int, wg *sync.WaitGroup) {

	file, err := os.Open(*file_path)

	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	wav_decoder := wav.NewDecoder(file)

	if !wav_decoder.IsValidFile() {
		log.Fatal("Invalid file")
	}

	wav_decoder.FwdToPCM()

	if wav_decoder.NumChans != 1 {
		log.Fatal("Only mono audio files are supported")
	}

	audio_buffer, err := wav_decoder.FullPCMBuffer()

	if err != nil {
		log.Fatal(err)
	}

	data_32float := audio_buffer.AsFloat32Buffer().Data

	buff := bytes.NewBuffer(make([]byte, 0, len(data_32float)*4))

	binary.Write(buff, binary.LittleEndian, uint32(wav_decoder.SampleRate))

	binary.Write(buff, binary.LittleEndian, uint32(len(data_32float)*4))

	binary.Write(buff, binary.LittleEndian, data_32float)

	chunk := buff.Bytes()
	go send_audio(conn, chunk)
	read_message(conn, *i)

	// to signal that the client has sent all the data
	conn.WriteControl(
		websocket.CloseMessage,
		websocket.FormatCloseMessage(websocket.CloseNormalClosure, "Done"),
		time.Now().Add(time.Second*10),
	)
	wg.Done()
}

func main() {

	flag.Parse()
	file_paths := flag.Args()

	url := fmt.Sprintf("ws://%s:%s", *addr, *port)
	client := &websocket.Dialer{}

	wg := &sync.WaitGroup{}

	for i, file_path := range file_paths {

		conn, res, err := client.Dial(url, nil)

		if err != nil {
			log.Println(err)
			log.Fatal(*res)
		}

		wg.Add(1)
		go send_recv(conn, &file_path, &i, wg)
	}
	wg.Wait()
}
