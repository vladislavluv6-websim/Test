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

public class TriggerBotOverlay extends BaseOverlayButton {
    private static final String TAG = "TriggerBot";
    private static final String SUPPORTED_MINECRAFT_VERSION = "1.26.60";
    private static boolean versionLogged = false;

    private final Handler triggerHandler = new Handler(Looper.getMainLooper());
    private final Random random = new Random();
    private boolean isActive = false;
    private long targetSince = 0L;
    private long nextAttackAt = 0L;

    private final Runnable triggerRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isActive) {
                return;
            }
            if (activity.isFinishing() || activity.isDestroyed()) {
                return;
            }
            if (PreloaderInput.isHudScreenOpen() || PreloaderInput.isPauseMenuOpen()
                    || PreloaderInput.isShowingMenu()) {
                targetSince = 0L;
                triggerHandler.postDelayed(this, 100L);
                return;
            }
            if (!(activity instanceof MinecraftActivity)) {
                triggerHandler.postDelayed(this, 100L);
                return;
            }

            long now = SystemClock.uptimeMillis();
            if (!AimAssistMod.isOnTarget()) {
                targetSince = 0L;
                triggerHandler.postDelayed(this, 30L);
                return;
            }
            if (targetSince == 0L) {
                targetSince = now;
            }

            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            int reaction = manager.getTriggerBotDelay() + random.nextInt(90);
            if (now - targetSince >= reaction && now >= nextAttackAt) {
                // Cooldown is deliberately in the sword-swing range with jitter,
                // and some swings are skipped, so attack intervals never look
                // machine-perfect to ML/Polar autoclicker checks.
                nextAttackAt = now + 520L + random.nextInt(420);
                targetSince = 0L;
                if (random.nextInt(100) >= 12) {
                    ((MinecraftActivity) activity).performInbuiltAttack();
                }
            }
            triggerHandler.postDelayed(this, 25L);
        }
    };

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
        AimAssistMod.setTriggerDelay(manager.getTriggerBotDelay());
        AimAssistMod.setTriggerBot(true);
    }

    @Override
    public void show(int startX, int startY) {
        if (!isActive && isSupportedVersion() && AimAssistMod.initialize()) {
            isActive = true;
            applyNativeSettings(InbuiltModManager.getInstance(activity));
            startLoop();
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
                startLoop();
            }
        } else {
            stopLoop();
            AimAssistMod.setTriggerBot(false);
        }
        isActive = nextState;
        updateButtonState(isActive);
    }

    private void startLoop() {
        triggerHandler.removeCallbacks(triggerRunnable);
        targetSince = 0L;
        nextAttackAt = 0L;
        triggerHandler.post(triggerRunnable);
    }

    private void stopLoop() {
        triggerHandler.removeCallbacks(triggerRunnable);
        targetSince = 0L;
        nextAttackAt = 0L;
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
        stopLoop();
        if (isActive) {
            AimAssistMod.setTriggerBot(false);
            isActive = false;
            updateButtonState(false);
        }
        super.hide();
    }
}
