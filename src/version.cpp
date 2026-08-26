#include "lob/version.hpp"

// Translation unit so the library is a real static lib from commit 1 onward.
// version() is inline in the header; this file exists to give CMake a source.
namespace lob {
int library_anchor() noexcept { return 0; }
}  // namespace lob
