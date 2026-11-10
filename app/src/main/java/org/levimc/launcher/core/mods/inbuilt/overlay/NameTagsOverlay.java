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
    public static final int STYLE_PLATE = 0;
    public static final int STYLE_GLASS = 1;
    public static final int STYLE_NEON = 2;
    public static final int STYLE_MINIMAL = 3;
    public static final int STYLE_BRACKET = 4;

    private static final String TAG = "NameTagsOverlay";

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint backdropPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint accentPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint distancePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint statPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final android.graphics.Path heartPath = new android.graphics.Path();
    private float density = 1f;
    private boolean attached;
    private boolean showing;
    private boolean moduleEnabled = true;
    private String[] labels = new String[0];
    private boolean frameCallbackPosted;
    private int style = STYLE_PLATE;
    private int accent = 0xFF4E83FF;

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
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        textPaint.setShadowLayer(3f, 0f, 1f, 0xCC000000);
        density = activity.getResources().getDisplayMetrics().density;
        textPaint.setTextSize(12.5f * density);
        distancePaint.setColor(0xFF9296A2);
        distancePaint.setTextAlign(Paint.Align.CENTER);
        distancePaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        distancePaint.setTextSize(9f * density);
        statPaint.setColor(0xFFD9DBE2);
        statPaint.setTextAlign(Paint.Align.LEFT);
        statPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        statPaint.setTextSize(8.5f * density);
        backdropPaint.setColor(0xE012151E);
        accentPaint.setStyle(Paint.Style.STROKE);
        applySettings();
    }

    private void applySettings() {
        InbuiltModManager manager = InbuiltModManager.getInstance(getContext());
        style = manager.getNameTagStyle();
        accent = manager.getNameTagColor();
        accentPaint.setColor(accent);
        textPaint.setColor(style == STYLE_NEON ? accent : Color.WHITE);
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
        moduleEnabled = manager.isNameTagsEnabled();
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
            String[] parts = entry.split("\t", -1);
            if (parts.length < 3) continue;
            try {
                float x = Float.parseFloat(parts[0]);
                float y = Float.parseFloat(parts[1]);
                float distance = Float.parseFloat(parts[2]);
                int armorPieces = 0;
                float health = 20f;
                String name;
                if (parts.length >= 6) {
                    armorPieces = Math.max(0,
                            Math.min(4, Integer.parseInt(parts[3].trim())));
                    health = Float.parseFloat(parts[4]);
                    name = parts[5];
                } else if (parts.length == 5) {
                    armorPieces = "1".equals(parts[3].trim()) ? 4 : 0;
                    name = parts[4];
                } else {
                    name = parts.length >= 4 ? parts[3] : "";
                }
                if (name.isEmpty()) continue;
                drawLabel(canvas, x, y, name, distance, armorPieces, health);
            } catch (NumberFormatException ignored) {
            }
        }
    }

    private void drawLabel(Canvas canvas, float x, float y, String name,
                           float distance, int armorPieces, float health) {
        String hpText = String.format(java.util.Locale.US, "%.1f", health);
        String distanceText = distance > 0f
                ? String.format(java.util.Locale.US, "%.0f m", distance)
                : "";
        float nameSize = 13f * density;
        textPaint.setTextSize(nameSize);
        float nameWidth = textPaint.measureText(name);

        float heartSize = 8f * density;
        float hpWidth = statPaint.measureText(hpText);
        float pipWidth = 4f * dp(3.6f);
        float armorWidth = 4 * dp(3.6f) + 3 * dp(1.5f);
        float distanceWidth = distanceText.isEmpty()
                ? 0f : distancePaint.measureText(distanceText);
        float gap = dp(5f);
        float statWidth = heartSize + dp(2f) + hpWidth + gap
                + armorWidth + (distanceText.isEmpty() ? 0f : gap + distanceWidth);
        float textWidth = Math.max(nameWidth, statWidth);

        float padX = 9f * density;
        float padY = 5f * density;
        float left = x - textWidth * 0.5f - padX;
        float right = x + textWidth * 0.5f + padX;
        float nameHeight = textPaint.getTextSize();
        float statHeight = statPaint.getTextSize();
        float top = y - nameHeight - statHeight - padY * 2.1f;
        float bottom = y + padY * 0.5f;
        float radius = 5f * density;
        float nameBaseline = top + padY + nameHeight * 0.82f;
        float statBaseline = bottom - padY * 0.95f;

        switch (style) {
            case STYLE_GLASS:
                backdropPaint.setColor(0x8A0D1018);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius, backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1f * density);
                accentPaint.setColor(0x66FFFFFF);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius, accentPaint);
                break;
            case STYLE_NEON:
                backdropPaint.setColor(0x660A0C12);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius, backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1.5f * density);
                accentPaint.setColor(accent);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius, accentPaint);
                break;
            case STYLE_MINIMAL:
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1.5f * density);
                accentPaint.setColor(accent);
                canvas.drawLine(left, bottom, right, bottom, accentPaint);
                break;
            case STYLE_BRACKET: {
                float arm = Math.min(9f * density, (bottom - top) * 0.45f);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1.8f * density);
                accentPaint.setColor(accent);
                canvas.drawLine(left, top, left + arm, top, accentPaint);
                canvas.drawLine(left, top, left, top + arm, accentPaint);
                canvas.drawLine(right, top, right - arm, top, accentPaint);
                canvas.drawLine(right, top, right, top + arm, accentPaint);
                canvas.drawLine(left, bottom, left + arm, bottom, accentPaint);
                canvas.drawLine(left, bottom, left, bottom - arm, accentPaint);
                canvas.drawLine(right, bottom, right - arm, bottom, accentPaint);
                canvas.drawLine(right, bottom, right, bottom - arm, accentPaint);
                break;
            }
            case STYLE_PLATE:
            default:
                backdropPaint.setColor(0xF00B0E15);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius, backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1f * density);
                accentPaint.setColor(0xFF20232E);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius, accentPaint);
                break;
        }

        textPaint.setColor(style == STYLE_NEON ? accent : Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(name, x, nameBaseline, textPaint);

        float statLeft = x - statWidth * 0.5f;
        float iconTop = statBaseline - statHeight * 0.92f;
        drawHeart(canvas, statLeft, iconTop, heartSize, 0xFF3DDC84);
        statPaint.setColor(style == STYLE_NEON ? accent : 0xFFD9DBE2);
        statPaint.setTextAlign(Paint.Align.LEFT);
        float cursor = statLeft + heartSize + dp(2f);
        canvas.drawText(hpText, cursor, statBaseline, statPaint);
        cursor += hpWidth + gap;

        for (int i = 0; i < 4; i++) {
            float pipW = dp(3.6f);
            float pipH = dp(5f);
            float pipTop = statBaseline - pipH;
            if (i < armorPieces) {
                accentPaint.setStyle(Paint.Style.FILL);
                accentPaint.setColor(0xFF58A6FF);
                canvas.drawRoundRect(cursor, pipTop, cursor + pipW,
                        pipTop + pipH, dp(1f), dp(1f), accentPaint);
            } else {
                accentPaint.setStyle(Paint.Style.FILL);
                accentPaint.setColor(0xFF303643);
                canvas.drawRoundRect(cursor, pipTop, cursor + pipW,
                        pipTop + pipH, dp(1f), dp(1f), accentPaint);
            }
            cursor += pipW + dp(1.5f);
        }

        if (!distanceText.isEmpty()) {
            cursor += gap - dp(1.5f);
            distancePaint.setColor(style == STYLE_NEON ? accent : 0xFF9296A2);
            distancePaint.setTextAlign(Paint.Align.LEFT);
            canvas.drawText(distanceText, cursor, statBaseline, distancePaint);
        }
    }

    private float dp(float value) {
        return value * density;
    }

    private void drawHeart(Canvas canvas, float left, float top, float size, int color) {
        float right = left + size;
        float centerX = left + size * 0.5f;
        float bottom = top + size;
        heartPath.reset();
        heartPath.moveTo(centerX, bottom);
        heartPath.cubicTo(left - size * 0.08f, top + size * 0.55f,
                left + size * 0.06f, top - size * 0.05f,
                centerX, top + size * 0.27f);
        heartPath.cubicTo(right - size * 0.06f, top - size * 0.05f,
                right + size * 0.08f, top + size * 0.55f,
                centerX, bottom);
        heartPath.close();
        accentPaint.setStyle(Paint.Style.FILL);
        accentPaint.setColor(color);
        canvas.drawPath(heartPath, accentPaint);
    }
}
