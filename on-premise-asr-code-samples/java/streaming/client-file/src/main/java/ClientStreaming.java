
import org.apache.commons.cli.*;

import javax.sound.sampled.*;
import javax.websocket.*;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CountDownLatch;
import java.util.logging.Level;
import java.util.logging.Logger;

@ClientEndpoint
public class ClientStreaming {

    public static final String ARG_HELP = "help";
    public static final String ARG_SERVER_ADDR = "addr";
    public static final String ARG_SERVER_PORT = "port";
    public static final String ARG_WAV_FILE = "file";
    public static final String ARG_SECONDS_PER_MESSAGE = "seconds-per-message";
    public static final String ARG_SAMPLES_PER_MESSAGE = "samples-per-message";
    private static final Logger logger = Logger.getLogger(ClientStreaming.class.getName());
    private CountDownLatch latch;
    private final String serverAddr;
    private final int serverPort;
    long milliesPerMessage;
    int samplesPerMessage;
    private final String soundFile;
    private byte[] waveData;
    private Session session;

    public ClientStreaming(String serverAddr, int serverPort, String soundFile,
                           long milliesPerMessage,
                           int samplesPerMessage) {
        this.soundFile = soundFile;
        this.serverAddr = serverAddr;
        this.serverPort = serverPort;
        this.milliesPerMessage = milliesPerMessage;
        this.samplesPerMessage = samplesPerMessage;
    }

    public static void main(String[] args) throws Exception {
        Options options = new Options();
        options.addOption(ARG_HELP, false, "List all options");
        options.addOption(ARG_SERVER_ADDR, true, "Address of the server");
        options.addOption(ARG_SERVER_PORT, true, "Port of the server");
        options.addOption(ARG_SAMPLES_PER_MESSAGE, true, "Number of samples per message");
        options.addOption(ARG_SECONDS_PER_MESSAGE,
                          true,
                          "We will simulate that the duration of two messages is of this value");
        Option optionFileList = new Option(ARG_WAV_FILE,
                                           true,
                                           "Input WAV file to decode (single channel, 16-bit int, 16 kHz sample rate)");
        optionFileList.setArgs(1);
        optionFileList.setValueSeparator(' ');
        options.addOption(optionFileList);
        CommandLine cmd = new DefaultParser().parse(options, args);

        if (cmd.hasOption(ARG_HELP)) {
            new HelpFormatter().printHelp(
                    "-addr localhost -port 6006 -seconds-per-message 0.1 -samples-per-message 8000 -file file1.wav",
                    options);
        }
        String serverAddr = cmd.getOptionValue(ARG_SERVER_ADDR, "localhost");
        int serverPort = Integer.parseInt(cmd.getOptionValue(ARG_SERVER_PORT, "6006"));
        long milliesPerMessage = (long) (Float.parseFloat(cmd.getOptionValue(ARG_SECONDS_PER_MESSAGE, "0.1")) * 1000);
        int samplesPerMessage = Integer.parseInt(cmd.getOptionValue(ARG_SAMPLES_PER_MESSAGE, "8000"));
        if (cmd.getArgList().isEmpty()) {
            return;
        }
        String waveFile = cmd.getArgList().get(0);

        new Thread(() -> {
            logger.info("waveFilename: " + waveFile);
            ClientStreaming client = new ClientStreaming(serverAddr, serverPort, waveFile,
                                                         milliesPerMessage, samplesPerMessage);
            client.sendWaveFile();
        }).start();
    }

    private void sendWaveFile() {
        try {
            waveData = readWave(soundFile);
        } catch (UnsupportedAudioFileException | IOException e) {
            throw new RuntimeException(e);
        }

        latch = new CountDownLatch(1);
        WebSocketContainer container = ContainerProvider.getWebSocketContainer();
        String uri = "ws://" + serverAddr + ":" + serverPort;
        try {
            container.connectToServer(this, new URI(uri));
            latch.await();  // Wait for the response
            // Close the session after sending "Done"
            session.close(new CloseReason(CloseReason.CloseCodes.NORMAL_CLOSURE, "Completed sending data"));
        } catch (InterruptedException | URISyntaxException | IOException | DeploymentException e) {
            throw new RuntimeException(e);
        }
    }

    private static byte[] readWave(String waveFilename) throws UnsupportedAudioFileException, IOException {
        File file = new File(waveFilename);
        AudioInputStream audioInputStream = AudioSystem.getAudioInputStream(file);
        AudioFormat format = audioInputStream.getFormat();

        if (format.getSampleRate() != 16000 || format.getChannels() != 1 || format.getSampleSizeInBits() != 16) {
            throw new IllegalArgumentException("Wave file must have 16000Hz, mono channel, 16-bit samples");
        }

        ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
        byte[] buffer = new byte[1024];
        int bytesRead;
        while ((bytesRead = audioInputStream.read(buffer)) != -1) {
            outputStream.write(buffer, 0, bytesRead);
        }
        byte[] samples = outputStream.toByteArray();
        audioInputStream.close();
        // Convert the byte array to float array.
        ShortBuffer shortBuffer = ByteBuffer.wrap(samples).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer();
        float[] samplesAsFloat = new float[shortBuffer.capacity()];
        for (int i = 0; i < shortBuffer.capacity(); i++) {
            samplesAsFloat[i] = shortBuffer.get(i) / 32768.0f;
        }
        // Each float is 4 bytes so the byte buffer needs to be 4 times bigger than the float array
        ByteBuffer floatByteBuffer = ByteBuffer.allocate(samplesAsFloat.length * 4).order(ByteOrder.LITTLE_ENDIAN);
        for (float sample : samplesAsFloat) {
            floatByteBuffer.putFloat(sample);
        }
        // The buffer needs to be flipped to make it in the correct order
        floatByteBuffer.flip();
        // The websocket sends the audio as a byte string
        byte[] byteArray = new byte[floatByteBuffer.remaining()];
        floatByteBuffer.get(byteArray);

        return byteArray;
    }

    @OnOpen
    public void onOpen(Session session) throws IOException {
        logger.info("Connected to server");
        this.session = session;
        new Thread(() -> {
            try {
                int start = 0;
                while (start < waveData.length) {
                    int end = Math.min(start + samplesPerMessage * 2, waveData.length);
                    byte[] data = new byte[end - start];
                    System.arraycopy(waveData, start, data, 0, end - start);
                    session.getBasicRemote().sendBinary(ByteBuffer.wrap(data));
                    start += samplesPerMessage * 2;
                    Thread.sleep(milliesPerMessage);
                }
                session.getBasicRemote().sendText("Done");
            } catch (Exception e) {
                logger.log(Level.SEVERE, "Exception in onOpen: ", e);
            }
        }).start();
    }

    @OnMessage
    public void onMessage(String message) {
        if (!"Done!".equals(message)) {
            logger.info(message);
        } else {
            latch.countDown();
        }
    }

    @OnError
    public void onError(Session session, Throwable throwable) {
        logger.log(Level.SEVERE, "WebSocket error: ", throwable);
        latch.countDown();
    }

    @OnClose
    public void onClose(Session session, CloseReason closeReason) {
        logger.info("Connection closed: " + closeReason);
        latch.countDown();
    }
}
