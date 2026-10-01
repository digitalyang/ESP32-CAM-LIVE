package com.esp32cam.live.network;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiNetworkSpecifier;

public final class Esp32NetworkManager {
    public interface Listener {
        void onConnected(Network network);
        void onDisconnected();
        void onUnavailable();
    }

    private static final String SSID = "ESP32-CAM";
    private static final String PASSWORD = "12345678";

    private final ConnectivityManager connectivityManager;
    private ConnectivityManager.NetworkCallback callback;

    public Esp32NetworkManager(Context context) {
        connectivityManager = context.getSystemService(ConnectivityManager.class);
    }

    public void connect(Listener listener) {
        disconnect();
        WifiNetworkSpecifier specifier = new WifiNetworkSpecifier.Builder()
                .setSsid(SSID)
                .setWpa2Passphrase(PASSWORD)
                .build();
        NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .setNetworkSpecifier(specifier)
                .build();
        callback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                listener.onConnected(network);
            }

            @Override
            public void onLost(Network network) {
                listener.onDisconnected();
            }

            @Override
            public void onUnavailable() {
                listener.onUnavailable();
            }
        };
        connectivityManager.requestNetwork(request, callback);
    }

    public void disconnect() {
        if (callback == null) {
            return;
        }
        try {
            connectivityManager.unregisterNetworkCallback(callback);
        } catch (IllegalArgumentException ignored) {
            // The callback may already have been released by Android.
        }
        callback = null;
    }
}
