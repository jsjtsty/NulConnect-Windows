#pragma once

#include <string>

namespace nc {

// Folders that hold this app's log and the privileged helper's logs.
std::wstring AppLogDirectory();
std::wstring HelperLogDirectory();

// Zips both log folders plus `summary` (as info.txt) into a new file on the
// desktop and returns its path. Throws AppError on failure.
std::wstring ExportDiagnostics(const std::string& summary);

}  // namespace nc
