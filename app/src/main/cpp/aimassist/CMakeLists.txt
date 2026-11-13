cmake_minimum_required(VERSION 3.22)
project(aimassist LANGUAGES CXX)

add_library(aimassist SHARED
        aimassist.cpp
        AimAssist.cpp
        AntiBot.cpp
        ClientBridge.cpp
        Esp.cpp
        Fullbright.cpp
        InventorySwap.cpp
        ../common/GameEcs.cpp)

target_compile_features(aimassist PRIVATE cxx_std_20)
target_compile_options(aimassist PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        # Не участвуем в C++-исключениях: иначе в libaimassist линкуется
        # собственная копия libunwind, и любое исключение игры, проброшенное
        # через захуканную функцию, раскручивается нашей копией -> SIGSEGV
        # в DWARF CFI parser (подтверждено минидампом). Без landing pad'ов
        # игровой unwinder просто перешагивает наши кадры по .eh_frame.
        -fno-exceptions
        -funwind-tables)
target_include_directories(aimassist PRIVATE
        ../common
        ../combatstate
        ../third_party)
target_compile_definitions(aimassist PRIVATE
        ENTT_SPARSE_PAGE=2048
        ENTT_PACKED_PAGE=128)

target_link_libraries(aimassist PRIVATE preloader combatstate log dl)
target_link_options(aimassist PRIVATE -Wl,-z,max-page-size=16384)
