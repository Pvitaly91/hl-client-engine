#include <hlclient/assets/asset_manager.hpp>
#include <hlclient/filesystem/rooted_file_system.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace filesystem = hlclient::filesystem;

class InMemoryFileSystem final : public filesystem::IFileSystem {
public:
    void add_file(
        std::filesystem::path virtual_path,
        std::vector<std::byte> bytes,
        std::optional<filesystem::FileMetadata> metadata = std::nullopt)
    {
        const auto key = virtual_path.generic_string();
        files_.insert_or_assign(
            key,
            filesystem::FileContents{
                std::move(virtual_path), std::move(bytes), std::move(metadata)});
    }

    void add_returned_path(
        const std::filesystem::path& requested_path,
        std::filesystem::path returned_path)
    {
        files_.insert_or_assign(
            requested_path.generic_string(),
            filesystem::FileContents{std::move(returned_path), {}, std::nullopt});
    }

    [[nodiscard]] filesystem::FileReadResult read_file(
        const std::filesystem::path& virtual_path) const override
    {
        ++read_count_;
        const auto found = files_.find(virtual_path.generic_string());
        if (found == files_.end()) {
            return filesystem::FileReadResult{
                std::nullopt,
                filesystem::FileReadError{
                    filesystem::FileReadErrorCode::NotFound,
                    virtual_path,
                    "Synthetic file was not found",
                },
            };
        }
        return filesystem::FileReadResult{found->second, std::nullopt};
    }

    [[nodiscard]] std::size_t read_count() const noexcept
    {
        return read_count_;
    }

private:
    std::unordered_map<std::string, filesystem::FileContents> files_;
    mutable std::size_t read_count_{0};
};

template<class Asset>
class RecordingImporter final : public assets::IAssetImporter<Asset> {
public:
    RecordingImporter(
        std::string importer_id,
        int* import_count = nullptr,
        std::optional<assets::AssetError> failure = std::nullopt,
        std::string supplied_source_name = {})
        : importer_id_{std::move(importer_id)},
          import_count_{import_count},
          failure_{std::move(failure)},
          supplied_source_name_{std::move(supplied_source_name)}
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return importer_id_;
    }

    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override
    {
        observed_extension_is_mdl_ = probe.extension_hint == ".mdl";
        observed_signature_ = probe.signature.empty() ? std::byte{} : probe.signature.front();
        observed_size_ = probe.structural_bytes.size();
        observed_version_ = probe.version_hint;
        return assets::AssetProbeConfidence{100U};
    }

    [[nodiscard]] assets::AssetResult<Asset> import(
        const assets::AssetSource&) const override
    {
        if (import_count_ != nullptr) {
            ++*import_count_;
        }
        if (failure_) {
            return assets::AssetResult<Asset>::failure(*failure_);
        }

        Asset result;
        result.identity.source_name = supplied_source_name_;
        return assets::AssetResult<Asset>::success(std::move(result));
    }

    [[nodiscard]] bool observed_extension_is_mdl() const noexcept
    {
        return observed_extension_is_mdl_;
    }

    [[nodiscard]] std::byte observed_signature() const noexcept
    {
        return observed_signature_;
    }

    [[nodiscard]] std::size_t observed_size() const noexcept
    {
        return observed_size_;
    }

    [[nodiscard]] std::optional<std::uint32_t> observed_version() const noexcept
    {
        return observed_version_;
    }

private:
    std::string importer_id_;
    int* import_count_{nullptr};
    std::optional<assets::AssetError> failure_;
    std::string supplied_source_name_;
    mutable bool observed_extension_is_mdl_{false};
    mutable std::byte observed_signature_{};
    mutable std::size_t observed_size_{0};
    mutable std::optional<std::uint32_t> observed_version_;
};

template<class Registry, class Asset>
void add_recording_importer(Registry& registry, std::string id, int* import_count)
{
    const auto registered = registry.register_importer(
        std::make_unique<RecordingImporter<Asset>>(std::move(id), import_count));
    if (!registered) {
        throw std::runtime_error{"Unable to register synthetic importer"};
    }
}

TEST_CASE("AssetManager routes every neutral asset type", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    file_system.add_file("models/barney.mdl", {std::byte{1}});
    file_system.add_file("maps/crossfire.bsp", {std::byte{2}});
    file_system.add_file("sprites/laser.spr", {std::byte{3}});
    file_system.add_file("gfx/loading.tga", {std::byte{4}});
    file_system.add_file("sound/button.wav", {std::byte{5}});

    int model_count = 0;
    int world_count = 0;
    int sprite_count = 0;
    int image_count = 0;
    int audio_count = 0;
    assets::AssetImporterRegistries importers;
    add_recording_importer<assets::ModelImporterRegistry, assets::ModelAsset>(
        importers.models, "model", &model_count);
    add_recording_importer<assets::WorldImporterRegistry, assets::WorldAsset>(
        importers.worlds, "world", &world_count);
    add_recording_importer<assets::SpriteImporterRegistry, assets::SpriteAsset>(
        importers.sprites, "sprite", &sprite_count);
    add_recording_importer<assets::ImageImporterRegistry, assets::ImageAsset>(
        importers.images, "image", &image_count);
    add_recording_importer<assets::AudioImporterRegistry, assets::AudioAsset>(
        importers.audio, "audio", &audio_count);

    const assets::AssetManager manager{file_system, importers};
    const auto model = manager.load_model("models/barney.mdl");
    const auto world = manager.load_world("maps/crossfire.bsp");
    const auto sprite = manager.load_sprite("sprites/laser.spr");
    const auto image = manager.load_image("gfx/loading.tga");
    const auto audio = manager.load_audio("sound/button.wav");

    REQUIRE(model);
    REQUIRE(world);
    REQUIRE(sprite);
    REQUIRE(image);
    REQUIRE(audio);
    CHECK(model.value().identity.source_name == "models/barney.mdl");
    CHECK(world.value().identity.source_name == "maps/crossfire.bsp");
    CHECK(sprite.value().identity.source_name == "sprites/laser.spr");
    CHECK(image.value().identity.source_name == "gfx/loading.tga");
    CHECK(audio.value().identity.source_name == "sound/button.wav");
    CHECK(model_count == 1);
    CHECK(world_count == 1);
    CHECK(sprite_count == 1);
    CHECK(image_count == 1);
    CHECK(audio_count == 1);
}

TEST_CASE("AssetManager passes owned filesystem bytes and metadata to probing", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    file_system.add_file(
        "models/otis.MDL",
        {std::byte{0x54}, std::byte{0x45}, std::byte{0x53}, std::byte{0x54}},
        filesystem::FileMetadata{4U, std::nullopt});

    assets::AssetImporterRegistries importers;
    auto importer = std::make_unique<RecordingImporter<assets::ModelAsset>>("synthetic-model");
    const auto* observed = importer.get();
    REQUIRE(importers.models.register_importer(std::move(importer)));

    const assets::AssetManager manager{file_system, importers};
    const auto result = manager.load_model("models/otis.MDL");

    REQUIRE(result);
    CHECK(observed->observed_extension_is_mdl());
    CHECK(observed->observed_signature() == std::byte{0x54});
    CHECK(observed->observed_size() == 4U);
    CHECK_FALSE(observed->observed_version());
}

TEST_CASE("AssetManager maps filesystem failures without invoking an importer", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    int import_count = 0;
    assets::AssetImporterRegistries importers;
    add_recording_importer<assets::ModelImporterRegistry, assets::ModelAsset>(
        importers.models, "synthetic-model", &import_count);

    const assets::AssetManager manager{file_system, importers};
    const auto result = manager.load_model("models/missing.mdl");

    REQUIRE_FALSE(result);
    CHECK(result.error().code == assets::AssetErrorCode::SourceReadFailed);
    CHECK(result.error().virtual_path == std::filesystem::path{"models/missing.mdl"});
    CHECK(result.error().context == "Synthetic file was not found");
    CHECK(import_count == 0);
}

TEST_CASE("AssetManager does not mask unsupported formats as missing files", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    file_system.add_file("models/unknown.asset", {std::byte{0x7F}});
    assets::AssetImporterRegistries importers;

    const assets::AssetManager manager{file_system, importers};
    const auto result = manager.load_model("models/unknown.asset");

    REQUIRE_FALSE(result);
    CHECK(result.error().code == assets::AssetErrorCode::UnsupportedFormat);
    CHECK(result.error().virtual_path == std::filesystem::path{"models/unknown.asset"});
    CHECK(file_system.read_count() == 1U);
}

TEST_CASE("AssetManager validates paths returned by filesystem providers", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    file_system.add_returned_path("models/request.mdl", "../outside.mdl");
    assets::AssetImporterRegistries importers;
    add_recording_importer<assets::ModelImporterRegistry, assets::ModelAsset>(
        importers.models, "synthetic-model", nullptr);

    const assets::AssetManager manager{file_system, importers};
    const auto result = manager.load_model("models/request.mdl");

    REQUIRE_FALSE(result);
    CHECK(result.error().code == assets::AssetErrorCode::InvalidVirtualPath);
    CHECK(result.error().virtual_path == std::filesystem::path{"../outside.mdl"});
}

TEST_CASE("AssetManager preserves selected importer failure details", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    file_system.add_file("models/broken.mdl", {std::byte{0}});
    assets::AssetError decoder_error;
    decoder_error.code = assets::AssetErrorCode::MalformedData;
    decoder_error.context = "invalid synthetic model header";

    assets::AssetImporterRegistries importers;
    REQUIRE(importers.models.register_importer(
        std::make_unique<RecordingImporter<assets::ModelAsset>>(
            "synthetic-model-v10", nullptr, decoder_error)));

    const assets::AssetManager manager{file_system, importers};
    const auto result = manager.load_model("models/broken.mdl");

    REQUIRE_FALSE(result);
    CHECK(result.error().code == assets::AssetErrorCode::MalformedData);
    CHECK(result.error().importer_id == "synthetic-model-v10");
    CHECK(result.error().virtual_path == std::filesystem::path{"models/broken.mdl"});
    CHECK(result.error().context == "invalid synthetic model header");
}

TEST_CASE("AssetManager performs a fresh read and import for every load", "[assets][manager]")
{
    InMemoryFileSystem file_system;
    file_system.add_file("models/repeated.mdl", {std::byte{0}});
    int import_count = 0;
    assets::AssetImporterRegistries importers;
    REQUIRE(importers.models.register_importer(
        std::make_unique<RecordingImporter<assets::ModelAsset>>(
            "synthetic-model", &import_count, std::nullopt, "importer-owned-name")));

    const assets::AssetManager manager{file_system, importers};
    const auto first = manager.load_model("models/repeated.mdl");
    const auto second = manager.load_model("models/repeated.mdl");

    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.value().identity.source_name == "importer-owned-name");
    CHECK(second.value().identity.source_name == "importer-owned-name");
    CHECK(file_system.read_count() == 2U);
    CHECK(import_count == 2);
}

class ScopedTemporaryDirectory final {
public:
    ScopedTemporaryDirectory()
        : temporary_root_{std::filesystem::temp_directory_path().lexically_normal()}
    {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0; attempt < 100U; ++attempt) {
            const auto name = std::string{"hlclient-asset-tests-"} +
                              std::to_string(timestamp) + '-' + std::to_string(attempt);
            auto candidate = temporary_root_ / name;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error) {
                throw std::runtime_error{"Unable to create test directory: " + error.message()};
            }
        }
        throw std::runtime_error{"Unable to allocate a unique test directory"};
    }

    ~ScopedTemporaryDirectory()
    {
        const auto normalized = path_.lexically_normal();
        if (!normalized.empty() && normalized.parent_path() == temporary_root_ &&
            normalized.filename().string().starts_with("hlclient-asset-tests-")) {
            std::error_code ignored;
            std::filesystem::remove_all(normalized, ignored);
        }
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path temporary_root_;
    std::filesystem::path path_;
};

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::ofstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"Unable to create synthetic binary file"};
    }
    if (!bytes.empty()) {
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) {
        throw std::runtime_error{"Unable to write synthetic binary file"};
    }
}

TEST_CASE("RootedFileSystem reads a contained binary file", "[filesystem][assets]")
{
    ScopedTemporaryDirectory temporary;
    REQUIRE(std::filesystem::create_directory(temporary.path() / "models"));
    const std::vector<std::byte> expected{std::byte{0}, std::byte{0xFF}, std::byte{0x2A}};
    write_bytes(temporary.path() / "models" / "binary.mdl", expected);

    auto created = filesystem::RootedFileSystem::create(temporary.path());
    REQUIRE(created);
    const auto result = created.file_system->read_file("models/binary.mdl");

    REQUIRE(result);
    CHECK(result.file->virtual_path == std::filesystem::path{"models/binary.mdl"});
    CHECK(result.file->bytes == expected);
    REQUIRE(result.file->metadata);
    CHECK(result.file->metadata->size == expected.size());
}

TEST_CASE("RootedFileSystem rejects traversal and absolute virtual paths", "[filesystem][assets]")
{
    ScopedTemporaryDirectory temporary;
    auto created = filesystem::RootedFileSystem::create(temporary.path());
    REQUIRE(created);

    const auto traversal = created.file_system->read_file("../outside.bin");
    const auto absolute = created.file_system->read_file(temporary.path());

    REQUIRE_FALSE(traversal);
    REQUIRE(traversal.error);
    CHECK(traversal.error->code == filesystem::FileReadErrorCode::InvalidVirtualPath);
    REQUIRE_FALSE(absolute);
    REQUIRE(absolute.error);
    CHECK(absolute.error->code == filesystem::FileReadErrorCode::InvalidVirtualPath);
}

TEST_CASE("RootedFileSystem distinguishes missing and non-regular paths", "[filesystem][assets]")
{
    ScopedTemporaryDirectory temporary;
    REQUIRE(std::filesystem::create_directory(temporary.path() / "directory"));
    auto created = filesystem::RootedFileSystem::create(temporary.path());
    REQUIRE(created);

    const auto missing = created.file_system->read_file("missing.bin");
    const auto directory = created.file_system->read_file("directory");

    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code == filesystem::FileReadErrorCode::NotFound);
    REQUIRE_FALSE(directory);
    REQUIRE(directory.error);
    CHECK(directory.error->code == filesystem::FileReadErrorCode::NotRegularFile);
}

TEST_CASE("RootedFileSystem enforces a positive configured size limit", "[filesystem][assets]")
{
    ScopedTemporaryDirectory temporary;
    write_bytes(
        temporary.path() / "large.bin",
        {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    const auto invalid = filesystem::RootedFileSystem::create(temporary.path(), 0U);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code == filesystem::FileReadErrorCode::InvalidConfiguration);

    auto limited = filesystem::RootedFileSystem::create(temporary.path(), 3U);
    REQUIRE(limited);
    const auto result = limited.file_system->read_file("large.bin");
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == filesystem::FileReadErrorCode::TooLarge);
}

TEST_CASE("RootedFileSystem rejects a symlink escape when links are available", "[filesystem][assets]")
{
    ScopedTemporaryDirectory temporary;
    const auto root = temporary.path() / "root";
    REQUIRE(std::filesystem::create_directory(root));
    write_bytes(temporary.path() / "outside.bin", {std::byte{0x2A}});

    std::error_code error;
    std::filesystem::create_symlink(
        temporary.path() / "outside.bin", root / "linked.bin", error);
    if (error) {
        SKIP("File symlinks are unavailable in this test environment: " << error.message());
    }

    auto created = filesystem::RootedFileSystem::create(root);
    REQUIRE(created);
    const auto result = created.file_system->read_file("linked.bin");
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == filesystem::FileReadErrorCode::InvalidVirtualPath);
}

} // namespace
