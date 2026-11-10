package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.view.View;
import android.widget.ImageButton;

import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;

/**
 * Compact named shortcut for modules whose on-screen output is not itself a
 * button (Name Tags, Target ESP). Tapping toggles the module; the module keeps
 * running when its shortcut is hidden in the module settings.
 */
public class ModuleToggleShortcut extends BaseOverlayButton {
    private final String modId;
    private final int normalIcon;
    private final int pressedIcon;
    private boolean active;

    public ModuleToggleShortcut(Activity activity, String modId,
                                int normalIcon, int pressedIcon) {
        super(activity);
        this.modId = modId;
        this.normalIcon = normalIcon;
        this.pressedIcon = pressedIcon;
        this.active = InbuiltModManager.getInstance(activity)
                .resolveInbuiltModEnabled(modId, false);
    }

    @Override
    protected String getModId() {
        return modId;
    }

    @Override
    protected int getIconResource() {
        return active ? pressedIcon : normalIcon;
    }

    @Override
    protected long getShowDelayMillis() {
        return 300L;
    }

    @Override
    public void tick() {
        boolean next = InbuiltModManager.getInstance(activity)
                .resolveInbuiltModEnabled(modId, false);
        if (next != active) {
            active = next;
            updateIcon();
        }
    }

    private void updateIcon() {
        if (overlayView == null) return;
        View icon = overlayView.findViewById(org.levimc.launcher.R.id.mod_overlay_button);
        if (icon instanceof ImageButton) {
            ((ImageButton) icon).setImageResource(getIconResource());
        }
    }

    @Override
    protected void onButtonClick() {
        InbuiltModManager manager = InbuiltModManager.getInstance(activity);
        boolean next = !manager.resolveInbuiltModEnabled(modId, false);
        manager.setInbuiltModEnabled(modId, next);
        InbuiltOverlayManager overlayManager = InbuiltOverlayManager.getInstance();
        if (overlayManager != null) {
            overlayManager.handleModToggle(modId, next);
        }
        active = next;
        updateIcon();
    }
}
