#pragma once

// ============================================================================
//  Extension points for your own modules
// ============================================================================
// This build contains NO version-specific hooks or memory patches. The old
// ones were written for Minecraft 1.21 and do not work on 1.26.60.4, so they
// were removed. The GUI, menu and config system are fully intact.
//
// Use the three functions below to plug your own modules in.
//
//   OnMinecraftLoaded(handle) - called ONCE in the Minecraft process, right
//                               after libminecraftpe.so is loaded. `handle` is
//                               the dlopen() handle; use dlsym(handle, "...")
//                               to resolve symbols and DobbyHook/A64HookFunction
//                               to hook them.
//
//   OnFrame()                 - called every frame while the menu is drawn.
//
//   CustomMenu()              - draw your own widgets inside the menu window.
// ============================================================================

inline void OnMinecraftLoaded(void* handle) {
    (void)handle;
    // Example:
    //   void* fn = dlsym(handle, "SomeFunction");
    //   DobbyHook(fn, (void*)myDetour, (void**)&orig);
}

inline void OnFrame() {
    // Example:
    //   if (myModuleEnabled) { ... }
}

inline void CustomMenu() {
    // Example:
    //   ImGui::Text("My modules");
    //   ImGui::Checkbox("My module", &myModuleEnabled);
}
