# Shared viewport GPU Mesh publication

Status: implemented native host slice (#456), not managed automatic asset loading.

Current evidence: MeshResourceStore and editor catalog selection already load immutable CPU leases.
BasicGpuMeshOwner and GPU draw retention currently require VulkanFrameLoop, while Studio uses independent
command buffers and fences. A render-thread notification or consumer texture release is not upload completion.

Owner/lifetime/thread: add an RHI VulkanSubmission scope for one host-owned command buffer and fence.
It performs the actual queue submission, exposes an immutable completion receipt, retains resources before
recording, and releases them only after vkGetFenceStatus proves completion. The host keeps the scope, fence,
command pool and device alive; destroying a pending scope is a contract violation, never an implicit wait.
Scope/receipt separation prevents an upload retention cycle. Failed or abandoned recording cannot publish.

Data/error/budget: existing bounded Mesh owner limits and typed errors remain. No render-thread file IO,
compiler, device-idle wait or guessed epoch is introduced. The native producer consumes one explicit immutable
GPU Mesh batch, matching the existing renderer contract; other GUIDs remain contextual no-draw results.

Reference-first design: adopt separate render ownership and explicit retirement from
[Unreal threaded rendering](https://dev.epicgames.com/documentation/unreal-engine/threaded-rendering-in-unreal-engine)
and [Godot RenderingServer](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html).
Do not equate their CPU render-command completion with GPU completion. Follow
[Vulkan queue submission](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html) and
[fence synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html): only successful
submission of this command buffer and its observed fence establish the receipt. No renderer-owned queue,
global resource registry or artificial VulkanFrameLoop is added.

Integration gate: first prove project GLB -> catalog -> CPU lease -> shared-view upload -> completed GPU
publication -> actual indexed draw on the production shared viewport producer. Managed catalog handoff,
multiple distinct Mesh batches and selection overlays for arbitrary GPU geometry are later integration work.
Exit evidence includes failed/unsubmitted publication, retained draw lifetime, replacement and real Vulkan
validation on both compilers. Existing Studio fixture path remains explicit when no GPU Mesh is supplied.


Validation: MSVC and ClangCL each pass 52 CTests, 33 sample-viewer smokes and the new shared Mesh
smoke with synchronization validation. Both native viewport V11 smokes pass with the validation layer
forced on. The smoke also rejects a deliberately unknown host view kind after upload recording;
its unsubmitted receipt cannot publish. Scene extraction now carries productGeneration into
meshRevision; the CPU regression asserts that field, and the real GPU binding validates it.

The shared producer accepts const descriptors because the explicit Mesh reference has shared ownership.
The RHI scope stores callbacks separately from the read-only receipt state so staging retention cannot
form a shared_ptr cycle. Submission fences remain host-owned and cannot be reset until completion is
observed. The scope does not turn device-loss into a successful receipt.

Managed regression: 371 tests pass (4 opt-in GPU tests skipped); the Scene Mesh process acceptance
test passes separately with GPU acceptance enabled. Isolated managed artifacts require
CODEX_WORKSPACE_ROOT to point to apps/studio for source-layering tests. This verifies the existing
Studio presentation path, not automatic project Mesh catalog handoff.
