#include "asharia/host_runtime/activation_eligibility.hpp"

#include <algorithm>
#include <new>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "activation_eligibility_state.hpp"

namespace asharia::host_runtime {
    namespace {

        // One out-of-line TLS slot is shared by all callers in the process image;
        // keeping it in a header can produce separate slots across static libraries.
        thread_local std::weak_ptr<const ControlThreadEpochAnchorV1>
            currentControlThreadEpoch; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic<std::shared_ptr<const CurrentProcessEpochAnchorV1>>
            currentProcessEpoch; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        constexpr std::uint32_t kCurrentTemplateRendererRevision = 2;
        constexpr std::uint32_t kCurrentCompositionRendererRevision = 3;
        constexpr std::string_view kCurrentProviderApi{
            "asharia-static-factory-provider-v2"};

        [[nodiscard]] ActivationEligibilityErrorV1
        makeError(ActivationEligibilityErrorCodeV1 code,
                  ActivationEligibilityFieldV1 field) noexcept {
            return {
                .stage = ActivationEligibilityStageV1::PreRegistration,
                .code = code,
                .field = field,
                .registrationCode = std::nullopt,
            };
        }

        [[nodiscard]] bool isLowerHexSha256(std::string_view value) noexcept {
            return value.size() == 64 &&
                   std::ranges::all_of(value, [](const char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                   });
        }

        [[nodiscard]] bool isGenerationId(std::string_view value) noexcept {
            constexpr std::string_view kPrefix{"sha256-"};
            return value.starts_with(kPrefix) &&
                   isLowerHexSha256(value.substr(kPrefix.size()));
        }

        [[nodiscard]] bool isValid(const ExactHostIdentityStateV1& host) noexcept {
            return isGenerationId(host.engineGenerationId) && !host.hostKind.empty() &&
                   !host.targetPlatform.empty();
        }

        [[nodiscard]] bool
        isValid(const GeneratedHostInputIdentityStateV1& input) noexcept {
            return isGenerationId(input.generationId) &&
                   isLowerHexSha256(input.manifestSha256);
        }

        [[nodiscard]] bool isValid(const HostArtifactIdentityStateV1& artifact) noexcept {
            return artifact.size != 0 && isLowerHexSha256(artifact.sha256);
        }

        [[nodiscard]] bool isCurrent(const HostGenerationTupleStateV1& tuple) noexcept {
            return tuple.templateRendererRevision == kCurrentTemplateRendererRevision &&
                   tuple.compositionRendererRevision ==
                       kCurrentCompositionRendererRevision &&
                   tuple.providerApi == kCurrentProviderApi;
        }

        [[nodiscard]] auto registrationKey(const StaticFactoryRegistrationV1& value) noexcept {
            return std::tie(value.packageId, value.packageVersion, value.moduleId,
                            value.factoryId, value.providerEntryPoint);
        }

        [[nodiscard]] bool isValidExpectedSnapshot(
            const DeepVerifiedHostBindingHandoffStateV1& binding) noexcept {
            const StaticFactoryRegistrationSnapshotV1& snapshot = binding.expectedSnapshot;
            if (snapshot.generationId != binding.staticComposition.generationId ||
                snapshot.hostActivationBlueprintSha256 != binding.blueprintIntegrity ||
                !isGenerationId(snapshot.generationId) ||
                !isLowerHexSha256(snapshot.hostActivationBlueprintSha256)) {
                return false;
            }

            for (std::size_t index = 0; index < snapshot.registrations.size(); ++index) {
                const StaticFactoryRegistrationV1& registration =
                    snapshot.registrations[index];
                if (registration.packageId.empty() || registration.packageVersion.empty() ||
                    registration.moduleId.empty() || registration.factoryId.empty() ||
                    registration.providerEntryPoint.empty()) {
                    return false;
                }
                if (index != 0 &&
                    registrationKey(snapshot.registrations[index - 1]) >=
                        registrationKey(registration)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool isValid(const ReadySessionHandoffStateV1& ready) noexcept {
            return isValid(ready.host) && isLowerHexSha256(ready.sessionFingerprint);
        }

        [[nodiscard]] bool isValid(
            const VerifiedHostActivationBlueprintHandoffStateV1& blueprint) noexcept {
            return isValid(blueprint.host) &&
                   isLowerHexSha256(blueprint.effectiveSessionIntegrity) &&
                   isLowerHexSha256(blueprint.blueprintIntegrity);
        }

        [[nodiscard]] bool
        isValidBindingCore(const DeepVerifiedHostBindingHandoffStateV1& binding) noexcept {
            return isValid(binding.host) && isGenerationId(binding.bindingGenerationId) &&
                   isValid(binding.staticComposition) && isValid(binding.hostTemplate) &&
                   isLowerHexSha256(binding.blueprintIntegrity) && isValid(binding.artifact);
        }

        [[nodiscard]] bool isValid(
            const VerifiedCurrentProcessLaunchHandoffStateV1& launchHandoff) noexcept {
            return isValid(launchHandoff.host) &&
                   isLowerHexSha256(launchHandoff.sessionFingerprint) &&
                   isGenerationId(launchHandoff.bindingGenerationId) &&
                   isValid(launchHandoff.staticComposition) &&
                   isValid(launchHandoff.hostTemplate) &&
                   isLowerHexSha256(launchHandoff.blueprintIntegrity) &&
                   isValid(launchHandoff.artifact) && launchHandoff.processEpoch != nullptr &&
                   launchHandoff.controlThreadEpoch != nullptr &&
                   launchHandoff.registrationCapacity != nullptr &&
                   launchHandoff.recordProviders != nullptr;
        }

        [[nodiscard]] std::optional<ActivationEligibilityErrorV1>
        validateAndClaimLaunchAuthority(
            const VerifiedCurrentProcessLaunchHandoffStateV1& launch) noexcept {
            if (!launch.processEpoch || !launch.controlThreadEpoch) {
                return makeError(ActivationEligibilityErrorCodeV1::LaunchHandoffInvalid,
                                 ActivationEligibilityFieldV1::LaunchHandoff);
            }
            if (!isCurrentControlThread(launch.controlThreadEpoch)) {
                return makeError(ActivationEligibilityErrorCodeV1::WrongControlThread,
                                 ActivationEligibilityFieldV1::ControlThread);
            }
            if (!isCurrentProcessEpoch(launch.processEpoch)) {
                return makeError(ActivationEligibilityErrorCodeV1::ProcessEpochStale,
                                 ActivationEligibilityFieldV1::CurrentProcess);
            }
            // Claim before checking the remaining lineage so a duplicated launch
            // anchor cannot replay a failed attempt with corrected facts.
            if (!tryClaimCurrentProcessEpoch(launch.processEpoch)) {
                return makeError(ActivationEligibilityErrorCodeV1::ProcessEpochConsumed,
                                 ActivationEligibilityFieldV1::CurrentProcess);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ActivationEligibilityErrorV1> validateHandoffFacts(
            const ReadySessionHandoffStateV1& ready,
            const VerifiedHostActivationBlueprintHandoffStateV1& blueprint,
            const DeepVerifiedHostBindingHandoffStateV1& binding,
            const VerifiedCurrentProcessLaunchHandoffStateV1& launch) noexcept {
            if (!isValid(ready)) {
                return makeError(ActivationEligibilityErrorCodeV1::ReadySessionInvalid,
                                 ActivationEligibilityFieldV1::ReadySession);
            }
            if (!isValid(blueprint)) {
                return makeError(ActivationEligibilityErrorCodeV1::BlueprintInvalid,
                                 ActivationEligibilityFieldV1::Blueprint);
            }
            if (!isValidBindingCore(binding)) {
                return makeError(ActivationEligibilityErrorCodeV1::BindingInvalid,
                                 ActivationEligibilityFieldV1::Binding);
            }
            if (!isValidExpectedSnapshot(binding)) {
                return makeError(
                    ActivationEligibilityErrorCodeV1::ExpectedSnapshotInvalid,
                    ActivationEligibilityFieldV1::ExpectedSnapshot);
            }
            if (!isValid(launch)) {
                return makeError(ActivationEligibilityErrorCodeV1::LaunchHandoffInvalid,
                                 ActivationEligibilityFieldV1::LaunchHandoff);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ActivationEligibilityErrorV1> validateExactLineage(
            const ReadySessionHandoffStateV1& ready,
            const VerifiedHostActivationBlueprintHandoffStateV1& blueprint,
            const DeepVerifiedHostBindingHandoffStateV1& binding,
            const VerifiedCurrentProcessLaunchHandoffStateV1& launch) noexcept {
            if (ready.sessionFingerprint != blueprint.effectiveSessionIntegrity ||
                ready.sessionFingerprint != launch.sessionFingerprint) {
                return makeError(
                    ActivationEligibilityErrorCodeV1::EffectiveSessionMismatch,
                    ActivationEligibilityFieldV1::EffectiveSessionIntegrity);
            }
            if (ready.host != blueprint.host || ready.host != binding.host ||
                ready.host != launch.host) {
                return makeError(ActivationEligibilityErrorCodeV1::HostIdentityMismatch,
                                 ActivationEligibilityFieldV1::HostIdentity);
            }
            if (blueprint.blueprintIntegrity != binding.blueprintIntegrity ||
                blueprint.blueprintIntegrity != launch.blueprintIntegrity) {
                return makeError(ActivationEligibilityErrorCodeV1::BlueprintMismatch,
                                 ActivationEligibilityFieldV1::BlueprintIntegrity);
            }
            if (!isCurrent(binding.generationTuple) ||
                !isCurrent(launch.generationTuple) ||
                binding.generationTuple != launch.generationTuple) {
                return makeError(
                    ActivationEligibilityErrorCodeV1::UnsupportedGenerationTuple,
                    ActivationEligibilityFieldV1::GenerationTuple);
            }
            if (binding.staticComposition != launch.staticComposition) {
                return makeError(
                    ActivationEligibilityErrorCodeV1::StaticCompositionMismatch,
                    ActivationEligibilityFieldV1::StaticComposition);
            }
            if (binding.hostTemplate != launch.hostTemplate) {
                return makeError(ActivationEligibilityErrorCodeV1::HostTemplateMismatch,
                                 ActivationEligibilityFieldV1::HostTemplate);
            }
            if (binding.bindingGenerationId != launch.bindingGenerationId) {
                return makeError(
                    ActivationEligibilityErrorCodeV1::BindingGenerationMismatch,
                    ActivationEligibilityFieldV1::BindingGeneration);
            }
            if (binding.artifact != launch.artifact) {
                return makeError(ActivationEligibilityErrorCodeV1::ArtifactMismatch,
                                 ActivationEligibilityFieldV1::ArtifactIdentity);
            }
            return std::nullopt;
        }

    } // namespace

    std::shared_ptr<const ControlThreadEpochAnchorV1>
    createAndBindCurrentControlThreadEpoch() {
        auto epoch = std::make_shared<const ControlThreadEpochAnchorV1>();
        currentControlThreadEpoch = epoch;
        return epoch;
    }

    bool isCurrentControlThread(
        const std::shared_ptr<const ControlThreadEpochAnchorV1>& expected) noexcept {
        const std::shared_ptr<const ControlThreadEpochAnchorV1> current =
            currentControlThreadEpoch.lock();
        return current != nullptr && current == expected;
    }

    std::shared_ptr<const CurrentProcessEpochAnchorV1>
    createAndBindCurrentProcessEpoch() {
        auto epoch = std::make_shared<const CurrentProcessEpochAnchorV1>();
        currentProcessEpoch.store(epoch, std::memory_order_release);
        return epoch;
    }

    bool isCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept {
        return expected != nullptr &&
               currentProcessEpoch.load(std::memory_order_acquire) == expected;
    }

    bool tryClaimCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept {
        if (!isCurrentProcessEpoch(expected)) {
            return false;
        }
        bool unclaimed = false;
        return expected->claimed.compare_exchange_strong(
            unclaimed, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    bool isClaimedCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept {
        return isCurrentProcessEpoch(expected) &&
               expected->claimed.load(std::memory_order_acquire);
    }

    ReadySessionHandoffV1::ReadySessionHandoffV1(
        std::unique_ptr<ReadySessionHandoffStateV1> state) noexcept
        : state_(std::move(state)) {}

    ReadySessionHandoffV1::~ReadySessionHandoffV1() = default;
    ReadySessionHandoffV1::ReadySessionHandoffV1(ReadySessionHandoffV1&&) noexcept = default;

    VerifiedHostActivationBlueprintHandoffV1::
        VerifiedHostActivationBlueprintHandoffV1(
            std::unique_ptr<VerifiedHostActivationBlueprintHandoffStateV1> state) noexcept
        : state_(std::move(state)) {}

    VerifiedHostActivationBlueprintHandoffV1::
        ~VerifiedHostActivationBlueprintHandoffV1() = default;
    VerifiedHostActivationBlueprintHandoffV1::
        VerifiedHostActivationBlueprintHandoffV1(
            VerifiedHostActivationBlueprintHandoffV1&&) noexcept = default;

    DeepVerifiedHostBindingHandoffV1::DeepVerifiedHostBindingHandoffV1(
        std::unique_ptr<DeepVerifiedHostBindingHandoffStateV1> state) noexcept
        : state_(std::move(state)) {}

    DeepVerifiedHostBindingHandoffV1::~DeepVerifiedHostBindingHandoffV1() = default;
    DeepVerifiedHostBindingHandoffV1::DeepVerifiedHostBindingHandoffV1(
        DeepVerifiedHostBindingHandoffV1&&) noexcept = default;

    VerifiedCurrentProcessLaunchHandoffV1::VerifiedCurrentProcessLaunchHandoffV1(
        std::unique_ptr<VerifiedCurrentProcessLaunchHandoffStateV1> state) noexcept
        : state_(std::move(state)) {}

    VerifiedCurrentProcessLaunchHandoffV1::~VerifiedCurrentProcessLaunchHandoffV1() = default;
    VerifiedCurrentProcessLaunchHandoffV1::VerifiedCurrentProcessLaunchHandoffV1(
        VerifiedCurrentProcessLaunchHandoffV1&&) noexcept = default;

    PreRegistrationAdmissionV1::PreRegistrationAdmissionV1(
        std::unique_ptr<ActivationEligibilityLineageStateV1> state) noexcept
        : state_(std::move(state)) {}

    PreRegistrationAdmissionV1::~PreRegistrationAdmissionV1() = default;
    PreRegistrationAdmissionV1::PreRegistrationAdmissionV1(
        PreRegistrationAdmissionV1&&) noexcept = default;

    ActivationEligibilityResultV1<PreRegistrationAdmissionV1>
    admitPreRegistration(ReadySessionHandoffV1 readySession,
                         VerifiedHostActivationBlueprintHandoffV1 blueprint,
                         DeepVerifiedHostBindingHandoffV1 binding,
                         VerifiedCurrentProcessLaunchHandoffV1 launchHandoff) noexcept {
        auto readyState = ActivationEligibilityStateAccessV1::take(std::move(readySession));
        auto blueprintState = ActivationEligibilityStateAccessV1::take(std::move(blueprint));
        auto bindingState = ActivationEligibilityStateAccessV1::take(std::move(binding));
        auto launchState = ActivationEligibilityStateAccessV1::take(std::move(launchHandoff));

        if (!readyState) {
            return std::unexpected(makeError(
                ActivationEligibilityErrorCodeV1::HandoffMovedFrom,
                ActivationEligibilityFieldV1::ReadySession));
        }
        if (!blueprintState) {
            return std::unexpected(makeError(
                ActivationEligibilityErrorCodeV1::HandoffMovedFrom,
                ActivationEligibilityFieldV1::Blueprint));
        }
        if (!bindingState) {
            return std::unexpected(makeError(
                ActivationEligibilityErrorCodeV1::HandoffMovedFrom,
                ActivationEligibilityFieldV1::Binding));
        }
        if (!launchState) {
            return std::unexpected(makeError(
                ActivationEligibilityErrorCodeV1::HandoffMovedFrom,
                ActivationEligibilityFieldV1::LaunchHandoff));
        }
        if (auto failure = validateAndClaimLaunchAuthority(*launchState)) {
            return std::unexpected(*failure);
        }
        if (auto failure = validateHandoffFacts(
                *readyState, *blueprintState, *bindingState, *launchState)) {
            return std::unexpected(*failure);
        }
        if (auto failure = validateExactLineage(
                *readyState, *blueprintState, *bindingState, *launchState)) {
            return std::unexpected(*failure);
        }
        try {
            auto lineage = std::make_unique<ActivationEligibilityLineageStateV1>(
                ActivationEligibilityLineageStateV1{
                    .host = std::move(bindingState->host),
                    .sessionFingerprint = std::move(readyState->sessionFingerprint),
                    .bindingGenerationId = std::move(bindingState->bindingGenerationId),
                    .staticComposition = std::move(bindingState->staticComposition),
                    .hostTemplate = std::move(bindingState->hostTemplate),
                    .generationTuple = std::move(bindingState->generationTuple),
                    .blueprintIntegrity = std::move(bindingState->blueprintIntegrity),
                    .artifact = std::move(bindingState->artifact),
                    .expectedSnapshot = std::move(bindingState->expectedSnapshot),
                    .processEpoch = std::move(launchState->processEpoch),
                    .controlThreadEpoch = std::move(launchState->controlThreadEpoch),
                    .registrationCapacity = launchState->registrationCapacity,
                    .recordProviders = launchState->recordProviders,
                });
            return ActivationEligibilityStateAccessV1::makePreRegistrationAdmission(
                std::move(lineage));
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(
                ActivationEligibilityErrorCodeV1::AllocationFailed,
                ActivationEligibilityFieldV1::Admission));
        }
    }

} // namespace asharia::host_runtime
