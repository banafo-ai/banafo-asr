#!/usr/bin/env python3

"""
A websocket client for banafo-streaming-server

Usage:
    ./streaming-client-file.py \
      --addr localhost \
      --port 6006 \
      --seconds-per-message 0.1 \
      --samples-per-message 8000 \
      /path/to/foo.wav

(Note: You have to first start the server before starting the client)
"""

import argparse
import asyncio
import logging
import wave
import websockets
import numpy as np


def read_wav(file):
    with wave.open(file) as f:
        assert f.getframerate() == 16000, f.getframerate()
        assert f.getnchannels() == 1, f.getnchannels()
        assert f.getsampwidth() == 2, f.getsampwidth()
        frames = f.readframes(f.getnframes())
        samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768
        return samples


def get_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )

    parser.add_argument(
        "--addr",
        type=str,
        default="localhost",
        help="Address of the server",
    )

    parser.add_argument(
        "--port",
        type=int,
        default=6006,
        help="Port of the server",
    )

    parser.add_argument(
        "--samples-per-message",
        type=int,
        default=8000,
        help="Number of samples per message",
    )

    parser.add_argument(
        "--seconds-per-message",
        type=float,
        default=0.1,
        help="We will simulate that the duration of two messages is of this value",
    )

    parser.add_argument(
        "file",
        type=str,
        help="Input WAV file to decode (single channel, 16-bit int, 16 kHz sample rate)",
    )

    return parser.parse_args()


async def receive_results(socket):
    last_message = ""
    async for message in socket:
        if message != "Done!":
            last_message = message
            logging.info(message)
        else:
            break
    return last_message


async def transcribe(
    addr,
    port,
    file,
    samples_per_message,
    seconds_per_message,
):
    samples = read_wav(file)

    async with websockets.connect(
        f"ws://{addr}:{port}"
    ) as websocket:
        logging.info(f"Sending {file}")

        receive_task = asyncio.create_task(receive_results(websocket))

        start = 0
        while start < samples.shape[0]:
            end = start + samples_per_message
            end = min(end, samples.shape[0])
            d = samples.data[start:end].tobytes()

            await websocket.send(d)

            # Simulate streaming. You can remove the sleep if you want
            await asyncio.sleep(seconds_per_message)  # in seconds

            start += samples_per_message

        # to signal that the client has sent all the data
        await websocket.send("Done")

        decoding_results = await receive_task


async def main():
    args = get_args()
    logging.info(vars(args))

    addr = args.addr
    port = args.port
    file = args.file
    samples_per_message = args.samples_per_message
    seconds_per_message = args.seconds_per_message

    await transcribe(
        addr=addr,
        port=port,
        file=file,
        samples_per_message=samples_per_message,
        seconds_per_message=seconds_per_message,
    )


if __name__ == "__main__":
    fmt = "%(asctime)s %(levelname)s [%(filename)s:%(lineno)d] %(message)s"
    logging.basicConfig(format=fmt, level=logging.INFO)
    asyncio.run(main())
