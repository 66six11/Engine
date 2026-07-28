#include "asharia/host_runtime/activation_eligibility.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "asharia/host_runtime/current_image_activation_descriptor_provider_access.hpp"

#include "activation_eligibility_state.hpp"

namespace asharia::host_runtime {
    namespace {

        // These out-of-line slots are shared by every static-library caller in one image.
        thread_local std::weak_ptr<const ControlThreadEpochAnchorV1>
            currentControlThreadEpoch; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic<std::shared_ptr<const ControlThreadEpochAnchorV1>>
            currentImageControlThreadEpoch; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic<std::shared_ptr<const CurrentProcessEpochAnchorV1>>
            currentProcessEpoch; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::mutex
            currentImageEpochMutex; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        constexpr std::uint32_t kCurrentTemplateRendererRevision = 3;
        constexpr std::uint32_t kCurrentCompositionRendererRevision = 6;
        constexpr std::uint32_t kCurrentRegistrationSnapshotSchemaVersion = 2;
        constexpr std::string_view kCurrentProviderApi{"asharia-static-factory-provider-v4"};
        constexpr std::string_view kCurrentLifecycleModel{
            "create-activate-quiesce-deactivate-destroy-v1"};

        [[nodiscard]] ActivationEligibilityErrorV2
        makeError(ActivationEligibilityErrorCodeV2 code,
                  ActivationEligibilityFieldV2 field) noexcept {
            return {
                .stage = ActivationEligibilityStageV2::PreRegistration,
                .code = code,
                .field = field,
                .registrationCode = std::nullopt,
            };
        }

        [[nodiscard]] bool isLowerHexSha256(std::string_view value) noexcept {
            return value.size() == 64 && std::ranges::all_of(value, [](const char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                   });
        }

        [[nodiscard]] bool isGenerationId(std::string_view value) noexcept {
            constexpr std::string_view kPrefix{"sha256-"};
            return value.starts_with(kPrefix) && isLowerHexSha256(value.substr(kPrefix.size()));
        }

        [[nodiscard]] bool isValid(const ExactHostIdentityStateV2& host) noexcept {
            return isGenerationId(host.engineGenerationId) && !host.hostKind.empty() &&
                   !host.targetPlatform.empty();
        }

        [[nodiscard]] bool isCurrent(const HostGenerationTupleStateV2& tuple) noexcept {
            return tuple.templateRendererRevision == kCurrentTemplateRendererRevision &&
                   tuple.compositionRendererRevision == kCurrentCompositionRendererRevision &&
                   tuple.providerApi == kCurrentProviderApi &&
                   tuple.registrationSnapshotSchemaVersion ==
                       kCurrentRegistrationSnapshotSchemaVersion;
        }

        [[nodiscard]] bool isValid(const ExactFactoryReferenceStateV1& reference) noexcept {
            return !reference.packageId.empty() && !reference.packageVersion.empty() &&
                   !reference.moduleId.empty() && !reference.factoryId.empty();
        }

        [[nodiscard]] bool sameFactory(const ExactFactoryReferenceStateV1& left,
                                       const ExactFactoryReferenceStateV1& right) noexcept {
            return left == right;
        }

        [[nodiscard]] bool
        isValidProcessProjection(const ActivationEligibilityLineageStateV2& lineage) noexcept {
            const ProcessScopeBlueprintProjectionStateV1& projection = lineage.processScope;
            if (projection.scope != HostScopeKindStateV1::Process ||
                projection.parentScope.has_value() ||
                projection.engineGenerationId != lineage.host.engineGenerationId ||
                projection.blueprintIntegrity != lineage.blueprintIntegrity ||
                projection.lifecycleModel != lineage.lifecycleModel ||
                projection.lifecycleModel != kCurrentLifecycleModel) {
                return false;
            }

            for (std::size_t factoryIndex = 0; factoryIndex < projection.factories.size();
                 ++factoryIndex) {
                const ProcessFactoryProjectionStateV1& factory = projection.factories[factoryIndex];
                if (!isValid(factory.reference)) {
                    return false;
                }
                for (std::size_t previous = 0; previous < factoryIndex; ++previous) {
                    if (sameFactory(projection.factories[previous].reference, factory.reference)) {
                        return false;
                    }
                }
                for (std::size_t requirementIndex = 0;
                     requirementIndex < factory.requirements.size(); ++requirementIndex) {
                    const ExactFactoryReferenceStateV1& requirement =
                        factory.requirements[requirementIndex];
                    if (!isValid(requirement)) {
                        return false;
                    }
                    for (std::size_t previous = 0; previous < requirementIndex; ++previous) {
                        if (sameFactory(factory.requirements[previous], requirement)) {
                            return false;
                        }
                    }
                    const auto dependency = std::ranges::find_if(
                        projection.factories.begin(),
                        projection.factories.begin() + static_cast<std::ptrdiff_t>(factoryIndex),
                        [&requirement](const ProcessFactoryProjectionStateV1& candidate) {
                            return sameFactory(candidate.reference, requirement);
                        });
                    if (dependency ==
                        projection.factories.begin() + static_cast<std::ptrdiff_t>(factoryIndex)) {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] std::optional<ActivationEligibilityErrorV2>
        validateDescriptor(const ActivationEligibilityLineageStateV2& lineage) noexcept {
            if (!isValid(lineage.host)) {
                return makeError(ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                                 ActivationEligibilityFieldV2::HostIdentity);
            }
            if (!isLowerHexSha256(lineage.effectiveSessionIntegrity)) {
                return makeError(ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                                 ActivationEligibilityFieldV2::EffectiveSessionIntegrity);
            }
            if (!isGenerationId(lineage.staticCompositionGenerationId)) {
                return makeError(ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                                 ActivationEligibilityFieldV2::StaticComposition);
            }
            if (!isLowerHexSha256(lineage.blueprintIntegrity)) {
                return makeError(ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                                 ActivationEligibilityFieldV2::BlueprintIntegrity);
            }
            if (!isCurrent(lineage.generationTuple)) {
                return makeError(ActivationEligibilityErrorCodeV2::UnsupportedGenerationTuple,
                                 ActivationEligibilityFieldV2::GenerationTuple);
            }
            if (lineage.registrationCapacity == nullptr || lineage.recordProviders == nullptr) {
                return makeError(ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                                 ActivationEligibilityFieldV2::RecordingFunction);
            }
            if (!isValidProcessProjection(lineage)) {
                return makeError(ActivationEligibilityErrorCodeV2::ProcessProjectionInvalid,
                                 ActivationEligibilityFieldV2::ProcessProjection);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::pair<std::shared_ptr<const CurrentProcessEpochAnchorV1>,
                                std::shared_ptr<const ControlThreadEpochAnchorV1>>
        bindCurrentImageEpochs() {
            const std::scoped_lock lock{currentImageEpochMutex};
            auto processEpoch = currentProcessEpoch.load(std::memory_order_acquire);
            auto controlEpoch = currentImageControlThreadEpoch.load(std::memory_order_acquire);
            if (processEpoch == nullptr || controlEpoch == nullptr) {
                processEpoch = createAndBindCurrentProcessEpoch();
                controlEpoch = createAndBindCurrentControlThreadEpoch();
            }
            return {std::move(processEpoch), std::move(controlEpoch)};
        }

        [[nodiscard]] ExactFactoryReferenceStateV1
        ownReference(const CurrentImageExactFactoryReferenceProviderV2& reference) {
            return {
                .packageId = std::string(reference.packageId),
                .packageVersion = std::string(reference.packageVersion),
                .moduleId = std::string(reference.moduleId),
                .factoryId = std::string(reference.factoryId),
            };
        }

        [[nodiscard]] ProcessScopeBlueprintProjectionStateV1
        ownProcessProjection(const CurrentImageActivationDescriptorProviderV2& provider) {
            ProcessScopeBlueprintProjectionStateV1 projection{
                .scope = HostScopeKindStateV1::Process,
                .parentScope = std::nullopt,
                .engineGenerationId = std::string(provider.engineGenerationId),
                .blueprintIntegrity = std::string(provider.hostActivationBlueprintSha256),
                .lifecycleModel = std::string(provider.lifecycleModel),
                .factories = {},
            };
            projection.factories.reserve(provider.processFactories.size());
            for (const CurrentImageProcessFactoryProviderV2& factory : provider.processFactories) {
                ProcessFactoryProjectionStateV1 owned{
                    .reference = ownReference(factory.reference),
                    .requirements = {},
                };
                owned.requirements.reserve(factory.requirements.size());
                for (const CurrentImageExactFactoryReferenceProviderV2& requirement :
                     factory.requirements) {
                    owned.requirements.push_back(ownReference(requirement));
                }
                projection.factories.push_back(std::move(owned));
            }
            return projection;
        }

    } // namespace

    std::shared_ptr<const ControlThreadEpochAnchorV1> createAndBindCurrentControlThreadEpoch() {
        auto epoch = std::make_shared<const ControlThreadEpochAnchorV1>();
        currentImageControlThreadEpoch.store(epoch, std::memory_order_release);
        currentControlThreadEpoch = epoch;
        return epoch;
    }

    bool isCurrentControlThread(
        const std::shared_ptr<const ControlThreadEpochAnchorV1>& expected) noexcept {
        const std::shared_ptr<const ControlThreadEpochAnchorV1> current =
            currentControlThreadEpoch.lock();
        return current != nullptr && current == expected;
    }

    std::shared_ptr<const CurrentProcessEpochAnchorV1> createAndBindCurrentProcessEpoch() {
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
        return expected->claimed.compare_exchange_strong(unclaimed, true, std::memory_order_acq_rel,
                                                         std::memory_order_acquire);
    }

    bool isClaimedCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept {
        return isCurrentProcessEpoch(expected) && expected->claimed.load(std::memory_order_acquire);
    }

    CurrentImageActivationDescriptorV2::CurrentImageActivationDescriptorV2(
        std::unique_ptr<ActivationEligibilityLineageStateV2> state) noexcept
        : state_(std::move(state)) {}

    CurrentImageActivationDescriptorV2::~CurrentImageActivationDescriptorV2() = default;
    CurrentImageActivationDescriptorV2::CurrentImageActivationDescriptorV2(
        CurrentImageActivationDescriptorV2&&) noexcept = default;

    PreRegistrationAdmissionV2::PreRegistrationAdmissionV2(
        std::unique_ptr<ActivationEligibilityLineageStateV2> state) noexcept
        : state_(std::move(state)) {}

    PreRegistrationAdmissionV2::~PreRegistrationAdmissionV2() = default;
    PreRegistrationAdmissionV2::PreRegistrationAdmissionV2(PreRegistrationAdmissionV2&&) noexcept =
        default;

    ActivationEligibilityResultV2<CurrentImageActivationDescriptorV2>
    detail::issueCurrentImageActivationDescriptorV2(
        const CurrentImageActivationDescriptorProviderV2& provider) noexcept {
        try {
            auto [processEpoch, controlThreadEpoch] = bindCurrentImageEpochs();
            auto state = std::make_unique<ActivationEligibilityLineageStateV2>(
                ActivationEligibilityLineageStateV2{
                    .host =
                        {
                            .engineGenerationId = std::string(provider.engineGenerationId),
                            .hostKind = std::string(provider.hostKind),
                            .targetPlatform = std::string(provider.targetPlatform),
                        },
                    .effectiveSessionIntegrity = std::string(provider.effectiveSessionIntegrity),
                    .staticCompositionGenerationId =
                        std::string(provider.staticCompositionGenerationId),
                    .blueprintIntegrity = std::string(provider.hostActivationBlueprintSha256),
                    .generationTuple =
                        {
                            .templateRendererRevision = provider.templateRendererRevision,
                            .compositionRendererRevision = provider.compositionRendererRevision,
                            .providerApi = std::string(provider.providerApi),
                            .registrationSnapshotSchemaVersion =
                                provider.registrationSnapshotSchemaVersion,
                        },
                    .lifecycleModel = std::string(provider.lifecycleModel),
                    .processScope = ownProcessProjection(provider),
                    .processEpoch = std::move(processEpoch),
                    .controlThreadEpoch = std::move(controlThreadEpoch),
                    .registrationCapacity = provider.registrationCapacity,
                    .recordProviders = provider.recordProviders,
                });
            return ActivationEligibilityStateAccessV2::makeDescriptor(std::move(state));
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(ActivationEligibilityErrorCodeV2::AllocationFailed,
                                             ActivationEligibilityFieldV2::CurrentImageDescriptor));
        } catch (const std::length_error&) {
            return std::unexpected(makeError(ActivationEligibilityErrorCodeV2::AllocationFailed,
                                             ActivationEligibilityFieldV2::CurrentImageDescriptor));
        }
    }

    ActivationEligibilityResultV2<PreRegistrationAdmissionV2>
    admitCurrentImagePreRegistration(CurrentImageActivationDescriptorV2 descriptor) noexcept {
        auto lineage = ActivationEligibilityStateAccessV2::take(std::move(descriptor));
        if (!lineage) {
            return std::unexpected(makeError(ActivationEligibilityErrorCodeV2::DescriptorMovedFrom,
                                             ActivationEligibilityFieldV2::CurrentImageDescriptor));
        }
        if (!lineage->processEpoch || !lineage->controlThreadEpoch) {
            return std::unexpected(
                makeError(ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                          ActivationEligibilityFieldV2::CurrentImageDescriptor));
        }
        if (!isCurrentControlThread(lineage->controlThreadEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityErrorCodeV2::WrongControlThread,
                                             ActivationEligibilityFieldV2::ControlThread));
        }
        if (!isCurrentProcessEpoch(lineage->processEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityErrorCodeV2::ProcessEpochStale,
                                             ActivationEligibilityFieldV2::CurrentProcess));
        }
        // Claim before validating remaining facts so a failed image cannot replay
        // with a second descriptor carrying corrected data.
        if (!tryClaimCurrentProcessEpoch(lineage->processEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityErrorCodeV2::ProcessEpochConsumed,
                                             ActivationEligibilityFieldV2::CurrentProcess));
        }
        if (auto failure = validateDescriptor(*lineage)) {
            return std::unexpected(*failure);
        }
        return ActivationEligibilityStateAccessV2::makePreRegistrationAdmission(std::move(lineage));
    }

} // namespace asharia::host_runtime
