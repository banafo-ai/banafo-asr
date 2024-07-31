# Go

This sample shows how to transcribe multiple files in parallel.

## Prerequisites

In order to run the code sample, the required dependencies need to be installed first, and it needs to be built. This can be done by running the following command:

```sh
go build
```

## Running

```sh
./pre-recorded-client-parallel file1.wav file2.wav file3.wav ...
```

Each file must be of WAVE format with a single channel. The sample rate of the file can be arbitrary and does not need to be 16 kHz.

All options can be seen by running the following command:

```sh
./pre-recorded-client-parallel --help
```
