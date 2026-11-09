package org.levimc.launcher.core.mods.inbuilt.manager;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.KeyEvent;

import org.levimc.launcher.core.mods.inbuilt.model.ModIds;

import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicLong;

public class InbuiltModManager {
    private static final String PREFS_NAME = "inbuilt_mods_prefs";
    private static final String KEY_AUTOSPRINT_KEY = "autosprint_key";
    private static final String KEY_SPEED_MODE = "speed_mode";
    private static final String KEY_SPEED_VALUE = "speed_value";
    private static final String KEY_SPEED_TICK_SPEED = "speed_tick_speed";
    private static final String KEY_SPEED_JUMP_TYPE = "speed_jump_type";
    private static final String KEY_SPEED_JUMP_HEIGHT = "speed_jump_height";
    private static final String KEY_SPEED_AVOID_CHECK = "speed_avoid_check";
    private static final String KEY_SPEED_AVOID_DELAY = "speed_avoid_delay";
    private static final String KEY_SPEED_DEBUG = "speed_debug";
    private static final String KEY_AIM_ASSIST_RANGE = "aim_assist_range";
    private static final String KEY_AIM_ASSIST_STRENGTH = "aim_assist_strength";
    private static final String KEY_AIM_ASSIST_LEGIT = "aim_assist_legit";
    private static final String KEY_AIM_ASSIST_FOV = "aim_assist_fov";
    private static final String KEY_AIM_ASSIST_WALL_CHECK = "aim_assist_wall_check";
    private static final String KEY_AIM_ASSIST_TARGET_PLAYERS = "aim_assist_target_players";
    private static final String KEY_AIM_ASSIST_TARGET_MOBS = "aim_assist_target_mobs";
    private static final String KEY_AIM_ASSIST_BODY_PART = "aim_assist_body_part";
    private static final String KEY_AIM_ASSIST_MAX_SPEED = "aim_assist_max_speed";
    private static final String KEY_AIM_ASSIST_SMOOTHING = "aim_assist_smoothing";
    private static final String KEY_AIM_ASSIST_ACCEL = "aim_assist_accel";
    private static final String KEY_AIM_ASSIST_DEADZONE = "aim_assist_deadzone";
    private static final String KEY_AIM_ASSIST_FULL_RANGE = "aim_assist_full_range";
    private static final String KEY_AIM_ASSIST_STICKY = "aim_assist_sticky";
    private static final String KEY_AIM_ASSIST_GRACE = "aim_assist_grace";
    private static final String KEY_AIM_ASSIST_PREFER_CLOSEST = "aim_assist_prefer_closest";
    private static final String KEY_ESP_SHOW_PLAYERS = "esp_show_players";
    private static final String KEY_ESP_SHOW_MOBS = "esp_show_mobs";
    private static final String KEY_ESP_RANGE = "esp_range";
    private static final String KEY_ESP_PLAYER_COLOR = "esp_player_color";
    private static final String KEY_ESP_MOB_COLOR = "esp_mob_color";
    private static final String KEY_TRIGGER_BOT_ENABLED = "trigger_bot_enabled";
    private static final String KEY_TRIGGER_BOT_DELAY = "trigger_bot_delay";
    private static final String KEY_TRIGGER_BOT_REACTION_JITTER = "trigger_bot_reaction_jitter";
    private static final String KEY_TRIGGER_BOT_RANGE = "trigger_bot_range";
    private static final String KEY_TRIGGER_BOT_TOLERANCE = "trigger_bot_tolerance";
    private static final String KEY_TRIGGER_BOT_TARGET_PLAYERS = "trigger_bot_target_players";
    private static final String KEY_TRIGGER_BOT_TARGET_MOBS = "trigger_bot_target_mobs";
    private static final String KEY_TRIGGER_BOT_HUMANIZATION = "trigger_bot_humanization";
    private static final String KEY_TRIGGER_BOT_INTERVAL = "trigger_bot_interval";
    private static final String KEY_TRIGGER_BOT_INTERVAL_JITTER = "trigger_bot_interval_jitter";
    private static final String KEY_TRIGGER_BOT_SKIP_CHANCE = "trigger_bot_skip_chance";
    private static final String KEY_TRIGGER_BOT_ONLY_CRIT = "trigger_bot_only_crit";
    private static final String KEY_INBUILT_KEYBIND_PREFIX = "inbuilt_keybind_";
    private static final String KEY_OVERLAY_BUTTON_SIZE_PREFIX = "overlay_button_size_";
    private static final String KEY_OVERLAY_OPACITY_PREFIX = "overlay_opacity_";
    private static final String KEY_MOD_MENU_ENABLED = "mod_menu_enabled";
    private static final String KEY_NOTIFICATIONS_ENABLED = "notifications_enabled";
    private static final String KEY_MOD_MENU_OPACITY = "mod_menu_opacity";
    private static final String KEY_MOD_MENU_BUTTON_OPACITY = "mod_menu_button_opacity";
    private static final String KEY_MOD_MENU_COMPACT = "mod_menu_compact";
    private static final String KEY_MOD_MENU_KEYBIND = "mod_menu_keybind";
    private static final String KEY_PAUSE_MENU_ONLY = "pause_menu_only";
    private static final String KEY_HIDE_LAUNCHER_HUD = "hide_launcher_hud_with_minecraft";
    private static final String KEY_FAVORITE_MOD_KEYS = "favorite_mod_keys";
    private static final String KEY_INBUILT_MOD_ENABLED_PREFIX = "inbuilt_mod_enabled_";
    private static final String KEY_EXTERNAL_MODULE_ENABLED_PREFIX = "external_module_enabled_";
    private static final String KEY_ZOOM_LEVEL = "zoom_level";
    private static final String KEY_ZOOM_KEYBIND = "zoom_keybind";
    private static final String KEY_ZOOM_TRANSITION_DURATION = "zoom_transition_duration";
    private static final String KEY_CURSOR_SENSITIVITY = "cursor_sensitivity";
    private static final String KEY_GYRO_SENSITIVITY_MULTIPLIER = "gyro_sensitivity_multiplier";
    private static final String KEY_GYRO_SENSITIVITY_X = "gyro_sensitivity_x";
    private static final String KEY_GYRO_SENSITIVITY_Y = "gyro_sensitivity_y";
    private static final String KEY_GYRO_INVERT_X = "gyro_invert_x";
    private static final String KEY_GYRO_INVERT_Y = "gyro_invert_y";
    private static final String KEY_GYRO_DEADZONE = "gyro_deadzone";
    private static final String KEY_OVERLAY_POSITION_X_PREFIX = "overlay_pos_x_";
    private static final String KEY_OVERLAY_POSITION_Y_PREFIX = "overlay_pos_y_";
    private static final String KEY_OVERLAY_LOCK_PREFIX = "overlay_lock_";
    private static final String KEY_OVERLAY_SHOW_EVERYWHERE_PREFIX = "overlay_show_everywhere_";
    private static final String KEY_HOTBAR_ITEM_ICONS = "hotbar_item_icons";
    private static final String KEY_HOTBAR_ITEM_COUNTS = "hotbar_item_counts";
    private static final String KEY_HOTBAR_SLOT_ENABLED_PREFIX = "hotbar_slot_enabled_";
    private static final int DEFAULT_OVERLAY_BUTTON_SIZE = 56;
    private static final int DEFAULT_OVERLAY_OPACITY = 100;
    private static final int MIN_MOD_MENU_OPACITY = 70;
    private static final int DEFAULT_ZOOM_LEVEL = 10;
    private static final int DEFAULT_ZOOM_TRANSITION_DURATION = 150;
    private static final int DEFAULT_CURSOR_SENSITIVITY = 120;
    private static final int DEFAULT_SPEED_MODE = 0;
    private static final int DEFAULT_SPEED_VALUE = 50;
    private static final int DEFAULT_SPEED_TICK_SPEED = 3;
    private static final int DEFAULT_SPEED_JUMP_TYPE = 0;
    private static final int DEFAULT_SPEED_JUMP_HEIGHT = 42;
    private static final boolean DEFAULT_SPEED_AVOID_CHECK = false;
    private static final int DEFAULT_SPEED_AVOID_DELAY = 400;
    private static final boolean DEFAULT_SPEED_DEBUG = false;
    private static final int DEFAULT_AIM_ASSIST_RANGE = 10;
    private static final int DEFAULT_AIM_ASSIST_STRENGTH = 100;
    private static final boolean DEFAULT_AIM_ASSIST_LEGIT = true;
    private static final int DEFAULT_AIM_ASSIST_FOV = 90;
    private static final boolean DEFAULT_AIM_ASSIST_WALL_CHECK = true;
    private static final boolean DEFAULT_AIM_ASSIST_TARGET_PLAYERS = true;
    private static final boolean DEFAULT_AIM_ASSIST_TARGET_MOBS = true;
    private static final int DEFAULT_AIM_ASSIST_BODY_PART = 0;
    private static final int DEFAULT_AIM_ASSIST_MAX_SPEED = 560;
    private static final int DEFAULT_AIM_ASSIST_SMOOTHING = 15;
    private static final int DEFAULT_AIM_ASSIST_ACCEL = 2200;
    private static final int DEFAULT_AIM_ASSIST_DEADZONE = 30;
    private static final int DEFAULT_AIM_ASSIST_FULL_RANGE = 18;
    private static final int DEFAULT_AIM_ASSIST_STICKY = 6;
    private static final int DEFAULT_AIM_ASSIST_GRACE = 10;
    private static final boolean DEFAULT_AIM_ASSIST_PREFER_CLOSEST = false;
    private static final boolean DEFAULT_ESP_SHOW_PLAYERS = true;
    private static final boolean DEFAULT_ESP_SHOW_MOBS = true;
    private static final int DEFAULT_ESP_RANGE = 32;
    private static final int DEFAULT_ESP_PLAYER_COLOR = 0xFFFF5555;
    private static final int DEFAULT_ESP_MOB_COLOR = 0xFFFFAA00;
    private static final boolean DEFAULT_TRIGGER_BOT_ENABLED = true;
    private static final int DEFAULT_TRIGGER_BOT_DELAY = 280;
    private static final int DEFAULT_TRIGGER_BOT_REACTION_JITTER = 70;
    private static final int DEFAULT_TRIGGER_BOT_RANGE = 30;
    private static final int DEFAULT_TRIGGER_BOT_TOLERANCE = 3;
    private static final boolean DEFAULT_TRIGGER_BOT_TARGET_PLAYERS = true;
    private static final boolean DEFAULT_TRIGGER_BOT_TARGET_MOBS = true;
    private static final boolean DEFAULT_TRIGGER_BOT_HUMANIZATION = true;
    private static final int DEFAULT_TRIGGER_BOT_INTERVAL = 620;
    private static final int DEFAULT_TRIGGER_BOT_INTERVAL_JITTER = 170;
    private static final int DEFAULT_TRIGGER_BOT_SKIP_CHANCE = 6;
    private static final boolean DEFAULT_TRIGGER_BOT_ONLY_CRIT = false;
    private static final int DEFAULT_GYRO_SENSITIVITY_MULTIPLIER = 100;
    private static final int DEFAULT_GYRO_SENSITIVITY = 100;
    private static final int DEFAULT_GYRO_DEADZONE = 5;

    private static volatile InbuiltModManager instance;
    private final SharedPreferences prefs;
    private final AtomicLong overlayVisibilityRevision = new AtomicLong(1L);
    private final SharedPreferences.OnSharedPreferenceChangeListener preferenceListener;

    private InbuiltModManager(Context context) {
        prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        preferenceListener = (sharedPreferences, key) -> {
            if (KEY_PAUSE_MENU_ONLY.equals(key) ||
                    (key != null && key.startsWith(KEY_OVERLAY_SHOW_EVERYWHERE_PREFIX))) {
                overlayVisibilityRevision.incrementAndGet();
            }
        };
        prefs.registerOnSharedPreferenceChangeListener(preferenceListener);
    }

    public static InbuiltModManager getInstance(Context context) {
        if (instance == null) {
            synchronized (InbuiltModManager.class) {
                if (instance == null) {
                    instance = new InbuiltModManager(context.getApplicationContext());
                }
            }
        }
        return instance;
    }

    public int getAutoSprintKeybind() {
        return prefs.getInt(KEY_AUTOSPRINT_KEY, KeyEvent.KEYCODE_CTRL_LEFT);
    }

    public void setAutoSprintKeybind(int keyCode) {
        prefs.edit().putInt(KEY_AUTOSPRINT_KEY, keyCode).apply();
    }

    public int getSpeedMode() {
        return Math.max(0, Math.min(3,
                prefs.getInt(KEY_SPEED_MODE, DEFAULT_SPEED_MODE)));
    }

    public void setSpeedMode(int mode) {
        prefs.edit().putInt(KEY_SPEED_MODE, Math.max(0, Math.min(3, mode))).apply();
    }

    public int getSpeedValue() {
        return Math.max(0, Math.min(1000,
                prefs.getInt(KEY_SPEED_VALUE, DEFAULT_SPEED_VALUE)));
    }

    public void setSpeedValue(int value) {
        prefs.edit().putInt(KEY_SPEED_VALUE, Math.max(0, Math.min(1000, value))).apply();
    }

    public int getSpeedTickSpeed() {
        return Math.max(0, Math.min(10,
                prefs.getInt(KEY_SPEED_TICK_SPEED, DEFAULT_SPEED_TICK_SPEED)));
    }

    public void setSpeedTickSpeed(int ticks) {
        prefs.edit().putInt(KEY_SPEED_TICK_SPEED, Math.max(0, Math.min(10, ticks))).apply();
    }

    public int getSpeedJumpType() {
        return Math.max(0, Math.min(2,
                prefs.getInt(KEY_SPEED_JUMP_TYPE, DEFAULT_SPEED_JUMP_TYPE)));
    }

    public void setSpeedJumpType(int type) {
        prefs.edit().putInt(KEY_SPEED_JUMP_TYPE, Math.max(0, Math.min(2, type))).apply();
    }

    public int getSpeedJumpHeight() {
        return Math.max(0, Math.min(100,
                prefs.getInt(KEY_SPEED_JUMP_HEIGHT, DEFAULT_SPEED_JUMP_HEIGHT)));
    }

    public void setSpeedJumpHeight(int height) {
        prefs.edit().putInt(KEY_SPEED_JUMP_HEIGHT, Math.max(0, Math.min(100, height))).apply();
    }

    public boolean isSpeedAvoidCheck() {
        return prefs.getBoolean(KEY_SPEED_AVOID_CHECK, DEFAULT_SPEED_AVOID_CHECK);
    }

    public void setSpeedAvoidCheck(boolean enabled) {
        prefs.edit().putBoolean(KEY_SPEED_AVOID_CHECK, enabled).apply();
    }

    public int getSpeedAvoidDelay() {
        return Math.max(100, Math.min(3000,
                prefs.getInt(KEY_SPEED_AVOID_DELAY, DEFAULT_SPEED_AVOID_DELAY)));
    }

    public void setSpeedAvoidDelay(int ms) {
        prefs.edit().putInt(KEY_SPEED_AVOID_DELAY, Math.max(100, Math.min(3000, ms))).apply();
    }

    public boolean isSpeedDebug() {
        return prefs.getBoolean(KEY_SPEED_DEBUG, DEFAULT_SPEED_DEBUG);
    }

    public void setSpeedDebug(boolean enabled) {
        prefs.edit().putBoolean(KEY_SPEED_DEBUG, enabled).apply();
    }

    public int getAimAssistRange() {
        return Math.max(3, Math.min(20,
                prefs.getInt(KEY_AIM_ASSIST_RANGE, DEFAULT_AIM_ASSIST_RANGE)));
    }

    public void setAimAssistRange(int range) {
        prefs.edit().putInt(KEY_AIM_ASSIST_RANGE,
                Math.max(3, Math.min(20, range))).apply();
    }

    public int getAimAssistFov() {
        return Math.max(30, Math.min(360,
                prefs.getInt(KEY_AIM_ASSIST_FOV, DEFAULT_AIM_ASSIST_FOV)));
    }

    public void setAimAssistFov(int fov) {
        prefs.edit().putInt(KEY_AIM_ASSIST_FOV,
                Math.max(30, Math.min(360, fov))).apply();
    }

    public boolean isAimAssistWallCheck() {
        return prefs.getBoolean(KEY_AIM_ASSIST_WALL_CHECK, DEFAULT_AIM_ASSIST_WALL_CHECK);
    }

    public void setAimAssistWallCheck(boolean enabled) {
        prefs.edit().putBoolean(KEY_AIM_ASSIST_WALL_CHECK, enabled).apply();
    }

    public boolean isAimAssistTargetPlayers() {
        return prefs.getBoolean(KEY_AIM_ASSIST_TARGET_PLAYERS, DEFAULT_AIM_ASSIST_TARGET_PLAYERS);
    }

    public void setAimAssistTargetPlayers(boolean enabled) {
        prefs.edit().putBoolean(KEY_AIM_ASSIST_TARGET_PLAYERS, enabled).apply();
    }

    public boolean isAimAssistTargetMobs() {
        return prefs.getBoolean(KEY_AIM_ASSIST_TARGET_MOBS, DEFAULT_AIM_ASSIST_TARGET_MOBS);
    }

    public void setAimAssistTargetMobs(boolean enabled) {
        prefs.edit().putBoolean(KEY_AIM_ASSIST_TARGET_MOBS, enabled).apply();
    }

    public int getAimAssistBodyPart() {
        return Math.max(0, Math.min(3,
                prefs.getInt(KEY_AIM_ASSIST_BODY_PART, DEFAULT_AIM_ASSIST_BODY_PART)));
    }

    public void setAimAssistBodyPart(int bodyPart) {
        prefs.edit().putInt(KEY_AIM_ASSIST_BODY_PART,
                Math.max(0, Math.min(3, bodyPart))).apply();
    }

    public boolean isEspShowPlayers() {
        return prefs.getBoolean(KEY_ESP_SHOW_PLAYERS, DEFAULT_ESP_SHOW_PLAYERS);
    }

    public void setEspShowPlayers(boolean enabled) {
        prefs.edit().putBoolean(KEY_ESP_SHOW_PLAYERS, enabled).apply();
    }

    public boolean isEspShowMobs() {
        return prefs.getBoolean(KEY_ESP_SHOW_MOBS, DEFAULT_ESP_SHOW_MOBS);
    }

    public void setEspShowMobs(boolean enabled) {
        prefs.edit().putBoolean(KEY_ESP_SHOW_MOBS, enabled).apply();
    }

    public int getEspRange() {
        return Math.max(4, Math.min(96,
                prefs.getInt(KEY_ESP_RANGE, DEFAULT_ESP_RANGE)));
    }

    public void setEspRange(int range) {
        prefs.edit().putInt(KEY_ESP_RANGE,
                Math.max(4, Math.min(96, range))).apply();
    }

    public int getEspPlayerColor() {
        return prefs.getInt(KEY_ESP_PLAYER_COLOR, DEFAULT_ESP_PLAYER_COLOR);
    }

    public void setEspPlayerColor(int argb) {
        prefs.edit().putInt(KEY_ESP_PLAYER_COLOR, argb).apply();
    }

    public int getEspMobColor() {
        return prefs.getInt(KEY_ESP_MOB_COLOR, DEFAULT_ESP_MOB_COLOR);
    }

    public void setEspMobColor(int argb) {
        prefs.edit().putInt(KEY_ESP_MOB_COLOR, argb).apply();
    }

    public int getAimAssistMaxSpeed() {
        return Math.max(100, Math.min(1500,
                prefs.getInt(KEY_AIM_ASSIST_MAX_SPEED, DEFAULT_AIM_ASSIST_MAX_SPEED)));
    }

    public void setAimAssistMaxSpeed(int degPerSecond) {
        prefs.edit().putInt(KEY_AIM_ASSIST_MAX_SPEED,
                Math.max(100, Math.min(1500, degPerSecond))).apply();
    }

    public int getAimAssistSmoothing() {
        return Math.max(1, Math.min(40,
                prefs.getInt(KEY_AIM_ASSIST_SMOOTHING, DEFAULT_AIM_ASSIST_SMOOTHING)));
    }

    public void setAimAssistSmoothing(int value) {
        prefs.edit().putInt(KEY_AIM_ASSIST_SMOOTHING,
                Math.max(1, Math.min(40, value))).apply();
    }

    public int getAimAssistAcceleration() {
        return Math.max(200, Math.min(10000,
                prefs.getInt(KEY_AIM_ASSIST_ACCEL, DEFAULT_AIM_ASSIST_ACCEL)));
    }

    public void setAimAssistAcceleration(int value) {
        prefs.edit().putInt(KEY_AIM_ASSIST_ACCEL,
                Math.max(200, Math.min(10000, value))).apply();
    }

    public int getAimAssistDeadzone() {
        return Math.max(0, Math.min(50,
                prefs.getInt(KEY_AIM_ASSIST_DEADZONE, DEFAULT_AIM_ASSIST_DEADZONE)));
    }

    public void setAimAssistDeadzone(int hundredthsDeg) {
        prefs.edit().putInt(KEY_AIM_ASSIST_DEADZONE,
                Math.max(0, Math.min(50, hundredthsDeg))).apply();
    }

    public int getAimAssistFullRange() {
        return Math.max(1, Math.min(45,
                prefs.getInt(KEY_AIM_ASSIST_FULL_RANGE, DEFAULT_AIM_ASSIST_FULL_RANGE)));
    }

    public void setAimAssistFullRange(int deg) {
        prefs.edit().putInt(KEY_AIM_ASSIST_FULL_RANGE,
                Math.max(1, Math.min(45, deg))).apply();
    }

    public int getAimAssistSticky() {
        return Math.max(0, Math.min(20,
                prefs.getInt(KEY_AIM_ASSIST_STICKY, DEFAULT_AIM_ASSIST_STICKY)));
    }

    public void setAimAssistSticky(int ticks) {
        prefs.edit().putInt(KEY_AIM_ASSIST_STICKY,
                Math.max(0, Math.min(20, ticks))).apply();
    }

    public int getAimAssistGrace() {
        return Math.max(0, Math.min(30,
                prefs.getInt(KEY_AIM_ASSIST_GRACE, DEFAULT_AIM_ASSIST_GRACE)));
    }

    public void setAimAssistGrace(int frames) {
        prefs.edit().putInt(KEY_AIM_ASSIST_GRACE,
                Math.max(0, Math.min(30, frames))).apply();
    }

    public boolean isAimAssistPreferClosest() {
        return prefs.getBoolean(KEY_AIM_ASSIST_PREFER_CLOSEST, DEFAULT_AIM_ASSIST_PREFER_CLOSEST);
    }

    public void setAimAssistPreferClosest(boolean enabled) {
        prefs.edit().putBoolean(KEY_AIM_ASSIST_PREFER_CLOSEST, enabled).apply();
    }

    public int getAimAssistStrength() {
        return Math.max(0, Math.min(100,
                prefs.getInt(KEY_AIM_ASSIST_STRENGTH, DEFAULT_AIM_ASSIST_STRENGTH)));
    }

    public void setAimAssistStrength(int strength) {
        prefs.edit().putInt(KEY_AIM_ASSIST_STRENGTH,
                Math.max(0, Math.min(100, strength))).apply();
    }

    public boolean isAimAssistLegitMode() {
        return prefs.getBoolean(KEY_AIM_ASSIST_LEGIT, DEFAULT_AIM_ASSIST_LEGIT);
    }

    public void setAimAssistLegitMode(boolean enabled) {
        prefs.edit().putBoolean(KEY_AIM_ASSIST_LEGIT, enabled).apply();
    }

    public boolean isTriggerBotEnabled() {
        return prefs.getBoolean(KEY_TRIGGER_BOT_ENABLED, DEFAULT_TRIGGER_BOT_ENABLED);
    }

    public void setTriggerBotEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_TRIGGER_BOT_ENABLED, enabled).apply();
    }

    public int getTriggerBotDelay() {
        int value = prefs.getInt(KEY_TRIGGER_BOT_DELAY, DEFAULT_TRIGGER_BOT_DELAY);
        if (value < 120) {
            value = 280;
            prefs.edit().putInt(KEY_TRIGGER_BOT_DELAY, value).apply();
        }
        return Math.max(120, Math.min(400, value));
    }

    public void setTriggerBotDelay(int delayMs) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_DELAY,
                Math.max(120, Math.min(400, delayMs))).apply();
    }

    public int getTriggerBotReactionJitter() {
        return Math.max(0, Math.min(300,
                prefs.getInt(KEY_TRIGGER_BOT_REACTION_JITTER, DEFAULT_TRIGGER_BOT_REACTION_JITTER)));
    }

    public void setTriggerBotReactionJitter(int jitterMs) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_REACTION_JITTER,
                Math.max(0, Math.min(300, jitterMs))).apply();
    }

    /** Дальность триггера в десятых долях блока (30 = 3.0 блока). */
    public int getTriggerBotRangeRaw() {
        return Math.max(20, Math.min(50,
                prefs.getInt(KEY_TRIGGER_BOT_RANGE, DEFAULT_TRIGGER_BOT_RANGE)));
    }

    public float getTriggerBotRange() {
        return getTriggerBotRangeRaw() / 10.0f;
    }

    public void setTriggerBotRange(int tenths) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_RANGE,
                Math.max(20, Math.min(50, tenths))).apply();
    }

    /** Допуск по хитбоксу в сотых долях блока (3 = 0.03 блока). */
    public int getTriggerBotToleranceRaw() {
        return Math.max(0, Math.min(25,
                prefs.getInt(KEY_TRIGGER_BOT_TOLERANCE, DEFAULT_TRIGGER_BOT_TOLERANCE)));
    }

    public float getTriggerBotTolerance() {
        return getTriggerBotToleranceRaw() / 100.0f;
    }

    public void setTriggerBotTolerance(int hundredths) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_TOLERANCE,
                Math.max(0, Math.min(25, hundredths))).apply();
    }

    public boolean isTriggerBotTargetPlayers() {
        return prefs.getBoolean(KEY_TRIGGER_BOT_TARGET_PLAYERS, DEFAULT_TRIGGER_BOT_TARGET_PLAYERS);
    }

    public void setTriggerBotTargetPlayers(boolean enabled) {
        prefs.edit().putBoolean(KEY_TRIGGER_BOT_TARGET_PLAYERS, enabled).apply();
    }

    public boolean isTriggerBotTargetMobs() {
        return prefs.getBoolean(KEY_TRIGGER_BOT_TARGET_MOBS, DEFAULT_TRIGGER_BOT_TARGET_MOBS);
    }

    public void setTriggerBotTargetMobs(boolean enabled) {
        prefs.edit().putBoolean(KEY_TRIGGER_BOT_TARGET_MOBS, enabled).apply();
    }

    public boolean isTriggerBotHumanization() {
        return prefs.getBoolean(KEY_TRIGGER_BOT_HUMANIZATION, DEFAULT_TRIGGER_BOT_HUMANIZATION);
    }

    public void setTriggerBotHumanization(boolean enabled) {
        prefs.edit().putBoolean(KEY_TRIGGER_BOT_HUMANIZATION, enabled).apply();
    }

    public int getTriggerBotInterval() {
        return Math.max(200, Math.min(1200,
                prefs.getInt(KEY_TRIGGER_BOT_INTERVAL, DEFAULT_TRIGGER_BOT_INTERVAL)));
    }

    public void setTriggerBotInterval(int intervalMs) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_INTERVAL,
                Math.max(200, Math.min(1200, intervalMs))).apply();
    }

    public int getTriggerBotIntervalJitter() {
        return Math.max(0, Math.min(400,
                prefs.getInt(KEY_TRIGGER_BOT_INTERVAL_JITTER, DEFAULT_TRIGGER_BOT_INTERVAL_JITTER)));
    }

    public void setTriggerBotIntervalJitter(int jitterMs) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_INTERVAL_JITTER,
                Math.max(0, Math.min(400, jitterMs))).apply();
    }

    public int getTriggerBotSkipChance() {
        return Math.max(0, Math.min(50,
                prefs.getInt(KEY_TRIGGER_BOT_SKIP_CHANCE, DEFAULT_TRIGGER_BOT_SKIP_CHANCE)));
    }

    public void setTriggerBotSkipChance(int percent) {
        prefs.edit().putInt(KEY_TRIGGER_BOT_SKIP_CHANCE,
                Math.max(0, Math.min(50, percent))).apply();
    }

    public boolean isTriggerBotOnlyCrit() {
        return prefs.getBoolean(KEY_TRIGGER_BOT_ONLY_CRIT, DEFAULT_TRIGGER_BOT_ONLY_CRIT);
    }

    public void setTriggerBotOnlyCrit(boolean enabled) {
        prefs.edit().putBoolean(KEY_TRIGGER_BOT_ONLY_CRIT, enabled).apply();
    }

    public int getDefaultInbuiltModKeybind(String modId) {
        if (ModIds.ZOOM.equals(modId)) return KeyEvent.KEYCODE_C;
        if (ModIds.AUTO_SPRINT.equals(modId)) return KeyEvent.KEYCODE_CTRL_LEFT;
        if (ModIds.SPEED.equals(modId)) return KeyEvent.KEYCODE_V;
        if (ModIds.AIM_ASSIST.equals(modId)) return KeyEvent.KEYCODE_B;
        if (ModIds.TRIGGER_BOT.equals(modId)) return KeyEvent.KEYCODE_G;
        if (ModIds.ESP.equals(modId)) return KeyEvent.KEYCODE_N;
        if (ModIds.SNAPLOOK.equals(modId)) return KeyEvent.KEYCODE_X;
        return KeyEvent.KEYCODE_UNKNOWN;
    }

    public int getInbuiltModKeybind(String modId) {
        if (ModIds.ZOOM.equals(modId)) return getZoomKeybind();
        if (ModIds.AUTO_SPRINT.equals(modId)) return getAutoSprintKeybind();
        return prefs.getInt(KEY_INBUILT_KEYBIND_PREFIX + modId, getDefaultInbuiltModKeybind(modId));
    }

    public void setInbuiltModKeybind(String modId, int keyCode) {
        if (modId == null || modId.isEmpty()) return;
        int value = Math.max(KeyEvent.KEYCODE_UNKNOWN, keyCode);
        if (ModIds.ZOOM.equals(modId)) setZoomKeybind(value);
        else if (ModIds.AUTO_SPRINT.equals(modId)) setAutoSprintKeybind(value);
        else prefs.edit().putInt(KEY_INBUILT_KEYBIND_PREFIX + modId, value).apply();
    }

    private String sharedOverlaySettingsId(String modId) {
        if (modId != null && modId.startsWith(ModIds.HOTBAR_SLOT + ":")) return ModIds.HOTBAR_SLOT;
        return modId;
    }

    public int getOverlayButtonSize(String modId) {
        String key = KEY_OVERLAY_BUTTON_SIZE_PREFIX + modId;
        if (prefs.contains(key)) return prefs.getInt(key, DEFAULT_OVERLAY_BUTTON_SIZE);
        String sharedId = sharedOverlaySettingsId(modId);
        if (sharedId != null && !sharedId.equals(modId)) {
            return prefs.getInt(KEY_OVERLAY_BUTTON_SIZE_PREFIX + sharedId, DEFAULT_OVERLAY_BUTTON_SIZE);
        }
        return DEFAULT_OVERLAY_BUTTON_SIZE;
    }

    public void setOverlayButtonSize(String modId, int sizeDp) {
        prefs.edit().putInt(KEY_OVERLAY_BUTTON_SIZE_PREFIX + modId, sizeDp).apply();
    }

    public int getOverlayOpacity(String modId) {
        String key = KEY_OVERLAY_OPACITY_PREFIX + modId;
        if (prefs.contains(key)) return prefs.getInt(key, DEFAULT_OVERLAY_OPACITY);
        String sharedId = sharedOverlaySettingsId(modId);
        if (sharedId != null && !sharedId.equals(modId)) {
            return prefs.getInt(KEY_OVERLAY_OPACITY_PREFIX + sharedId, DEFAULT_OVERLAY_OPACITY);
        }
        return DEFAULT_OVERLAY_OPACITY;
    }

    public void setOverlayOpacity(String modId, int opacity) {
        prefs.edit().putInt(KEY_OVERLAY_OPACITY_PREFIX + modId, Math.max(0, Math.min(100, opacity))).apply();
    }

    public boolean isModMenuEnabled() {
        return prefs.getBoolean(KEY_MOD_MENU_ENABLED, true);
    }

    public void setModMenuEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_MOD_MENU_ENABLED, enabled).apply();
    }

    public boolean isLauncherHudHideEnabled() {
        return prefs.getBoolean(KEY_HIDE_LAUNCHER_HUD, true);
    }

    public void setLauncherHudHideEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_HIDE_LAUNCHER_HUD, enabled).apply();
    }

    public boolean isNotificationsEnabled() {
        return prefs.getBoolean(KEY_NOTIFICATIONS_ENABLED, true);
    }

    public void setNotificationsEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_NOTIFICATIONS_ENABLED, enabled).apply();
    }

    public int getModMenuOpacity() {
        return Math.max(MIN_MOD_MENU_OPACITY, prefs.getInt(KEY_MOD_MENU_OPACITY, DEFAULT_OVERLAY_OPACITY));
    }

    public void setModMenuOpacity(int opacity) {
        prefs.edit().putInt(KEY_MOD_MENU_OPACITY, Math.max(MIN_MOD_MENU_OPACITY, Math.min(100, opacity))).apply();
    }

    public int getModMenuButtonOpacity() {
        return prefs.getInt(KEY_MOD_MENU_BUTTON_OPACITY, DEFAULT_OVERLAY_OPACITY);
    }

    public void setModMenuButtonOpacity(int opacity) {
        prefs.edit().putInt(KEY_MOD_MENU_BUTTON_OPACITY, Math.max(0, Math.min(100, opacity))).apply();
    }

    public boolean isModMenuCompact() {
        return prefs.getBoolean(KEY_MOD_MENU_COMPACT, false);
    }

    public void setModMenuCompact(boolean compact) {
        prefs.edit().putBoolean(KEY_MOD_MENU_COMPACT, compact).apply();
    }

    public int getModMenuKeybind() {
        return prefs.getInt(KEY_MOD_MENU_KEYBIND, KeyEvent.KEYCODE_M);
    }

    public void setModMenuKeybind(int keyCode) {
        prefs.edit().putInt(KEY_MOD_MENU_KEYBIND, Math.max(KeyEvent.KEYCODE_UNKNOWN, keyCode)).apply();
    }

    public boolean isPauseMenuOnly() {
        return prefs.getBoolean(KEY_PAUSE_MENU_ONLY, false);
    }

    public void setPauseMenuOnly(boolean enabled) {
        prefs.edit().putBoolean(KEY_PAUSE_MENU_ONLY, enabled).apply();
    }

    public Set<String> getFavoriteModKeys() {
        return new HashSet<>(prefs.getStringSet(KEY_FAVORITE_MOD_KEYS, new HashSet<>()));
    }

    public void setModFavorite(String favoriteKey, boolean favorite) {
        if (favoriteKey == null || favoriteKey.isEmpty()) return;
        Set<String> favorites = getFavoriteModKeys();
        if (favorite) {
            favorites.add(favoriteKey);
        } else {
            favorites.remove(favoriteKey);
        }
        prefs.edit().putStringSet(KEY_FAVORITE_MOD_KEYS, favorites).apply();
    }

    public boolean resolveInbuiltModEnabled(String modId, boolean defaultEnabled) {
        if (modId == null || modId.isEmpty()) return defaultEnabled;
        return prefs.getBoolean(KEY_INBUILT_MOD_ENABLED_PREFIX + modId, defaultEnabled);
    }

    public void setInbuiltModEnabled(String modId, boolean enabled) {
        if (modId == null || modId.isEmpty()) return;
        prefs.edit().putBoolean(KEY_INBUILT_MOD_ENABLED_PREFIX + modId, enabled).apply();
    }

    public boolean resolveExternalModuleEnabled(String moduleId, boolean defaultEnabled) {
        if (moduleId == null || moduleId.isEmpty()) return defaultEnabled;
        String key = KEY_EXTERNAL_MODULE_ENABLED_PREFIX + moduleId;
        if (!prefs.contains(key)) {
            prefs.edit().putBoolean(key, defaultEnabled).apply();
            return defaultEnabled;
        }
        return prefs.getBoolean(key, defaultEnabled);
    }

    public void setExternalModuleEnabled(String moduleId, boolean enabled) {
        if (moduleId == null || moduleId.isEmpty()) return;
        prefs.edit().putBoolean(KEY_EXTERNAL_MODULE_ENABLED_PREFIX + moduleId, enabled).apply();
    }

    public int getZoomLevel() {
        try {
            return prefs.getInt(KEY_ZOOM_LEVEL, DEFAULT_ZOOM_LEVEL);
        } catch (ClassCastException e) {
            prefs.edit().remove(KEY_ZOOM_LEVEL).apply();
            return DEFAULT_ZOOM_LEVEL;
        }
    }

    public void setZoomLevel(int level) {
        prefs.edit().putInt(KEY_ZOOM_LEVEL, Math.max(-20, Math.min(100, level))).apply();
    }

    public int getZoomKeybind() {
        return prefs.getInt(KEY_ZOOM_KEYBIND, KeyEvent.KEYCODE_C);
    }

    public void setZoomKeybind(int keyCode) {
        prefs.edit().putInt(KEY_ZOOM_KEYBIND, keyCode).apply();
    }

    public int getZoomTransitionDuration() {
        return prefs.getInt(KEY_ZOOM_TRANSITION_DURATION, DEFAULT_ZOOM_TRANSITION_DURATION);
    }

    public void setZoomTransitionDuration(int duration) {
        prefs.edit().putInt(KEY_ZOOM_TRANSITION_DURATION, Math.max(0, Math.min(1000, duration))).apply();
    }

    public int getCursorSensitivity() {
        return prefs.getInt(KEY_CURSOR_SENSITIVITY, DEFAULT_CURSOR_SENSITIVITY);
    }

    public void setCursorSensitivity(int sensitivity) {
        prefs.edit().putInt(KEY_CURSOR_SENSITIVITY, Math.max(10, Math.min(300, sensitivity))).apply();
    }

    public int getOverlayPositionX(String modId, int defaultX) {
        return prefs.getInt(KEY_OVERLAY_POSITION_X_PREFIX + modId, defaultX);
    }

    public int getOverlayPositionY(String modId, int defaultY) {
        return prefs.getInt(KEY_OVERLAY_POSITION_Y_PREFIX + modId, defaultY);
    }

    public void setOverlayPosition(String modId, int x, int y) {
        prefs.edit()
            .putInt(KEY_OVERLAY_POSITION_X_PREFIX + modId, x)
            .putInt(KEY_OVERLAY_POSITION_Y_PREFIX + modId, y)
            .apply();
    }

    public boolean isOverlayLocked(String modId) {
        return prefs.getBoolean(KEY_OVERLAY_LOCK_PREFIX + sharedOverlaySettingsId(modId), false);
    }

    public void setOverlayLocked(String modId, boolean locked) {
        prefs.edit().putBoolean(KEY_OVERLAY_LOCK_PREFIX + sharedOverlaySettingsId(modId), locked).apply();
    }

    public boolean isOverlayShowEverywhere(String modId) {
        return prefs.getBoolean(KEY_OVERLAY_SHOW_EVERYWHERE_PREFIX + sharedOverlaySettingsId(modId), false);
    }

    public void setOverlayShowEverywhere(String modId, boolean showEverywhere) {
        prefs.edit().putBoolean(KEY_OVERLAY_SHOW_EVERYWHERE_PREFIX + sharedOverlaySettingsId(modId), showEverywhere).apply();
    }

    public void clearOverlaySettings(String modId) {
        if (modId == null || modId.isEmpty()) return;
        prefs.edit()
                .remove(KEY_OVERLAY_BUTTON_SIZE_PREFIX + modId)
                .remove(KEY_OVERLAY_OPACITY_PREFIX + modId)
                .remove(KEY_OVERLAY_POSITION_X_PREFIX + modId)
                .remove(KEY_OVERLAY_POSITION_Y_PREFIX + modId)
                .remove(KEY_OVERLAY_LOCK_PREFIX + modId)
                .remove(KEY_OVERLAY_SHOW_EVERYWHERE_PREFIX + modId)
                .apply();
        overlayVisibilityRevision.incrementAndGet();
    }

    public long getOverlayVisibilityRevision() {
        return overlayVisibilityRevision.get();
    }

    public int getGyroSensitivityMultiplier() {
        return prefs.getInt(KEY_GYRO_SENSITIVITY_MULTIPLIER, DEFAULT_GYRO_SENSITIVITY_MULTIPLIER);
    }

    public void setGyroSensitivityMultiplier(int multiplier) {
        prefs.edit().putInt(KEY_GYRO_SENSITIVITY_MULTIPLIER, Math.max(25, Math.min(5000, multiplier))).apply();
    }

    public int getGyroSensitivityX() {
        return prefs.getInt(KEY_GYRO_SENSITIVITY_X, DEFAULT_GYRO_SENSITIVITY);
    }

    public void setGyroSensitivityX(int sensitivity) {
        prefs.edit().putInt(KEY_GYRO_SENSITIVITY_X, Math.max(10, Math.min(300, sensitivity))).apply();
    }

    public int getGyroSensitivityY() {
        return prefs.getInt(KEY_GYRO_SENSITIVITY_Y, DEFAULT_GYRO_SENSITIVITY);
    }

    public void setGyroSensitivityY(int sensitivity) {
        prefs.edit().putInt(KEY_GYRO_SENSITIVITY_Y, Math.max(10, Math.min(300, sensitivity))).apply();
    }

    public boolean isGyroInvertX() {
        return prefs.getBoolean(KEY_GYRO_INVERT_X, false);
    }

    public void setGyroInvertX(boolean invert) {
        prefs.edit().putBoolean(KEY_GYRO_INVERT_X, invert).apply();
    }

    public boolean isGyroInvertY() {
        return prefs.getBoolean(KEY_GYRO_INVERT_Y, false);
    }

    public void setGyroInvertY(boolean invert) {
        prefs.edit().putBoolean(KEY_GYRO_INVERT_Y, invert).apply();
    }

    public int getGyroDeadzone() {
        return prefs.getInt(KEY_GYRO_DEADZONE, DEFAULT_GYRO_DEADZONE);
    }

    public void setGyroDeadzone(int deadzone) {
        prefs.edit().putInt(KEY_GYRO_DEADZONE, Math.max(0, Math.min(100, deadzone))).apply();
    }
    public boolean isHotbarItemIconsEnabled() {
        return prefs.getBoolean(KEY_HOTBAR_ITEM_ICONS, false);
    }

    public void setHotbarItemIconsEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_HOTBAR_ITEM_ICONS, enabled).apply();
    }

    public boolean isHotbarItemCountsEnabled() {
        return prefs.getBoolean(KEY_HOTBAR_ITEM_COUNTS, false);
    }

    public void setHotbarItemCountsEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_HOTBAR_ITEM_COUNTS, enabled).apply();
    }

    public boolean isHotbarSlotEnabled(int slot) {
        if (slot < 1 || slot > 9) return false;
        return prefs.getBoolean(KEY_HOTBAR_SLOT_ENABLED_PREFIX + slot, true);
    }

    public void setHotbarSlotEnabled(int slot, boolean enabled) {
        if (slot < 1 || slot > 9) return;
        prefs.edit().putBoolean(KEY_HOTBAR_SLOT_ENABLED_PREFIX + slot, enabled).apply();
    }

}
