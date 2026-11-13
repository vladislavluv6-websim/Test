package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.ImageButton;
import android.widget.Toast;

import org.levimc.launcher.R;
import org.levimc.launcher.core.mods.ModManager;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
import org.levimc.launcher.core.mods.inbuilt.nativemod.InventorySwapMod;
import org.levimc.launcher.core.versions.GameVersion;

public class InventorySwapOverlay extends BaseOverlayButton {
    private static final String TAG = "InventorySwap";
    private static final String SUPPORTED_VERSION = "1.26.60";
    private static boolean versionLogged;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private boolean isActive;
    private boolean wantActive;
    private boolean initPending;
    private int resultAttempts;

    public InventorySwapOverlay(Activity activity) {
        super(activity);
    }

    private boolean isSupportedVersion() {
        GameVersion version = ModManager.getInstance().getCurrentVersion();
        if (!versionLogged) {
            versionLogged = true;
            Log.i(TAG, "Minecraft version=" + (version == null ? "unknown" : version.versionCode));
        }
        return version == null || version.versionCode == null || version.versionCode.isEmpty()
                || version.versionCode.startsWith(SUPPORTED_VERSION);
    }

    private void requestActivate() {
        if (initPending || wantActive) return;
        wantActive = true;
        initPending = true;
        InventorySwapMod.initializeAsync(ok -> {
            initPending = false;
            if (!wantActive || activity.isFinishing() || activity.isDestroyed()) return;
            if (!ok) {
                wantActive = false;
                updateButtonState(false);
                toast(R.string.auto_swap_unavailable);
                return;
            }
            if (!isSupportedVersion()) {
                wantActive = false;
                updateButtonState(false);
                toast(R.string.auto_swap_unavailable);
                return;
            }
            resultAttempts = 0;
            InventorySwapMod.setEnabled(true);
            pollResult();
        });
    }

    private void pollResult() {
        mainHandler.postDelayed(() -> {
            if (!wantActive || initPending || activity.isFinishing() || activity.isDestroyed()) return;
            int status = InventorySwapMod.getStatus();
            if (status == 2) {
                isActive = true;
                updateButtonState(true);
                toast(R.string.auto_swap_equipped);
                return;
            }
            if (status != 1) {
                wantActive = false;
                isActive = false;
                updateButtonState(false);
                int result = InventorySwapMod.getResult();
                switch (result) {
                    case -1 -> toast(R.string.auto_swap_no_totem);
                    case -3 -> toast(R.string.auto_swap_no_local_player);
                    default -> toast(R.string.auto_swap_container_unavailable);
                }
                return;
            }
            if (++resultAttempts < 16) {
                pollResult();
            } else {
                wantActive = false;
                isActive = false;
                updateButtonState(false);
                toast(R.string.auto_swap_container_unavailable);
            }
        }, 150L);
    }

    @Override
    protected String getModId() {
        return ModIds.INVENTORY_SWAP;
    }

    @Override
    protected int getIconResource() {
        return isActive ? R.drawable.ic_auto_swap_pressed
                : R.drawable.ic_auto_swap_normal;
    }

    @Override
    public void show(int startX, int startY) {
        if (!isActive && isSupportedVersion()
                && InbuiltModManager.getInstance(activity).resolveInbuiltModEnabled(getModId(), false)) {
            requestActivate();
        }
        super.show(startX, startY);
    }

    @Override
    protected void onButtonClick() {
        if (isActive) {
            wantActive = false;
            initPending = false;
            InventorySwapMod.setEnabled(false);
            isActive = false;
            updateButtonState(false);
            toast(R.string.auto_swap_restored);
            return;
        }
        if (!isSupportedVersion()) {
            toast(R.string.auto_swap_unavailable);
            return;
        }
        requestActivate();
    }

    private void updateButtonState(boolean active) {
        if (overlayView == null) return;
        ImageButton button = overlayView.findViewById(R.id.mod_overlay_button);
        if (button != null) {
            button.setAlpha(getButtonOpacity());
            button.setImageResource(active ? R.drawable.ic_auto_swap_pressed
                    : R.drawable.ic_auto_swap_normal);
        }
    }

    @Override
    public void hide() {
        wantActive = false;
        initPending = false;
        if (isActive) InventorySwapMod.setEnabled(false);
        isActive = false;
        updateButtonState(false);
        super.hide();
    }

    @Override
    protected boolean isShortcutActive() {
        return isActive;
    }

    @Override
    protected boolean autoActivateOnConfig() {
        return true;
    }

    @Override
    public void onModuleConfigDisabled() {
        wantActive = false;
        initPending = false;
        if (isActive) InventorySwapMod.setEnabled(false);
        isActive = false;
        updateButtonState(false);
    }

    private void toast(int res) {
        Toast.makeText(activity, res, Toast.LENGTH_SHORT).show();
    }
}
