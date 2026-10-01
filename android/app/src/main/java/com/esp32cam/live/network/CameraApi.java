package com.esp32cam.live.network;

import android.net.Network;

import com.esp32cam.live.BuildConfig;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class CameraApi implements AutoCloseable {
    public interface Callback {
        void onComplete(boolean success, String message);
    }

    private static final String CONTROL_URL = "http://" + BuildConfig.CAMERA_HOST
            + ":" + BuildConfig.CONTROL_PORT + "/control?";
    private final Network network;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    public CameraApi(Network network) {
        this.network = network;
    }

    public void setAutoExposure(boolean enabled, Callback callback) {
        send("auto=" + (enabled ? "1" : "0"), callback);
    }

    public void setExposure(int value, Callback callback) {
        send("exposure=" + value, callback);
    }

    public void setResolution(String value, Callback callback) {
        send("resolution=" + value, callback);
    }

    public void setQuality(int value, Callback callback) {
        send("quality=" + value, callback);
    }

    private void send(String query, Callback callback) {
        executor.execute(() -> {
            HttpURLConnection connection = null;
            try {
                connection = (HttpURLConnection) network.openConnection(
                        new URL(CONTROL_URL + query));
                connection.setConnectTimeout(2000);
                connection.setReadTimeout(2000);
                connection.setUseCaches(false);
                int status = connection.getResponseCode();
                callback.onComplete(status == HttpURLConnection.HTTP_OK,
                        "HTTP " + status);
            } catch (IOException error) {
                callback.onComplete(false, error.getMessage());
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
        });
    }

    @Override
    public void close() {
        executor.shutdownNow();
    }
}
