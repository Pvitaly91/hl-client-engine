#include <hlclient/assets/asset_importer_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hlclient::assets::AssetError;
using hlclient::assets::AssetErrorCode;
using hlclient::assets::AssetProbe;
using hlclient::assets::AssetProbeConfidence;
using hlclient::assets::AssetResult;
using hlclient::assets::AssetSource;
using hlclient::assets::AssetSourceMetadata;
using hlclient::assets::IModelImporter;
using hlclient::assets::ModelAsset;
using hlclient::assets::ModelImporterRegistry;

[[nodiscard]] AssetSource make_source(
    std::filesystem::path virtual_path = "models/scientist.MDL",
    std::vector<std::byte> bytes = {std::byte{0x54}, std::byte{0x45}, std::byte{0x53},
                                    std::byte{0x54}},
    std::optional<AssetSourceMetadata> metadata = std::nullopt)
{
    auto result = AssetSource::create(
        std::move(virtual_path), std::move(bytes), std::move(metadata));
    if (!result) {
        throw std::runtime_error{"Unable to construct synthetic asset source"};
    }
    return std::move(*result.source);
}

class SyntheticModelImporter final : public IModelImporter {
public:
    SyntheticModelImporter(
        std::string importer_id,
        const AssetProbeConfidence confidence,
        std::string result_name,
        std::optional<AssetError> failure = std::nullopt,
        int* import_count = nullptr,
        int* destruction_count = nullptr,
        int* probe_count = nullptr)
        : importer_id_{std::move(importer_id)},
          confidence_{confidence},
          result_name_{std::move(result_name)},
          failure_{std::move(failure)},
          import_count_{import_count},
          destruction_count_{destruction_count},
          probe_count_{probe_count}
    {
    }

    ~SyntheticModelImporter() override
    {
        if (destruction_count_ != nullptr) {
            ++*destruction_count_;
        }
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return importer_id_;
    }

    [[nodiscard]] AssetProbeConfidence probe(const AssetProbe& probe) const noexcept override
    {
        if (probe_count_ != nullptr) {
            ++*probe_count_;
        }
        observed_signature_size_ = probe.signature.size();
        observed_structural_size_ = probe.structural_bytes.size();
        observed_version_ = probe.version_hint;
        return confidence_;
    }

    [[nodiscard]] AssetResult<ModelAsset> import(const AssetSource&) const override
    {
        if (import_count_ != nullptr) {
            ++*import_count_;
        }
        if (failure_) {
            return AssetResult<ModelAsset>::failure(*failure_);
        }

        ModelAsset result;
        result.identity.source_name = result_name_;
        return AssetResult<ModelAsset>::success(std::move(result));
    }

    [[nodiscard]] std::size_t observed_signature_size() const noexcept
    {
        return observed_signature_size_;
    }

    [[nodiscard]] std::size_t observed_structural_size() const noexcept
    {
        return observed_structural_size_;
    }

    [[nodiscard]] std::optional<std::uint32_t> observed_version() const noexcept
    {
        return observed_version_;
    }

private:
    std::string importer_id_;
    AssetProbeConfidence confidence_{0};
    std::string result_name_;
    std::optional<AssetError> failure_;
    int* import_count_{nullptr};
    int* destruction_count_{nullptr};
    int* probe_count_{nullptr};
    mutable std::size_t observed_signature_size_{0};
    mutable std::size_t observed_structural_size_{0};
    mutable std::optional<std::uint32_t> observed_version_;
};

class SyntheticHeaderImporter final : public IModelImporter {
public:
    SyntheticHeaderImporter(
        std::string importer_id,
        std::array<std::byte, 8U> magic,
        const std::byte supported_version,
        const std::size_t minimum_size,
        const AssetProbeConfidence confidence,
        const bool extension_only = false)
        : importer_id_{std::move(importer_id)},
          magic_{magic},
          supported_version_{supported_version},
          minimum_size_{minimum_size},
          confidence_{confidence},
          extension_only_{extension_only}
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return importer_id_;
    }

    [[nodiscard]] AssetProbeConfidence probe(const AssetProbe& probe) const noexcept override
    {
        if (extension_only_) {
            return probe.extension_hint == ".mdl" ? confidence_ : 0U;
        }
        if (probe.signature.size() < magic_.size() ||
            !std::equal(magic_.begin(), magic_.end(), probe.signature.begin())) {
            return 0U;
        }

        // The signature establishes the family. A missing version remains a match so
        // import() can return a precise malformed-data error rather than unsupported.
        if (probe.structural_bytes.size() <= magic_.size()) {
            return confidence_;
        }
        return probe.structural_bytes[magic_.size()] == supported_version_ ? confidence_ : 0U;
    }

    [[nodiscard]] AssetResult<ModelAsset> import(const AssetSource& source) const override
    {
        if (source.bytes().size() < minimum_size_) {
            return AssetResult<ModelAsset>::failure(AssetError{
                AssetErrorCode::MalformedData,
                {},
                {},
                "Synthetic header is truncated",
                {},
            });
        }

        ModelAsset asset;
        asset.identity.source_name = importer_id_;
        return AssetResult<ModelAsset>::success(std::move(asset));
    }

private:
    std::string importer_id_;
    std::array<std::byte, 8U> magic_{};
    std::byte supported_version_{};
    std::size_t minimum_size_{0};
    AssetProbeConfidence confidence_{0};
    bool extension_only_{false};
};

inline constexpr std::array<std::byte, 8U> kTestModelOneMagic{
    std::byte{0x54}, std::byte{0x45}, std::byte{0x53}, std::byte{0x54},
    std::byte{0x4D}, std::byte{0x44}, std::byte{0x4C}, std::byte{0x31},
};

inline constexpr std::array<std::byte, 8U> kTestModelTwoMagic{
    std::byte{0x54}, std::byte{0x45}, std::byte{0x53}, std::byte{0x54},
    std::byte{0x4D}, std::byte{0x44}, std::byte{0x4C}, std::byte{0x32},
};

[[nodiscard]] std::vector<std::byte> synthetic_header(
    const std::array<std::byte, 8U>& magic,
    const std::byte version,
    const std::size_t size = 12U)
{
    std::vector<std::byte> result(size);
    const auto copied_size = std::min(result.size(), magic.size());
    std::copy_n(magic.begin(), copied_size, result.begin());
    if (result.size() > magic.size()) {
        result[magic.size()] = version;
    }
    return result;
}

TEST_CASE("AssetSource owns normalized probe input", "[assets][registry]")
{
    std::vector<std::byte> original_bytes(20U, std::byte{0x2A});
    auto copied_bytes = original_bytes;
    AssetSourceMetadata metadata;
    metadata.content_size = copied_bytes.size();
    metadata.extension_hint = "MdL";
    metadata.version_hint = 10U;

    auto source = make_source("models/scientist.MDL", std::move(copied_bytes), metadata);
    original_bytes.assign(20U, std::byte{0x7F});
    const auto probe = hlclient::assets::make_asset_probe(source);

    CHECK(source.virtual_path() == std::filesystem::path{"models/scientist.MDL"});
    CHECK(source.extension_hint() == ".mdl");
    CHECK(source.bytes().size() == 20U);
    CHECK(source.bytes().front() == std::byte{0x2A});
    CHECK(source.signature().size() == hlclient::assets::kAssetProbeSignatureSize);
    REQUIRE(source.metadata());
    CHECK(source.metadata()->content_size == 20U);
    CHECK(probe.virtual_path == source.virtual_path());
    CHECK(probe.extension_hint == ".mdl");
    CHECK(probe.signature.size() == 16U);
    CHECK(probe.structural_bytes.size() == 20U);
    CHECK(probe.version_hint == 10U);

    ModelImporterRegistry registry;
    auto importer =
        std::make_unique<SyntheticModelImporter>(
            "synthetic-probe", AssetProbeConfidence{50U}, "decoded");
    const auto* observed = importer.get();
    REQUIRE(registry.register_importer(std::move(importer)));
    REQUIRE(registry.import(source));
    CHECK(observed->observed_signature_size() == 16U);
    CHECK(observed->observed_structural_size() == 20U);
    CHECK(observed->observed_version() == 10U);
}

TEST_CASE("AssetSource rejects unsafe virtual paths", "[assets][registry]")
{
    CHECK_FALSE(AssetSource::create("../outside.mdl", {}));
    CHECK_FALSE(AssetSource::create(std::filesystem::current_path().root_path(), {}));
    CHECK_FALSE(AssetSource::create({}, {}));
}

TEST_CASE("Registry pure probe reports an empty registry without importing", "[assets][registry][probe]")
{
    ModelImporterRegistry registry;

    const auto result = registry.probe(make_source());

    CHECK(result.state == hlclient::assets::AssetImporterProbeState::no_match);
    CHECK(result.best_confidence == hlclient::assets::kAssetProbeNoMatch);
    CHECK(result.best_priority == 0);
    CHECK(result.top_candidates.empty());
    CHECK_FALSE(result.selected());
}

TEST_CASE("Registry pure probe returns sorted top candidates without importing", "[assets][registry][probe]")
{
    int alpha_probe_count = 0;
    int zeta_probe_count = 0;
    int alpha_import_count = 0;
    int zeta_import_count = 0;
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "zeta",
        AssetProbeConfidence{75U},
        "zeta",
        std::nullopt,
        &zeta_import_count,
        nullptr,
        &zeta_probe_count), 5));
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "alpha",
        AssetProbeConfidence{75U},
        "alpha",
        std::nullopt,
        &alpha_import_count,
        nullptr,
        &alpha_probe_count), 5));

    const auto result = registry.probe(make_source());

    CHECK(result.state == hlclient::assets::AssetImporterProbeState::ambiguous);
    CHECK(result.best_confidence == AssetProbeConfidence{75U});
    CHECK(result.best_priority == 5);
    REQUIRE(result.top_candidates.size() == 2U);
    CHECK(result.top_candidates[0].importer_id == "alpha");
    CHECK(result.top_candidates[1].importer_id == "zeta");
    CHECK(result.top_candidates[0].confidence == AssetProbeConfidence{75U});
    CHECK(result.top_candidates[0].priority == 5);
    CHECK(alpha_probe_count == 1);
    CHECK(zeta_probe_count == 1);
    CHECK(alpha_import_count == 0);
    CHECK(zeta_import_count == 0);
}

TEST_CASE("Registry import shares probe ranking and invokes only the winner", "[assets][registry][probe]")
{
    int weak_probe_count = 0;
    int strong_probe_count = 0;
    int weak_import_count = 0;
    int strong_import_count = 0;
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "weak",
        AssetProbeConfidence{99U},
        "weak",
        std::nullopt,
        &weak_import_count,
        nullptr,
        &weak_probe_count), 100));
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "strong",
        AssetProbeConfidence{100U},
        "strong",
        std::nullopt,
        &strong_import_count,
        nullptr,
        &strong_probe_count), -100));

    const auto result = registry.import(make_source());

    REQUIRE(result);
    CHECK(result.value().identity.source_name == "strong");
    CHECK(weak_probe_count == 1);
    CHECK(strong_probe_count == 1);
    CHECK(weak_import_count == 0);
    CHECK(strong_import_count == 1);
}

TEST_CASE("Registry rejects importer IDs beyond the diagnostic bound", "[assets][registry][probe]")
{
    ModelImporterRegistry registry;
    const std::string long_id(
        hlclient::assets::kMaximumAssetImporterIdBytes + 1U, 'x');

    const auto result = registry.register_importer(
        std::make_unique<SyntheticModelImporter>(
            long_id, AssetProbeConfidence{50U}, "unused"));

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          hlclient::assets::AssetImporterRegistrationErrorCode::ImporterIdTooLong);
    CHECK(result.error->importer_id.empty());
    CHECK(registry.size() == 0U);
}

TEST_CASE("Registry imports through a matching typed importer", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>(
            "synthetic-model", AssetProbeConfidence{25U}, "synthetic-result")));

    const auto result = registry.import(make_source());
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "synthetic-result");
}

TEST_CASE("Registry selects a synthetic importer by magic and version", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "synthetic-model-v1",
        kTestModelOneMagic,
        std::byte{1},
        12U,
        AssetProbeConfidence{100U})));
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "synthetic-model-v2",
        kTestModelTwoMagic,
        std::byte{2},
        12U,
        AssetProbeConfidence{100U})));

    const auto result = registry.import(make_source(
        "models/example.mdl", synthetic_header(kTestModelTwoMagic, std::byte{2})));
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "synthetic-model-v2");
}

TEST_CASE("A wrong extension hint cannot override a correct signature", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "extension-guess",
        kTestModelTwoMagic,
        std::byte{2},
        12U,
        AssetProbeConfidence{20U},
        true)));
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "signature-match",
        kTestModelOneMagic,
        std::byte{1},
        12U,
        AssetProbeConfidence{100U})));

    const auto result = registry.import(make_source(
        "models/misnamed.mdl", synthetic_header(kTestModelOneMagic, std::byte{1})));
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "signature-match");
}

TEST_CASE("One extension can route incompatible synthetic signatures", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "synthetic-one",
        kTestModelOneMagic,
        std::byte{1},
        12U,
        AssetProbeConfidence{100U})));
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "synthetic-two",
        kTestModelTwoMagic,
        std::byte{2},
        12U,
        AssetProbeConfidence{100U})));

    const auto first = registry.import(make_source(
        "models/shared.mdl", synthetic_header(kTestModelOneMagic, std::byte{1})));
    const auto second = registry.import(make_source(
        "models/shared.mdl", synthetic_header(kTestModelTwoMagic, std::byte{2})));

    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.value().identity.source_name == "synthetic-one");
    CHECK(second.value().identity.source_name == "synthetic-two");
}

TEST_CASE("A recognized but truncated synthetic header is malformed", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticHeaderImporter>(
        "synthetic-one",
        kTestModelOneMagic,
        std::byte{1},
        12U,
        AssetProbeConfidence{100U})));

    const auto result = registry.import(make_source(
        "models/truncated.mdl",
        std::vector<std::byte>(kTestModelOneMagic.begin(), kTestModelOneMagic.end())));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == AssetErrorCode::MalformedData);
    CHECK(result.error().importer_id == "synthetic-one");
    CHECK(result.error().context == "Synthetic header is truncated");
}

TEST_CASE("Registry selects the highest confidence", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>("weak", AssetProbeConfidence{10U}, "weak")));
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>("strong", AssetProbeConfidence{90U}, "strong")));

    const auto result = registry.import(make_source());
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "strong");
}

TEST_CASE("Confidence outranks explicit importer priority", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>(
            "priority", AssetProbeConfidence{20U}, "priority"), 100));
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>(
            "confidence", AssetProbeConfidence{21U}, "confidence"), -100));

    const auto result = registry.import(make_source());
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "confidence");
}

TEST_CASE("Priority breaks an equal-confidence tie", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>("low", AssetProbeConfidence{50U}, "low"), 4));
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>(
            "high", AssetProbeConfidence{50U}, "high"), 5));

    const auto result = registry.import(make_source());
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "high");
}

TEST_CASE("Exact selection ties are ambiguous and not registration ordered", "[assets][registry]")
{
    int first_import_count = 0;
    int second_import_count = 0;
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "zeta", AssetProbeConfidence{75U}, "zeta", std::nullopt, &first_import_count)));
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "alpha", AssetProbeConfidence{75U}, "alpha", std::nullopt, &second_import_count)));

    const auto result = registry.import(make_source());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == AssetErrorCode::AmbiguousFormat);
    const std::vector<std::string> expected_candidates{"alpha", "zeta"};
    CHECK(result.error().candidate_importer_ids == expected_candidates);
    CHECK(first_import_count == 0);
    CHECK(second_import_count == 0);
}

TEST_CASE("Registry reports unsupported format distinctly", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>(
            "never", AssetProbeConfidence{0U}, "never")));

    const auto result = registry.import(make_source("models/unknown.bin"));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == AssetErrorCode::UnsupportedFormat);
    CHECK(result.error().importer_id.empty());
}

TEST_CASE("Registry rejects duplicate stable importer IDs", "[assets][registry]")
{
    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(
        std::make_unique<SyntheticModelImporter>("same", AssetProbeConfidence{30U}, "first")));
    const auto duplicate = registry.register_importer(
        std::make_unique<SyntheticModelImporter>("same", AssetProbeConfidence{90U}, "second"));

    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
          hlclient::assets::AssetImporterRegistrationErrorCode::DuplicateImporterId);
    CHECK(registry.size() == 1U);
    const auto result = registry.import(make_source());
    REQUIRE(result);
    CHECK(result.value().identity.source_name == "first");
}

TEST_CASE("Registry rejects null and empty-ID importers", "[assets][registry]")
{
    ModelImporterRegistry registry;

    const auto null_result = registry.register_importer(nullptr);
    REQUIRE_FALSE(null_result);
    REQUIRE(null_result.error);
    CHECK(null_result.error->code ==
          hlclient::assets::AssetImporterRegistrationErrorCode::NullImporter);

    const auto empty_result = registry.register_importer(
        std::make_unique<SyntheticModelImporter>("", AssetProbeConfidence{50U}, "empty"));
    REQUIRE_FALSE(empty_result);
    REQUIRE(empty_result.error);
    CHECK(empty_result.error->code ==
          hlclient::assets::AssetImporterRegistrationErrorCode::EmptyImporterId);
    CHECK(registry.size() == 0U);
}

TEST_CASE("Registry owns registered importers for its own lifetime", "[assets][registry]")
{
    int destruction_count = 0;
    {
        ModelImporterRegistry registry;
        static_cast<void>(registry.register_importer(
            std::make_unique<SyntheticModelImporter>(
                "owned",
                AssetProbeConfidence{50U},
                "owned",
                std::nullopt,
                nullptr,
                &destruction_count)));
        CHECK(destruction_count == 0);
        CHECK(registry.size() == 1U);
        CHECK(registry.import(make_source()));
    }
    CHECK(destruction_count == 1);
}

TEST_CASE("Registry enriches importer errors without replacing their meaning", "[assets][registry]")
{
    AssetError decoder_error;
    decoder_error.code = AssetErrorCode::MalformedData;
    decoder_error.virtual_path = "wrong/path";
    decoder_error.importer_id = "wrong-importer";
    decoder_error.context = "synthetic header has an invalid length";

    ModelImporterRegistry registry;
    REQUIRE(registry.register_importer(std::make_unique<SyntheticModelImporter>(
        "synthetic-decoder", AssetProbeConfidence{100U}, "", decoder_error)));

    const auto result = registry.import(make_source("models/broken.mdl"));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == AssetErrorCode::MalformedData);
    CHECK(result.error().virtual_path == std::filesystem::path{"models/broken.mdl"});
    CHECK(result.error().importer_id == "synthetic-decoder");
    CHECK(result.error().context == "synthetic header has an invalid length");
}

TEST_CASE("Importer registry containers are independent values", "[assets][registry]")
{
    hlclient::assets::AssetImporterRegistries first;
    hlclient::assets::AssetImporterRegistries second;
    REQUIRE(first.models.register_importer(
        std::make_unique<SyntheticModelImporter>(
            "synthetic-model", AssetProbeConfidence{50U}, "first")));

    CHECK(first.models.size() == 1U);
    CHECK(second.models.size() == 0U);
    CHECK(first.images.size() == 0U);
}

} // namespace
