package com.furina.black;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.view.Choreographer;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Cursor-only transparent surface. Its updates never wait for MediaCodec. */
public final class CursorLayer {
    private static Layer layer;
    private static Bitmap shape;
    private static int x,y,sourceW=1,sourceH=1;
    private static boolean pointerVisible,videoSeparate,videoVisible,scheduled;
    private static long generation;
    private static int updates;

    public static synchronized void submit(byte[] packet) {
        if(packet==null||packet.length<4)return;
        ByteBuffer b=ByteBuffer.wrap(packet).order(ByteOrder.BIG_ENDIAN);
        int type=b.getInt();
        if(type==1&&packet.length==24){
            int nx=b.getInt(),ny=b.getInt(),w=b.getInt(),h=b.getInt(),show=b.getInt();
            if(w<1||h<1||w>32768||h>32768||nx< -512||ny< -512||nx>=w||ny>=h||(show!=0&&show!=1))return;
            x=nx;y=ny;sourceW=w;sourceH=h;pointerVisible=show!=0;++updates;
        }else if(type==2&&packet.length>=16){
            int kind=b.getInt(),w=b.getInt(),h=b.getInt();
            if((kind!=1&&kind!=2&&kind!=4)||w<1||h<1||w>512||h>512||packet.length!=16+w*h*4)return;
            int[] argb=new int[w*h];
            for(int i=0;i<argb.length;++i){int blue=b.get()&255,green=b.get()&255,red=b.get()&255,alpha=b.get()&255;
                // Desktop XOR requires sampling the desktop. The independent
                // surface uses a high-contrast outline for those cursor pixels.
                if(kind==1){if(red==255&&green==0){alpha=0;red=green=blue=0;}else{alpha=255;red=blue=green;}}
                else if(kind==4){if(alpha!=0){if((red|green|blue)==0)alpha=0;else{red=green=blue=255;alpha=255;}}else alpha=255;}
                argb[i]=(alpha<<24)|(red<<16)|(green<<8)|blue;
            }
            shape=Bitmap.createBitmap(argb,w,h,Bitmap.Config.ARGB_8888);
        }else return;
        redraw();
    }
    public static synchronized void mode(boolean separate){if(videoSeparate==separate)return;videoSeparate=separate;FurinaActivity.nativeStatus(separate?"鼠标显示方式：独立映射\nEN: Cursor rendering: independent mapping":"鼠标显示方式：视频合成\nEN: Cursor rendering: video composition");redraw();}
    public static synchronized void reset(){++generation;shape=null;pointerVisible=false;videoSeparate=false;updates=0;redraw();}
    public static synchronized int takeUpdates(){int count=updates;updates=0;return count;}
    public static synchronized void release(){++generation;layer=null;shape=null;pointerVisible=false;videoVisible=false;videoSeparate=false;scheduled=false;}
    // Called by VideoCore.fit() on the Android UI thread, using the exact same
    // fitted video rectangle for portrait, letterboxing and full screen.
    public static synchronized void show(FurinaActivity activity,int left,int top,int width,int height,boolean visible){
        if(layer==null&&visible){
            layer=new Layer(activity);FrameLayout parent=activity.findViewById(android.R.id.content);
            parent.addView(layer,new FrameLayout.LayoutParams(1,1));
        }
        videoVisible=visible;if(layer==null)return;
        FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(Math.max(1,width),Math.max(1,height));p.leftMargin=left;p.topMargin=top;
        if(!sameBounds(layer,p))layer.setLayoutParams(p);
        layer.setVisibility(visible?View.VISIBLE:View.GONE);redraw();
    }
    private static boolean sameBounds(Layer view,FrameLayout.LayoutParams p){
        if(!(view.getLayoutParams() instanceof FrameLayout.LayoutParams))return false;
        FrameLayout.LayoutParams old=(FrameLayout.LayoutParams)view.getLayoutParams();return old.width==p.width&&old.height==p.height&&old.leftMargin==p.leftMargin&&old.topMargin==p.topMargin;
    }
    private static synchronized void redraw(){
        FurinaActivity a=FurinaActivity.currentActivity();if(layer==null||a==null||scheduled)return;
        scheduled=true;long token=generation;
        a.runOnUiThread(()->Choreographer.getInstance().postFrameCallback(time->{Layer target;synchronized(CursorLayer.class){scheduled=false;if(token!=generation){redraw();return;}target=layer;}if(target!=null)target.drawCursor();}));
    }
    private static final class Layer extends SurfaceView implements SurfaceHolder.Callback {
        boolean surfaceReady;final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        final PorterDuffColorFilter outline=new PorterDuffColorFilter(Color.BLACK,PorterDuff.Mode.SRC_IN);
        Layer(FurinaActivity a){super(a);setZOrderOnTop(true);getHolder().setFormat(PixelFormat.TRANSLUCENT);getHolder().addCallback(this);setClickable(false);setFocusable(false);setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);}
        @Override public void surfaceCreated(SurfaceHolder h){surfaceReady=true;redraw();}
        @Override public void surfaceChanged(SurfaceHolder h,int format,int w,int height){redraw();}
        @Override public void surfaceDestroyed(SurfaceHolder h){surfaceReady=false;}
        void drawCursor(){Bitmap bitmap;int px,py,sw,sh;boolean draw;
            synchronized(CursorLayer.class){if(!surfaceReady||!videoVisible)return;bitmap=shape;px=x;py=y;sw=sourceW;sh=sourceH;draw=videoSeparate&&pointerVisible&&bitmap!=null;}Canvas c=null;
            try{c=getHolder().lockHardwareCanvas();c.drawColor(Color.TRANSPARENT,PorterDuff.Mode.CLEAR);
                if(draw){
                    c.save();c.scale(getWidth()/(float)sw,getHeight()/(float)sh);
                    paint.setColorFilter(outline);for(int[] offset:new int[][]{{-1,0},{1,0},{0,-1},{0,1}})c.drawBitmap(bitmap,px+offset[0],py+offset[1],paint);
                    paint.setColorFilter(null);c.drawBitmap(bitmap,px,py,paint);c.restore();
                }
            }catch(Exception ignored){}finally{if(c!=null)getHolder().unlockCanvasAndPost(c);}
        }
    }
}
