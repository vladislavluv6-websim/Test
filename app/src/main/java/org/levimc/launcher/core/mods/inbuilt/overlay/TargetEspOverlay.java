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
    public static final int STYLE_GHOST = 0;
    public static final int STYLE_SQUARE = 1;
    public static final int STYLE_CIRCLE = 2;
    public static final int STYLE_CRYSTAL = 3;

    private static final long FADE_MS = 180L;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();
    private float density = 1f;
    private boolean attached;
    private boolean showing;
    private boolean moduleEnabled = true;
    private int style = STYLE_GHOST;
    private int accent = 0xFF4E83FF;
    private long holdMs = 650L;
    private float x, y, w, h;
    private long ageMs = -1;
    private boolean framePosted;

    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            framePosted = false;
            if (!attached) return;
            poll();
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
        if (!enabled) {
            ageMs = -1;
            invalidate();
        }
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
            case STYLE_SQUARE:
                drawSquare(canvas, alpha);
                break;
            case STYLE_CIRCLE:
                drawCircle(canvas, alpha);
                break;
            case STYLE_CRYSTAL:
                drawCrystal(canvas, alpha);
                break;
            case STYLE_GHOST:
            default:
                drawGhost(canvas, alpha);
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

    private static void rectIns(RectF r, float inset) {
        r.inset(inset, inset);
    }
}
