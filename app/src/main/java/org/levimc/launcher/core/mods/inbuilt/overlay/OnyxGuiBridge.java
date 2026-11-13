package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.app.AlertDialog;
import android.text.InputType;
import android.util.Log;
import android.view.KeyEvent;
import android.webkit.JavascriptInterface;
import android.widget.EditText;

import org.levimc.launcher.core.mods.inbuilt.InbuiltConfigStore;
import org.levimc.launcher.core.mods.inbuilt.UnifiedMod;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class OnyxGuiBridge {
    private static final String TAG = "OnyxGuiBridge";

    private final Activity activity;
    private final OnyxMenuOverlay overlay;

    public OnyxGuiBridge(Activity activity, OnyxMenuOverlay overlay) {
        this.activity = activity;
        this.overlay = overlay;
    }

    @JavascriptInterface
    public void setEnabled(String moduleKey, boolean enabled) {
        post(() -> {
            UnifiedMod mod = overlay.findModule(moduleKey);
            if (mod != null) mod.applyEnabled(enabled);
        });
    }

    @JavascriptInterface
    public void setConfig(String moduleKey, String configKey, String value) {
        post(() -> {
            UnifiedMod mod = overlay.findModule(moduleKey);
            if (mod != null) mod.updateConfig(configKey, value == null ? "" : value);
        });
    }

    @JavascriptInterface
    public void setShortcut(String moduleKey, boolean visible) {
        post(() -> {
            UnifiedMod mod = overlay.findModule(moduleKey);
            if (mod == null) return;
            if (mod.getSource() != UnifiedMod.Source.INBUILT) return;
            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            manager.setOverlayShortcutVisible(mod.getId(), visible);
            InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
            if (overlayManager != null) {
                overlayManager.syncShortcutVisibility(mod.getId());
            }
        });
    }

    @JavascriptInterface
    public void captureKeybind(String moduleKey, String configKey) {
        post(() -> showKeybindDialog(moduleKey, configKey));
    }

    @JavascriptInterface
    public void hideMenu() {
        post(overlay::hide);
    }

    @JavascriptInterface
    public void exportLogs() {
        post(overlay::exportLogs);
    }

    @JavascriptInterface
    public String listConfigs() {
        try {
            return InbuiltConfigStore.listJson(activity);
        } catch (Throwable error) {
            Log.e(TAG, "listConfigs failed", error);
            return "[]";
        }
    }

    @JavascriptInterface
    public void refreshConfigs() {
        post(overlay::refreshConfigs);
    }

    @JavascriptInterface
    public void moveShortcuts() {
        post(() -> {
            InbuiltOverlayManager manager = InbuiltOverlayManager.getInstance();
            if (manager != null) manager.enterShortcutMoveMode();
        });
    }

    @JavascriptInterface
    public void saveConfigPrompt() {
        post(this::showSaveConfigDialog);
    }

    @JavascriptInterface
    public void importConfig() {
        post(() -> InbuiltConfigStore.requestImport(activity));
    }

    @JavascriptInterface
    public void applyConfig(String name) {
        post(() -> {
            InbuiltConfigStore.apply(activity, name);
            overlay.refreshConfigs();
        });
    }

    @JavascriptInterface
    public void shareConfig(String name) {
        post(() -> InbuiltConfigStore.share(activity, name));
    }

    @JavascriptInterface
    public void deleteConfigPrompt(String name) {
        post(() -> showDeleteConfigDialog(name));
    }

    private void post(Runnable action) {
        try {
            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                try {
                    action.run();
                } catch (Throwable error) {
                    Log.e(TAG, "bridge action failed", error);
                }
            });
        } catch (Throwable error) {
            Log.e(TAG, "bridge post failed", error);
        }
    }

    private void showSaveConfigDialog() {
        EditText input = new EditText(activity);
        input.setSingleLine(true);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setHint("Название конфига");
        String def = "cfg_" + new SimpleDateFormat("ddMM_HHmm", Locale.US)
                .format(new Date());
        input.setText(def);
        input.setSelection(input.getText().length());
        new AlertDialog.Builder(activity)
                .setTitle("Сохранить конфиг")
                .setView(input)
                .setPositiveButton("Сохранить", (d, which) -> {
                    InbuiltConfigStore.save(activity, input.getText().toString());
                    overlay.refreshConfigs();
                })
                .setNegativeButton("Отмена", null)
                .show();
    }

    private void showDeleteConfigDialog(String name) {
        if (name == null || name.isEmpty()) return;
        new AlertDialog.Builder(activity)
                .setTitle("Удалить конфиг")
                .setMessage(name)
                .setPositiveButton("Удалить", (d, which) -> {
                    InbuiltConfigStore.delete(activity, name);
                    overlay.refreshConfigs();
                })
                .setNegativeButton("Отмена", null)
                .show();
    }

    private void showKeybindDialog(String moduleKey, String configKey) {
        UnifiedMod mod = overlay.findModule(moduleKey);
        if (mod == null) return;
        AlertDialog dialog = new AlertDialog.Builder(activity)
                .setTitle("Keybind")
                .setMessage("Press a key")
                .setNegativeButton("Cancel", null)
                .setNeutralButton("Clear", (d, which) -> {
                    mod.updateConfig(configKey, String.valueOf(KeyEvent.KEYCODE_UNKNOWN));
                    overlay.updateKeybindLabel(moduleKey, configKey, "None");
                })
                .create();
        dialog.setOnKeyListener((d, keyCode, event) -> {
            if (event.getAction() != KeyEvent.ACTION_DOWN) return false;
            if (keyCode == KeyEvent.KEYCODE_BACK) {
                dialog.dismiss();
                return true;
            }
            if (!event.hasNoModifiers() && !KeyEvent.isModifierKey(keyCode)) return false;
            mod.updateConfig(configKey, String.valueOf(keyCode));
            overlay.updateKeybindLabel(moduleKey, configKey, keyLabel(keyCode));
            dialog.dismiss();
            return true;
        });
        dialog.show();
    }

    private static String keyLabel(int keyCode) {
        if (keyCode == KeyEvent.KEYCODE_UNKNOWN) return "None";
        String name = KeyEvent.keyCodeToString(keyCode);
        if (name == null) return "None";
        String label = name.replace("KEYCODE_", "").replace('_', ' ');
        if (label.isEmpty()) return "None";
        return label;
    }
}
