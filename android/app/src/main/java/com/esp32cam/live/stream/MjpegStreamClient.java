package com.esp32cam.live.stream;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Network;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MjpegStreamClient implements AutoCloseable {
    public interface Listener {
        void onFrame(Bitmap bitmap, double fps, long kbps);
        void onError(String message);
    }

    private static final String STREAM_URL = "http://192.168.4.1:81/stream";
    private static final int MAX_JPEG_BYTES = 1024 * 1024;

    private final Network network;
    private final Listener listener;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private volatile boolean running;
    private volatile HttpURLConnection connection;

    public MjpegStreamClient(Network network, Listener listener) {
        this.network = network;
        this.listener = listener;
    }

    public void start() {
        if (running) {
            return;
        }
        running = true;
        executor.execute(this::readLoop);
    }

    private void readLoop() {
        long windowStartedNs = System.nanoTime();
        long windowBytes = 0;
        int windowFrames = 0;
        try {
            connection = (HttpURLConnection) network.openConnection(new URL(STREAM_URL));
            connection.setConnectTimeout(3000);
            connection.setReadTimeout(4000);
            connection.setUseCaches(false);
            connection.connect();
            if (connection.getResponseCode() != HttpURLConnection.HTTP_OK) {
                throw new IOException("Video HTTP " + connection.getResponseCode());
            }

            try (BufferedInputStream input = new BufferedInputStream(
                    connection.getInputStream(), 32 * 1024)) {
                while (running) {
                    int contentLength = readPartHeader(input);
                    if (contentLength <= 0 || contentLength > MAX_JPEG_BYTES) {
                        throw new IOException("Invalid JPEG length: " + contentLength);
                    }
                    byte[] jpeg = readExactly(input, contentLength);
                    Bitmap bitmap = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length);
                    if (bitmap == null) {
                        continue;
                    }

                    windowFrames++;
                    windowBytes += jpeg.length;
                    long nowNs = System.nanoTime();
                    double elapsed = (nowNs - windowStartedNs) / 1_000_000_000.0;
                    double fps = elapsed > 0 ? windowFrames / elapsed : 0;
                    long kbps = elapsed > 0
                            ? Math.round(windowBytes * 8.0 / elapsed / 1000.0) : 0;
                    listener.onFrame(bitmap, fps, kbps);
                    if (elapsed >= 1.0) {
                        windowStartedNs = nowNs;
                        windowFrames = 0;
                        windowBytes = 0;
                    }
                }
            }
        } catch (IOException error) {
            if (running) {
                listener.onError(error.getMessage() == null
                        ? error.getClass().getSimpleName() : error.getMessage());
            }
        } finally {
            running = false;
            if (connection != null) {
                connection.disconnect();
                connection = null;
            }
        }
    }

    private static int readPartHeader(BufferedInputStream input) throws IOException {
        String line;
        do {
            line = readAsciiLine(input);
            if (line == null) {
                throw new EOFException("Video stream ended");
            }
        } while (!line.startsWith("--"));

        int contentLength = -1;
        while ((line = readAsciiLine(input)) != null && !line.isEmpty()) {
            String lower = line.toLowerCase(Locale.US);
            if (lower.startsWith("content-length:")) {
                contentLength = Integer.parseInt(line.substring(line.indexOf(':') + 1).trim());
            }
        }
        return contentLength;
    }

    private static String readAsciiLine(BufferedInputStream input) throws IOException {
        ByteArrayOutputStream line = new ByteArrayOutputStream(128);
        int value;
        boolean gotAny = false;
        while ((value = input.read()) != -1) {
            gotAny = true;
            if (value == '\n') {
                break;
            }
            if (value != '\r') {
                line.write(value);
            }
            if (line.size() > 4096) {
                throw new IOException("MJPEG header is too large");
            }
        }
        if (!gotAny && value == -1) {
            return null;
        }
        return line.toString(StandardCharsets.US_ASCII.name());
    }

    private static byte[] readExactly(BufferedInputStream input, int length)
            throws IOException {
        byte[] data = new byte[length];
        int offset = 0;
        while (offset < length) {
            int count = input.read(data, offset, length - offset);
            if (count < 0) {
                throw new EOFException("JPEG frame ended early");
            }
            offset += count;
        }
        return data;
    }

    @Override
    public void close() {
        running = false;
        if (connection != null) {
            connection.disconnect();
        }
        executor.shutdownNow();
    }
}
