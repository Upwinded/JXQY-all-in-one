#include "LuaFunctionOutline.h"

#include "LuaLexer.h"

namespace
{
QList<const LuaToken*> significantTokens(
    const LuaLexResult& lexResult)
{
    QList<const LuaToken*> tokens;
    tokens.reserve(lexResult.tokens.size());
    for (const LuaToken& token :
         lexResult.tokens)
    {
        if (!token.complete)
            break;
        if (LuaLexer::isSignificant(token))
            tokens.append(&token);
    }
    return tokens;
}

bool isIdentifier(const LuaToken& token)
{
    return token.kind ==
        LuaTokenKind::Identifier;
}

bool isSymbol(
    const LuaToken& token,
    const QChar symbol)
{
    return token.kind == LuaTokenKind::Symbol &&
        token.text.size() == 1 && token.text.front() == symbol;
}
}

QList<LuaFunctionOutlineEntry> LuaFunctionOutline::parse(
    const QString& sourceText)
{
    const LuaLexResult lexResult =
        LuaLexer::lex(sourceText);
    const QList<const LuaToken*> tokens =
        significantTokens(lexResult);
    QList<LuaFunctionOutlineEntry> entries;
    for (int index = 0; index < tokens.size(); ++index)
    {
        const LuaToken& functionToken =
            *tokens[index];
        if (functionToken.kind !=
                LuaTokenKind::Keyword ||
            functionToken.text != QStringLiteral("function"))
        {
            continue;
        }

        const bool localDeclaration = index > 0 &&
            tokens[index - 1]->kind ==
                LuaTokenKind::Keyword &&
            tokens[index - 1]->text ==
                QStringLiteral("local");
        const bool globalExtension = index > 0 &&
            isIdentifier(*tokens[index - 1]) &&
            tokens[index - 1]->text ==
                QStringLiteral("global");
        if (globalExtension)
            continue;

        int cursor = index + 1;
        if (cursor >= tokens.size() ||
            !isIdentifier(*tokens[cursor]))
        {
            continue;
        }

        QString functionName =
            tokens[cursor++]->text;
        if (!localDeclaration)
        {
            while (cursor + 1 < tokens.size() &&
                   isSymbol(*tokens[cursor], '.') &&
                   isIdentifier(
                       *tokens[cursor + 1]))
            {
                functionName +=
                    QStringLiteral(".") +
                    tokens[cursor + 1]->text;
                cursor += 2;
            }
            if (cursor + 1 < tokens.size() &&
                isSymbol(*tokens[cursor], ':') &&
                isIdentifier(
                    *tokens[cursor + 1]))
            {
                functionName +=
                    QStringLiteral(":") +
                    tokens[cursor + 1]->text;
                cursor += 2;
            }
        }

        if (cursor >= tokens.size() ||
            !isSymbol(*tokens[cursor], '('))
        {
            continue;
        }

        entries.append({functionName,
                        functionToken.range.start.line,
                        localDeclaration});
    }
    return entries;
}
