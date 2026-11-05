#include <jni.h>

#include <android/log.h>

#include "AimAssist.hpp"
#include "ClientBridge.hpp"

namespace {

constexpr const char *kTag = "AimAssistJni";

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeInit(
    JNIEnv *, jclass) {
  const bool ok = aimbot::ClientBridge::instance().initialize();
  if (ok) {
    aimbot::AimAssist::instance().onConfigLoad();
  }
  __android_log_print(ANDROID_LOG_INFO, kTag, "nativeInit=%d", ok ? 1 : 0);
  return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetEnabled(
    JNIEnv *, jclass, jboolean enabled) {
  aimbot::AimAssist::instance().setEnabled(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeIsEnabled(
    JNIEnv *, jclass) {
  return aimbot::AimAssist::instance().isEnabled() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetRange(
    JNIEnv *, jclass, jfloat range) {
  aimbot::AimAssist::instance().setRange(range);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetStrength(
    JNIEnv *, jclass, jfloat strength) {
  aimbot::AimAssist::instance().setStrength(strength);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetFov(
    JNIEnv *, jclass, jfloat fovDeg) {
  aimbot::AimAssist::instance().setFov(fovDeg);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetWallCheck(
    JNIEnv *, jclass, jboolean enabled) {
  aimbot::AimAssist::instance().setWallCheck(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTargetPlayers(
    JNIEnv *, jclass, jboolean enabled) {
  aimbot::AimAssist::instance().setTargetPlayers(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTargetMobs(
    JNIEnv *, jclass, jboolean enabled) {
  aimbot::AimAssist::instance().setTargetMobs(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetLegitMode(
    JNIEnv *, jclass, jboolean enabled) {
  aimbot::AimAssist::instance().setLegitMode(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTriggerBot(
    JNIEnv *, jclass, jboolean enabled) {
  aimbot::AimAssist::instance().setTriggerBot(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTriggerDelay(
    JNIEnv *, jclass, jint delayMs) {
  aimbot::AimAssist::instance().setTriggerDelay(delayMs);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeIsOnTarget(
    JNIEnv *, jclass) {
  return aimbot::AimAssist::instance().isOnTarget() ? JNI_TRUE : JNI_FALSE;
}

// Дополнительные точки входа для input-слоя клиента.
extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTouchActive(
    JNIEnv *, jclass, jboolean active) {
  aimbot::AimAssist::instance().setTouchActive(active == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeNotifyCameraLook(
    JNIEnv *, jclass) {
  aimbot::AimAssist::instance().notifyCameraLook();
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetActivationHeld(
    JNIEnv *, jclass, jboolean held) {
  aimbot::AimAssist::instance().setActivationHeld(held == JNI_TRUE);
}
