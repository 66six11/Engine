# Studio Play Session And Extension Gates Design

## Intent

Define the entry conditions for Play Session and managed extension work while keeping the current Studio roadmap focused on editor-framework infrastructure. This document prevents premature native ABI, plugin hot reload, script VM, or renderer ownership work.

## Play Session Entry Conditions

- Edit World and Play World identity/remap rules are documented.
- Enter Play creates a copy/load of the edit scene.
- Exiting Play does not dirty the edit scene.
- Applying changes back to Edit World is explicit and transaction-backed.
- Scene View selection remains edit-world selection.
- Game View debug selection uses an explicit remap.

## Managed Plugin Entry Conditions

- Background activity and diagnostics surfaces exist.
- Command execution can route failures and long-running work.
- Contribution ids are stable and diffable.
- ALC clean unload and negative unload smoke designs exist.
- Reload failure keeps previous valid contribution state or disables with diagnostics.
- Native bridge is not introduced until a CPU-only bridge consumer exists.

## Deferrals

- No gameplay ScriptHost under `Asharia.Studio.*`.
- No raw plugin-created Avalonia controls in v0.
- No plugin-owned C++ pointers.
- No native Vulkan viewport in the managed bridge stage.

## Source-Informed Direction

- Unity Play Mode and domain reload patterns require explicit state reset and restoration rules; Studio Play Session must treat editor state and play state as separate lifetimes.
- Unreal Play In Editor patterns keep play/testing flows inside the editor but still distinguish editor world state from simulation state; Studio should make remap rules visible before adding Game View interactions.
- Godot `@tool` scripts show that code running inside an editor needs strict lifecycle and diagnostics because failed editor-time code can destabilize authoring state.
- .NET `AssemblyLoadContext` unloadability is cooperative, so plugin reload cannot be trusted until leak-negative unload smokes exist.

## Non-Goals

- No runtime gameplay scripting host in this stage.
- No plugin hot reload implementation in this stage.
- No native scene, renderer, or RHI bridge in this stage.
- No plugin-created raw windows or arbitrary Avalonia controls in v0.
