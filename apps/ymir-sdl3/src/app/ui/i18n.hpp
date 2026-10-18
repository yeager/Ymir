#pragma once

namespace app::ui::i18n {

/// Returns the Swedish UI string when the process locale is Swedish.
/// English remains the fallback while more languages are added.
const char *tr(const char *english);

} // namespace app::ui::i18n
