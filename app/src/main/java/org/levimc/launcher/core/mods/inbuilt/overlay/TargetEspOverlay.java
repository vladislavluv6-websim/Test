package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
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
 * Target ESP: рисуется только после удара TriggerBot по цели.
 * Native отдаёт экранный прямоугольник цели, Java рисует выбранную модель
 * (призрак, квадрат, круг, кристалл) с плавным появлением и затуханием.
 */
public class TargetEspOverlay extends View {
    public static final int STYLE_CRYSTAL = 0;
    public static final int STYLE_PRISM = 1;
    public static final int STYLE_RUNE = 2;
    public static final int STYLE_NOVA = 3;
    public static final int STYLE_PULSE = 4;
    public static final int STYLE_HEX = 5;
    public static final int STYLE_ORBIT = 6;
    public static final int STYLE_LOCK = 7;

    private static final long FADE_MS = 180L;
    private static final long POLL_INTERVAL_NANOS = 33_000_000L;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();
    private float density = 1f;
    private boolean attached;
    private volatile boolean showing;
    private volatile boolean moduleEnabled = true;
    private int style = STYLE_CRYSTAL;
    private int accent = 0xFF4E83FF;
    private long holdMs = 650L;
    private float x, y, w, h;
    private long ageMs = -1;
    private boolean framePosted;
    private long lastPollNanos;

    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            framePosted = false;
            if (!attached) return;
            if (frameTimeNanos - lastPollNanos >= POLL_INTERVAL_NANOS) {
                lastPollNanos = frameTimeNanos;
                poll();
            }
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
        strokePaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        glowPaint.setStyle(Paint.Style.STROKE);
        glowPaint.setStrokeWidth(7f * density);
        fillPaint.setStyle(Paint.Style.FILL);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        textPaint.setTextSize(10f * density);
        textPaint.setShadowLayer(3f, 0f, 1f, 0xCC000000);
        applySettings();
    }

    private void applySettings() {
        InbuiltModManager manager = InbuiltModManager.getInstance(getContext());
        style = manager.getTargetEspStyle();
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
        if (moduleEnabled && gameplayVisible() && EspMod.isInitialized()) {
            raw = EspMod.getTargetEsp();
        }
        boolean changed = false;
        if (raw == null || raw.isEmpty()) {
            if (ageMs >= 0) {
                ageMs = -1;
                changed = true;
            }
        } else {
            try {
                String[] parts = raw.split("\\t");
                if (parts.length >= 5) {
                    x = Float.parseFloat(parts[0]);
                    y = Float.parseFloat(parts[1]);
                    w = Float.parseFloat(parts[2]);
                    h = Float.parseFloat(parts[3]);
                    ageMs = Long.parseLong(parts[4]);
                    changed = true;
                }
            } catch (NumberFormatException ignored) {
            }
        }
        if (changed) invalidate();
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
        glowPaint.setColor(faint);
        fillPaint.setColor(Color.argb((int) (26 * alpha), Color.red(accent),
                Color.green(accent), Color.blue(accent)));

        switch (style) {
            case STYLE_PRISM:
                drawPrism(canvas, alpha);
                break;
            case STYLE_RUNE:
                drawRune(canvas, alpha);
                break;
            case STYLE_NOVA:
                drawNova(canvas, alpha);
                break;
            case STYLE_PULSE:
                drawPulse(canvas, alpha);
                break;
            case STYLE_HEX:
                drawHex(canvas, alpha);
                break;
            case STYLE_ORBIT:
                drawOrbit(canvas, alpha);
                break;
            case STYLE_LOCK:
                drawLock(canvas, alpha);
                break;
            case STYLE_CRYSTAL:
            default:
                drawCrystal(canvas, alpha);
                break;
        }
    }

    private void drawSquare(Canvas canvas, float alpha) {
        float radius = 6f * density;
        canvas.drawRoundRect(rect, radius, radius, fillPaint);
        canvas.drawRoundRect(rect, radius, radius, glowPaint);
        canvas.drawRoundRect(rect, radius, radius, strokePaint);
        float arm = Math.min(rect.width(), rect.height()) * 0.22f;
        strokePaint.setStrokeWidth(3f * density);
        canvas.drawLine(rect.left, rect.top, rect.left + arm, rect.top, strokePaint);
        canvas.drawLine(rect.left, rect.top, rect.left, rect.top + arm, strokePaint);
        canvas.drawLine(rect.right, rect.top, rect.right - arm, rect.top, strokePaint);
        canvas.drawLine(rect.right, rect.top, rect.right, rect.top + arm, strokePaint);
        canvas.drawLine(rect.left, rect.bottom, rect.left + arm, rect.bottom, strokePaint);
        canvas.drawLine(rect.left, rect.bottom, rect.left, rect.bottom - arm, strokePaint);
        canvas.drawLine(rect.right, rect.bottom, rect.right - arm, rect.bottom, strokePaint);
        canvas.drawLine(rect.right, rect.bottom, rect.right, rect.bottom - arm, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
    }

    private void drawCircle(Canvas canvas, float alpha) {
        float rx = rect.width() * 0.5f;
        float ry = rect.height() * 0.5f;
        canvas.drawOval(rect, fillPaint);
        canvas.drawOval(rect, glowPaint);
        canvas.drawOval(rect, strokePaint);
        float rotation = (ageMs % 1100L) / 1100f * 360f;
        rectIns(rect, 5f * density);
        path.reset();
        path.addArc(rect, rotation, 70f);
        strokePaint.setStrokeWidth(3f * density);
        canvas.drawPath(path, strokePaint);
        path.reset();
        path.addArc(rect, rotation + 180f, 70f);
        canvas.drawPath(path, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
    }

    private void drawGhost(Canvas canvas, float alpha) {
        float pulse = (float) (0.5 + 0.5 * Math.sin(ageMs / 130.0));
        for (int i = 3; i >= 0; i--) {
            float trail = i * 7f * density;
            float sway = (float) Math.sin((ageMs + i * 90) / 220.0) * 4f * density;
            rectIns(rect, -trail);
            glowPaint.setAlpha((int) ((34 + 16 * i) * alpha));
            canvas.drawRoundRect(rect, 10f * density, 10f * density, glowPaint);
            canvas.drawRoundRect(rect, 10f * density, 10f * density, strokePaint);
            rectIns(rect, trail);
            canvas.drawCircle(rect.left + sway, rect.top + rect.height() * (0.25f + 0.2f * i),
                    2.5f * density, fillPaint);
        }
        glowPaint.setAlpha((int) (70 * alpha));
        rectIns(rect, 2f * density);
        canvas.drawRoundRect(rect, 8f * density, 8f * density, fillPaint);
        float arm = Math.min(rect.width(), rect.height()) * 0.18f;
        strokePaint.setStrokeWidth((2f + pulse) * density);
        canvas.drawLine(rect.left, rect.centerY(), rect.left + arm, rect.centerY(), strokePaint);
        canvas.drawLine(rect.right, rect.centerY(), rect.right - arm, rect.centerY(), strokePaint);
        strokePaint.setStrokeWidth(2f * density);
    }

    private void drawCrystal(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float rotation = (ageMs % 1600L) / 1600f * 360f;
        float radiusX = rect.width() * 0.52f;
        float radiusY = rect.height() * 0.52f;
        canvas.save();
        canvas.rotate(rotation, cx, cy);
        path.reset();
        path.moveTo(cx, cy - radiusY);
        path.lineTo(cx + radiusX, cy);
        path.lineTo(cx, cy + radiusY);
        path.lineTo(cx - radiusX, cy);
        path.close();
        canvas.drawPath(path, fillPaint);
        canvas.drawPath(path, glowPaint);
        canvas.drawPath(path, strokePaint);
        canvas.restore();
        canvas.save();
        canvas.rotate(-rotation * 0.6f, cx, cy);
        path.reset();
        float small = 0.55f;
        path.moveTo(cx, cy - radiusY * small);
        path.lineTo(cx + radiusX * small, cy);
        path.lineTo(cx, cy + radiusY * small);
        path.lineTo(cx - radiusX * small, cy);
        path.close();
        strokePaint.setStrokeWidth(3f * density);
        canvas.drawPath(path, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
        canvas.restore();
    }


    private void drawPrism(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float rotation = (ageMs % 2200L) / 2200f * 360f;
        float rx = rect.width() * 0.54f;
        float ry = rect.height() * 0.54f;
        canvas.save();
        canvas.rotate(rotation, cx, cy);
        for (int i = 0; i < 3; i++) {
            canvas.save();
            canvas.rotate(i * 60f, cx, cy);
            path.reset();
            path.moveTo(cx, cy - ry);
            path.lineTo(cx + rx, cy);
            path.lineTo(cx, cy + ry);
            path.lineTo(cx - rx, cy);
            path.close();
            canvas.drawPath(path, i == 0 ? fillPaint : glowPaint);
            canvas.drawPath(path, strokePaint);
            canvas.restore();
        }
        canvas.restore();
        path.reset();
        path.addCircle(cx, cy, Math.min(rx, ry) * 0.16f, Path.Direction.CW);
        canvas.drawPath(path, strokePaint);
    }

    private void drawRune(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float radius = Math.min(rect.width(), rect.height()) * 0.56f;
        float rotation = (ageMs % 2600L) / 2600f * 360f;
        path.reset();
        for (int i = 0; i <= 6; i++) {
            double a = Math.toRadians(rotation + i * 60);
            float px = cx + (float) Math.cos(a) * radius;
            float py = cy + (float) Math.sin(a) * radius;
            if (i == 0) path.moveTo(px, py); else path.lineTo(px, py);
        }
        path.close();
        canvas.drawPath(path, fillPaint);
        canvas.drawPath(path, glowPaint);
        strokePaint.setStrokeWidth(3f * density);
        canvas.drawPath(path, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
        for (int i = 0; i < 6; i++) {
            double a = Math.toRadians(rotation + i * 60);
            float px = cx + (float) Math.cos(a) * radius * 0.78f;
            float py = cy + (float) Math.sin(a) * radius * 0.78f;
            canvas.drawCircle(px, py, 2.5f * density, strokePaint);
        }
    }

    private void drawNova(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float radius = Math.min(rect.width(), rect.height()) * 0.58f;
        float rotation = (ageMs % 1800L) / 1800f * 360f;
        canvas.save();
        canvas.rotate(rotation, cx, cy);
        for (int i = 0; i < 8; i++) {
            float len = (i % 2 == 0) ? radius : radius * 0.48f;
            path.reset();
            path.moveTo(cx, cy - len);
            path.lineTo(cx + radius * 0.12f, cy);
            path.lineTo(cx, cy + len);
            path.lineTo(cx - radius * 0.12f, cy);
            path.close();
            canvas.drawPath(path, i % 2 == 0 ? glowPaint : strokePaint);
            canvas.rotate(45f, cx, cy);
        }
        canvas.restore();
        fillPaint.setAlpha((int) (42 * alpha));
        canvas.drawCircle(cx, cy, radius * 0.22f, fillPaint);
        canvas.drawCircle(cx, cy, radius * 0.22f, strokePaint);
    }

    private void drawPulse(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float base = Math.min(rect.width(), rect.height()) * 0.58f;
        for (int i = 0; i < 3; i++) {
            float phase = ((ageMs + i * 320L) % 960L) / 960f;
            float radius = base * (0.45f + 0.55f * phase);
            int a = (int) ((1f - phase) * 110 * alpha);
            glowPaint.setAlpha(a);
            canvas.drawCircle(cx, cy, radius, glowPaint);
        }
        glowPaint.setAlpha((int) (70 * alpha));
        canvas.drawCircle(cx, cy, base * 0.42f, strokePaint);
        canvas.drawCircle(cx, cy, base * 0.18f, fillPaint);
        canvas.drawCircle(cx, cy, base * 0.18f, strokePaint);
    }


    private void drawHex(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float radius = Math.min(rect.width(), rect.height()) * 0.58f;
        float rotation = (ageMs % 2400L) / 2400f * 360f;
        for (int ring = 0; ring < 2; ring++) {
            float r = radius * (ring == 0 ? 1f : 0.68f);
            path.reset();
            for (int i = 0; i <= 6; i++) {
                double a = Math.toRadians(rotation + i * 60 + ring * 30);
                float px = cx + (float) Math.cos(a) * r;
                float py = cy + (float) Math.sin(a) * r;
                if (i == 0) path.moveTo(px, py); else path.lineTo(px, py);
            }
            path.close();
            canvas.drawPath(path, ring == 0 ? glowPaint : fillPaint);
            strokePaint.setStrokeWidth((ring == 0 ? 2.5f : 2f) * density);
            canvas.drawPath(path, strokePaint);
        }
        strokePaint.setStrokeWidth(2f * density);
        for (int i = 0; i < 6; i++) {
            double a = Math.toRadians(rotation + i * 60);
            canvas.drawLine(cx, cy,
                    cx + (float) Math.cos(a) * radius,
                    cy + (float) Math.sin(a) * radius, glowPaint);
        }
    }

    private void drawOrbit(Canvas canvas, float alpha) {
        float cx = rect.centerX();
        float cy = rect.centerY();
        float rx = rect.width() * 0.56f;
        float ry = rect.height() * 0.30f;
        float rotation = (ageMs % 2000L) / 2000f * 360f;
        canvas.save();
        canvas.rotate(rotation, cx, cy);
        canvas.drawOval(cx - rx, cy - ry, cx + rx, cy + ry, glowPaint);
        path.reset();
        path.addOval(cx - rx, cy - ry, cx + rx, cy + ry, Path.Direction.CW);
        canvas.drawPath(path, strokePaint);
        canvas.restore();
        canvas.save();
        canvas.rotate(-rotation * 0.8f + 90f, cx, cy);
        canvas.drawOval(cx - rx, cy - ry, cx + rx, cy + ry, glowPaint);
        canvas.restore();
        for (int i = 0; i < 3; i++) {
            double a = Math.toRadians(rotation + i * 120);
            float px = cx + (float) Math.cos(a) * rx * 0.95f;
            float py = cy + (float) Math.sin(a) * ry * 0.95f;
            canvas.save();
            canvas.rotate(rotation, cx, cy);
            canvas.drawCircle(px, cy, 3f * density, fillPaint);
            canvas.restore();
        }
        fillPaint.setAlpha((int) (36 * alpha));
        canvas.drawCircle(cx, cy, Math.min(rx, ry) * 0.45f, fillPaint);
    }

    private void drawLock(Canvas canvas, float alpha) {
        float inset = 3f * density;
        rectIns(rect, inset);
        float arm = Math.min(rect.width(), rect.height()) * 0.30f;
        strokePaint.setStrokeWidth(2.5f * density);
        canvas.drawLine(rect.left, rect.top, rect.left + arm, rect.top, strokePaint);
        canvas.drawLine(rect.left, rect.top, rect.left, rect.top + arm, strokePaint);
        canvas.drawLine(rect.right, rect.top, rect.right - arm, rect.top, strokePaint);
        canvas.drawLine(rect.right, rect.top, rect.right, rect.top + arm, strokePaint);
        canvas.drawLine(rect.left, rect.bottom, rect.left + arm, rect.bottom, strokePaint);
        canvas.drawLine(rect.left, rect.bottom, rect.left, rect.bottom - arm, strokePaint);
        canvas.drawLine(rect.right, rect.bottom, rect.right - arm, rect.bottom, strokePaint);
        canvas.drawLine(rect.right, rect.bottom, rect.right, rect.bottom - arm, strokePaint);
        float cx = rect.centerX();
        float cy = rect.centerY();
        float r = Math.min(rect.width(), rect.height()) * 0.16f;
        float pulse = (float) (0.5 + 0.5 * Math.sin(ageMs / 140.0));
        path.reset();
        path.moveTo(cx, cy - r);
        path.lineTo(cx + r, cy);
        path.lineTo(cx, cy + r);
        path.lineTo(cx - r, cy);
        path.close();
        glowPaint.setAlpha((int) ((45 + 45 * pulse) * alpha));
        canvas.drawPath(path, glowPaint);
        canvas.drawPath(path, strokePaint);
        strokePaint.setStrokeWidth(2f * density);
        rectIns(rect, -inset);
    }

    private static void rectIns(RectF r, float inset) {
        r.inset(inset, inset);
    }
}
