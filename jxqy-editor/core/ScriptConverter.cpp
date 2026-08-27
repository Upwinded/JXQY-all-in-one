#include "ScriptConverter.h"
#include "LegacyTextDecoder.h"
#include "Util.h"

#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <map>
#include <set>

namespace
{
bool isIdentifierStart(char ch)
{
    unsigned char c = static_cast<unsigned char>(ch);
    return std::isalpha(c) || ch == '_';
}

bool isIdentifierChar(char ch)
{
    unsigned char c = static_cast<unsigned char>(ch);
    return std::isalnum(c) || ch == '_';
}

bool isBareIdentifier(const std::string& text)
{
    if (text.empty() || !isIdentifierStart(text[0]))
        return false;
    for (char ch : text)
    {
        if (!isIdentifierChar(ch))
            return false;
    }
    return true;
}

std::string toLowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trimCopy(const std::string& text)
{
    size_t start = 0;
    size_t end = text.size();

    while (start < end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n'))
        start++;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
        end--;

    return text.substr(start, end - start);
}

bool startsWithIgnoreCase(const std::string& text, const std::string& prefix)
{
    if (text.size() < prefix.size())
        return false;
    return toLowerAscii(text.substr(0, prefix.size())) == toLowerAscii(prefix);
}

bool replaceUtf8Token(const std::string& text, size_t pos, const char* token)
{
    size_t tokenLength = std::strlen(token);
    return pos + tokenLength <= text.size() && text.compare(pos, tokenLength, token) == 0;
}

std::string normalizeLegacyPunctuationOutsideStrings(const std::string& line)
{
    std::string result;
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < line.size(); i++)
    {
        char ch = line[i];
        if (escaped)
        {
            result += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            result += ch;
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            result += ch;
            inString = !inString;
            continue;
        }

        if (!inString)
        {
            if (replaceUtf8Token(line, i, "\xEF\xBC\x8C"))
            {
                result += ',';
                i += 2;
                continue;
            }
            if (replaceUtf8Token(line, i, "\xEF\xBC\x9B"))
            {
                result += ';';
                i += 2;
                continue;
            }
            if (replaceUtf8Token(line, i, "\xEF\xBC\x9A"))
            {
                result += ':';
                i += 2;
                continue;
            }
            if (replaceUtf8Token(line, i, "\xEF\xBC\x88"))
            {
                result += '(';
                i += 2;
                continue;
            }
            if (replaceUtf8Token(line, i, "\xEF\xBC\x89"))
            {
                result += ')';
                i += 2;
                continue;
            }
        }

        result += ch;
    }

    return result;
}

size_t findLineCommentStart(const std::string& line)
{
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i + 1 < line.size(); i++)
    {
        char ch = line[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\')
        {
            escaped = inString;
            continue;
        }
        if (ch == '"')
        {
            inString = !inString;
            continue;
        }
        if (!inString && ch == '/' && line[i + 1] == '/')
        {
            return i;
        }
    }
    return std::string::npos;
}

bool hasNonAsciiByte(const std::string& text)
{
    for (unsigned char ch : text)
    {
        if (ch >= 0x80)
            return true;
    }
    return false;
}

bool isQuotedStringLiteral(const std::string& text)
{
    return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

size_t findClosingQuote(const std::string& text, size_t openQuotePos)
{
    bool escaped = false;
    for (size_t i = openQuotePos + 1; i < text.size(); i++)
    {
        char ch = text[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\')
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
            return i;
    }
    return std::string::npos;
}

bool looksLikeAttachedArgument(const std::string& text)
{
    std::string trimmed = trimCopy(text);
    if (trimmed.empty())
        return false;

    unsigned char first = static_cast<unsigned char>(trimmed.front());
    return std::isdigit(first) || trimmed.front() == '-' || trimmed.front() == '$';
}

std::string escapeLuaStringContent(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    bool escaped = false;
    for (char ch : text)
    {
        if (escaped)
        {
            result += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\')
        {
            result += ch;
            escaped = true;
            continue;
        }
        if (ch == '"')
            result += '\\';
        result += ch;
    }
    return result;
}

std::string quoteLuaStringLiteral(const std::string& text)
{
    return "\"" + escapeLuaStringContent(text) + "\"";
}

std::string escapeInteriorQuotesInStringLiteral(const std::string& text)
{
    if (!isQuotedStringLiteral(text))
        return text;

    std::string result;
    result.reserve(text.size());
    result += '"';
    bool escaped = false;
    for (size_t i = 1; i + 1 < text.size(); i++)
    {
        char ch = text[i];
        if (escaped)
        {
            result += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\')
        {
            result += ch;
            escaped = true;
            continue;
        }
        if (ch == '"')
            result += '\\';
        result += ch;
    }
    result += '"';
    return result;
}

bool parseLuaLabel(const std::string& line, std::string& labelName, size_t& labelStart, size_t& labelEnd)
{
    labelName.clear();
    labelStart = 0;
    labelEnd = 0;

    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if (start + 4 > line.size() || line.compare(start, 2, "::") != 0)
    {
        return false;
    }

    size_t closePos = line.find("::", start + 2);
    if (closePos == std::string::npos || closePos == start + 2)
        return false;

    std::string trailing = trimCopy(line.substr(closePos + 2));
    if (!trailing.empty() && trailing.rfind("--", 0) != 0)
        return false;

    labelName = line.substr(start + 2, closePos - start - 2);
    labelStart = start;
    labelEnd = closePos + 2;
    return true;
}

std::vector<std::string> extractGotoTargets(const std::string& line)
{
    std::vector<std::string> targets;
    bool inString = false;
    bool escaped = false;
    size_t commentPos = findLineCommentStart(line);
    size_t end = commentPos == std::string::npos ? line.size() : commentPos;

    for (size_t i = 0; i + 4 <= end; i++)
    {
        char ch = line[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            inString = !inString;
            continue;
        }
        if (inString || line.compare(i, 4, "goto") != 0)
            continue;

        bool validBefore = i == 0 || !isIdentifierChar(line[i - 1]);
        bool validAfter = i + 4 >= end || !isIdentifierChar(line[i + 4]);
        if (!validBefore || !validAfter)
            continue;

        size_t targetStart = i + 4;
        while (targetStart < end && (line[targetStart] == ' ' || line[targetStart] == '\t'))
            targetStart++;
        if (targetStart >= end || !isIdentifierStart(line[targetStart]))
            continue;

        size_t targetEnd = targetStart + 1;
        while (targetEnd < end && isIdentifierChar(line[targetEnd]))
            targetEnd++;
        targets.push_back(line.substr(targetStart, targetEnd - targetStart));
        i = targetEnd;
    }

    return targets;
}

std::string stripTrailingSemicolon(std::string text)
{
    text = trimCopy(text);
    while (!text.empty() && text.back() == ';')
    {
        text.pop_back();
        text = trimCopy(text);
    }
    return text;
}

std::string repairMissingCloseParenAfterNumericUnderscore(const std::string& compact)
{
    if (compact.find('(') == std::string::npos ||
        compact.find(')') != std::string::npos ||
        compact.size() < 3)
    {
        return compact;
    }

    if (compact[compact.size() - 2] != '_' ||
        compact[compact.size() - 1] != ';' ||
        !std::isdigit(static_cast<unsigned char>(compact[compact.size() - 3])))
    {
        return compact;
    }

    return compact.substr(0, compact.size() - 2) + ");";
}

std::string stripBalancedParentheses(std::string text)
{
    text = trimCopy(text);
    if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
    {
        int depth = 0;
        bool balanced = true;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '(')
                depth++;
            else if (text[i] == ')')
                depth--;
            if (depth == 0 && i + 1 < text.size())
            {
                balanced = false;
                break;
            }
        }
        if (balanced)
            return trimCopy(text.substr(1, text.size() - 2));
    }
    return text;
}

std::string normalizeVariableName(std::string text)
{
    text = stripTrailingSemicolon(stripBalancedParentheses(trimCopy(text)));
    if (!text.empty() && text.front() == '$')
        text.erase(text.begin());
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    return text;
}

std::string normalizeLabelName(std::string text)
{
    text = stripTrailingSemicolon(stripBalancedParentheses(trimCopy(text)));
    if (startsWithIgnoreCase(text, "goto"))
    {
        text = trimCopy(text.substr(4));
        text = stripBalancedParentheses(text);
    }
    if (!text.empty() && text.front() == '@')
        text.erase(text.begin());
    while (!text.empty() && text.back() == ':')
        text.pop_back();
    text = trimCopy(text);

    // Replace characters illegal in Lua identifiers with underscore
    std::string sanitized;
    sanitized.reserve(text.size());
    for (char ch : text)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_')
            sanitized += ch;
        else
            sanitized += '_';
    }
    text = sanitized;

    // Lua labels cannot start with a digit; prefix with L_
    if (!text.empty() && text[0] >= '0' && text[0] <= '9')
        text = "L_" + text;

    // Lua reserved keywords cannot be used as label names
    static const std::set<std::string> luaKeywords = {
        "and", "break", "do", "else", "elseif", "end",
        "false", "for", "function", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while"
    };
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (luaKeywords.count(lower))
        text = "L_" + text;

    return text;
}

std::vector<std::string> splitArguments(const std::string& text)
{
    std::vector<std::string> result;
    std::string current;
    bool inString = false;
    bool escaped = false;
    int depth = 0;

    for (char ch : text)
    {
        if (escaped)
        {
            current += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            current += ch;
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            current += ch;
            inString = !inString;
            continue;
        }
        if (!inString)
        {
            if (ch == '(')
            {
                depth++;
            }
            else if (ch == ')' && depth > 0)
            {
                depth--;
            }
            else if (ch == ',' && depth == 0)
            {
                result.push_back(trimCopy(current));
                current.clear();
                continue;
            }
        }
        current += ch;
    }

    if (!current.empty() || !text.empty())
        result.push_back(trimCopy(current));
    return result;
}

bool extractCall(const std::string& line, std::string& name, std::string& args, std::string& suffix)
{
    std::string trimmed = trimCopy(line);
    if (trimmed.empty() || !isIdentifierStart(trimmed[0]))
        return false;

    size_t pos = 1;
    while (pos < trimmed.size() && isIdentifierChar(trimmed[pos]))
        pos++;

    if (pos >= trimmed.size() || trimmed[pos] != '(')
        return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t closePos = std::string::npos;
    for (size_t i = pos; i < trimmed.size(); i++)
    {
        char ch = trimmed[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            inString = !inString;
            continue;
        }
        if (!inString)
        {
            if (ch == '(')
                depth++;
            else if (ch == ')')
            {
                depth--;
                if (depth == 0)
                {
                    closePos = i;
                    break;
                }
            }
        }
    }

    if (closePos == std::string::npos)
        return false;

    name = trimmed.substr(0, pos);
    args = trimmed.substr(pos + 1, closePos - pos - 1);
    suffix = trimmed.substr(closePos + 1);
    return true;
}

bool isVariableArgument(const std::string& arg)
{
    std::string trimmed = stripTrailingSemicolon(stripBalancedParentheses(trimCopy(arg)));
    return !trimmed.empty() && trimmed.front() == '$';
}

std::string variableNameLiteral(const std::string& arg)
{
    return "\"" + normalizeVariableName(arg) + "\"";
}

bool outputVariableNameLiteral(const std::string& arg, std::string& literal)
{
    if (isVariableArgument(arg))
    {
        literal = variableNameLiteral(arg);
        return true;
    }

    std::string trimmed = stripTrailingSemicolon(stripBalancedParentheses(trimCopy(arg)));
    std::string name;
    std::string args;
    std::string suffix;
    if (!extractCall(trimmed, name, args, suffix) || !trimCopy(suffix).empty())
        return false;

    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name != "getvar")
        return false;

    std::vector<std::string> parsedArgs = splitArguments(args);
    if (parsedArgs.size() != 1)
        return false;

    literal = "\"" + escapeLuaStringContent(normalizeVariableName(parsedArgs[0])) + "\"";
    return true;
}

bool isLegacyWildcardArgument(const std::string& arg)
{
    std::string trimmed = stripTrailingSemicolon(stripBalancedParentheses(trimCopy(arg)));
    return trimmed == "*" || trimmed == "-*";
}

std::string normalizeLegacyStringFileExtension(const std::string& arg)
{
    std::string trimmed = trimCopy(arg);
    if (trimmed.empty() || trimmed.front() != '"')
        return arg;

    size_t closePos = findClosingQuote(trimmed, 0);
    if (closePos == std::string::npos || closePos + 1 >= trimmed.size())
        return arg;

    std::string rest = trimCopy(trimmed.substr(closePos + 1));
    if (startsWithIgnoreCase(rest, ".ini") && trimCopy(rest.substr(4)).empty())
        return trimmed.substr(0, closePos) + rest + "\"";

    return arg;
}

std::string normalizeValueArgument(const std::string& arg)
{
    std::string trimmed = trimCopy(arg);
    if (isVariableArgument(arg))
        return "getvar(" + variableNameLiteral(arg) + ")";
    if (trimmed.empty())
        return "-1";
    if (isLegacyWildcardArgument(arg))
        return "-1";
    trimmed = normalizeLegacyStringFileExtension(trimmed);
    if (hasNonAsciiByte(trimmed) && !isQuotedStringLiteral(trimmed))
        return quoteLuaStringLiteral(trimmed);
    return escapeInteriorQuotesInStringLiteral(trimmed);
}

std::string joinArguments(const std::vector<std::string>& args)
{
    std::string result;
    for (size_t i = 0; i < args.size(); i++)
    {
        if (i > 0)
            result += ",";
        result += args[i];
    }
    return result;
}

void normalizeLegacyOutputArguments(const std::string& lowerName, std::vector<std::string>& args)
{
    std::string outputLiteral;
    if ((lowerName == "getrandnum" ||
         lowerName == "getpartneridx" ||
         lowerName == "getmoneynum" ||
         lowerName == "checkyear" ||
         lowerName == "getplayerlevel") &&
        args.size() >= 1 && outputVariableNameLiteral(args[0], outputLiteral))
    {
        args[0] = outputLiteral;
    }
    else if (lowerName == "choose" && args.size() >= 4 && outputVariableNameLiteral(args[3], outputLiteral))
    {
        args[3] = outputLiteral;
    }
    else if ((lowerName == "chooseex" || lowerName == "chooseplus") && args.size() >= 3 && outputVariableNameLiteral(args.back(), outputLiteral))
    {
        args.back() = outputLiteral;
    }
    else if (lowerName == "select" && args.size() >= 4)
    {
        if (outputVariableNameLiteral(args[0], outputLiteral))
        {
            args = {args[1], args[2], args[3], outputLiteral};
        }
        else if (outputVariableNameLiteral(args[3], outputLiteral))
        {
            args[3] = outputLiteral;
        }
    }
    else if (lowerName == "gamble" && args.size() >= 3 && outputVariableNameLiteral(args[2], outputLiteral))
    {
        args[2] = outputLiteral;
    }
    else if (lowerName == "getplayermagiclevel" && args.size() >= 2 && outputVariableNameLiteral(args[1], outputLiteral))
    {
        args[1] = outputLiteral;
    }
    else if (lowerName == "getleechcraftdifference" && args.size() >= 2 && outputVariableNameLiteral(args[1], outputLiteral))
    {
        args[1] = outputLiteral;
    }
    else if (lowerName == "getnpcstate" && args.size() >= 3 && outputVariableNameLiteral(args[2], outputLiteral))
    {
        args[2] = outputLiteral;
    }

    for (std::string& arg : args)
    {
        arg = normalizeValueArgument(arg);
    }
}

void repairLegacyArgumentList(std::vector<std::string>& args)
{
    std::vector<std::string> repaired;
    for (const std::string& arg : args)
    {
        std::string trimmed = trimCopy(arg);
        if (trimmed.empty())
        {
            repaired.push_back(trimmed);
            continue;
        }

        if (trimmed.front() == '"')
        {
            size_t closePos = findClosingQuote(trimmed, 0);
            if (closePos != std::string::npos && closePos + 1 < trimmed.size())
            {
                std::string rest = trimCopy(trimmed.substr(closePos + 1));
                if (looksLikeAttachedArgument(rest))
                {
                    repaired.push_back(trimmed.substr(0, closePos + 1));
                    repaired.push_back(rest);
                    continue;
                }
            }
        }

        repaired.push_back(trimmed);
    }

    while (!repaired.empty() && trimCopy(repaired.back()).empty())
        repaired.pop_back();

    args.swap(repaired);
}

const char* scriptReturnLabelName();

void normalizeConvertedLabelsAndGotos(std::vector<std::string>& lines, bool usesReturnLabel)
{
    std::map<std::string, int> labelCounts;
    std::set<std::string> labelNames;

    for (std::string& line : lines)
    {
        std::string labelName;
        size_t labelStart = 0;
        size_t labelEnd = 0;
        if (!parseLuaLabel(line, labelName, labelStart, labelEnd))
            continue;

        int labelCount = ++labelCounts[labelName];
        if (labelCount == 1)
        {
            labelNames.insert(labelName);
            continue;
        }

        std::string duplicateLabelName;
        int suffix = labelCount;
        do
        {
            duplicateLabelName = labelName + "__dup" + std::to_string(suffix++);
        }
        while (labelNames.count(duplicateLabelName) > 0);

        line.replace(labelStart, labelEnd - labelStart, "::" + duplicateLabelName + "::");
        labelNames.insert(duplicateLabelName);
        labelCounts[duplicateLabelName] = 1;
    }

    std::set<std::string> missingLabels;
    for (const std::string& line : lines)
    {
        for (const std::string& target : extractGotoTargets(line))
        {
            if (usesReturnLabel && target == scriptReturnLabelName())
                continue;
            if (labelNames.count(target) == 0)
                missingLabels.insert(target);
        }
    }

    for (const std::string& labelName : missingLabels)
    {
        lines.push_back("::" + labelName + "::");
        labelNames.insert(labelName);
    }
}

std::string addSemicolonIfMissing(std::string line)
{
    std::string trimmed = trimCopy(line);
    if (trimmed.empty())
        return line;
    if (trimmed.back() != ';' && trimmed.rfind("::", 0) != 0 && !startsWithIgnoreCase(trimmed, "if "))
        return line + ";";
    return line;
}

const char* scriptReturnLabelName()
{
    return "__jx_script_return";
}

std::string scriptReturnGotoStatement()
{
    return std::string("goto ") + scriptReturnLabelName();
}

std::string makeUnhandledComment(const std::string& message, const std::string& line)
{
    return "-- TODO(jx-script-converter): " + message + ": " + line;
}

const std::set<std::string>& knownRuntimeScriptApis()
{
    static const std::set<std::string> names = {
        "add", "addattack", "adddefend", "addevade", "addexp", "addflyinis",
        "addgoods", "addkindvalue", "addlife", "addlifemax", "addmagic",
        "addmagicexp", "addmana", "addmanamax", "addmoney", "addmovespeedpercent",
        "addnpc", "addnpcmagic", "addnpcproperty", "addobj", "addonemagic",
        "addrandgoods", "addrandmoney", "addtalent", "addthew", "addthewmax",
        "addtomemo", "assign", "beginrain", "buygoods", "buygoodsonly",
        "changeasfcolor", "changeflyini", "changeflyini2", "changelife", "changemana",
        "changemapcolor", "changethew", "checkfreegoodsspace", "checkfreemagicspace",
        "checkyear", "choose", "chooseex", "choosemultiple", "chooseplus",
        "clearallsave", "clearallvar", "clearbody", "cleareffect", "cleargoods",
        "clearmagic", "clearmemo", "closebox", "closetimelimit", "closewatereffect",
        "delcurobj", "delgoodbyname", "delgoods", "delmagic", "delmemo", "delnpc",
        "delobj", "disabledrop", "disablefight", "disableinput", "disablejump",
        "disablemapscroll", "disablenpcai", "disablepartnercombat", "disablerun",
        "disablesave", "displaymessage", "drawbackground", "enabledrop", "enablefight",
        "enableinput", "enablejump", "enablemapscroll", "enablenpcai",
        "enablepartnercombat", "enablerun", "enablesave", "endrain", "equipgoods",
        "fadein", "fadeout", "follownpc", "followplayer", "freemap",
        "frozenmillisecond", "fulllife", "fullmana", "fullthew", "gamble",
        "getcurrentmappath", "geteffectstate", "getexp", "getgoodscountbyfile",
        "getgoodscountbyname", "getgoodsnum", "getgoodsnumbyname", "getgoodsstate",
        "getleechcraftdifference", "getmagiclevel", "getmagicstate", "getmapstate",
        "getmoney", "getmoneynum", "getnpccount", "getnpcpos", "getnpcstate",
        "getobjpos", "getobjstate", "getpartneridx", "getpartnerindex", "getplayerexp",
        "getplayerlevel", "getplayermagiclevel", "getplayerstat", "getplayerstate",
        "getrandnum", "getvar", "hasgoodsfreespace", "hasmagicfreespace",
        "hidebottomwnd", "hideinterface", "hidemousecursor", "hidetimerwnd",
        "interactnearestnpc", "interactnearestobj", "isequipweapon", "limitmana",
        "loadgame", "loadgoods", "loadgoodssnapshot", "loadmap", "loadnpc", "loadobj",
        "loadonenpc", "loadplayer", "loadplayersnapshot", "memo", "mergenpc",
        "movemagic", "movescreen", "movescreenex", "npcattack", "npcgoto",
        "npcgotodir", "npcgotoex", "npcspecialaction", "npcspecialactionex",
        "npcusemagic", "openbox", "openobj", "opentimelimit", "openwatereffect",
        "petrifymillisecond", "playeraddemotion", "playeraddjustice", "playerchange",
        "playergoto", "playergotodir", "playergotoex", "playerjumpto", "playerrunto",
        "playerruntoex", "playmovie", "playmusic", "playrandommusic", "playsound",
        "poisonmillisecond", "printf", "randrun", "returntotitle",
        "runobjrightscript", "runobjscript", "runparallelscript", "runscript",
        "savegame", "savegoods", "savegoodssnapshot", "savemaptrap", "savenpc",
        "saveobj", "saveplayer", "saveplayersnapshot", "say", "select", "sellgoods",
        "setallnpcdeathscript", "setallnpcisenemy", "setallnpcscript",
        "setdropenabled", "setdropini", "setfadelum", "setfightenabled",
        "setinputenabled", "setinterfacevisible", "setjumpenabled", "setkeepattack",
        "setlevelfile", "setmagiclevel", "setmainlum", "setmapnpcattr", "setmappos",
        "setmaptime", "setmaptrap", "setmoneynum", "setnpcaction",
        "setnpcactionfile", "setnpcactiontype", "setnpcaienabled", "setnpcclickscript",
        "setnpcdeathscript", "setnpcdestination", "setnpcdir", "setnpckind",
        "setnpclevel", "setnpcmagicfile", "setnpcmagiclevel",
        "setnpcmagictousewhenbeattacked", "setnpcpartner", "setnpcpos",
        "setnpcrelation", "setnpcres", "setnpcscript", "setnpcstate",
        "setnpctalkcontent", "setobjkind", "setobjofs", "setobjpos", "setobjscript",
        "setpartnerlevel", "setplayerdir", "setplayerlevel", "setplayerlum",
        "setplayermagictousewhenbeattacked", "setplayerpos", "setplayerscn",
        "setplayerstate", "setrunenabled", "setsaveenabled", "setshowmappos",
        "setsignaltiphidden", "settimescript", "settrap", "setwalkisrun",
        "showbottomwnd", "showdicegame", "showfishgame", "showgamble",
        "showgivegoodswin", "showinterface", "showmessage", "showmousecursor", "shownpc",
        "showrain", "showrandomsnow", "showsignaltip", "showsnow", "showstealwin",
        "showsystemmsg", "showtalk", "sleep", "stopmovie", "stopmusic", "stopsound",
        "sub", "talk", "talkselftip", "tononfightingstate", "updatestate",
        "usemagic", "watch"
    };
    return names;
}

const std::set<std::string>& acceptedRuntimeScriptApis()
{
    static std::set<std::string> names = []() {
        std::set<std::string> result = knownRuntimeScriptApis();
        const std::set<std::string> aliases = {
            "addmemo", "addmemobyid", "addnpcflyinis", "addonemogic", "addrandomgoods",
            "assing", "cameramove", "cameramoveto", "centercamera", "changenpcflyini",
            "changenpcflyini2", "changenpclife", "changenpcmana", "changenpcthew",
            "changespritecolor", "clearallsaves", "clearallvars", "closetimer",
            "deletecurrentobj", "deletegoodsbyname", "deletemagic", "deletememo",
            "deletememobyid", "deletenpc", "deleteobj", "enabeldrop", "frozen",
            "getgoodsmun", "hidebottomwindow", "hidetimer", "interactnearestnpc",
            "interactnearestobject", "loadmapnpc", "lodaobj", "message", "messagebox",
            "npcaction", "npcfollow", "npcfollowplayer", "npcspecialactionnonblocking",
            "npcwalkto", "npcwalktodir", "npcwalktononblocking", "npcwatch", "opentimer",
            "petrify", "playerruntononblocking", "playerwalkto", "playerwalktodir",
            "playerwalktononblocking", "playgoto", "poison", "removegoods", "runscirpt",
            "savetrap", "setcamerapos", "setmoney", "setnpckeepattack",
            "setnpcmagictousewhenbeatacked", "setnpcmagicwhenattacked", "setnpcresource",
            "setobjoffset", "setplayermagictousewhenbeatacked",
            "setplayermagicwhenattacked", "setplayrdir", "settimerscript", "setvar",
            "showbottomwindow", "showsystemmessage"
        };
        result.insert(aliases.begin(), aliases.end());
        return result;
    }();
    return names;
}

struct StatementContinuationState
{
    bool inString = false;
    bool escaped = false;
};

StatementContinuationState scanStatementContinuation(const std::string& text)
{
    StatementContinuationState state;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (state.escaped)
        {
            state.escaped = false;
            continue;
        }
        if (state.inString && ch == '\\')
        {
            state.escaped = true;
            continue;
        }
        if (ch == '"')
        {
            state.inString = !state.inString;
            continue;
        }
        if (state.inString)
            continue;
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/')
            break;
    }
    return state;
}

bool isSignedIntegerText(const std::string& text)
{
    if (text.empty())
        return false;
    size_t index = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    if (index == text.size())
        return false;
    for (; index < text.size(); ++index)
    {
        if (!std::isdigit(static_cast<unsigned char>(text[index])))
            return false;
    }
    return true;
}

std::string repairProductionFunctionCallTypos(std::string compact)
{
    while (compact.size() >= 2 && compact.compare(compact.size() - 2, 2, ";;") == 0)
        compact.pop_back();

    if (compact.size() >= 4 && compact.compare(compact.size() - 2, 2, ");") == 0)
    {
        size_t quoteCount = 0;
        bool escaped = false;
        for (char ch : compact)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (ch == '\\')
            {
                escaped = true;
                continue;
            }
            if (ch == '"')
                quoteCount++;
        }

        const size_t duplicateQuote = compact.rfind("\"\"");
        if (quoteCount % 2 == 1 && duplicateQuote != std::string::npos &&
            duplicateQuote + 2 == compact.size() - 2)
        {
            compact.erase(duplicateQuote, 1);
            quoteCount--;
        }

        const size_t commaQuote = compact.rfind(",\"");
        if (quoteCount % 2 == 1 && commaQuote != std::string::npos)
        {
            const std::string numericText = compact.substr(
                commaQuote + 2, compact.size() - (commaQuote + 2) - 2);
            if (isSignedIntegerText(numericText))
                compact.replace(commaQuote, compact.size() - commaQuote,
                    "," + numericText + ");");
        }
    }

    const size_t firstOpen = compact.find('(');
    if (firstOpen != std::string::npos && firstOpen + 1 < compact.size() &&
        compact[firstOpen + 1] == '(')
    {
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (char ch : compact)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (inString && ch == '\\')
            {
                escaped = true;
                continue;
            }
            if (ch == '"')
            {
                inString = !inString;
                continue;
            }
            if (!inString)
            {
                if (ch == '(')
                    depth++;
                else if (ch == ')')
                    depth--;
            }
        }
        if (depth == 1)
            compact.erase(firstOpen, 1);
    }
    return compact;
}

size_t findOperatorOutsideStrings(const std::string& text, const std::string& op)
{
    bool inString = false;
    bool escaped = false;
    for (size_t i = 0; i + op.size() <= text.size(); i++)
    {
        char ch = text[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            inString = !inString;
            continue;
        }
        if (!inString && text.compare(i, op.size(), op) == 0)
            return i;
    }
    return std::string::npos;
}
}

ScriptConverter::ScriptConverter()
{
}

ScriptConverter::~ScriptConverter()
{
}

const std::set<std::string>& ScriptConverter::runtimeApiNames()
{
    return knownRuntimeScriptApis();
}

std::string ScriptConverter::getLastMessage() const
{
    return lastMessage;
}

const std::vector<ScriptConversionDiagnostic>& ScriptConverter::getDiagnostics() const
{
    return diagnostics;
}

void ScriptConverter::recordDiagnostic(const std::string& category, const std::string& message, const std::string& line)
{
    ScriptConversionDiagnostic diagnostic;
    diagnostic.lineNumber = currentLineNumber;
    diagnostic.category = category;
    diagnostic.message = message;
    diagnostic.line = line;
    diagnostics.push_back(diagnostic);
}

bool ScriptConverter::isScriptFile(const std::string& fileName)
{
    size_t dotPos = fileName.rfind('.');
    if (dotPos == std::string::npos)
        return false;

    std::string extension = fileName.substr(dotPos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".txt";
}

bool ScriptConverter::isIniFile(const std::string& content)
{
    std::istringstream stream(content);
    std::string line;
    int iniSectionCount = 0;
    int totalNonEmpty = 0;
    while (std::getline(stream, line))
    {
        std::string trimmed = trimCopy(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            continue;
        totalNonEmpty++;
        if (trimmed.size() >= 3 && trimmed[0] == '[' && trimmed.back() == ']')
        {
            std::string sectionName = trimmed.substr(1, trimmed.size() - 2);
            bool valid = true;
            for (char ch : sectionName)
            {
                if (ch == ' ' || ch == '\t' || ch == '[' || ch == ']' || ch == '\n')
                {
                    valid = false;
                    break;
                }
            }
            if (valid && !sectionName.empty())
                iniSectionCount++;
        }
    }
    return iniSectionCount > 0 && totalNonEmpty > 0;
}

std::vector<std::string> ScriptConverter::splitLines(const std::string& content)
{
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }

    return lines;
}

std::string ScriptConverter::trimLine(const std::string& line)
{
    size_t start = 0;
    size_t end = line.size();

    while (start < end && (line[start] == ' ' || line[start] == '\t' || line[start] == '\r' || line[start] == '\n'))
        start++;

    while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r' || line[end - 1] == '\n'))
        end--;

    return line.substr(start, end - start);
}

std::string ScriptConverter::removeSpaces(const std::string& line)
{
    std::string result;
    bool inString = false;
    bool escaped = false;
    for (char ch : line)
    {
        if (escaped)
        {
            result += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            result += ch;
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            result += ch;
            inString = !inString;
            continue;
        }
        if (!inString && (ch == ' ' || ch == '\t'))
            continue;
        result += ch;
    }
    return result;
}

std::vector<std::string> ScriptConverter::extractTokens(const std::string& line, const std::string& delimiters)
{
    std::vector<std::string> tokens;
    std::string current;

    for (char ch : line)
    {
        if (delimiters.find(ch) != std::string::npos)
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += ch;
        }
    }

    if (!current.empty())
        tokens.push_back(current);

    return tokens;
}

std::string ScriptConverter::findOperator(const std::string& line, int& position)
{
    position = -1;

    size_t pos = line.find("<>");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return "<>";
    }

    pos = line.find("==");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return "==";
    }

    pos = line.find("!=");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return "!=";
    }

    pos = line.find("<=");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return "<=";
    }

    pos = line.find(">=");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return ">=";
    }

    pos = line.find(">");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return ">";
    }

    pos = line.find("<");
    if (pos != std::string::npos)
    {
        position = static_cast<int>(pos);
        return "<";
    }

    return "";
}

std::string ScriptConverter::convertOperatorToLua(const std::string& op)
{
    if (op == "<>" || op == "!=")
        return "~=";
    if (op == ">>")
        return ">";
    if (op == "<<")
        return "<";
    if (op == "==")
        return "==";
    if (op == "<=")
        return "<=";
    if (op == ">=")
        return ">=";
    if (op == ">")
        return ">";
    if (op == "<")
        return "<";
    return op;
}

std::string ScriptConverter::convertAssignStatement(const std::string& line)
{
    std::string name;
    std::string args;
    std::string suffix;
    if (extractCall(line, name, args, suffix))
    {
        std::vector<std::string> parsedArgs = splitArguments(args);
        if (parsedArgs.size() >= 2)
        {
            std::string varName = normalizeVariableName(parsedArgs[0]);
            std::string value = normalizeValueArgument(stripTrailingSemicolon(parsedArgs[1]));
            return "assign(\"" + varName + "\"," + value + ");";
        }
    }
    recordDiagnostic("UnhandledStatement", "cannot convert Assign statement", line);
    return makeUnhandledComment("unhandled Assign statement", line);
}

std::string ScriptConverter::convertAddStatement(const std::string& line)
{
    std::string name;
    std::string args;
    std::string suffix;
    if (extractCall(line, name, args, suffix))
    {
        std::vector<std::string> parsedArgs = splitArguments(args);
        if (parsedArgs.size() >= 2)
        {
            std::string varName = normalizeVariableName(parsedArgs[0]);
            std::string value = normalizeValueArgument(stripTrailingSemicolon(parsedArgs[1]));
            return "add(\"" + varName + "\"," + value + ");";
        }
    }
    recordDiagnostic("UnhandledStatement", "cannot convert Add statement", line);
    return makeUnhandledComment("unhandled Add statement", line);
}

std::string ScriptConverter::convertVariableAssignment(const std::string& line)
{
    std::string compact = removeSpaces(line);
    size_t eqPos = std::string::npos;
    bool inStr = false;
    bool esc = false;
    for (size_t i = 0; i < compact.size(); i++)
    {
        char ch = compact[i];
        if (esc) { esc = false; continue; }
        if (ch == '\\' && inStr) { esc = true; continue; }
        if (ch == '"') { inStr = !inStr; continue; }
        if (!inStr && ch == '=')
        {
            if (i + 1 < compact.size() && compact[i + 1] == '=')
            {
                i++;
                continue;
            }
            eqPos = i;
            break;
        }
    }
    if (!compact.empty() && compact[0] == '$' && eqPos != std::string::npos)
    {
        std::string varName = normalizeVariableName(compact.substr(0, eqPos));
        std::string value = normalizeValueArgument(stripTrailingSemicolon(compact.substr(eqPos + 1)));
        return "assign(\"" + varName + "\"," + value + ");";
    }
    recordDiagnostic("UnhandledStatement", "cannot convert variable assignment", line);
    return makeUnhandledComment("unhandled variable assignment", line);
}

std::string ScriptConverter::convertIfStatement(const std::string& line)
{
    std::string compact = removeSpaces(line);
    std::string lower = toLowerAscii(compact);
    if (!startsWithIgnoreCase(lower, "if("))
        return line;

    size_t openPos = compact.find('(');
    size_t closePos = std::string::npos;
    if (openPos != std::string::npos)
    {
        int depth = 0;
        bool inStr = false;
        bool esc = false;
        for (size_t i = openPos; i < compact.size(); i++)
        {
            char ch = compact[i];
            if (esc) { esc = false; continue; }
            if (ch == '\\' && inStr) { esc = true; continue; }
            if (ch == '"') { inStr = !inStr; continue; }
            if (!inStr)
            {
                if (ch == '(') depth++;
                else if (ch == ')')
                {
                    depth--;
                    if (depth == 0) { closePos = i; break; }
                }
            }
        }
    }
    if (openPos == std::string::npos || closePos == std::string::npos || closePos <= openPos + 1)
    {
        recordDiagnostic("UnhandledIf", "cannot parse if condition", line);
        return makeUnhandledComment("unhandled if statement", line);
    }

    std::string condition = compact.substr(openPos + 1, closePos - openPos - 1);
    std::string luaCondition = convertLegacyConditionToLua(condition);
    if (luaCondition.empty())
    {
        recordDiagnostic("UnhandledIf", "cannot convert if condition", line);
        return makeUnhandledComment("unhandled if condition", line);
    }

    std::string action = stripTrailingSemicolon(compact.substr(closePos + 1));
    if (action.empty())
        return "if " + luaCondition + " then";

    if (startsWithIgnoreCase(action, "goto") || (!action.empty() && action[0] == '@'))
    {
        std::string labelName = normalizeLabelName(action);
        if (labelName.empty())
        {
            recordDiagnostic("UnhandledIf", "cannot convert if goto target", line);
            return makeUnhandledComment("unhandled if goto target", line);
        }
        return "if " + luaCondition + " then goto " + labelName + " end";
    }
    if (isBareIdentifier(action))
    {
        std::string labelName = normalizeLabelName(action);
        if (!labelName.empty())
            return "if " + luaCondition + " then goto " + labelName + " end";
    }

    std::string convertedAction = convertCommand(action);
    if (convertedAction.empty())
    {
        recordDiagnostic("UnhandledIf", "cannot convert if action", line);
        return "if " + luaCondition + " then";
    }
    if (convertedAction.rfind("--", 0) == 0)
        return "if " + luaCondition + " then\n  " + convertedAction + "\nend";
    return "if " + luaCondition + " then " + convertedAction + " end";
}

std::string ScriptConverter::convertLabelStatement(const std::string& line)
{
    std::string compact = removeSpaces(line);
    if (compact.empty() || compact[0] != '@')
        return line;

    std::string labelName = normalizeLabelName(compact);
    if (labelName.empty())
    {
        recordDiagnostic("UnhandledStatement", "cannot convert label statement", line);
        return makeUnhandledComment("unhandled label statement", line);
    }
    return "::" + labelName + "::";
}

std::string ScriptConverter::convertLegacyConditionToLua(const std::string& condition)
{
    static const std::vector<std::string> operators = {
        "<>", "==", "!=", ">>", ">=", "<<", "<=", ">", "<"
    };

    std::string compact = removeSpaces(condition);
    for (const auto& op : operators)
    {
        size_t opPos = findOperatorOutsideStrings(compact, op);
        if (opPos == std::string::npos)
            continue;

        std::string varName = normalizeVariableName(compact.substr(0, opPos));
        std::string value = stripTrailingSemicolon(compact.substr(opPos + op.size()));
        if (varName.empty() || value.empty())
            return "";

        std::string rhs;
        if (isVariableArgument(value))
            rhs = "getvar(" + variableNameLiteral(value) + ")";
        else
            rhs = value;

        return "getvar(\"" + varName + "\") " + convertOperatorToLua(op) + " " + rhs;
    }

    const size_t commaPos = findOperatorOutsideStrings(compact, ",");
    if (commaPos != std::string::npos &&
        !compact.empty() && compact.front() == '$' &&
        findOperatorOutsideStrings(compact.substr(commaPos + 1), ",") == std::string::npos)
    {
        const std::string varName = normalizeVariableName(compact.substr(0, commaPos));
        const std::string value = stripTrailingSemicolon(compact.substr(commaPos + 1));
        if (!varName.empty() && isSignedIntegerText(value))
            return "getvar(\"" + varName + "\") == " + value;
    }

    return "";
}

std::string ScriptConverter::convertGotoStatement(const std::string& line)
{
    std::string compact = removeSpaces(line);
    if (startsWithIgnoreCase(compact, "goto"))
    {
        std::string labelName = normalizeLabelName(compact);
        if (!labelName.empty())
            return "goto " + labelName;
    }

    recordDiagnostic("UnhandledStatement", "cannot convert goto statement", line);
    return makeUnhandledComment("unhandled goto statement", line);
}

std::string ScriptConverter::convertReturnStatement(const std::string& line)
{
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "return" || lower == "return;" ||
        lower == "return()" || lower == "return();" ||
        lower == "reurn" || lower == "reurn;")
        return scriptReturnGotoStatement();

    size_t pos = lower.find("return;");
    if (pos != std::string::npos)
        return scriptReturnGotoStatement();

    return line;
}

std::string ScriptConverter::convertComment(const std::string& line)
{
    size_t pos = findLineCommentStart(line);
    if (pos != std::string::npos)
    {
        return line.substr(0, pos) + "--" + line.substr(pos + 2);
    }
    return line;
}

std::string ScriptConverter::convertCommand(const std::string& line)
{
    std::string original = trimLine(line);
    if (original.empty())
        return "";

    size_t commentPos = findLineCommentStart(original);
    std::string comment;
    std::string code = original;
    if (commentPos != std::string::npos)
    {
        comment = "--" + original.substr(commentPos + 2);
        code = trimLine(original.substr(0, commentPos));
    }

    if (code.empty())
        return comment;

    code = normalizeLegacyPunctuationOutsideStrings(code);

    std::string compact = removeSpaces(code);
    compact = repairMissingCloseParenAfterNumericUnderscore(compact);
    compact = repairProductionFunctionCallTypos(compact);
    std::string lower = toLowerAscii(compact);
    std::string converted;

    if (lower == "return" || lower == "return;" ||
        lower == "return()" || lower == "return();" ||
        lower == "reurn" || lower == "reurn;" ||
        lower == "retuen" || lower == "retuen;")
        converted = convertReturnStatement("return;");
    else if (!compact.empty() && compact[0] == '@')
        converted = convertLabelStatement(compact);
    else if (lower.rfind("assign(", 0) == 0 || lower.rfind("assing(", 0) == 0)
        converted = convertAssignStatement(compact);
    else if (startsWithIgnoreCase(compact, "add") && compact.size() > 3 && compact[3] == '(')
        converted = convertAddStatement(compact);
    else if (!compact.empty() && compact[0] == '$')
        converted = convertVariableAssignment(compact);
    else if (startsWithIgnoreCase(compact, "if") && compact.size() > 2 && compact[2] == '(')
        converted = convertIfStatement(compact);
    else if (startsWithIgnoreCase(compact, "goto"))
        converted = convertGotoStatement(compact);
    else
        converted = normalizeFunctionCall(compact);

    if (!comment.empty())
    {
        if (converted.empty())
            return comment;
        return converted + " " + comment;
    }
    return converted;
}

std::string ScriptConverter::normalizeFunctionCall(const std::string& line)
{
    std::string name;
    std::string args;
    std::string suffix;
    if (!extractCall(line, name, args, suffix))
    {
        std::string bareName = stripTrailingSemicolon(line);
        std::string lowerBareName = toLowerAscii(bareName);
        if (isBareIdentifier(bareName) &&
            acceptedRuntimeScriptApis().find(lowerBareName) != acceptedRuntimeScriptApis().end())
        {
            return lowerBareName + "();";
        }
        recordDiagnostic("UnhandledStatement", "cannot parse statement as a function call", line);
        return makeUnhandledComment("unhandled statement", line);
    }

    std::string lowerName = toLowerAscii(name);
    static const std::map<std::string, std::string> aliasMap = {
        {"getgoodsmun", "getgoodsnum"},
        {"hidebottomwindow", "hidebottomwnd"},
        {"lodaobj", "loadobj"},
        {"memo", "addtomemo"},
        {"messagebox", "displaymessage"},
        {"npcaction", "setnpcaction"},
        {"playerruntoex", "playerrunto"},
        {"playgoto", "playergoto"},
        {"showbottomwindow", "showbottomwnd"},
        {"runscirpt", "runscript"},
        {"setplayrdir", "setplayerdir"}
    };

    auto aliasIter = aliasMap.find(lowerName);
    if (aliasIter != aliasMap.end())
        lowerName = aliasIter->second;

    if (acceptedRuntimeScriptApis().find(lowerName) == acceptedRuntimeScriptApis().end())
    {
        recordDiagnostic("UnsupportedApi", "no matching runtime script API for call " + lowerName, line);
    }

    std::vector<std::string> parsedArgs = splitArguments(args);
    if (lowerName == "runscript")
    {
        if (parsedArgs.size() >= 2)
        {
            std::string firstArg = stripTrailingSemicolon(parsedArgs[0]);
            if (!firstArg.empty() && std::isdigit(static_cast<unsigned char>(firstArg[0])))
            {
                parsedArgs = {parsedArgs[1]};
            }
        }
    }

    repairLegacyArgumentList(parsedArgs);
    normalizeLegacyOutputArguments(lowerName, parsedArgs);
    args = joinArguments(parsedArgs);

    std::string normalized = lowerName + "(" + args + ")";
    suffix = stripTrailingSemicolon(suffix);
    if (suffix == ":")
    {
        suffix.clear();
    }
    if (!suffix.empty())
    {
        recordDiagnostic("UnhandledStatement", "function call has unsupported trailing text", line);
        return makeUnhandledComment("unhandled function call suffix", line);
    }
    return addSemicolonIfMissing(normalized);
}

std::string ScriptConverter::wrapInFunction(const std::string& body, const std::string& functionName)
{
    return "function " + functionName + "()\n" + body + "\nend\n";
}

std::string ScriptConverter::convertScript(const std::string& scriptContent)
{
    lastMessage.clear();
    diagnostics.clear();
    currentLineNumber = 0;

    if (isIniFile(scriptContent))
    {
        lastMessage = "INI file, skip conversion";
        return scriptContent;
    }

    std::vector<std::string> str = splitLines(scriptContent);
    std::vector<std::string> convertedLines;
    bool usesReturnLabel = false;
    for (size_t i = 0; i < str.size(); i++)
    {
        const int sourceLineNumber = static_cast<int>(i + 1);
        currentLineNumber = sourceLineNumber;
        std::string line = str[i];
        StatementContinuationState continuation = scanStatementContinuation(line);
        const std::string trimmedFirstLine = trimCopy(line);
        const bool joinMultilineString = continuation.inString &&
            !(trimmedFirstLine.size() >= 2 &&
              trimmedFirstLine.compare(trimmedFirstLine.size() - 2, 2, ");") == 0);
        size_t joinedLines = 0;
        while (joinMultilineString && continuation.inString &&
               i + 1 < str.size() && joinedLines < 100)
        {
            ++i;
            ++joinedLines;
            line += "\\n";
            line += trimCopy(str[i]);
            continuation = scanStatementContinuation(line);
        }
        std::string converted = convertCommand(line);
        if (converted.find(scriptReturnGotoStatement()) != std::string::npos)
            usesReturnLabel = true;
        if (converted.empty())
        {
            convertedLines.push_back("");
        }
        else
        {
            std::vector<std::string> splitConvertedLines = splitLines(converted);
            if (splitConvertedLines.empty())
                convertedLines.push_back(converted);
            else
                convertedLines.insert(convertedLines.end(), splitConvertedLines.begin(), splitConvertedLines.end());
        }
    }

    normalizeConvertedLabelsAndGotos(convertedLines, usesReturnLabel);

    if (usesReturnLabel)
    {
        convertedLines.push_back("::" + std::string(scriptReturnLabelName()) + "::");
        convertedLines.push_back("return;");
    }

    std::string result;
    for (const std::string& line : convertedLines)
    {
        if (line.empty())
            result += "\n";
        else
            result += "  " + line + "\n";
    }

    currentLineNumber = 0;
    return result;
}

bool ScriptConverter::convertFile(const std::string& fileName)
{
    std::vector<uint8_t> fileData = Util::readFileToBuffer(fileName);
    if (fileData.empty())
    {
        lastMessage = "Cannot open file: " + fileName;
        return false;
    }

    std::string content(reinterpret_cast<const char*>(fileData.data()), fileData.size());

    if (isIniFile(content))
    {
        lastMessage = "INI file, skip conversion: " + fileName;
        return true;
    }

    std::string converted = convertScript(content);

    if (!Util::writeFileFromBuffer(fileName, converted.data(), converted.size()))
    {
        lastMessage = "Cannot write file: " + fileName;
        return false;
    }

    lastMessage = "Script converted successfully: " + fileName;
    return true;
}

bool ScriptConverter::convertFile(const std::string& inputFileName, const std::string& outputFileName)
{
    std::vector<uint8_t> fileData = Util::readFileToBuffer(inputFileName);
    if (fileData.empty())
    {
        lastMessage = "Cannot open file: " + inputFileName;
        return false;
    }

    std::string content(reinterpret_cast<const char*>(fileData.data()), fileData.size());

    if (isIniFile(content))
    {
        lastMessage = "INI file, skip conversion: " + inputFileName;
        return true;
    }

    std::string converted = convertScript(content);

    if (!Util::writeFileFromBuffer(outputFileName, converted.data(), converted.size()))
    {
        lastMessage = "Cannot write file: " + outputFileName;
        return false;
    }

    lastMessage = "Script converted successfully: " + inputFileName + " -> " + outputFileName;
    return true;
}

bool ScriptConverter::detectAndConvertEncoding(std::string& content)
{
    std::string utf8Content;
    if (!LegacyTextDecoder::decodeToUtf8(
            content,
            LegacyTextEncoding::Auto,
            utf8Content))
    {
        return false;
    }
    content = std::move(utf8Content);
    return true;
}

bool ScriptConverter::convertFileWithEncoding(const std::string& fileName, bool convertToUtf8)
{
    return convertFileWithEncoding(fileName, fileName, convertToUtf8);
}

bool ScriptConverter::convertFileWithEncoding(const std::string& inputFileName, const std::string& outputFileName, bool convertToUtf8)
{
    if (!convertToUtf8)
    {
        lastMessage = "GBK output is disabled; text output must remain UTF-8: " + inputFileName;
        return false;
    }

    std::vector<uint8_t> fileData = Util::readFileToBuffer(inputFileName);
    if (fileData.empty())
    {
        lastMessage = "Cannot open file: " + inputFileName;
        return false;
    }

    std::string content(reinterpret_cast<const char*>(fileData.data()), fileData.size());

    if (isIniFile(content))
    {
        if (!detectAndConvertEncoding(content))
        {
            lastMessage = "Encoding conversion failed: " + inputFileName;
            return false;
        }
        std::vector<uint8_t> outputData;
        const uint8_t bom[] = {0xEF, 0xBB, 0xBF};
        outputData.insert(outputData.end(), bom, bom + 3);
        outputData.insert(outputData.end(), content.begin(), content.end());
        if (!Util::writeFileFromBuffer(outputFileName, outputData.data(), outputData.size()))
        {
            lastMessage = "Cannot write file: " + outputFileName;
            return false;
        }
        lastMessage = "INI file, encoding converted: " + inputFileName;
        return true;
    }

    if (!detectAndConvertEncoding(content))
    {
        lastMessage = "Encoding conversion failed: " + inputFileName;
        return false;
    }

    std::string converted = convertScript(content);

    std::vector<uint8_t> outputData;
    const uint8_t bom[] = {0xEF, 0xBB, 0xBF};
    outputData.insert(outputData.end(), bom, bom + 3);
    outputData.insert(outputData.end(), converted.begin(), converted.end());

    if (!Util::writeFileFromBuffer(outputFileName, outputData.data(), outputData.size()))
    {
        lastMessage = "Cannot write file: " + outputFileName;
        return false;
    }

    lastMessage = "Script converted successfully (UTF-8): " + inputFileName;
    return true;
}
