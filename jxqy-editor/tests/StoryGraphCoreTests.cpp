#include "../core/LuaLexer.h"
#include "../core/StoryGraphAnalyzer.h"
#include "../core/StoryGraphLayout.h"
#include "../core/StoryGraphModel.h"
#include "../core/StoryGraphRuntimeApiCatalog.h"
#include "../core/StoryGraphSemanticCatalog.h"

#include <QCoreApplication>

#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QList<const LuaToken*> significantTokens(
    const LuaLexResult& result)
{
    QList<const LuaToken*> tokens;
    for (const LuaToken& token : result.tokens)
    {
        if (LuaLexer::isSignificant(token))
            tokens.append(&token);
    }
    return tokens;
}

StoryGraphDocumentResult analyzeSource(
    const QString& source,
    const QString& virtualPath =
        QStringLiteral(
            "script/map/test/story.lua"))
{
    StoryGraphAnalysisRequest request;
    request.source.identity.portableRootKey =
        QStringLiteral("active:test:0");
    request.source.identity.virtualPath =
        virtualPath;
    request.source.utf8Bytes =
        source.toUtf8();
    request.analysisGeneration = 7;
    request.effectiveMapFolder =
        QStringLiteral("test");
    request.effectiveMapFolderKnown = true;
    return StoryGraphAnalyzer::analyze(
        request);
}

int nodeKindCount(
    const StoryGraphResult& graph,
    StoryGraphNodeKind kind)
{
    int count = 0;
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.kind == kind)
            ++count;
    }
    return count;
}

int nodeKindCountAtLine(
    const StoryGraphResult& graph,
    StoryGraphNodeKind kind,
    int line)
{
    int count = 0;
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.kind == kind &&
            node.sourceRange.start.line == line)
        {
            ++count;
        }
    }
    return count;
}

bool hasEdgeKind(
    const StoryGraphResult& graph,
    StoryGraphEdgeKind kind)
{
    for (const StoryGraphEdge& edge :
         graph.edges)
    {
        if (edge.kind == kind)
            return true;
    }
    return false;
}

const StoryGraphNode* findSemanticCall(
    const StoryGraphResult& graph,
    const QString& apiName,
    const QString& literalTarget =
        QString())
{
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.apiName == apiName &&
            (literalTarget.isEmpty() ||
             node.literalTarget ==
                 literalTarget))
        {
            return &node;
        }
    }
    return nullptr;
}

QList<const StoryGraphNode*> findSemanticCalls(
    const StoryGraphResult& graph,
    const QString& apiName)
{
    QList<const StoryGraphNode*> result;
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.apiName == apiName)
            result.append(&node);
    }
    return result;
}

const StoryGraphNode* findVariableNode(
    const StoryGraphResult& graph,
    StoryGraphNodeKind kind,
    const QString& variableName,
    int line = 0)
{
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.kind == kind &&
            node.variableName == variableName &&
            (line <= 0 ||
             node.sourceRange.start.line == line))
        {
            return &node;
        }
    }
    return nullptr;
}

int variableNodeCount(
    const StoryGraphResult& graph,
    StoryGraphNodeKind kind)
{
    int count = 0;
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.kind == kind)
            ++count;
    }
    return count;
}

bool hasDirectedEdge(
    const StoryGraphResult& graph,
    const QString& fromNodeId,
    const QString& toNodeId,
    StoryGraphEdgeKind kind)
{
    for (const StoryGraphEdge& edge :
         graph.edges)
    {
        if (edge.fromNodeId == fromNodeId &&
            edge.toNodeId == toNodeId &&
            edge.kind == kind)
        {
            return true;
        }
    }
    return false;
}

bool hasAnyDirectedEdge(
    const StoryGraphResult& graph,
    const QString& fromNodeId,
    const QString& toNodeId)
{
    for (const StoryGraphEdge& edge :
         graph.edges)
    {
        if (edge.fromNodeId == fromNodeId &&
            edge.toNodeId == toNodeId)
        {
            return true;
        }
    }
    return false;
}

const StoryGraphNode* findControlNodeAtLine(
    const StoryGraphResult& graph,
    StoryGraphNodeKind kind,
    int line)
{
    for (const StoryGraphNode& node :
         graph.nodes)
    {
        if (node.kind == kind &&
            node.sourceRange.start.line == line)
        {
            return &node;
        }
    }
    return nullptr;
}

const StoryGraphEdge* findDirectedEdge(
    const StoryGraphResult& graph,
    const QString& fromNodeId,
    const QString& toNodeId,
    StoryGraphEdgeKind kind)
{
    for (const StoryGraphEdge& edge :
         graph.edges)
    {
        if (edge.fromNodeId == fromNodeId &&
            edge.toNodeId == toNodeId &&
            edge.kind == kind)
        {
            return &edge;
        }
    }
    return nullptr;
}

const StoryGraphEdge* findOutgoingEdge(
    const StoryGraphResult& graph,
    const QString& fromNodeId,
    StoryGraphEdgeKind kind)
{
    for (const StoryGraphEdge& edge :
         graph.edges)
    {
        if (edge.fromNodeId == fromNodeId &&
            edge.kind == kind)
        {
            return &edge;
        }
    }
    return nullptr;
}

int warningCodeCount(
    const StoryGraphDocumentResult& result,
    const QString& diagnosticCode)
{
    int count = 0;
    for (const StoryGraphWarning& warning :
         result.warnings)
    {
        if (warning.diagnosticCode ==
            diagnosticCode)
        {
            ++count;
        }
    }
    return count;
}

int warningCodeCountAtLine(
    const StoryGraphDocumentResult& result,
    const QString& diagnosticCode,
    int line)
{
    int count = 0;
    for (const StoryGraphWarning& warning :
         result.warnings)
    {
        if (warning.diagnosticCode ==
                diagnosticCode &&
            warning.sourceRange.start.line == line)
        {
            ++count;
        }
    }
    return count;
}

bool testKeywordsNumbersAndSymbols()
{
    const LuaLexResult result = LuaLexer::lex(
        QStringLiteral(
            "local value = 0x1.fp+2 + .5e1\n"
            "if value >= 2 then goto done end\n"
            "::done::"));
    const QList<const LuaToken*> tokens =
        significantTokens(result);
    bool passed = true;
    passed &= check(result.warnings.isEmpty(),
                    "valid token fixture has no warnings");
    passed &= check(tokens.size() == 17,
                    "valid token fixture token count");
    passed &= check(tokens.at(0)->kind ==
                        LuaTokenKind::Keyword &&
                    tokens.at(0)->text ==
                        QStringLiteral("local"),
                    "local is a keyword");
    passed &= check(!LuaLexer::isKeyword(
                        QStringLiteral("global")),
                    "global remains a Lua 5.5 contextual identifier");
    passed &= check(tokens.at(3)->kind ==
                        LuaTokenKind::Number &&
                    tokens.at(3)->text ==
                        QStringLiteral("0x1.fp+2"),
                    "hexadecimal float remains one token");
    passed &= check(tokens.at(5)->kind ==
                        LuaTokenKind::Number &&
                    tokens.at(5)->text ==
                        QStringLiteral(".5e1"),
                    "leading-dot decimal remains one token");
    passed &= check(tokens.at(8)->text ==
                        QStringLiteral(">="),
                    "multi-character comparison remains one token");
    passed &= check(tokens.at(14)->text ==
                        QStringLiteral("::") &&
                    tokens.at(16)->text ==
                        QStringLiteral("::"),
                    "label delimiters remain whole tokens");
    return passed;
}

bool testStringsCommentsAndLongBrackets()
{
    const QString source =
        QStringLiteral(
            "-- ignored \"text\"\n"
            "Talk(\"line\\ntext\", 'tab\\tvalue')\n"
            "local path = [=[\nscript/common/入口.txt]=]\n"
            "--[==[ long\ncomment ]==]\n");
    const LuaLexResult result =
        LuaLexer::lex(
            source,
            QStringLiteral(
                "script/测试.txt"));
    const QList<const LuaToken*> tokens =
        significantTokens(result);
    bool passed = true;
    passed &= check(result.warnings.isEmpty(),
                    "valid strings and comments have no warnings");
    passed &= check(result.tokens.first().kind ==
                        LuaTokenKind::Comment,
                    "line comment is represented by the shared lexer");
    passed &= check(tokens.at(2)->kind ==
                        LuaTokenKind::String &&
                    tokens.at(2)->decodedText ==
                        QStringLiteral("line\ntext"),
                    "double-quoted string is decoded");
    passed &= check(tokens.at(4)->kind ==
                        LuaTokenKind::String &&
                    tokens.at(4)->decodedText ==
                        QStringLiteral("tab\tvalue"),
                    "single-quoted string is decoded");
    passed &= check(tokens.at(9)->kind ==
                        LuaTokenKind::LongString &&
                    tokens.at(9)->decodedText ==
                        QStringLiteral(
                            "script/common/入口.txt"),
                    "long string strips the opening newline");
    const LuaToken& finalComment =
        result.tokens.at(
            result.tokens.size() - 2);
    passed &= check(finalComment.kind ==
                        LuaTokenKind::Comment &&
                    finalComment.range.start.line == 5 &&
                    finalComment.range.end.line == 6,
                    "long comment preserves its line range");
    return passed;
}

bool testUnicodeAndBytePositions()
{
    const QString source =
        QStringLiteral("\"中😀\"; Talk()");
    const LuaLexResult result =
        LuaLexer::lex(
            source,
            QStringLiteral("script/空 格.txt"));
    const QList<const LuaToken*> tokens =
        significantTokens(result);
    bool passed = true;
    passed &= check(result.warnings.isEmpty(),
                    "Unicode string has no warnings");
    passed &= check(tokens.size() == 5,
                    "Unicode position fixture token count");
    passed &= check(tokens.at(0)->range.start.column == 1 &&
                    tokens.at(0)->range.end.column == 5,
                    "columns count Unicode scalar values");
    passed &= check(tokens.at(0)->range.start.utf8ByteOffset == 0 &&
                    tokens.at(0)->range.end.utf8ByteOffset == 9,
                    "string UTF-8 byte range includes non-BMP bytes");
    passed &= check(tokens.at(2)->text ==
                        QStringLiteral("Talk") &&
                    tokens.at(2)->range.start.column == 7 &&
                    tokens.at(2)->range.start.utf16Offset == 7 &&
                    tokens.at(2)->range.start.utf8ByteOffset == 11,
                    "following token positions stay exact");
    passed &= check(tokens.at(2)->range.sourcePath ==
                        QStringLiteral("script/空 格.txt"),
                    "source path is retained on token ranges");
    return passed;
}

bool testWarningsAndRecovery()
{
    const LuaLexResult result =
        LuaLexer::lex(
            QStringLiteral(
                "\"bad\\q\"\n"
                "'raw\n"
                "[=[open"));
    bool passed = true;
    passed &= check(result.warnings.size() == 4,
                    "invalid fixture emits all recoverable warnings");
    passed &= check(result.warnings.at(0).code ==
                        LuaLexWarningCode::InvalidEscape,
                    "invalid escape has stable warning kind");
    passed &= check(result.warnings.at(1).code ==
                        LuaLexWarningCode::RawNewlineInString &&
                    result.warnings.at(2).code ==
                        LuaLexWarningCode::UnterminatedString,
                    "raw newline and unterminated string are distinct");
    passed &= check(result.warnings.at(3).code ==
                        LuaLexWarningCode::UnterminatedLongString,
                    "unterminated long string is reported");
    passed &= check(
        result.warnings.at(3).diagnosticCode ==
            QStringLiteral(
                "story_graph.lexer.unterminated_long_string"),
        "warning exposes stable diagnostic code");
    return passed;
}

bool testCancellation()
{
    const QString source =
        QStringLiteral("[=[") +
        QString(10000, QLatin1Char('a')) +
        QStringLiteral("]=]");
    int callbackCount = 0;
    const LuaLexResult result =
        LuaLexer::lex(
            source,
            QString(),
            [&callbackCount]()
            {
                ++callbackCount;
                return callbackCount == 2;
            });
    bool passed = true;
    passed &= check(result.cancelled,
                    "lexer reports cancellation");
    passed &= check(callbackCount == 2,
                    "lexer checks cancellation inside a long token");
    passed &= check(significantTokens(result).isEmpty(),
                    "cancelled lexer does not publish later tokens");
    return passed;
}

bool testUtf8BomAndInvalidInput()
{
    QByteArray source("\xEF\xBB\xBF");
    source.append(
        QStringLiteral(
            "talk(\"中😀\")")
            .toUtf8());
    const LuaLexResult valid =
        LuaLexer::lexUtf8(
            source,
            QStringLiteral(
                "script/入口.txt"));
    const QList<const LuaToken*> tokens =
        significantTokens(valid);
    bool passed = true;
    passed &= check(valid.warnings.isEmpty(),
                    "valid UTF-8 BOM input has no warnings");
    passed &= check(tokens.size() == 4,
                    "UTF-8 BOM fixture token count");
    passed &= check(tokens.at(0)->text ==
                        QStringLiteral("talk") &&
                    tokens.at(0)->range.start.utf16Offset == 0 &&
                    tokens.at(0)->range.start.utf8ByteOffset == 3,
                    "BOM is skipped while raw byte offsets retain it");
    passed &= check(tokens.at(2)->range.end.utf8ByteOffset ==
                        source.size() - 1,
                    "UTF-8 byte range remains relative to original bytes");

    const QByteArray invalid =
        QByteArray::fromHex(
            "74616c6b28c0af29");
    const LuaLexResult invalidResult =
        LuaLexer::lexUtf8(
            invalid,
            QStringLiteral(
                "script/invalid.txt"));
    passed &= check(invalidResult.warnings.size() == 1 &&
                    invalidResult.warnings.first().code ==
                        LuaLexWarningCode::InvalidUtf8 &&
                    invalidResult.warnings.first().diagnosticCode ==
                        QStringLiteral(
                            "story_graph.lexer.invalid_utf8"),
                    "invalid UTF-8 is rejected with a stable warning");
    passed &= check(significantTokens(invalidResult).isEmpty(),
                    "invalid UTF-8 does not publish guessed tokens");
    return passed;
}

bool testStableGraphIdentifiers()
{
    StoryGraphStableNodeIdInput nodeInput;
    nodeInput.portableRootKey =
        QStringLiteral("active:pack:0");
    nodeInput.strictVirtualPath =
        QStringLiteral(
            "script/map/test/story.lua");
    nodeInput.scopeId =
        QStringLiteral("chunk");
    nodeInput.kind =
        StoryGraphNodeKind::Dialogue;
    nodeInput.semanticFingerprint =
        QStringLiteral(
            "call:say:string:text");
    nodeInput.structuralOccurrenceKey =
        QStringLiteral("dialogue#0");

    const QString firstNodeId =
        makeStoryGraphNodeId(nodeInput);
    const QString repeatedNodeId =
        makeStoryGraphNodeId(nodeInput);
    StoryGraphStableNodeIdInput movedInput =
        nodeInput;
    const QString movedNodeId =
        makeStoryGraphNodeId(movedInput);
    movedInput.strictVirtualPath =
        QStringLiteral(
            "script/map/test/other.lua");
    const QString otherSourceNodeId =
        makeStoryGraphNodeId(movedInput);

    StoryGraphStableEdgeIdInput edgeInput;
    edgeInput.fromNodeId = firstNodeId;
    edgeInput.toNodeId = otherSourceNodeId;
    edgeInput.kind =
        StoryGraphEdgeKind::Call;
    edgeInput.semanticFingerprint =
        QStringLiteral("serial");
    edgeInput.structuralOccurrenceKey =
        QStringLiteral("0");
    const QString edgeId =
        makeStoryGraphEdgeId(edgeInput);

    bool passed = true;
    passed &= check(firstNodeId == repeatedNodeId &&
                    firstNodeId == movedNodeId,
                    "equivalent portable node identity is deterministic");
    passed &= check(firstNodeId != otherSourceNodeId,
                    "virtual source identity affects node ID");
    passed &= check(isStoryGraphNodeId(firstNodeId) &&
                    isStoryGraphEdgeId(edgeId),
                    "stable graph IDs use self-describing SHA-256 shapes");
    return passed;
}

bool testSemanticCatalogContracts()
{
    const StoryGraphSemanticDefinition* runScript =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("runscript"));
    const StoryGraphSemanticDefinition* runScriptAlias =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("runscirpt"));
    const StoryGraphSemanticDefinition* talk =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("talk"));
    const StoryGraphSemanticDefinition* say =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("say"));
    const StoryGraphSemanticDefinition* chooseMultiple =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("choosemultiple"));
    const StoryGraphSemanticDefinition* select =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("select"));
    const StoryGraphSemanticDefinition* runParallelScript =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("runparallelscript"));
    const StoryGraphSemanticDefinition* loadMap =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("loadmap"));
    const StoryGraphSemanticDefinition* loadGame =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("loadgame"));
    const StoryGraphSemanticDefinition* npcAttack =
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("npcattack"));
    const StoryGraphCallSignature* runScriptTrailing =
        runScript != nullptr
        ? runScript->signatureForArgumentCount(3)
        : nullptr;
    const StoryGraphCallSignature* parallelTrailing =
        runParallelScript != nullptr
        ? runParallelScript->
            signatureForArgumentCount(3)
        : nullptr;
    const StoryGraphCallSignature* loadMapTrailing =
        loadMap != nullptr
        ? loadMap->signatureForArgumentCount(2)
        : nullptr;
    const StoryGraphCallSignature* loadGameTrailing =
        loadGame != nullptr
        ? loadGame->signatureForArgumentCount(2)
        : nullptr;

    bool passed = true;
    passed &= check(runScript != nullptr &&
                    runScript->scriptCallKind ==
                        StoryGraphScriptCallKind::Serial &&
                    runScriptTrailing != nullptr &&
                    runScriptTrailing->
                        ignoredTrailingArgumentCount(3) == 2,
                    "runscript is serial and models runtime ignored trailing arguments");
    passed &= check(runScriptAlias != nullptr &&
                    runScriptAlias->canonicalName ==
                        QStringLiteral("runscript") &&
                    runScriptAlias->isAlias(),
                    "runscirpt remains an explicit runtime alias");
    passed &= check(
        StoryGraphSemanticCatalog::findExact(
            QStringLiteral("RunScript")) == nullptr,
        "semantic lookup remains exact and case-sensitive");
    const StoryGraphCallSignature* talkWithTrailing =
        talk != nullptr
        ? talk->signatureForArgumentCount(3)
        : nullptr;
    passed &= check(talkWithTrailing != nullptr &&
                    talkWithTrailing->
                        ignoredTrailingArgumentCount(3) == 1,
                    "talk models runtime ignored trailing arguments");
    passed &= check(say != nullptr &&
                    say->signatureForArgumentCount(4) == nullptr,
                    "legacy four-argument say is not treated as current signature");
    passed &= check(chooseMultiple != nullptr &&
                    chooseMultiple->
                        indexedVariableOutput.enabled &&
                    chooseMultiple->
                        indexedVariableOutput.generatedNamePrefix ==
                        QStringLiteral("$"),
                    "choosemultiple exposes indexed result variables");
    passed &= check(
        select != nullptr &&
        select->signatureForArgumentCount(6) != nullptr &&
        select->signatureForArgumentCount(6)->
            ignoredTrailingArgumentCount(6) == 2,
        "select fixes its four runtime arguments and ignores trailing values");
    passed &= check(
        parallelTrailing != nullptr &&
        parallelTrailing->
            ignoredTrailingArgumentCount(3) == 1 &&
        loadMapTrailing != nullptr &&
        loadMapTrailing->
            ignoredTrailingArgumentCount(2) == 1 &&
        loadGameTrailing != nullptr &&
        loadGameTrailing->
            ignoredTrailingArgumentCount(2) == 1,
        "parallel scripts and map transitions model runtime trailing argument behavior");
    passed &= check(npcAttack != nullptr &&
                    npcAttack->category ==
                        StoryGraphSemanticCategory::Combat,
                    "confirmed combat API is in the semantic catalog");
    return passed;
}

bool testDialogueAndSelectSemanticCalls()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "talk(\"section\")\n"
                "talk(1, 2, ignored)\n"
                "showtalk(3, 4)\n"
                "say(\"text\")\n"
                "say(\"portrait\", 5)\n"
                "select(6, 7, 8, \"result\", ignoredA, ignoredB)\n"
                "Talk(\"mixed\")\n"));
    const QList<const StoryGraphNode*> talkCalls =
        findSemanticCalls(
            result.semanticGraph,
            QStringLiteral("talk"));
    const StoryGraphNode* showTalk =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("showtalk"));
    const QList<const StoryGraphNode*> sayCalls =
        findSemanticCalls(
            result.semanticGraph,
            QStringLiteral("say"));
    const StoryGraphNode* select =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("select"));
    const StoryGraphNode* mixedCaseTalk =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("Talk"));
    const auto hasValidSingleLineRange =
        [](const StoryGraphNode* node, int line)
        {
            return node != nullptr &&
                node->sourceRange.isValid() &&
                node->sourceRange.start.line == line &&
                node->sourceRange.end.line == line &&
                node->sourceRange.start.column == 1 &&
                node->sourceRange.end.column >
                    node->sourceRange.start.column;
        };

    bool passed = true;
    passed &= check(
        talkCalls.size() == 2 &&
            talkCalls.at(0)->kind ==
                StoryGraphNodeKind::Dialogue &&
            talkCalls.at(0)->certainty ==
                StoryGraphCertainty::Certain &&
            hasValidSingleLineRange(
                talkCalls.at(0), 1),
        "literal talk section is a certain dialogue with a valid source range");
    passed &= check(
        talkCalls.size() == 2 &&
            talkCalls.at(1)->kind ==
                StoryGraphNodeKind::Dialogue &&
            talkCalls.at(1)->certainty ==
                StoryGraphCertainty::Certain &&
            hasValidSingleLineRange(
                talkCalls.at(1), 2) &&
            warningCodeCountAtLine(
                result,
                QStringLiteral(
                    "story_graph.semantic.ignored_trailing_arguments"),
                2) == 1,
        "indexed talk ignores trailing arguments without losing certainty");
    passed &= check(
        showTalk != nullptr &&
            showTalk->kind ==
                StoryGraphNodeKind::Dialogue &&
            showTalk->certainty ==
                StoryGraphCertainty::Certain &&
            hasValidSingleLineRange(
                showTalk, 3),
        "showtalk indices produce a certain dialogue with a valid source range");
    passed &= check(
        sayCalls.size() == 2 &&
            sayCalls.at(0)->kind ==
                StoryGraphNodeKind::Dialogue &&
            sayCalls.at(0)->certainty ==
                StoryGraphCertainty::Certain &&
            hasValidSingleLineRange(
                sayCalls.at(0), 4) &&
            sayCalls.at(1)->kind ==
                StoryGraphNodeKind::Dialogue &&
            sayCalls.at(1)->certainty ==
                StoryGraphCertainty::Certain &&
            hasValidSingleLineRange(
                sayCalls.at(1), 5),
        "say text and optional portrait remain certain dialogue calls");
    passed &= check(
        select != nullptr &&
            select->kind ==
                StoryGraphNodeKind::Choice &&
            select->certainty ==
                StoryGraphCertainty::Certain &&
            select->variableName ==
                QStringLiteral("result") &&
            hasValidSingleLineRange(
                select, 6) &&
            warningCodeCountAtLine(
                result,
                QStringLiteral(
                    "story_graph.semantic.ignored_trailing_arguments"),
                6) == 1,
        "select keeps the fourth result variable while ignoring trailing arguments");
    passed &= check(
        mixedCaseTalk != nullptr &&
            mixedCaseTalk->kind !=
                StoryGraphNodeKind::Dialogue &&
            mixedCaseTalk->certainty !=
                StoryGraphCertainty::Certain &&
            hasValidSingleLineRange(
                mixedCaseTalk, 7) &&
            warningCodeCountAtLine(
                result,
                QStringLiteral(
                    "story_graph.semantic.api_case_mismatch"),
                7) == 1,
        "mixed-case Talk is visible but never upgraded to a certain dialogue");
    return passed;
}

bool testRuntimeApiCatalogContracts()
{
    const StoryGraphRuntimeApiDefinition* playSound =
        StoryGraphRuntimeApiCatalog::findExact(
            QStringLiteral("playsound"));
    const StoryGraphRuntimeApiDefinition* messageBox =
        StoryGraphRuntimeApiCatalog::findExact(
            QStringLiteral("messagebox"));

    bool passed = true;
    passed &= check(
        StoryGraphRuntimeApiCatalog::definitions().size() ==
            343,
        "generated runtime API catalog has every unique registration");
    passed &= check(playSound != nullptr &&
                    !playSound->isAlias() &&
                    playSound->canonicalName ==
                        QStringLiteral("playsound"),
                    "canonical runtime API registration is generated");
    passed &= check(messageBox != nullptr &&
                    messageBox->isAlias() &&
                    messageBox->canonicalName ==
                        QStringLiteral("displaymessage"),
                    "runtime alias retains its canonical target");
    passed &= check(
        !StoryGraphRuntimeApiCatalog::containsExact(
            QStringLiteral("PlaySound")),
        "runtime API catalog lookup is exact and case-sensitive");
    const QString fingerprint =
        StoryGraphRuntimeApiCatalog::catalogFingerprint();
    passed &= check(
        fingerprint.startsWith(
            QStringLiteral("story-api-v1:")) &&
        fingerprint.size() == 77,
        "runtime API catalog exposes a versioned SHA-256 identity");

    for (const StoryGraphSemanticDefinition& definition :
         StoryGraphSemanticCatalog::definitions())
    {
        passed &= check(
            StoryGraphRuntimeApiCatalog::containsExact(
                definition.registeredName),
            "every specialized semantic API exists in runtime catalog");
    }
    return passed;
}

bool testDeterministicRecoverableLayout()
{
    StoryGraphResult graph;
    graph.kind = StoryGraphKind::ControlFlow;

    for (const QString& nodeId :
         {QStringLiteral("a"),
          QStringLiteral("b"),
          QStringLiteral("c"),
          QStringLiteral("d")})
    {
        StoryGraphNode node;
        node.id = nodeId;
        node.title =
            QStringLiteral("Node ") + nodeId;
        node.summary =
            QStringLiteral("Deterministic layout fixture");
        graph.nodes.append(node);
    }

    const auto appendEdge =
        [&graph](
            const QString& edgeId,
            const QString& fromNodeId,
            const QString& toNodeId)
    {
        StoryGraphEdge edge;
        edge.id = edgeId;
        edge.fromNodeId = fromNodeId;
        edge.toNodeId = toNodeId;
        graph.edges.append(edge);
    };
    appendEdge(
        QStringLiteral("a-b"),
        QStringLiteral("a"),
        QStringLiteral("b"));
    appendEdge(
        QStringLiteral("b-a"),
        QStringLiteral("b"),
        QStringLiteral("a"));
    appendEdge(
        QStringLiteral("b-c"),
        QStringLiteral("b"),
        QStringLiteral("c"));
    appendEdge(
        QStringLiteral("d-d"),
        QStringLiteral("d"),
        QStringLiteral("d"));
    appendEdge(
        QStringLiteral("missing"),
        QStringLiteral("c"),
        QStringLiteral("missing-node"));

    const StoryGraphLayoutResult first =
        StoryGraphLayout::layout(graph);
    const StoryGraphLayoutResult repeated =
        StoryGraphLayout::layout(graph);

    bool passed = true;
    passed &= check(
        first.status ==
            StoryGraphLayoutStatus::Partial &&
        first.isUsable() &&
        first.nodeRectangles.size() == 4,
        "layout preserves usable nodes while reporting a missing endpoint");
    passed &= check(
        first.nodeRectangles ==
            repeated.nodeRectangles,
        "layout rectangles are deterministic");
    passed &= check(
        first.edgePlacements.size() ==
            repeated.edgePlacements.size(),
        "layout edge output count is deterministic");
    for (int edgeIndex = 0;
         edgeIndex < first.edgePlacements.size() &&
         edgeIndex < repeated.edgePlacements.size();
         ++edgeIndex)
    {
        const StoryGraphEdgePlacement& firstEdge =
            first.edgePlacements.at(edgeIndex);
        const StoryGraphEdgePlacement& repeatedEdge =
            repeated.edgePlacements.at(edgeIndex);
        passed &= check(
            firstEdge.edgeId == repeatedEdge.edgeId &&
            firstEdge.pathPoints ==
                repeatedEdge.pathPoints,
            "layout edge routes are deterministic");
    }

    const QList<QRectF> rectangles =
        first.nodeRectangles.values();
    for (int firstIndex = 0;
         firstIndex < rectangles.size();
         ++firstIndex)
    {
        passed &= check(
            first.bounds.contains(
                rectangles.at(firstIndex)),
            "layout bounds contain every node");
        for (int secondIndex = firstIndex + 1;
             secondIndex < rectangles.size();
             ++secondIndex)
        {
            passed &= check(
                !rectangles.at(firstIndex).intersects(
                    rectangles.at(secondIndex)),
                "layout node rectangles do not overlap");
        }
    }

    bool sawCycleNode = false;
    bool sawSecondWeakComponent = false;
    for (const StoryGraphNodePlacement& placement :
         first.nodePlacements)
    {
        sawCycleNode |= placement.belongsToCycle;
        sawSecondWeakComponent |=
            placement.weakComponentIndex > 0;
    }
    bool sawSelfLoop = false;
    bool sawRoutedCycle = false;
    bool sawMissingEndpoint = false;
    for (const StoryGraphEdgePlacement& placement :
         first.edgePlacements)
    {
        sawSelfLoop |=
            placement.edgeId ==
                QStringLiteral("d-d") &&
            placement.selfLoop &&
            placement.routed;
        sawRoutedCycle |=
            placement.cycleEdge &&
            placement.routed;
        sawMissingEndpoint |=
            placement.edgeId ==
                QStringLiteral("missing") &&
            !placement.routed;
    }
    passed &= check(
        sawCycleNode &&
        sawSecondWeakComponent &&
        sawSelfLoop &&
        sawRoutedCycle &&
        sawMissingEndpoint,
        "layout marks cycles, isolates components, and retains unrouted edges");

    const StoryGraphLayoutResult cancelled =
        StoryGraphLayout::layout(
            graph,
            StoryGraphLayoutOptions(),
            []()
            {
                return true;
            });
    passed &= check(
        cancelled.wasCancelled() &&
        cancelled.nodeRectangles.isEmpty() &&
        cancelled.nodePlacements.isEmpty() &&
        cancelled.edgePlacements.isEmpty(),
        "cancelled layout publishes no partial geometry");
    return passed;
}

bool testControlFlowAndSemanticAnalysis()
{
    const QString source =
        QStringLiteral(
            "if getvar(\"flag\") == 1 then\n"
            "  say(\"yes\")\n"
            "else\n"
            "  choose(\"M\", \"A\", \"B\", \"answer\")\n"
            "end\n"
            "::again::\n"
            "add(\"score\", 1)\n"
            "while getvar(\"loop\") < 3 do\n"
            "  sub(\"loop\", 1)\n"
            "  if getvar(\"stop\") == 1 then\n"
            "    break\n"
            "  end\n"
            "end\n"
            "goto again\n"
            "return\n");
    const StoryGraphDocumentResult result =
        analyzeSource(source);

    bool passed = true;
    passed &= check(result.hasUsableGraph(),
                    "valid source produces usable graphs");
    passed &= check(
        nodeKindCount(
            result.controlFlowGraph,
            StoryGraphNodeKind::Condition) >= 2 &&
        nodeKindCount(
            result.controlFlowGraph,
            StoryGraphNodeKind::LoopHeader) >= 1 &&
        nodeKindCount(
            result.controlFlowGraph,
            StoryGraphNodeKind::Label) == 1 &&
        nodeKindCount(
            result.controlFlowGraph,
            StoryGraphNodeKind::Goto) == 1 &&
        nodeKindCount(
            result.controlFlowGraph,
            StoryGraphNodeKind::Return) == 1,
        "CFG contains conditions, loop, label, goto, and return");
    passed &= check(
        hasEdgeKind(
            result.controlFlowGraph,
            StoryGraphEdgeKind::TrueBranch) &&
        hasEdgeKind(
            result.controlFlowGraph,
            StoryGraphEdgeKind::FalseBranch) &&
        hasEdgeKind(
            result.controlFlowGraph,
            StoryGraphEdgeKind::LoopBack) &&
        hasEdgeKind(
            result.controlFlowGraph,
            StoryGraphEdgeKind::Break) &&
        hasEdgeKind(
            result.controlFlowGraph,
            StoryGraphEdgeKind::Goto),
        "CFG exposes independent control edge kinds");
    passed &= check(
        nodeKindCount(
            result.semanticGraph,
            StoryGraphNodeKind::VariableRead) >= 3 &&
        nodeKindCount(
            result.semanticGraph,
            StoryGraphNodeKind::VariableWrite) >= 2 &&
        nodeKindCount(
            result.semanticGraph,
            StoryGraphNodeKind::Dialogue) == 1 &&
        nodeKindCount(
            result.semanticGraph,
            StoryGraphNodeKind::Choice) == 1,
        "semantic graph extracts dialogue, choice, and variables");
    return passed;
}

bool testGotoLabelBlockVisibility()
{
    bool passed = true;

    const StoryGraphDocumentResult ancestor =
        analyzeSource(
            QStringLiteral(
                "::outer::\n"
                "do\n"
                "  goto outer\n"
                "end\n"));
    const StoryGraphNode* ancestorLabel =
        findControlNodeAtLine(
            ancestor.controlFlowGraph,
            StoryGraphNodeKind::Label,
            1);
    const StoryGraphNode* innerGoto =
        findControlNodeAtLine(
            ancestor.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            3);
    const StoryGraphEdge* ancestorEdge =
        ancestorLabel != nullptr &&
            innerGoto != nullptr
        ? findDirectedEdge(
              ancestor.controlFlowGraph,
              innerGoto->id,
              ancestorLabel->id,
              StoryGraphEdgeKind::Goto)
        : nullptr;
    passed &= check(
        ancestorEdge != nullptr &&
            ancestorEdge->certainty ==
                StoryGraphCertainty::Certain &&
            innerGoto->certainty ==
                StoryGraphCertainty::Certain &&
            warningCodeCount(
                ancestor,
                QStringLiteral(
                    "story_graph.control.unresolved_goto")) ==
                0,
        "goto in an inner block resolves to a visible ancestor label");

    const StoryGraphDocumentResult hiddenChild =
        analyzeSource(
            QStringLiteral(
                "goto hidden\n"
                "do\n"
                "  ::hidden::\n"
                "end\n"
                "say(\"after\")\n"));
    const StoryGraphNode* outerGoto =
        findControlNodeAtLine(
            hiddenChild.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            1);
    const StoryGraphEdge* hiddenUnknown =
        outerGoto != nullptr
        ? findOutgoingEdge(
              hiddenChild.controlFlowGraph,
              outerGoto->id,
              StoryGraphEdgeKind::Unknown)
        : nullptr;
    passed &= check(
        outerGoto != nullptr &&
            outerGoto->certainty ==
                StoryGraphCertainty::Warning &&
            hiddenUnknown != nullptr &&
            hiddenUnknown->certainty ==
                StoryGraphCertainty::Warning &&
            findOutgoingEdge(
                hiddenChild.controlFlowGraph,
                outerGoto->id,
                StoryGraphEdgeKind::Goto) ==
                nullptr &&
            warningCodeCount(
                hiddenChild,
                QStringLiteral(
                    "story_graph.control.unresolved_goto")) ==
                1,
        "goto cannot resolve to a label in a child block");

    const StoryGraphDocumentResult siblings =
        analyzeSource(
            QStringLiteral(
                "do\n"
                "  ::shared::\n"
                "  goto shared\n"
                "end\n"
                "do\n"
                "  ::shared::\n"
                "  goto shared\n"
                "end\n"));
    const StoryGraphNode* firstSiblingLabel =
        findControlNodeAtLine(
            siblings.controlFlowGraph,
            StoryGraphNodeKind::Label,
            2);
    const StoryGraphNode* firstSiblingGoto =
        findControlNodeAtLine(
            siblings.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            3);
    const StoryGraphNode* secondSiblingLabel =
        findControlNodeAtLine(
            siblings.controlFlowGraph,
            StoryGraphNodeKind::Label,
            6);
    const StoryGraphNode* secondSiblingGoto =
        findControlNodeAtLine(
            siblings.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            7);
    passed &= check(
        firstSiblingLabel != nullptr &&
            firstSiblingGoto != nullptr &&
            secondSiblingLabel != nullptr &&
            secondSiblingGoto != nullptr &&
            findDirectedEdge(
                siblings.controlFlowGraph,
                firstSiblingGoto->id,
                firstSiblingLabel->id,
                StoryGraphEdgeKind::Goto) != nullptr &&
            findDirectedEdge(
                siblings.controlFlowGraph,
                secondSiblingGoto->id,
                secondSiblingLabel->id,
                StoryGraphEdgeKind::Goto) != nullptr &&
            warningCodeCount(
                siblings,
                QStringLiteral(
                    "story_graph.control.duplicate_label")) ==
                0,
        "same-name labels in sibling blocks remain independent");

    const StoryGraphDocumentResult duplicate =
        analyzeSource(
            QStringLiteral(
                "::duplicate::\n"
                "::duplicate::\n"
                "say(\"after\")\n"));
    const StoryGraphNode* duplicateLabel =
        findControlNodeAtLine(
            duplicate.controlFlowGraph,
            StoryGraphNodeKind::Label,
            2);
    passed &= check(
        duplicateLabel != nullptr &&
            duplicateLabel->certainty ==
                StoryGraphCertainty::Warning &&
            warningCodeCount(
                duplicate,
                QStringLiteral(
                    "story_graph.control.duplicate_label")) ==
                1,
        "same-name labels in one block retain a stable duplicate warning");

    const StoryGraphDocumentResult isolated =
        analyzeSource(
            QStringLiteral(
                "::outside::\n"
                "function first()\n"
                "  goto outside\n"
                "end\n"
                "function second()\n"
                "  ::inside::\n"
                "end\n"
                "goto inside\n"));
    const StoryGraphNode* functionGoto =
        findControlNodeAtLine(
            isolated.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            3);
    const StoryGraphNode* chunkGoto =
        findControlNodeAtLine(
            isolated.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            8);
    passed &= check(
        functionGoto != nullptr &&
            chunkGoto != nullptr &&
            functionGoto->certainty ==
                StoryGraphCertainty::Warning &&
            chunkGoto->certainty ==
                StoryGraphCertainty::Warning &&
            findOutgoingEdge(
                isolated.controlFlowGraph,
                functionGoto->id,
                StoryGraphEdgeKind::Unknown) !=
                nullptr &&
            findOutgoingEdge(
                isolated.controlFlowGraph,
                chunkGoto->id,
                StoryGraphEdgeKind::Unknown) !=
                nullptr &&
            warningCodeCount(
                isolated,
                QStringLiteral(
                    "story_graph.control.unresolved_goto")) ==
                2,
        "labels never cross nested-function boundaries");

    const StoryGraphDocumentResult sameNameAcrossFunction =
        analyzeSource(
            QStringLiteral(
                "::shared::\n"
                "function isolated()\n"
                "  ::shared::\n"
                "  goto shared\n"
                "end\n"
                "goto shared\n"));
    const StoryGraphNode* chunkLabel =
        findControlNodeAtLine(
            sameNameAcrossFunction.controlFlowGraph,
            StoryGraphNodeKind::Label,
            1);
    const StoryGraphNode* functionLabel =
        findControlNodeAtLine(
            sameNameAcrossFunction.controlFlowGraph,
            StoryGraphNodeKind::Label,
            3);
    const StoryGraphNode* sameNameFunctionGoto =
        findControlNodeAtLine(
            sameNameAcrossFunction.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            4);
    const StoryGraphNode* sameNameChunkGoto =
        findControlNodeAtLine(
            sameNameAcrossFunction.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            6);
    passed &= check(
        chunkLabel != nullptr &&
            functionLabel != nullptr &&
            sameNameFunctionGoto != nullptr &&
            sameNameChunkGoto != nullptr &&
            findDirectedEdge(
                sameNameAcrossFunction.controlFlowGraph,
                sameNameFunctionGoto->id,
                functionLabel->id,
                StoryGraphEdgeKind::Goto) != nullptr &&
            findDirectedEdge(
                sameNameAcrossFunction.controlFlowGraph,
                sameNameChunkGoto->id,
                chunkLabel->id,
                StoryGraphEdgeKind::Goto) != nullptr &&
            warningCodeCount(
                sameNameAcrossFunction,
                QStringLiteral(
                    "story_graph.control.duplicate_label")) ==
                0,
        "same-name labels resolve independently across function boundaries");
    return passed;
}

bool testGotoLocalScopeRestrictions()
{
    bool passed = true;

    const StoryGraphDocumentResult skippedLocal =
        analyzeSource(
            QStringLiteral(
                "goto after_local\n"
                "local hidden = 1\n"
                "::after_local::\n"
                "say(\"after\")\n"));
    const StoryGraphNode* skippedLocalGoto =
        findControlNodeAtLine(
            skippedLocal.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            1);
    const StoryGraphEdge* skippedLocalUnknown =
        skippedLocalGoto != nullptr
        ? findOutgoingEdge(
              skippedLocal.controlFlowGraph,
              skippedLocalGoto->id,
              StoryGraphEdgeKind::Unknown)
        : nullptr;
    passed &= check(
        skippedLocalGoto != nullptr &&
            skippedLocalGoto->certainty ==
                StoryGraphCertainty::Warning &&
            skippedLocalUnknown != nullptr &&
            skippedLocalUnknown->certainty ==
                StoryGraphCertainty::Warning &&
            findOutgoingEdge(
                skippedLocal.controlFlowGraph,
                skippedLocalGoto->id,
                StoryGraphEdgeKind::Goto) ==
                nullptr &&
            warningCodeCount(
                skippedLocal,
                QStringLiteral(
                    "story_graph.control.goto_into_local_scope")) ==
                1,
        "forward goto cannot skip a local declaration into its scope");

    const StoryGraphDocumentResult nestedLocal =
        analyzeSource(
            QStringLiteral(
                "goto after_nested\n"
                "do\n"
                "  local nested = 1\n"
                "end\n"
                "::after_nested::\n"
                "say(\"after\")\n"));
    const StoryGraphNode* nestedLocalGoto =
        findControlNodeAtLine(
            nestedLocal.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            1);
    const StoryGraphNode* nestedLocalLabel =
        findControlNodeAtLine(
            nestedLocal.controlFlowGraph,
            StoryGraphNodeKind::Label,
            5);
    const StoryGraphEdge* nestedLocalEdge =
        nestedLocalGoto != nullptr &&
            nestedLocalLabel != nullptr
        ? findDirectedEdge(
              nestedLocal.controlFlowGraph,
              nestedLocalGoto->id,
              nestedLocalLabel->id,
              StoryGraphEdgeKind::Goto)
        : nullptr;
    passed &= check(
        nestedLocalEdge != nullptr &&
            nestedLocalEdge->certainty ==
                StoryGraphCertainty::Certain &&
            nestedLocalGoto->certainty ==
                StoryGraphCertainty::Certain &&
            warningCodeCount(
                nestedLocal,
                QStringLiteral(
                    "story_graph.control.goto_into_local_scope")) ==
                0,
        "a nested-block local does not expand into the enclosing block");

    const StoryGraphDocumentResult backward =
        analyzeSource(
            QStringLiteral(
                "::again::\n"
                "local current = 1\n"
                "goto again\n"));
    const StoryGraphNode* backwardLabel =
        findControlNodeAtLine(
            backward.controlFlowGraph,
            StoryGraphNodeKind::Label,
            1);
    const StoryGraphNode* backwardGoto =
        findControlNodeAtLine(
            backward.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            3);
    const StoryGraphEdge* backwardEdge =
        backwardLabel != nullptr &&
            backwardGoto != nullptr
        ? findDirectedEdge(
              backward.controlFlowGraph,
              backwardGoto->id,
              backwardLabel->id,
              StoryGraphEdgeKind::Goto)
        : nullptr;
    passed &= check(
        backwardEdge != nullptr &&
            backwardEdge->certainty ==
                StoryGraphCertainty::Certain &&
            warningCodeCount(
                backward,
                QStringLiteral(
                    "story_graph.control.goto_into_local_scope")) ==
                0,
        "backward goto may leave a local scope and re-enter before its declaration");

    const StoryGraphDocumentResult terminalLabel =
        analyzeSource(
            QStringLiteral(
                "goto finished\n"
                "local skipped = 1\n"
                "::finished::\n"));
    const StoryGraphNode* terminalGoto =
        findControlNodeAtLine(
            terminalLabel.controlFlowGraph,
            StoryGraphNodeKind::Goto,
            1);
    const StoryGraphNode* terminalTarget =
        findControlNodeAtLine(
            terminalLabel.controlFlowGraph,
            StoryGraphNodeKind::Label,
            3);
    const StoryGraphEdge* terminalEdge =
        terminalGoto != nullptr &&
            terminalTarget != nullptr
        ? findDirectedEdge(
              terminalLabel.controlFlowGraph,
              terminalGoto->id,
              terminalTarget->id,
              StoryGraphEdgeKind::Goto)
        : nullptr;
    passed &= check(
        terminalEdge != nullptr &&
            terminalEdge->certainty ==
                StoryGraphCertainty::Certain &&
            warningCodeCount(
                terminalLabel,
                QStringLiteral(
                    "story_graph.control.goto_into_local_scope")) ==
                0,
        "a terminal label uses the block-entry local scope");
    return passed;
}

bool testPartialRecoveryAndAnalyzerCancellation()
{
    const StoryGraphDocumentResult partial =
        analyzeSource(
            QStringLiteral(
                "if true then\n"
                "  say(\"still visible\")\n"));
    bool sawMissingEnd = false;
    for (const StoryGraphWarning& warning :
         partial.warnings)
    {
        sawMissingEnd |=
            warning.diagnosticCode ==
            QStringLiteral(
                "story_graph.parser.missing_end");
    }

    bool passed = true;
    passed &= check(
        partial.status ==
            StoryGraphDocumentStatus::Partial &&
        partial.hasUsableGraph() &&
        sawMissingEnd &&
        nodeKindCount(
            partial.semanticGraph,
            StoryGraphNodeKind::Dialogue) == 1,
        "missing end reports a stable warning and retains usable semantics");

    StoryGraphAnalysisRequest request;
    request.source.identity.portableRootKey =
        QStringLiteral("active:test:0");
    request.source.identity.virtualPath =
        QStringLiteral(
            "script/map/test/cancel.txt");
    request.source.utf8Bytes =
        QByteArray("say(\"cancel\")\n");
    request.analysisGeneration = 23;
    int callbackCount = 0;
    const StoryGraphDocumentResult cancelled =
        StoryGraphAnalyzer::analyze(
            request,
            [&callbackCount]()
            {
                ++callbackCount;
                return callbackCount >= 2;
            });
    passed &= check(
        callbackCount >= 2 &&
        cancelled.status ==
            StoryGraphDocumentStatus::Cancelled &&
        !cancelled.controlFlowGraph.complete &&
        !cancelled.semanticGraph.complete,
        "parser cancellation is explicit and marks both graphs incomplete");
    return passed;
}

bool testExactCallsDynamicBoundariesAndStableEditIds()
{
    const QString source =
        QStringLiteral(
            "runscript(\"next.lua\")\n"
            "RunScript(\"wrong.lua\")\n"
            "local runscript = helper\n"
            "runscript(\"dynamic.lua\")\n"
            "api.runscript(\"qualified.lua\")\n"
            "runparallelscript([=[parallel.lua]=], 25)\n"
            "playsound(\"effect/test.wav\")\n"
            "say(\"#name\", \"正文\", 2, 0)\n"
            "choosemultiple(2, 2, \"pick\", \"M\", "
            "\"A{Flag==1}\", \"B\")\n");
    const StoryGraphDocumentResult result =
        analyzeSource(source);
    const StoryGraphNode* serial =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("runscript"),
            QStringLiteral("next.lua"));
    const StoryGraphNode* dynamic =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("runscript"),
            QStringLiteral("dynamic.lua"));
    const StoryGraphNode* parallel =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("runparallelscript"),
            QStringLiteral("parallel.lua"));
    const StoryGraphNode* legacySay =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("say"));
    const StoryGraphNode* registeredCall =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("playsound"));

    bool passed = true;
    passed &= check(serial != nullptr &&
                    serial->kind ==
                        StoryGraphNodeKind::SerialScriptCall &&
                    serial->certainty ==
                        StoryGraphCertainty::Certain,
                    "exact lowercase literal runscript is certain");
    passed &= check(dynamic != nullptr &&
                    dynamic->kind ==
                        StoryGraphNodeKind::DynamicCall,
                    "locally shadowed runtime API remains dynamic");
    passed &= check(parallel != nullptr &&
                    parallel->kind ==
                        StoryGraphNodeKind::ParallelScriptCall &&
                    parallel->certainty ==
                        StoryGraphCertainty::Certain,
                    "long-string parallel target is certain and distinct");
    passed &= check(legacySay != nullptr &&
                    legacySay->certainty ==
                        StoryGraphCertainty::Warning,
                    "legacy say signature is visible but not definite");
    passed &= check(registeredCall != nullptr &&
                    registeredCall->kind ==
                        StoryGraphNodeKind::RegisteredApiCall &&
                    registeredCall->certainty ==
                        StoryGraphCertainty::Certain,
                    "ordinary exact runtime API call is registered, not unknown");

    bool sawCaseMismatch = false;
    bool sawChoiceCondition = false;
    for (const StoryGraphWarning& warning :
         result.warnings)
    {
        sawCaseMismatch |=
            warning.diagnosticCode ==
            QStringLiteral(
                "story_graph.semantic.api_case_mismatch");
        sawChoiceCondition |=
            warning.diagnosticCode ==
            QStringLiteral(
                "story_graph.semantic.choice_condition_unexpanded");
    }
    passed &= check(sawCaseMismatch &&
                    sawChoiceCondition,
                    "case mismatch and unexpanded option condition are explicit");

    const StoryGraphDocumentResult original =
        analyzeSource(
            QStringLiteral("say(\"same\")\n"));
    const StoryGraphDocumentResult shifted =
        analyzeSource(
            QStringLiteral(
                "local unrelated = 1\n"
                "say(\"same\")\n"));
    const StoryGraphNode* originalSay =
        findSemanticCall(
            original.semanticGraph,
            QStringLiteral("say"));
    const StoryGraphNode* shiftedSay =
        findSemanticCall(
            shifted.semanticGraph,
            QStringLiteral("say"));
    passed &= check(originalSay != nullptr &&
                    shiftedSay != nullptr &&
                    originalSay->id == shiftedSay->id &&
                    originalSay->sourceRange.start.line !=
                        shiftedSay->sourceRange.start.line,
                    "unrelated leading edit moves range without changing semantic ID");
    return passed;
}

bool testChooseMultipleIndexedVariableWrites()
{
    const StoryGraphDocumentResult literal =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, 3, \" \\tPick \\r\", "
                "\"M\", \"A\", \"B\", \"C\")\n"));
    const StoryGraphNode* choice =
        findSemanticCall(
            literal.semanticGraph,
            QStringLiteral("choosemultiple"));
    const StoryGraphNode* firstWrite =
        findVariableNode(
            literal.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$Pick0"),
            1);
    const StoryGraphNode* secondWrite =
        findVariableNode(
            literal.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$Pick1"),
            1);
    const StoryGraphNode* thirdWrite =
        findVariableNode(
            literal.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$Pick2"),
            1);

    bool passed = true;
    passed &= check(
        choice != nullptr &&
            choice->kind ==
                StoryGraphNodeKind::Choice &&
            choice->certainty ==
                StoryGraphCertainty::Certain &&
            choice->variableName.isEmpty() &&
            firstWrite != nullptr &&
            secondWrite != nullptr &&
            thirdWrite != nullptr &&
            firstWrite->certainty ==
                StoryGraphCertainty::Dynamic &&
            secondWrite->certainty ==
                StoryGraphCertainty::Dynamic &&
            thirdWrite->certainty ==
                StoryGraphCertainty::Dynamic &&
            variableNodeCount(
                literal.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                3 &&
            findVariableNode(
                literal.semanticGraph,
                StoryGraphNodeKind::VariableWrite,
                QStringLiteral("Pick")) ==
                nullptr,
        "choosemultiple expands only ASCII-trimmed dynamic result variables");

    const StoryGraphDocumentResult dynamicCount =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, count, \"pick\", "
                "\"M\", \"A\")\n"));
    const StoryGraphNode* dynamicChoice =
        findSemanticCall(
            dynamicCount.semanticGraph,
            QStringLiteral("choosemultiple"));
    passed &= check(
        dynamicChoice != nullptr &&
            dynamicChoice->certainty ==
                StoryGraphCertainty::Dynamic &&
            variableNodeCount(
                dynamicCount.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                0 &&
            warningCodeCount(
                dynamicCount,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_dynamic_output_count")) ==
                1,
        "dynamic choosemultiple count stays dynamic without fabricated writes");

    const StoryGraphDocumentResult invalidCount =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, -1, \"pick\", "
                "\"M\", \"A\")\n"));
    const StoryGraphNode* invalidChoice =
        findSemanticCall(
            invalidCount.semanticGraph,
            QStringLiteral("choosemultiple"));
    passed &= check(
        invalidChoice != nullptr &&
            invalidChoice->certainty ==
                StoryGraphCertainty::Warning &&
            variableNodeCount(
                invalidCount.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                0 &&
            warningCodeCount(
                invalidCount,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_invalid_output_count")) ==
                1,
        "invalid choosemultiple count produces a warning and no writes");

    const StoryGraphDocumentResult fractionalCount =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, 2.5, \"pick\", "
                "\"M\", \"A\")\n"));
    passed &= check(
        variableNodeCount(
            fractionalCount.semanticGraph,
            StoryGraphNodeKind::VariableWrite) ==
                0 &&
            warningCodeCount(
                fractionalCount,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_invalid_output_count")) ==
                1,
        "non-integral choosemultiple literal is rejected conservatively");

    const StoryGraphDocumentResult excessiveCount =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, 33, \"pick\", "
                "\"M\", \"A\")\n"));
    const StoryGraphNode* excessiveChoice =
        findSemanticCall(
            excessiveCount.semanticGraph,
            QStringLiteral("choosemultiple"));
    passed &= check(
        excessiveChoice != nullptr &&
            excessiveChoice->certainty ==
                StoryGraphCertainty::Warning &&
            variableNodeCount(
                excessiveCount.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                0 &&
            warningCodeCount(
                excessiveCount,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_output_limit")) ==
                1,
        "choosemultiple expansion has an explicit 32-variable upper bound");

    const StoryGraphDocumentResult dynamicBase =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, 2, base, "
                "\"M\", \"A\")\n"));
    passed &= check(
        variableNodeCount(
            dynamicBase.semanticGraph,
            StoryGraphNodeKind::VariableWrite) ==
                0 &&
            warningCodeCount(
                dynamicBase,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_dynamic_output_base")) ==
                1,
        "dynamic choosemultiple base does not create definite result names");

    const StoryGraphDocumentResult emptyBase =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, 2, \" \\t\\r\", "
                "\"M\", \"A\", \"B\")\n"));
    const StoryGraphNode* emptyBaseFirst =
        findVariableNode(
            emptyBase.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$0"),
            1);
    const StoryGraphNode* emptyBaseSecond =
        findVariableNode(
            emptyBase.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$1"),
            1);
    passed &= check(
        emptyBaseFirst != nullptr &&
            emptyBaseSecond != nullptr &&
            emptyBaseFirst->certainty ==
                StoryGraphCertainty::Dynamic &&
            emptyBaseSecond->certainty ==
                StoryGraphCertainty::Dynamic &&
            variableNodeCount(
                emptyBase.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                2 &&
            warningCodeCount(
                emptyBase,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_invalid_output_base")) ==
                0,
        "ASCII-empty choosemultiple base still maps to runtime names $0 and $1");

    const StoryGraphDocumentResult excessiveSelection =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, 3, \"few\", "
                "\"M\", \"A\", \"B\")\n"));
    const StoryGraphNode* excessiveSelectionChoice =
        findSemanticCall(
            excessiveSelection.semanticGraph,
            QStringLiteral("choosemultiple"));
    const StoryGraphNode* possibleFirstWrite =
        findVariableNode(
            excessiveSelection.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$few0"),
            1);
    const StoryGraphNode* possibleSecondWrite =
        findVariableNode(
            excessiveSelection.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("$few1"),
            1);
    passed &= check(
        excessiveSelectionChoice != nullptr &&
            excessiveSelectionChoice->certainty ==
                StoryGraphCertainty::Warning &&
            possibleFirstWrite != nullptr &&
            possibleSecondWrite != nullptr &&
            possibleFirstWrite->certainty ==
                StoryGraphCertainty::Dynamic &&
            possibleSecondWrite->certainty ==
                StoryGraphCertainty::Dynamic &&
            findVariableNode(
                excessiveSelection.semanticGraph,
                StoryGraphNodeKind::VariableWrite,
                QStringLiteral("$few2")) ==
                nullptr &&
            variableNodeCount(
                excessiveSelection.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                2 &&
            warningCodeCount(
                excessiveSelection,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_selection_count_exceeds_options")) ==
                1,
        "selection count above option slots retains only possible dynamic automation writes");

    const StoryGraphDocumentResult unaryPlusCount =
        analyzeSource(
            QStringLiteral(
                "choosemultiple(2, +2, \"plus\", "
                "\"M\", \"A\", \"B\")\n"));
    const StoryGraphNode* unaryPlusChoice =
        findSemanticCall(
            unaryPlusCount.semanticGraph,
            QStringLiteral("choosemultiple"));
    passed &= check(
        unaryPlusChoice != nullptr &&
            unaryPlusChoice->certainty ==
                StoryGraphCertainty::Warning &&
            variableNodeCount(
                unaryPlusCount.semanticGraph,
                StoryGraphNodeKind::VariableWrite) ==
                0 &&
            warningCodeCount(
                unaryPlusCount,
                QStringLiteral(
                    "story_graph.semantic.choice_multiple_invalid_output_count")) ==
                1,
        "Lua 5.5 rejects unary plus selection counts without fabricated writes");
    return passed;
}

bool testComputedCalleeBoundaries()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "local alias = runscript\n"
                "api.runscript(\"field.lua\")\n"
                "alias(\"alias.lua\");\n"
                "(runscript)(\"parenthesized.lua\")\n"
                "handlers[key](\"indexed.lua\")\n"
                "factory()(\"returned.lua\")\n"));
    const StoryGraphNode* fieldCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            2);
    const StoryGraphNode* aliasCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            3);
    const StoryGraphNode* parenthesizedCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            4);
    const StoryGraphNode* indexedCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            5);
    const StoryGraphNode* returnedCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            6);
    const StoryGraphNode* factoryCall =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("factory"));

    bool passed = true;
    passed &= check(
        fieldCall != nullptr &&
            fieldCall->apiName ==
                QStringLiteral("runscript") &&
            fieldCall->certainty ==
                StoryGraphCertainty::Dynamic &&
            aliasCall != nullptr &&
            aliasCall->apiName ==
                QStringLiteral("alias") &&
            aliasCall->certainty ==
                StoryGraphCertainty::Dynamic &&
            warningCodeCount(
                result,
                QStringLiteral(
                    "story_graph.semantic.api_not_direct_global")) ==
                1 &&
            warningCodeCount(
                result,
                QStringLiteral(
                    "story_graph.semantic.dynamic_callee")) ==
                1,
        "field and local-alias calls stay dynamic with stable warnings");
    passed &= check(
        parenthesizedCall != nullptr &&
            parenthesizedCall->apiName.isEmpty() &&
            parenthesizedCall->certainty ==
                StoryGraphCertainty::Dynamic,
        "parenthesized callee retains an outer dynamic node");
    passed &= check(
        indexedCall != nullptr &&
            indexedCall->apiName.isEmpty() &&
            indexedCall->certainty ==
                StoryGraphCertainty::Dynamic,
        "indexed callee retains an outer dynamic node");
    passed &= check(
        returnedCall != nullptr &&
            returnedCall->apiName.isEmpty() &&
            returnedCall->certainty ==
                StoryGraphCertainty::Dynamic,
        "returned callee retains an outer dynamic node");
    passed &= check(
        warningCodeCount(
            result,
            QStringLiteral(
                "story_graph.semantic.computed_callee")) ==
            3,
        "each computed callee emits one stable warning");
    passed &= check(
        factoryCall != nullptr &&
            factoryCall->kind ==
                StoryGraphNodeKind::UnknownCall &&
            findSemanticCall(
                result.semanticGraph,
                QStringLiteral("runscript"),
                QStringLiteral("parenthesized.lua")) ==
                nullptr,
        "computed outer calls do not upgrade references to definite runtime APIs");
    return passed;
}

bool testLuaCallSugarAndArbitrarySuffixChains()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "runscript \"direct.lua\";\n"
                "api.runscript [=[field.lua]=];\n"
                "(runscript) \"parenthesized.lua\";\n"
                "(runscript) [=[parenthesized-long.lua]=];\n"
                "(runscript) {\"parenthesized-table.lua\"};\n"
                "handlers[key] \"indexed.lua\";\n"
                "handlers[key] [=[indexed-long.lua]=];\n"
                "handlers[key] {target = \"indexed-table.lua\"};\n"
                "factory() \"returned.lua\";\n"
                "factory() [=[returned-long.lua]=];\n"
                "factory() {target = \"returned-table.lua\"};\n"
                "factory()[key](\"returned-indexed.lua\");\n"
                "(factory())[key] [=[parenthesized-return-indexed.lua]=];\n"
                "handlers[key]()[next] \"chained.lua\";\n"
                "factory(){value}[key] \"table-chain.lua\";\n"));

    const StoryGraphNode* directCall =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("runscript"),
            QStringLiteral("direct.lua"));
    const StoryGraphNode* fieldCall =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("runscript"),
            QStringLiteral("field.lua"));

    bool passed = true;
    passed &= check(
        directCall != nullptr &&
            directCall->kind ==
                StoryGraphNodeKind::SerialScriptCall &&
            directCall->certainty ==
                StoryGraphCertainty::Certain &&
            directCall->sourceRange.start.line == 1,
        "literal-string call sugar preserves a certain direct runtime API call");
    passed &= check(
        fieldCall != nullptr &&
            fieldCall->kind ==
                StoryGraphNodeKind::DynamicCall &&
            fieldCall->certainty ==
                StoryGraphCertainty::Dynamic &&
            fieldCall->sourceRange.start.line == 2 &&
            warningCodeCountAtLine(
                result,
                QStringLiteral(
                    "story_graph.semantic.api_not_direct_global"),
                2) ==
                1,
        "long-string call sugar preserves a dynamic field call");

    for (int line = 3; line <= 13; ++line)
    {
        const int dynamicCount =
            nodeKindCountAtLine(
                result.semanticGraph,
                StoryGraphNodeKind::DynamicCall,
                line);
        const int computedWarningCount =
            warningCodeCountAtLine(
                result,
                QStringLiteral(
                    "story_graph.semantic.computed_callee"),
                line);
        if (dynamicCount != 1 ||
            computedWarningCount != 1)
        {
            std::cerr
                << "call sugar line "
                << line
                << ": dynamic="
                << dynamicCount
                << ", warnings="
                << computedWarningCount
                << '\n';
        }
        passed &= check(
            dynamicCount == 1 &&
            computedWarningCount == 1 &&
            nodeKindCountAtLine(
                result.semanticGraph,
                StoryGraphNodeKind::SerialScriptCall,
                line) ==
                0,
        "parenthesized, indexed, and returned call sugar remains one computed call");
    }
    for (int line : {14, 15})
    {
        passed &= check(
            nodeKindCountAtLine(
                result.semanticGraph,
                StoryGraphNodeKind::DynamicCall,
                line) ==
                2 &&
            warningCodeCountAtLine(
                result,
                QStringLiteral(
                    "story_graph.semantic.computed_callee"),
                line) ==
                2,
            "arbitrary indexed, returned, and table suffix chain emits each computed call");
    }
    passed &= check(
        warningCodeCount(
            result,
            QStringLiteral(
                "story_graph.semantic.computed_callee")) ==
            15,
        "each valid computed call-sugar suffix has one stable warning");
    return passed;
}

bool testOrdinaryLuaVariableExtraction()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "local first, second = source, object.field\n"
                "global result = first + helper(second)\n"
                "plain = result + tableValue\n"
                "record.field = plain + indexValue\n"
                "array[key] = result\n"
                "local alias = runscript\n"));

    bool passed = true;
    for (const auto& expectedWrite :
         {
             qMakePair(QStringLiteral("first"), 1),
             qMakePair(QStringLiteral("second"), 1),
             qMakePair(QStringLiteral("result"), 2),
             qMakePair(QStringLiteral("plain"), 3),
             qMakePair(QStringLiteral("alias"), 6)
         })
    {
        passed &= check(
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableWrite,
                expectedWrite.first,
                expectedWrite.second) !=
                nullptr,
            "simple local and global assignment targets become writes");
    }
    passed &= check(
        variableNodeCount(
            result.semanticGraph,
            StoryGraphNodeKind::VariableWrite) ==
            5,
        "complex assignment targets do not become definite writes");

    for (const auto& expectedRead :
         {
             qMakePair(QStringLiteral("source"), 1),
             qMakePair(QStringLiteral("object"), 1),
             qMakePair(QStringLiteral("first"), 2),
             qMakePair(QStringLiteral("second"), 2),
             qMakePair(QStringLiteral("result"), 3),
             qMakePair(QStringLiteral("tableValue"), 3),
             qMakePair(QStringLiteral("plain"), 4),
             qMakePair(QStringLiteral("indexValue"), 4),
             qMakePair(QStringLiteral("result"), 5)
         })
    {
        passed &= check(
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableRead,
                expectedRead.first,
                expectedRead.second) !=
                nullptr,
            "RHS identifiers become exact ordinary variable reads");
    }
    passed &= check(
        variableNodeCount(
            result.semanticGraph,
            StoryGraphNodeKind::VariableRead) ==
                9 &&
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableRead,
                QStringLiteral("field")) ==
                nullptr &&
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableRead,
                QStringLiteral("helper")) ==
                nullptr &&
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableRead,
                QStringLiteral("runscript")) ==
                nullptr &&
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableRead,
                QStringLiteral("key")) ==
                nullptr &&
            warningCodeCount(
                result,
                QStringLiteral(
                    "story_graph.semantic.complex_assignment_target")) ==
                2,
        "field names, callees, API names, declarations, and complex LHS tokens are excluded");
    return passed;
}

bool testLua55DeclarationAttributesAndContextualGlobal()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "global = source\n"
                "target = global\n"
                "local<const> runscript = helper\n"
                "runscript(\"local-prefix.lua\")\n"
                "do\n"
                "  local playsound<const> = helper\n"
                "  playsound(\"local-postfix.wav\")\n"
                "  local resource<close> = closeable\n"
                "end\n"
                "playsound(\"outside.wav\")\n"
                "global choose = helper\n"
                "choose(\"M\", \"A\", \"B\", \"answer\")\n"
                "global<const> loadmap = helper\n"
                "loadmap(\"map01\")\n"
                "global add<const>, sub = helperA, helperB\n"
                "add(\"score\", 1)\n"
                "sub(\"score\", 1)\n"));

    bool passed = true;
    for (const auto& expectedWrite :
         {
             qMakePair(QStringLiteral("global"), 1),
             qMakePair(QStringLiteral("target"), 2),
             qMakePair(QStringLiteral("runscript"), 3),
             qMakePair(QStringLiteral("playsound"), 6),
             qMakePair(QStringLiteral("resource"), 8),
             qMakePair(QStringLiteral("choose"), 11),
             qMakePair(QStringLiteral("loadmap"), 13),
             qMakePair(QStringLiteral("add"), 15),
             qMakePair(QStringLiteral("sub"), 15)
         })
    {
        passed &= check(
            findVariableNode(
                result.semanticGraph,
                StoryGraphNodeKind::VariableWrite,
                expectedWrite.first,
                expectedWrite.second) !=
                nullptr,
            "Lua 5.5 declaration names become writes without attribute tokens");
    }
    passed &= check(
        findVariableNode(
            result.semanticGraph,
            StoryGraphNodeKind::VariableRead,
            QStringLiteral("source"),
            1) !=
            nullptr &&
        findVariableNode(
            result.semanticGraph,
            StoryGraphNodeKind::VariableRead,
            QStringLiteral("global"),
            2) !=
            nullptr &&
        findVariableNode(
            result.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("const")) ==
            nullptr &&
        findVariableNode(
            result.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("close")) ==
            nullptr,
        "contextual global is an ordinary variable outside declarations and attributes are not variables");

    const StoryGraphNode* localPrefixCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            4);
    const StoryGraphNode* localPostfixCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            7);
    const StoryGraphNode* outsideLocalCall =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::RegisteredApiCall,
            10);
    passed &= check(
        localPrefixCall != nullptr &&
            localPrefixCall->apiName ==
                QStringLiteral("runscript") &&
            localPostfixCall != nullptr &&
            localPostfixCall->apiName ==
                QStringLiteral("playsound") &&
            outsideLocalCall != nullptr &&
            outsideLocalCall->apiName ==
                QStringLiteral("playsound") &&
            outsideLocalCall->kind ==
                StoryGraphNodeKind::RegisteredApiCall &&
            outsideLocalCall->certainty ==
                StoryGraphCertainty::Certain,
        "prefixed and postfixed local attributes shadow APIs only in lexical scope");

    for (const auto& expectedDynamicCall :
         {
             qMakePair(QStringLiteral("choose"), 12),
             qMakePair(QStringLiteral("loadmap"), 14),
             qMakePair(QStringLiteral("add"), 16),
             qMakePair(QStringLiteral("sub"), 17)
         })
    {
        const StoryGraphNode* call =
            findControlNodeAtLine(
                result.semanticGraph,
                StoryGraphNodeKind::DynamicCall,
                expectedDynamicCall.second);
        passed &= check(
            call != nullptr &&
                call->apiName ==
                    expectedDynamicCall.first &&
                call->certainty ==
                    StoryGraphCertainty::Dynamic,
            "initialized global declarations invalidate direct runtime APIs");
    }
    passed &= check(
        warningCodeCount(
            result,
            QStringLiteral(
                "story_graph.semantic.api_not_direct_global")) ==
            6,
        "local and global declaration shadowing emits one warning per affected call");
    return passed;
}

bool testSemanticPostOrderAndCfgProjection()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "say(getvar(\"dialogue\"))\n"
                "if flag then\n"
                "  choose(\"M\", \"A\", \"B\", \"choice\")\n"
                "else\n"
                "  add(\"score\", 1)\n"
                "end\n"
                "function dormant()\n"
                "  sub(\"dormant\", 1)\n"
                "end\n"
                "local anonymous = function()\n"
                "  do\n"
                "    local nested = true\n"
                "  end\n"
                "  usemagic(\"anonymous\")\n"
                "end\n"
                "assign(\"after\", 1)\n"));
    const StoryGraphNode* getvar =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("getvar"));
    const StoryGraphNode* say =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("say"));
    const StoryGraphNode* choose =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("choose"));
    const StoryGraphNode* add =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("add"));
    const StoryGraphNode* dormant =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("sub"));
    const StoryGraphNode* after =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("assign"));
    const StoryGraphNode* anonymous =
        findSemanticCall(
            result.semanticGraph,
            QStringLiteral("usemagic"));
    const StoryGraphNode* anonymousAssignment =
        findVariableNode(
            result.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("anonymous"),
            10);

    bool passed = true;
    passed &= check(
        result.status ==
                StoryGraphDocumentStatus::Complete &&
            result.controlFlowGraph.complete &&
            result.semanticGraph.complete,
        "complete analysis marks both graph projections complete");
    passed &= check(
        getvar != nullptr &&
            say != nullptr &&
            choose != nullptr &&
            add != nullptr &&
            dormant != nullptr &&
            after != nullptr &&
            anonymousAssignment != nullptr &&
            anonymous == nullptr,
        "projection fixture exposes calls and the executed local write");
    if (getvar == nullptr ||
        say == nullptr ||
        choose == nullptr ||
        add == nullptr ||
        dormant == nullptr ||
        after == nullptr ||
        anonymousAssignment == nullptr)
    {
        return false;
    }
    const StoryGraphEdge* chooseToAssignment =
        findDirectedEdge(
            result.semanticGraph,
            choose->id,
            anonymousAssignment->id,
            StoryGraphEdgeKind::Fallthrough);
    const StoryGraphEdge* addToAssignment =
        findDirectedEdge(
            result.semanticGraph,
            add->id,
            anonymousAssignment->id,
            StoryGraphEdgeKind::Fallthrough);
    const StoryGraphEdge* assignmentToAfter =
        findDirectedEdge(
            result.semanticGraph,
            anonymousAssignment->id,
            after->id,
            StoryGraphEdgeKind::Sequential);

    passed &= check(
        hasDirectedEdge(
            result.semanticGraph,
            result.semanticGraph.entryNodeId,
            getvar->id,
            StoryGraphEdgeKind::Sequential) &&
            hasDirectedEdge(
                result.semanticGraph,
                getvar->id,
                say->id,
                StoryGraphEdgeKind::Sequential),
        "nested calls are emitted post-order before their caller");
    passed &= check(
        hasDirectedEdge(
            result.semanticGraph,
            say->id,
            choose->id,
            StoryGraphEdgeKind::TrueBranch) &&
            hasDirectedEdge(
                result.semanticGraph,
                say->id,
                add->id,
                StoryGraphEdgeKind::FalseBranch),
        "semantic branch endpoints project exact CFG arms");
    passed &= check(
        chooseToAssignment != nullptr &&
            chooseToAssignment->certainty ==
                StoryGraphCertainty::Certain &&
            addToAssignment != nullptr &&
            addToAssignment->certainty ==
                StoryGraphCertainty::Certain &&
            assignmentToAfter != nullptr &&
            assignmentToAfter->certainty ==
                StoryGraphCertainty::Certain,
        "both semantic arms converge through the executed local write");
    passed &= check(
        !hasAnyDirectedEdge(
            result.semanticGraph,
            choose->id,
            add->id) &&
            !hasAnyDirectedEdge(
                result.semanticGraph,
                add->id,
                choose->id),
        "mutually exclusive semantic arms are not serialized");
    passed &= check(
        !hasAnyDirectedEdge(
            result.semanticGraph,
            say->id,
            dormant->id) &&
            !hasAnyDirectedEdge(
                result.semanticGraph,
                dormant->id,
                after->id) &&
            hasDirectedEdge(
                result.semanticGraph,
                after->id,
                result.semanticGraph.exitNodeId,
                StoryGraphEdgeKind::Fallthrough),
        "uncalled function body remains detached from chunk semantics");

    const StoryGraphDocumentResult anonymousArgument =
        analyzeSource(
            QStringLiteral(
                "helper(function()\n"
                "  local first, second = pair\n"
                "  usemagic(\"dormant\")\n"
                "end, getvar(\"outside\"))\n"));
    const StoryGraphNode* argumentRead =
        findSemanticCall(
            anonymousArgument.semanticGraph,
            QStringLiteral("getvar"));
    const StoryGraphNode* outerHelper =
        findSemanticCall(
            anonymousArgument.semanticGraph,
            QStringLiteral("helper"));
    passed &= check(
        findSemanticCall(
            anonymousArgument.semanticGraph,
            QStringLiteral("usemagic")) == nullptr &&
            argumentRead != nullptr &&
            outerHelper != nullptr &&
            hasDirectedEdge(
                anonymousArgument.semanticGraph,
                argumentRead->id,
                outerHelper->id,
                StoryGraphEdgeKind::Sequential),
        "anonymous call arguments do not leak uncalled function-body semantics");
    return passed;
}

bool testRuntimeApiOverwriteAndLoopScopes()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "say, runscript = customSay, customRun\n"
                "say(\"mutated\")\n"
                "runscript(\"mutated.lua\")\n"
                "function playsound() end\n"
                "playsound(\"mutated.wav\")\n"
                "_ENV.choose = customChoose\n"
                "choose(\"M\", \"A\", \"B\", \"value\")\n"
                "_ENV[\"loadmap\"] = customLoadMap\n"
                "loadmap(\"mutated.map\")\n"
                "function _ENV.add() end\n"
                "add(\"score\", 1)\n"
                "for npcattack = npcattack(\"header\", 1, 2), 1 do\n"
                "  npcattack(\"inside\", 1, 2)\n"
                "end\n"
                "npcattack(\"outside\", 1, 2)\n"
                "repeat\n"
                "  local usemagic = helper\n"
                "  usemagic(\"inside\")\n"
                "until usemagic(\"condition\")\n"
                "usemagic(\"outside\")\n"
                "local _ENV = customEnvironment\n"
                "assign(\"inside_env\", 1)\n"));

    bool passed = true;
    for (const QString& overwrittenName :
         {
             QStringLiteral("say"),
             QStringLiteral("runscript"),
             QStringLiteral("playsound"),
             QStringLiteral("choose"),
             QStringLiteral("loadmap"),
             QStringLiteral("add"),
             QStringLiteral("assign")
         })
    {
        const StoryGraphNode* call =
            findSemanticCall(
                result.semanticGraph,
                overwrittenName);
        if (call == nullptr ||
            call->kind !=
                StoryGraphNodeKind::DynamicCall ||
            call->certainty !=
                StoryGraphCertainty::Dynamic)
        {
            std::cerr
                << "overwrite mismatch: "
                << overwrittenName.toStdString()
                << '\n';
        }
        passed &= check(
            call != nullptr &&
                call->kind ==
                    StoryGraphNodeKind::DynamicCall &&
                call->certainty ==
                    StoryGraphCertainty::Dynamic,
            "runtime API overwrite is classified as dynamic");
    }

    const QList<const StoryGraphNode*> attacks =
        findSemanticCalls(
            result.semanticGraph,
            QStringLiteral("npcattack"));
    passed &= check(
        attacks.size() == 3 &&
            attacks.at(0)->sourceRange.start.line == 12 &&
            attacks.at(0)->kind ==
                StoryGraphNodeKind::Battle &&
            attacks.at(1)->sourceRange.start.line == 13 &&
            attacks.at(1)->kind ==
                StoryGraphNodeKind::DynamicCall &&
            attacks.at(2)->sourceRange.start.line == 15 &&
            attacks.at(2)->kind ==
                StoryGraphNodeKind::Battle,
        "for control variable excludes the header and shadows only the body");

    const StoryGraphDocumentResult environmentOverwrite =
        analyzeSource(
            QStringLiteral(
                "_ENV = replacement\n"
                "npcattack(\"environment\", 1, 2)\n"));
    const StoryGraphNode* environmentCall =
        findSemanticCall(
            environmentOverwrite.semanticGraph,
            QStringLiteral("npcattack"));
    passed &= check(
        environmentCall != nullptr &&
            environmentCall->kind ==
                StoryGraphNodeKind::DynamicCall &&
            environmentCall->certainty ==
                StoryGraphCertainty::Dynamic,
        "assigning _ENV invalidates direct runtime API classification");

    const StoryGraphDocumentResult environmentMethodOverwrite =
        analyzeSource(
            QStringLiteral(
                "function _ENV:addattack() end\n"
                "addattack()\n"));
    const StoryGraphNode* environmentMethodCall =
        findSemanticCall(
            environmentMethodOverwrite.semanticGraph,
            QStringLiteral("addattack"));
    passed &= check(
        environmentMethodCall != nullptr &&
            environmentMethodCall->kind ==
                StoryGraphNodeKind::DynamicCall &&
            environmentMethodCall->certainty ==
                StoryGraphCertainty::Dynamic,
        "method-form _ENV function definitions overwrite runtime APIs");

    const StoryGraphDocumentResult localOverwrite =
        analyzeSource(
            QStringLiteral(
                "do\n"
                "  local say = helper\n"
                "  say = replacement\n"
                "  local playsound\n"
                "  function playsound() end\n"
                "  local _ENV = replacementEnvironment\n"
                "  _ENV.add = replacement\n"
                "  function _ENV.choose() end\n"
                "  npcattack = replacement\n"
                "  function usemagic() end\n"
                "end\n"
                "say(\"outside\")\n"
                "playsound(\"outside.wav\")\n"
                "add(\"outside\", 1)\n"
                "choose(\"M\", \"A\", \"B\", \"value\")\n"
                "npcattack(\"outside\", 1, 2)\n"
                "usemagic(\"outside\")\n"));
    for (const QString& localOnlyName :
         {
             QStringLiteral("say"),
             QStringLiteral("playsound"),
             QStringLiteral("add"),
             QStringLiteral("choose"),
             QStringLiteral("npcattack"),
             QStringLiteral("usemagic")
         })
    {
        const StoryGraphNode* call =
            findSemanticCall(
                localOverwrite.semanticGraph,
                localOnlyName);
        passed &= check(
            call != nullptr &&
                call->kind !=
                    StoryGraphNodeKind::DynamicCall &&
                call->certainty ==
                    StoryGraphCertainty::Certain,
            "local API and _ENV writes do not poison the outer global scope");
    }

    const QList<const StoryGraphNode*> magics =
        findSemanticCalls(
            result.semanticGraph,
            QStringLiteral("usemagic"));
    passed &= check(
        magics.size() == 3 &&
            magics.at(0)->sourceRange.start.line == 18 &&
            magics.at(0)->kind ==
                StoryGraphNodeKind::DynamicCall &&
            magics.at(1)->sourceRange.start.line == 19 &&
            magics.at(1)->kind ==
                StoryGraphNodeKind::DynamicCall &&
            magics.at(2)->sourceRange.start.line == 20 &&
            magics.at(2)->kind ==
                StoryGraphNodeKind::Battle,
        "repeat locals remain visible through until and then leave scope");
    return passed;
}

bool testGlobalTableRuntimeApiOverwrite()
{
    const StoryGraphDocumentResult result =
        analyzeSource(
            QStringLiteral(
                "_G.runscript = helper\n"
                "runscript(\"dot.lua\")\n"
                "_G[\"choose\"] = helper\n"
                "choose(\"M\", \"A\", \"B\", \"answer\")\n"
                "function _G.loadmap() end\n"
                "loadmap(\"map01\")\n"
                "do\n"
                "  local _G = replacement\n"
                "  _G.playsound = helper\n"
                "  function _G.add() end\n"
                "end\n"
                "playsound(\"outside.wav\")\n"
                "add(\"score\", 1)\n"
                "_G[dynamicName] = helper\n"
                "sub(\"score\", 1)\n"));

    bool passed = true;
    for (const auto& expectedDynamicCall :
         {
             qMakePair(QStringLiteral("runscript"), 2),
             qMakePair(QStringLiteral("choose"), 4),
             qMakePair(QStringLiteral("loadmap"), 6),
             qMakePair(QStringLiteral("sub"), 15)
         })
    {
        const StoryGraphNode* call =
            findControlNodeAtLine(
                result.semanticGraph,
                StoryGraphNodeKind::DynamicCall,
                expectedDynamicCall.second);
        passed &= check(
            call != nullptr &&
                call->apiName ==
                    expectedDynamicCall.first &&
                call->certainty ==
                    StoryGraphCertainty::Dynamic,
            "standard global table overwrite invalidates the affected runtime API");
    }

    const StoryGraphNode* outsidePlaySound =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::RegisteredApiCall,
            12);
    const StoryGraphNode* outsideAdd =
        findControlNodeAtLine(
            result.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            13);
    passed &= check(
        outsidePlaySound != nullptr &&
            outsidePlaySound->apiName ==
                QStringLiteral("playsound") &&
            outsidePlaySound->certainty ==
                StoryGraphCertainty::Certain &&
            outsideAdd != nullptr &&
            outsideAdd->apiName ==
                QStringLiteral("add") &&
            outsideAdd->certainty ==
                StoryGraphCertainty::Certain,
        "writes through a local _G do not poison the outer runtime globals");
    passed &= check(
        warningCodeCount(
            result,
            QStringLiteral(
                "story_graph.semantic.api_not_direct_global")) ==
            4,
        "literal, function-definition, and dynamic _G overwrites have stable warnings");
    return passed;
}

bool testEnvironmentIndexAndGlobalDeclarationBoundaries()
{
    const StoryGraphDocumentResult dynamicEnvironmentIndex =
        analyzeSource(
            QStringLiteral(
                "_ENV[dynamicName] = helper\n"
                "playsound(\"dynamic.wav\")\n"));
    const StoryGraphNode* dynamicEnvironmentCall =
        findControlNodeAtLine(
            dynamicEnvironmentIndex.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            2);

    bool passed = true;
    passed &= check(
        dynamicEnvironmentCall != nullptr &&
            dynamicEnvironmentCall->apiName ==
                QStringLiteral("playsound") &&
            dynamicEnvironmentCall->certainty ==
                StoryGraphCertainty::Dynamic &&
            nodeKindCountAtLine(
                dynamicEnvironmentIndex.semanticGraph,
                StoryGraphNodeKind::RegisteredApiCall,
                2) ==
                0 &&
            warningCodeCountAtLine(
                dynamicEnvironmentIndex,
                QStringLiteral(
                    "story_graph.semantic.api_not_direct_global"),
                2) ==
                1,
        "dynamic _ENV index invalidates later direct runtime APIs");

    const StoryGraphDocumentResult
        parenthesizedEnvironmentIndex =
            analyzeSource(
                QStringLiteral(
                    "_ENV[(\"runscript\")] = helper\n"
                    "runscript(\"parenthesized.lua\")\n"));
    const StoryGraphNode* parenthesizedEnvironmentCall =
        findControlNodeAtLine(
            parenthesizedEnvironmentIndex.semanticGraph,
            StoryGraphNodeKind::DynamicCall,
            2);
    passed &= check(
        parenthesizedEnvironmentCall != nullptr &&
            parenthesizedEnvironmentCall->apiName ==
                QStringLiteral("runscript") &&
            parenthesizedEnvironmentCall->certainty ==
                StoryGraphCertainty::Dynamic &&
            nodeKindCountAtLine(
                parenthesizedEnvironmentIndex.semanticGraph,
                StoryGraphNodeKind::SerialScriptCall,
                2) ==
                0 &&
            warningCodeCountAtLine(
                parenthesizedEnvironmentIndex,
                QStringLiteral(
                    "story_graph.semantic.api_not_direct_global"),
                2) ==
                1,
        "parenthesized _ENV index does not preserve false direct-call certainty");

    const StoryGraphDocumentResult globalEnvironmentNames =
        analyzeSource(
            QStringLiteral(
                "global _ENV = replacementEnvironment\n"
                "playsound(\"after-env.wav\")\n"
                "global _G = replacementGlobalTable\n"
                "add(\"score\", 1)\n"));
    const StoryGraphNode* afterGlobalEnvironment =
        findControlNodeAtLine(
            globalEnvironmentNames.semanticGraph,
            StoryGraphNodeKind::RegisteredApiCall,
            2);
    const StoryGraphNode* afterGlobalTable =
        findControlNodeAtLine(
            globalEnvironmentNames.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            4);
    passed &= check(
        findVariableNode(
            globalEnvironmentNames.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("_ENV"),
            1) !=
            nullptr &&
        afterGlobalEnvironment != nullptr &&
            afterGlobalEnvironment->apiName ==
                QStringLiteral("playsound") &&
            afterGlobalEnvironment->certainty ==
                StoryGraphCertainty::Certain,
        "global _ENV declaration writes the named global without rebinding the environment");
    passed &= check(
        findVariableNode(
            globalEnvironmentNames.semanticGraph,
            StoryGraphNodeKind::VariableWrite,
            QStringLiteral("_G"),
            3) !=
            nullptr &&
        afterGlobalTable != nullptr &&
            afterGlobalTable->apiName ==
                QStringLiteral("add") &&
            afterGlobalTable->certainty ==
                StoryGraphCertainty::Certain &&
        warningCodeCount(
            globalEnvironmentNames,
            QStringLiteral(
                "story_graph.semantic.api_not_direct_global")) ==
            0,
        "global _G declaration does not replace the active environment table");
    return passed;
}

bool testAnalyzerEntryAndLargeLoopCancellation()
{
    StoryGraphAnalysisRequest emptyRequest;
    emptyRequest.source.identity.portableRootKey =
        QStringLiteral("active:test:0");
    emptyRequest.source.identity.virtualPath =
        QStringLiteral(
            "script/map/test/empty-cancel.lua");
    int entryCallbackCount = 0;
    const StoryGraphDocumentResult entryCancelled =
        StoryGraphAnalyzer::analyze(
            emptyRequest,
            [&entryCallbackCount]()
            {
                ++entryCallbackCount;
                return true;
            });

    QString largeSource;
    largeSource.reserve(2400);
    for (int index = 0; index < 600; ++index)
        largeSource.append(QStringLiteral("a=1;"));
    StoryGraphAnalysisRequest largeRequest;
    largeRequest.source.identity.portableRootKey =
        QStringLiteral("active:test:0");
    largeRequest.source.identity.virtualPath =
        QStringLiteral(
            "script/map/test/large-cancel.lua");
    largeRequest.source.utf8Bytes =
        largeSource.toUtf8();
    int largeCallbackCount = 0;
    const StoryGraphDocumentResult largeCancelled =
        StoryGraphAnalyzer::analyze(
            largeRequest,
            [&largeCallbackCount]()
            {
                ++largeCallbackCount;
                return largeCallbackCount >= 4;
            });

    bool passed = true;
    passed &= check(
        entryCallbackCount == 1 &&
            entryCancelled.status ==
                StoryGraphDocumentStatus::Cancelled &&
            !entryCancelled.controlFlowGraph.complete &&
            !entryCancelled.semanticGraph.complete &&
            entryCancelled.controlFlowGraph.nodes.isEmpty() &&
            entryCancelled.semanticGraph.nodes.isEmpty(),
        "entry cancellation publishes no graph and marks both incomplete");
    passed &= check(
        largeCallbackCount == 4 &&
            largeCancelled.status ==
                StoryGraphDocumentStatus::Cancelled &&
            !largeCancelled.controlFlowGraph.complete &&
            !largeCancelled.semanticGraph.complete &&
            largeCancelled.controlFlowGraph.nodes.isEmpty() &&
            largeCancelled.semanticGraph.nodes.isEmpty(),
        "large parser token-filtering loop observes cancellation consistently");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testKeywordsNumbersAndSymbols();
    passed &= testStringsCommentsAndLongBrackets();
    passed &= testUnicodeAndBytePositions();
    passed &= testWarningsAndRecovery();
    passed &= testCancellation();
    passed &= testUtf8BomAndInvalidInput();
    passed &= testStableGraphIdentifiers();
    passed &= testSemanticCatalogContracts();
    passed &= testDialogueAndSelectSemanticCalls();
    passed &= testRuntimeApiCatalogContracts();
    passed &= testDeterministicRecoverableLayout();
    passed &= testControlFlowAndSemanticAnalysis();
    passed &= testGotoLabelBlockVisibility();
    passed &= testGotoLocalScopeRestrictions();
    passed &= testPartialRecoveryAndAnalyzerCancellation();
    passed &= testExactCallsDynamicBoundariesAndStableEditIds();
    passed &= testChooseMultipleIndexedVariableWrites();
    passed &= testComputedCalleeBoundaries();
    passed &= testLuaCallSugarAndArbitrarySuffixChains();
    passed &= testOrdinaryLuaVariableExtraction();
    passed &= testLua55DeclarationAttributesAndContextualGlobal();
    passed &= testSemanticPostOrderAndCfgProjection();
    passed &= testRuntimeApiOverwriteAndLoopScopes();
    passed &= testGlobalTableRuntimeApiOverwrite();
    passed &= testEnvironmentIndexAndGlobalDeclarationBoundaries();
    passed &= testAnalyzerEntryAndLargeLoopCancellation();
    if (passed)
        std::cout << "All story graph core tests passed\n";
    return passed ? 0 : 1;
}
