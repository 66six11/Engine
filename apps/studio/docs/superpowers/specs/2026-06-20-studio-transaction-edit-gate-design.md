# Studio Transaction Edit Gate Design

## Intent

Define the minimum editor-framework gates required before any Inspector field can write scene data. The goal is to protect selection state, undo/redo, dirty state, validation, and future native scene ownership before introducing writable UI.

## Required Before Writable Inspector

- Stable object identity in scene snapshots.
- Schema/editor metadata for each editable field.
- Command object captures target id, field id, old value, new value, validation result, merge policy, and display label.
- Transaction service supports begin, commit, rollback, undo, redo, dirty-state publication, and diagnostics.
- Inspector edit never mutates runtime data directly.

## First Writable Slice

Only Transform position is writable. Scale, rotation, material, hierarchy mutation, prefab, and multi-object mixed edits are deferred.

## Validation

- Select object.
- Edit Transform position.
- Dirty state becomes true.
- Undo restores previous value.
- Redo restores new value.
- Save/reload keeps the new value only after commit.

## Source-Informed Direction

- Unity-style editor undo records object state before the edit, so Studio transaction commands must capture old and new values before applying writes.
- Unreal-style scoped transactions use a user-visible session name/context, so Studio commands need stable display labels for undo history and diagnostics.
- Godot separates editor undo history by edited scene and uses a global history for non-scene edits, so Studio must not merge scene edits, project settings, and transient UI state into one hidden stack.

## Non-Goals

- No direct C++ object pointer mutation from Inspector view models.
- No native bridge or ABI in this gate.
- No writable multi-selection, prefab, material, hierarchy, or asset edits in the first slice.
