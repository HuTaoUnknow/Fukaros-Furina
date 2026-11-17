package com.furina.black;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.hardware.display.DisplayManager;
import android.media.Image;
import android.media.ImageReader;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.util.Size;
import android.view.Display;
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
    private static final String TAG = "Furina";
    private static CameraDevice camera;
    private static CameraCaptureSession session;
    private static ImageReader reader;
    private static HandlerThread thread;
    private static Handler handler;
    private static FrameLayout overlay;
    private static TextureView preview;
    private static boolean delivered;
    private static String cameraId = "";
    private static int sensorOrientation;
    private static boolean frontFacing;
    private static boolean continuousFocus;
    private static int bufferWidth = 1280;
    private static int bufferHeight = 720;
    private static int loggedDisplay = Integer.MIN_VALUE;
    private static int loggedUpright = Integer.MIN_VALUE;
    private static int loggedViewW = Integer.MIN_VALUE;
    private static int loggedViewH = Integer.MIN_VALUE;
    private static int loggedHal = Integer.MIN_VALUE;
    private static boolean loggedFrame;
    private static long lastDecodeMs;
    private static final long DECODE_INTERVAL_MS = 120;
    private static DisplayManager displayManager;
    private static DisplayManager.DisplayListener displayListener;

    private PairingCamera() {}

    public static native int nativeUprightDegrees(int sensorOrientation, int displayDegrees, boolean frontFacing);

    public static native String nativeDecode(byte[] luminance, int width, int height, int rowStride, int pixelStride,
                                              int uprightDegrees, int displayDegrees);

    public static native void nativeFinished(String text);

    /** Asks the activity for CAMERA, then opens the scanner once it is granted. */
    public static void requestStart() {
        final FurinaActivity activity = FurinaActivity.currentActivity();
        if (activity == null) {
            Log.i(TAG, "camera start without activity");
            nativeFinished("");
            return;
        }
        activity.runOnUiThread(() -> {
            if (activity.checkSelfPermission(Manifest.permission.CAMERA)
                    == PackageManager.PERMISSION_GRANTED) {
                Log.i(TAG, "camera permission granted");
                open(activity);
                return;
            }
            Log.i(TAG, "camera permission requesting");
            activity.requestCameraPermission();
        });
    }

    /** Called by the activity after the system permission dialog is answered. */
    public static void onPermissionResult(boolean granted) {
        if (!granted) {
            Log.i(TAG, "camera permission denied");
            nativeFinished("");
            return;
        }
        Log.i(TAG, "camera permission granted");
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
        loggedFrame = false;
        lastDecodeMs = 0;
        loggedDisplay = Integer.MIN_VALUE;
        loggedUpright = Integer.MIN_VALUE;
        loggedViewW = Integer.MIN_VALUE;
        loggedViewH = Integer.MIN_VALUE;
        loggedHal = Integer.MIN_VALUE;
        if (activity.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "camera permission missing");
            nativeFinished("");
            return;
        }
        try {
            chooseCamera(activity);
        } catch (CameraAccessException e) {
            Log.i(TAG, "camera choose failed " + e);
            nativeFinished("");
            return;
        }
        if (cameraId.isEmpty()) {
            Log.i(TAG, "camera none");
            nativeFinished("");
            return;
        }
        Log.i(TAG, "camera id=" + cameraId + " facing=" + (frontFacing ? "front" : "back") + " sensorOrientation="
                           + sensorOrientation + " buffer=" + bufferWidth + "x" + bufferHeight + " focus="
                           + (continuousFocus ? "continuous" : "default"));
        preview = new TextureView(activity);
        // A rotation re-lays out the overlay without changing the display rotation we
        // last saw, so the transform has to follow the view size as well.
        preview.addOnLayoutChangeListener(
                (view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
                    if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop)
                        applyPreviewTransform();
                });
        Button cancel = new Button(activity);
        cancel.setText("取消");
        cancel.setOnClickListener(view -> finish(""));
        overlay = new FrameLayout(activity);
        overlay.setBackgroundColor(Color.BLACK);
        overlay.addView(preview, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        FrameLayout.LayoutParams cancelParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        cancelParams.bottomMargin = 48;
        overlay.addView(cancel, cancelParams);
        ViewGroup root = (ViewGroup) activity.getWindow().getDecorView();
        root.addView(overlay, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        displayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        displayListener = new DisplayManager.DisplayListener() {
            @Override
            public void onDisplayAdded(int displayId) {}

            @Override
            public void onDisplayRemoved(int displayId) {}

            @Override
            public void onDisplayChanged(int displayId) {
                FurinaActivity current = FurinaActivity.currentActivity();
                if (current != null)
                    current.runOnUiThread(PairingCamera::applyPreviewTransform);
            }
        };
        if (displayManager != null)
            displayManager.registerDisplayListener(displayListener, new Handler(activity.getMainLooper()));
        thread = new HandlerThread("furina-pairing-camera");
        thread.start();
        handler = new Handler(thread.getLooper());
        preview.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
            @Override
            public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
                surface.setDefaultBufferSize(bufferWidth, bufferHeight);
                applyPreviewTransform();
                openCamera(activity, preview);
                // halQuarterTurns() reads the SurfaceTexture transform, which only carries
                // the HAL's mapping once frames flow. Re-apply it after the first frames so
                // the preview is corrected even though the initial pass saw a fresh surface.
                final Handler main = new Handler(activity.getMainLooper());
                main.postDelayed(PairingCamera::applyPreviewTransform, 1500);
            }

            @Override
            public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
                applyPreviewTransform();
            }

            @Override
            public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
                return true;
            }

            @Override
            public void onSurfaceTextureUpdated(SurfaceTexture surface) {}
        });
    }

    private static void chooseCamera(FurinaActivity activity) throws CameraAccessException {
        CameraManager manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
        String fallbackId = null;
        CameraCharacteristics fallback = null;
        for (String id : manager.getCameraIdList()) {
            CameraCharacteristics characteristics = manager.getCameraCharacteristics(id);
            Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
            if (fallbackId == null) {
                fallbackId = id;
                fallback = characteristics;
            }
            if (facing != null && facing == CameraCharacteristics.LENS_FACING_BACK) {
                rememberCamera(id, characteristics, false);
                return;
            }
        }
        if (fallbackId == null || fallback == null) {
            cameraId = "";
            return;
        }
        Integer facing = fallback.get(CameraCharacteristics.LENS_FACING);
        rememberCamera(fallbackId, fallback,
                facing != null && facing == CameraCharacteristics.LENS_FACING_FRONT);
    }

    private static void rememberCamera(String id, CameraCharacteristics characteristics, boolean front) {
        cameraId = id;
        frontFacing = front;
        Integer sensor = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);
        sensorOrientation = sensor == null ? 0 : sensor;
        int[] modes = characteristics.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES);
        continuousFocus = false;
        if (modes != null) {
            for (int mode : modes) {
                if (mode == CameraCharacteristics.CONTROL_AF_MODE_CONTINUOUS_PICTURE) {
                    continuousFocus = true;
                    break;
                }
            }
        }
        bufferWidth = 1280;
        bufferHeight = 720;
        StreamConfigurationMap map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        Size[] sizes = map == null ? null : map.getOutputSizes(ImageFormat.YUV_420_888);
        Size best = null;
        int bestEdge = -1;
        if (sizes != null) {
            for (Size size : sizes) {
                int edge = Math.max(size.getWidth(), size.getHeight());
                int minor = Math.min(size.getWidth(), size.getHeight());
                if (edge < 960 || edge > 1600 || minor < 480)
                    continue;
                if (edge > bestEdge) {
                    best = size;
                    bestEdge = edge;
                }
            }
            if (best == null) {
                for (Size size : sizes) {
                    int edge = Math.max(size.getWidth(), size.getHeight());
                    if (edge > bestEdge) {
                        best = size;
                        bestEdge = edge;
                    }
                }
            }
        }
        if (best != null) {
            bufferWidth = best.getWidth();
            bufferHeight = best.getHeight();
        }
    }

    private static int displayDegrees(FurinaActivity activity) {
        if (activity == null)
            return 0;
        Display display = null;
        if (Build.VERSION.SDK_INT >= 30)
            display = activity.getDisplay();
        if (display == null) {
            DisplayManager manager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
            display = manager == null ? null : manager.getDisplay(Display.DEFAULT_DISPLAY);
        }
        if (display == null)
            return 0;
        switch (display.getRotation()) {
            case Surface.ROTATION_90:
                return 90;
            case Surface.ROTATION_180:
                return 180;
            case Surface.ROTATION_270:
                return 270;
            default:
                return 0;
        }
    }

    /**
     * Quarter turns (clockwise) that the camera HAL's SurfaceTexture transform already applies.
     *
     * TextureView multiplies its own matrix by the SurfaceTexture transform, so a HAL matrix
     * that swaps the axes turns the preview on its own before our matrix runs. Measured on the
     * Xiaomi tablet: the HAL matrix is a fixed transpose regardless of display rotation, which
     * added exactly 90 degrees to whatever we asked for.
     *
     * The HAL matrix is expressed in texture (u,v) space, where v grows upward, so its 2x2
     * part has to be read with that sign flip in mind.
     */
    private static int halQuarterTurns() {
        TextureView view = preview;
        if (view == null)
            return 0;
        SurfaceTexture surface = view.getSurfaceTexture();
        if (surface == null)
            return 0;
        float[] st = new float[16];
        surface.getTransformMatrix(st);
        // st is column-major: u' = st[0]*u + st[4]*v + st[12], v' = st[1]*u + st[5]*v + st[13].
        // Convert to screen axes by negating the v row (flip y), then read off the rotation.
        float m00 = st[0], m01 = st[4], m10 = -st[1], m11 = -st[5];
        // A pure quarter turn has one of four shapes; classify by the dominant axis mapping.
        if (Math.abs(m00) < 0.5f && Math.abs(m01) > 0.5f) {
            // x comes from v: a quarter turn. m01 is -sin(theta), so a negative m01
            // means +90 and a positive one means 270.
            return m01 < 0 ? 90 : 270;
        }
        if (Math.abs(m00) > 0.5f && Math.abs(m01) < 0.5f) {
            // No axis swap; 0 or 180 depending on the sign of the x mapping.
            return m00 > 0 ? 0 : 180;
        }
        return 0;
    }

    private static void applyPreviewTransform() {
        TextureView view = preview;
        FurinaActivity activity = FurinaActivity.currentActivity();
        if (view == null || activity == null || bufferWidth < 1 || bufferHeight < 1)
            return;
        int viewW = view.getWidth();
        int viewH = view.getHeight();
        if (viewW < 1 || viewH < 1)
            return;
        int display = displayDegrees(activity);
        int upright = ((nativeUprightDegrees(sensorOrientation, display, frontFacing) % 360) + 360) % 360;
        // TextureView composes its matrix with the SurfaceTexture's transform, and this
        // camera HAL ships a non-identity one. Apply only the remainder, or the preview
        // ends up a quarter turn off (measured: 180 deg on screen when 90 was asked for).
        final int halTurns = halQuarterTurns();
        final int applied = ((upright - halTurns) % 360 + 360) % 360;
        // setTransform works in view coordinates, not buffer coordinates: TextureView
        // already stretches the buffer to fill the view. So the first thing this matrix
        // has to undo is that implicit non-uniform stretch, otherwise the rotation below
        // is applied to a skewed image and the result lands outside the view.
        // The extent that has to cover the view is the buffer rotated by what we actually apply,
        // not by upright: the HAL has already turned the image, so the two differ by a quarter
        // turn and scaling for the wrong one leaves bars along one axis in landscape.
        final int appliedMod = ((applied % 360) + 360) % 360;
        boolean swap = appliedMod == 90 || appliedMod == 270;
        float rotatedW = swap ? bufferHeight : bufferWidth;
        float rotatedH = swap ? bufferWidth : bufferHeight;
        float cover = Math.max(viewW / rotatedW, viewH / rotatedH);
        Matrix matrix = new Matrix();
        matrix.postScale(bufferWidth / (float) viewW, bufferHeight / (float) viewH);
        matrix.postTranslate(-bufferWidth / 2f, -bufferHeight / 2f);
        matrix.postRotate(applied);
        // Undo the mirroring when the front camera is used, before scaling up.
        matrix.postScale(frontFacing ? -cover : cover, cover);
        matrix.postTranslate(viewW / 2f, viewH / 2f);
        view.setTransform(matrix);
        if (display != loggedDisplay || upright != loggedUpright || viewW != loggedViewW || viewH != loggedViewH
                || halTurns != loggedHal) {
            loggedDisplay = display;
            loggedUpright = upright;
            loggedViewW = viewW;
            loggedViewH = viewH;
            loggedHal = halTurns;
            Log.i(TAG, "preview display=ROTATION_" + display + " sensor=" + sensorOrientation + " upright=" + upright
                               + " hal=" + halTurns + " applied=" + applied + " buffer=" + bufferWidth + "x"
                               + bufferHeight + " view=" + viewW + "x" + viewH + " cover=" + cover + " facing="
                               + (frontFacing ? "front" : "back"));
        }
    }

    private static void openCamera(FurinaActivity activity, TextureView view) {
        CameraManager manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
        try {
            if (cameraId.isEmpty() || view.getSurfaceTexture() == null) {
                finish("");
                return;
            }
            view.getSurfaceTexture().setDefaultBufferSize(bufferWidth, bufferHeight);
            reader = ImageReader.newInstance(bufferWidth, bufferHeight, ImageFormat.YUV_420_888, 2);
            reader.setOnImageAvailableListener(PairingCamera::onImage, handler);
            manager.openCamera(cameraId, new CameraDevice.StateCallback() {
                @Override
                public void onOpened(CameraDevice opened) {
                    camera = opened;
                    try {
                        Surface texture = new Surface(view.getSurfaceTexture());
                        camera.createCaptureSession(Arrays.asList(texture, reader.getSurface()),
                                new CameraCaptureSession.StateCallback() {
                                    @Override
                                    public void onConfigured(CameraCaptureSession configured) {
                                        session = configured;
                                        try {
                                            CaptureRequest.Builder request =
                                                    camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                                            request.addTarget(texture);
                                            request.addTarget(reader.getSurface());
                                            if (continuousFocus)
                                                request.set(CaptureRequest.CONTROL_AF_MODE,
                                                        CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);
                                            session.setRepeatingRequest(request.build(), null, handler);
                                        } catch (CameraAccessException ignored) {
                                            Log.i(TAG, "camera request failed");
                                            finish("");
                                        }
                                    }

                                    @Override
                                    public void onConfigureFailed(CameraCaptureSession configured) {
                                        Log.i(TAG, "camera session failed");
                                        finish("");
                                    }
                                }, handler);
                    } catch (CameraAccessException ignored) {
                        Log.i(TAG, "camera session failed");
                        finish("");
                    }
                }

                @Override
                public void onDisconnected(CameraDevice opened) {
                    Log.i(TAG, "camera disconnected");
                    finish("");
                }

                @Override
                public void onError(CameraDevice opened, int error) {
                    Log.i(TAG, "camera error " + error);
                    finish("");
                }
            }, handler);
        } catch (CameraAccessException | SecurityException e) {
            Log.i(TAG, "camera open failed " + e);
            finish("");
        }
    }

    private static void onImage(ImageReader imageReader) {
        if (delivered)
            return;
        try (Image image = imageReader.acquireLatestImage()) {
            if (image == null)
                return;
            // Decoding every frame on the camera thread starves the preview and the
            // decoder alike. A few frames per second is plenty for a static code.
            long now = System.currentTimeMillis();
            if (now - lastDecodeMs < DECODE_INTERVAL_MS)
                return;
            lastDecodeMs = now;
            Image.Plane plane = image.getPlanes()[0];
            ByteBuffer buffer = plane.getBuffer().duplicate();
            buffer.rewind();
            byte[] data = new byte[buffer.remaining()];
            buffer.get(data);
            FurinaActivity activity = FurinaActivity.currentActivity();
            int display = displayDegrees(activity);
            int upright = nativeUprightDegrees(sensorOrientation, display, frontFacing);
            if (!loggedFrame) {
                loggedFrame = true;
                Log.i(TAG, "frame " + image.getWidth() + "x" + image.getHeight() + " format=" + image.getFormat()
                                   + " rowStride=" + plane.getRowStride() + " pixelStride=" + plane.getPixelStride()
                                   + " bytes=" + data.length + " upright=" + upright + " display=ROTATION_" + display);
            }
            String text = nativeDecode(data, image.getWidth(), image.getHeight(), plane.getRowStride(),
                    plane.getPixelStride(), upright, display);
            if (text != null && !text.isEmpty())
                finish(text);
        } catch (RuntimeException e) {
            Log.i(TAG, "frame failed " + e);
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
        if (displayManager != null && displayListener != null)
            displayManager.unregisterDisplayListener(displayListener);
        displayManager = null;
        displayListener = null;
        if (overlay != null) {
            ViewGroup parent = (ViewGroup) overlay.getParent();
            if (parent != null)
                parent.removeView(overlay);
            overlay = null;
        }
        preview = null;
    }
}
