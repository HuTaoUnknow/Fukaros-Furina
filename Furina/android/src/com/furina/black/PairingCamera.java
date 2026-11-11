package com.furina.black;

import android.Manifest;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Gravity;
import android.view.Surface;
import android.view.TextureView;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import java.nio.ByteBuffer;
import java.util.Arrays;

/** Opens the back camera and returns the first QR payload. The payload is not logged. */
public final class PairingCamera {
    private static CameraDevice camera;
    private static CameraCaptureSession session;
    private static ImageReader reader;
    private static HandlerThread thread;
    private static Handler handler;
    private static FrameLayout overlay;
    private static boolean delivered;

    private PairingCamera() {}

    public static native String nativeDecode(byte[] luminance, int width, int height, int rowStride);

    public static native void nativeFinished(String text);

    public static void start() {
        final FurinaActivity activity = FurinaActivity.currentActivity();
        if (activity == null) {
            nativeFinished("");
            return;
        }
        activity.runOnUiThread(() -> open(activity));
    }

    private static void open(FurinaActivity activity) {
        stop();
        delivered = false;
        if (activity.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            nativeFinished("");
            return;
        }
        TextureView preview = new TextureView(activity);
        Button cancel = new Button(activity);
        cancel.setText("取消");
        cancel.setOnClickListener(view -> finish(""));
        overlay = new FrameLayout(activity);
        overlay.setBackgroundColor(Color.BLACK);
        overlay.addView(preview, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        FrameLayout.LayoutParams cancelParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        cancelParams.bottomMargin = 48;
        overlay.addView(cancel, cancelParams);
        ViewGroup root = (ViewGroup) activity.getWindow().getDecorView();
        root.addView(overlay, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        thread = new HandlerThread("furina-pairing-camera");
        thread.start();
        handler = new Handler(thread.getLooper());
        preview.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
            @Override
            public void onSurfaceTextureAvailable(android.graphics.SurfaceTexture surface, int width, int height) {
                openCamera(activity, preview);
            }

            @Override
            public void onSurfaceTextureSizeChanged(android.graphics.SurfaceTexture surface, int width, int height) {}

            @Override
            public boolean onSurfaceTextureDestroyed(android.graphics.SurfaceTexture surface) {
                return true;
            }

            @Override
            public void onSurfaceTextureUpdated(android.graphics.SurfaceTexture surface) {}
        });
    }

    private static void openCamera(FurinaActivity activity, TextureView preview) {
        CameraManager manager = (CameraManager) activity.getSystemService(FurinaActivity.CAMERA_SERVICE);
        try {
            String chosen = null;
            for (String id : manager.getCameraIdList()) {
                chosen = id;
                break;
            }
            if (chosen == null) {
                finish("");
                return;
            }
            reader = ImageReader.newInstance(640, 480, ImageFormat.YUV_420_888, 2);
            reader.setOnImageAvailableListener(PairingCamera::onImage, handler);
            manager.openCamera(chosen, new CameraDevice.StateCallback() {
                @Override
                public void onOpened(CameraDevice opened) {
                    camera = opened;
                    try {
                        preview.getSurfaceTexture().setDefaultBufferSize(640, 480);
                        Surface view = new Surface(preview.getSurfaceTexture());
                        camera.createCaptureSession(Arrays.asList(view, reader.getSurface()),
                                new CameraCaptureSession.StateCallback() {
                                    @Override
                                    public void onConfigured(CameraCaptureSession configured) {
                                        session = configured;
                                        try {
                                            CaptureRequest.Builder request =
                                                    camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                                            request.addTarget(view);
                                            request.addTarget(reader.getSurface());
                                            session.setRepeatingRequest(request.build(), null, handler);
                                        } catch (CameraAccessException ignored) {
                                            finish("");
                                        }
                                    }

                                    @Override
                                    public void onConfigureFailed(CameraCaptureSession configured) {
                                        finish("");
                                    }
                                }, handler);
                    } catch (CameraAccessException ignored) {
                        finish("");
                    }
                }

                @Override
                public void onDisconnected(CameraDevice opened) {
                    finish("");
                }

                @Override
                public void onError(CameraDevice opened, int error) {
                    finish("");
                }
            }, handler);
        } catch (CameraAccessException | SecurityException ignored) {
            finish("");
        }
    }

    private static void onImage(ImageReader imageReader) {
        if (delivered)
            return;
        try (Image image = imageReader.acquireLatestImage()) {
            if (image == null)
                return;
            Image.Plane plane = image.getPlanes()[0];
            ByteBuffer buffer = plane.getBuffer();
            byte[] data = new byte[buffer.remaining()];
            buffer.get(data);
            String text = nativeDecode(data, image.getWidth(), image.getHeight(), plane.getRowStride());
            if (text != null && !text.isEmpty())
                finish(text);
        } catch (RuntimeException ignored) {
            finish("");
        }
    }

    private static synchronized boolean claim() {
        if (delivered)
            return false;
        delivered = true;
        return true;
    }

    private static void finish(String text) {
        if (!claim())
            return;
        final String payload = text == null ? "" : text;
        FurinaActivity activity = FurinaActivity.currentActivity();
        if (activity != null)
            activity.runOnUiThread(() -> {
                stop();
                nativeFinished(payload);
            });
        else
            nativeFinished(payload);
    }

    public static void stop() {
        if (session != null) {
            session.close();
            session = null;
        }
        if (camera != null) {
            camera.close();
            camera = null;
        }
        if (reader != null) {
            reader.close();
            reader = null;
        }
        if (thread != null) {
            thread.quitSafely();
            thread = null;
            handler = null;
        }
        if (overlay != null) {
            ViewGroup parent = (ViewGroup) overlay.getParent();
            if (parent != null)
                parent.removeView(overlay);
            overlay = null;
        }
    }
}
