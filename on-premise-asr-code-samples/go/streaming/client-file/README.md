# Go

This sample shows how to transcribe streaming audio via simulation.

## Prerequisites

In order to run the code sample, the required dependencies need to be installed first, and it needs to be built. This can be done by running the following command:

```sh
go build
```

## Running

```sh
./streaming-client-file file.wav
```

The file must be of WAVE format with a single channel and 16 kHz sample rate.

All options can be seen by running the following command:

```sh
./streaming-client-file --help
```
