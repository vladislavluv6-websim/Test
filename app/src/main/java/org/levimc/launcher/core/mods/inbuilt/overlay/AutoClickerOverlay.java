package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.widget.ImageButton;

import org.levimc.launcher.R;
import org.levimc.launcher.core.minecraft.MinecraftActivity;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
import org.levimc.launcher.preloader.PreloaderInput;

import java.util.Random;

/**
 * AutoClicker: пока включён, непрерывно бьёт (тап по центру экрана) с
 * заданным CPS. В отличие от TriggerBot не ждёт цель в прицеле и не
 * требует нативного модуля — работает на любой версии игры.
 */
public class AutoClickerOverlay extends BaseOverlayButton {
    private static final int MIN_CPS = 1;
    private static final int MAX_CPS = 20;

    private final Handler clickHandler = new Handler(Looper.getMainLooper());
    private final Random random = new Random();
    private boolean isActive = false;

    private final Runnable clickRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isActive || activity.isFinishing() || activity.isDestroyed()) {
                return;
            }
            InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
            // Кликаем только в геймплее: не бьём сквозь меню клиента,
            // паузу и экраны инвентаря.
            if (!PreloaderInput.isHudScreenOpen() || PreloaderInput.isPauseMenuOpen()
                    || PreloaderInput.isShowingMenu()
                    || (overlayManager != null && overlayManager.isModMenuShowing())) {
                clickHandler.postDelayed(this, 100L);
                return;
            }
            if (!(activity instanceof MinecraftActivity)) {
                clickHandler.postDelayed(this, 100L);
                return;
            }

            long now = SystemClock.uptimeMillis();
            boolean clicked = ((MinecraftActivity) activity).performInbuiltAttack();

            int cps = InbuiltModManager.getInstance(activity).getAutoClickerCps();
            cps = Math.max(MIN_CPS, Math.min(MAX_CPS, cps));
            long interval = 1000L / cps;
            // ±25% разброс, чтобы CPS не был константным.
            long jitter = interval > 4
                    ? (long) random.nextInt((int) (interval / 2) + 1) - interval / 4
                    : 0L;
            // Если тап не ушёл (предыдущий синтетический палец ещё зажат),
            // пробуем снова скоро, а не ждём целый интервал.
            clickHandler.postDelayed(this, clicked ? Math.max(15L, interval + jitter) : 60L);
        }
    };

    public AutoClickerOverlay(Activity activity) {
        super(activity);
    }

    @Override
    protected String getModId() {
        return ModIds.AUTO_CLICKER;
    }

    @Override
    protected int getIconResource() {
        return isActive ? R.drawable.ic_auto_clicker_pressed : R.drawable.ic_auto_clicker_normal;
    }

    @Override
    public void show(int startX, int startY) {
        // «Просто при включении бьёт»: показ кнопки = кликер работает.
        if (!isActive) {
            isActive = true;
            startLoop();
        }
        super.show(startX, startY);
        updateButtonState(isActive);
    }

    @Override
    protected void onButtonClick() {
        isActive = !isActive;
        if (isActive) {
            startLoop();
        } else {
            stopLoop();
        }
        updateButtonState(isActive);
    }

    private void startLoop() {
        clickHandler.removeCallbacks(clickRunnable);
        clickHandler.post(clickRunnable);
    }

    private void stopLoop() {
        clickHandler.removeCallbacks(clickRunnable);
    }

    private void updateButtonState(boolean active) {
        if (overlayView != null) {
            ImageButton btn = overlayView.findViewById(R.id.mod_overlay_button);
            if (btn != null) {
                btn.setAlpha(getButtonOpacity());
                btn.setImageResource(active
                        ? R.drawable.ic_auto_clicker_pressed
                        : R.drawable.ic_auto_clicker_normal);
            }
        }
    }

    @Override
    public void hide() {
        isActive = false;
        stopLoop();
        super.hide();
    }
}
