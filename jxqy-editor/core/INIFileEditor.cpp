#include "INIFileEditor.h"
#include "ini.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <limits>
#include <sstream>
#include <set>

static std::string toLowerAscii(const std::string& s)
{
    std::string result = s;
    for (char& character : result)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

static std::string trimRight(const std::string& s)
{
    size_t end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return "";
    return s.substr(0, end + 1);
}

static std::string trimLeft(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start);
}

static std::string trim(const std::string& s)
{
    return trimLeft(trimRight(s));
}

static std::string stripUtf8Bom(const std::string& s)
{
    static const std::string bom("\xEF\xBB\xBF", 3);
    return s.compare(0, bom.size(), bom) == 0 ? s.substr(bom.size()) : s;
}

INIFileEditor::INIFileEditor()
{
}

INIFileEditor::~INIFileEditor()
{
}

bool INIFileEditor::loadFromFile(const std::string& fileName)
{
    std::vector<uint8_t> buffer = Util::readFileToBuffer(fileName);
    if (buffer.empty())
    {
        error = -1;
        return false;
    }

    std::string content(buffer.begin(), buffer.end());
    return loadFromString(content);
}

bool INIFileEditor::loadFromString(const std::string& content)
{
    return loadFromString(content, UnrecognizedLinePolicy::Reject);
}

bool INIFileEditor::loadFromString(
    const std::string& content,
    UnrecognizedLinePolicy unrecognizedLinePolicy)
{
    iniMap.sections.clear();
    originalLines.clear();
    if (content.empty() || content.find('\0') != std::string::npos)
    {
        error = -1;
        return false;
    }

    // Store original lines for format preservation. Several production NPCRes
    // files use // as a whole-line comment even though the underlying INI
    // parser only recognizes ; and #. Normalize only the parser copy so the
    // original comment spelling survives a save round-trip.
    std::istringstream stream(content);
    std::string line;
    std::string parserContent;
    while (std::getline(stream, line))
    {
        // Normalize line endings: remove trailing \r
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        originalLines.push_back(line);

        std::string parserLine = originalLines.size() == 1
            ? stripUtf8Bom(line) : line;
        const size_t firstNonSpace = parserLine.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos &&
            parserLine.compare(firstNonSpace, 2, "//") == 0)
        {
            parserLine.replace(firstNonSpace, 2, ";;");
        }
        else if (firstNonSpace != std::string::npos &&
                 parserLine[firstNonSpace] == '[')
        {
            const size_t closeBracket = parserLine.find(']', firstNonSpace + 1);
            if (closeBracket != std::string::npos)
            {
                const std::string sectionName = trim(parserLine.substr(
                    firstNonSpace + 1, closeBracket - firstNonSpace - 1));
                parserLine.replace(firstNonSpace + 1,
                    closeBracket - firstNonSpace - 1, sectionName);
            }
        }
        else if (unrecognizedLinePolicy ==
                     UnrecognizedLinePolicy::Preserve &&
                 firstNonSpace != std::string::npos &&
                 parserLine[firstNonSpace] != ';' &&
                 parserLine[firstNonSpace] != '#' &&
                 parserLine.find_first_of("=:", firstNonSpace) ==
                     std::string::npos)
        {
            // Production dialogue files may contain author notes as bare
            // lines. Hide them from the strict parser while retaining the
            // original line for a preserving save.
            parserLine.insert(firstNonSpace, ";;");
        }
        parserContent += parserLine + "\n";
    }

    error = ini_parse_string(parserContent.c_str(), valueHandler, this);
    if (error != 0)
        return false;

    // ini_parse does not report empty sections through the value callback.
    // Keep them in the model so an editor does not silently delete a real,
    // intentionally empty action section.
    for (size_t lineIndex = 0; lineIndex < originalLines.size(); ++lineIndex)
    {
        const std::string& originalLine = originalLines[lineIndex];
        const std::string stripped = trim(originalLine);
        const std::string comparable = lineIndex == 0
            ? stripUtf8Bom(stripped) : stripped;
        const size_t closeBracket = comparable.find(']');
        if (comparable.size() < 2 || comparable.front() != '[' ||
            closeBracket == std::string::npos)
            continue;
        const std::string sectionName = trim(
            comparable.substr(1, closeBracket - 1));
        if (sectionName.empty())
            continue;
        const std::string lowerSection = toLowerAscii(sectionName);
        if (iniMap.sections.find(lowerSection) == iniMap.sections.end())
        {
            IniSection section;
            section.originalName = sectionName;
            iniMap.sections.emplace(lowerSection, std::move(section));
        }
    }
    return error == 0;
}

bool INIFileEditor::loadFromBuffer(const std::unique_ptr<char[]>& buffer)
{
    if (!buffer)
    {
        error = -1;
        return false;
    }
    return loadFromString(std::string(buffer.get()));
}

bool INIFileEditor::loadFromBuffer(const char* data, int length)
{
    return loadFromBuffer(
        data, length, UnrecognizedLinePolicy::Reject);
}

bool INIFileEditor::loadFromBuffer(
    const char* data,
    int length,
    UnrecognizedLinePolicy unrecognizedLinePolicy)
{
    if (!data || length <= 0)
    {
        error = -1;
        return false;
    }
    return loadFromString(
        std::string(data, length), unrecognizedLinePolicy);
}

bool INIFileEditor::saveToFile(const std::string& fileName) const
{
    std::string content = saveToString();
    return Util::writeFileFromBuffer(fileName, content.c_str(), content.size());
}

std::string INIFileEditor::saveToString() const
{
    // If we have original lines, use line-preserving output
    if (!originalLines.empty())
    {
        // Build a set of (lowercase section, lowercase key) -> value for all current keys
        std::map<std::string, std::map<std::string, std::string>> currentValues;
        for (auto& secIt : iniMap.sections)
        {
            auto& sectionValues = currentValues[secIt.first];
            for (auto& keyIt : secIt.second.keys)
            {
                sectionValues[keyIt.first] = keyIt.second.value;
            }
        }

        // Track which keys have been written
        std::map<std::string, std::set<std::string>> writtenKeys;

        std::string result;
        std::string currentLowerSection;
        bool currentSectionRemoved = false;

        for (size_t lineIndex = 0; lineIndex < originalLines.size(); ++lineIndex)
        {
            const std::string& line = originalLines[lineIndex];
            std::string trimmed = trim(line);
            const std::string comparable = lineIndex == 0
                ? stripUtf8Bom(trimmed) : trimmed;

            // Detect section header
            const size_t closeBracket = comparable.find(']');
            if (comparable.size() >= 2 && comparable[0] == '[' &&
                closeBracket != std::string::npos)
            {
                // Before switching section, append any new keys from previous section
                if (!currentLowerSection.empty())
                {
                    auto secIt = currentValues.find(currentLowerSection);
                    if (secIt != currentValues.end())
                    {
                        for (auto& kv : secIt->second)
                        {
                            if (writtenKeys[currentLowerSection].find(kv.first) == writtenKeys[currentLowerSection].end())
                            {
                                // Find original key name
                                auto iniSecIt = iniMap.sections.find(currentLowerSection);
                                if (iniSecIt != iniMap.sections.end())
                                {
                                    auto keyIt = iniSecIt->second.keys.find(kv.first);
                                    if (keyIt != iniSecIt->second.keys.end())
                                    {
                                        result += keyIt->second.originalName + "=" + kv.second + "\r\n";
                                    }
                                    else
                                    {
                                        result += kv.first + "=" + kv.second + "\r\n";
                                    }
                                }
                            }
                        }
                    }
                }

                // Extract section name and check if section was removed
                std::string sectionName = trim(
                    comparable.substr(1, closeBracket - 1));
                currentLowerSection = toLowerAscii(sectionName);
                currentSectionRemoved =
                    currentValues.find(currentLowerSection) == currentValues.end();

                // Skip section header if the section was removed
                if (currentSectionRemoved)
                {
                    continue;
                }

                // Mark the section itself as written even when it has no keys.
                // Otherwise a retained empty section is mistaken for an entirely
                // new section and appended a second time below.
                writtenKeys[currentLowerSection];
                result += line + "\r\n";
                continue;
            }

            // Removing a section removes its complete body, including comments
            // and malformed legacy lines, rather than leaking them into the
            // preceding section.
            if (currentSectionRemoved)
                continue;

            const bool wholeLineComment =
                !trimmed.empty() &&
                (trimmed[0] == ';' || trimmed[0] == '#' ||
                 trimmed.compare(0, 2, "//") == 0);

            // Detect key=value line
            if (!trimmed.empty() && !wholeLineComment)
            {
                size_t separatorPos = trimmed.find_first_of("=:");
                if (separatorPos != std::string::npos && separatorPos > 0)
                {
                    std::string keyName = trim(trimmed.substr(0, separatorPos));
                    std::string lowerKey = toLowerAscii(keyName);

                    if (!currentLowerSection.empty())
                    {
                        // Check if this key still exists in current data
                        auto secIt = currentValues.find(currentLowerSection);
                        if (secIt != currentValues.end())
                        {
                            auto valIt = secIt->second.find(lowerKey);
                            if (valIt == secIt->second.end())
                            {
                                // Key was removed — skip this line
                                writtenKeys[currentLowerSection].insert(lowerKey);
                                continue;
                            }

                            writtenKeys[currentLowerSection].insert(lowerKey);

                            // Preserve inline comment
                            size_t originalSeparatorPos = line.find_first_of("=:");
                            char separator = line[originalSeparatorPos];
                            std::string afterSeparator = line.substr(originalSeparatorPos + 1);
                            std::string inlineComment;
                            // The parser recognizes an inline semicolon only when it
                            // follows whitespace. A literal value such as "a;b" must
                            // not be duplicated as a comment during a save round-trip.
                            for (size_t i = 1; i < afterSeparator.size(); i++)
                            {
                                if (afterSeparator[i] == ';' &&
                                    std::isspace(static_cast<unsigned char>(afterSeparator[i - 1])))
                                {
                                    inlineComment = afterSeparator.substr(i);
                                    break;
                                }
                            }

                            // Preserve original key name case and separator from the line.
                            result += keyName + separator + valIt->second;
                            if (!inlineComment.empty())
                            {
                                result += " " + inlineComment;
                            }
                            result += "\r\n";
                            continue;
                        }
                        else
                        {
                            // Entire section was removed — skip this line
                            writtenKeys[currentLowerSection].insert(lowerKey);
                            continue;
                        }
                    }
                }
            }

            // Comment, blank line, or unrecognized line: preserve as-is
            result += line + "\r\n";
        }

        // Append new keys from the last section
        if (!currentLowerSection.empty())
        {
            auto secIt = currentValues.find(currentLowerSection);
            if (secIt != currentValues.end())
            {
                for (auto& kv : secIt->second)
                {
                    if (writtenKeys[currentLowerSection].find(kv.first) == writtenKeys[currentLowerSection].end())
                    {
                        auto iniSecIt = iniMap.sections.find(currentLowerSection);
                        if (iniSecIt != iniMap.sections.end())
                        {
                            auto keyIt = iniSecIt->second.keys.find(kv.first);
                            if (keyIt != iniSecIt->second.keys.end())
                            {
                                result += keyIt->second.originalName + "=" + kv.second + "\r\n";
                            }
                        }
                    }
                }
            }
        }

        // Append entirely new sections
        for (auto& secIt : iniMap.sections)
        {
            if (writtenKeys.find(secIt.first) == writtenKeys.end())
            {
                result += "\r\n[" + secIt.second.originalName + "]\r\n";
                for (auto& keyIt : secIt.second.keys)
                {
                    result += keyIt.second.originalName + "=" + keyIt.second.value + "\r\n";
                }
            }
        }

        return result;
    }

    // Fallback: no original lines, generate from data
    std::string result;
    for (auto sectionIt = iniMap.sections.begin(); sectionIt != iniMap.sections.end(); ++sectionIt)
    {
        result += "[" + sectionIt->second.originalName + "]\r\n";
        for (auto keyIt = sectionIt->second.keys.begin(); keyIt != sectionIt->second.keys.end(); ++keyIt)
        {
            result += keyIt->second.originalName + "=" + keyIt->second.value + "\r\n";
        }
        result += "\r\n";
    }
    return result;
}

int INIFileEditor::parseError() const
{
    return error;
}

std::string INIFileEditor::get(const std::string& section, const std::string& name, const std::string& defaultValue) const
{
    std::string lowerSection = toLowerAscii(section);
    std::string lowerName = toLowerAscii(name);

    auto sectionIt = iniMap.sections.find(lowerSection);
    if (sectionIt != iniMap.sections.end())
    {
        auto keyIt = sectionIt->second.keys.find(lowerName);
        if (keyIt != sectionIt->second.keys.end())
        {
            return keyIt->second.value;
        }
    }
    return defaultValue;
}

long INIFileEditor::getInteger(const std::string& section, const std::string& name, long defaultValue) const
{
    std::string valueString = get(section, name, "");
    if (valueString.empty())
        return defaultValue;
    try
    {
        return std::stol(valueString, nullptr, 0);
    }
    catch (const std::exception&)
    {
        return defaultValue;
    }
}

bool INIFileEditor::tryGetInt64(const std::string& section,
                                const std::string& name,
                                std::int64_t& value) const
{
    const std::string valueString = get(section, name, "");
    return tryParseInt64(valueString, value);
}

bool INIFileEditor::tryParseInt64(const std::string& text,
                                  std::int64_t& value)
{
    if (text.empty())
        return false;
    try
    {
        size_t parsedLength = 0;
        const long long parsed = std::stoll(text, &parsedLength, 0);
        if (parsedLength != text.size() ||
            parsed < static_cast<long long>(std::numeric_limits<std::int64_t>::min()) ||
            parsed > static_cast<long long>(std::numeric_limits<std::int64_t>::max()))
            return false;
        value = static_cast<std::int64_t>(parsed);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::int64_t INIFileEditor::getInt64(const std::string& section,
                                     const std::string& name,
                                     std::int64_t defaultValue) const
{
    std::int64_t value = 0;
    return tryGetInt64(section, name, value) ? value : defaultValue;
}

float INIFileEditor::getReal(const std::string& section, const std::string& name, float defaultValue) const
{
    std::string valueString = get(section, name, "");
    if (valueString.empty())
        return defaultValue;
    try
    {
        return std::stof(valueString);
    }
    catch (const std::exception&)
    {
        return defaultValue;
    }
}

bool INIFileEditor::getBoolean(const std::string& section, const std::string& name, bool defaultValue) const
{
    std::string valueString = get(section, name, "");
    if (valueString.empty())
        return defaultValue;
    std::string lower = toLowerAscii(valueString);
    if (lower == "true" || lower == "yes" || lower == "on" || lower == "1")
        return true;
    if (lower == "false" || lower == "no" || lower == "off" || lower == "0")
        return false;
    return defaultValue;
}

uint32_t INIFileEditor::getColor(const std::string& section, const std::string& name, uint32_t defaultValue) const
{
    unsigned char defaultData[3] = {
        (unsigned char)((defaultValue & 0xFF0000) >> 16),
        (unsigned char)((defaultValue & 0xFF00) >> 8),
        (unsigned char)(defaultValue & 0xFF)
    };
    std::string defaultString = std::to_string(defaultData[0]) + "," +
        std::to_string(defaultData[1]) + "," +
        std::to_string(defaultData[2]);

    std::string colorString = get(section, name, defaultString);

    unsigned char colorData[3] = { defaultData[0], defaultData[1], defaultData[2] };
    std::vector<std::string> parts;
    std::string current;
    for (char character : colorString)
    {
        if (character == ',')
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current += character;
        }
    }
    if (!current.empty())
        parts.push_back(current);

    for (size_t i = 0; i < (parts.size() > 3 ? 3 : parts.size()); i++)
    {
        if (!parts[i].empty())
        {
            try
            {
                colorData[i] = (unsigned char)std::stoul(parts[i]);
            }
            catch (const std::exception&)
            {
            }
        }
    }
    return 0xFF000000 | ((colorData[0] << 16) + (colorData[1] << 8) + colorData[2]);
}

void INIFileEditor::set(const std::string& section, const std::string& name, const std::string& value)
{
    std::string lowerSection = toLowerAscii(section);
    std::string lowerName = toLowerAscii(name);

    auto sectionIt = iniMap.sections.find(lowerSection);
    if (sectionIt == iniMap.sections.end())
    {
        IniSection newSection;
        newSection.originalName = section;
        newSection.keys[lowerName] = { name, value };
        iniMap.sections[lowerSection] = newSection;
    }
    else
    {
        auto keyIt = sectionIt->second.keys.find(lowerName);
        if (keyIt == sectionIt->second.keys.end())
        {
            sectionIt->second.keys[lowerName] = { name, value };
        }
        else
        {
            keyIt->second.value = value;
            // Preserve original name from first insertion unless the new name differs in case
        }
    }
}

void INIFileEditor::setInteger(const std::string& section, const std::string& name, long value)
{
    set(section, name, std::to_string(value));
}

void INIFileEditor::setInt64(const std::string& section,
                             const std::string& name,
                             std::int64_t value)
{
    set(section, name, std::to_string(value));
}

void INIFileEditor::setReal(const std::string& section, const std::string& name, float value)
{
    set(section, name, std::to_string(value));
}

void INIFileEditor::setBoolean(const std::string& section, const std::string& name, bool value)
{
    set(section, name, value ? "1" : "0");
}

void INIFileEditor::setColor(const std::string& section, const std::string& name, uint32_t value)
{
    unsigned char colorData[3] = {
        (unsigned char)((value & 0xFF0000) >> 16),
        (unsigned char)((value & 0xFF00) >> 8),
        (unsigned char)(value & 0xFF)
    };
    std::string colorString = std::to_string(colorData[0]) + "," +
        std::to_string(colorData[1]) + "," +
        std::to_string(colorData[2]);
    set(section, name, colorString);
}

bool INIFileEditor::hasSection(const std::string& section) const
{
    return iniMap.sections.find(toLowerAscii(section)) != iniMap.sections.end();
}

bool INIFileEditor::hasKey(const std::string& section, const std::string& name) const
{
    std::string lowerSection = toLowerAscii(section);
    std::string lowerName = toLowerAscii(name);

    auto sectionIt = iniMap.sections.find(lowerSection);
    if (sectionIt == iniMap.sections.end())
        return false;
    return sectionIt->second.keys.find(lowerName) != sectionIt->second.keys.end();
}

bool INIFileEditor::addSection(const std::string& section)
{
    const std::string sectionName = trim(section);
    if (sectionName.empty() || sectionName.find_first_of("[]\r\n") != std::string::npos)
        return false;

    const std::string lowerSection = toLowerAscii(sectionName);
    if (iniMap.sections.find(lowerSection) != iniMap.sections.end())
        return false;

    IniSection newSection;
    newSection.originalName = sectionName;
    iniMap.sections.emplace(lowerSection, std::move(newSection));
    return true;
}

std::vector<std::string> INIFileEditor::getSectionNames() const
{
    std::vector<std::string> names;
    for (auto it = iniMap.sections.begin(); it != iniMap.sections.end(); ++it)
    {
        names.push_back(it->second.originalName);
    }
    return names;
}

std::vector<std::string> INIFileEditor::getKeyNames(const std::string& section) const
{
    std::vector<std::string> names;
    std::string lowerSection = toLowerAscii(section);

    auto sectionIt = iniMap.sections.find(lowerSection);
    if (sectionIt != iniMap.sections.end())
    {
        for (auto it = sectionIt->second.keys.begin(); it != sectionIt->second.keys.end(); ++it)
        {
            names.push_back(it->second.originalName);
        }
    }
    return names;
}

void INIFileEditor::removeSection(const std::string& section)
{
    iniMap.sections.erase(toLowerAscii(section));
}

void INIFileEditor::removeKey(const std::string& section, const std::string& name)
{
    std::string lowerSection = toLowerAscii(section);
    std::string lowerName = toLowerAscii(name);

    auto sectionIt = iniMap.sections.find(lowerSection);
    if (sectionIt != iniMap.sections.end())
    {
        sectionIt->second.keys.erase(lowerName);
    }
}

const IniMap& INIFileEditor::getIniMap() const
{
    return iniMap;
}

IniMap& INIFileEditor::getIniMapRef()
{
    return iniMap;
}

int INIFileEditor::valueHandler(void* user, const char* section, const char* name, const char* value)
{
    INIFileEditor* reader = static_cast<INIFileEditor*>(user);
    reader->set(section, name, value);
    return 1;
}
