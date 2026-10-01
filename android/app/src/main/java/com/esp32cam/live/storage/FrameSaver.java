package com.esp32cam.live.storage;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;

import java.io.IOException;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class FrameSaver implements AutoCloseable {
    public interface Callback {
        void onComplete(Uri uri, String error);
    }

    private final Context context;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    public FrameSaver(Context context) {
        this.context = context.getApplicationContext();
    }

    public void save(Bitmap source, Callback callback) {
        Bitmap copy = source.copy(Bitmap.Config.ARGB_8888, false);
        executor.execute(() -> {
            Uri uri = null;
            try {
                String timestamp = new SimpleDateFormat(
                        "yyyyMMdd_HHmmss_SSS", Locale.US).format(new Date());
                ContentValues values = new ContentValues();
                values.put(MediaStore.Images.Media.DISPLAY_NAME,
                        "ESP32_CAM_" + timestamp + ".jpg");
                values.put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg");
                values.put(MediaStore.Images.Media.RELATIVE_PATH,
                        Environment.DIRECTORY_PICTURES + "/ESP32-CAM");
                values.put(MediaStore.Images.Media.IS_PENDING, 1);

                ContentResolver resolver = context.getContentResolver();
                uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri == null) {
                    throw new IOException("Unable to create photo");
                }
                try (OutputStream output = resolver.openOutputStream(uri)) {
                    if (output == null || !copy.compress(Bitmap.CompressFormat.JPEG, 95, output)) {
                        throw new IOException("Unable to encode photo");
                    }
                }
                values.clear();
                values.put(MediaStore.Images.Media.IS_PENDING, 0);
                resolver.update(uri, values, null, null);
                callback.onComplete(uri, null);
            } catch (Exception error) {
                if (uri != null) {
                    context.getContentResolver().delete(uri, null, null);
                }
                callback.onComplete(null, error.getMessage());
            } finally {
                copy.recycle();
            }
        });
    }

    @Override
    public void close() {
        executor.shutdownNow();
    }
}
