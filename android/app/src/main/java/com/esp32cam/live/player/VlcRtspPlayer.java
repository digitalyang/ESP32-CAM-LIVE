package com.esp32cam.live.player;

import android.content.Context;
import android.graphics.Bitmap;
import android.net.TrafficStats;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.view.PixelCopy;
import android.view.SurfaceView;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;

import org.videolan.libvlc.LibVLC;
import org.videolan.libvlc.Media;
import org.videolan.libvlc.MediaPlayer;
import org.videolan.libvlc.interfaces.IMedia;
import org.videolan.libvlc.util.VLCVideoLayout;

import java.util.ArrayList;
import java.util.Arrays;

/** Owns the native VLC RTSP/RTP pipeline and exposes only app-level events. */
public final class VlcRtspPlayer implements AutoCloseable {
    public interface Listener {
        void onPlaying();
        void onMetrics(double fps, long kbps);
        void onError(String message);
    }

    public interface CaptureCallback {
        void onComplete(Bitmap bitmap, String error);
    }

    private final VLCVideoLayout videoLayout;
    private final Listener listener;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final LibVLC libVlc;
    private final MediaPlayer mediaPlayer;
    private boolean attached;
    private boolean closed;
    private boolean retryScheduled;
    private String streamUrl;
    private int previousFrames;
    private long previousRxBytes;
    private long previousMetricsAt;

    private final Runnable metricsTask = new Runnable() {
        @Override
        public void run() {
            if (closed) {
                return;
            }
            updateMetrics();
            mainHandler.postDelayed(this, 1000);
        }
    };

    private final Runnable retryTask = () -> {
        retryScheduled = false;
        if (!closed && streamUrl != null) {
            play(streamUrl);
        }
    };

    public VlcRtspPlayer(Context context, VLCVideoLayout videoLayout, Listener listener) {
        this.videoLayout = videoLayout;
        this.listener = listener;
        libVlc = new LibVLC(context, new ArrayList<>(Arrays.asList(
                "--no-audio",
                "--stats",
                "--network-caching=180",
                "--live-caching=100",
                "--clock-jitter=0",
                "--clock-synchro=0"
        )));
        mediaPlayer = new MediaPlayer(libVlc);
        mediaPlayer.setEventListener(this::onPlayerEvent);
    }

    public void play(String url) {
        if (closed) {
            return;
        }
        streamUrl = url;
        if (!attached) {
            // Prefer a TextureView when supported; PixelCopy covers VLC's SurfaceView fallback.
            mediaPlayer.attachViews(videoLayout, null, true, false);
            attached = true;
        }
        Media media = new Media(libVlc, Uri.parse(url));
        media.setHWDecoderEnabled(true, false);
        media.addOption(":no-audio");
        media.addOption(":network-caching=180");
        media.addOption(":rtsp-caching=180");
        media.addOption(":live-caching=100");
        mediaPlayer.setMedia(media);
        media.release();
        mediaPlayer.play();
        resetMetrics();
        mainHandler.removeCallbacks(metricsTask);
        mainHandler.postDelayed(metricsTask, 1000);
    }

    public void captureFrame(CaptureCallback callback) {
        TextureView textureView = findTextureView(videoLayout);
        if (textureView != null && textureView.isAvailable()) {
            Bitmap bitmap = textureView.getBitmap();
            callback.onComplete(bitmap, bitmap == null ? "无法读取播放器画面" : null);
            return;
        }
        SurfaceView surfaceView = findSurfaceView(videoLayout);
        if (surfaceView == null || surfaceView.getWidth() == 0 || surfaceView.getHeight() == 0) {
            callback.onComplete(null, "播放器画面尚未准备好");
            return;
        }
        Bitmap bitmap = Bitmap.createBitmap(surfaceView.getWidth(), surfaceView.getHeight(),
                Bitmap.Config.ARGB_8888);
        PixelCopy.request(surfaceView, bitmap, result -> {
            if (result == PixelCopy.SUCCESS) {
                callback.onComplete(bitmap, null);
            } else {
                bitmap.recycle();
                callback.onComplete(null, "视频截图失败（" + result + "）");
            }
        }, mainHandler);
    }

    private void onPlayerEvent(MediaPlayer.Event event) {
        if (closed) {
            return;
        }
        if (event.type == MediaPlayer.Event.Playing) {
            retryScheduled = false;
            mainHandler.removeCallbacks(retryTask);
            mainHandler.post(listener::onPlaying);
        } else if (event.type == MediaPlayer.Event.EncounteredError) {
            mainHandler.post(() -> listener.onError("RTSP 播放失败"));
            scheduleRetry();
        } else if (event.type == MediaPlayer.Event.EndReached) {
            mainHandler.post(() -> listener.onError("RTSP 视频流已结束"));
            scheduleRetry();
        }
    }

    private void scheduleRetry() {
        if (!closed && !retryScheduled) {
            retryScheduled = true;
            mainHandler.postDelayed(retryTask, 1500);
        }
    }

    private void updateMetrics() {
        long now = android.os.SystemClock.elapsedRealtime();
        long rxBytes = TrafficStats.getUidRxBytes(android.os.Process.myUid());
        IMedia currentMedia = mediaPlayer.getMedia();
        IMedia.Stats stats = currentMedia == null ? null : currentMedia.getStats();
        int frames = stats == null ? previousFrames
                : Math.max(stats.displayedPictures, stats.decodedVideo);
        if (previousMetricsAt != 0 && now > previousMetricsAt) {
            long elapsed = now - previousMetricsAt;
            int frameDelta = frames >= previousFrames ? frames - previousFrames : 0;
            long byteDelta = rxBytes >= previousRxBytes ? rxBytes - previousRxBytes : 0;
            listener.onMetrics(frameDelta * 1000.0 / elapsed,
                    Math.round(byteDelta * 8.0 / elapsed));
        }
        previousFrames = frames;
        previousRxBytes = rxBytes;
        previousMetricsAt = now;
    }

    private void resetMetrics() {
        previousFrames = 0;
        previousRxBytes = TrafficStats.getUidRxBytes(android.os.Process.myUid());
        previousMetricsAt = 0;
    }

    private static TextureView findTextureView(View view) {
        if (view instanceof TextureView) {
            return (TextureView) view;
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                TextureView result = findTextureView(group.getChildAt(i));
                if (result != null) {
                    return result;
                }
            }
        }
        return null;
    }

    private static SurfaceView findSurfaceView(View view) {
        if (view instanceof SurfaceView) {
            return (SurfaceView) view;
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                SurfaceView result = findSurfaceView(group.getChildAt(i));
                if (result != null) {
                    return result;
                }
            }
        }
        return null;
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;
        mainHandler.removeCallbacksAndMessages(null);
        mediaPlayer.setEventListener(null);
        mediaPlayer.stop();
        if (attached) {
            mediaPlayer.detachViews();
            attached = false;
        }
        mediaPlayer.release();
        libVlc.release();
    }
}
