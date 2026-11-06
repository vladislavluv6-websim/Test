package org.levimc.launcher.core.mods.inbuilt.nativemod;

public final class AimAssistMod {
    private static boolean libraryLoaded;
    private static volatile boolean initialized;
    private static long lastInitAttempt;
    private static final NativeModInit.Task INIT_TASK = new NativeModInit.Task();

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

    /**
     * Initializes on a background thread (signature scan takes seconds) and
     * reports the result on the main thread.
     */
    public static void initializeAsync(NativeModInit.Callback callback) {
        if (initialized) {
            if (callback != null) callback.onResult(true);
            return;
        }
        INIT_TASK.request(AimAssistMod::initialize, callback);
    }

    /** Requires a completed {@link #initializeAsync}; never blocks the caller. */
    public static boolean setEnabled(boolean enabled, float range, float strength) {
        if (enabled && !initialized) return false;
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

    public static void setBodyPart(int bodyPart) {
        if (initialized) nativeSetBodyPart(bodyPart);
    }

    public static void setMaxSpeed(float degPerSecond) {
        if (initialized) nativeSetMaxSpeed(degPerSecond);
    }

    public static void setSmoothing(float speed) {
        if (initialized) nativeSetSmoothing(speed);
    }

    public static void setAcceleration(float degPerSecond2) {
        if (initialized) nativeSetAcceleration(degPerSecond2);
    }

    public static void setDeadzone(float deg) {
        if (initialized) nativeSetDeadzone(deg);
    }

    public static void setFullSpeedRange(float deg) {
        if (initialized) nativeSetFullSpeedRange(deg);
    }

    public static void setStickyTicks(int ticks) {
        if (initialized) nativeSetStickyTicks(ticks);
    }

    public static void setTargetGrace(int frames) {
        if (initialized) nativeSetTargetGrace(frames);
    }

    public static void setPreferClosest(boolean enabled) {
        if (initialized) nativeSetPreferClosest(enabled);
    }

    public static void setFov(float fovDeg) {
        if (initialized) nativeSetFov(fovDeg);
    }

    public static void setWallCheck(boolean enabled) {
        if (initialized) nativeSetWallCheck(enabled);
    }

    public static void setTargetPlayers(boolean enabled) {
        if (initialized) nativeSetTargetPlayers(enabled);
    }

    public static void setTargetMobs(boolean enabled) {
        if (initialized) nativeSetTargetMobs(enabled);
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
    private static native void nativeSetFov(float fovDeg);
    private static native void nativeSetWallCheck(boolean enabled);
    private static native void nativeSetTargetPlayers(boolean enabled);
    private static native void nativeSetTargetMobs(boolean enabled);
    private static native void nativeSetLegitMode(boolean enabled);
    private static native void nativeSetBodyPart(int bodyPart);
    private static native void nativeSetMaxSpeed(float degPerSecond);
    private static native void nativeSetSmoothing(float speed);
    private static native void nativeSetAcceleration(float degPerSecond2);
    private static native void nativeSetDeadzone(float deg);
    private static native void nativeSetFullSpeedRange(float deg);
    private static native void nativeSetStickyTicks(int ticks);
    private static native void nativeSetTargetGrace(int frames);
    private static native void nativeSetPreferClosest(boolean enabled);
    private static native void nativeSetTriggerBot(boolean enabled);
    private static native void nativeSetTriggerDelay(int delayMs);
    private static native boolean nativeIsOnTarget();
    private static native void nativeSetTouchActive(boolean active);
    private static native void nativeNotifyCameraLook();
    private static native void nativeSetActivationHeld(boolean held);
}
