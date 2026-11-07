package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

import org.levimc.launcher.core.mods.inbuilt.UnifiedMod;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Solstice-style ClickGui: horizontally scrollable category columns whose rows
 * are module toggles. Enabled modules are filled with the accent gradient, a
 * gear opens the module config and the star toggles a favourite. Rendered with
 * Canvas so it looks identical on every OEM theme and cannot crash on a
 * Material theme mismatch.
 */
public class SolsticeModulesView extends View {

    public interface Listener {
        void onToggle(UnifiedMod mod, boolean enabled);
        void onConfig(UnifiedMod mod);
        void onFavorite(UnifiedMod mod, boolean favorite);
    }

    private static final int COLOR_HEADER = 0xFF181818;
    private static final int COLOR_ROW = 0xFF282828;
    private static final int COLOR_ROW_LAST = 0xFF242424;
    private static final int COLOR_TEXT = 0xFFF2F2F2;
    private static final int COLOR_TEXT_DIM = 0xFFB8B8B8;
    private static final int COLOR_GEAR = 0xFFB0B0B0;
    private static final int COLOR_STAR = 0xFF7A7A7A;
    private static final int COLOR_STAR_ON = 0xFFFFC94D;
    private static final int COLOR_SEPARATOR = 0x14FFFFFF;
    private static final int ACCENT_START = 0xFFFF63CA;
    private static final int ACCENT_END = 0xFF92F5FF;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path starPath = new Path();
    private final RectF rect = new RectF();
    private final Handler handler = new Handler(Looper.getMainLooper());

    private final float density;
    private final float scaledDensity;
    private final int touchSlop;
    private final int longPressTimeout;

    private final float rowHeight;
    private final float headerHeight;
    private final float gap;
    private final float padding;
    private final float radius;

    private final List<Column> columns = new ArrayList<>();
    private Listener listener;

    private float columnWidth;
    private float contentWidth;
    private float contentHeight;
    private float scrollX;
    private float scrollY;

    private float downX;
    private float downY;
    private float lastX;
    private float lastY;
    private float dragDx;
    private float dragDy;
    private float downScrollX;
    private float downScrollY;
    private long downTime;
    private boolean dragging;
    private boolean longPressFired;
    private Hit pendingHit;

    private final Runnable longPressRunnable = new Runnable() {
        @Override
        public void run() {
            if (dragging || pendingHit == null || pendingHit.mod == null) return;
            longPressFired = true;
            if (listener != null) listener.onConfig(pendingHit.mod);
            invalidate();
        }
    };

    public SolsticeModulesView(Context context) {
        this(context, null);
    }

    public SolsticeModulesView(Context context, AttributeSet attrs) {
        super(context, attrs);
        density = getResources().getDisplayMetrics().density;
        scaledDensity = getResources().getDisplayMetrics().scaledDensity;
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        longPressTimeout = ViewConfiguration.getLongPressTimeout();

        rowHeight = 34f * density;
        headerHeight = 32f * density;
        gap = 10f * density;
        padding = 8f * density;
        radius = 9f * density;
        columnWidth = 200f * density;

        textPaint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        setClickable(true);
        setFocusable(false);
    }

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void setContent(List<UnifiedMod> mods, Set<String> favoriteKeys) {
        columns.clear();
        Map<String, Column> byGroup = new LinkedHashMap<>();
        if (mods != null) {
            for (UnifiedMod mod : mods) {
                String group = mod.getGroupName();
                if (group == null || group.trim().isEmpty()) group = "Modules";
                Column column = byGroup.get(group);
                if (column == null) {
                    column = new Column(group);
                    byGroup.put(group, column);
                    columns.add(column);
                }
                boolean favorite = favoriteKeys != null && favoriteKeys.contains(mod.getStableKey());
                column.rows.add(new Row(mod, favorite));
            }
        }
        recomputeLayout();
        clampScroll();
        invalidate();
    }

    @Override
    protected void onDetachedFromWindow() {
        handler.removeCallbacks(longPressRunnable);
        super.onDetachedFromWindow();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        columnWidth = Math.max(150f * density, Math.min(210f * density, w * 0.62f));
        recomputeLayout();
        clampScroll();
    }

    private void recomputeLayout() {
        float x = padding;
        float maxHeight = 0f;
        for (Column column : columns) {
            column.x = x;
            column.width = columnWidth;
            column.height = headerHeight + column.rows.size() * rowHeight;
            x += columnWidth + gap;
            maxHeight = Math.max(maxHeight, column.height);
        }
        contentWidth = columns.isEmpty() ? 0f : x - gap + padding;
        contentHeight = maxHeight + padding * 2f;
    }

    private void clampScroll() {
        float maxX = Math.max(0f, contentWidth - getWidth());
        float maxY = Math.max(0f, contentHeight - getHeight());
        scrollX = Math.max(0f, Math.min(scrollX, maxX));
        scrollY = Math.max(0f, Math.min(scrollY, maxY));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (columns.isEmpty()) return;

        float baseY = padding - scrollY;
        for (Column column : columns) {
            float x = column.x - scrollX;
            if (x > getWidth() || x + column.width < 0) continue;
            drawColumn(canvas, column, x, baseY);
        }
    }

    private void drawColumn(Canvas canvas, Column column, float x, float top) {
        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(COLOR_HEADER);
        rect.set(x, top, x + column.width, top + headerHeight);
        canvas.drawRoundRect(rect, radius, radius, paint);
        rect.set(x, top + headerHeight - radius, x + column.width, top + headerHeight);
        canvas.drawRect(rect, paint);

        textPaint.setTextSize(11.5f * scaledDensity);
        textPaint.setColor(COLOR_TEXT);
        textPaint.setTextAlign(Paint.Align.CENTER);
        float headerBaseline = top + headerHeight / 2f
            - (textPaint.ascent() + textPaint.descent()) / 2f;
        canvas.drawText(column.name.toUpperCase(java.util.Locale.ROOT),
            x + column.width / 2f, headerBaseline, textPaint);

        int count = column.rows.size();
        for (int i = 0; i < count; i++) {
            Row row = column.rows.get(i);
            float rowTop = top + headerHeight + i * rowHeight;
            float rowBottom = rowTop + rowHeight;
            if (rowBottom < 0 || rowTop > getHeight()) continue;

            boolean last = i == count - 1;
            float corner = last ? radius : 0f;
            rect.set(x, rowTop, x + column.width, rowBottom);

            if (row.mod.isEnabled()) {
                paint.setShader(new LinearGradient(x, rowTop, x + column.width, rowTop,
                    ACCENT_START, ACCENT_END, Shader.TileMode.CLAMP));
                paint.setAlpha(235);
                canvas.drawRoundRect(rect, corner, corner, paint);
                paint.setAlpha(255);
                paint.setShader(null);
                paint.setColor(0x33000000);
                canvas.drawRoundRect(rect, corner, corner, paint);
            } else {
                paint.setShader(null);
                paint.setColor(last ? COLOR_ROW_LAST : COLOR_ROW);
                canvas.drawRoundRect(rect, corner, corner, paint);
            }

            paint.setColor(COLOR_SEPARATOR);
            paint.setStrokeWidth(density);
            canvas.drawLine(x + padding / 2f, rowTop, x + column.width - padding / 2f,
                rowTop, paint);

            float iconSpace = 58f * density;
            textPaint.setTextSize(12f * scaledDensity);
            textPaint.setColor(row.mod.isEnabled() ? Color.WHITE : COLOR_TEXT_DIM);
            textPaint.setTextAlign(Paint.Align.CENTER);
            float textBaseline = rowTop + rowHeight / 2f
                - (textPaint.ascent() + textPaint.descent()) / 2f;
            float textCenter = x + (column.width - iconSpace) / 2f;
            canvas.drawText(ellipsize(row.mod.getName(), column.width - iconSpace - padding * 2f),
                textCenter, textBaseline, textPaint);

            drawStar(canvas, x + column.width - 42f * density, rowTop + rowHeight / 2f,
                row.favorite);
            drawGear(canvas, x + column.width - 17f * density, rowTop + rowHeight / 2f);
        }
    }

    private void drawStar(Canvas canvas, float cx, float cy, boolean filled) {
        iconPaint.setShader(null);
        iconPaint.setStyle(Paint.Style.FILL);
        iconPaint.setColor(filled ? COLOR_STAR_ON : COLOR_STAR);
        buildStar(cx, cy, 6.5f * density, 2.9f * density);
        canvas.drawPath(starPath, iconPaint);
    }

    private void buildStar(float cx, float cy, float outer, float inner) {
        starPath.reset();
        for (int i = 0; i < 10; i++) {
            double angle = -Math.PI / 2 + i * Math.PI / 5;
            float r = (i % 2 == 0) ? outer : inner;
            float px = cx + (float) Math.cos(angle) * r;
            float py = cy + (float) Math.sin(angle) * r;
            if (i == 0) starPath.moveTo(px, py);
            else starPath.lineTo(px, py);
        }
        starPath.close();
    }

    private void drawGear(Canvas canvas, float cx, float cy) {
        iconPaint.setShader(null);
        iconPaint.setStyle(Paint.Style.STROKE);
        iconPaint.setStrokeWidth(1.3f * density);
        iconPaint.setColor(COLOR_GEAR);
        float outer = 4.6f * density;
        float inner = 2.0f * density;
        canvas.drawCircle(cx, cy, inner, iconPaint);
        for (int i = 0; i < 8; i++) {
            double angle = i * Math.PI / 4;
            float cos = (float) Math.cos(angle);
            float sin = (float) Math.sin(angle);
            canvas.drawLine(cx + cos * outer * 0.75f, cy + sin * outer * 0.75f,
                cx + cos * outer, cy + sin * outer, iconPaint);
        }
    }

    private String ellipsize(String text, float maxWidth) {
        if (text == null) return "";
        if (textPaint.measureText(text) <= maxWidth) return text;
        String ellipsis = "...";
        int length = text.length();
        while (length > 0 && textPaint.measureText(text, 0, length) + textPaint.measureText(ellipsis) > maxWidth) {
            length--;
        }
        return text.substring(0, length) + ellipsis;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX = lastX = event.getX();
                downY = lastY = event.getY();
                dragDx = 0f;
                dragDy = 0f;
                downScrollX = scrollX;
                downScrollY = scrollY;
                downTime = SystemClock.uptimeMillis();
                dragging = false;
                longPressFired = false;
                pendingHit = hitTest(downX, downY);
                if (pendingHit != null && pendingHit.mod != null) {
                    handler.postDelayed(longPressRunnable, longPressTimeout);
                }
                return true;
            case MotionEvent.ACTION_MOVE: {
                float x = event.getX();
                float y = event.getY();
                dragDx += x - lastX;
                dragDy += y - lastY;
                lastX = x;
                lastY = y;
                if (!dragging && Math.hypot(dragDx, dragDy) > touchSlop) {
                    dragging = true;
                    handler.removeCallbacks(longPressRunnable);
                }
                if (dragging) {
                    scrollX = downScrollX - dragDx;
                    scrollY = downScrollY - dragDy;
                    clampScroll();
                    invalidate();
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
                handler.removeCallbacks(longPressRunnable);
                if (!dragging && !longPressFired
                        && SystemClock.uptimeMillis() - downTime < longPressTimeout) {
                    handleTap(event.getX(), event.getY());
                }
                pendingHit = null;
                return true;
            case MotionEvent.ACTION_CANCEL:
                handler.removeCallbacks(longPressRunnable);
                pendingHit = null;
                return true;
            default:
                return true;
        }
    }

    private void handleTap(float x, float y) {
        Hit hit = hitTest(x, y);
        if (hit == null || hit.mod == null) return;
        float iconSpace = 58f * density;
        float iconStart = hit.column.x + hit.column.width - iconSpace;
        if (x + scrollX >= iconStart) {
            float starCenter = hit.column.x + hit.column.width - 42f * density;
            float gearCenter = hit.column.x + hit.column.width - 17f * density;
            float contentX = x + scrollX;
            float contentY = y + scrollY;
            if (Math.abs(contentX - starCenter) <= 12f * density
                    && Math.abs(contentY - hit.rowCenterY) <= 12f * density) {
                boolean favorite = !hit.row.favorite;
                hit.row.favorite = favorite;
                if (listener != null) listener.onFavorite(hit.mod, favorite);
                invalidate();
                return;
            }
            if (Math.abs(contentX - gearCenter) <= 12f * density
                    && Math.abs(contentY - hit.rowCenterY) <= 12f * density) {
                if (listener != null) listener.onConfig(hit.mod);
                return;
            }
        }
        boolean enabled = !hit.mod.isEnabled();
        hit.mod.applyEnabled(enabled);
        if (listener != null) listener.onToggle(hit.mod, enabled);
        invalidate();
    }

    private Hit hitTest(float x, float y) {
        float contentX = x + scrollX;
        float localY = (y + scrollY) - padding - headerHeight;
        if (localY < 0) return null;
        int index = (int) (localY / rowHeight);
        for (Column column : columns) {
            if (contentX < column.x || contentX > column.x + column.width) continue;
            if (index < 0 || index >= column.rows.size()) return null;
            Hit hit = new Hit();
            hit.column = column;
            hit.row = column.rows.get(index);
            hit.mod = hit.row.mod;
            hit.rowCenterY = padding + headerHeight + index * rowHeight + rowHeight / 2f;
            return hit;
        }
        return null;
    }

    private static final class Column {
        final String name;
        final List<Row> rows = new ArrayList<>();
        float x;
        float width;
        float height;

        Column(String name) {
            this.name = name;
        }
    }

    private static final class Row {
        final UnifiedMod mod;
        boolean favorite;

        Row(UnifiedMod mod, boolean favorite) {
            this.mod = mod;
            this.favorite = favorite;
        }
    }

    private static final class Hit {
        Column column;
        Row row;
        UnifiedMod mod;
        float rowCenterY;
    }
}
