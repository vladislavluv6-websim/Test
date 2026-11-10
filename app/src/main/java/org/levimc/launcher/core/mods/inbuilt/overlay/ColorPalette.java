package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;

import java.util.ArrayList;
import java.util.List;

/** Circular colour palette styled like desktop cheat menus. */
public final class ColorPalette {
    public interface Listener {
        void onColorChanged(int color);
    }

    private static final int[] PRESETS = {
            0xFFFFFFFF, 0xFFE8EAF1, 0xFF9AA0AE, 0xFF5B6270, 0xFF232833, 0xFF000000,
            0xFFFF4D4D, 0xFFFF7A00, 0xFFFFC94D, 0xFFFFFF4D, 0xFF7CFF4D, 0xFF00E676,
            0xFF00E5FF, 0xFF4E83FF, 0xFF7C4DFF, 0xFFE040FB, 0xFFFF4081, 0xFFFF8A80
    };

    private ColorPalette() {}

    public static View create(Context context, int initial, boolean withAlpha, Listener listener) {
        float density = context.getResources().getDisplayMetrics().density;
        LinearLayout root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(0, (int) (6 * density), 0, (int) (2 * density));

        int[] current = {initial};
        List<SwatchView> circles = new ArrayList<>();
        List<Integer> colors = new ArrayList<>();

        for (int start = 0; start < PRESETS.length; start += 6) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            for (int i = start; i < Math.min(PRESETS.length, start + 6); i++) {
                int preset = PRESETS[i];
                SwatchView swatch = new SwatchView(context, preset);
                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                        0, (int) (42 * density), 1f);
                swatch.setLayoutParams(params);
                swatch.setOnClickListener(v -> {
                    current[0] = Color.argb(Color.alpha(current[0]),
                            Color.red(preset), Color.green(preset), Color.blue(preset));
                    paint(circles, colors, current[0], density);
                    listener.onColorChanged(current[0]);
                });
                circles.add(swatch);
                colors.add(preset);
                row.addView(swatch);
            }
            root.addView(row, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT));
        }
        paint(circles, colors, initial, density);

        if (withAlpha) {
            LinearLayout alphaRow = new LinearLayout(context);
            alphaRow.setOrientation(LinearLayout.HORIZONTAL);
            alphaRow.setGravity(Gravity.CENTER_VERTICAL);
            alphaRow.setPadding((int) (6 * density), (int) (10 * density),
                    (int) (6 * density), 0);
            android.widget.TextView alphaLabel = new android.widget.TextView(context);
            alphaLabel.setText("A");
            alphaLabel.setTextColor(0xFF9296A2);
            alphaLabel.setTextSize(12);
            alphaLabel.setPadding(0, 0, (int) (10 * density), 0);
            SeekBar alpha = new SeekBar(context);
            alpha.setMax(255);
            alpha.setProgress(Color.alpha(initial));
            alpha.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                    if (!fromUser) return;
                    current[0] = Color.argb(progress, Color.red(current[0]),
                            Color.green(current[0]), Color.blue(current[0]));
                    listener.onColorChanged(current[0]);
                }
                @Override public void onStartTrackingTouch(SeekBar seekBar) {}
                @Override public void onStopTrackingTouch(SeekBar seekBar) {}
            });
            alphaRow.addView(alphaLabel);
            alphaRow.addView(alpha, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            root.addView(alphaRow);
        }
        return root;
    }

    private static void paint(List<SwatchView> circles, List<Integer> colors,
                              int selected, float density) {
        for (int i = 0; i < circles.size(); i++) {
            int base = colors.get(i);
            boolean match = Color.red(base) == Color.red(selected)
                    && Color.green(base) == Color.green(selected)
                    && Color.blue(base) == Color.blue(selected);
            circles.get(i).setChosen(match);
        }
    }

    private static final class SwatchView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint checkPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF oval = new RectF();
        private final int color;
        private boolean selected;

        SwatchView(Context context, int color) {
            super(context);
            this.color = color;
            ringPaint.setStyle(Paint.Style.STROKE);
            checkPaint.setStyle(Paint.Style.STROKE);
            checkPaint.setStrokeCap(Paint.Cap.ROUND);
            setClickable(true);
        }

        void setChosen(boolean selected) {
            this.selected = selected;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float cx = getWidth() * 0.5f;
            float cy = getHeight() * 0.5f;
            float radius = Math.min(getWidth(), getHeight()) * 0.34f;
            if (selected) {
                ringPaint.setStrokeWidth(dp(2.2f));
                ringPaint.setColor(0xFFFFFFFF);
                canvas.drawCircle(cx, cy, radius + dp(4f), ringPaint);
            }
            paint.setShader(new RadialGradient(cx - radius * 0.35f, cy - radius * 0.4f,
                    radius * 1.55f,
                    new int[]{lighten(color), color, darken(color)},
                    new float[]{0f, 0.58f, 1f}, Shader.TileMode.CLAMP));
            canvas.drawCircle(cx, cy, radius, paint);
            paint.setShader(null);
            ringPaint.setStrokeWidth(dp(1f));
            ringPaint.setColor(0x33FFFFFF);
            canvas.drawCircle(cx, cy, radius, ringPaint);
            if (selected) {
                boolean dark = luminance(color) < 0.55f;
                checkPaint.setStrokeWidth(dp(2.6f));
                checkPaint.setColor(dark ? 0xFFFFFFFF : 0xFF12151E);
                float s = radius * 0.48f;
                canvas.drawLine(cx - s, cy, cx - s * 0.15f, cy + s * 0.7f, checkPaint);
                canvas.drawLine(cx - s * 0.15f, cy + s * 0.7f, cx + s, cy - s * 0.65f, checkPaint);
            }
        }

        private float dp(float value) {
            return value * getResources().getDisplayMetrics().density;
        }

        private static int lighten(int c) {
            return Color.argb(Color.alpha(c),
                    Math.min(255, Color.red(c) + 44),
                    Math.min(255, Color.green(c) + 44),
                    Math.min(255, Color.blue(c) + 44));
        }

        private static int darken(int c) {
            return Color.argb(Color.alpha(c),
                    (int) (Color.red(c) * 0.62f),
                    (int) (Color.green(c) * 0.62f),
                    (int) (Color.blue(c) * 0.62f));
        }

        private static float luminance(int c) {
            return (0.2126f * Color.red(c) + 0.7152f * Color.green(c)
                    + 0.0722f * Color.blue(c)) / 255f;
        }
    }
}
