#pragma once

namespace lob {

inline constexpr const char* kVersion = "0.1.0";

[[nodiscard]] inline const char* version() noexcept { return kVersion; }

}  // namespace lob
