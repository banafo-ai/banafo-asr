#!/usr/bin/env node

/*
 * A websocket client for banafo-streaming-server
 *
 * Usage:
 *     ./streaming-client-file.mjs \
 *       --addr localhost \
 *       --port 6006 \
 *       --seconds-per-message 0.1 \
 *       --samples-per-message 8000 \
 *       /path/to/foo.wav
 *
 * (Note: You have to first start the server before starting the client)
 */

import fsPromises from "node:fs/promises";
import { WebSocket } from "ws";
import wavefile from "wavefile";
import { hideBin } from "yargs/helpers";
import yargs from "yargs/yargs";

const getArgs = () => {
  return yargs(hideBin(process.argv))
    .scriptName("streaming-client-file.mjs")
    .command(
      "$0 [addr] [port] [samples-per-message] [seconds-per-message] <file>",
      "",
    )
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
    .option("samples-per-message", {
      type: "number",
      default: 8000,
      description: "Number of samples per message",
    })
    .option("seconds-per-message", {
      type: "number",
      default: 0.1,
      description:
        "We will simulate that the duration of two messages is of this value",
    })
    .positional("file", {
      type: "string",
      description:
        "Input WAV file to decode (single channel, 16 kHz sample rate)",
    })
    .strict()
    .wrap(yargs().terminalWidth())
    .parse();
};

const sleep = (seconds) => {
  return new Promise((resolve) => {
    setTimeout(resolve, seconds * 1000);
  });
};

const readWav = async (file) => {
  const f = new wavefile.WaveFile(await fsPromises.readFile(file));

  if (f.fmt.sampleRate !== 16000) {
    throw new Error("Only 16 kHz sample rate supported");
  }

  if (f.fmt.numChannels !== 1) {
    throw new Error("Only mono audio files are supported");
  }

  f.toBitDepth("32f");
  return f.getSamples(false, Float32Array);
};

const transcribe = async (
  addr,
  port,
  file,
  samplesPerMessage,
  secondsPerMessage,
) => {
  let websocket;

  try {
    const samples = await readWav(file);

    websocket = new WebSocket(`ws://${addr}:${port}`);

    console.info(`Sending ${file}`);

    const donePromise = new Promise((resolve) => {
      websocket.onmessage = (ev) => {
        const message = ev.data;
        if (message === "Done!") {
          resolve();
        } else {
          console.log(message);
        }
      };
    });

    await new Promise((resolve, reject) => {
      websocket.once("open", () => resolve());
      websocket.once("close", () => reject(new Error("Connection failure")));
    });

    let start = 0;
    while (start < samples.length) {
      let end = start + samplesPerMessage;
      end = Math.min(end, samples.length);
      const d = samples.subarray(start, end);

      websocket.send(d);

      // Simulate streaming. You can remove the sleep if you want
      await sleep(secondsPerMessage); // in seconds

      start += samplesPerMessage;
    }

    // to signal that the client has sent all the data
    websocket.send("Done");

    await donePromise;
  } finally {
    websocket?.close();
  }
};

const main = async () => {
  const args = getArgs();

  const addr = args.addr;
  const port = args.port;
  const file = args.file;
  const samplesPerMessage = args.samplesPerMessage;
  const secondsPerMessage = args.secondsPerMessage;

  await transcribe(addr, port, file, samplesPerMessage, secondsPerMessage);
};

main();
