import org.apache.commons.cli.*;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.UnsupportedAudioFileException;
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
import java.util.concurrent.CountDownLatch;
import java.util.logging.Level;
import java.util.logging.Logger;

@ClientEndpoint
public class ClientParallel {
    public static final String ARG_HELP = "help";
    public static final String ARG_SERVER_ADDR = "addr";
    public static final String ARG_SERVER_PORT = "port";
    public static final String ARG_WAV_FILE_LIST = "files";
    private static final Logger logger = Logger.getLogger(ClientParallel.class.getName());
    private CountDownLatch latch;
    private final String serverAddr;
    private final int serverPort;
    private final String waveFilename;
    private Session session;
    private final int samplesPerMessage = 1000000;

    public ClientParallel(String serverAddr, int serverPort, String waveFilename) {
        this.waveFilename = waveFilename;
        this.serverAddr = serverAddr;
        this.serverPort = serverPort;
    }

    public static void main(String[] args) throws Exception {
        Options options = new Options();
        options.addOption(ARG_HELP, false, "List all options");
        options.addOption(ARG_SERVER_ADDR, true, "Address of the server");
        options.addOption(ARG_SERVER_PORT, true, "Port of the server");
        Option optionFileList = new Option(ARG_WAV_FILE_LIST,true,"Input WAV files to decode (single channel, 16-bit int, any sample rate)");
        optionFileList.setValueSeparator(' ');
        options.addOption(optionFileList);
        CommandLine cmd = new DefaultParser().parse(options, args);

        if (cmd.hasOption(ARG_HELP)) {
            new HelpFormatter().printHelp("-addr localhost -port 6006 file1.wav file2.wav", options);
        }
        String serverAddr = cmd.getOptionValue(ARG_SERVER_ADDR, "localhost");
        int serverPort = Integer.parseInt(cmd.getOptionValue(ARG_SERVER_PORT, "6006"));
        List<String> waveFilenames = cmd.getArgList();
        if (waveFilenames.isEmpty()) {
            return;
        }

        for (String waveFilename : waveFilenames) {
            logger.info("waveFilename: " + waveFilename);
            new Thread(() -> {
                ClientParallel client = new ClientParallel(serverAddr, serverPort, waveFilename);
                client.sendWaveFile();
            }).start();
        }
    }

    public void sendWaveFile() {
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

        if (format.getChannels() != 1 || format.getSampleSizeInBits() != 16) {
            throw new UnsupportedAudioFileException("Only single channel 16-bit audio is supported.");
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
                byte[] waveData;
                waveData = readWave(waveFilename);
                ByteBuffer buffer = ByteBuffer.allocate(8 + waveData.length).order(ByteOrder.LITTLE_ENDIAN);
                buffer.putInt(16000);  // Assuming 16kHz sample rate
                buffer.putInt(waveData.length);
                buffer.put(waveData);

                byte[] buf = buffer.array();
                for (int chunk = 0; chunk < buf.length; chunk += samplesPerMessage) {
                    int end = Math.min(buf.length, chunk + samplesPerMessage);
                    session.getBasicRemote().sendBinary(ByteBuffer.wrap(buf, chunk, end - chunk));
                }
            } catch (Exception e) {
                logger.log(Level.SEVERE, "Exception in onOpen: ", e);
                latch.countDown();
            }
        }).start();
    }

    @OnMessage
    public void onMessage(String message) {
        logger.info("File: " + waveFilename + " Received message: " + message);
        latch.countDown();
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
