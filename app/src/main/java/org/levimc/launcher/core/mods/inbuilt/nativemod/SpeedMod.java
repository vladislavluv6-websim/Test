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

    public static void setSpeedMode(int mode) {
        if (initialized) nativeSetSpeedMode(mode);
    }

    public static void setSpeedValue(int raw) {
        if (initialized) nativeSetSpeedValue(raw);
    }

    public static void setTickSpeed(int ticks) {
        if (initialized) nativeSetTickSpeed(ticks);
    }

    public static void setJumpType(int type) {
        if (initialized) nativeSetJumpType(type);
    }

    public static void setJumpHeight(int raw) {
        if (initialized) nativeSetJumpHeight(raw);
    }

    public static void setAvoidCheck(boolean enabled) {
        if (initialized) nativeSetAvoidCheck(enabled);
    }

    public static void setAvoidDelay(int ms) {
        if (initialized) nativeSetAvoidDelay(ms);
    }

    public static void setDebug(boolean enabled) {
        if (initialized) nativeSetDebug(enabled);
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
    private static native void nativeSetSpeedMode(int mode);
    private static native void nativeSetSpeedValue(int raw);
    private static native void nativeSetTickSpeed(int ticks);
    private static native void nativeSetJumpType(int type);
    private static native void nativeSetJumpHeight(int raw);
    private static native void nativeSetAvoidCheck(boolean enabled);
    private static native void nativeSetAvoidDelay(int ms);
    private static native void nativeSetDebug(boolean enabled);
}
