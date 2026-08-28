#include "../core/LuaLexer.h"
#include "../core/LuaScriptSyntaxValidator.h"

#include <QCoreApplication>

#include <iostream>
#include <string>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool lexerAccepted(const QString& source)
{
    const LuaLexResult result =
        LuaLexer::lex(source);
    if (!result.warnings.isEmpty() ||
        result.cancelled)
    {
        return false;
    }
    for (const LuaToken& token : result.tokens)
    {
        if (!token.complete)
            return false;
    }
    return true;
}

bool runtimeAccepted(const QString& source)
{
    const QByteArray utf8 = source.toUtf8();
    const std::string content(
        utf8.constData(),
        static_cast<std::size_t>(utf8.size()));
    return LuaScriptSyntaxValidator::
        validateScriptContent(
            QStringLiteral(
                "story-graph-dialect.lua"),
            content)
        .message.isEmpty();
}

bool testAcceptedLua55Dialect()
{
    const QList<QString> fixtures = {
        QStringLiteral(
            "local value = 0x1.fp+2 // 2 << 1\n"
            "return value\n"),
        QStringLiteral(
            "local text = [=[\nlong text]=]\n"
            "local emoji = \"\\u{1F600}\\z  next\"\n"),
        QStringLiteral(
            "global function story_entry()\n"
            "  return true\n"
            "end\n"),
        QStringLiteral(
            "local attribute <const> = 3\n"
            "return attribute\n")
    };

    bool passed = true;
    for (const QString& fixture : fixtures)
    {
        passed &= check(
            runtimeAccepted(fixture),
            "bundled Lua 5.5 accepts dialect fixture");
        passed &= check(
            lexerAccepted(fixture),
            "shared lexer accepts Lua 5.5 dialect fixture");
    }
    return passed;
}

bool testRejectedLua55Dialect()
{
    const QList<QString> fixtures = {
        QStringLiteral(
            "local text = \"bad\\q\"\n"),
        QStringLiteral(
            "local text = [=[unterminated\n"),
        QStringLiteral(
            "local text = 'raw\nnewline'\n")
    };

    bool passed = true;
    for (const QString& fixture : fixtures)
    {
        passed &= check(
            !runtimeAccepted(fixture),
            "bundled Lua 5.5 rejects malformed fixture");
        passed &= check(
            !lexerAccepted(fixture),
            "shared lexer diagnoses malformed fixture");
    }
    return passed;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testAcceptedLua55Dialect();
    passed &= testRejectedLua55Dialect();
    if (passed)
    {
        std::cout
            << "All story graph dialect tests passed\n";
    }
    return passed ? 0 : 1;
}
