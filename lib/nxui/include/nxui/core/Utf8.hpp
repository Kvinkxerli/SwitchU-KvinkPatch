#pragma once

#include <string>

namespace nxui {

// Removes the last Unicode code point (not just the last byte) from a UTF-8
// string. Truncating text with a plain pop_back() leaves half of a multi-byte
// character behind, which renders as a broken glyph for Cyrillic, accents,
// CJK and so on.
inline void popUtf8Codepoint(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80)
            break;
    }
}

} // namespace nxui
