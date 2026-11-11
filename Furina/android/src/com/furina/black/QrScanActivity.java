package com.furina.black;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import java.nio.ByteBuffer;
import java.util.Collections;

public class QrScanActivity extends Activity {
    private CameraDevice camera;
    private CameraCaptureSession session;
    private ImageReader reader;
    private HandlerThread thread;
    private Handler handler;
    private boolean done;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.CAMERA}, 41);
            return;
        }
        openCamera();
    }

    @Override
    public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        if (request == 41 && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED)
            openCamera();
        else
            finish();
    }

    private void openCamera() {
        thread = new HandlerThread("furina-qr");
        thread.start();
        handler = new Handler(thread.getLooper());
        reader = ImageReader.newInstance(640, 480, ImageFormat.YUV_420_888, 2);
        reader.setOnImageAvailableListener(this::onFrame, handler);
        try {
            CameraManager manager = (CameraManager)getSystemService(CAMERA_SERVICE);
            String chosen = manager.getCameraIdList()[0];
            for (String id : manager.getCameraIdList()) {
                Integer facing = manager.getCameraCharacteristics(id).get(CameraCharacteristics.LENS_FACING);
                if (facing != null && facing == CameraCharacteristics.LENS_FACING_BACK) {
                    chosen = id;
                    break;
                }
            }
            manager.openCamera(chosen, new CameraDevice.StateCallback() {
                @Override
                public void onOpened(CameraDevice device) {
                    camera = device;
                    try {
                        device.createCaptureSession(Collections.singletonList(reader.getSurface()),
                                                    new CameraCaptureSession.StateCallback() {
                                                        @Override
                                                        public void onConfigured(CameraCaptureSession capture) {
                                                            session = capture;
                                                            try {
                                                                CaptureRequest.Builder request = device.createCaptureRequest(
                                                                    CameraDevice.TEMPLATE_PREVIEW);
                                                                request.addTarget(reader.getSurface());
                                                                capture.setRepeatingRequest(request.build(), null, handler);
                                                            } catch (Exception e) {
                                                                finish();
                                                            }
                                                        }

                                                        @Override
                                                        public void onConfigureFailed(CameraCaptureSession capture) {
                                                            finish();
                                                        }
                                                    }, handler);
                    } catch (Exception e) {
                        finish();
                    }
                }

                @Override
                public void onDisconnected(CameraDevice device) {
                    device.close();
                }

                @Override
                public void onError(CameraDevice device, int error) {
                    device.close();
                    finish();
                }
            }, handler);
        } catch (Exception e) {
            finish();
        }
    }

    private void onFrame(ImageReader images) {
        Image image = images.acquireLatestImage();
        if (image == null)
            return;
        try {
            if (done)
                return;
            Image.Plane plane = image.getPlanes()[0];
            ByteBuffer buffer = plane.getBuffer();
            int width = image.getWidth(), height = image.getHeight();
            int stride = plane.getRowStride(), pixel = plane.getPixelStride();
            byte[] luma = new byte[width * height];
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int index = y * stride + x * pixel;
                    if (index >= buffer.limit())
                        return;
                    luma[y * width + x] = buffer.get(index);
                }
            }
            String text = FurinaActivity.decodeQr(luma, width, height);
            if (text != null && text.startsWith("furina:")) {
                done = true;
                FurinaActivity.deliverQr(text);
                finish();
            }
        } finally {
            image.close();
        }
    }

    @Override
    protected void onDestroy() {
        if (session != null)
            session.close();
        if (camera != null)
            camera.close();
        if (reader != null)
            reader.close();
        if (thread != null)
            thread.quitSafely();
        super.onDestroy();
    }
}
