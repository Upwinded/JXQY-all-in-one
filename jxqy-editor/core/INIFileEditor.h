#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

struct IniKeyEntry
{
    std::string originalName;
    std::string value;
};

struct IniSection
{
    std::string originalName;
    std::map<std::string, IniKeyEntry> keys;  // lookup key is lowercase
};

struct IniMap
{
    std::map<std::string, IniSection> sections;  // lookup key is lowercase section name
};

class INIFileEditor
{
public:
    enum class UnrecognizedLinePolicy
    {
        Reject,
        Preserve
    };

    INIFileEditor();
    ~INIFileEditor();

    bool loadFromFile(const std::string& fileName);
    bool loadFromString(const std::string& content);
    bool loadFromString(const std::string& content,
                        UnrecognizedLinePolicy unrecognizedLinePolicy);
    bool loadFromBuffer(const std::unique_ptr<char[]>& buffer);
    bool loadFromBuffer(const char* data, int length);
    bool loadFromBuffer(const char* data, int length,
                        UnrecognizedLinePolicy unrecognizedLinePolicy);

    bool saveToFile(const std::string& fileName) const;
    std::string saveToString() const;

    int parseError() const;

    std::string get(const std::string& section, const std::string& name, const std::string& defaultValue = "") const;
    long getInteger(const std::string& section, const std::string& name, long defaultValue = 0) const;
    bool tryGetInt64(const std::string& section, const std::string& name,
                     std::int64_t& value) const;
    std::int64_t getInt64(const std::string& section, const std::string& name,
                          std::int64_t defaultValue = 0) const;
    static bool tryParseInt64(const std::string& text, std::int64_t& value);
    float getReal(const std::string& section, const std::string& name, float defaultValue = 0.0f) const;
    bool getBoolean(const std::string& section, const std::string& name, bool defaultValue = false) const;
    uint32_t getColor(const std::string& section, const std::string& name, uint32_t defaultValue = 0xFFFFFFFF) const;

    void set(const std::string& section, const std::string& name, const std::string& value);
    void setInteger(const std::string& section, const std::string& name, long value);
    void setInt64(const std::string& section, const std::string& name,
                  std::int64_t value);
    void setReal(const std::string& section, const std::string& name, float value);
    void setBoolean(const std::string& section, const std::string& name, bool value);
    void setColor(const std::string& section, const std::string& name, uint32_t value);

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& name) const;
    bool addSection(const std::string& section);

    std::vector<std::string> getSectionNames() const;
    std::vector<std::string> getKeyNames(const std::string& section) const;

    void removeSection(const std::string& section);
    void removeKey(const std::string& section, const std::string& name);

    const IniMap& getIniMap() const;
    IniMap& getIniMapRef();

private:
    static int valueHandler(void* user, const char* section, const char* name, const char* value);

    int error = 0;
    IniMap iniMap;
    std::vector<std::string> originalLines;
};
