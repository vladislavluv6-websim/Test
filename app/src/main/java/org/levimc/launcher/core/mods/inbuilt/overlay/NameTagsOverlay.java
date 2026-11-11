package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
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
import java.util.Locale;

public class NameTagsOverlay extends View {
    private static final String TAG = "NameTagsOverlay";
    private static final String[] MATERIALS = {
            "", "leather", "chainmail", "iron", "gold", "diamond",
            "netherite", "copper", "turtle", "other"
    };
    private static final String[] PIECES = {
            "helmet", "chestplate", "leggings", "boots"
    };

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint healthPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint accentPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final android.graphics.Path heartPath = new android.graphics.Path();
    private final Bitmap[][] armorIcons = new Bitmap[MATERIALS.length][PIECES.length];
    private final ColorFilter leatherFilter =
            new PorterDuffColorFilter(0xFFA0643C, PorterDuff.Mode.MULTIPLY);
    private final ColorFilter otherFilter =
            new PorterDuffColorFilter(0xFF8B8F98, PorterDuff.Mode.MULTIPLY);
    private float density = 1f;
    private boolean attached;
    private volatile boolean showing;
    private volatile boolean moduleEnabled = true;
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
        density = activity.getResources().getDisplayMetrics().density;
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.LEFT);
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        textPaint.setTextSize(13f * density);
        textPaint.setShadowLayer(3f, 0f, 1f, 0xCC000000);
        healthPaint.setColor(0xFFFF6B6B);
        healthPaint.setTextAlign(Paint.Align.LEFT);
        healthPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        healthPaint.setTextSize(11f * density);
        healthPaint.setShadowLayer(3f, 0f, 1f, 0xCC000000);
        iconPaint.setFilterBitmap(false);
        iconPaint.setDither(false);
        accentPaint.setStyle(Paint.Style.FILL);
    }

    public void applyConfigurationChanges() {
        invalidate();
    }

    public void show() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(this::show);
            return;
        }
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
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(this::hide);
            return;
        }
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

    public void setModuleEnabled(boolean enabled) {
        if (moduleEnabled == enabled) return;
        moduleEnabled = enabled;
        EspMod.setNameTags(enabled);
        if (enabled) return;
        Runnable clear = () -> {
            labels = new String[0];
            invalidate();
        };
        if (Looper.myLooper() == Looper.getMainLooper()) clear.run();
        else mainHandler.post(clear);
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
                int armorPieces = 0;
                float health = 20f;
                int armorMask = 0;
                String name;
                if (parts.length >= 7) {
                    armorPieces = Math.max(0, Math.min(4,
                            Integer.parseInt(parts[3].trim())));
                    health = Float.parseFloat(parts[4]);
                    armorMask = (int) Long.parseLong(parts[5].trim(), 16);
                    name = parts[6];
                } else if (parts.length >= 6) {
                    armorPieces = Math.max(0, Math.min(4,
                            Integer.parseInt(parts[3].trim())));
                    health = Float.parseFloat(parts[4]);
                    name = parts[5];
                } else if (parts.length == 5) {
                    armorPieces = "1".equals(parts[3].trim()) ? 4 : 0;
                    name = parts[4];
                } else {
                    name = parts.length >= 4 ? parts[3] : "";
                }
                if (name.isEmpty()) continue;
                drawLabel(canvas, x, y, name, health, armorPieces, armorMask);
            } catch (NumberFormatException ignored) {
            }
        }
    }

    private void drawLabel(Canvas canvas, float x, float y, String name,
                           float health, int armorPieces, int armorMask) {
        final float iconSize = 15f * density;
        final float iconGap = 1.5f * density;
        final float hpGap = 5f * density;
        final float heartSize = 7f * density;

        textPaint.setTextSize(13f * density);
        healthPaint.setTextSize(11f * density);
        String hpText = formatHealth(health);
        float nameWidth = textPaint.measureText(name);
        float hpWidth = healthPaint.measureText(hpText);

        int iconCount = 0;
        for (int slot = 0; slot < 4; slot++) {
            if (((armorMask >>> (slot * 8)) & 0xFF) != 0) iconCount++;
        }
        if (iconCount == 0) iconCount = Math.min(4, Math.max(0, armorPieces));
        float iconsWidth = iconCount > 0
                ? iconCount * iconSize + (iconCount - 1) * iconGap : 0f;
        float iconsBottom = y - dp(2f);
        float iconsTop = iconsBottom - iconSize;

        if (armorMask != 0) {
            float cursor = x - iconsWidth * 0.5f;
            for (int slot = 0; slot < 4; slot++) {
                int material = (armorMask >>> (slot * 8)) & 0xFF;
                if (material == 0) continue;
                Bitmap icon = armorIcon(material, slot);
                if (icon != null) {
                    iconPaint.setColorFilter(material == 1 ? leatherFilter
                            : material == 9 ? otherFilter : null);
                    canvas.drawBitmap(icon, null,
                            new android.graphics.RectF(cursor, iconsTop,
                                    cursor + iconSize, iconsBottom), iconPaint);
                }
                cursor += iconSize + iconGap;
            }
            iconPaint.setColorFilter(null);
        } else {
            float cursor = x - iconsWidth * 0.5f;
            for (int i = 0; i < iconCount; i++) {
                Bitmap icon = armorIcon(3, i);
                if (icon != null) {
                    iconPaint.setColorFilter(otherFilter);
                    canvas.drawBitmap(icon, null,
                            new android.graphics.RectF(cursor, iconsTop,
                                    cursor + iconSize, iconsBottom), iconPaint);
                }
                cursor += iconSize + iconGap;
            }
            iconPaint.setColorFilter(null);
        }

        float lineWidth = nameWidth + hpGap + heartSize + dp(2f) + hpWidth;
        float left = x - lineWidth * 0.5f;
        float baseline = y;
        textPaint.setTextAlign(Paint.Align.LEFT);
        canvas.drawText(name, left, baseline, textPaint);

        float heartLeft = left + nameWidth + hpGap;
        float heartTop = baseline - heartSize * 0.82f;
        drawHeart(canvas, heartLeft, heartTop, heartSize, 0xFFFF5B67);
        healthPaint.setTextAlign(Paint.Align.LEFT);
        canvas.drawText(hpText, heartLeft + heartSize + dp(2f), baseline,
                healthPaint);
    }

    private String formatHealth(float health) {
        if (!Float.isFinite(health)) health = 20f;
        return Math.abs(health - Math.round(health)) < 0.05f
                ? String.format(Locale.US, "%.0f", health)
                : String.format(Locale.US, "%.1f", health);
    }

    private Bitmap armorIcon(int material, int slot) {
        if (material <= 0 || material >= MATERIALS.length ||
                slot < 0 || slot >= PIECES.length) {
            return null;
        }
        Bitmap cached = armorIcons[material][slot];
        if (cached != null) return cached;
        int resource = getResources().getIdentifier(
                "armor_" + MATERIALS[material] + "_" + PIECES[slot],
                "drawable", getContext().getPackageName());
        if (resource == 0 && material == 9) {
            resource = getResources().getIdentifier(
                    "armor_iron_" + PIECES[slot], "drawable",
                    getContext().getPackageName());
        }
        if (resource == 0 && material == 8) {
            resource = getResources().getIdentifier(
                    "armor_iron_" + PIECES[slot], "drawable",
                    getContext().getPackageName());
        }
        if (resource == 0) return null;
        Bitmap bitmap = BitmapFactory.decodeResource(getResources(), resource);
        armorIcons[material][slot] = bitmap;
        return bitmap;
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
        accentPaint.setColor(color);
        canvas.drawPath(heartPath, accentPaint);
    }
}
