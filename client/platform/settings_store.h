#pragma once

#include "game/input/input.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace duel::platform {

inline constexpr std::uint32_t kSettingsSchemaVersion = 1;

struct Settings {
    std::uint32_t schemaVersion = kSettingsSchemaVersion;
    std::vector<game::input::Binding> bindings;

    [[nodiscard]] static Settings Defaults();
};

struct SettingsLoadResult {
    Settings settings;
    bool usedDefaults = false;
    std::string warning;
};

struct SettingsSaveResult {
    bool ok = false;
    std::string error;

    explicit operator bool() const noexcept { return ok; }
};

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path);

    [[nodiscard]] SettingsLoadResult Load() const;
    [[nodiscard]] SettingsSaveResult Save(const Settings& settings) const;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::filesystem::path DefaultSettingsPath();
[[nodiscard]] std::string SerializeSettings(const Settings& settings);

} // namespace duel::platform
