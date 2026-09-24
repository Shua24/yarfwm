#ifndef RIVER_XKB_BINDINGS_V1_HPP
#define RIVER_XKB_BINDINGS_V1_HPP

// C++-safe inclusion of the generated river_xkb_bindings_v1 client header.
//
// The protocol names one argument of
// river_xkb_bindings_seat_v1.modifiers_update "new". Argument names are
// documentation only, so the vendored XML is kept byte identical to
// /usr/share/river-protocols/stable/river-xkb-bindings-v1.xml -- but "new" is a
// C++ keyword, and wayland-scanner's output is valid C, not C++. Rename the
// argument for the duration of the include and put the keyword back afterwards.
// Nothing else in the generated header uses the token, and comments and string
// literals are not macro-expanded.
#define new new_modifiers
#include "river-xkb-bindings-v1-client-protocol.h"
#undef new

#endif // RIVER_XKB_BINDINGS_V1_HPP
