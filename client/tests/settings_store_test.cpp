#include "platform/settings_store.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

using duel::game::input::Action;
using duel::game::input::InputBindings;
using duel::game::input::InputCode;
using duel::platform::Settings;
using duel::platform::SettingsStore;

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

std::filesystem::path TemporaryDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("pvp-duel-settings-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void TestRoundTripAndCredentialSeparation() {
    const auto directory = TemporaryDirectory();
    const auto path = directory / "settings.json";
    SettingsStore store(path);

    auto bindings = InputBindings::Defaults();
    Require(bindings.Rebind(Action::MoveUp, InputCode::ArrowUp));
    Settings settings{.bindings = bindings.All()};
    Require(store.Save(settings).ok);

    const auto serialized = ReadFile(path);
    Require(serialized.find("arrow_up") != std::string::npos);
    Require(serialized.find("token") == std::string::npos);
    Require(serialized.find("password") == std::string::npos);
    Require(serialized.find("access") == std::string::npos);
    Require(serialized.find("refresh") == std::string::npos);

    const auto loaded = store.Load();
    Require(!loaded.usedDefaults);
    const InputBindings loadedBindings(loaded.settings.bindings);
    Require(loadedBindings.InputFor(Action::MoveUp) == InputCode::ArrowUp);
    std::filesystem::remove_all(directory);
}

void TestInvalidSavePreservesValidFile() {
    const auto directory = TemporaryDirectory();
    const auto path = directory / "settings.json";
    SettingsStore store(path);
    Require(store.Save(Settings::Defaults()).ok);
    const auto validContents = ReadFile(path);

    auto invalid = Settings::Defaults();
    invalid.bindings.back().input = invalid.bindings.front().input;
    const auto result = store.Save(invalid);
    Require(!result);
    Require(ReadFile(path) == validContents);
    Require(!std::filesystem::exists(path.string() + ".tmp"));
    std::filesystem::remove_all(directory);
}

void TestInvalidFileFallsBackWithoutOverwrite() {
    const auto directory = TemporaryDirectory();
    const auto path = directory / "settings.json";
    const std::string invalidContents = R"({"schema_version":1,"bindings":[]})";
    {
        std::ofstream output(path);
        output << invalidContents;
    }

    SettingsStore store(path);
    const auto loaded = store.Load();
    Require(loaded.usedDefaults);
    Require(!loaded.warning.empty());
    const InputBindings bindings(loaded.settings.bindings);
    Require(bindings.InputFor(Action::LightAttack) == InputCode::Space);
    Require(ReadFile(path) == invalidContents);
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    TestRoundTripAndCredentialSeparation();
    TestInvalidSavePreservesValidFile();
    TestInvalidFileFallsBackWithoutOverwrite();
    return 0;
}
