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
    private boolean wantActive = false;
    private boolean initPending = false;
    private long targetSince = 0L;
    private long nextAttackAt = 0L;

    // Меч в Bedrock: cooldown ~620 мс. Держим интервал чуть выше него
    // со случайным разбросом, чтобы CPS не был константным.
    private static final long ATTACK_INTERVAL_BASE_MS = 620L;
    private static final int ATTACK_INTERVAL_JITTER_MS = 170;
    private static final int SKIP_CHANCE_PERCENT = 6;

    private final Runnable triggerRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isActive) {
                return;
            }
            if (activity.isFinishing() || activity.isDestroyed()) {
                return;
            }
            InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
            if (PreloaderInput.isHudScreenOpen() || PreloaderInput.isPauseMenuOpen()
                    || PreloaderInput.isShowingMenu()
                    || (overlayManager != null && overlayManager.isModMenuShowing())) {
                // Меню клиента тоже перекрывает игру — не бьём сквозь него.
                targetSince = 0L;
                triggerHandler.postDelayed(this, 100L);
                return;
            }
            if (!(activity instanceof MinecraftActivity)) {
                triggerHandler.postDelayed(this, 100L);
                return;
            }

            long now = SystemClock.uptimeMillis();

            // isOnTarget теперь считается нативно по пересечению луча
            // прицела с хитбоксом (игроки + мобы), а не по цели aim assist.
            boolean onTarget = AimAssistMod.isOnTarget();
            if (!onTarget) {
                targetSince = 0L;
                triggerHandler.postDelayed(this, 20L);
                return;
            }
            
            // Цель обнаружена
            if (targetSince == 0L) {
                targetSince = now;
            }

            InbuiltModManager manager = InbuiltModManager.getInstance(activity);
            int reaction = manager.getTriggerBotDelay() + random.nextInt(70);
            if (now - targetSince < reaction || now < nextAttackAt) {
                triggerHandler.postDelayed(this, 15L);
                return;
            }

            boolean attacked = ((MinecraftActivity) activity).performInbuiltAttack();
            if (attacked) {
                if (random.nextInt(100) < SKIP_CHANCE_PERCENT) {
                    // Пропущенный удар: живой игрок тоже иногда не кликает.
                    nextAttackAt = now + 90L + random.nextInt(120);
                } else {
                    nextAttackAt = now + ATTACK_INTERVAL_BASE_MS
                            + random.nextInt(ATTACK_INTERVAL_JITTER_MS);
                }
            } else {
                // Событие не дошло до игры — не забиваем очередь, пробуем позже.
                nextAttackAt = now + 120L;
            }
            triggerHandler.postDelayed(this, 15L);
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
        // Reach ванильной атаки в Bedrock — 3.0 блока от глаз.
        AimAssistMod.setTriggerRange(3.0f);
        // Увеличен допуск для более стабильной работы триггера.
        // Включает компенсацию интерполяции и небольшую сетевую задержку.
        AimAssistMod.setTriggerTolerance(0.05f);
        AimAssistMod.setTargetPlayers(true);
        AimAssistMod.setTargetMobs(true);
        // Humanization: реакция на новую цель + микро-тремор. Триггер
        // при этом считается по реальному лучу/хитбоксу каждый кадр.
        AimAssistMod.setHumanization(true);
        AimAssistMod.setReactionDelay(120, 80);
        AimAssistMod.setAimTremor(0.02f);
        AimAssistMod.setTriggerBot(true);
    }

    // Native init идёт в фоне (скан сигнатур занимает секунды).
    private void requestActivate(boolean fromClick) {
        wantActive = true;
        if (initPending) return;
        initPending = true;
        AimAssistMod.initializeAsync(ok -> {
            initPending = false;
            if (!wantActive || activity.isFinishing() || activity.isDestroyed()) return;
            if (ok) {
                applyNativeSettings(InbuiltModManager.getInstance(activity));
                startLoop();
                isActive = true;
            } else {
                isActive = false;
                wantActive = false;
                if (fromClick) {
                    Toast.makeText(activity, R.string.aim_assist_mod_unavailable, Toast.LENGTH_SHORT).show();
                }
            }
            updateButtonState(isActive);
        });
    }

    @Override
    public void show(int startX, int startY) {
        if (!isActive && isSupportedVersion()) {
            requestActivate(false);
        }
        super.show(startX, startY);
    }

    @Override
    protected void onButtonClick() {
        if (!isActive) {
            if (!isSupportedVersion()) {
                Toast.makeText(activity, R.string.aim_assist_mod_unavailable, Toast.LENGTH_SHORT).show();
                return;
            }
            requestActivate(true);
            return;
        }
        wantActive = false;
        stopLoop();
        AimAssistMod.setTriggerBot(false);
        isActive = false;
        updateButtonState(false);
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
        wantActive = false;
        stopLoop();
        if (isActive) {
            AimAssistMod.setTriggerBot(false);
            isActive = false;
            updateButtonState(false);
        }
        super.hide();
    }
}
