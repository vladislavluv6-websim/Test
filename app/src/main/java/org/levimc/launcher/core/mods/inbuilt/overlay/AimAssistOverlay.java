package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.widget.ImageButton;
import android.widget.Toast;

import org.levimc.launcher.R;
import org.levimc.launcher.core.minecraft.MinecraftActivity;
import org.levimc.launcher.core.mods.ModManager;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
import org.levimc.launcher.core.mods.inbuilt.nativemod.AimAssistMod;
import org.levimc.launcher.core.versions.GameVersion;
import org.levimc.launcher.preloader.PreloaderInput;

import java.util.Random;

public class AimAssistOverlay extends BaseOverlayButton {
    private static final String TAG = "AimAssist";
    private static final String SUPPORTED_MINECRAFT_VERSION = "1.26.60";
    private static boolean versionLogged = false;
    private boolean isActive = false;
    private final Handler triggerHandler = new Handler(Looper.getMainLooper());
    private final Random triggerRandom = new Random();
    private long nextTriggerAt = 0L;
    private final Runnable triggerRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isActive || !AimAssistMod.isEnabled()) {
                return;
            }
            if (activity.isFinishing() || activity.isDestroyed()) {
                return;
            }
            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            if (!manager.isTriggerBotEnabled()) {
                triggerHandler.postDelayed(this, 100L);
                return;
            }
            if (!PreloaderInput.isHudScreenOpen() || PreloaderInput.isPauseMenuOpen()
                    || PreloaderInput.isShowingMenu()) {
                triggerHandler.postDelayed(this, 80L);
                return;
            }
            long now = SystemClock.uptimeMillis();
            if (now >= nextTriggerAt && AimAssistMod.isOnTarget()) {
                int baseDelay = manager.getTriggerBotDelay();
                int jitter = triggerRandom.nextInt(Math.max(120, baseDelay));
                nextTriggerAt = now + baseDelay + jitter;
                if (triggerRandom.nextFloat() >= 0.18f && activity instanceof MinecraftActivity) {
                    boolean attacked = ((MinecraftActivity) activity).performInbuiltAttack();
                    if (!attacked) {
                        Log.d(TAG, "Trigger Bot attack was not consumed");
                    }
                }
            }
            triggerHandler.postDelayed(this, 35L);
        }
    };

    public AimAssistOverlay(Activity activity) {
        super(activity);
    }

    private void applyNativeSettings(InbuiltModManager manager) {
        AimAssistMod.setRange(manager.getAimAssistRange());
        AimAssistMod.setStrength(manager.getAimAssistStrength());
        AimAssistMod.setLegitMode(manager.isAimAssistLegitMode());
        AimAssistMod.setTriggerBot(manager.isTriggerBotEnabled());
        AimAssistMod.setTriggerDelay(manager.getTriggerBotDelay());
    }

    private void startTriggerLoop() {
        triggerHandler.removeCallbacks(triggerRunnable);
        triggerHandler.postDelayed(triggerRunnable, 50L);
    }

    private void stopTriggerLoop() {
        triggerHandler.removeCallbacks(triggerRunnable);
        nextTriggerAt = 0L;
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
        return ModIds.AIM_ASSIST;
    }

    @Override
    protected int getIconResource() {
        return isActive ? R.drawable.ic_aimassist_pressed : R.drawable.ic_aimassist_normal;
    }

    @Override
    public void show(int startX, int startY) {
        if (!isActive && isSupportedVersion()) {
            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            isActive = AimAssistMod.setEnabled(true,
                    manager.getAimAssistRange(), manager.getAimAssistStrength());
            if (isActive) {
                applyNativeSettings(manager);
                startTriggerLoop();
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
            } else {
                InbuiltModManager manager = InbuiltModManager.getInstance(activity);
                if (!AimAssistMod.setEnabled(true,
                        manager.getAimAssistRange(), manager.getAimAssistStrength())) {
                    Toast.makeText(activity, R.string.aim_assist_mod_unavailable, Toast.LENGTH_SHORT).show();
                    nextState = false;
                } else {
                    applyNativeSettings(manager);
                    startTriggerLoop();
                }
            }
        } else {
            stopTriggerLoop();
            AimAssistMod.setEnabled(false, 8.0f, 100.0f);
        }
        isActive = nextState;
        updateButtonState(isActive);
    }

    private void updateButtonState(boolean active) {
        if (overlayView != null) {
            ImageButton btn = overlayView.findViewById(R.id.mod_overlay_button);
            if (btn != null) {
                btn.setAlpha(getButtonOpacity());
                btn.setImageResource(active ? R.drawable.ic_aimassist_pressed : R.drawable.ic_aimassist_normal);
            }
        }
    }

    @Override
    public void applyConfigurationChanges() {
        super.applyConfigurationChanges();
        if (isActive) {
            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            applyNativeSettings(manager);
            if (manager.isTriggerBotEnabled()) {
                startTriggerLoop();
            } else {
                stopTriggerLoop();
            }
        }
    }

    @Override
    public void hide() {
        stopTriggerLoop();
        if (isActive) {
            AimAssistMod.setEnabled(false, 8.0f, 100.0f);
            isActive = false;
            updateButtonState(false);
        }
        super.hide();
    }
}
