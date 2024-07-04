#!/usr/bin/env python3

"""
A websocket client for post-processed-websocket-server

This file shows how to transcribe multiple
files in parallel. We create a separate connection for transcribing each file.

Usage:
    ./post-processed-client-parallel.py \
      --addr localhost \
      --port 6006 \
      /path/to/foo.wav \
      /path/to/bar.wav \
      /path/to/16kHz.wav \
      /path/to/8kHz.wav

(Note: You have to first start the server before starting the client)
"""

import argparse
import asyncio
import logging
import wave
import websockets
import numpy as np


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
        "files",
        type=str,
        nargs="+",
        help="Input WAV files to decode (single channel, 16-bit int, any sample rate)",
    )

    return parser.parse_args()


def read_wav(file):
    with wave.open(file) as f:
        assert f.getnchannels() == 1, f.getnchannels()
        assert f.getsampwidth() == 2, f.getsampwidth()
        frames = f.readframes(f.getnframes())
        samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768
        return samples, f.getframerate()


async def transcribe(addr, port, file):
    async with websockets.connect(f"ws://{addr}:{port}", max_size=3000000) as ws:
        logging.info(f"Sending {file}")
        samples, rate = read_wav(file)
        buf = rate.to_bytes(4, byteorder="little") + (samples.size * 4).to_bytes(4, byteorder="little") + samples.tobytes()

        for i in range(0, len(buf), 1_000_000):
            await ws.send(buf[i:i + 1_000_000])

        result = await ws.recv()
        logging.info(f"{file}\n{result}")

        await ws.send("Done")


async def main():
    args = get_args()
    logging.info(vars(args))

    tasks = [transcribe(args.addr, args.port, f) for f in args.files]
    await asyncio.gather(*tasks)


if __name__ == "__main__":
    fmt = "%(asctime)s %(levelname)s [%(filename)s:%(lineno)d] %(message)s"
    logging.basicConfig(format=fmt, level=logging.INFO)
    asyncio.run(main())
