package com.furina.black;
import android.app.Service;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.res.Configuration;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.DisplayMetrics;
import android.view.WindowManager;
import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;

public class CaptureService extends Service {
    private static volatile CaptureService current;
    private HandlerThread worker;private Handler handler;
    private MediaProjection projection;private VirtualDisplay display;private VideoCore.Encoder encoder;private GpuCapture gpu;
    private int sourceW,sourceH,density;private volatile boolean destroyed;
    @Override public void onCreate(){super.onCreate();current=this;worker=new HandlerThread("furina-video-encode");worker.start();handler=new Handler(worker.getLooper());density=getResources().getConfiguration().densityDpi;}
    @Override public int onStartCommand(Intent intent,int flags,int id){
        if(intent!=null&&"STOP".equals(intent.getAction())){stopSelf();return START_NOT_STICKY;}
        if(intent==null||intent.getParcelableExtra("data")==null){stopSelf();return START_NOT_STICKY;}if(projection!=null)return START_NOT_STICKY;
        NotificationManager nm=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);nm.createNotificationChannel(new NotificationChannel("furina_capture","Furina 屏幕共享",NotificationManager.IMPORTANCE_LOW));
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,CaptureService.class).setAction("STOP"),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification=new Notification.Builder(this,"furina_capture").setSmallIcon(android.R.drawable.ic_menu_camera).setContentTitle("Furina 正在共享屏幕").setContentText("H.264 硬件编码 · 目标 60 FPS").setOngoing(true).addAction(new Notification.Action.Builder(null,"停止共享",stop).build()).build();
        if(Build.VERSION.SDK_INT>=29)startForeground(8301,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);else startForeground(8301,notification);
        final int result=intent.getIntExtra("result",-1);final Intent data=intent.getParcelableExtra("data");
        handler.post(()->{
            if(destroyed)return;
            try{
                MediaProjectionManager manager=(MediaProjectionManager)getSystemService(MEDIA_PROJECTION_SERVICE);projection=manager.getMediaProjection(result,data);
                projection.registerCallback(new MediaProjection.Callback(){
                    @Override public void onStop(){FurinaActivity.nativeStatus("系统已停止屏幕共享");stopSelf();}
                    @Override public void onCapturedContentResize(int w,int h){if(w!=sourceW||h!=sourceH){sourceW=w;sourceH=h;if(gpu!=null)gpu.resizeCapture(w,h);if(display!=null)display.resize(w,h,density);configure(VideoCore.captureEdge);}}
                },handler);
                int[] size=screenSize();sourceW=size[0];sourceH=size[1];encoder=new VideoCore.Encoder(sourceW,sourceH,VideoCore.captureEdge,handler);
                gpu=new GpuCapture(sourceW,sourceH,encoder,handler);display=projection.createVirtualDisplay("Furina",sourceW,sourceH,density,DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,gpu.captureSurface,null,handler);
            }catch(Exception e){FurinaActivity.nativeStatus("录屏启动失败："+e);stopSelf();}
        });return START_NOT_STICKY;
    }
    private int[] screenSize(){WindowManager wm=(WindowManager)getSystemService(WINDOW_SERVICE);if(Build.VERSION.SDK_INT>=30){Rect r=wm.getMaximumWindowMetrics().getBounds();return new int[]{r.width(),r.height()};}DisplayMetrics m=new DisplayMetrics();wm.getDefaultDisplay().getRealMetrics(m);return new int[]{m.widthPixels,m.heightPixels};}
    public static void stopGpuFailure(){CaptureService service=current;if(service!=null)service.stopSelf();}
    public static void reconfigureVideo(int edge){CaptureService service=current;if(service!=null)service.handler.post(()->service.configure(edge));}
    private void configure(int edge){if(destroyed||display==null||sourceW<2||sourceH<2)return;
        double scale=Math.min(1.0,edge/(double)Math.max(sourceW,sourceH));int w=((int)(sourceW*scale)/2)*2,h=((int)(sourceH*scale)/2)*2;if(encoder!=null&&encoder.width==w&&encoder.height==h)return;
        try{VideoCore.Encoder next=new VideoCore.Encoder(sourceW,sourceH,edge,handler);VideoCore.Encoder old=encoder;gpu.target(next);encoder=next;if(old!=null)old.close();}
        catch(Exception e){FurinaActivity.nativeStatus("分辨率切换失败："+e);}
    }
    @Override public void onConfigurationChanged(Configuration config){super.onConfigurationChanged(config);if(Build.VERSION.SDK_INT<34)handler.post(()->{int[] size=screenSize();sourceW=size[0];sourceH=size[1];configure(VideoCore.captureEdge);});}
    @Override public void onDestroy(){current=null;destroyed=true;
        handler.post(()->{if(display!=null){display.release();display=null;}if(gpu!=null){gpu.close();gpu=null;}if(encoder!=null){encoder.close();encoder=null;}if(projection!=null){projection.stop();projection=null;}worker.quitSafely();});
        stopForeground(true);FurinaActivity.nativeStopped();super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent){return null;}
}
