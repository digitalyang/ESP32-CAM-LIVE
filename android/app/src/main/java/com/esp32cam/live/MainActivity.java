package com.esp32cam.live;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.net.Network;
import android.net.ConnectivityManager;
import android.os.Build;
import android.os.Bundle;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import com.esp32cam.live.network.CameraApi;
import com.esp32cam.live.network.Esp32NetworkManager;
import com.esp32cam.live.storage.FrameSaver;
import com.esp32cam.live.stream.MjpegStreamClient;

import java.util.Locale;
import java.util.concurrent.atomic.AtomicReference;

public final class MainActivity extends Activity {
    private static final int PERMISSION_REQUEST = 100;
    private static final int[] EXPOSURE_VALUES = {100, 300, 600};
    private static final String[] RESOLUTION_VALUES = {"qvga", "cif", "vga"};
    private static final int[] QUALITY_VALUES = {30, 20, 12};

    private final AtomicReference<Bitmap> latestFrame = new AtomicReference<>();
    private Esp32NetworkManager networkManager;
    private CameraApi cameraApi;
    private MjpegStreamClient streamClient;
    private FrameSaver frameSaver;
    private Network cameraNetwork;

    private Button connectButton;
    private Button captureButton;
    private TextView statusText;
    private TextView fpsText;
    private ImageView videoView;
    private Switch autoExposureSwitch;
    private Spinner exposureSpinner;
    private Spinner resolutionSpinner;
    private Spinner qualitySpinner;
    private boolean controlsReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        networkManager = new Esp32NetworkManager(this);
        frameSaver = new FrameSaver(this);
        bindViews();
        configureControls();
    }

    private void bindViews() {
        connectButton = findViewById(R.id.connectButton);
        captureButton = findViewById(R.id.captureButton);
        statusText = findViewById(R.id.statusText);
        fpsText = findViewById(R.id.fpsText);
        videoView = findViewById(R.id.videoView);
        autoExposureSwitch = findViewById(R.id.autoExposureSwitch);
        exposureSpinner = findViewById(R.id.exposureSpinner);
        resolutionSpinner = findViewById(R.id.resolutionSpinner);
        qualitySpinner = findViewById(R.id.qualitySpinner);

        connectButton.setOnClickListener(view -> {
            if (cameraNetwork == null) {
                requestPermissionAndConnect();
            } else {
                disconnectCamera();
            }
        });
        captureButton.setOnClickListener(view -> captureCurrentFrame());
    }

    private void configureControls() {
        setSpinner(exposureSpinner, new String[]{"短 · 100", "中 · 300", "长 · 600"}, 0);
        setSpinner(resolutionSpinner,
                new String[]{"320×240", "400×296", "640×480"}, 0);
        setSpinner(qualitySpinner, new String[]{"流畅", "均衡", "清晰"}, 1);

        autoExposureSwitch.setChecked(false);
        autoExposureSwitch.setOnCheckedChangeListener((button, checked) -> {
            exposureSpinner.setEnabled(!checked);
            if (controlsReady && cameraApi != null) {
                cameraApi.setAutoExposure(checked, this::onControlResult);
            }
        });
        exposureSpinner.setOnItemSelectedListener(listener(position ->
                cameraApi.setExposure(EXPOSURE_VALUES[position], this::onControlResult)));
        resolutionSpinner.setOnItemSelectedListener(listener(position ->
                cameraApi.setResolution(RESOLUTION_VALUES[position], this::onControlResult)));
        qualitySpinner.setOnItemSelectedListener(listener(position ->
                cameraApi.setQuality(QUALITY_VALUES[position], this::onControlResult)));
        controlsReady = true;
    }

    private AdapterView.OnItemSelectedListener listener(PositionAction action) {
        return new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, android.view.View view,
                                       int position, long id) {
                if (controlsReady && cameraApi != null) {
                    action.run(position);
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        };
    }

    private void setSpinner(Spinner spinner, String[] entries, int selection) {
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item, entries);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        spinner.setSelection(selection);
    }

    private void requestPermissionAndConnect() {
        if (!BuildConfig.USE_WIFI_SPECIFIER) {
            ConnectivityManager manager = getSystemService(ConnectivityManager.class);
            Network activeNetwork = manager.getActiveNetwork();
            if (activeNetwork == null) {
                showToast("模拟器网络不可用");
                return;
            }
            startCamera(activeNetwork);
            return;
        }
        String permission = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                ? Manifest.permission.NEARBY_WIFI_DEVICES
                : Manifest.permission.ACCESS_FINE_LOCATION;
        if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{permission}, PERMISSION_REQUEST);
            return;
        }
        connectCamera();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQUEST && grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            connectCamera();
        } else if (requestCode == PERMISSION_REQUEST) {
            showToast("需要附近设备权限才能连接 ESP32-CAM");
        }
    }

    private void connectCamera() {
        statusText.setText(R.string.status_requesting);
        connectButton.setEnabled(false);
        networkManager.connect(new Esp32NetworkManager.Listener() {
            @Override
            public void onConnected(Network network) {
                runOnUiThread(() -> startCamera(network));
            }

            @Override
            public void onDisconnected() {
                runOnUiThread(() -> setDisconnected(R.string.status_disconnected));
            }

            @Override
            public void onUnavailable() {
                runOnUiThread(() -> setDisconnected(R.string.status_disconnected));
            }

            @Override
            public void onError(String message) {
                runOnUiThread(() -> {
                    setDisconnected(R.string.status_connection_failed);
                    showToast("连接失败：" + message);
                });
            }
        });
    }

    private void startCamera(Network network) {
        disconnectClients();
        cameraNetwork = network;
        cameraApi = new CameraApi(network);
        streamClient = new MjpegStreamClient(network, new MjpegStreamClient.Listener() {
            @Override
            public void onFrame(Bitmap bitmap, double fps, long kbps) {
                latestFrame.set(bitmap);
                runOnUiThread(() -> {
                    videoView.setImageBitmap(bitmap);
                    statusText.setText(R.string.status_streaming);
                    fpsText.setText(String.format(Locale.US,
                            "FPS %.1f · %d kb/s", fps, kbps));
                    captureButton.setEnabled(true);
                });
            }

            @Override
            public void onError(String message) {
                runOnUiThread(() -> statusText.setText("视频重连中：" + message));
            }
        });
        streamClient.start();
        statusText.setText(R.string.status_connected);
        connectButton.setText(R.string.disconnect);
        connectButton.setEnabled(true);
    }

    private void onControlResult(boolean success, String message) {
        if (!success) {
            runOnUiThread(() -> showToast("设置失败：" + message));
        }
    }

    private void captureCurrentFrame() {
        Bitmap frame = latestFrame.get();
        if (frame == null) {
            showToast("还没有可保存的视频帧");
            return;
        }
        captureButton.setEnabled(false);
        frameSaver.save(frame, (uri, error) -> runOnUiThread(() -> {
            captureButton.setEnabled(true);
            showToast(error == null ? "照片已保存到 Pictures/ESP32-CAM" :
                    "保存失败：" + error);
        }));
    }

    private void disconnectCamera() {
        networkManager.disconnect();
        setDisconnected(R.string.status_disconnected);
    }

    private void setDisconnected(int message) {
        disconnectClients();
        cameraNetwork = null;
        statusText.setText(message);
        fpsText.setText(R.string.fps_placeholder);
        connectButton.setText(R.string.connect);
        connectButton.setEnabled(true);
        captureButton.setEnabled(false);
    }

    private void disconnectClients() {
        if (streamClient != null) {
            streamClient.close();
            streamClient = null;
        }
        if (cameraApi != null) {
            cameraApi.close();
            cameraApi = null;
        }
    }

    private void showToast(String message) {
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show();
    }

    @Override
    protected void onDestroy() {
        disconnectClients();
        networkManager.disconnect();
        frameSaver.close();
        super.onDestroy();
    }

    private interface PositionAction {
        void run(int position);
    }
}
