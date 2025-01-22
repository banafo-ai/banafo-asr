#!/usr/bin/env node

/*
 * A websocket client for hosted API
 *
 * Usage:
 *     ./streaming-client-file.mjs \
 *       --api-key f2e9d370-6146-11ef-a150-cde0bc2e1668.d8de95feaa7637603e1f0187709f644ca5995e3d \
 *       --language en-US \
 *       --endpoints true \
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
      "$0 [api-key] [language] [endpoints] [samples-per-message] [seconds-per-message] <file>",
      "",
    )
    .option("api-key", {
      type: "string",
      demandOption: true,
      description: "API key",
    })
    .option("language", {
      type: "string",
      default: "en-US",
      description: "Transcript language",
    })
    .option("endpoints", {
      type: "string",
      choices: ["false", "true"],
      default: "true",
      description: "Enable endpointing",
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
  apiKey,
  language,
  endpoints,
  file,
  samplesPerMessage,
  secondsPerMessage,
) => {
  let websocket;

  try {
    const samples = await readWav(file);

    websocket = new WebSocket(
      `wss://app.banafo.ai/api/v1/transcripts/streaming?apiKey=${apiKey}&languageCode=${language}&endpoints=${endpoints}`,
    );

    websocket.addEventListener("message", (ev) => {
      console.log("Message:", ev.data);
    });

    websocket.addEventListener("close", (ev) => {
      console.log(
        'Connection closed (wasClean: %s, code: %d, reason: "%s")',
        ev.wasClean,
        ev.code,
        ev.reason,
      );
    });

    console.info(`Sending ${file}`);

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
  } finally {
    websocket?.close();
  }
};

const main = async () => {
  const args = getArgs();

  const apiKey = args.apiKey;
  const language = args.language;
  const endpoints = args.endpoints;
  const file = args.file;
  const samplesPerMessage = args.samplesPerMessage;
  const secondsPerMessage = args.secondsPerMessage;

  await transcribe(
    apiKey,
    language,
    endpoints,
    file,
    samplesPerMessage,
    secondsPerMessage,
  );
};

main();
