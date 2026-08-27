#pragma once

#include <string>

enum class LegacyTextEncoding
{
    Auto,
    Gbk,
    Utf8
};

enum class DecodedTextEncoding
{
    Utf8,
    Utf8Bom,
    Gbk
};

class LegacyTextDecoder
{
public:
    static bool parseEncoding(
        const std::string& name,
        LegacyTextEncoding& encoding);
    static bool decodeToUtf8(
        const std::string& content,
        LegacyTextEncoding sourceEncoding,
        std::string& utf8Content,
        DecodedTextEncoding* detectedEncoding = nullptr,
        bool allowTruncatedTail = false);
    static const char* encodingName(DecodedTextEncoding encoding);
};
