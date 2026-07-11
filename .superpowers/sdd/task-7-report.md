# Task 7 Report: Architecture Gates, Documentation, and Validation

## Status

Implemented locally on `codex/studio-code-first-extraction-design` in commit `6daf53fb`. No remote Issue, push, or PR operation was performed. Publishing remains with the controller after final whole-branch review.

## RED → GREEN evidence

1. Final ownership gate:
   - RED: `Code_first_source_is_owned_only_by_public_editor` failed because the empty legacy `apps/studio/Core/CodeFirstUI` directory still existed.
   - GREEN: after verifying the directory tree contained no files and deleting only that empty tree, the focused architecture test passed 1/1.
2. Legacy solution regression discovered by the full gate:
   - RED: `Editor.sln` passed 599 tests and failed `StudioLayeringTests.Core_code_first_ui_does_not_reference_avalonia_or_shell` because that obsolete test enumerated the now-deleted legacy directory.
   - GREEN: the test now asserts that the legacy directory does not exist; the focused test and both full solutions pass.
3. Formatting/encoding gate:
   - RED: `dotnet format` reported `CHARSET` for four Task 6 public Code-first files. The strict changed-file scan then identified 35 branch-touched managed/project files carrying legacy BOMs.
   - GREEN: only branch-touched managed/project files were normalized to strict UTF-8 without BOM; all four format gates and the strict scan pass.

## Final validation evidence

- Public build: `Asharia.Editor.csproj` Release `--no-restore -warnaserror` — 0 warnings, 0 errors.
- Public API tests: `Asharia.Editor.Tests` — 123 passed, 0 failed.
- Architecture tests: `Asharia.Studio.Architecture.Tests` — 6 passed, 0 failed.
- Legacy solution: `Editor.sln` — 600 passed, 0 failed.
- Target solution: `Asharia.Studio.sln` — 600 legacy + 123 public + 6 architecture tests passed, 0 failed.
- Original Code-first behavior suite: the five relocated classes (`GuiFrameBuilderTests`, `GuiEventQueueTests`, `GuiStateStoreTests`, `GuiTreeValidatorTests`, and `EditorGuiTests`) — 58 passed, 0 failed.
- Broad `CodeFirst` filter: 68 passed, including the 58 original behavior cases plus 10 ownership/SPI gates.
- Formatting: public project, public tests, branch-changed legacy production, and branch-changed legacy tests passed `dotnet format --verify-no-changes`; legacy workspace loading emitted non-failing warnings.
- Encoding/docs/diff: repository encoding check passed; doc sync passed with the prescribed Studio-local reason; working and cached diff checks passed; strict UTF-8 no-BOM validation covered every branch-changed managed/Markdown file.

## #233 evidence draft

The Code-first authoring/tree/state/events/validation implementation now belongs to dependency-free `Asharia.Editor`. Diagnostics, Commands, and Panels were promoted as the minimal prerequisite contract closure. The legacy `Editor` executable references the public assembly and retains Avalonia, Dock, Shell host, and dispatcher implementation. Cross-assembly panel lifecycle dispatch uses the explicit `ICodeFirstEditorPanelHost` SPI; no new production `InternalsVisibleTo` was added, and the pre-existing test-only friend in `apps/studio/Properties/AssemblyInfo.cs` remains unchanged.

The 58 original Code-first cases are preserved under `Tests/Asharia.Editor.Tests`; final counts and RED/GREEN reasons are recorded above. Contribution descriptors, registry/Host resolver behavior, Package generation, activation topology, reload, native ABI, renderer, Viewport, and Play Mode remain intentionally deferred.

## Draft PR handoff

Suggested summary:

- Promote the minimal Diagnostics/Commands/Panels prerequisite contracts into `Asharia.Editor`.
- Move the full UI-neutral Code-first kernel and authoring API into the public assembly.
- Add the explicit panel host SPI while keeping Avalonia/Dock/Shell implementation private.
- Preserve all 58 original Code-first behavior cases and add enforceable source/assembly ownership gates.
- Update Studio architecture, refactor-plan, extraction spec, and implementation-plan facts.

Use `Closes #233` only after the controller's whole-branch review confirms the acceptance criteria and Done evidence. Keep the PR Draft until review confirms that public API exposes no legacy Workbench, Avalonia, Dock implementation, native vocabulary, or unintended friend assembly.

## Concerns for final review

- Avalonia Accelerate prints its community telemetry notice during solution builds; it is informational and did not produce a warning/error or test failure.
- Legacy scoped `dotnet format` reports workspace-load warnings without diagnostics at minimal verbosity; the command exits successfully and changed files pass verification.
- Do not push or create the PR until the controller finishes the required whole-branch review.

## Changes-requested follow-up

Fix commit: `fix(studio): address task 7 review findings` (this review-fix commit).

Changes:

- Rewrote the architecture current-facts section so the remaining legacy `Editor` host/feature ownership is explicit, Code-first ownership is assigned to `Asharia.Editor`, and the stable boundary is enforced by `ProjectReference`, architecture tests, and the compiler.
- Strengthened `Code_first_source_is_owned_only_by_public_editor` by materializing an ordinally sorted `publicFiles` array and asserting it is non-empty before source inspection.

Validation commands and results:

- `dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore` — 6 passed, 0 failed.
- `dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~CodeFirstAssemblyOwnershipTests|FullyQualifiedName~PublicPrerequisiteContractTests"` — 18 passed, 0 failed.
- `dotnet format apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj --verify-no-changes --no-restore` — passed.
- `powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1` — passed.
- `powershell -ExecutionPolicy Bypass -File tools/check-doc-sync.ps1 -NoDocsReason "Studio-local architecture, spec, and plan docs under apps/studio/docs were updated; the repository-level checker only classifies root docs paths."` — passed.
- `git diff --check` and `git diff --cached --check` — passed.

The architecture test build continues to emit the pre-existing nullable warning CS8631 at `ProjectReferenceGraphTests.cs:59`; the requested ownership tests pass and this review fix does not touch that unrelated assertion.
