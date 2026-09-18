#include "platform/settings_store.h"

#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace duel::platform {
namespace {

using game::input::ActionName;
using game::input::InputBindings;
using game::input::InputCodeName;
using game::input::ParseAction;
using game::input::ParseInputCode;
using Json = nlohmann::json;

bool HasOnlyKeys(const Json& object, std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, unused] : object.items()) {
        static_cast<void>(unused);
        bool found = false;
        for (const auto candidate : allowed) {
            if (key == candidate) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

Settings ParseSettings(const Json& document) {
    if (!document.is_object() || !HasOnlyKeys(document, {"schema_version", "bindings"})) {
        throw std::invalid_argument("settings root contains unsupported fields");
    }
    if (!document.contains("schema_version") || !document["schema_version"].is_number_unsigned()
        || document["schema_version"].get<std::uint32_t>() != kSettingsSchemaVersion) {
        throw std::invalid_argument("unsupported settings schema version");
    }
    if (!document.contains("bindings") || !document["bindings"].is_array()) {
        throw std::invalid_argument("settings bindings must be an array");
    }

    std::vector<game::input::Binding> bindings;
    for (const auto& value : document["bindings"]) {
        if (!value.is_object() || !HasOnlyKeys(value, {"action", "input"})
            || !value.contains("action") || !value["action"].is_string()
            || !value.contains("input") || !value["input"].is_string()) {
            throw std::invalid_argument("invalid binding entry");
        }
        const auto action = ParseAction(value["action"].get<std::string>());
        const auto input = ParseInputCode(value["input"].get<std::string>());
        if (!action || !input) {
            throw std::invalid_argument("unknown action or input code");
        }
        bindings.push_back({*action, *input});
    }

    std::string_view validationError;
    if (!InputBindings::Validate(bindings, &validationError)) {
        throw std::invalid_argument(std::string(validationError));
    }
    return Settings{
        .schemaVersion = kSettingsSchemaVersion,
        .bindings = std::move(bindings),
    };
}

bool AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    return MoveFileExW(
        temporary.c_str(),
        target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

} // namespace

Settings Settings::Defaults() {
    return Settings{
        .schemaVersion = kSettingsSchemaVersion,
        .bindings = game::input::InputBindings::Defaults().All(),
    };
}

SettingsStore::SettingsStore(std::filesystem::path path)
    : path_(std::move(path)) {
    if (path_.empty()) {
        throw std::invalid_argument("settings path cannot be empty");
    }
}

SettingsLoadResult SettingsStore::Load() const {
    std::ifstream input(path_, std::ios::binary);
    if (!input.is_open()) {
        std::error_code existenceError;
        const bool exists = std::filesystem::exists(path_, existenceError);
        if (!exists && !existenceError) {
            return {
                .settings = Settings::Defaults(),
                .usedDefaults = false,
                .warning = {},
            };
        }
        return {
            .settings = Settings::Defaults(),
            .usedDefaults = true,
            .warning = "Settings could not be opened; defaults are active",
        };
    }

    try {
        Json document;
        input >> document;
        return {
            .settings = ParseSettings(document),
            .usedDefaults = false,
            .warning = {},
        };
    } catch (const std::exception& exception) {
        return {
            .settings = Settings::Defaults(),
            .usedDefaults = true,
            .warning = std::string("Invalid settings; defaults are active: ") + exception.what(),
        };
    }
}

SettingsSaveResult SettingsStore::Save(const Settings& settings) const {
    std::string serialized;
    try {
        serialized = SerializeSettings(settings);
    } catch (const std::exception& exception) {
        return {.error = exception.what()};
    }

    std::error_code error;
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return {.error = "Could not create settings directory: " + error.message()};
        }
    }

    auto temporary = path_;
    temporary += ".tmp";
    std::filesystem::remove(temporary, error);
    error.clear();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return {.error = "Could not open temporary settings file"};
        }
        output << serialized;
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return {.error = "Could not write temporary settings file"};
        }
    }

    if (!AtomicReplace(temporary, path_)) {
        std::filesystem::remove(temporary, error);
        return {.error = "Could not atomically replace settings file"};
    }
    return {.ok = true, .error = {}};
}

const std::filesystem::path& SettingsStore::Path() const noexcept {
    return path_;
}

std::filesystem::path DefaultSettingsPath() {
#ifdef _WIN32
    if (const auto* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "pvp-duel" / "settings.json";
    }
#else
    if (const auto* configHome = std::getenv("XDG_CONFIG_HOME");
        configHome != nullptr && *configHome != '\0') {
        return std::filesystem::path(configHome) / "pvp-duel" / "settings.json";
    }
    if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "pvp-duel" / "settings.json";
    }
#endif
    return std::filesystem::current_path() / "pvp-duel-settings.json";
}

std::string SerializeSettings(const Settings& settings) {
    if (settings.schemaVersion != kSettingsSchemaVersion) {
        throw std::invalid_argument("unsupported settings schema version");
    }
    std::string_view validationError;
    if (!InputBindings::Validate(settings.bindings, &validationError)) {
        throw std::invalid_argument(std::string(validationError));
    }

    Json document{
        {"schema_version", settings.schemaVersion},
        {"bindings", Json::array()},
    };
    for (const auto& binding : settings.bindings) {
        document["bindings"].push_back({
            {"action", ActionName(binding.action)},
            {"input", InputCodeName(binding.input)},
        });
    }
    return document.dump(2) + '\n';
}

} // namespace duel::platform
