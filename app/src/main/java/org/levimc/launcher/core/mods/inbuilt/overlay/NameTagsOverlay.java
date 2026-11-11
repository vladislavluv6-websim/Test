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
    public static final int STYLE_CAPSULE = 0;
    public static final int STYLE_HUD = 1;
    public static final int STYLE_GLOW = 2;
    public static final int STYLE_SPLIT = 3;
    public static final int STYLE_CYBER = 4;

    private static final String TAG = "NameTagsOverlay";

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint backdropPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint accentPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint distancePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint statPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final android.graphics.Path heartPath = new android.graphics.Path();
    private final android.graphics.Path panelPath = new android.graphics.Path();
    private final android.graphics.Path shieldPath = new android.graphics.Path();
    private float density = 1f;
    private boolean attached;
    private boolean showing;
    private boolean moduleEnabled = true;
    private String[] labels = new String[0];
    private boolean frameCallbackPosted;
    private int style = STYLE_CAPSULE;
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
        textPaint.setColor((style == STYLE_GLOW || style == STYLE_CYBER)
                ? accent : Color.WHITE);
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
        float shieldWidth = dp(4.4f);
        float shieldHeight = dp(5.6f);
        float shieldGap = dp(1.2f);
        float armorWidth = 4 * shieldWidth + 3 * shieldGap;
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
            case STYLE_HUD: {
                backdropPaint.setColor(0xF20B0E16);
                canvas.drawRoundRect(left, top, right, bottom, dp(3f), dp(3f),
                        backdropPaint);
                accentPaint.setStyle(Paint.Style.FILL);
                accentPaint.setColor(accent);
                canvas.drawRect(left + dp(3f), top,
                        left + (right - left) * 0.58f, top + dp(2f), accentPaint);
                accentPaint.setColor(0x552F3B52);
                canvas.drawRect(right - dp(11f), top + dp(2f), right,
                        top + dp(2.7f), accentPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1f * density);
                accentPaint.setColor(0xFF252B38);
                canvas.drawRoundRect(left, top, right, bottom, dp(3f), dp(3f),
                        accentPaint);
                break;
            }
            case STYLE_GLOW: {
                final int glow = (accent & 0x00FFFFFF) | 0x33000000;
                final int edge = (accent & 0x00FFFFFF) | 0xCC000000;
                backdropPaint.setColor(glow);
                canvas.drawRoundRect(left - dp(1.5f), top - dp(1.5f),
                        right + dp(1.5f), bottom + dp(1.5f),
                        radius + dp(1.5f), radius + dp(1.5f), backdropPaint);
                backdropPaint.setColor(0xCC090C13);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius,
                        backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1.2f * density);
                accentPaint.setColor(edge);
                canvas.drawRoundRect(left, top, right, bottom, radius, radius,
                        accentPaint);
                break;
            }
            case STYLE_SPLIT: {
                backdropPaint.setColor(0x52070A10);
                canvas.drawRoundRect(left, top, right, bottom, dp(2f), dp(2f),
                        backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1.2f * density);
                accentPaint.setColor(accent);
                canvas.drawLine(left, top, right, top, accentPaint);
                canvas.drawLine(left, bottom, right, bottom, accentPaint);
                accentPaint.setColor(0x66343C4D);
                accentPaint.setStrokeWidth(1f * density);
                float dividerY = top + padY + nameHeight + dp(1.2f);
                canvas.drawLine(left + padX * 0.25f, dividerY,
                        right - padX * 0.25f, dividerY, accentPaint);
                break;
            }
            case STYLE_CYBER: {
                float cut = dp(5f);
                panelPath.reset();
                panelPath.moveTo(left + cut, top);
                panelPath.lineTo(right, top);
                panelPath.lineTo(right, bottom - cut);
                panelPath.lineTo(right - cut, bottom);
                panelPath.lineTo(left, bottom);
                panelPath.lineTo(left, top + cut);
                panelPath.close();
                backdropPaint.setColor(0xE60A0E17);
                canvas.drawPath(panelPath, backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1.1f * density);
                accentPaint.setColor(accent);
                canvas.drawPath(panelPath, accentPaint);
                accentPaint.setStyle(Paint.Style.FILL);
                accentPaint.setColor((accent & 0x00FFFFFF) | 0x66000000);
                canvas.drawRect(left + cut, top + dp(3f),
                        left + cut + dp(9f), top + dp(3.8f), accentPaint);
                break;
            }
            case STYLE_CAPSULE:
            default: {
                float capsuleRadius = (bottom - top) * 0.5f;
                backdropPaint.setColor(0xEE080B12);
                canvas.drawRoundRect(left, top, right, bottom, capsuleRadius,
                        capsuleRadius, backdropPaint);
                accentPaint.setStyle(Paint.Style.STROKE);
                accentPaint.setStrokeWidth(1f * density);
                accentPaint.setColor((accent & 0x00FFFFFF) | 0x99000000);
                canvas.drawRoundRect(left, top, right, bottom, capsuleRadius,
                        capsuleRadius, accentPaint);
                accentPaint.setStyle(Paint.Style.FILL);
                accentPaint.setColor(accent);
                canvas.drawRoundRect(left + dp(2.6f), top + dp(3.2f),
                        left + dp(4.4f), bottom - dp(3.2f), dp(0.9f),
                        dp(0.9f), accentPaint);
                break;
            }
        }

        textPaint.setColor((style == STYLE_GLOW || style == STYLE_CYBER)
                ? accent : Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(name, x, nameBaseline, textPaint);

        float statLeft = x - statWidth * 0.5f;
        float iconTop = statBaseline - statHeight * 0.92f;
        drawHeart(canvas, statLeft, iconTop, heartSize, 0xFF3DDC84);
        statPaint.setColor((style == STYLE_GLOW || style == STYLE_CYBER)
                ? 0xFFEAF2FF : 0xFFD9DBE2);
        statPaint.setTextAlign(Paint.Align.LEFT);
        float cursor = statLeft + heartSize + dp(2f);
        canvas.drawText(hpText, cursor, statBaseline, statPaint);
        cursor += hpWidth + gap;

        for (int i = 0; i < 4; i++) {
            drawArmorShield(canvas, cursor, statBaseline, shieldWidth,
                    shieldHeight, i < armorPieces);
            cursor += shieldWidth + shieldGap;
        }

        if (!distanceText.isEmpty()) {
            cursor += gap - shieldGap;
            distancePaint.setColor((style == STYLE_GLOW || style == STYLE_CYBER)
                    ? 0xFFB9C7E6 : 0xFF9296A2);
            distancePaint.setTextAlign(Paint.Align.LEFT);
            canvas.drawText(distanceText, cursor, statBaseline, distancePaint);
        }
    }

    private float dp(float value) {
        return value * density;
    }

    private void drawArmorShield(Canvas canvas, float left, float baseline,
                                 float width, float height, boolean filled) {
        float top = baseline - height;
        shieldPath.reset();
        shieldPath.moveTo(left, top);
        shieldPath.lineTo(left + width, top);
        shieldPath.lineTo(left + width, top + height * 0.56f);
        shieldPath.lineTo(left + width * 0.5f, top + height);
        shieldPath.lineTo(left, top + height * 0.56f);
        shieldPath.close();

        if (filled) {
            accentPaint.setStyle(Paint.Style.FILL);
            accentPaint.setColor(0xFF4F8DFF);
            accentPaint.setShadowLayer(dp(2.2f), 0f, 0f, 0x884F8DFF);
            canvas.drawPath(shieldPath, accentPaint);
            accentPaint.clearShadowLayer();
            accentPaint.setStyle(Paint.Style.STROKE);
            accentPaint.setStrokeWidth(dp(0.7f));
            accentPaint.setColor(0xFFD7E7FF);
            canvas.drawPath(shieldPath, accentPaint);
        } else {
            accentPaint.setStyle(Paint.Style.FILL);
            accentPaint.setColor(0xFF252B36);
            canvas.drawPath(shieldPath, accentPaint);
            accentPaint.setStyle(Paint.Style.STROKE);
            accentPaint.setStrokeWidth(dp(0.7f));
            accentPaint.setColor(0xFF3B4352);
            canvas.drawPath(shieldPath, accentPaint);
        }
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
