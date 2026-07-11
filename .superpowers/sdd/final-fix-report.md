# Dialog Final Fix Report

## Root cause

`EditorDialogHostViewModel` projected each button command with only its action ID. The command later called `Complete(result)`, which read the mutable global `completion_`. After generation 1 completed and generation 2 opened, a retained generation-1 command therefore cleared and completed generation 2.

The same defect applied to repeated action delivery and to an action signal arriving after system-dismiss had already won. The state was cleared before `TrySetResult`, but the terminal signal was not scoped to the request that created it.

## TDD evidence

Focused RED command:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~Stale_first_generation_button_does_not_complete_second_dialog|FullyQualifiedName~Repeated_action_signal_completes_only_its_own_dialog|FullyQualifiedName~Action_signal_after_system_dismiss_does_not_complete_later_dialog"
```

Result: 0 passed, 3 failed. Every test failed at `Assert.False(secondResultTask.IsCompleted)` with `Actual: True`, directly reproducing the cross-generation mutation.

Focused GREEN command:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorDialogHostViewModelTests|FullyQualifiedName~EditorDialogHostViewTests"
```

Result: 12 passed, 0 failed, 0 skipped.

The architecture test initially included compiler-generated record `Equals(object)` methods and failed for that test-construction reason. It was narrowed to exported constructors and declared public static API methods, then passed 1/1 while retaining the complete Dialog-source `CancellationToken` ban.

## Fix

- Each button command captures the `TaskCompletionSource<EditorDialogResult>` created by its own `ShowAsync` call.
- `Complete(expectedCompletion, result)` uses `Interlocked.CompareExchange` to clear `completion_` only when it still references that expected generation. Any stale or losing terminal signal is a no-op.
- `TrySystemDismiss` snapshots the current completion and returns the generation-scoped completion result.
- `TaskCreationOptions.RunContinuationsAsynchronously`, clear-before-`TrySetResult`, single-flight rejection, visible-state clearing order, and the public API remain unchanged.
- The Dialog architecture gate now rejects `CancellationToken` in the full Dialog source and inspects authored exported constructor/static-method parameter types for `Type`, `object`, `CancellationToken`, and delegates.

## Files

- `apps/studio/Shell/ViewModels/Dialogs/EditorDialogHostViewModel.cs`
- `apps/studio/Tests/Editor.Tests/Shell/ViewModels/Dialogs/EditorDialogHostViewModelTests.cs`
- `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- `apps/studio/docs/superpowers/specs/2026-07-11-studio-public-dialog-contract-design.md`
- `apps/studio/docs/superpowers/plans/2026-07-11-studio-public-dialog-contract.md`
- `apps/studio/docs/superpowers/plans/2026-07-11-studio-editor-framework-refactor.md`
- `.superpowers/sdd/final-fix-report.md`

## Commits

- `e0eaa64f` — `fix(studio): scope dialog completion generations`
- The documentation-only commit containing this report follows the implementation commit; its immutable SHA is recorded in the task handoff because a file cannot contain its own commit SHA.

## Fresh verification

- Warning-as-error builds: `Asharia.Editor` and `Asharia.Studio.Architecture.Tests`, both 0 warnings and 0 errors.
- Public tests: 174 passed, 0 failed, 0 skipped.
- Architecture tests: 8 passed, 0 failed, 0 skipped.
- Legacy tests: 602 passed, 0 failed, 0 skipped.
- Focused Dialog/MainWindow compatibility: 49 passed, 0 failed, 0 skipped.
- `Editor.sln`: 602 passed, 0 failed, 0 skipped.
- `Asharia.Studio.sln`: public 174, architecture 8, legacy 602; all passed with 0 failed and 0 skipped.
- Format verification: public project, public tests, architecture tests, and the two changed legacy C# files passed. The scoped legacy run emitted only the existing workspace-load warning.
- Repository encoding checker: 743 files checked; 0 missing BOM, 0 unexpected BOM, 0 invalid UTF-8.
- Corrected repo-wide tracked `.cs`/`.md` strict UTF-8/no-BOM scan: 599 files passed.
- Final doc sync, working-tree diff, and staged diff checks passed.

## Concerns

- `dotnet format` reports the existing workspace-load warning for the legacy project even when its scoped verification exits successfully.
- The Avalonia Accelerate community-tier telemetry notice remains informational during legacy builds/tests.
- User-owned untracked `apps/studio/.vs/` and `qodana.yaml` remain untouched and uncommitted.
