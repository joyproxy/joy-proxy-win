#include "joyproxy/security.h"

namespace joyproxy {

SECURITY_ATTRIBUTES* EveryoneSecurityAttributes() {
    static SECURITY_DESCRIPTOR sd{};
    static SECURITY_ATTRIBUTES sa{};
    static bool initialized = false;
    if (!initialized) {
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        initialized = true;
    }
    return &sa;
}

}  // namespace joyproxy
