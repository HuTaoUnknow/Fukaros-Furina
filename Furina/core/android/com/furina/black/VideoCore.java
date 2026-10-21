package com.furina.black;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.concurrent.atomic.AtomicInteger;

/** Native video path. CPU never reads capture pixels or compresses Bitmaps. */
public final class VideoCore {
    public static final int FPS = 60, HEADER = 32, KEY = 1;
    private static int epochCounter = 0;
    public static volatile int captureEdge = 1080;
    private static volatile Encoder activeEncoder;
    private static Decoder decoder;
    private static SurfaceView texture;
    private static int viewX, viewY, viewW, viewH;
    private static boolean visible;

    private static boolean hardware(MediaCodecInfo info) {
        if (Build.VERSION.SDK_INT >= 29)
            return info.isHardwareAccelerated();
        String name = info.getName().toLowerCase();
        return !name.startsWith("omx.google.") && !name.startsWith("c2.android.");
    }

    private static String findCodec(boolean encoding, int width, int height) {
        for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos()) {
            if (info.isEncoder() != encoding || !hardware(info))
                continue;
            for (String type : info.getSupportedTypes())
                if (type.equalsIgnoreCase("video/avc")) {
                    try {
                        MediaCodecInfo.VideoCapabilities caps =
                            info.getCapabilitiesForType(type).getVideoCapabilities();
                        if (caps.areSizeAndRateSupported(width, height, FPS))
                            return info.getName();
                    } catch (Exception ignored) {
                    }
                }
        }
        return null;
    }

    private static byte[] bytes(ByteBuffer buffer) {
        ByteBuffer copy = buffer.duplicate();
        byte[] result = new byte[copy.remaining()];
        copy.get(result);
        return result;
    }

    private static byte[] join(byte[] a, byte[] b) {
        byte[] result = new byte[a.length + b.length];
        System.arraycopy(a, 0, result, 0, a.length);
        System.arraycopy(b, 0, result, a.length, b.length);
        return result;
    }

    public static final class Encoder {
        public final int width, height, epoch;
        public final Surface surface;
        private final Handler handler;
        private MediaCodec codec;
        private byte[] configuration = new byte[0];
        private boolean closed, waitKey = true;
        private int sequence, frames;
        private long gpuWorkNs;

        void renderWork(long ns) {
            gpuWorkNs += ns;
        }

        private long sample = SystemClock.elapsedRealtime();

        public Encoder(int sourceW, int sourceH, int edge, Handler worker) throws Exception {
            handler = worker;
            double scale = Math.min(1.0, edge / (double)Math.max(sourceW, sourceH));
            width = Math.max(2, ((int)(sourceW * scale) / 2) * 2);
            height = Math.max(2, ((int)(sourceH * scale) / 2) * 2);
            epoch = ++epochCounter;
            String name = findCodec(true, width, height);
            if (name == null)
                throw new IllegalStateException("No hardware H.264 encoder supporting " + width + "x" +
                                                height + "@60");
            codec = MediaCodec.createByCodecName(name);
            codec.setCallback(new MediaCodec.Callback() {
                @Override
                public void onInputBufferAvailable(MediaCodec c, int index) {}
                @Override
                public void onOutputFormatChanged(MediaCodec c, MediaFormat format) {
                    configuration = new byte[0];
                    for (String key : new String[] {"csd-0", "csd-1"})
                        if (format.containsKey(key))
                            configuration = join(configuration, bytes(format.getByteBuffer(key)));
                }
                @Override
                public void onOutputBufferAvailable(MediaCodec c, int index, MediaCodec.BufferInfo info) {
                    try {
                        if (closed)
                            return;
                        if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0 || info.size <= 0)
                            return;
                        boolean key = (info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
                        if (waitKey && !key)
                            return;
                        ByteBuffer data = c.getOutputBuffer(index).duplicate();
                        data.position(info.offset);
                        data.limit(info.offset + info.size);
                        byte[] body = bytes(data);
                        if (key) {
                            body = join(configuration, body);
                            waitKey = false;
                        }
                        ByteBuffer packet =
                            ByteBuffer.allocate(HEADER + body.length).order(ByteOrder.BIG_ENDIAN);
                        packet.putInt(key ? KEY : 0)
                            .putInt(++sequence)
                            .putInt(width)
                            .putInt(height)
                            .putLong(info.presentationTimeUs)
                            .putInt(epoch)
                            .putInt(1)
                            .put(body);
                        if (!FurinaActivity.nativeVideo(packet.array()))
                            requestKey();
                        else
                            ++frames;
                        long now = SystemClock.elapsedRealtime();
                        if (now - sample >= 1000) {
                            FurinaActivity.nativeVideoStats(true, frames * 1000.0 / (now - sample),
                                                            frames == 0 ? 0 : gpuWorkNs / 1000000.0 / frames,
                                                            0, 0, width, height, now - sample);
                            frames = 0;
                            gpuWorkNs = 0;
                            sample = now;
                        }
                    } catch (Exception e) {
                        FurinaActivity.nativeStatus("视频编码帧失败：" + e);
                    } finally {
                        try {
                            c.releaseOutputBuffer(index, false);
                        } catch (Exception ignored) {
                        }
                    }
                }
                @Override
                public void onError(MediaCodec c, MediaCodec.CodecException e) {
                    FurinaActivity.nativeStatus("硬件编码错误：" + e);
                }
            }, handler);
            MediaFormat format = MediaFormat.createVideoFormat("video/avc", width, height);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                              MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, FPS);
            format.setInteger(MediaFormat.KEY_BIT_RATE,
                              Math.max(6000000, Math.min(28000000, width * height * 10)));
            // Avoid a large intra-frame quality pulse every second. Recovery can still
            // request a sync frame immediately after an actual packet loss.
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 5);
            MediaCodecInfo.EncoderCapabilities rateCaps =
                codec.getCodecInfo().getCapabilitiesForType("video/avc").getEncoderCapabilities();
            if (rateCaps.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR))
                format.setInteger(MediaFormat.KEY_BITRATE_MODE,
                                  MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR);
            if (Build.VERSION.SDK_INT >= 31) {
                format.setInteger(MediaFormat.KEY_VIDEO_QP_I_MAX, 28);
                format.setInteger(MediaFormat.KEY_VIDEO_QP_P_MAX, 30);
                format.setInteger(MediaFormat.KEY_VIDEO_QP_B_MAX, 30);
            }
            format.setInteger(MediaFormat.KEY_PRIORITY, 0);
            format.setFloat(MediaFormat.KEY_OPERATING_RATE, FPS);
            if (Build.VERSION.SDK_INT >= 29)
                format.setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0);
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            surface = codec.createInputSurface();
            codec.start();
            activeEncoder = this;
            FurinaActivity.nativeStatus(
                "安卓 H.264 硬件编码：编码器 " + name + "，分辨率 " + width + "×" + height +
                " 像素，目标帧率 60 帧/秒\nEN: Android hardware H.264 encoding: codec " + name +
                ", resolution " + width + "x" + height + ", target 60 FPS");
        }

        public void requestKey() {
            if (closed)
                return;
            waitKey = true;
            Bundle parameters = new Bundle();
            parameters.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
            try {
                codec.setParameters(parameters);
            } catch (Exception ignored) {
            }
        }

        public void close() {
            closed = true;
            if (activeEncoder == this)
                activeEncoder = null;
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Exception ignored) {
                }
                codec.release();
                codec = null;
            }
            surface.release();
        }
    }

    public static void releaseActivity() {
        synchronized (VideoCore.class) {
            if (decoder != null) {
                Decoder old = decoder;
                decoder = null;
                old.handler.post(() -> {
                    old.close();
                    old.output = null;
                    old.handler.removeCallbacksAndMessages(null);
                    old.thread.quitSafely();
                });
            }
        }
        texture = null;
        visible = false;
        CursorLayer.release();
    }

    public static void setCaptureEdge(int edge) {
        captureEdge = edge;
        CaptureService.reconfigureVideo(edge);
    }

    public static void requestKeyframe() {
        Encoder encoder = activeEncoder;
        if (encoder != null)
            encoder.handler.post(encoder::requestKey);
    }

    public static void submit(byte[] packet) {
        synchronized (VideoCore.class) {
            if (decoder == null)
                decoder = new Decoder();
            decoder.offer(packet);
        }
    }

    public static void reset() {
        CursorLayer.reset();
        synchronized (VideoCore.class) {
            if (decoder != null)
                decoder.reset();
        }
        FurinaActivity a = FurinaActivity.currentActivity();
        if (a != null)
            a.runOnUiThread(() -> {
                visible = false;
                if (texture != null)
                    texture.setVisibility(View.GONE);
                CursorLayer.show(a, viewX, viewY, viewW, viewH, false);
            });
    }

    public static void show(int x, int y, int width, int height, boolean show, boolean fullscreen) {
        FurinaActivity a = FurinaActivity.currentActivity();
        if (a == null)
            return;
        a.runOnUiThread(() -> {
            if (texture == null && !show)
                return;
            if (texture == null) {
                FrameLayout parent = (FrameLayout)a.findViewById(android.R.id.content);
                texture = new SurfaceView(a);
                texture.setZOrderMediaOverlay(true);
                texture.setVisibility(View.GONE);
                parent.addView(texture, new FrameLayout.LayoutParams(1, 1));
                texture.getHolder().addCallback(new SurfaceHolder.Callback() {
                    @Override
                    public void surfaceCreated(SurfaceHolder holder) {
                        synchronized (VideoCore.class) {
                            if (decoder == null)
                                decoder = new Decoder();
                            decoder.surface(holder.getSurface());
                        }
                        fit();
                    }
                    @Override
                    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {}
                    @Override
                    public void surfaceDestroyed(SurfaceHolder holder) {
                        synchronized (VideoCore.class) {
                            if (decoder != null)
                                decoder.surface(null);
                        }
                    }
                });
            }
            visible = show;
            viewX = x;
            viewY = y;
            viewW = Math.max(1, width);
            viewH = Math.max(1, height);
            texture.setVisibility(show ? View.VISIBLE : View.GONE);
            fit();
        });
    }

    private static void fit() {
        if (texture == null)
            return;
        Decoder d = decoder;
        int width = viewW, height = viewH;
        if (d != null && d.width > 1 && d.height > 1) {
            float scale = Math.min(viewW / (float)d.width, viewH / (float)d.height);
            width = Math.max(1, (int)(d.width * scale));
            height = Math.max(1, (int)(d.height * scale));
        }
        FrameLayout.LayoutParams params =
            new FrameLayout.LayoutParams(Math.max(1, width), Math.max(1, height));
        params.leftMargin = viewX + (viewW - width) / 2;
        params.topMargin = viewY + (viewH - height) / 2;
        texture.setLayoutParams(params);
        CursorLayer.show(FurinaActivity.currentActivity(), params.leftMargin, params.topMargin, params.width,
                         params.height, visible);
    }

    private static final class Decoder {
        final AtomicInteger presented = new AtomicInteger();
        final HandlerThread thread = new HandlerThread("furina-video-decode");
        final Handler handler;
        final ArrayDeque<byte[]> queue = new ArrayDeque<>();
        final Runnable consumeTask = this::consume;
        volatile boolean surfaceReady;
        MediaCodec codec;
        Surface output;
        int width, height, epoch, frames;
        boolean waitKey = true, scheduled, flushNeeded;
        volatile long warmingUntil;
        String restartReason = "initial";
        int restarts, overflows, queuePeak;
        final java.util.TreeMap<Long, Boolean> cursorModes = new java.util.TreeMap<>();
        long sample = SystemClock.elapsedRealtime(), workNs;

        Decoder() {
            thread.start();
            handler = new Handler(thread.getLooper());
            handler.postDelayed(this::statistics, 1000);
        }

        void offer(byte[] packet) {
            if (!surfaceReady || packet == null || packet.length <= HEADER)
                return;
            synchronized (queue) {
                // Codec startup can briefly take longer than six frame intervals.
                // Give it a bounded warm-up queue, then return to the low-latency limit.
                int limit = SystemClock.elapsedRealtime() < warmingUntil ? 16 : 6;
                if (queue.size() >= limit) {
                    ++overflows;
                    android.util.Log.i("FurinaVideo", "recovery reason=queue-overflow pending=" + queue.size());
                    queue.clear();
                    waitKey = true;
                    // Recovery belongs to the decode loop. Posting close() here can
                    // destroy a newly configured codec several times in succession.
                    if (!flushNeeded) {
                        flushNeeded = true;
                        FurinaActivity.nativeNeedKey();
                    }
                }
                if (waitKey && (ByteBuffer.wrap(packet).getInt() & KEY) == 0)
                    return;
                waitKey = false;
                queue.add(packet);
                queuePeak = Math.max(queuePeak, queue.size());
                schedule();
            }
        }

        void schedule() {
            if (!scheduled) {
                scheduled = true;
                handler.post(consumeTask);
            }
        }

        void surface(Surface surface) {
            android.util.Log.i("FurinaVideo", "surface ready=" + (surface != null));
            surfaceReady = surface != null;
            handler.post(() -> {
                handler.removeCallbacks(consumeTask);
                close();
                restartReason = "surface-change";
                output = surface;
                synchronized (queue) {
                    waitKey = true;
                    queue.clear();
                    scheduled = false;
                    flushNeeded = false;
                }
                // Home destroys the Surface, not the approved peer session.
                // Request fresh references only when a replacement is ready.
                if (output != null) {
                    FurinaActivity.nativeNeedKey();
                    if (Build.VERSION.SDK_INT >= 30)
                        output.setFrameRate(FPS, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
                }
            });
        }

        void reset() {
            handler.post(() -> {
                close();
                restartReason = "session-reset";
                synchronized (queue) {
                    queue.clear();
                    waitKey = true;
                    flushNeeded = false;
                }
                frames = 0;
                presented.set(0);
            });
        }

        void close() {
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Exception ignored) {
                }
                codec.release();
                codec = null;
            }
            cursorModes.clear();
        }

        void consume() {
            try {
                if (output == null || !surfaceReady) {
                    synchronized (queue) {
                        scheduled = false;
                    }
                    return;
                }
                byte[] packet;
                boolean recover;
                synchronized (queue) {
                    packet = queue.peek();
                    recover = packet != null && flushNeeded;
                    if (packet != null)
                        flushNeeded = false;
                }
                if (packet == null) {
                    drain();
                    if (codec != null) {
                        synchronized (queue) {
                            scheduled = true;
                            handler.postDelayed(consumeTask, 2);
                        }
                    } else {
                        synchronized (queue) {
                            scheduled = false;
                        }
                    }
                    return;
                }
                ByteBuffer header = ByteBuffer.wrap(packet).order(ByteOrder.BIG_ENDIAN);
                int flags = header.getInt(), sequence = header.getInt(), w = header.getInt(),
                    h = header.getInt();
                long pts = header.getLong();
                int e = header.getInt();
                if (codec == null || epoch != e) {
                    if (codec != null)
                        restartReason = "stream-epoch " + epoch + "->" + e;
                    if ((flags & KEY) == 0) {
                        synchronized (queue) {
                            if (queue.peek() == packet)
                                queue.poll();
                            waitKey = true;
                            scheduled = false;
                        }
                        FurinaActivity.nativeNeedKey();
                        return;
                    }
                    close();
                    warmingUntil = SystemClock.elapsedRealtime() + 1000;
                    String name = findCodec(false, w, h);
                    if (name == null)
                        throw new IllegalStateException("No hardware decoder supporting " + w + "x" + h +
                                                        "@60");
                    codec = MediaCodec.createByCodecName(name);
                    MediaFormat format = MediaFormat.createVideoFormat("video/avc", w, h);
                    format.setInteger(MediaFormat.KEY_PRIORITY, 0);
                    format.setInteger(MediaFormat.KEY_FRAME_RATE, FPS);
                    if (Build.VERSION.SDK_INT >= 30)
                        format.setInteger(MediaFormat.KEY_LOW_LATENCY, 1);
                    codec.configure(format, output, null, 0);
                    codec.setOnFrameRenderedListener((c, renderPts, nano) -> {
                        presented.incrementAndGet();
                        java.util.Map.Entry<Long, Boolean> entry = cursorModes.floorEntry(renderPts);
                        if (entry != null) {
                            CursorLayer.mode(entry.getValue());
                            cursorModes.headMap(renderPts, true).clear();
                        }
                    }, handler);
                    codec.start();
                    warmingUntil = SystemClock.elapsedRealtime() + 250;
                    width = w;
                    height = h;
                    epoch = e;
                    android.util.Log.i("FurinaVideo", "decoder-start count=" + (++restarts) +
                        " reason=" + restartReason + " epoch=" + e + " size=" + w + "x" + h);
                    FurinaActivity.nativeStatus(
                        "安卓 H.264 硬件解码：解码器 " + name + "，分辨率 " + w + "×" + h +
                        " 像素，目标帧率 60 帧/秒\nEN: Android hardware H.264 decoding: codec " + name +
                        ", resolution " + w + "x" + h + ", target 60 FPS");
                    FurinaActivity a = FurinaActivity.currentActivity();
                    if (a != null)
                        a.runOnUiThread(VideoCore::fit);
                } else if (recover) {
                    // Compressed references were dropped: flush once, then resume
                    // from the queued keyframe (which carries fresh SPS/PPS).
                    codec.flush();
                    cursorModes.clear();
                    android.util.Log.i("FurinaVideo", "decoder-flush reason=queue-overflow epoch=" + epoch);
                }
                long start = System.nanoTime();
                int index = codec.dequeueInputBuffer(0);
                if (index >= 0) {
                    ByteBuffer buffer = codec.getInputBuffer(index);
                    buffer.clear();
                    buffer.put(packet, HEADER, packet.length - HEADER);
                    cursorModes.put(pts, (flags & 2) != 0);
                    while (cursorModes.size() > 120)
                        cursorModes.pollFirstEntry();
                    codec.queueInputBuffer(index, 0, packet.length - HEADER, pts, 0);
                    synchronized (queue) {
                        if (queue.peek() == packet)
                            queue.poll();
                    }
                }
                drain();
                workNs += System.nanoTime() - start;
                synchronized (queue) {
                    if (!queue.isEmpty()) {
                        scheduled = true;
                        handler.postDelayed(consumeTask, index < 0 ? 1 : 0);
                    } else {
                        scheduled = true;
                        handler.postDelayed(consumeTask, 2);
                    }
                }
            } catch (Exception e) {
                restartReason = "decode-error";
                android.util.Log.e("FurinaVideo", "decode-error", e);
                FurinaActivity.nativeStatus("视频解码失败：" + e);
                close();
                synchronized (queue) {
                    queue.clear();
                    waitKey = true;
                    scheduled = false;
                    flushNeeded = false;
                }
                FurinaActivity.nativeNeedKey();
            }
        }

        void drain() {
            if (codec == null)
                return;
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            int latest = -1;
            // Live desktop viewing needs the newest decoded picture at the next
            // display refresh, not a fixed 50 ms playback timeline. Keep all
            // compressed reference frames; discard only superseded OUTPUTs.
            for (int n = 0; n < 8; ++n) {
                int index = codec.dequeueOutputBuffer(info, 0);
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED)
                    continue;
                if (index < 0)
                    break;
                if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    codec.releaseOutputBuffer(index, false);
                    continue;
                }
                if (latest >= 0)
                    codec.releaseOutputBuffer(latest, false);
                latest = index;
                ++frames;
            }
            if (latest >= 0)
                codec.releaseOutputBuffer(latest, true);
        }

        void statistics() {
            long now = SystemClock.elapsedRealtime(), elapsed = now - sample;
            if (elapsed > 0) {
                int count = presented.getAndSet(0), pending;
                synchronized (queue) {
                    pending = queue.size();
                    android.util.Log.i("FurinaVideo", "sample decoded=" + (frames * 1000.0 / elapsed) +
                        " shown=" + (count * 1000.0 / elapsed) + " pending=" + pending +
                        " peak=" + queuePeak + " overflows=" + overflows + " restarts=" + restarts);
                    queuePeak = 0;
                }
                if (codec != null)
                    FurinaActivity.nativeVideoStats(
                        false, frames * 1000.0 / elapsed, frames == 0 ? 0 : workNs / 1000000.0 / frames,
                        pending * 1000 / FPS, count * 1000.0 / elapsed, width, height, elapsed);
                frames = 0;
                workNs = 0;
                sample = now;
            }
            handler.postDelayed(this::statistics, 1000);
        }
    }
}
