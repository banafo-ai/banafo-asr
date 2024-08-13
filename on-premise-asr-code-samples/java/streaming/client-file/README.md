# Java

This sample shows how to transcribe streaming audio via simulation.

## Prerequisites

In order to run the code sample, the required dependencies need to be installed first. This can be done by running the following command:

```sh
mvn clean compile
```

## Running

```sh
mvn exec:java -Dexec.args="file1.wav"
```

The file must be of WAVE format with a single channel and 16 kHz sample rate.

All options can be seen by running the following command:

```sh
mvn exec:java -Dexec.args="-help"
```
