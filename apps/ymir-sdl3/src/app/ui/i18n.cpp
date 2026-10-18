#include "i18n.hpp"

#include <cmrc/cmrc.hpp>

#include <toml++/toml.hpp>

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

CMRC_DECLARE(Ymir_sdl3_rc);

namespace app::ui::i18n {
namespace {

using Catalog = std::unordered_map<std::string, std::string>;

std::string NormalizeLanguage(std::string_view language) {
    std::string normalized{};
    for (const char character : language) {
        if (character == '_' || character == '-' || character == '.' || character == '@') {
            break;
        }
        normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return normalized;
}

std::string SystemLanguage() {
    for (const char *name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const char *locale = std::getenv(name); locale != nullptr && *locale != '\0') {
            return NormalizeLanguage(locale);
        }
    }
    return {};
}

Catalog LoadCatalog(std::string_view language) {
    const auto resources = cmrc::Ymir_sdl3_rc::get_filesystem();
    const std::string path = "i18n/" + std::string{language} + ".toml";
    if (!resources.exists(path)) {
        return {};
    }

    const cmrc::file file = resources.open(path);
    auto result = toml::parse(std::string{file.begin(), file.end()});
    if (!result) {
        return {};
    }
    const toml::table *strings = result.table()["strings"].as_table();
    if (strings == nullptr) {
        return {};
    }

    Catalog catalog{};
    for (const auto &[key, value] : *strings) {
        if (const auto translation = value.value<std::string>()) {
            catalog.emplace(std::string{key.str()}, *translation);
        }
    }
    return catalog;
}

const Catalog *FindCatalog(std::string_view language) {
    static std::unordered_map<std::string, Catalog> catalogs{};
    auto [it, inserted] = catalogs.try_emplace(std::string{language});
    if (inserted) {
        try {
            it->second = LoadCatalog(language);
        } catch (const toml::parse_error &) {
            return nullptr;
        }
    }
    return it->second.empty() ? nullptr : &it->second;
}

std::string g_language{};

} // namespace

void SetLanguage(std::string_view language) {
    g_language = NormalizeLanguage(language);
}

std::string_view CurrentLanguage() {
    if (!g_language.empty()) {
        return g_language;
    }
    static std::string systemLanguage{};
    systemLanguage = SystemLanguage();
    return systemLanguage;
}

const char *tr(const char *english) {
    const Catalog *catalog = FindCatalog(CurrentLanguage());
    if (catalog == nullptr) {
        return english;
    }
    const auto it = catalog->find(english);
    return it == catalog->end() ? english : it->second.c_str();
}

} // namespace app::ui::i18n
