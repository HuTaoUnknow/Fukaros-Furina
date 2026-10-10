package com.furina.black;

import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.content.Intent;
import android.media.projection.MediaProjectionManager;
import android.media.projection.MediaProjectionConfig;
import android.provider.Settings;
import org.qtproject.qt.android.bindings.QtActivity;

public class FurinaActivity extends QtActivity {
    private static FurinaActivity current;
    private boolean awaitingConsent;
    private static final int CAPTURE_REQUEST = 8301;
    public static native boolean nativeWantFrame();
    public static native void nativeFrame(byte[] jpeg);
    public static native void nativeStatus(String text);
    public static native void nativeStopped();
    public static native boolean nativeVideo(byte[] packet);
    public static native void nativeVideoStats(boolean sender,double fps,double workMs,int queueMs,double displayedFps,int width,int height);
    public static native void nativeNeedKey();
    public static native void nativeExitFullscreen();
    public static FurinaActivity currentActivity(){return current;}

    @Override public void onCreate(Bundle state) { super.onCreate(state); current = this; }
    @Override public void onDestroy() { if (current == this) { VideoCore.releaseActivity(); current = null; } super.onDestroy(); }
    public static String deviceCode() {
        final FurinaActivity a=current;
        if(a==null)return "";
        String code=Settings.Secure.getString(a.getContentResolver(),Settings.Secure.ANDROID_ID);
        return code==null?"":code;
    }
    public static void requestCapture() {
        final FurinaActivity a = current;
        if (a == null) return;
        a.runOnUiThread(() -> {
            if (a.awaitingConsent) return;
            try {
                MediaProjectionManager manager = (MediaProjectionManager)a.getSystemService(MEDIA_PROJECTION_SERVICE);
                Intent request = Build.VERSION.SDK_INT >= 34
                    ? manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
                    : manager.createScreenCaptureIntent();
                a.awaitingConsent = true;
                a.startActivityForResult(request, CAPTURE_REQUEST);
            } catch (Exception e) { a.awaitingConsent=false; nativeStatus("录屏授权失败："+e); nativeStopped(); }
        });
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != CAPTURE_REQUEST) return;
        if (!awaitingConsent) return;
        awaitingConsent=false;
        if (result != Activity.RESULT_OK || data == null) { nativeStatus("录屏授权已取消"); nativeStopped(); return; }
        try {
            Intent service = new Intent(this, CaptureService.class);
            service.putExtra("result", result); service.putExtra("data", data);
            startForegroundService(service);
        } catch (Exception e) { nativeStatus("启动录屏失败："+e); nativeStopped(); }
    }
    public static void stopCapture() {
        final FurinaActivity a=current;
        if(a!=null)a.runOnUiThread(() -> {a.awaitingConsent=false; a.stopService(new Intent(a,CaptureService.class));});
    }
    public static void openAccessibility() {
        final FurinaActivity a=current;
        if(a!=null)a.runOnUiThread(() -> a.startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)));
    }
    public static void remoteGesture(int x1,int y1,int x2,int y2,int duration) {
        final FurinaActivity a=current;
        if(a!=null)a.runOnUiThread(() -> ControlService.gesture(x1,y1,x2,y2,duration));
    }
    public static void remoteKey(int key) {
        final FurinaActivity a=current;
        if(a!=null)a.runOnUiThread(() -> ControlService.key(key));
    }
}
