#include "LegacyTextDecoder.h"

#include "Util.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace
{
std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool isGbkLeadByte(unsigned char byte)
{
    return byte >= 0x81 && byte <= 0xFE;
}

bool isValidGbk(const std::string& bytes)
{
    std::size_t index = 0;
    while (index < bytes.size())
    {
        const unsigned char first =
            static_cast<unsigned char>(bytes[index]);
        if (first <= 0x7F || first == 0x80)
        {
            index++;
            continue;
        }
        if (!isGbkLeadByte(first) || index + 1 >= bytes.size())
        {
            return false;
        }
        const unsigned char second =
            static_cast<unsigned char>(bytes[index + 1]);
        if (second < 0x40 || second > 0xFE || second == 0x7F)
        {
            return false;
        }
        index += 2;
    }
    return true;
}

bool trimTruncatedUtf8(std::string& bytes)
{
    if (Util::isUtf8(
            reinterpret_cast<const std::uint8_t*>(bytes.data()),
            bytes.size()))
    {
        return true;
    }
    for (int count = 1; count <= 3 &&
         count < static_cast<int>(bytes.size()); count++)
    {
        const std::string candidate =
            bytes.substr(0, bytes.size() - count);
        if (Util::isUtf8(
                reinterpret_cast<const std::uint8_t*>(candidate.data()),
                candidate.size()))
        {
            bytes = candidate;
            return true;
        }
    }
    return false;
}
}

bool LegacyTextDecoder::parseEncoding(
    const std::string& name,
    LegacyTextEncoding& encoding)
{
    const std::string normalized = lowerAscii(name);
    if (normalized == "gbk" || normalized == "cp936")
    {
        encoding = LegacyTextEncoding::Gbk;
        return true;
    }
    if (normalized == "utf8" || normalized == "utf-8")
    {
        encoding = LegacyTextEncoding::Utf8;
        return true;
    }
    return false;
}

bool LegacyTextDecoder::decodeToUtf8(
    const std::string& content,
    LegacyTextEncoding sourceEncoding,
    std::string& utf8Content,
    DecodedTextEncoding* detectedEncoding,
    bool allowTruncatedTail)
{
    utf8Content.clear();
    std::string payload = content;
    const bool hadUtf8Bom = payload.size() >= 3 &&
        Util::isUtf8Bom(
            reinterpret_cast<const std::uint8_t*>(payload.data()),
            payload.size());
    if (hadUtf8Bom)
    {
        payload.erase(0, 3);
    }

    std::string utf8Candidate = payload;
    const bool validUtf8 = allowTruncatedTail
        ? trimTruncatedUtf8(utf8Candidate)
        : Util::isUtf8(
            reinterpret_cast<const std::uint8_t*>(utf8Candidate.data()),
            utf8Candidate.size());
    if (sourceEncoding == LegacyTextEncoding::Auto &&
        hadUtf8Bom && !validUtf8)
    {
        return false;
    }
    if (sourceEncoding == LegacyTextEncoding::Utf8)
    {
        if (!validUtf8)
        {
            return false;
        }
        utf8Content = utf8Candidate;
        if (detectedEncoding != nullptr)
        {
            *detectedEncoding = hadUtf8Bom
                ? DecodedTextEncoding::Utf8Bom
                : DecodedTextEncoding::Utf8;
        }
        return true;
    }

    if (sourceEncoding == LegacyTextEncoding::Gbk)
    {
        utf8Content = Util::gbkToUtf8(payload);
        if (!payload.empty() && utf8Content.empty())
        {
            return false;
        }
        if (detectedEncoding != nullptr)
        {
            *detectedEncoding = DecodedTextEncoding::Gbk;
        }
        return true;
    }

    if (sourceEncoding == LegacyTextEncoding::Auto && validUtf8 &&
        !Util::isLikelyGbkTextMisreadAsUtf8(utf8Candidate))
    {
        utf8Content = utf8Candidate;
        if (detectedEncoding != nullptr)
        {
            *detectedEncoding = hadUtf8Bom
                ? DecodedTextEncoding::Utf8Bom
                : DecodedTextEncoding::Utf8;
        }
        return true;
    }

    std::string gbkCandidate = payload;
    if (!isValidGbk(gbkCandidate))
    {
        if (!allowTruncatedTail || gbkCandidate.empty() ||
            !isGbkLeadByte(
                static_cast<unsigned char>(gbkCandidate.back())))
        {
            return false;
        }
        gbkCandidate.pop_back();
        if (!isValidGbk(gbkCandidate))
        {
            return false;
        }
    }

    utf8Content = Util::gbkToUtf8(gbkCandidate);
    if ((!gbkCandidate.empty() && utf8Content.empty()) ||
        !Util::isUtf8(
            reinterpret_cast<const std::uint8_t*>(utf8Content.data()),
            utf8Content.size()))
    {
        utf8Content.clear();
        return false;
    }
    if (detectedEncoding != nullptr)
    {
        *detectedEncoding = DecodedTextEncoding::Gbk;
    }
    return true;
}

const char* LegacyTextDecoder::encodingName(
    DecodedTextEncoding encoding)
{
    switch (encoding)
    {
    case DecodedTextEncoding::Utf8Bom:
        return "UTF-8 BOM";
    case DecodedTextEncoding::Gbk:
        return "GBK";
    case DecodedTextEncoding::Utf8:
    default:
        return "UTF-8";
    }
}
