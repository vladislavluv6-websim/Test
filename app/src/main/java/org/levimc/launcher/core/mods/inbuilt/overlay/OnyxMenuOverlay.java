package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

import org.json.JSONArray;
import org.json.JSONObject;
import org.levimc.launcher.core.mods.inbuilt.ExternalModuleProvider;
import org.levimc.launcher.core.mods.inbuilt.InbuiltModuleProvider;
import org.levimc.launcher.core.mods.inbuilt.UnifiedMod;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class OnyxMenuOverlay {
    private static final String TAG = "OnyxMenuOverlay";
    private static final String ASSET_URL = "file:///android_asset/onyx_client_mobile.html";
    private static final String BRIDGE_NAME = "OnyxBridge";
    private static final long IDLE_RELEASE_MS = 20000L;

    private final Activity activity;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<UnifiedMod> modules = new ArrayList<>();
    private final Runnable idleReleaseRunnable;
    private final WindowManager windowManager;
    private WindowManager.LayoutParams wmParams;
    private WebView webView;
    private ViewGroup fallbackRoot;
    private boolean showing;
    private boolean attachedToWindow;
    private boolean pageReady;

    public OnyxMenuOverlay(Activity activity) {
        this.activity = activity;
        this.windowManager = (WindowManager) activity.getSystemService(Context.WINDOW_SERVICE);
        this.idleReleaseRunnable = () -> {
            if (!showing) release();
        };
    }

    public void show() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            handler.post(this::show);
            return;
        }
        if (activity.isFinishing() || activity.isDestroyed()) return;
        handler.removeCallbacks(idleReleaseRunnable);
        if (showing) {
            refreshMods();
            return;
        }
        showing = true;
        try {
            ensureWebView();
            attachToWindow();
            if (webView != null && attachedToWindow) webView.requestFocus();
            refreshMods();
        } catch (Throwable error) {
            Log.e(TAG, "show failed", error);
            release();
        }
    }

    public void preload() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            handler.post(this::preload);
            return;
        }
        if (webView != null) return;
        if (activity.isFinishing() || activity.isDestroyed()) return;
        try {
            ensureWebView();
            scheduleIdleRelease();
        } catch (Throwable error) {
            Log.e(TAG, "preload failed", error);
        }
    }

    public void hide() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            handler.post(this::hide);
            return;
        }
        if (!showing && !attachedToWindow) return;
        showing = false;
        detachFromWindow();
        scheduleIdleRelease();
    }

    private void scheduleIdleRelease() {
        handler.removeCallbacks(idleReleaseRunnable);
        handler.postDelayed(idleReleaseRunnable, IDLE_RELEASE_MS);
    }

    public void release() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            handler.post(this::release);
            return;
        }
        handler.removeCallbacks(idleReleaseRunnable);
        showing = false;
        detachFromWindow();
        WebView view = webView;
        webView = null;
        pageReady = false;
        if (view != null) {
            try {
                view.removeJavascriptInterface(BRIDGE_NAME);
            } catch (Exception ignored) {
            }
            try {
                view.destroy();
            } catch (Exception ignored) {
            }
        }
    }

    public boolean isShowing() {
        return showing;
    }

    public void refreshMods() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            handler.post(this::refreshMods);
            return;
        }
        loadModules();
        if (showing && pageReady) pushModules();
    }

    UnifiedMod findModule(String stableKey) {
        if (stableKey == null) return null;
        for (UnifiedMod mod : modules) {
            if (stableKey.equals(mod.getStableKey())) return mod;
        }
        return null;
    }

    void updateKeybindLabel(String moduleKey, String configKey, String label) {
        WebView view = webView;
        if (view == null) return;
        try {
            String script = "if(window.onyxKeybindResult)window.onyxKeybindResult("
                    + JSONObject.quote(moduleKey) + ","
                    + JSONObject.quote(configKey) + ","
                    + JSONObject.quote(label) + ");";
            view.evaluateJavascript(script, null);
        } catch (Throwable error) {
            Log.e(TAG, "updateKeybindLabel failed", error);
        }
    }

    private void loadModules() {
        modules.clear();
        try {
            modules.addAll(InbuiltModuleProvider.load(activity));
            modules.addAll(ExternalModuleProvider.load(activity));
        } catch (Throwable error) {
            Log.e(TAG, "loadModules failed", error);
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private void ensureWebView() {
        if (webView != null) return;
        loadModules();
        MenuWebView view = new MenuWebView(activity, this::hide);
        view.setBackgroundColor(0xFF0D0F16);
        view.setOverScrollMode(View.OVER_SCROLL_NEVER);
        view.setVerticalScrollBarEnabled(false);
        view.setHorizontalScrollBarEnabled(false);
        view.setDrawingCacheEnabled(false);
        WebSettings settings = view.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccessFromFileURLs(true);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setSupportZoom(false);
        settings.setBuiltInZoomControls(false);
        settings.setDisplayZoomControls(false);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setGeolocationEnabled(false);
        settings.setSaveFormData(false);
        settings.setTextZoom(100);
        view.addJavascriptInterface(new OnyxGuiBridge(activity, this), BRIDGE_NAME);
        view.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView web, String url) {
                pageReady = true;
                try {
                    pushModules();
                } catch (Throwable error) {
                    Log.e(TAG, "onPageFinished push failed", error);
                }
            }

            @Override
            public boolean onRenderProcessGone(WebView web, RenderProcessGoneDetail detail) {
                Log.w(TAG, "WebView renderer gone, didCrash="
                        + (detail != null && detail.didCrash()));
                release();
                return true;
            }
        });
        webView = view;
        view.loadUrl(ASSET_URL);
    }

    private void attachToWindow() {
        WebView view = webView;
        if (view == null || attachedToWindow) return;
        if (activity.isFinishing() || activity.isDestroyed()) return;

        DisplayMetrics dm = activity.getResources().getDisplayMetrics();
        int panelWidth = Math.max(1, Math.min(
                (int) (dm.widthPixels * 0.94f),
                (int) (720f * dm.density)));
        int panelHeight = Math.max(1, Math.min(
                (int) (dm.heightPixels * 0.9f),
                (int) (470f * dm.density)));

        wmParams = new WindowManager.LayoutParams(
                panelWidth,
                panelHeight,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_FULLSCREEN,
                PixelFormat.OPAQUE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            wmParams.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        wmParams.gravity = Gravity.CENTER;
        wmParams.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE;
        wmParams.token = activity.getWindow().getDecorView().getWindowToken();

        try {
            windowManager.addView(view, wmParams);
            attachedToWindow = true;
        } catch (Throwable windowError) {
            wmParams = null;
            ViewGroup root = activity.findViewById(android.R.id.content);
            if (root == null) throw new RuntimeException(windowError);
            fallbackRoot = root;
            FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                    panelWidth, panelHeight);
            params.gravity = Gravity.CENTER;
            root.addView(view, params);
            attachedToWindow = true;
        }
        try {
            view.onResume();
        } catch (Throwable ignored) {
        }
    }

    private void detachFromWindow() {
        WebView view = webView;
        if (view == null || !attachedToWindow) {
            wmParams = null;
            fallbackRoot = null;
            attachedToWindow = false;
            return;
        }
        try {
            view.onPause();
        } catch (Throwable ignored) {
        }
        try {
            if (wmParams != null && windowManager != null) {
                windowManager.removeView(view);
            } else if (fallbackRoot != null) {
                fallbackRoot.removeView(view);
            }
        } catch (Exception ignored) {
        }
        wmParams = null;
        fallbackRoot = null;
        attachedToWindow = false;
    }

    private void pushModules() {
        WebView view = webView;
        if (view == null || !pageReady || !attachedToWindow) return;
        try {
            String json = buildModulesJson();
            view.evaluateJavascript("if(window.onyxInit)window.onyxInit(" + json + ");", null);
        } catch (Throwable error) {
            Log.e(TAG, "pushModules failed", error);
        }
    }

    private String buildModulesJson() {
        JSONArray array = new JSONArray();
        InbuiltModManager manager = InbuiltModManager.getInstance(activity);
        for (UnifiedMod mod : modules) {
            try {
                boolean external = mod.getSource() == UnifiedMod.Source.EXTERNAL;
                JSONObject node = new JSONObject();
                node.put("key", mod.getStableKey());
                node.put("id", mod.getId());
                node.put("name", mod.getName());
                node.put("desc", mod.getDescription());
                node.put("enabled", mod.isEnabled());
                node.put("source", external ? "external" : "inbuilt");
                node.put("category", categoryFor(mod));
                node.put("shortcut_supported", !external);
                node.put("shortcut", !external && manager != null
                        && manager.isOverlayShortcutVisible(mod.getId()));

                JSONArray configs = new JSONArray();
                for (UnifiedMod.ConfigEntry config : mod.getConfigEntries()) {
                    if ("overlay_shortcut".equals(config.key)) continue;
                    configs.put(buildConfig(mod, config));
                }
                node.put("configs", configs);
                array.put(node);
            } catch (Exception error) {
                Log.e(TAG, "build module failed: " + mod.getId(), error);
            }
        }
        return array.toString();
    }

    private JSONObject buildConfig(UnifiedMod mod, UnifiedMod.ConfigEntry config)
            throws Exception {
        String value = mod.getConfigValue(config.key, config.defaultValue);
        if (value == null) value = "";
        JSONObject node = new JSONObject();
        node.put("key", config.key);
        node.put("label", config.displayName);
        node.put("depends", config.dependsOn == null ? "" : config.dependsOn);

        switch (config.type) {
            case TOGGLE:
                node.put("type", "toggle");
                node.put("value", isTrue(value));
                break;
            case SLIDER_INT:
                node.put("type", "slider_int");
                node.put("value", intValue(value, intValue(config.minValue, 0)));
                node.put("min_value", intValue(config.minValue, 0));
                node.put("max_value", intValue(config.maxValue, 100));
                break;
            case SLIDER_FLOAT:
                node.put("type", "slider_float");
                node.put("value", floatValue(value, floatValue(config.minValue, 0f)));
                node.put("min_value", floatValue(config.minValue, 0f));
                node.put("max_value", floatValue(config.maxValue, 100f));
                node.put("decimals", 2);
                break;
            case RADIO:
                node.put("type", "choice");
                node.put("value", value);
                node.put("options", radioOptions(config.minValue));
                break;
            case COLOR:
                node.put("type", "color");
                node.put("value", normalizeColor(value));
                break;
            case KEYBIND:
                node.put("type", "keybind");
                node.put("value", intValue(value, KeyEvent.KEYCODE_UNKNOWN));
                node.put("key_label", keyLabel(intValue(value, KeyEvent.KEYCODE_UNKNOWN)));
                break;
            case BUTTON:
                node.put("type", "button");
                node.put("value", "");
                break;
            case TEXT:
            default:
                node.put("type", "text");
                node.put("value", value);
                break;
        }
        return node;
    }

    private static JSONArray radioOptions(String labels) {
        JSONArray options = new JSONArray();
        if (labels == null || labels.isEmpty()) return options;
        String[] parts = labels.split(",");
        for (int i = 0; i < parts.length; i++) {
            try {
                options.put(new JSONObject()
                        .put("value", String.valueOf(i))
                        .put("label", parts[i].trim()));
            } catch (Exception ignored) {
            }
        }
        return options;
    }

    private static String categoryFor(UnifiedMod mod) {
        if (mod.getSource() == UnifiedMod.Source.EXTERNAL) return "misc";
        String id = mod.getId() == null ? "" : mod.getId().toLowerCase(Locale.US);
        switch (id) {
            case ModIds.AIM_ASSIST:
            case ModIds.TRIGGER_BOT:
            case ModIds.TARGET_ESP:
            case ModIds.SWING:
            case ModIds.FRIEND:
                return "combat";
            case ModIds.SPEED:
            case ModIds.AUTO_SPRINT:
            case ModIds.ZOOM:
            case ModIds.SNAPLOOK:
            case ModIds.GYRO:
            case ModIds.VIRTUAL_CURSOR:
            case ModIds.QUICK_DROP:
            case ModIds.CAMERA_PERSPECTIVE:
                return "movement";
            case ModIds.ESP:
            case ModIds.NAME_TAGS:
            case ModIds.FULLBRIGHT:
            case ModIds.TOGGLE_HUD:
            case ModIds.FPS_DISPLAY:
            case ModIds.CPS_DISPLAY:
                return "visual";
            default:
                return "misc";
        }
    }

    private static boolean isTrue(String value) {
        return "true".equalsIgnoreCase(value) || "1".equals(value);
    }

    private static int intValue(String value, int fallback) {
        if (value == null) return fallback;
        try {
            return Integer.parseInt(value.trim());
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private static float floatValue(String value, float fallback) {
        if (value == null) return fallback;
        try {
            return Float.parseFloat(value.trim());
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private static String normalizeColor(String value) {
        if (value == null || value.trim().isEmpty()) return "#FF4B7EFF";
        String color = value.trim();
        if (color.startsWith("#")) {
            if (color.length() == 7) return "#FF" + color.substring(1).toUpperCase(Locale.US);
            if (color.length() == 9) return color.toUpperCase(Locale.US);
            return "#FF4B7EFF";
        }
        try {
            int argb = Integer.decode(color);
            return String.format(Locale.US, "#%08X", argb);
        } catch (Exception ignored) {
            return "#FF4B7EFF";
        }
    }

    private static String keyLabel(int keyCode) {
        if (keyCode == KeyEvent.KEYCODE_UNKNOWN) return "None";
        String name = KeyEvent.keyCodeToString(keyCode);
        if (name == null) return "None";
        String label = name.replace("KEYCODE_", "").replace('_', ' ');
        return label.isEmpty() ? "None" : label;
    }

    private interface BackHandler {
        void onBack();
    }

    private static class MenuWebView extends WebView {
        private final BackHandler backHandler;

        MenuWebView(Context context, Runnable onBack) {
            super(context);
            this.backHandler = onBack::run;
        }

        @Override
        public boolean dispatchKeyEvent(KeyEvent event) {
            if (event.getKeyCode() == KeyEvent.KEYCODE_BACK
                    && event.getAction() == KeyEvent.ACTION_UP) {
                backHandler.onBack();
                return true;
            }
            return super.dispatchKeyEvent(event);
        }
    }
}
