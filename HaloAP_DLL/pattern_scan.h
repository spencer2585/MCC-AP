#pragma once
#pragma once

#include <windows.h>
#include <vector>
#include <string>

namespace haloap {

    // Parse a pattern string like "48 89 5C 24 08 ? ? ?? 48 81 EC"
    // where "?" or "??" are wildcards.
    // Returns (bytes, mask) where mask[i] = 1 if bytes[i] must match, 0 if wildcard.
    struct ParsedPattern {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;
    };
    ParsedPattern ParsePattern(const std::string& pattern);

    // Find a pattern in a module's loaded memory.
    // Returns the address of the first match, or nullptr if not found.
    void* FindPatternInModule(HMODULE module, const std::string& pattern);

}