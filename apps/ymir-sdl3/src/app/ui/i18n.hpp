#pragma once

#include <string_view>

namespace app::ui::i18n {

/// Selects the ISO 639-1 language used for UI strings. An empty tag follows the
/// process locale (LC_ALL, LC_MESSAGES, then LANG).
void SetLanguage(std::string_view language);

/// Returns the selected language, or the language resolved from the process locale.
std::string_view CurrentLanguage();

/// Returns a localized UI string from the registered language catalog.
/// English is returned when the selected language or translation is unavailable.
const char *tr(const char *english);

} // namespace app::ui::i18n
