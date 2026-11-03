package org.levimc.launcher.core.mods.inbuilt.nativemod;

public final class SpeedMod {
    private static boolean libraryLoaded;
    private static boolean initialized;
    private static long lastInitAttempt;

    private SpeedMod() {}

    private static synchronized boolean loadLibrary() {
        if (libraryLoaded) return true;
        try {
            System.loadLibrary("speedmod");
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

    public static boolean setEnabled(boolean enabled, float multiplier) {
        if (enabled && !initialize()) return false;
        if (!initialized) return !enabled;
        setMultiplier(multiplier);
        nativeSetEnabled(enabled);
        return true;
    }

    public static void setMultiplier(float multiplier) {
        if (initialized) nativeSetMultiplier(multiplier);
    }

    public static boolean isEnabled() {
        return initialized && nativeIsEnabled();
    }

    public static boolean isInitialized() {
        return initialized;
    }

    private static native boolean nativeInit();
    private static native void nativeSetEnabled(boolean enabled);
    private static native boolean nativeIsEnabled();
    private static native void nativeSetMultiplier(float multiplier);
}
