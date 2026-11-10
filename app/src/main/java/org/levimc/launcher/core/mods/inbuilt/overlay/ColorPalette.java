package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;

import java.util.ArrayList;
import java.util.List;

/** Reusable circular colour palette with an optional alpha slider. */
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
        root.setPadding(0, (int) (4 * density), 0, (int) (2 * density));

        int[] current = {initial};
        List<View> circles = new ArrayList<>();
        List<Integer> colors = new ArrayList<>();

        for (int start = 0; start < PRESETS.length; start += 6) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            for (int i = start; i < Math.min(PRESETS.length, start + 6); i++) {
                int preset = PRESETS[i];
                android.widget.FrameLayout cell = new android.widget.FrameLayout(context);
                LinearLayout.LayoutParams cellParams = new LinearLayout.LayoutParams(
                        0, (int) (38 * density), 1f);
                cell.setLayoutParams(cellParams);
                View circle = new View(context);
                android.widget.FrameLayout.LayoutParams circleParams =
                        new android.widget.FrameLayout.LayoutParams(
                                (int) (28 * density), (int) (28 * density));
                circleParams.gravity = Gravity.CENTER;
                circle.setLayoutParams(circleParams);
                circle.setContentDescription(String.format("#%06X", preset & 0xFFFFFF));
                circle.setOnClickListener(v -> {
                    current[0] = Color.argb(Color.alpha(current[0]),
                            Color.red(preset), Color.green(preset), Color.blue(preset));
                    paint(circles, colors, current[0], density);
                    listener.onColorChanged(current[0]);
                });
                circles.add(circle);
                colors.add(preset);
                cell.addView(circle);
                row.addView(cell);
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
            alphaRow.setPadding(0, (int) (8 * density), 0, 0);
            android.widget.TextView alphaLabel = new android.widget.TextView(context);
            alphaLabel.setText("A");
            alphaLabel.setTextColor(0xFF9296A2);
            alphaLabel.setTextSize(12);
            alphaLabel.setPadding(0, 0, (int) (8 * density), 0);
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

    private static void paint(List<View> circles, List<Integer> colors, int selected, float density) {
        for (int i = 0; i < circles.size(); i++) {
            int base = colors.get(i);
            boolean match = Color.red(base) == Color.red(selected)
                    && Color.green(base) == Color.green(selected)
                    && Color.blue(base) == Color.blue(selected);
            GradientDrawable shape = new GradientDrawable();
            shape.setShape(GradientDrawable.OVAL);
            shape.setColor(base);
            shape.setStroke((int) ((match ? 2.5f : 1f) * density),
                    match ? 0xFFFFFFFF : 0x44FFFFFF);
            circles.get(i).setBackground(shape);
            circles.get(i).setScaleX(match ? 1.12f : 1f);
            circles.get(i).setScaleY(match ? 1.12f : 1f);
            circles.get(i).setAlpha(match ? 255 : 210);
        }
    }
}
