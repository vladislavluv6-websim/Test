package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.nativemod.EspMod;
import org.levimc.launcher.preloader.PreloaderInput;

import java.util.Arrays;

/**
 * Java-side ESP box models. Native still computes the projected screen
 * rectangles; this overlay renders the selected 2D model (box, corners, glow
 * or filled) and lets native draw only the original 3D variant.
 */
public class EspBoxesOverlay extends View {
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pipPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private float density = 1f;
    private boolean attached;
    private boolean showing;
    private boolean moduleEnabled = true;
    private int style = 0;
    private String[] boxes = new String[0];
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

    public EspBoxesOverlay(Activity activity) {
        super(activity);
        setWillNotDraw(false);
        setBackgroundColor(Color.TRANSPARENT);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        setElevation(94f);
        density = activity.getResources().getDisplayMetrics().density;
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.6f * density);
        glowPaint.setStyle(Paint.Style.STROKE);
        glowPaint.setStrokeWidth(6f * density);
        pipPaint.setStyle(Paint.Style.FILL);
        applySettings();
    }

    private void applySettings() {
        style = InbuiltModManager.getInstance(getContext()).getEspBoxStyle();
        EspMod.setEspBoxStyle(style);
    }

    public void applyConfigurationChanges() {
        applySettings();
        invalidate();
    }

    public void show() {
        if (showing) return;
        showing = true;
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
                    root.addView(EspBoxesOverlay.this, params);
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
        boxes = new String[0];
        try {
            if (getParent() instanceof ViewGroup) {
                ((ViewGroup) getParent()).removeView(this);
            }
        } catch (Exception ignored) {
        }
    }

    public void setModuleEnabled(boolean enabled) {
        moduleEnabled = enabled;
        if (!enabled) {
            boxes = new String[0];
            invalidate();
        }
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        attached = true;
        applySettings();
        EspMod.initializeAsync(ok -> {
            if (!attached || !ok) return;
            EspMod.setEspBoxStyle(style);
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
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateSurfaceSize(w, h);
    }

    private void updateSurfaceSize(int w, int h) {
        if (w > 0 && h > 0) EspMod.setNameTagScreenSize(w, h);
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
        String[] next = new String[0];
        if (moduleEnabled && gameplayVisible() && EspMod.isInitialized()) {
            String[] nativeBoxes = EspMod.getEspBoxes();
            if (nativeBoxes != null) next = nativeBoxes;
        }
        if (!Arrays.equals(boxes, next)) {
            boxes = next;
            invalidate();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!moduleEnabled || style <= 0 || boxes.length == 0) return;
        for (String entry : boxes) {
            if (entry == null) continue;
            String[] parts = entry.split("\\t");
            if (parts.length < 5) continue;
            try {
                float x = Float.parseFloat(parts[0]);
                float y = Float.parseFloat(parts[1]);
                float w = Float.parseFloat(parts[2]);
                float h = Float.parseFloat(parts[3]);
                int color = (int) Long.parseLong(parts[4].trim());
                int armorPieces = 0;
                if (parts.length >= 6) {
                    try {
                        armorPieces = Integer.parseInt(parts[5].trim());
                    } catch (NumberFormatException ignored) {
                    }
                }
                if (style > 0) drawBox(canvas, x, y, w, h, color);
                if (armorPieces > 0) {
                    drawArmorPips(canvas, x, y, w, armorPieces);
                }
            } catch (NumberFormatException ignored) {
            }
        }
    }

    private void drawArmorPips(Canvas canvas, float x, float y, float w,
                               int armorPieces) {
        float pipW = 4f * density;
        float pipH = 6f * density;
        float gap = 1.5f * density;
        float total = 4 * pipW + 3 * gap;
        float left = x + (w - total) * 0.5f;
        float top = y - pipH - 4f * density;
        for (int i = 0; i < 4; i++) {
            pipPaint.setColor(i < armorPieces ? 0xFF58A6FF : 0xFF303643);
            float px = left + i * (pipW + gap);
            canvas.drawRoundRect(px, top, px + pipW, top + pipH,
                    density, density, pipPaint);
        }
    }

    private void drawBox(Canvas canvas, float x, float y, float w, float h, int color) {
        rect.set(x, y, x + w, y + h);
        switch (style) {
            case 1:
                paint.setColor(color);
                paint.setStrokeWidth(1.6f * density);
                canvas.drawRoundRect(rect, 1.5f * density, 1.5f * density, paint);
                break;
            case 2: {
                float arm = Math.min(Math.min(w, h) * 0.24f, 18f * density);
                paint.setColor(color);
                paint.setStrokeWidth(1.8f * density);
                canvas.drawLine(rect.left, rect.top, rect.left + arm, rect.top, paint);
                canvas.drawLine(rect.left, rect.top, rect.left, rect.top + arm, paint);
                canvas.drawLine(rect.right, rect.top, rect.right - arm, rect.top, paint);
                canvas.drawLine(rect.right, rect.top, rect.right, rect.top + arm, paint);
                canvas.drawLine(rect.left, rect.bottom, rect.left + arm, rect.bottom, paint);
                canvas.drawLine(rect.left, rect.bottom, rect.left, rect.bottom - arm, paint);
                canvas.drawLine(rect.right, rect.bottom, rect.right - arm, rect.bottom, paint);
                canvas.drawLine(rect.right, rect.bottom, rect.right, rect.bottom - arm, paint);
                break;
            }
            case 3:
                glowPaint.setColor((color & 0x00FFFFFF) | 0x55000000);
                glowPaint.setStrokeWidth(6f * density);
                canvas.drawRoundRect(rect, 2f * density, 2f * density, glowPaint);
                paint.setColor(color);
                paint.setStrokeWidth(1.4f * density);
                canvas.drawRoundRect(rect, 2f * density, 2f * density, paint);
                break;
            case 4:
                paint.setStyle(Paint.Style.FILL);
                paint.setColor((color & 0x00FFFFFF) | 0x33000000);
                canvas.drawRoundRect(rect, 1.5f * density, 1.5f * density, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(color);
                paint.setStrokeWidth(1.5f * density);
                canvas.drawRoundRect(rect, 1.5f * density, 1.5f * density, paint);
                break;
            default:
                break;
        }
    }
}
