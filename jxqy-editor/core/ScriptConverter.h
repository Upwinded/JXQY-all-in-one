#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <set>

struct ScriptCommand
{
    std::string name;
    std::vector<std::string> arguments;
    std::string rawLine;
    int lineNumber = 0;
};

struct IfBlock
{
    int ifLine = -1;
    int labelLine = -1;
    std::string labelName;
    std::string condition;
    std::vector<std::string> bodyLines;
};

struct LabelInfo
{
    std::string name;
    int lineNumber = -1;
};

struct ScriptConversionDiagnostic
{
    int lineNumber = 0;
    std::string category;
    std::string message;
    std::string line;
};

class ScriptConverter
{
public:
    ScriptConverter();
    ~ScriptConverter();

    bool convertFile(const std::string& fileName);
    bool convertFile(const std::string& inputFileName, const std::string& outputFileName);
    bool convertFileWithEncoding(const std::string& fileName, bool convertToUtf8 = true);
    bool convertFileWithEncoding(const std::string& inputFileName, const std::string& outputFileName, bool convertToUtf8 = true);

    std::string convertScript(const std::string& scriptContent);
    std::string getLastMessage() const;
    const std::vector<ScriptConversionDiagnostic>& getDiagnostics() const;

    static bool isScriptFile(const std::string& fileName);
    static bool isIniFile(const std::string& content);
    static bool detectAndConvertEncoding(std::string& content);
    static const std::set<std::string>& runtimeApiNames();

private:
    std::vector<std::string> splitLines(const std::string& content);
    std::string trimLine(const std::string& line);
    std::string removeSpaces(const std::string& line);

    std::vector<std::string> extractTokens(const std::string& line, const std::string& delimiters);

    std::string convertAssignStatement(const std::string& line);
    std::string convertAddStatement(const std::string& line);
    std::string convertVariableAssignment(const std::string& line);
    std::string convertIfStatement(const std::string& line);
    std::string convertLabelStatement(const std::string& line);
    std::string normalizeFunctionCall(const std::string& line);

    std::string findOperator(const std::string& line, int& position);
    std::string convertOperatorToLua(const std::string& op);
    std::string convertLegacyConditionToLua(const std::string& condition);

    std::string convertCommand(const std::string& line);
    std::string convertGotoStatement(const std::string& line);
    std::string convertReturnStatement(const std::string& line);
    std::string convertComment(const std::string& line);

    std::string wrapInFunction(const std::string& body, const std::string& functionName = "Run");
    void recordDiagnostic(const std::string& category, const std::string& message, const std::string& line);

    std::string lastMessage;
    std::vector<ScriptConversionDiagnostic> diagnostics;
    int currentLineNumber = 0;
};
