#include "pattern_scan.h"
#include <cctype>
#include <cstdio>

namespace haloap {

    ParsedPattern ParsePattern(const std::string& pattern) {
        ParsedPattern result;
        size_t i = 0;
        while (i < pattern.size()) {
            // Skip whitespace.
            while (i < pattern.size() && std::isspace((unsigned char)pattern[i])) i++;
            if (i >= pattern.size()) break;

            if (pattern[i] == '?') {
                // Wildcard. Accept either "?" or "??".
                result.bytes.push_back(0);
                result.mask.push_back(false);
                i++;
                if (i < pattern.size() && pattern[i] == '?') i++;
            }
            else {
                // Hex byte.
                char hex[3] = { pattern[i], 0, 0 };
                i++;
                if (i < pattern.size() && std::isxdigit((unsigned char)pattern[i])) {
                    hex[1] = pattern[i];
                    i++;
                }
                result.bytes.push_back((uint8_t)std::strtoul(hex, nullptr, 16));
                result.mask.push_back(true);
            }
        }
        return result;
    }

    void* FindPatternInModule(HMODULE module, const std::string& pattern) {
        if (!module) return nullptr;

        ParsedPattern parsed = ParsePattern(pattern);
        if (parsed.bytes.empty()) return nullptr;

        // Get the module's memory range from its PE headers.
        auto dosHeader = (IMAGE_DOS_HEADER*)module;
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        auto ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)module + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        uint8_t* base = (uint8_t*)module;
        size_t size = ntHeaders->OptionalHeader.SizeOfImage;

        // Linear scan. Could be faster with Boyer-Moore but this is fine for now.
        size_t patLen = parsed.bytes.size();
        if (size < patLen) return nullptr;

        size_t end = size - patLen;
        for (size_t i = 0; i <= end; i++) {
            bool match = true;
            for (size_t j = 0; j < patLen; j++) {
                if (parsed.mask[j] && base[i + j] != parsed.bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return base + i;
            }
        }

        return nullptr;
    }

}  // namespace haloap