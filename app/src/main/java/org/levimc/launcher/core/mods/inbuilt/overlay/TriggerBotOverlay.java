package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.util.Log;
import android.widget.ImageButton;
import android.widget.Toast;

import org.levimc.launcher.R;
import org.levimc.launcher.core.mods.ModManager;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
import org.levimc.launcher.core.mods.inbuilt.nativemod.AimAssistMod;
import org.levimc.launcher.core.versions.GameVersion;

public class TriggerBotOverlay extends BaseOverlayButton {
    private static final String TAG = "TriggerBot";
    private static final String SUPPORTED_MINECRAFT_VERSION = "1.26.60";
    private static boolean versionLogged = false;
    private boolean isActive = false;

    public TriggerBotOverlay(Activity activity) {
        super(activity);
    }

    private boolean isSupportedVersion() {
        GameVersion version = ModManager.getInstance().getCurrentVersion();
        if (!versionLogged) {
            versionLogged = true;
            Log.i(TAG, "Minecraft version=" + (version == null ? "unknown" : version.versionCode));
        }
        if (version == null || version.versionCode == null || version.versionCode.isEmpty()) {
            return true;
        }
        return version.versionCode.startsWith(SUPPORTED_MINECRAFT_VERSION);
    }

    @Override
    protected String getModId() {
        return ModIds.TRIGGER_BOT;
    }

    @Override
    protected int getIconResource() {
        return isActive ? R.drawable.ic_trigger_bot_pressed : R.drawable.ic_trigger_bot_normal;
    }

    private void applyNativeSettings(InbuiltModManager manager) {
        AimAssistMod.setRange(manager.getAimAssistRange());
        AimAssistMod.setTriggerBot(true);
        AimAssistMod.setTriggerDelay(manager.getTriggerBotDelay());
    }

    @Override
    public void show(int startX, int startY) {
        if (!isActive && isSupportedVersion()) {
            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            isActive = AimAssistMod.initialize();
            if (isActive) {
                applyNativeSettings(manager);
            }
        }
        super.show(startX, startY);
    }

    @Override
    protected void onButtonClick() {
        boolean nextState = !isActive;
        if (nextState) {
            if (!isSupportedVersion()) {
                Toast.makeText(activity, R.string.aim_assist_mod_unavailable, Toast.LENGTH_SHORT).show();
                nextState = false;
            } else if (!AimAssistMod.initialize()) {
                Toast.makeText(activity, R.string.aim_assist_mod_unavailable, Toast.LENGTH_SHORT).show();
                nextState = false;
            } else {
                applyNativeSettings(InbuiltModManager.getInstance(activity));
            }
        } else {
            AimAssistMod.setTriggerBot(false);
        }
        isActive = nextState;
        updateButtonState(isActive);
    }

    private void updateButtonState(boolean active) {
        if (overlayView != null) {
            ImageButton btn = overlayView.findViewById(R.id.mod_overlay_button);
            if (btn != null) {
                btn.setAlpha(getButtonOpacity());
                btn.setImageResource(active ? R.drawable.ic_trigger_bot_pressed : R.drawable.ic_trigger_bot_normal);
            }
        }
    }

    @Override
    public void applyConfigurationChanges() {
        super.applyConfigurationChanges();
        if (isActive) {
            applyNativeSettings(InbuiltModManager.getInstance(activity));
        }
    }

    @Override
    public void hide() {
        if (isActive) {
            AimAssistMod.setTriggerBot(false);
            isActive = false;
            updateButtonState(false);
        }
        super.hide();
    }
}
