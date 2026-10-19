package com.furina.black;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.GestureDescription;
import android.view.accessibility.AccessibilityEvent;
import android.view.WindowManager;
import android.util.DisplayMetrics;
import android.graphics.Path;
import android.graphics.Rect;
import android.os.Build;

public class ControlService extends AccessibilityService {
    private static ControlService current;
    private boolean busy;

    @Override
    protected void onServiceConnected() {
        current = this;
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {}

    @Override
    public void onInterrupt() {}

    @Override
    public void onDestroy() {
        if (current == this)
            current = null;
        super.onDestroy();
    }

    public static void gesture(int x1, int y1, int x2, int y2, int duration) {
        ControlService service = current;
        if (service == null) {
            FurinaActivity.nativeStatus("请在平板设置中启用 Furina 无障碍服务");
            return;
        }
        if (service.busy) {
            FurinaActivity.nativeStatus("上一手势尚未完成");
            return;
        }
        WindowManager wm = (WindowManager)service.getSystemService(WINDOW_SERVICE);
        int width, height;
        if (Build.VERSION.SDK_INT >= 30) {
            Rect r = wm.getMaximumWindowMetrics().getBounds();
            width = r.width();
            height = r.height();
        } else {
            DisplayMetrics m = new DisplayMetrics();
            wm.getDefaultDisplay().getRealMetrics(m);
            width = m.widthPixels;
            height = m.heightPixels;
        }
        float a = x1 / 65535f * (width - 1), b = y1 / 65535f * (height - 1), c = x2 / 65535f * (width - 1),
              d = y2 / 65535f * (height - 1);
        Path path = new Path();
        path.moveTo(a, b);
        if (Math.abs(a - c) > 1 || Math.abs(b - d) > 1)
            path.lineTo(c, d);
        GestureDescription action = new GestureDescription.Builder()
                                        .addStroke(new GestureDescription.StrokeDescription(
                                            path, 0, Math.max(1, Math.min(duration, 2000))))
                                        .build();
        service.busy = true;
        boolean accepted = service.dispatchGesture(action, new GestureResultCallback() {
            @Override
            public void onCompleted(GestureDescription g) {
                service.busy = false;
            }
            @Override
            public void onCancelled(GestureDescription g) {
                service.busy = false;
                FurinaActivity.nativeStatus("系统取消了触控手势");
            }
        }, null);
        if (!accepted) {
            service.busy = false;
            FurinaActivity.nativeStatus("系统拒绝了触控手势");
        }
    }

    public static void key(int key) {
        ControlService s = current;
        if (s == null) {
            FurinaActivity.nativeStatus("无障碍服务未开启");
            return;
        }
        int action = key == 4     ? GLOBAL_ACTION_BACK
                     : key == 3   ? GLOBAL_ACTION_HOME
                     : key == 187 ? GLOBAL_ACTION_RECENTS
                                  : 0;
        if (action != 0 && !s.performGlobalAction(action))
            FurinaActivity.nativeStatus("系统拒绝了导航操作");
    }
}
