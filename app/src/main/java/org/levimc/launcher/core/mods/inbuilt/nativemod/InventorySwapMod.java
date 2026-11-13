package org.levimc.launcher.core.mods.inbuilt.nativemod;

public final class InventorySwapMod {
    private static boolean libraryLoaded;
    private static volatile boolean initialized;

    private InventorySwapMod() {}

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

    public static void initializeAsync(NativeModInit.Callback callback) {
        // Native bridge общий: он ставит ClientInstance/normalTick hooks и
        // обязан находиться на игровом потоке перед любой записью в ItemStack.
        AimAssistMod.initializeAsync(ok -> {
            initialized = ok;
            if (callback != null) callback.onResult(ok);
        });
    }

    public static boolean setEnabled(boolean value) {
        if (value && !initialized) return false;
        if (!initialized) return !value;
        loadLibrary();
        nativeSetEnabled(value);
        return true;
    }

    public static int getStatus() {
        return initialized ? nativeGetStatus() : 0;
    }

    public static int getResult() {
        return initialized ? nativeGetResult() : 0;
    }

    public static void reset() {
        if (initialized) nativeReset();
    }

    private static native void nativeSetEnabled(boolean enabled);
    private static native int nativeGetStatus();
    private static native int nativeGetResult();
    private static native void nativeReset();
}
