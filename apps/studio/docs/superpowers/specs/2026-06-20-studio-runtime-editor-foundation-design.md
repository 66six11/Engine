# Studio Runtime Editor Foundation Design

## Intent

Build the shared Studio UI and data-flow foundation needed for runtime-editor features while deferring full Scene authoring, Play Mode, native ABI, and plugin hot reload.

## Decisions

- Studio remains an Avalonia presentation host, not the owner of engine world or renderer lifetime.
- Feature modules contribute descriptors and view models; Shell owns workbench orchestration.
- Background work, diagnostics, dialogs, commands, shortcuts, and status feedback are shared UI infrastructure.
- Hierarchy and Inspector continue through `ISceneSnapshotProvider` until a real scene bridge exists.
- The first real scene bridge is read-only.
- Writable Inspector fields require transaction, dirty-state, validation, and schema/editor metadata.
- Play Session starts only after Edit World / Play World copy or load semantics are testable.
- Managed plugin reload starts only after ALC unload smokes and contribution lifecycle diagnostics exist.
- UI-bound state changes from background work must marshal through the Avalonia UI dispatcher.

Unity's domain reload model shows why Edit/Play transitions need explicit state reset and restoration rules. Unity's serialization model reinforces that saved data shape affects editor reconstruction, so Studio needs schema/editor metadata before writable fields. Unreal's Play In Editor model keeps testing inside the editor while still separating editor state from simulation state, and Unreal editor modules show why editor-only contributions should stay separate from runtime modules. Godot's `@tool` scripts show that editor-running code needs strict lifecycle and diagnostics because failures can destabilize authoring. O3DE's reflection contexts separate serialized data, editor-facing metadata, and behavior/script exposure; Studio should follow that separation before writable Inspector work. .NET `AssemblyLoadContext` unloading is cooperative, so plugin reload needs leak-negative unload checks before it is trusted.

## Non-Goals

- No native C ABI in this stage.
- No Avalonia-owned Vulkan viewport.
- No raw plugin-created Avalonia controls.
- No script VM or runtime gameplay ScriptHost.
- No direct mutable C++ object pointer in Studio view models.

## Entry Gates

- Background activity service exists and is visible in the Studio shell.
- Command execution can report success, failure, disabled state, and long-running progress.
- Problems and Console can receive structured diagnostics from UI-level operations.
- Hierarchy and Inspector consume the same read-only snapshot provider.
- Transaction and Play Session follow-up slices have explicit smoke evidence.

## References

- Unity Domain Reloading: https://docs.unity3d.com/6000.0/Documentation/Manual/domain-reloading.html
- Unity Script Serialization: https://docs.unity3d.com/2022.3/Documentation/Manual/script-Serialization.html
- Unreal Editor Modules: https://dev.epicgames.com/documentation/unreal-engine/setting-up-editor-modules-for-customizing-the-editor-in-unreal-engine
- Unreal Play In Editor: https://dev.epicgames.com/documentation/unreal-engine/ineditor-testing-play-and-simulate-in-unreal-engine
- Godot `@tool`: https://docs.godotengine.org/en/stable/tutorials/plugins/running_code_in_the_editor.html
- O3DE Reflection Contexts: https://www.docs.o3de.org/docs/user-guide/programming/components/reflection/
- .NET AssemblyLoadContext unloadability: https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability
- Avalonia Threading: https://docs.avaloniaui.net/docs/app-development/threading
