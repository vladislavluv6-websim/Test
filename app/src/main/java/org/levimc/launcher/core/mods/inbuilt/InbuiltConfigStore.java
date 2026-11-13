package org.levimc.launcher.core.mods.inbuilt;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import org.json.JSONArray;
import org.json.JSONObject;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.overlay.InbuiltOverlayManager;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Stores in-game settings ("CFG") as JSON files.
 *
 * The working copy lives in the app external files dir so it can be listed,
 * deleted and shared through the FileProvider. Saving also drops a copy into
 * Download/LeviLauncher/CFG via MediaStore (scoped storage safe).
 */
public final class InbuiltConfigStore {
    public static final int REQUEST_IMPORT = 0x4C46;

    private static final String TAG = "InbuiltConfigStore";
    private static final String FORMAT = "levi_inbuilt_cfg";
    private static final int FORMAT_VERSION = 1;
    private static final String EXT = ".cfg.json";
    private static final String AUTHORITY_SUFFIX = ".fileprovider";
    private static final String DOWNLOAD_SUBDIR =
            Environment.DIRECTORY_DOWNLOADS + "/LeviLauncher/CFG";
    private static final String KEY_ENABLED_PREFIX = "inbuilt_mod_enabled_";
    private static final String KEY_EXTERNAL_ENABLED_PREFIX = "external_module_enabled_";
    private static final String KEY_REVISION_SUFFIX = "_revision";

    private InbuiltConfigStore() {
    }

    private static boolean usable(Activity activity) {
        return activity != null && !activity.isFinishing() && !activity.isDestroyed();
    }

    private static File configDir(Context context) {
        File base = context.getExternalFilesDir(null);
        if (base == null) base = context.getFilesDir();
        File dir = new File(base, "configs");
        if (!dir.exists() && !dir.mkdirs()) {
            Log.w(TAG, "unable to create config dir " + dir);
        }
        return dir;
    }

    private static String safeName(String raw) {
        String name = raw == null ? "" : raw.trim();
        name = name.replaceAll("[\\\\/:*?\"<>|\\r\\n\\t]", "_");
        if (name.length() > 48) name = name.substring(0, 48);
        while (name.endsWith(".")) name = name.substring(0, name.length() - 1);
        return name.trim();
    }

    private static String defaultName() {
        return "cfg_" + new SimpleDateFormat("ddMM_HHmm", Locale.US).format(new Date());
    }

    private static boolean skipKey(String key) {
        if (key == null || key.isEmpty()) return true;
        if (key.startsWith(KEY_ENABLED_PREFIX)) return true;
        if (key.startsWith(KEY_EXTERNAL_ENABLED_PREFIX)) return true;
        return key.endsWith(KEY_REVISION_SUFFIX);
    }

    public static String listJson(Context context) {
        JSONArray array = new JSONArray();
        File[] files = configDir(context).listFiles((dir, name) -> name.endsWith(EXT));
        if (files != null) {
            List<File> sorted = new ArrayList<>();
            for (File file : files) sorted.add(file);
            sorted.sort((a, b) -> Long.compare(b.lastModified(), a.lastModified()));
            for (File file : sorted) {
                try {
                    String file_name = file.getName();
                    String name = file_name.substring(0, file_name.length() - EXT.length());
                    JSONObject node = new JSONObject();
                    node.put("name", name);
                    node.put("saved", file.lastModified());
                    node.put("size", file.length());
                    array.put(node);
                } catch (Throwable ignored) {
                }
            }
        }
        return array.toString();
    }

    public static void save(Activity activity, String requestedName) {
        if (!usable(activity)) return;
        String name = safeName(requestedName);
        if (name.isEmpty()) name = defaultName();
        try {
            JSONObject root = new JSONObject();
            root.put("format", FORMAT);
            root.put("version", FORMAT_VERSION);
            root.put("name", name);
            root.put("saved_at", System.currentTimeMillis());
            JSONObject prefs = new JSONObject();
            Map<String, ?> all = InbuiltModManager.getInstance(activity)
                    .getPreferences().getAll();
            for (Map.Entry<String, ?> entry : all.entrySet()) {
                String key = entry.getKey();
                if (skipKey(key)) continue;
                Object value = entry.getValue();
                JSONObject node = new JSONObject();
                if (value instanceof Boolean) {
                    node.put("t", "b");
                    node.put("v", (Boolean) value);
                } else if (value instanceof Integer) {
                    node.put("t", "i");
                    node.put("v", (Integer) value);
                } else if (value instanceof Long) {
                    node.put("t", "l");
                    node.put("v", (Long) value);
                } else if (value instanceof Float) {
                    node.put("t", "f");
                    node.put("v", ((Float) value).doubleValue());
                } else if (value instanceof String) {
                    node.put("t", "s");
                    node.put("v", (String) value);
                } else {
                    continue;
                }
                prefs.put(key, node);
            }
            root.put("prefs", prefs);
            byte[] bytes = root.toString(2).getBytes(StandardCharsets.UTF_8);
            File target = new File(configDir(activity), name + EXT);
            writeFile(target, bytes);
            exportToDownload(activity, name + EXT, bytes);
            toast(activity, "Конфиг сохранён: " + name);
        } catch (Throwable error) {
            Log.e(TAG, "save failed", error);
            toast(activity, "Не удалось сохранить конфиг");
        }
    }

    public static boolean apply(Activity activity, String name) {
        String safe = safeName(name);
        if (safe.isEmpty()) return false;
        return applyFile(activity, new File(configDir(activity), safe + EXT));
    }

    private static boolean applyFile(Activity activity, File file) {
        if (!usable(activity)) return false;
        if (file == null || !file.exists()) {
            toast(activity, "Конфиг не найден");
            return false;
        }
        try {
            JSONObject root = new JSONObject(readFile(file));
            if (!FORMAT.equals(root.optString("format"))) {
                toast(activity, "Это не конфиг LeviLauncher");
                return false;
            }
            JSONObject prefs = root.optJSONObject("prefs");
            if (prefs == null) {
                toast(activity, "Конфиг повреждён");
                return false;
            }
            SharedPreferences.Editor editor =
                    InbuiltModManager.getInstance(activity).getPreferences().edit();
            int applied = 0;
            Iterator<String> keys = prefs.keys();
            while (keys.hasNext()) {
                String key = keys.next();
                if (skipKey(key)) continue;
                JSONObject node = prefs.optJSONObject(key);
                if (node == null) continue;
                String type = node.optString("t");
                switch (type) {
                    case "b":
                        editor.putBoolean(key, node.optBoolean("v"));
                        break;
                    case "i":
                        editor.putInt(key, node.optInt("v"));
                        break;
                    case "l":
                        editor.putLong(key, node.optLong("v"));
                        break;
                    case "f":
                        editor.putFloat(key, (float) node.optDouble("v"));
                        break;
                    case "s":
                        editor.putString(key, node.optString("v"));
                        break;
                    default:
                        continue;
                }
                applied++;
            }
            editor.apply();
            if (applied == 0) {
                toast(activity, "В конфиге нет настроек");
                return false;
            }
            String label = root.optString("name");
            if (label == null || label.isEmpty()) label = file.getName();
            toast(activity, "Применено: " + label);
            refreshRuntime();
            return true;
        } catch (Throwable error) {
            Log.e(TAG, "apply failed", error);
            toast(activity, "Не удалось применить конфиг");
            return false;
        }
    }

    private static void refreshRuntime() {
        InbuiltOverlayManager manager = InbuiltOverlayManager.getInstance();
        if (manager != null) {
            manager.reloadAfterConfigApplied();
        }
    }

    public static void delete(Activity activity, String name) {
        if (!usable(activity)) return;
        String safe = safeName(name);
        if (safe.isEmpty()) return;
        try {
            File file = new File(configDir(activity), safe + EXT);
            boolean removed = file.exists() && file.delete();
            removeDownload(activity, safe + EXT);
            toast(activity, removed ? "Конфиг удалён" : "Конфиг не найден");
        } catch (Throwable error) {
            Log.e(TAG, "delete failed", error);
        }
    }

    public static void requestImport(Activity activity) {
        if (!usable(activity)) return;
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            activity.startActivityForResult(intent, REQUEST_IMPORT);
        } catch (Throwable error) {
            Log.e(TAG, "import picker failed", error);
            toast(activity, "Не удалось открыть выбор файла");
        }
    }

    public static boolean onActivityResult(Activity activity, int requestCode,
                                           int resultCode, Intent data) {
        if (requestCode != REQUEST_IMPORT) return false;
        if (resultCode != Activity.RESULT_OK || data == null) return true;
        Uri uri = data.getData();
        if (uri == null) return true;
        try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
            if (in == null) {
                toast(activity, "Не удалось прочитать файл");
                return true;
            }
            byte[] bytes = readAll(in);
            JSONObject root = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
            if (!FORMAT.equals(root.optString("format"))) {
                toast(activity, "Это не конфиг LeviLauncher");
                return true;
            }
            String name = safeName(root.optString("name"));
            if (name.isEmpty()) name = defaultName();
            File target = new File(configDir(activity), name + EXT);
            writeFile(target, bytes);
            applyFile(activity, target);
        } catch (Throwable error) {
            Log.e(TAG, "import failed", error);
            toast(activity, "Не удалось импортировать конфиг");
        }
        return true;
    }

    public static void share(Activity activity, String name) {
        if (!usable(activity)) return;
        String safe = safeName(name);
        if (safe.isEmpty()) {
            toast(activity, "Сначала выбери конфиг");
            return;
        }
        File file = new File(configDir(activity), safe + EXT);
        if (!file.exists()) {
            toast(activity, "Конфиг не найден");
            return;
        }
        try {
            Uri uri = FileProvider.getUriForFile(activity,
                    activity.getPackageName() + AUTHORITY_SUFFIX, file);
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("application/json");
            send.putExtra(Intent.EXTRA_STREAM, uri);
            send.putExtra(Intent.EXTRA_SUBJECT, "LeviLauncher CFG: " + safe);
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            activity.startActivity(Intent.createChooser(send, "Отправить конфиг"));
        } catch (Throwable error) {
            Log.e(TAG, "share failed", error);
            toast(activity, "Не удалось отправить конфиг");
        }
    }

    private static void exportToDownload(Context context, String displayName, byte[] bytes) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return;
        try {
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
            values.put(MediaStore.MediaColumns.MIME_TYPE, "application/json");
            values.put(MediaStore.MediaColumns.RELATIVE_PATH, DOWNLOAD_SUBDIR);
            context.getContentResolver().delete(
                    MediaStore.Downloads.EXTERNAL_CONTENT_URI,
                    MediaStore.MediaColumns.DISPLAY_NAME + "=? AND "
                            + MediaStore.MediaColumns.RELATIVE_PATH + "=?",
                    new String[]{displayName, DOWNLOAD_SUBDIR + "/"});
            Uri target = context.getContentResolver().insert(
                    MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (target == null) return;
            try (OutputStream out = context.getContentResolver().openOutputStream(target)) {
                if (out == null) return;
                out.write(bytes);
                out.flush();
            }
        } catch (Throwable error) {
            Log.w(TAG, "download export failed", error);
        }
    }

    private static void removeDownload(Context context, String displayName) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return;
        try {
            context.getContentResolver().delete(
                    MediaStore.Downloads.EXTERNAL_CONTENT_URI,
                    MediaStore.MediaColumns.DISPLAY_NAME + "=? AND "
                            + MediaStore.MediaColumns.RELATIVE_PATH + "=?",
                    new String[]{displayName, DOWNLOAD_SUBDIR + "/"});
        } catch (Throwable ignored) {
        }
    }

    private static void writeFile(File file, byte[] bytes) throws Exception {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(bytes);
            out.flush();
        }
    }

    private static String readFile(File file) throws Exception {
        try (InputStream in = new FileInputStream(file)) {
            return new String(readAll(in), StandardCharsets.UTF_8);
        }
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int read;
        while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
        return out.toByteArray();
    }

    private static void toast(Activity activity, String text) {
        try {
            Toast.makeText(activity, text, Toast.LENGTH_SHORT).show();
        } catch (Throwable ignored) {
        }
    }
}
