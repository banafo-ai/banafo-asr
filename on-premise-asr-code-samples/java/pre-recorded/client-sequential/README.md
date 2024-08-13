# Java

This sample shows how to use a single connection to transcribe multiple files sequentially.

## Prerequisites

In order to run the code sample, the required dependencies need to be installed first. This can be done by running the following command:

```sh
mvn clean compile
```

## Running

```sh
mvn exec:java -Dexec.args="file1.wav file2.wav"
```

Each file must be of WAVE format with a single channel. The sample rate of the file can be arbitrary and does not need to be 16 kHz.

All options can be seen by running the following command:

```sh
mvn exec:java -Dexec.args="-help"
```
