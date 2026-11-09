package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
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

import java.util.Arrays;

/**
 * Прозрачный полноэкранный слой для ников игроков.
 *
 * <p>Экранные координаты и имена считает native на игровом потоке
 * (Esp::render), Java лишь рисует готовые подписи. Слой не перехватывает
 * касания и не мешает SurfaceView игры.</p>
 */
public class NameTagsOverlay extends View {
    private static final String TAG = "NameTagsOverlay";

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean attached;
    private boolean showing;
    private boolean moduleEnabled = true;
    private String[] labels = new String[0];
    private boolean frameCallbackPosted;

    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            frameCallbackPosted = false;
            if (!attached) return;
            pollLabels();
            postFrameCallback();
        }
    };

    public NameTagsOverlay(Activity activity) {
        super(activity);
        setWillNotDraw(false);
        setBackgroundColor(Color.TRANSPARENT);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        setElevation(90f);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        textPaint.setShadowLayer(3f, 0f, 1f, 0xCC000000);
        float density = activity.getResources().getDisplayMetrics().density;
        textPaint.setTextSize(13f * density);
    }

    public void show() {
        if (showing) return;
        showing = true;
        mainHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!showing) return;
                try {
                    ViewGroup root = ((Activity) getContext())
                            .findViewById(android.R.id.content);
                    if (root == null) return;
                    if (getParent() != null) return;
                    FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.MATCH_PARENT);
                    root.addView(NameTagsOverlay.this, params);
                } catch (Exception ignored) {
                }
            }
        }, 100);
    }

    public void hide() {
        showing = false;
        attached = false;
        Choreographer.getInstance().removeFrameCallback(frameCallback);
        frameCallbackPosted = false;
        EspMod.setNameTags(false);
        try {
            if (getParent() instanceof ViewGroup) {
                ((ViewGroup) getParent()).removeView(this);
            }
        } catch (Exception ignored) {
        }
    }

    /**
     * Синхронизирует состояние тумблера из настроек. Вызывается из
     * {@link InbuiltOverlayManager#tick()} (20 Гц), поэтому сравнение
     * обязательно.
     */
    public void setModuleEnabled(boolean enabled) {
        if (moduleEnabled == enabled) return;
        moduleEnabled = enabled;
        EspMod.setNameTags(enabled);
        if (!enabled) {
            labels = new String[0];
            invalidate();
        }
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        attached = true;
        InbuiltModManager manager = InbuiltModManager.getInstance(getContext());
        moduleEnabled = manager.isEspNameTags();
        EspMod.initializeAsync(ok -> {
            if (!attached || !ok) return;
            EspMod.setNameTags(moduleEnabled);
        });
        updateSurfaceSize(getWidth(), getHeight());
        postFrameCallback();
    }

    @Override
    protected void onDetachedFromWindow() {
        attached = false;
        frameCallbackPosted = false;
        Choreographer.getInstance().removeFrameCallback(frameCallback);
        super.onDetachedFromWindow();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateSurfaceSize(w, h);
    }

    private void updateSurfaceSize(int w, int h) {
        if (w > 0 && h > 0) {
            EspMod.setNameTagScreenSize(w, h);
        }
    }

    private void postFrameCallback() {
        if (frameCallbackPosted || !attached) return;
        frameCallbackPosted = true;
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

    private void pollLabels() {
        String[] next = new String[0];
        if (moduleEnabled && gameplayVisible() && EspMod.isInitialized()) {
            String[] nativeLabels = EspMod.getNameTags();
            if (nativeLabels != null) next = nativeLabels;
        }
        if (!Arrays.equals(labels, next)) {
            labels = next;
            invalidate();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!moduleEnabled || labels.length == 0) return;
        for (String entry : labels) {
            if (entry == null) continue;
            int first = entry.indexOf('\t');
            if (first < 0) continue;
            int second = entry.indexOf('\t', first + 1);
            if (second < 0) continue;
            try {
                float x = Float.parseFloat(entry.substring(0, first));
                float y = Float.parseFloat(entry.substring(first + 1, second));
                String name = entry.substring(second + 1);
                if (!name.isEmpty()) {
                    canvas.drawText(name, x, y, textPaint);
                }
            } catch (NumberFormatException ignored) {
            }
        }
    }
}
