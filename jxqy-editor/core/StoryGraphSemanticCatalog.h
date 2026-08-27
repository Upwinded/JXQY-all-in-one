#pragma once

#include <QList>
#include <QString>

constexpr int StoryGraphUnboundedArgumentCount = -1;

enum class StoryGraphSemanticCategory
{
    VariableRead,
    VariableWrite,
    Dialogue,
    Choice,
    ScriptCall,
    MapTransition,
    MapContextInvalidator,
    Combat
};

enum class StoryGraphArgumentCountPolicy
{
    Exact,
    BoundedRange,
    Variadic
};

enum class StoryGraphArgumentValueKind
{
    Any,
    String,
    Integer,
    Boolean,
    VariableName
};

enum class StoryGraphArgumentRole
{
    VariableReadName,
    VariableWriteName,
    VariableReadWriteName,
    VariableWriteValue,
    DialogueSection,
    DialogueIndex,
    DialogueText,
    PortraitIndex,
    ChoiceMessage,
    ChoiceOption,
    ChoiceResultVariable,
    ChoiceColumnCount,
    ChoiceSelectionCount,
    ChoiceMultipleResultBase,
    SpeakerName,
    DialoguePosition,
    ScriptTarget,
    ScriptDelayMilliseconds,
    MapTarget,
    SaveSlot,
    CombatNpcName,
    CombatEnabled,
    CombatPositionX,
    CombatPositionY,
    CombatMagicName,
    CombatMagicLevel,
    CombatNpcKind,
    CombatNpcRelation
};

enum class StoryGraphArgumentAnchor
{
    FromStart,
    FromEnd
};

struct StoryGraphArgumentPosition
{
    StoryGraphArgumentAnchor anchor =
        StoryGraphArgumentAnchor::FromStart;
    int offset = 0;

    int resolve(int argumentCount) const;
};

struct StoryGraphArgumentRoleBinding
{
    StoryGraphArgumentRole role =
        StoryGraphArgumentRole::DialogueText;
    StoryGraphArgumentValueKind valueKind =
        StoryGraphArgumentValueKind::Any;
    StoryGraphArgumentPosition first;
    StoryGraphArgumentPosition last;

    bool resolveRange(
        int argumentCount,
        int& firstArgumentIndex,
        int& lastArgumentIndex) const;
};

struct StoryGraphCallSignature
{
    StoryGraphArgumentCountPolicy argumentCountPolicy =
        StoryGraphArgumentCountPolicy::Exact;
    int minimumArgumentCount = 0;
    int maximumArgumentCount = 0;
    QList<StoryGraphArgumentRoleBinding> argumentRoles;
    int firstIgnoredTrailingArgumentIndex = -1;

    bool acceptsArgumentCount(int argumentCount) const;
    int ignoredTrailingArgumentCount(
        int argumentCount) const;
};

enum class StoryGraphScriptCallKind
{
    None,
    Serial,
    Parallel
};

enum class StoryGraphMapContextEffect
{
    None,
    ReplaceFromMapArgument,
    Invalidate
};

enum class StoryGraphOptionConditionPolicy
{
    NotApplicable,
    RuntimeMicroSyntaxUnexpanded
};

struct StoryGraphIndexedVariableOutput
{
    bool enabled = false;
    StoryGraphArgumentPosition baseArgument;
    StoryGraphArgumentPosition outputCountArgument;
    QString generatedNamePrefix;
    int firstIndex = 0;
    bool trimAsciiWhitespace = false;
    bool appendDecimalIndex = false;
};

struct StoryGraphSemanticDefinition
{
    QString registeredName;
    QString canonicalName;
    StoryGraphSemanticCategory category =
        StoryGraphSemanticCategory::Dialogue;
    QList<StoryGraphCallSignature> signatures;
    StoryGraphScriptCallKind scriptCallKind =
        StoryGraphScriptCallKind::None;
    StoryGraphMapContextEffect mapContextEffect =
        StoryGraphMapContextEffect::None;
    StoryGraphOptionConditionPolicy optionConditionPolicy =
        StoryGraphOptionConditionPolicy::NotApplicable;
    StoryGraphIndexedVariableOutput indexedVariableOutput;

    bool isAlias() const;
    const StoryGraphCallSignature* signatureForArgumentCount(
        int argumentCount) const;
};

class StoryGraphSemanticCatalog
{
public:
    static const QList<StoryGraphSemanticDefinition>& definitions();

    // Runtime globals are lowercase and Lua lookup is case-sensitive.
    // This lookup deliberately does not trim or normalize its input.
    static const StoryGraphSemanticDefinition* findExact(
        const QString& registeredName);
};
