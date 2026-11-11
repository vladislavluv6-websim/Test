package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Handler;
import android.os.Looper;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.nativemod.EspMod;
import org.levimc.launcher.preloader.PreloaderInput;

/**
 * Target ESP: рисуется на цели, которую ведёт AimAssist/TriggerBot.
 * Три модели: круг (качается вверх-вниз), ромб (вращается вместе с
 * поворотом игрока) и сфера (стоит на месте).
 */
public class TargetEspOverlay extends View {
    public static final int STYLE_CIRCLE = 0;
    public static final int STYLE_DIAMOND = 1;
    public static final int STYLE_SPHERE = 2;

    private static final long FADE_MS = 180L;
    private static final long POLL_INTERVAL_NANOS = 33_000_000L;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();
    private final RectF shape = new RectF();
    private float density = 1f;
    private boolean attached;
    private volatile boolean showing;
    private volatile boolean moduleEnabled = true;
    private int style = STYLE_CIRCLE;
    private int accent = 0xFF4E83FF;
    private long holdMs = 650L;
    private float x, y, w, h;
    private float yawDeg;
    private long ageMs = -1;
    private boolean framePosted;
    private long lastPollNanos;
    private boolean paused;

    private final Runnable resumeRunnable = () -> {
        if (attached) postFrame();
    };

    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            framePosted = false;
            if (!attached) return;
            if (!gameplayVisible()) {
                if (!paused) {
                    paused = true;
                    ageMs = -1;
                    invalidate();
                }
                mainHandler.removeCallbacks(resumeRunnable);
                mainHandler.postDelayed(resumeRunnable, 300L);
                return;
            }
            paused = false;
            if (frameTimeNanos - lastPollNanos >= POLL_INTERVAL_NANOS) {
                lastPollNanos = frameTimeNanos;
                poll();
            }
            if (ageMs >= 0) invalidate();
            postFrame();
        }
    };

    public TargetEspOverlay(Activity activity) {
        super(activity);
        setWillNotDraw(false);
        setBackgroundColor(Color.TRANSPARENT);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        setElevation(95f);
        density = activity.getResources().getDisplayMetrics().density;
        strokePaint.setStyle(Paint.Style.STROKE);
        strokePaint.setStrokeWidth(2f * density);
        glowPaint.setStyle(Paint.Style.STROKE);
        glowPaint.setStrokeWidth(7f * density);
        fillPaint.setStyle(Paint.Style.FILL);
        applySettings();
    }

    private void applySettings() {
        InbuiltModManager manager = InbuiltModManager.getInstance(getContext());
        style = Math.max(0, Math.min(STYLE_SPHERE, manager.getTargetEspStyle()));
        accent = manager.getTargetEspColor();
        holdMs = Math.max(150, manager.getTargetEspHoldMs());
        EspMod.setTargetEspHold((int) holdMs);
    }

    public void show() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(this::show);
            return;
        }
        if (showing) return;
        showing = true;
        applySettings();
        mainHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!showing || getParent() != null) return;
                try {
                    ViewGroup root = ((Activity) getContext())
                            .findViewById(android.R.id.content);
                    if (root == null) return;
                    FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.MATCH_PARENT);
                    root.addView(TargetEspOverlay.this, params);
                } catch (Exception ignored) {
                }
            }
        }, 100);
    }

    public void hide() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(this::hide);
            return;
        }
        showing = false;
        attached = false;
        mainHandler.removeCallbacks(resumeRunnable);
        Choreographer.getInstance().removeFrameCallback(frameCallback);
        framePosted = false;
        EspMod.setTargetEsp(false);
        ageMs = -1;
        try {
            if (getParent() instanceof ViewGroup) {
                ((ViewGroup) getParent()).removeView(this);
            }
        } catch (Exception ignored) {
        }
    }

    public void setModuleEnabled(boolean enabled) {
        if (moduleEnabled == enabled) return;
        moduleEnabled = enabled;
        EspMod.setTargetEsp(enabled);
        if (enabled) return;
        Runnable clear = () -> {
            ageMs = -1;
            invalidate();
        };
        if (Looper.myLooper() == Looper.getMainLooper()) clear.run();
        else mainHandler.post(clear);
    }

    public void applyConfigurationChanges() {
        applySettings();
        invalidate();
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        attached = true;
        InbuiltModManager manager = InbuiltModManager.getInstance(getContext());
        moduleEnabled = manager.isTargetEspEnabled();
        applySettings();
        EspMod.initializeAsync(ok -> {
            if (!attached || !ok) return;
            EspMod.setTargetEspHold((int) holdMs);
            EspMod.setTargetEsp(moduleEnabled);
        });
        updateSurfaceSize(getWidth(), getHeight());
        postFrame();
    }

    @Override
    protected void onDetachedFromWindow() {
        attached = false;
        framePosted = false;
        mainHandler.removeCallbacks(resumeRunnable);
        Choreographer.getInstance().removeFrameCallback(frameCallback);
        super.onDetachedFromWindow();
    }

    @Override
    protected void onSizeChanged(int nw, int nh, int oldw, int oldh) {
        super.onSizeChanged(nw, nh, oldw, oldh);
        updateSurfaceSize(nw, nh);
    }

    private void updateSurfaceSize(int nw, int nh) {
        if (nw > 0 && nh > 0) EspMod.setNameTagScreenSize(nw, nh);
    }

    private void postFrame() {
        if (framePosted || !attached) return;
        framePosted = true;
        Choreographer.getInstance().postFrameCallback(frameCallback);
    }

    private boolean gameplayVisible() {
        boolean hudOpen = PreloaderInput.isHudScreenOpen();
        boolean pauseOpen = PreloaderInput.isPauseMenuOpen();
        boolean menuOpen = PreloaderInput.isShowingMenu();
        InbuiltOverlayManager manager = InbuiltOverlayManager.getInstance();
        boolean modMenu = manager != null && manager.isModMenuShowing();
        return hudOpen && !pauseOpen && !menuOpen && !modMenu;
    }

    private void poll() {
        String raw = null;
        try {
            if (moduleEnabled && gameplayVisible() && EspMod.isInitialized()) {
                raw = EspMod.getTargetEsp();
            }
        } catch (Throwable ignored) {
            raw = null;
        }
        if (raw == null || raw.isEmpty()) {
            if (ageMs >= 0) ageMs = -1;
            return;
        }
        try {
            String[] parts = raw.split("\t");
            if (parts.length >= 5) {
                x = Float.parseFloat(parts[0]);
                y = Float.parseFloat(parts[1]);
                w = Float.parseFloat(parts[2]);
                h = Float.parseFloat(parts[3]);
                ageMs = Long.parseLong(parts[4]);
                if (parts.length >= 6) {
                    yawDeg = Float.parseFloat(parts[5]);
                }
            }
        } catch (NumberFormatException ignored) {
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!moduleEnabled || ageMs < 0 || w <= 0 || h <= 0) return;

        float appear = Math.min(1f, ageMs / 110f);
        float fadeStart = Math.max(0L, holdMs - FADE_MS);
        float alpha = ageMs <= fadeStart
                ? 1f
                : Math.max(0f, 1f - (ageMs - fadeStart) / (float) FADE_MS);
        if (alpha <= 0.01f) return;

        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float scale = 0.82f + 0.18f * appear;
        float halfW = w * 0.5f * scale;
        float halfH = h * 0.5f * scale;
        rect.set(cx - halfW, cy - halfH, cx + halfW, cy + halfH);

        int color = Color.argb((int) (255 * alpha), Color.red(accent),
                Color.green(accent), Color.blue(accent));
        int faint = Color.argb((int) (70 * alpha), Color.red(accent),
                Color.green(accent), Color.blue(accent));
        strokePaint.setColor(color);
        strokePaint.setStrokeWidth(2f * density);
        glowPaint.setColor(faint);
        glowPaint.setStrokeWidth(7f * density);
        fillPaint.setShader(null);
        fillPaint.setColor(Color.argb((int) (40 * alpha), Color.red(accent),
                Color.green(accent), Color.blue(accent)));

        switch (style) {
            case STYLE_DIAMOND:
                drawDiamond(canvas);
                break;
            case STYLE_SPHERE:
                drawSphere(canvas, alpha);
                break;
            case STYLE_CIRCLE:
            default:
                drawCircle(canvas, alpha);
                break;
        }
    }

    private void drawCircle(Canvas canvas, float alpha) {
        float bob = (float) Math.sin(ageMs / 240.0) * 7f * density;
        float cx = rect.centerX();
        float cy = rect.centerY() + bob;
        float radius = Math.min(rect.width(), rect.height()) * 0.56f;
        shape.set(cx - radius, cy - radius, cx + radius, cy + radius);
        canvas.drawOval(shape, glowPaint);
        canvas.drawOval(shape, strokePaint);

        float spin = (ageMs % 1200L) / 1200f * 360f;
        float inset = radius * 0.82f;
        shape.set(cx - inset, cy - inset, cx + inset, cy + inset);
        strokePaint.setStrokeWidth(3f * density);
        path.reset();
        path.addArc(shape, spin, 68f);
        canvas.drawPath(path, strokePaint);
        path.reset();
        path.addArc(shape, spin + 180f, 68f);
        canvas.drawPath(path, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
    }

    private void drawDiamond(Canvas canvas) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float rx = rect.width() * 0.56f;
        float ry = rect.height() * 0.56f;
        canvas.save();
        canvas.rotate(-yawDeg, cx, cy);
        path.reset();
        path.moveTo(cx, cy - ry);
        path.lineTo(cx + rx, cy);
        path.lineTo(cx, cy + ry);
        path.lineTo(cx - rx, cy);
        path.close();
        canvas.drawPath(path, fillPaint);
        canvas.drawPath(path, glowPaint);
        canvas.drawPath(path, strokePaint);
        float small = 0.5f;
        path.reset();
        path.moveTo(cx, cy - ry * small);
        path.lineTo(cx + rx * small, cy);
        path.lineTo(cx, cy + ry * small);
        path.lineTo(cx - rx * small, cy);
        path.close();
        strokePaint.setStrokeWidth(3f * density);
        canvas.drawPath(path, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
        canvas.restore();
    }

    private void drawSphere(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float radius = Math.max(4f, Math.min(rect.width(), rect.height()) * 0.5f);
        RadialGradient gradient = new RadialGradient(
                cx - radius * 0.28f, cy - radius * 0.28f, radius * 1.2f,
                new int[]{
                        Color.argb((int) (110 * alpha), 255, 255, 255),
                        Color.argb((int) (70 * alpha), Color.red(accent),
                                Color.green(accent), Color.blue(accent)),
                        Color.argb(0, Color.red(accent), Color.green(accent),
                                Color.blue(accent))
                },
                new float[]{0f, 0.55f, 1f}, Shader.TileMode.CLAMP);
        fillPaint.setShader(gradient);
        canvas.drawCircle(cx, cy, radius, fillPaint);
        fillPaint.setShader(null);
        canvas.drawCircle(cx, cy, radius, strokePaint);
    }
}
