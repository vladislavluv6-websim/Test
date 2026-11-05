#pragma once

#include <cstdint>

// Все версионно-зависимые значения собраны здесь.
// При обновлении Minecraft меняются только сигнатуры/оффсеты в этом файле.
namespace aimbot::offsets {

inline constexpr const char *kMinecraftLibrary = "libminecraftpe.so";

// ============================================================
// СИГНАТУРЫ (ARM64, формат pl::memory::resolveSignature)
// ============================================================
inline constexpr const char *kNormalTickSignature =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39";

inline constexpr const char *kClientInstanceUpdateSignature =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "? ? ? D1 59 D0 3B D5 ? ? ? 52 F5 03 02 2A";

inline constexpr const char *kClientInstanceGetLocalPlayerSignature =
    "? ? ? D1 ? ? ? A9 ? ? ? F9 ? ? ? 91 53 D0 3B D5 E8 03 00 AA ? ? ? 91 ? ? ? F9 "
    "? ? ? 91 ? ? ? F8 ? ? ? ? ? ? ? 91 ? ? ? ? ? ? ? 36 ? ? ? 91 ? ? ? 52 ? ? ? ? "
    "? ? ? ? E0 03 1F AA ? ? ? F9 ? ? ? F8 1F 01 09 EB ? ? ? 54 ? ? ? A9 ? ? ? F9 "
    "? ? ? 91 C0 03 5F D6 ? ? ? ? C0 03 5F D6";

inline constexpr const char *kApplyTurnDeltaSignature =
    "? ? ? D1 ? ? ? FD ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 56 D0 3B D5 "
    "F3 03 00 AA F4 03 01 AA ? ? ? F9 ? ? ? F8 ? ? ? F9 ? ? ? F9";

inline constexpr const char *kFetchNearbyActorsSignature =
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "5B D0 3B D5 F3 03 08 AA F5 03 02 2A";

inline constexpr const char *kActorIsPlayerSignature =
    "? ? ? F9 ? ? ? 52 ? ? ? 72 ? ? ? A9 08 01 0B CB ? ? ? D3 ? ? ? 51 0C 01 0A 8A "
    "? ? ? B9 6B 0D 0C 8B ? ? ? F9 ? ? ? F9 ? ? ? B1 ? ? ? 54 8B 15 0B 8B ? ? ? B9 "
    "BF 01 0A 6B ? ? ? 54 ? ? ? F9 7F 01 09 EB ? ? ? 54 ? ? ? F9 ? ? ? B4 ? ? ? A9 "
    "? ? ? D3 6B 01 09 CB 5F 0D 8B EB ? ? ? 54 ? ? ? F8 ? ? ? B4 ? ? ? 92 ? ? ? 12 "
    "? ? ? 92 ? ? ? B8 28 01 08 4A ? ? ? 12 1F 01 09 6B E0 27 9F 1A C0 03 5F D6 "
    "E0 03 1F 2A C0 03 5F D6 ? ? ? D1 ? ? ? FD";

// ============================================================
// OFFSETS (Minecraft Bedrock 1.26.60, ARM64)
// ============================================================
// Actor -> StateVectorComponent*, внутри StateVectorComponent + 0 -> Vec3.
inline constexpr uintptr_t kActorStateVectorComponent = 0x228;
inline constexpr uintptr_t kStatePosition = 0;

// ActorRotationComponent: { pitch, yaw }.
// В этой версии поле является body-rotation; камеру двигаем через applyTurnDelta.
inline constexpr uintptr_t kActorRotationComponent = 0x238;

// Если в твоей сборке есть отдельный head/view-компонент, впиши его сюда.
// 0 = использовать applyTurnDelta (как в текущей 1.26.60).
inline constexpr uintptr_t kActorHeadRotation = 0;

// Если известен AABB / flags / team — заполни. 0 = фича отключена.
inline constexpr uintptr_t kActorAabb = 0;
inline constexpr uintptr_t kActorFlags = 0;
inline constexpr uintptr_t kActorTeam = 0;

inline constexpr uint64_t kFlagInvisible = 1ULL << 0;

// ActorType, который возвращает fetchNearbyActors для мобов/игроков.
inline constexpr int kActorTypeMob = 0x100;

// ============================================================
// КОНСТАНТЫ ЦЕЛИ
// ============================================================
inline constexpr float kEyeHeight = 1.62f;
// Центр хитбокса игрока над StateVector position (ноги).
inline constexpr float kCenterOffset = 0.90f;

} // namespace aimbot::offsets
