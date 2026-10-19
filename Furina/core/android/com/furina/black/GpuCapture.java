package com.furina.black;

import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLExt;
import android.opengl.EGLDisplay;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.opengl.GLES11Ext;
import android.os.Handler;
import android.view.Surface;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

/** Samples the latest MediaProjection texture at 60 Hz entirely on the GPU. */
final class GpuCapture {
    final Surface captureSurface;
    private final Handler handler;
    private final EGLDisplay display;
    private final EGLConfig config;
    private final EGLContext context;
    private EGLSurface target;
    private final SurfaceTexture capture;
    private final FloatBuffer vertices;
    private final float[] transform = new float[16];
    private final int program, texture, position, uv, matrix;
    private int width, height;
    private VideoCore.Encoder encoder;
    private boolean available, hasFrame, closed;
    private long due;

    GpuCapture(int sourceW, int sourceH, VideoCore.Encoder encoder, Handler worker) {
        this.encoder = encoder;
        handler = worker;
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        int[] version = new int[2];
        if (!EGL14.eglInitialize(display, version, 0, version, 1))
            throw new IllegalStateException("EGL initialize");
        int[] attributes = {EGL14.EGL_RED_SIZE,
                            8,
                            EGL14.EGL_GREEN_SIZE,
                            8,
                            EGL14.EGL_BLUE_SIZE,
                            8,
                            EGL14.EGL_RENDERABLE_TYPE,
                            EGL14.EGL_OPENGL_ES2_BIT,
                            0x3142,
                            1,
                            EGL14.EGL_NONE};
        EGLConfig[] configs = new EGLConfig[1];
        int[] count = new int[1];
        if (!EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) || count[0] < 1)
            throw new IllegalStateException("EGL recordable config");
        config = configs[0];
        context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT,
                                         new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
        target =
            EGL14.eglCreateWindowSurface(display, config, encoder.surface, new int[] {EGL14.EGL_NONE}, 0);
        if (!EGL14.eglMakeCurrent(display, target, target, context))
            throw new IllegalStateException("EGL capture context");
        EGL14.eglSwapInterval(display, 0);
        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);
        texture = textures[0];
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER,
                               GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER,
                               GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S,
                               GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T,
                               GLES20.GL_CLAMP_TO_EDGE);
        capture = new SurfaceTexture(texture);
        capture.setDefaultBufferSize(sourceW, sourceH);
        capture.setOnFrameAvailableListener(t -> available = true, handler);
        captureSurface = new Surface(capture);
        int vertex = shader(
            GLES20.GL_VERTEX_SHADER,
            "attribute vec2 position;attribute vec2 uv;uniform mat4 matrix;varying vec2 tex;void main(){gl_Position=vec4(position,0.,1.);tex=(matrix*vec4(uv,0.,1.)).xy;}");
        int fragment = shader(
            GLES20.GL_FRAGMENT_SHADER,
            "#extension GL_OES_EGL_image_external : require\nprecision mediump float;uniform samplerExternalOES image;varying vec2 tex;void main(){gl_FragColor=texture2D(image,tex);}");
        program = GLES20.glCreateProgram();
        GLES20.glAttachShader(program, vertex);
        GLES20.glAttachShader(program, fragment);
        GLES20.glLinkProgram(program);
        GLES20.glDeleteShader(vertex);
        GLES20.glDeleteShader(fragment);
        int[] linked = new int[1];
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linked, 0);
        if (linked[0] == 0)
            throw new IllegalStateException(GLES20.glGetProgramInfoLog(program));
        position = GLES20.glGetAttribLocation(program, "position");
        uv = GLES20.glGetAttribLocation(program, "uv");
        matrix = GLES20.glGetUniformLocation(program, "matrix");
        vertices = ByteBuffer.allocateDirect(16 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        vertices.put(new float[] {-1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1}).position(0);
        width = encoder.width;
        height = encoder.height;
        due = System.nanoTime();
        handler.post(this::draw);
    }

    private static int shader(int type, String source) {
        int id = GLES20.glCreateShader(type);
        GLES20.glShaderSource(id, source);
        GLES20.glCompileShader(id);
        int[] compiled = new int[1];
        GLES20.glGetShaderiv(id, GLES20.GL_COMPILE_STATUS, compiled, 0);
        if (compiled[0] == 0)
            throw new IllegalStateException(GLES20.glGetShaderInfoLog(id));
        return id;
    }

    void resizeCapture(int width, int height) {
        capture.setDefaultBufferSize(width, height);
    }

    void target(VideoCore.Encoder next) {
        EGLSurface surface =
            EGL14.eglCreateWindowSurface(display, config, next.surface, new int[] {EGL14.EGL_NONE}, 0);
        if (surface == EGL14.EGL_NO_SURFACE)
            throw new IllegalStateException("EGL encoder surface");
        EGL14.eglMakeCurrent(display, surface, surface, context);
        EGL14.eglDestroySurface(display, target);
        target = surface;
        encoder = next;
        width = next.width;
        height = next.height;
        EGL14.eglSwapInterval(display, 0);
    }

    private void draw() {
        if (closed)
            return;
        long now = System.nanoTime();
        if (now < due) {
            handler.postDelayed(this::draw, Math.max(1, (due - now) / 1000000));
            return;
        }
        due += 1000000000L / 60;
        if (now - due > 16666666L)
            due = now + 16666666L;
        try {
            if (available) {
                available = false;
                capture.updateTexImage();
                capture.getTransformMatrix(transform);
                hasFrame = true;
            }
            if (hasFrame) {
                GLES20.glViewport(0, 0, width, height);
                GLES20.glUseProgram(program);
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
                GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture);
                vertices.position(0);
                GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false, 16, vertices);
                GLES20.glEnableVertexAttribArray(position);
                vertices.position(2);
                GLES20.glVertexAttribPointer(uv, 2, GLES20.GL_FLOAT, false, 16, vertices);
                GLES20.glEnableVertexAttribArray(uv);
                GLES20.glUniformMatrix4fv(matrix, 1, false, transform, 0);
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
                EGLExt.eglPresentationTimeANDROID(display, target, due - 1000000000L / 60);
                if (!EGL14.eglSwapBuffers(display, target))
                    throw new IllegalStateException("EGL encoder swap");
            }
            if (hasFrame)
                encoder.renderWork(System.nanoTime() - now);
            handler.postDelayed(this::draw, Math.max(1, (due - System.nanoTime()) / 1000000));
        } catch (Exception e) {
            FurinaActivity.nativeStatus("GPU 录屏失败：" + e);
            CaptureService.stopGpuFailure();
        }
    }

    void close() {
        closed = true;
        captureSurface.release();
        capture.release();
        GLES20.glDeleteProgram(program);
        GLES20.glDeleteTextures(1, new int[] {texture}, 0);
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
        EGL14.eglDestroySurface(display, target);
        EGL14.eglDestroyContext(display, context);
        EGL14.eglTerminate(display);
    }
}
