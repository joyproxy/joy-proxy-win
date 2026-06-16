#pragma once

#include <Windows.h>

namespace joyproxy {

// NULL DACL: allow same-session standard + elevated processes to connect.
SECURITY_ATTRIBUTES* EveryoneSecurityAttributes();

}  // namespace joyproxy
