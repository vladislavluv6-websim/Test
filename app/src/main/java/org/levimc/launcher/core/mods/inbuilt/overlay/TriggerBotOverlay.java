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

    // Поведение атаки (берётся из настроек модуля и кешируется).
    private int reactionDelayMs = 280;
    private int reactionJitterMs = 70;
    private int attackIntervalMs = 620;
    private int attackIntervalJitterMs = 170;
    private int skipChancePercent = 6;
    private boolean humanizationEnabled = true;
    private float triggerRange = 3.0f;
    private float triggerTolerance = 0.03f;

    private final Runnable triggerRunnable = new Runnable() {
        private long lastDiagAt = 0L;

        @Override
        public void run() {
            if (!isActive) {
                return;
            }
            if (activity.isFinishing() || activity.isDestroyed()) {
                return;
            }
            InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
            boolean hudOpen = PreloaderInput.isHudScreenOpen();
            boolean pauseOpen = PreloaderInput.isPauseMenuOpen();
            boolean showingMenu = PreloaderInput.isShowingMenu();
            boolean modMenuShowing = overlayManager != null && overlayManager.isModMenuShowing();
            long diagNow = SystemClock.uptimeMillis();
            boolean diagDue = diagNow - lastDiagAt >= 1000L;
            if (diagDue) {
                lastDiagAt = diagNow;
                Log.i(TAG, "loop: hudOpen=" + hudOpen + " pause=" + pauseOpen
                        + " showingMenu=" + showingMenu + " modMenu=" + modMenuShowing
                        + " onTarget=" + AimAssistMod.isOnTarget());
            }
            // isHudScreenOpen() == идёт обычный геймплей (HUD-экран открыт).
            // Бьём только в геймплее и никогда — в меню/паузе/инвентаре.
            if (!hudOpen || pauseOpen || showingMenu || modMenuShowing) {
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
            if (!AimAssistMod.isOnTarget()) {
                targetSince = 0L;
                triggerHandler.postDelayed(this, 20L);
                return;
            }
            if (targetSince == 0L) {
                targetSince = now;
            }

            int reaction = 0;
            if (humanizationEnabled) {
                reaction = reactionDelayMs + (reactionJitterMs > 0
                        ? random.nextInt(reactionJitterMs + 1) : 0);
            }
            if (now - targetSince < reaction || now < nextAttackAt) {
                triggerHandler.postDelayed(this, 15L);
                return;
            }

            // Удар уходит нативно: GameMode::attack(Actor&) через vtable игры.
            // Синтетических касаний экрана больше нет, поэтому раскладка
            // управления (в т.ч. контрол «прыжок» в центре) ни на что не влияет.
            boolean attacked = AimAssistMod.attack();
            if (diagDue) {
                Log.i(TAG, "attack dispatched=" + attacked
                        + " nativeOk=" + AimAssistMod.lastAttackOk());
            }
            if (attacked) {
                if (skipChancePercent > 0 && random.nextInt(100) < skipChancePercent) {
                    // Пропущенный удар: живой игрок тоже иногда не кликает.
                    nextAttackAt = now + 90L + random.nextInt(120);
                } else {
                    int jitter = attackIntervalJitterMs > 0
                            ? random.nextInt(attackIntervalJitterMs + 1) : 0;
                    nextAttackAt = now + attackIntervalMs + jitter;
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
        triggerRange = manager.getTriggerBotRange();
        triggerTolerance = manager.getTriggerBotTolerance();
        reactionDelayMs = manager.getTriggerBotDelay();
        reactionJitterMs = manager.getTriggerBotReactionJitter();
        attackIntervalMs = manager.getTriggerBotInterval();
        attackIntervalJitterMs = manager.getTriggerBotIntervalJitter();
        skipChancePercent = manager.getTriggerBotSkipChance();
        humanizationEnabled = manager.isTriggerBotHumanization();

        // Настройки самого триггера (не трогаем параметры aim assist, чтобы
        // модули не перезаписывали друг друга).
        AimAssistMod.setTriggerDelay(reactionDelayMs);
        AimAssistMod.setTriggerRange(triggerRange);
        AimAssistMod.setTriggerTolerance(triggerTolerance);
        AimAssistMod.setTargetPlayers(manager.isTriggerBotTargetPlayers());
        AimAssistMod.setTargetMobs(manager.isTriggerBotTargetMobs());
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
                isActive = true;
                startLoop();
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
