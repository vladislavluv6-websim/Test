package org.levimc.launcher.core.mods.inbuilt.nativemod;

public final class AimAssistMod {
    private static boolean libraryLoaded;
    private static boolean initialized;
    private static long lastInitAttempt;

    private AimAssistMod() {}

    private static synchronized boolean loadLibrary() {
        if (libraryLoaded) return true;
        try {
            System.loadLibrary("aimassist");
            libraryLoaded = true;
        } catch (UnsatisfiedLinkError ignored) {
            libraryLoaded = false;
        }
        return libraryLoaded;
    }

    public static synchronized boolean initialize() {
        if (initialized) return true;
        long now = System.currentTimeMillis();
        if (now - lastInitAttempt < 2000) return false;
        lastInitAttempt = now;
        if (!loadLibrary()) return false;
        initialized = nativeInit();
        return initialized;
    }

    public static boolean setEnabled(boolean enabled, float range, float strength) {
        if (enabled && !initialize()) return false;
        if (!initialized) return !enabled;
        setRange(range);
        setStrength(strength);
        nativeSetEnabled(enabled);
        return true;
    }

    public static void setRange(float range) {
        if (initialized) nativeSetRange(range);
    }

    public static void setStrength(float strength) {
        if (initialized) nativeSetStrength(strength);
    }

    public static void setLegitMode(boolean enabled) {
        if (initialized) nativeSetLegitMode(enabled);
    }

    public static void setTriggerBot(boolean enabled) {
        if (initialized) nativeSetTriggerBot(enabled);
    }

    public static void setTriggerDelay(int delayMs) {
        if (initialized) nativeSetTriggerDelay(delayMs);
    }

    public static boolean isOnTarget() {
        return initialized && nativeIsOnTarget();
    }

    public static boolean isEnabled() {
        return initialized && nativeIsEnabled();
    }

    public static void setTouchActive(boolean active) {
        if (initialized) nativeSetTouchActive(active);
    }

    public static void notifyCameraLook() {
        if (initialized) nativeNotifyCameraLook();
    }

    public static void setActivationHeld(boolean held) {
        if (initialized) nativeSetActivationHeld(held);
    }

    public static boolean isInitialized() {
        return initialized;
    }

    private static native boolean nativeInit();
    private static native void nativeSetEnabled(boolean enabled);
    private static native boolean nativeIsEnabled();
    private static native void nativeSetRange(float range);
    private static native void nativeSetStrength(float strength);
    private static native void nativeSetLegitMode(boolean enabled);
    private static native void nativeSetTriggerBot(boolean enabled);
    private static native void nativeSetTriggerDelay(int delayMs);
    private static native boolean nativeIsOnTarget();
    private static native void nativeSetTouchActive(boolean active);
    private static native void nativeNotifyCameraLook();
    private static native void nativeSetActivationHeld(boolean held);
}
