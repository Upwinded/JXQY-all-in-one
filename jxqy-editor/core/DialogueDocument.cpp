#include "DialogueDocument.h"

#include "AuthoringMutationGate.h"
#include "LuaLexer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace
{
bool readFileBytes(const QString& filePath, QByteArray& bytes,
                   QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    bytes = file.readAll();
    return true;
}

bool writeAtomically(const QString& filePath, const QByteArray& bytes,
                     QString* errorMessage)
{
    auto mutationLease = AuthoringMutationGate::instance().
        acquireMutationLeaseForPath(filePath);
    if (!mutationLease)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("资源正在更新或进行其他写入。");
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        file.cancelWriting();
        return false;
    }
    return true;
}

bool isPositiveNumber(const QString& value, int* number = nullptr)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed <= 0 || QString::number(parsed) != value)
        return false;
    if (number)
        *number = parsed;
    return true;
}

int speakerDelimiterPosition(const QString& raw, QChar* delimiter = nullptr)
{
    const int fullWidth = raw.indexOf(QChar(0xff1a));
    const int ascii = raw.indexOf(QLatin1Char(':'));
    int position = -1;
    if (fullWidth > 0 && ascii > 0)
        position = std::min(fullWidth, ascii);
    else if (fullWidth > 0)
        position = fullWidth;
    else if (ascii > 0)
        position = ascii;

    if (position <= 0 || position > 48 ||
        raw.left(position).contains(QStringLiteral("<enter>"),
                                    Qt::CaseInsensitive))
    {
        return -1;
    }
    if (delimiter)
        *delimiter = raw.at(position);
    return position;
}
}

bool DialogueDocument::load(const QByteArray& bytes, QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    orderedSections.clear();
    loaded = false;
    if (bytes.isEmpty())
    {
        ini = INIFileEditor();
        loaded = true;
        return true;
    }
    if (bytes.contains('\0') ||
        !ini.loadFromBuffer(
            bytes.constData(), bytes.size(),
            INIFileEditor::UnrecognizedLinePolicy::Preserve))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("对话文件必须是可读的 INI 文本。");
        return false;
    }

    const QString source = QString::fromUtf8(bytes);
    static const QRegularExpression sectionExpression(
        QStringLiteral(R"(^\s*\[([^\]]+)\])"));
    QSet<QString> seen;
    const QStringList sourceLines = source.split(QRegularExpression(
        QStringLiteral("\\r?\\n")));
    for (QString sourceLine : sourceLines)
    {
        if (!sourceLine.isEmpty() && sourceLine.front() == QChar(0xfeff))
            sourceLine.remove(0, 1);
        const QRegularExpressionMatch match =
            sectionExpression.match(sourceLine);
        if (!match.hasMatch())
            continue;
        const QString section = match.captured(1).trimmed();
        const QString key = section.toCaseFolded();
        if (section.isEmpty() || key == QStringLiteral("name") ||
            seen.contains(key) ||
            !ini.hasKey(section.toUtf8().toStdString(), "1"))
        {
            continue;
        }
        seen.insert(key);
        orderedSections.append(section);
    }

    for (const std::string& rawSection : ini.getSectionNames())
    {
        const QString section = QString::fromUtf8(rawSection);
        const QString key = section.toCaseFolded();
        if (key == QStringLiteral("name") || seen.contains(key) ||
            !ini.hasKey(rawSection, "1"))
        {
            continue;
        }
        seen.insert(key);
        orderedSections.append(section);
    }

    loaded = true;
    return true;
}

bool DialogueDocument::openFile(const QString& filePath,
                                QString* errorMessage)
{
    QByteArray bytes;
    return readFileBytes(filePath, bytes, errorMessage) &&
        load(bytes, errorMessage);
}

bool DialogueDocument::saveFile(const QString& filePath,
                                QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    if (!loaded)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("尚未打开对话文件。");
        return false;
    }
    return writeAtomically(filePath, serializedBytes(), errorMessage);
}

QByteArray DialogueDocument::serializedBytes() const
{
    if (!loaded)
        return {};
    const std::string content = ini.saveToString();
    return QByteArray(content.data(), static_cast<qsizetype>(content.size()));
}

QStringList DialogueDocument::sectionNames() const
{
    return orderedSections;
}

bool DialogueDocument::hasSection(const QString& section) const
{
    return std::any_of(
        orderedSections.cbegin(), orderedSections.cend(),
        [&section](const QString& candidate)
        {
            return candidate.compare(section, Qt::CaseInsensitive) == 0;
        });
}

int DialogueDocument::lineCount(const QString& section) const
{
    if (!loaded || !hasSection(section))
        return 0;
    const std::string rawSection = section.toUtf8().toStdString();
    int count = 0;
    while (count < 10000 && ini.hasKey(
               rawSection, std::to_string(count + 1)))
    {
        ++count;
    }
    return count;
}

DialogueLine DialogueDocument::line(const QString& section, int row) const
{
    DialogueLine result;
    const int number = row + 1;
    if (!loaded || row < 0 || number > lineCount(section))
        return result;

    result.number = number;
    const std::string rawSection = section.toUtf8().toStdString();
    const std::string numberKey = std::to_string(number);
    const QString raw = QString::fromUtf8(
        ini.get(rawSection, numberKey, ""));
    const int delimiterPosition = speakerDelimiterPosition(raw);
    if (delimiterPosition > 0)
    {
        result.speaker = raw.left(delimiterPosition).trimmed();
        result.text = decodeDialogueText(raw.mid(delimiterPosition + 1));
    }
    else
    {
        result.text = decodeDialogueText(raw);
    }

    const std::string portraitKey = "head" + numberKey;
    if (!ini.hasKey(rawSection, portraitKey))
    {
        result.portraitMode = DialoguePortraitMode::KeepPrevious;
    }
    else
    {
        result.portraitReference = QString::fromUtf8(
            ini.get(rawSection, portraitKey, ""));
        result.portraitMode = result.portraitReference == "NoHeadChange"
            ? DialoguePortraitMode::KeepPrevious : result.portraitReference.isEmpty()
            ? DialoguePortraitMode::NoPortrait
            : DialoguePortraitMode::Reference;
    }
    return result;
}

DialogueResolvedPortrait DialogueDocument::resolvedPortrait(
    const QString& section, int row) const
{
    DialogueResolvedPortrait result;
    for (int currentRow = row; currentRow >= 0; currentRow -= 2)
    {
        const DialogueLine current = line(section, currentRow);
        if (current.number == 0)
            break;
        if (current.portraitMode == DialoguePortraitMode::Reference)
        {
            result.reference = current.portraitReference;
            result.sourceRow = currentRow;
            return result;
        }
        if (current.portraitMode == DialoguePortraitMode::NoPortrait)
        {
            result.sourceRow = currentRow;
            result.explicitlyHidden = true;
            return result;
        }
    }
    return result;
}

bool DialogueDocument::setLine(const QString& section, int row,
                               const QString& speaker,
                               const QString& text)
{
    const DialogueLine previous = line(section, row);
    if (previous.number == 0)
        return false;

    QChar delimiter(0xff1a);
    const std::string rawSection = section.toUtf8().toStdString();
    const std::string numberKey = std::to_string(row + 1);
    const QString previousRaw = QString::fromUtf8(
        ini.get(rawSection, numberKey, ""));
    speakerDelimiterPosition(previousRaw, &delimiter);

    const QString normalizedSpeaker = speaker.trimmed();
    const QString encodedText = encodeDialogueText(text);
    const QString value = normalizedSpeaker.isEmpty()
        ? encodedText
        : normalizedSpeaker + delimiter + encodedText;
    ini.set(rawSection, numberKey, value.toUtf8().toStdString());
    return true;
}

bool DialogueDocument::setPortrait(const QString& section, int row,
                                   DialoguePortraitMode mode,
                                   const QString& reference)
{
    if (line(section, row).number == 0)
        return false;
    const auto previous = line(section, row);
    if (previous.portraitMode == mode &&
        (mode != DialoguePortraitMode::Reference || previous.portraitReference == reference)) return true;
    const std::string rawSection = section.toUtf8().toStdString();
    const std::string portraitKey = "head" + std::to_string(row + 1);
    switch (mode)
    {
    case DialoguePortraitMode::KeepPrevious:
        ini.removeKey(rawSection, portraitKey);
        return true;
    case DialoguePortraitMode::NoPortrait:
        ini.set(rawSection, portraitKey, "");
        return true;
    case DialoguePortraitMode::Reference:
        if (reference.trimmed().isEmpty())
            return false;
        ini.set(rawSection, portraitKey,
                reference.trimmed().toUtf8().toStdString());
        return true;
    }
    return false;
}

bool DialogueDocument::addSection(const QString& name, const QString& firstText)
{
    const QString clean = name.trimmed();
    if (!loaded || clean.compare("name", Qt::CaseInsensitive) == 0 || firstText.isEmpty() ||
        clean.contains(QChar('\0')) || !ini.addSection(clean.toUtf8().toStdString())) return false;
    ini.set(clean.toUtf8().toStdString(), "1", encodeDialogueText(firstText).toUtf8().toStdString());
    orderedSections.append(clean);
    return true;
}

bool DialogueDocument::copySection(const QString& section, const QString& name)
{
    const QString clean = name.trimmed();
    if (!hasSection(section) || clean.compare("name", Qt::CaseInsensitive) == 0 ||
        clean.contains(QChar('\0')) || !ini.copySection(section.toUtf8().toStdString(), clean.toUtf8().toStdString())) return false;
    orderedSections.append(clean);
    return true;
}

bool DialogueDocument::renameSection(const QString& section, const QString& name)
{
    const QString clean = name.trimmed();
    if (!hasSection(section) || clean.compare("name", Qt::CaseInsensitive) == 0 ||
        clean.contains(QChar('\0')) || !ini.renameSection(section.toUtf8().toStdString(), clean.toUtf8().toStdString())) return false;
    for (auto& item : orderedSections)
        if (item.compare(section, Qt::CaseInsensitive) == 0) { item = clean; break; }
    return true;
}

bool DialogueDocument::removeSection(const QString& section)
{
    if (!hasSection(section)) return false;
    ini.removeSection(section.toUtf8().toStdString());
    for (int index = 0; index < orderedSections.size(); ++index)
        if (orderedSections[index].compare(section, Qt::CaseInsensitive) == 0) { orderedSections.removeAt(index); break; }
    return true;
}

bool DialogueDocument::canReorderLines(const QString& section) const
{
    const int count = lineCount(section);
    if (count <= 0) return false;
    for (const auto& name : ini.getKeyNames(section.toUtf8().toStdString()))
    {
        QString key = QString::fromUtf8(name);
        if (key.startsWith("head", Qt::CaseInsensitive)) key.remove(0, 4);
        int number = 0;
        if (isPositiveNumber(key, &number) && number > count) return false;
    }
    return true;
}

bool DialogueDocument::rewriteLines(const QString& section, const QVector<int>& order, const QString& insertedText)
{
    if (!canReorderLines(section) || order.isEmpty() || order.size() > 10000) return false;
    const int count = lineCount(section);
    const auto rawSection = section.toUtf8().toStdString();
    struct Row
    {
        std::string text;
        bool hasPortrait;
        std::string portrait;
        DialogueResolvedPortrait resolved;
    };
    QVector<Row> rows;
    for (int index : order)
    {
        if (index < -1 || index >= count) return false;
        if (index < 0)
        {
            if (insertedText.isEmpty()) return false;
            rows.append({encodeDialogueText(insertedText).toUtf8().toStdString(), true, "", {}});
            continue;
        }
        const auto number = std::to_string(index + 1);
        rows.append({ini.get(rawSection, number, ""), ini.hasKey(rawSection, "head" + number),
            ini.get(rawSection, "head" + number, ""), resolvedPortrait(section, index)});
    }
    for (int index = 1; index <= count; ++index)
    {
        ini.removeKey(rawSection, std::to_string(index));
        ini.removeKey(rawSection, "head" + std::to_string(index));
    }
    for (int index = 0; index < rows.size(); ++index)
    {
        const auto& row = rows[index];
        const auto number = std::to_string(index + 1);
        ini.set(rawSection, number, row.text);
        if (!row.hasPortrait || row.portrait == "NoHeadChange")
        {
            const auto previous = index >= 2 ? resolvedPortrait(section, index - 2) : DialogueResolvedPortrait();
            if (previous.reference != row.resolved.reference || previous.explicitlyHidden != row.resolved.explicitlyHidden)
                ini.set(rawSection, "head" + number, row.resolved.reference.toUtf8().toStdString());
            else if (row.hasPortrait) ini.set(rawSection, "head" + number, row.portrait);
        }
        else ini.set(rawSection, "head" + number, row.portrait);
    }
    return true;
}

bool DialogueDocument::insertLine(const QString& section, int row, const QString& text)
{
    const int count = lineCount(section);
    if (row < 0 || row > count) return false;
    QVector<int> order;
    for (int index = 0; index <= count; ++index)
    {
        if (index == row) order.append(-1);
        if (index < count) order.append(index);
    }
    return rewriteLines(section, order, text);
}

bool DialogueDocument::removeLine(const QString& section, int row)
{
    const int count = lineCount(section);
    if (row < 0 || row >= count || count <= 1) return false;
    QVector<int> order;
    for (int index = 0; index < count; ++index) if (index != row) order.append(index);
    return rewriteLines(section, order);
}

bool DialogueDocument::moveLine(const QString& section, int from, int to)
{
    const int count = lineCount(section);
    if (from < 0 || to < 0 || from >= count || to >= count || from == to) return false;
    QVector<int> order;
    for (int index = 0; index < count; ++index) order.append(index);
    order.move(from, to);
    return rewriteLines(section, order);
}

int DialogueDocument::hiddenFieldCount() const
{
    if (!loaded)
        return 0;
    QSet<QString> dialogueSections;
    for (const QString& section : orderedSections)
        dialogueSections.insert(section.toCaseFolded());

    int count = 0;
    for (const auto& sectionPair : ini.getIniMap().sections)
    {
        const QString section = QString::fromUtf8(
            sectionPair.second.originalName);
        if (!dialogueSections.contains(section.toCaseFolded()))
        {
            count += static_cast<int>(sectionPair.second.keys.size());
            continue;
        }
        const int visibleLines = lineCount(section);
        for (const auto& keyPair : sectionPair.second.keys)
        {
            const QString key = QString::fromUtf8(keyPair.first);
            int number = 0;
            bool visible = isPositiveNumber(key, &number) &&
                number <= visibleLines;
            if (!visible && key.startsWith(QStringLiteral("head")))
            {
                visible = isPositiveNumber(key.mid(4), &number) &&
                    number <= visibleLines;
            }
            if (!visible)
                ++count;
        }
    }
    return count;
}

QVector<DialogueReference> DialogueDocument::findLiteralTalkReferences(
    const QString& scriptText)
{
    const auto lexed = LuaLexer::lex(scriptText);
    QList<LuaToken> tokens;
    for (const auto& token : lexed.tokens)
        if (LuaLexer::isSignificant(token)) tokens.append(token);
    QVector<DialogueReference> references;
    for (int index = 0; index + 3 < tokens.size(); ++index)
    {
        const auto& call = tokens[index];
        const auto& argument = tokens[index + 2];
        if (call.kind != LuaTokenKind::Identifier || call.text.compare("talk", Qt::CaseInsensitive) != 0 ||
            tokens[index + 1].text != "(" || tokens[index + 3].kind != LuaTokenKind::Symbol ||
            tokens[index + 3].text != ")" || !argument.complete || argument.decodedText.isEmpty() ||
            (argument.kind != LuaTokenKind::String && argument.kind != LuaTokenKind::LongString) ||
            (index > 0 && (tokens[index - 1].text == "." || tokens[index - 1].text == ":" || tokens[index - 1].text == "function")))
            continue;
        DialogueReference reference;
        reference.section = argument.decodedText;
        reference.startOffset = static_cast<int>(call.range.start.utf16Offset);
        reference.endOffset = static_cast<int>(tokens[index + 3].range.end.utf16Offset);
        reference.line = call.range.start.line;
        reference.column = call.range.start.column;
        references.append(reference);
    }
    return references;
}

std::optional<DialogueReference> DialogueDocument::literalTalkReferenceAt(
    const QString& scriptText, int cursorPosition)
{
    const int boundedPosition = std::clamp(
        cursorPosition, 0, static_cast<int>(scriptText.size()));
    const int cursorLine = scriptText.left(boundedPosition).count(
        QLatin1Char('\n')) + 1;
    const QVector<DialogueReference> references =
        findLiteralTalkReferences(scriptText);
    for (const DialogueReference& reference : references)
    {
        if (boundedPosition >= reference.startOffset &&
            boundedPosition <= reference.endOffset)
        {
            return reference;
        }
    }
    for (const DialogueReference& reference : references)
    {
        if (reference.line == cursorLine)
            return reference;
    }
    return std::nullopt;
}

QString DialogueDocument::talkFileForScript(const QString& scriptPath)
{
    if (scriptPath.trimmed().isEmpty())
        return {};
    return QDir(QFileInfo(scriptPath).absolutePath()).filePath(
        QStringLiteral("talk.txt"));
}

QString DialogueDocument::decodeDialogueText(const QString& value)
{
    QString result = value;
    result.replace(QRegularExpression(
        QStringLiteral("<enter>"),
        QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("\n"));
    return result;
}

QString DialogueDocument::encodeDialogueText(const QString& value)
{
    QString result = value;
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    result.replace(QLatin1Char('\n'), QStringLiteral("<enter>"));
    return result;
}

QString DialogueDocument::unescapeLuaString(const QString& value)
{
    QString result;
    result.reserve(value.size());
    for (int index = 0; index < value.size(); ++index)
    {
        const QChar current = value.at(index);
        if (current != QLatin1Char('\\') || index + 1 >= value.size())
        {
            result.append(current);
            continue;
        }
        const QChar escaped = value.at(++index);
        if (escaped == QLatin1Char('n'))
            result.append(QLatin1Char('\n'));
        else if (escaped == QLatin1Char('r'))
            result.append(QLatin1Char('\r'));
        else if (escaped == QLatin1Char('t'))
            result.append(QLatin1Char('\t'));
        else
            result.append(escaped);
    }
    return result;
}
