#pragma once

#include "model/Types.h"

namespace nc {

// Per-user system proxy (WinINet / "Internet Options"). Needs no elevation.
// The previous configuration is saved to disk before it is changed, so a
// crash can be repaired on the next launch by RestoreIfNeeded().
class SystemProxy {
public:
    static void Enable(const ProxyEndpoint& endpoint, const std::string& serverHost);
    static void Restore();
    // Restores a configuration left behind by a previous run that did not
    // shut down cleanly. Returns true when something was restored.
    static bool RestoreIfNeeded();
};

}  // namespace nc
