package org.levimc.launcher.core.mods.inbuilt;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import org.levimc.launcher.R;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
import org.levimc.launcher.core.mods.inbuilt.overlay.InbuiltOverlayManager;
import org.levimc.launcher.core.mods.inbuilt.overlay.MoreButtonsEditor;

import java.util.ArrayList;
import java.util.List;

public final class InbuiltModuleProvider {
    private static final String GROUP_ID = "inbuilt";
    private static final String MOD_ID = "inbuilt";

    private static final String CFG_OVERLAY_SIZE = "overlay_size";
    private static final String CFG_OVERLAY_OPACITY = "overlay_opacity";
    private static final String CFG_OVERLAY_LOCK = "overlay_lock";
    private static final String CFG_OVERLAY_SHOW_EVERYWHERE = "overlay_show_everywhere";
    private static final String CFG_HIDE_LAUNCHER_HUD = "hide_launcher_hud";
    private static final String CFG_ACTIVATION_KEYBIND = "activation_keybind";
    private static final String CFG_EDIT_CONTROLS = "edit_controls";
    private static final String CFG_AUTO_SPRINT_KEYBIND = "auto_sprint_keybind";
    private static final String CFG_SPEED_MULTIPLIER = "speed_multiplier";
    private static final String CFG_AIM_ASSIST_RANGE = "aim_assist_range";
    private static final String CFG_AIM_ASSIST_STRENGTH = "aim_assist_strength";
    private static final String CFG_AIM_ASSIST_LEGIT = "aim_assist_legit";
    private static final String CFG_AIM_ASSIST_FOV = "aim_assist_fov";
    private static final String CFG_AIM_ASSIST_WALL_CHECK = "aim_assist_wall_check";
    private static final String CFG_AIM_ASSIST_TARGET_PLAYERS = "aim_assist_target_players";
    private static final String CFG_AIM_ASSIST_TARGET_MOBS = "aim_assist_target_mobs";
    private static final String CFG_AIM_ASSIST_BODY_PART = "aim_assist_body_part";
    private static final String CFG_AIM_ASSIST_MAX_SPEED = "aim_assist_max_speed";
    private static final String CFG_AIM_ASSIST_SMOOTHING = "aim_assist_smoothing";
    private static final String CFG_AIM_ASSIST_ACCEL = "aim_assist_accel";
    private static final String CFG_AIM_ASSIST_DEADZONE = "aim_assist_deadzone";
    private static final String CFG_AIM_ASSIST_FULL_RANGE = "aim_assist_full_range";
    private static final String CFG_AIM_ASSIST_STICKY = "aim_assist_sticky";
    private static final String CFG_AIM_ASSIST_GRACE = "aim_assist_grace";
    private static final String CFG_AIM_ASSIST_PREFER_CLOSEST = "aim_assist_prefer_closest";
    private static final String CFG_ESP_SHOW_PLAYERS = "esp_show_players";
    private static final String CFG_ESP_SHOW_MOBS = "esp_show_mobs";
    private static final String CFG_ESP_RANGE = "esp_range";
    private static final String CFG_ESP_PLAYER_COLOR = "esp_player_color";
    private static final String CFG_ESP_MOB_COLOR = "esp_mob_color";
    private static final String CFG_TRIGGER_BOT_DELAY = "trigger_bot_delay";
    private static final String CFG_CURSOR_SENSITIVITY = "cursor_sensitivity";
    private static final String CFG_ZOOM_LEVEL = "zoom_level";
    private static final String CFG_ZOOM_TRANSITION = "zoom_transition";
    private static final String CFG_ZOOM_KEYBIND = "zoom_keybind";
    private static final String CFG_GYRO_SENSITIVITY_MULTIPLIER = "gyro_sensitivity_multiplier";
    private static final String CFG_GYRO_SENSITIVITY_X = "gyro_sensitivity_x";
    private static final String CFG_GYRO_SENSITIVITY_Y = "gyro_sensitivity_y";
    private static final String CFG_GYRO_INVERT_X = "gyro_invert_x";
    private static final String CFG_GYRO_INVERT_Y = "gyro_invert_y";
    private static final String CFG_GYRO_DEADZONE = "gyro_deadzone";
    private static final String CFG_HOTBAR_ITEM_ICONS = "hotbar_item_icons";
    private static final String CFG_HOTBAR_ITEM_COUNTS = "hotbar_item_counts";
    private static final String CFG_HOTBAR_SLOT_PREFIX = "hotbar_slot_";
    private static final String CFG_HOTBAR_SLOT_ENABLED = "enabled";
    private static final String CFG_HOTBAR_SLOT_SIZE = "size";
    private static final String CFG_HOTBAR_SLOT_OPACITY = "opacity";

    private InbuiltModuleProvider() {
    }

    public static List<UnifiedMod> load(Activity activity) {
        InbuiltModManager manager = InbuiltModManager.getInstance(activity);
        InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
        String groupName = activity.getString(R.string.mod_menu_group_inbuilt);
        List<UnifiedMod> mods = new ArrayList<>();

        mods.add(create(activity, manager, overlayManager, ModIds.QUICK_DROP,
                R.string.inbuilt_mod_quick_drop, R.string.inbuilt_mod_quick_drop_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.CAMERA_PERSPECTIVE,
                R.string.inbuilt_mod_camera, R.string.inbuilt_mod_camera_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.TOGGLE_HUD,
                R.string.inbuilt_mod_hud, R.string.inbuilt_mod_hud_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.AUTO_SPRINT,
                R.string.inbuilt_mod_autosprint, R.string.inbuilt_mod_autosprint_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.SPEED,
                R.string.inbuilt_mod_speed, R.string.inbuilt_mod_speed_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.AIM_ASSIST,
                R.string.inbuilt_mod_aim_assist, R.string.inbuilt_mod_aim_assist_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.ESP,
                R.string.inbuilt_mod_esp, R.string.inbuilt_mod_esp_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.TRIGGER_BOT,
                R.string.inbuilt_mod_trigger_bot, R.string.inbuilt_mod_trigger_bot_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.CHICK_PET,
                R.string.inbuilt_mod_chick_pet, R.string.inbuilt_mod_chick_pet_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.ZOOM,
                R.string.inbuilt_mod_zoom, R.string.inbuilt_mod_zoom_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.FPS_DISPLAY,
                R.string.inbuilt_mod_fps_display, R.string.inbuilt_mod_fps_display_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.CPS_DISPLAY,
                R.string.inbuilt_mod_cps_display, R.string.inbuilt_mod_cps_display_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.SNAPLOOK,
                R.string.inbuilt_mod_snaplook, R.string.inbuilt_mod_snaplook_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.VIRTUAL_CURSOR,
                R.string.inbuilt_mod_virtual_cursor, R.string.inbuilt_mod_virtual_cursor_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.GYRO,
                R.string.inbuilt_mod_gyro, R.string.inbuilt_mod_gyro_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.POJAV_CONTROLS,
                R.string.inbuilt_mod_pojav_controls, R.string.inbuilt_mod_pojav_controls_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.MORE_BUTTONS,
                R.string.inbuilt_mod_more_buttons, R.string.inbuilt_mod_more_buttons_desc,
                groupName));
        mods.add(create(activity, manager, overlayManager, ModIds.HOTBAR_SLOT,
                R.string.inbuilt_mod_hotbar_slot, R.string.inbuilt_mod_hotbar_slot_desc,
                groupName));

        return mods;
    }

    private static UnifiedMod create(Activity activity, InbuiltModManager manager,
                                     InbuiltOverlayManager overlayManager, String id,
                                     int nameRes, int descRes, String groupName) {
        boolean active = overlayManager != null
                ? overlayManager.isModActive(id)
                : manager.resolveInbuiltModEnabled(id, false);
        boolean customConfig = ModIds.MORE_BUTTONS.equals(id);
        UnifiedMod result = new UnifiedMod(
                id,
                activity.getString(nameRes),
                activity.getString(descRes),
                MOD_ID,
                UnifiedMod.Source.INBUILT,
                active,
                createConfigs(activity, manager, id),
                customConfig,
                GROUP_ID,
                groupName,
                (mod, enabled) -> setEnabled(manager, mod, enabled),
                (mod, config, value) -> setConfig(manager, mod, config, value),
                ModIds.MORE_BUTTONS.equals(id) ? mod -> MoreButtonsEditor.show(activity) : null
        );
        result.setLocalConfigSchema(createLocalConfigSchema(activity, result));
        return result;
    }

    private static RuntimeConfigSchema createLocalConfigSchema(Context context, UnifiedMod mod) {
        String modId = mod.getId();
        if (ModIds.CHICK_PET.equals(modId) || ModIds.MORE_BUTTONS.equals(modId)) return null;
        boolean hotbar = ModIds.HOTBAR_SLOT.equals(modId);
        boolean gyro = ModIds.GYRO.equals(modId);
        boolean zoom = ModIds.ZOOM.equals(modId);
        boolean cursor = ModIds.VIRTUAL_CURSOR.equals(modId);
        boolean pojav = ModIds.POJAV_CONTROLS.equals(modId);
        boolean aim = ModIds.AIM_ASSIST.equals(modId);
        boolean esp = ModIds.ESP.equals(modId);
        try {
            JSONArray categories = new JSONArray();
            JSONArray nodes = new JSONArray();
            String defaultCategory = "behavior";
            if (hotbar) {
                defaultCategory = "slots";
                categories.put(configCategory(context, "slots", R.string.mod_config_category_slots));
                categories.put(configCategory(context, "appearance", R.string.mod_config_category_appearance));
                nodes.put(configNode(mod, CFG_HOTBAR_ITEM_ICONS, "slots"));
                nodes.put(configNode(mod, CFG_HOTBAR_ITEM_COUNTS, "slots"));
                JSONArray slots = new JSONArray();
                for (int slot = 1; slot <= 9; slot++) {
                    String key = hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_ENABLED);
                    slots.put(new JSONObject().put("key", key).put("value", key)
                            .put("label", mod.findConfigEntry(key).displayName));
                    String section = "slot_" + slot;
                    nodes.put(new JSONObject().put("id", section).put("type", "section")
                            .put("category", "appearance").put("collapsible", true)
                            .put("title", context.getString(R.string.mod_config_hotbar_slot_section, slot)));
                    nodes.put(configNode(mod, hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_SIZE), "appearance")
                            .put("section", section));
                    nodes.put(configNode(mod, hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_OPACITY), "appearance")
                            .put("section", section));
                }
                nodes.put(new JSONObject().put("id", "visible_slots").put("type", "toggle_group")
                        .put("category", "slots").put("title", context.getString(R.string.mod_config_visible_slots))
                        .put("options", slots));
            } else if (aim) {
                defaultCategory = "aim_target";
                categories.put(configCategory(context, "aim_target",
                        R.string.mod_config_category_aim_target));
                categories.put(configCategory(context, "aim_motion",
                        R.string.mod_config_category_aim_motion));
                categories.put(configCategory(context, "appearance",
                        R.string.mod_config_category_appearance));
            } else if (esp) {
                defaultCategory = "appearance";
                categories.put(configCategory(context, "appearance",
                        R.string.mod_config_category_appearance));
            } else if (!pojav) {
                if (gyro || cursor || zoom) {
                    defaultCategory = gyro || cursor ? "motion" : "zoom";
                    categories.put(configCategory(context, defaultCategory,
                            zoom ? R.string.inbuilt_mod_zoom : R.string.mod_config_category_motion));
                } else {
                    defaultCategory = "appearance";
                }
                categories.put(configCategory(context, "appearance", R.string.mod_config_category_appearance));
            }
            categories.put(configCategory(context, "behavior", R.string.mod_config_category_behavior));
            for (UnifiedMod.ConfigEntry config : mod.getConfigEntries()) {
                if (hotbar && (config.key.startsWith(CFG_HOTBAR_SLOT_PREFIX)
                        || CFG_HOTBAR_ITEM_ICONS.equals(config.key) || CFG_HOTBAR_ITEM_COUNTS.equals(config.key))) {
                    continue;
                }
                String category = "behavior";
                if (CFG_OVERLAY_SIZE.equals(config.key) || CFG_OVERLAY_OPACITY.equals(config.key)) {
                    category = "appearance";
                } else if (aim && isAimMotionKey(config.key)) {
                    category = "aim_motion";
                } else if (aim && config.key.startsWith("aim_assist_")
                        && !CFG_AIM_ASSIST_LEGIT.equals(config.key)
                        && !CFG_AIM_ASSIST_WALL_CHECK.equals(config.key)) {
                    category = "aim_target";
                } else if (esp && (CFG_ESP_PLAYER_COLOR.equals(config.key)
                        || CFG_ESP_MOB_COLOR.equals(config.key))) {
                    category = "appearance";
                } else if (config.key.startsWith("gyro_") || CFG_CURSOR_SENSITIVITY.equals(config.key)) {
                    category = "motion";
                } else if (CFG_ZOOM_LEVEL.equals(config.key) || CFG_ZOOM_TRANSITION.equals(config.key)) {
                    category = "zoom";
                }
                nodes.put(configNode(mod, config.key, category));
            }
            return RuntimeConfigSchema.parse(new JSONObject().put("version", 2)
                    .put("default_category", defaultCategory)
                    .put("categories", categories).put("nodes", nodes).toString());
        } catch (JSONException e) {
            throw new IllegalStateException("Unable to build inbuilt config schema", e);
        }
    }

    private static boolean isAimMotionKey(String key) {
        return CFG_AIM_ASSIST_STRENGTH.equals(key)
                || CFG_AIM_ASSIST_MAX_SPEED.equals(key)
                || CFG_AIM_ASSIST_SMOOTHING.equals(key)
                || CFG_AIM_ASSIST_ACCEL.equals(key)
                || CFG_AIM_ASSIST_DEADZONE.equals(key)
                || CFG_AIM_ASSIST_FULL_RANGE.equals(key);
    }

    private static JSONObject configCategory(Context context, String id, int titleRes) throws JSONException {
        return new JSONObject().put("id", id).put("title", context.getString(titleRes));
    }

    private static JSONObject configNode(UnifiedMod mod, String key, String category) throws JSONException {
        UnifiedMod.ConfigEntry config = mod.findConfigEntry(key);
        String type;
        switch (config.type) {
            case TOGGLE: type = "toggle"; break;
            case KEYBIND: type = "keybind"; break;
            case BUTTON: type = "button"; break;
            case RADIO: type = "choice"; break;
            case COLOR: type = "color"; break;
            case SLIDER_FLOAT: type = "slider_float"; break;
            default: type = "slider_int"; break;
        }
        JSONObject node = new JSONObject().put("id", key).put("key", key)
                .put("category", category).put("title", config.displayName)
                .put("type", type)
                .put("default_value", config.defaultValue)
                .put("min_value", config.minValue).put("max_value", config.maxValue);
        if (config.type == UnifiedMod.ConfigType.RADIO) {
            node.put("style", "radio");
            JSONArray options = new JSONArray();
            String[] labels = config.minValue.split(",");
            for (int i = 0; i < labels.length; i++) {
                options.put(new JSONObject().put("value", String.valueOf(i))
                        .put("label", labels[i].trim()));
            }
            node.put("options", options);
        }
        if (!config.dependsOn.isEmpty()) {
            node.put("enabled_when", new JSONArray().put(new JSONObject()
                    .put("key", config.dependsOn).put("op", "truthy")));
        }
        return node;
    }

    private static List<UnifiedMod.ConfigEntry> createConfigs(Context context,
                                                              InbuiltModManager manager,
                                                              String modId) {
        List<UnifiedMod.ConfigEntry> configs = new ArrayList<>();
        if (ModIds.MORE_BUTTONS.equals(modId)) return configs;
        if (ModIds.POJAV_CONTROLS.equals(modId)) {
            configs.add(activationKeybind(context, manager, modId));
            configs.add(config(CFG_EDIT_CONTROLS,
                    context.getString(R.string.mod_config_edit_controls),
                    UnifiedMod.ConfigType.BUTTON, "", "", "", ""));
            return configs;
        }
        if (!ModIds.CHICK_PET.equals(modId)) {
            if (!ModIds.HOTBAR_SLOT.equals(modId)) {
                configs.add(config(CFG_OVERLAY_SIZE,
                        context.getString(ModIds.FPS_DISPLAY.equals(modId) || ModIds.CPS_DISPLAY.equals(modId)
                                ? R.string.mod_config_display_size_dp : R.string.mod_config_overlay_button_size_dp),
                        UnifiedMod.ConfigType.SLIDER_INT,
                        "56", "20", "100",
                        String.valueOf(manager.getOverlayButtonSize(modId))));
                configs.add(config(CFG_OVERLAY_OPACITY,
                        context.getString(R.string.mod_config_overlay_opacity_percent),
                        UnifiedMod.ConfigType.SLIDER_INT,
                        "100", "0", "100",
                        String.valueOf(manager.getOverlayOpacity(modId))));
            }
            configs.add(config(CFG_OVERLAY_LOCK,
                    context.getString(R.string.overlay_button_lock),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isOverlayLocked(modId))));
            configs.add(config(CFG_OVERLAY_SHOW_EVERYWHERE,
                    context.getString(R.string.mod_config_overlay_show_everywhere),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isOverlayShowEverywhere(modId))));
        }

        if (ModIds.TOGGLE_HUD.equals(modId)) {
            configs.add(config(CFG_HIDE_LAUNCHER_HUD,
                    context.getString(R.string.mod_config_hide_launcher_hud),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isLauncherHudHideEnabled())));
        }

        if (ModIds.HOTBAR_SLOT.equals(modId)) {
            configs.add(config(CFG_HOTBAR_ITEM_ICONS,
                    context.getString(R.string.mod_config_hotbar_item_icons),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isHotbarItemIconsEnabled())));
            configs.add(config(CFG_HOTBAR_ITEM_COUNTS,
                    context.getString(R.string.mod_config_hotbar_item_counts),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isHotbarItemCountsEnabled())));
            for (int slot = 1; slot <= 9; slot++) {
                String enabledKey = hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_ENABLED);
                String overlayKey = ModIds.HOTBAR_SLOT + ":" + slot;
                configs.add(config(enabledKey,
                        context.getString(R.string.mod_config_hotbar_slot_enabled, slot),
                        UnifiedMod.ConfigType.TOGGLE,
                        "true", "", "",
                        String.valueOf(manager.isHotbarSlotEnabled(slot))));
                configs.add(config(hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_SIZE),
                        context.getString(R.string.mod_config_hotbar_slot_size, slot),
                        UnifiedMod.ConfigType.SLIDER_INT,
                        "56", "20", "100",
                        String.valueOf(manager.getOverlayButtonSize(overlayKey)), enabledKey));
                configs.add(config(hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_OPACITY),
                        context.getString(R.string.mod_config_hotbar_slot_opacity, slot),
                        UnifiedMod.ConfigType.SLIDER_INT,
                        "100", "0", "100",
                        String.valueOf(manager.getOverlayOpacity(overlayKey)), enabledKey));
            }
        }

        if (ModIds.AUTO_SPRINT.equals(modId)) {
            configs.add(config(CFG_AUTO_SPRINT_KEYBIND,
                    context.getString(R.string.mod_config_auto_sprint_keybind),
                    UnifiedMod.ConfigType.KEYBIND,
                    String.valueOf(manager.getDefaultInbuiltModKeybind(modId)), "", "",
                    String.valueOf(manager.getAutoSprintKeybind())));
        } else if (ModIds.SPEED.equals(modId)) {
            configs.add(config(CFG_SPEED_MULTIPLIER,
                    context.getString(R.string.mod_config_speed_multiplier),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "200", "100", "500",
                    String.valueOf(manager.getSpeedMultiplier())));
        } else if (ModIds.AIM_ASSIST.equals(modId)) {
            configs.add(config(CFG_AIM_ASSIST_RANGE,
                    context.getString(R.string.mod_config_aim_assist_range),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "12", "3", "20",
                    String.valueOf(manager.getAimAssistRange())));
            configs.add(config(CFG_AIM_ASSIST_FOV,
                    context.getString(R.string.mod_config_aim_assist_fov),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "360", "30", "360",
                    String.valueOf(manager.getAimAssistFov())));
            configs.add(config(CFG_AIM_ASSIST_BODY_PART,
                    context.getString(R.string.mod_config_aim_assist_body_part),
                    UnifiedMod.ConfigType.RADIO,
                    "0",
                    context.getString(R.string.mod_config_aim_assist_head) + ","
                            + context.getString(R.string.mod_config_aim_assist_chest) + ","
                            + context.getString(R.string.mod_config_aim_assist_legs),
                    "",
                    String.valueOf(manager.getAimAssistBodyPart())));
            configs.add(config(CFG_AIM_ASSIST_TARGET_PLAYERS,
                    context.getString(R.string.mod_config_aim_assist_target_players),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isAimAssistTargetPlayers())));
            configs.add(config(CFG_AIM_ASSIST_TARGET_MOBS,
                    context.getString(R.string.mod_config_aim_assist_target_mobs),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isAimAssistTargetMobs())));
            configs.add(config(CFG_AIM_ASSIST_PREFER_CLOSEST,
                    context.getString(R.string.mod_config_aim_assist_prefer_closest),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isAimAssistPreferClosest())));
            configs.add(config(CFG_AIM_ASSIST_STICKY,
                    context.getString(R.string.mod_config_aim_assist_sticky),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "4", "0", "20",
                    String.valueOf(manager.getAimAssistSticky())));
            configs.add(config(CFG_AIM_ASSIST_GRACE,
                    context.getString(R.string.mod_config_aim_assist_grace),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "12", "0", "30",
                    String.valueOf(manager.getAimAssistGrace())));
            configs.add(config(CFG_AIM_ASSIST_MAX_SPEED,
                    context.getString(R.string.mod_config_aim_assist_max_speed),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "650", "100", "1500",
                    String.valueOf(manager.getAimAssistMaxSpeed())));
            configs.add(config(CFG_AIM_ASSIST_SMOOTHING,
                    context.getString(R.string.mod_config_aim_assist_smoothing),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "18", "1", "40",
                    String.valueOf(manager.getAimAssistSmoothing())));
            configs.add(config(CFG_AIM_ASSIST_ACCEL,
                    context.getString(R.string.mod_config_aim_assist_accel),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "2600", "200", "10000",
                    String.valueOf(manager.getAimAssistAcceleration())));
            configs.add(config(CFG_AIM_ASSIST_DEADZONE,
                    context.getString(R.string.mod_config_aim_assist_deadzone),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "10", "0", "50",
                    String.valueOf(manager.getAimAssistDeadzone())));
            configs.add(config(CFG_AIM_ASSIST_FULL_RANGE,
                    context.getString(R.string.mod_config_aim_assist_full_range),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "12", "1", "45",
                    String.valueOf(manager.getAimAssistFullRange())));
            configs.add(config(CFG_AIM_ASSIST_STRENGTH,
                    context.getString(R.string.mod_config_aim_assist_strength),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "100", "0", "100",
                    String.valueOf(manager.getAimAssistStrength())));
            configs.add(config(CFG_AIM_ASSIST_WALL_CHECK,
                    context.getString(R.string.mod_config_aim_assist_wall_check),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isAimAssistWallCheck())));
            configs.add(config(CFG_AIM_ASSIST_LEGIT,
                    context.getString(R.string.mod_config_aim_assist_legit),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isAimAssistLegitMode())));
        } else if (ModIds.ESP.equals(modId)) {
            configs.add(config(CFG_ESP_RANGE,
                    context.getString(R.string.mod_config_esp_range),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "32", "4", "96",
                    String.valueOf(manager.getEspRange())));
            configs.add(config(CFG_ESP_SHOW_PLAYERS,
                    context.getString(R.string.mod_config_esp_players),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isEspShowPlayers())));
            configs.add(config(CFG_ESP_SHOW_MOBS,
                    context.getString(R.string.mod_config_esp_mobs),
                    UnifiedMod.ConfigType.TOGGLE,
                    "true", "", "",
                    String.valueOf(manager.isEspShowMobs())));
            configs.add(config(CFG_ESP_PLAYER_COLOR,
                    context.getString(R.string.mod_config_esp_player_color),
                    UnifiedMod.ConfigType.COLOR,
                    formatColor(manager.getEspPlayerColor()), "", "",
                    formatColor(manager.getEspPlayerColor())));
            configs.add(config(CFG_ESP_MOB_COLOR,
                    context.getString(R.string.mod_config_esp_mob_color),
                    UnifiedMod.ConfigType.COLOR,
                    formatColor(manager.getEspMobColor()), "", "",
                    formatColor(manager.getEspMobColor())));
        } else if (ModIds.TRIGGER_BOT.equals(modId)) {
            configs.add(config(CFG_TRIGGER_BOT_DELAY,
                    context.getString(R.string.mod_config_trigger_bot_delay),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "250", "120", "400",
                    String.valueOf(manager.getTriggerBotDelay())));
        } else if (ModIds.VIRTUAL_CURSOR.equals(modId)) {
            configs.add(config(CFG_CURSOR_SENSITIVITY,
                    context.getString(R.string.mod_config_cursor_sensitivity_percent),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "120", "10", "200",
                    String.valueOf(manager.getCursorSensitivity())));
        } else if (ModIds.ZOOM.equals(modId)) {
            configs.add(config(CFG_ZOOM_LEVEL,
                    context.getString(R.string.mod_config_zoom_level_percent),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "10", "-20", "100",
                    String.valueOf(manager.getZoomLevel())));
            configs.add(config(CFG_ZOOM_TRANSITION,
                    context.getString(R.string.mod_config_zoom_transition),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "150", "0", "1000",
                    String.valueOf(manager.getZoomTransitionDuration())));
            configs.add(config(CFG_ZOOM_KEYBIND,
                    context.getString(R.string.mod_config_zoom_keybind),
                    UnifiedMod.ConfigType.KEYBIND,
                    String.valueOf(manager.getDefaultInbuiltModKeybind(modId)), "", "",
                    String.valueOf(manager.getZoomKeybind())));
        } else if (ModIds.GYRO.equals(modId)) {
            configs.add(config(CFG_GYRO_SENSITIVITY_MULTIPLIER,
                    context.getString(R.string.mod_config_gyro_sensitivity_multiplier),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "100", "25", "5000",
                    String.valueOf(manager.getGyroSensitivityMultiplier())));
            configs.add(config(CFG_GYRO_SENSITIVITY_X,
                    context.getString(R.string.mod_config_gyro_horizontal_speed),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "100", "10", "300",
                    String.valueOf(manager.getGyroSensitivityX())));
            configs.add(config(CFG_GYRO_SENSITIVITY_Y,
                    context.getString(R.string.mod_config_gyro_vertical_speed),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "100", "10", "300",
                    String.valueOf(manager.getGyroSensitivityY())));
            configs.add(config(CFG_GYRO_INVERT_X,
                    context.getString(R.string.mod_config_gyro_invert_x),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isGyroInvertX())));
            configs.add(config(CFG_GYRO_INVERT_Y,
                    context.getString(R.string.mod_config_gyro_invert_y),
                    UnifiedMod.ConfigType.TOGGLE,
                    "false", "", "",
                    String.valueOf(manager.isGyroInvertY())));
            configs.add(config(CFG_GYRO_DEADZONE,
                    context.getString(R.string.mod_config_gyro_small_movement_filter),
                    UnifiedMod.ConfigType.SLIDER_INT,
                    "5", "0", "100",
                    String.valueOf(manager.getGyroDeadzone())));
        }
        if (!ModIds.CHICK_PET.equals(modId) && !ModIds.AUTO_SPRINT.equals(modId) && !ModIds.ZOOM.equals(modId)) {
            configs.add(activationKeybind(context, manager, modId));
        }
        return configs;
    }

    private static UnifiedMod.ConfigEntry activationKeybind(Context context, InbuiltModManager manager, String modId) {
        return config(CFG_ACTIVATION_KEYBIND,
                context.getString(R.string.mod_config_activation_keybind),
                UnifiedMod.ConfigType.KEYBIND,
                String.valueOf(manager.getDefaultInbuiltModKeybind(modId)), "", "",
                String.valueOf(manager.getInbuiltModKeybind(modId)));
    }

    private static UnifiedMod.ConfigEntry config(String key, String displayName,
                                                 UnifiedMod.ConfigType type,
                                                 String defaultValue, String minValue,
                                                 String maxValue, String currentValue) {
        return config(key, displayName, type, defaultValue, minValue, maxValue, currentValue, "");
    }

    private static UnifiedMod.ConfigEntry config(String key, String displayName,
                                                 UnifiedMod.ConfigType type,
                                                 String defaultValue, String minValue,
                                                 String maxValue, String currentValue,
                                                 String dependsOn) {
        return new UnifiedMod.ConfigEntry(
                key, displayName, type, defaultValue, minValue, maxValue,
                currentValue, dependsOn
        );
    }

    private static String hotbarSlotConfigKey(int slot, String setting) {
        return CFG_HOTBAR_SLOT_PREFIX + slot + "_" + setting;
    }

    private static void setEnabled(InbuiltModManager manager, UnifiedMod mod, boolean enabled) {
        manager.setInbuiltModEnabled(mod.getId(), enabled);
        InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
        if (overlayManager != null) {
            overlayManager.handleModToggle(mod.getId(), enabled);
        }
    }

    private static void setConfig(InbuiltModManager manager, UnifiedMod mod, UnifiedMod.ConfigEntry config,
                                  String value) {
        if (ModIds.HOTBAR_SLOT.equals(mod.getId()) && setHotbarSlotConfig(manager, config.key, value)) return;
        switch (config.key) {
            case CFG_ACTIVATION_KEYBIND:
                manager.setInbuiltModKeybind(mod.getId(), parseInt(value, manager.getInbuiltModKeybind(mod.getId())));
                break;
            case CFG_EDIT_CONTROLS:
                InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
                if (overlayManager != null) overlayManager.openPojavControlsEditor();
                break;
            case CFG_OVERLAY_SIZE:
                manager.setOverlayButtonSize(mod.getId(), parseInt(value, manager.getOverlayButtonSize(mod.getId())));
                break;
            case CFG_OVERLAY_OPACITY:
                manager.setOverlayOpacity(mod.getId(), parseInt(value, manager.getOverlayOpacity(mod.getId())));
                break;
            case CFG_OVERLAY_LOCK:
                manager.setOverlayLocked(mod.getId(), parseBoolean(value));
                break;
            case CFG_OVERLAY_SHOW_EVERYWHERE:
                manager.setOverlayShowEverywhere(mod.getId(), parseBoolean(value));
                break;
            case CFG_HIDE_LAUNCHER_HUD:
                manager.setLauncherHudHideEnabled(parseBoolean(value));
                InbuiltOverlayManager hudManager = InbuiltOverlayManager.getInstance();
                if (hudManager != null) hudManager.refreshRuntimeVisibility();
                break;
            case CFG_AUTO_SPRINT_KEYBIND:
                manager.setAutoSprintKeybind(parseInt(value, manager.getAutoSprintKeybind()));
                break;
            case CFG_SPEED_MULTIPLIER:
                manager.setSpeedMultiplier(parseInt(value, manager.getSpeedMultiplier()));
                break;
            case CFG_AIM_ASSIST_RANGE:
                manager.setAimAssistRange(parseInt(value, manager.getAimAssistRange()));
                break;
            case CFG_AIM_ASSIST_FOV:
                manager.setAimAssistFov(parseInt(value, manager.getAimAssistFov()));
                break;
            case CFG_AIM_ASSIST_STRENGTH:
                manager.setAimAssistStrength(parseInt(value, manager.getAimAssistStrength()));
                break;
            case CFG_AIM_ASSIST_WALL_CHECK:
                manager.setAimAssistWallCheck(parseBoolean(value));
                break;
            case CFG_AIM_ASSIST_TARGET_PLAYERS:
                manager.setAimAssistTargetPlayers(parseBoolean(value));
                break;
            case CFG_AIM_ASSIST_TARGET_MOBS:
                manager.setAimAssistTargetMobs(parseBoolean(value));
                break;
            case CFG_AIM_ASSIST_BODY_PART:
                manager.setAimAssistBodyPart(parseInt(value, manager.getAimAssistBodyPart()));
                break;
            case CFG_AIM_ASSIST_MAX_SPEED:
                manager.setAimAssistMaxSpeed(parseInt(value, manager.getAimAssistMaxSpeed()));
                break;
            case CFG_AIM_ASSIST_SMOOTHING:
                manager.setAimAssistSmoothing(parseInt(value, manager.getAimAssistSmoothing()));
                break;
            case CFG_AIM_ASSIST_ACCEL:
                manager.setAimAssistAcceleration(parseInt(value, manager.getAimAssistAcceleration()));
                break;
            case CFG_AIM_ASSIST_DEADZONE:
                manager.setAimAssistDeadzone(parseInt(value, manager.getAimAssistDeadzone()));
                break;
            case CFG_AIM_ASSIST_FULL_RANGE:
                manager.setAimAssistFullRange(parseInt(value, manager.getAimAssistFullRange()));
                break;
            case CFG_AIM_ASSIST_STICKY:
                manager.setAimAssistSticky(parseInt(value, manager.getAimAssistSticky()));
                break;
            case CFG_AIM_ASSIST_GRACE:
                manager.setAimAssistGrace(parseInt(value, manager.getAimAssistGrace()));
                break;
            case CFG_AIM_ASSIST_PREFER_CLOSEST:
                manager.setAimAssistPreferClosest(parseBoolean(value));
                break;
            case CFG_AIM_ASSIST_LEGIT:
                manager.setAimAssistLegitMode(parseBoolean(value));
                break;
            case CFG_ESP_RANGE:
                manager.setEspRange(parseInt(value, manager.getEspRange()));
                break;
            case CFG_ESP_SHOW_PLAYERS:
                manager.setEspShowPlayers(parseBoolean(value));
                break;
            case CFG_ESP_SHOW_MOBS:
                manager.setEspShowMobs(parseBoolean(value));
                break;
            case CFG_ESP_PLAYER_COLOR:
                manager.setEspPlayerColor(parseColor(value, manager.getEspPlayerColor()));
                break;
            case CFG_ESP_MOB_COLOR:
                manager.setEspMobColor(parseColor(value, manager.getEspMobColor()));
                break;
            case CFG_TRIGGER_BOT_DELAY:
                manager.setTriggerBotDelay(parseInt(value, manager.getTriggerBotDelay()));
                break;
            case CFG_CURSOR_SENSITIVITY:
                manager.setCursorSensitivity(parseInt(value, manager.getCursorSensitivity()));
                break;
            case CFG_ZOOM_LEVEL:
                manager.setZoomLevel(parseInt(value, manager.getZoomLevel()));
                break;
            case CFG_ZOOM_TRANSITION:
                manager.setZoomTransitionDuration(parseInt(value, manager.getZoomTransitionDuration()));
                break;
            case CFG_ZOOM_KEYBIND:
                manager.setZoomKeybind(parseInt(value, manager.getZoomKeybind()));
                break;
            case CFG_GYRO_SENSITIVITY_MULTIPLIER:
                manager.setGyroSensitivityMultiplier(parseInt(value, manager.getGyroSensitivityMultiplier()));
                break;
            case CFG_GYRO_SENSITIVITY_X:
                manager.setGyroSensitivityX(parseInt(value, manager.getGyroSensitivityX()));
                break;
            case CFG_GYRO_SENSITIVITY_Y:
                manager.setGyroSensitivityY(parseInt(value, manager.getGyroSensitivityY()));
                break;
            case CFG_GYRO_INVERT_X:
                manager.setGyroInvertX(parseBoolean(value));
                break;
            case CFG_GYRO_INVERT_Y:
                manager.setGyroInvertY(parseBoolean(value));
                break;
            case CFG_GYRO_DEADZONE:
                manager.setGyroDeadzone(parseInt(value, manager.getGyroDeadzone()));
                break;
            case CFG_HOTBAR_ITEM_ICONS:
                manager.setHotbarItemIconsEnabled(parseBoolean(value));
                break;
            case CFG_HOTBAR_ITEM_COUNTS:
                manager.setHotbarItemCountsEnabled(parseBoolean(value));
                break;
            default:
                break;
        }
    }

    private static boolean setHotbarSlotConfig(InbuiltModManager manager, String key, String value) {
        for (int slot = 1; slot <= 9; slot++) {
            String overlayKey = ModIds.HOTBAR_SLOT + ":" + slot;
            if (hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_ENABLED).equals(key)) {
                manager.setHotbarSlotEnabled(slot, parseBoolean(value));
                return true;
            }
            if (hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_SIZE).equals(key)) {
                manager.setOverlayButtonSize(overlayKey, parseInt(value, manager.getOverlayButtonSize(overlayKey)));
                return true;
            }
            if (hotbarSlotConfigKey(slot, CFG_HOTBAR_SLOT_OPACITY).equals(key)) {
                manager.setOverlayOpacity(overlayKey, parseInt(value, manager.getOverlayOpacity(overlayKey)));
                return true;
            }
        }
        return false;
    }

    private static String formatColor(int argb) {
        return String.format(java.util.Locale.US, "#%08X", argb);
    }

    private static int parseColor(String value, int fallback) {
        try {
            return Color.parseColor(value);
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private static int parseInt(String value, int fallback) {
        try {
            return Integer.parseInt(value);
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private static boolean parseBoolean(String value) {
        return "true".equalsIgnoreCase(value) || "1".equals(value);
    }
}
