#!/usr/bin/env node

/*
 * A websocket client for post-processed-websocket-server
 *
 * This file shows how to use a single connection to transcribe multiple
 * files sequentially.
 *
 * Usage:
 *     ./post-processed-client-sequential.mjs \
 *       --addr localhost \
 *       --port 6006 \
 *       /path/to/foo.wav \
 *       /path/to/bar.wav \
 *       /path/to/16kHz.wav \
 *       /path/to/8kHz.wav
 *
 * (Note: You have to first start the server before starting the client)
 */

import fsPromises from "node:fs/promises";
import { promisify } from "node:util";
import { WebSocket } from "ws";
import wavefile from "wavefile";
import { hideBin } from "yargs/helpers";
import yargs from "yargs/yargs";

const getArgs = () => {
  return yargs(hideBin(process.argv))
    .scriptName("post-processed-client-sequential.mjs")
    .command("$0 [addr] [port] <files ...>", "")
    .option("addr", {
      type: "string",
      default: "localhost",
      description: "Address of the server",
    })
    .option("port", {
      type: "number",
      default: 6006,
      description: "Port of the server",
    })
    .positional("files", {
      type: "string",
      description:
        "Input WAV files to decode (single channel, any sample rate)",
    })
    .strict()
    .wrap(yargs().terminalWidth())
    .parse();
};

const connect = async (url) => {
  return new Promise((resolve, reject) => {
    const connection = new WebSocket(url);

    connection.onopen = () => {
      resolve({
        send: promisify(connection.send.bind(connection)),
        recv: () => {
          return new Promise((resolve) => {
            connection.once("message", (data) => resolve(data));
          });
        },
        close: connection.close.bind(connection, 1000),
      });
    };

    connection.onerror = () => reject(new Error("Connection failure"));

    connection.onclose = (ev) => {
      reject(new Error(ev.reason));
    };
  });
};

const readWav = async (file) => {
  const f = new wavefile.WaveFile(await fsPromises.readFile(file));

  if (f.fmt.numChannels !== 1) {
    throw new Error("Only mono audio files are supported");
  }

  f.toBitDepth("32f");
  return [f.getSamples(false, Float32Array), f.fmt.sampleRate];
};

const transcribe = async (addr, port, files) => {
  let websocket;

  try {
    websocket = await connect(`ws://${addr}:${port}`);

    for (const file of files) {
      console.info(`Sending ${file}`);
      const [samples, sampleRate] = await readWav(file);
      const buf = Buffer.alloc(8 + samples.byteLength);
      buf.writeUInt32LE(sampleRate, 0);
      buf.writeUInt32LE(samples.byteLength, 4);
      buf.set(Buffer.from(samples.buffer), 8);

      for (let chunk = 0; chunk < buf.length; chunk += 1_000_000) {
        const end = Math.min(buf.length, chunk + 1_000_000);
        await websocket.send(buf.subarray(chunk, end));
      }

      const decodingResults = await websocket.recv();
      console.log(decodingResults.toString());
    }

    // to signal that the client has sent all the data
    await websocket.send("Done");
  } finally {
    websocket?.close();
  }
};

const main = async () => {
  const args = getArgs();

  const addr = args.addr;
  const port = args.port;
  const files = args.files;

  await transcribe(addr, port, files);
};

main();
