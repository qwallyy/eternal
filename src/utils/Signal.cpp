#include "eternal/utils/Signal.hpp"

// Signal<Args...> and ConnectionGuard<Args...> are fully template-based and
// implemented entirely in the header.  This translation unit exists so that
// the build system has a corresponding .cpp for every header, and to provide
// an anchor for potential future non-template helpers.

namespace eternal {

// Intentionally empty - all logic lives in the header templates.

} // namespace eternal
