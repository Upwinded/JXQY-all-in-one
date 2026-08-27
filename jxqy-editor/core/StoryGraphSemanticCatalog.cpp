#include "StoryGraphSemanticCatalog.h"

namespace
{
StoryGraphArgumentPosition fromStart(int offset)
{
    StoryGraphArgumentPosition position;
    position.anchor = StoryGraphArgumentAnchor::FromStart;
    position.offset = offset;
    return position;
}

StoryGraphArgumentPosition fromEnd(int offset)
{
    StoryGraphArgumentPosition position;
    position.anchor = StoryGraphArgumentAnchor::FromEnd;
    position.offset = offset;
    return position;
}

StoryGraphArgumentRoleBinding roleAt(
    StoryGraphArgumentRole role,
    StoryGraphArgumentValueKind valueKind,
    const StoryGraphArgumentPosition& position)
{
    StoryGraphArgumentRoleBinding binding;
    binding.role = role;
    binding.valueKind = valueKind;
    binding.first = position;
    binding.last = position;
    return binding;
}

StoryGraphArgumentRoleBinding roleRange(
    StoryGraphArgumentRole role,
    StoryGraphArgumentValueKind valueKind,
    const StoryGraphArgumentPosition& first,
    const StoryGraphArgumentPosition& last)
{
    StoryGraphArgumentRoleBinding binding;
    binding.role = role;
    binding.valueKind = valueKind;
    binding.first = first;
    binding.last = last;
    return binding;
}

StoryGraphCallSignature exactSignature(
    int argumentCount,
    const QList<StoryGraphArgumentRoleBinding>& argumentRoles)
{
    StoryGraphCallSignature signature;
    signature.argumentCountPolicy =
        StoryGraphArgumentCountPolicy::Exact;
    signature.minimumArgumentCount = argumentCount;
    signature.maximumArgumentCount = argumentCount;
    signature.argumentRoles = argumentRoles;
    return signature;
}

StoryGraphCallSignature boundedSignature(
    int minimumArgumentCount,
    int maximumArgumentCount,
    const QList<StoryGraphArgumentRoleBinding>& argumentRoles)
{
    StoryGraphCallSignature signature;
    signature.argumentCountPolicy =
        StoryGraphArgumentCountPolicy::BoundedRange;
    signature.minimumArgumentCount = minimumArgumentCount;
    signature.maximumArgumentCount = maximumArgumentCount;
    signature.argumentRoles = argumentRoles;
    return signature;
}

StoryGraphCallSignature variadicSignature(
    int minimumArgumentCount,
    const QList<StoryGraphArgumentRoleBinding>& argumentRoles)
{
    StoryGraphCallSignature signature;
    signature.argumentCountPolicy =
        StoryGraphArgumentCountPolicy::Variadic;
    signature.minimumArgumentCount = minimumArgumentCount;
    signature.maximumArgumentCount =
        StoryGraphUnboundedArgumentCount;
    signature.argumentRoles = argumentRoles;
    return signature;
}

StoryGraphCallSignature ignoredTrailingSignature(
    int minimumArgumentCount,
    int firstIgnoredTrailingArgumentIndex,
    const QList<StoryGraphArgumentRoleBinding>& argumentRoles)
{
    StoryGraphCallSignature signature = variadicSignature(
        minimumArgumentCount, argumentRoles);
    signature.firstIgnoredTrailingArgumentIndex =
        firstIgnoredTrailingArgumentIndex;
    return signature;
}

StoryGraphCallSignature boundedIgnoredTrailingSignature(
    int minimumArgumentCount,
    int maximumArgumentCount,
    int firstIgnoredTrailingArgumentIndex,
    const QList<StoryGraphArgumentRoleBinding>& argumentRoles)
{
    StoryGraphCallSignature signature = boundedSignature(
        minimumArgumentCount,
        maximumArgumentCount,
        argumentRoles);
    signature.firstIgnoredTrailingArgumentIndex =
        firstIgnoredTrailingArgumentIndex;
    return signature;
}

StoryGraphSemanticDefinition definition(
    const QString& registeredName,
    StoryGraphSemanticCategory category,
    const QList<StoryGraphCallSignature>& signatures)
{
    StoryGraphSemanticDefinition result;
    result.registeredName = registeredName;
    result.canonicalName = registeredName;
    result.category = category;
    result.signatures = signatures;
    return result;
}

StoryGraphSemanticDefinition aliasDefinition(
    const QString& registeredName,
    const StoryGraphSemanticDefinition& canonicalDefinition)
{
    StoryGraphSemanticDefinition result = canonicalDefinition;
    result.registeredName = registeredName;
    result.canonicalName = canonicalDefinition.canonicalName;
    return result;
}

QList<StoryGraphSemanticDefinition> buildDefinitions()
{
    const StoryGraphArgumentPosition firstArgument =
        fromStart(0);
    const StoryGraphArgumentPosition secondArgument =
        fromStart(1);
    const StoryGraphArgumentPosition thirdArgument =
        fromStart(2);
    const StoryGraphArgumentPosition fourthArgument =
        fromStart(3);
    const StoryGraphArgumentPosition fifthArgument =
        fromStart(4);
    const StoryGraphArgumentPosition lastArgument =
        fromEnd(0);
    const StoryGraphArgumentPosition beforeLastArgument =
        fromEnd(1);

    QList<StoryGraphSemanticDefinition> result;

    StoryGraphSemanticDefinition getVariable = definition(
        QStringLiteral("getvar"),
        StoryGraphSemanticCategory::VariableRead,
        {exactSignature(1, {
            roleAt(
                StoryGraphArgumentRole::VariableReadName,
                StoryGraphArgumentValueKind::VariableName,
                firstArgument)})});
    result.append(getVariable);

    StoryGraphSemanticDefinition assign = definition(
        QStringLiteral("assign"),
        StoryGraphSemanticCategory::VariableWrite,
        {exactSignature(2, {
            roleAt(
                StoryGraphArgumentRole::VariableWriteName,
                StoryGraphArgumentValueKind::VariableName,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::VariableWriteValue,
                StoryGraphArgumentValueKind::Integer,
                lastArgument)})});
    result.append(assign);
    result.append(aliasDefinition(QStringLiteral("setvar"), assign));
    result.append(aliasDefinition(QStringLiteral("assing"), assign));

    StoryGraphSemanticDefinition add = definition(
        QStringLiteral("add"),
        StoryGraphSemanticCategory::VariableWrite,
        {exactSignature(2, {
            roleAt(
                StoryGraphArgumentRole::VariableReadWriteName,
                StoryGraphArgumentValueKind::VariableName,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::VariableWriteValue,
                StoryGraphArgumentValueKind::Integer,
                lastArgument)})});
    result.append(add);

    StoryGraphSemanticDefinition subtract = add;
    subtract.registeredName = QStringLiteral("sub");
    subtract.canonicalName = QStringLiteral("sub");
    result.append(subtract);

    StoryGraphSemanticDefinition talk = definition(
        QStringLiteral("talk"),
        StoryGraphSemanticCategory::Dialogue,
        {
            exactSignature(1, {
                roleAt(
                    StoryGraphArgumentRole::DialogueSection,
                    StoryGraphArgumentValueKind::String,
                    firstArgument)}),
            ignoredTrailingSignature(2, 2, {
                roleRange(
                    StoryGraphArgumentRole::DialogueIndex,
                    StoryGraphArgumentValueKind::Integer,
                    firstArgument,
                    secondArgument)})
        });
    result.append(talk);

    StoryGraphSemanticDefinition showTalk = definition(
        QStringLiteral("showtalk"),
        StoryGraphSemanticCategory::Dialogue,
        {ignoredTrailingSignature(2, 2, {
            roleRange(
                StoryGraphArgumentRole::DialogueIndex,
                StoryGraphArgumentValueKind::Integer,
                firstArgument,
                secondArgument)})});
    result.append(showTalk);

    StoryGraphSemanticDefinition say = definition(
        QStringLiteral("say"),
        StoryGraphSemanticCategory::Dialogue,
        {
            exactSignature(1, {
                roleAt(
                    StoryGraphArgumentRole::DialogueText,
                    StoryGraphArgumentValueKind::String,
                    firstArgument)}),
            exactSignature(2, {
                roleAt(
                    StoryGraphArgumentRole::DialogueText,
                    StoryGraphArgumentValueKind::String,
                    firstArgument),
                roleAt(
                    StoryGraphArgumentRole::PortraitIndex,
                    StoryGraphArgumentValueKind::Integer,
                    lastArgument)})
        });
    result.append(say);

    StoryGraphSemanticDefinition choose = definition(
        QStringLiteral("choose"),
        StoryGraphSemanticCategory::Choice,
        {exactSignature(4, {
            roleAt(
                StoryGraphArgumentRole::ChoiceMessage,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleRange(
                StoryGraphArgumentRole::ChoiceOption,
                StoryGraphArgumentValueKind::String,
                secondArgument,
                beforeLastArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceResultVariable,
                StoryGraphArgumentValueKind::VariableName,
                lastArgument)})});
    result.append(choose);

    StoryGraphSemanticDefinition chooseExtended = definition(
        QStringLiteral("chooseex"),
        StoryGraphSemanticCategory::Choice,
        {variadicSignature(3, {
            roleAt(
                StoryGraphArgumentRole::ChoiceMessage,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleRange(
                StoryGraphArgumentRole::ChoiceOption,
                StoryGraphArgumentValueKind::String,
                secondArgument,
                beforeLastArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceResultVariable,
                StoryGraphArgumentValueKind::VariableName,
                lastArgument)})});
    chooseExtended.optionConditionPolicy =
        StoryGraphOptionConditionPolicy::
            RuntimeMicroSyntaxUnexpanded;
    result.append(chooseExtended);

    StoryGraphSemanticDefinition chooseMultiple = definition(
        QStringLiteral("choosemultiple"),
        StoryGraphSemanticCategory::Choice,
        {variadicSignature(5, {
            roleAt(
                StoryGraphArgumentRole::ChoiceColumnCount,
                StoryGraphArgumentValueKind::Integer,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceSelectionCount,
                StoryGraphArgumentValueKind::Integer,
                secondArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceMultipleResultBase,
                StoryGraphArgumentValueKind::VariableName,
                thirdArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceMessage,
                StoryGraphArgumentValueKind::String,
                fourthArgument),
            roleRange(
                StoryGraphArgumentRole::ChoiceOption,
                StoryGraphArgumentValueKind::String,
                fifthArgument,
                lastArgument)})});
    chooseMultiple.optionConditionPolicy =
        StoryGraphOptionConditionPolicy::
            RuntimeMicroSyntaxUnexpanded;
    chooseMultiple.indexedVariableOutput.enabled = true;
    chooseMultiple.indexedVariableOutput.baseArgument =
        thirdArgument;
    chooseMultiple.indexedVariableOutput.outputCountArgument =
        secondArgument;
    chooseMultiple.indexedVariableOutput.generatedNamePrefix =
        QStringLiteral("$");
    chooseMultiple.indexedVariableOutput.firstIndex = 0;
    chooseMultiple.indexedVariableOutput.trimAsciiWhitespace =
        true;
    chooseMultiple.indexedVariableOutput.appendDecimalIndex =
        true;
    result.append(chooseMultiple);

    StoryGraphSemanticDefinition choosePlus = definition(
        QStringLiteral("chooseplus"),
        StoryGraphSemanticCategory::Choice,
        {variadicSignature(6, {
            roleAt(
                StoryGraphArgumentRole::SpeakerName,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::PortraitIndex,
                StoryGraphArgumentValueKind::Integer,
                secondArgument),
            roleAt(
                StoryGraphArgumentRole::DialoguePosition,
                StoryGraphArgumentValueKind::Integer,
                thirdArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceMessage,
                StoryGraphArgumentValueKind::String,
                fourthArgument),
            roleRange(
                StoryGraphArgumentRole::ChoiceOption,
                StoryGraphArgumentValueKind::String,
                fifthArgument,
                beforeLastArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceResultVariable,
                StoryGraphArgumentValueKind::VariableName,
                lastArgument)})});
    choosePlus.optionConditionPolicy =
        StoryGraphOptionConditionPolicy::
            RuntimeMicroSyntaxUnexpanded;
    result.append(choosePlus);

    StoryGraphSemanticDefinition select = definition(
        QStringLiteral("select"),
        StoryGraphSemanticCategory::Choice,
        {ignoredTrailingSignature(4, 4, {
            roleRange(
                StoryGraphArgumentRole::DialogueIndex,
                StoryGraphArgumentValueKind::Integer,
                firstArgument,
                thirdArgument),
            roleAt(
                StoryGraphArgumentRole::ChoiceResultVariable,
                StoryGraphArgumentValueKind::VariableName,
                fourthArgument)})});
    result.append(select);

    StoryGraphSemanticDefinition runScript = definition(
        QStringLiteral("runscript"),
        StoryGraphSemanticCategory::ScriptCall,
        {ignoredTrailingSignature(1, 1, {
            roleAt(
                StoryGraphArgumentRole::ScriptTarget,
                StoryGraphArgumentValueKind::String,
                firstArgument)})});
    runScript.scriptCallKind =
        StoryGraphScriptCallKind::Serial;
    result.append(runScript);
    result.append(aliasDefinition(
        QStringLiteral("runscirpt"), runScript));

    StoryGraphSemanticDefinition runParallelScript = definition(
        QStringLiteral("runparallelscript"),
        StoryGraphSemanticCategory::ScriptCall,
        {
            exactSignature(1, {
                roleAt(
                    StoryGraphArgumentRole::ScriptTarget,
                    StoryGraphArgumentValueKind::String,
                    firstArgument)}),
            ignoredTrailingSignature(2, 2, {
                roleAt(
                    StoryGraphArgumentRole::ScriptTarget,
                    StoryGraphArgumentValueKind::String,
                    firstArgument),
                roleAt(
                    StoryGraphArgumentRole::ScriptDelayMilliseconds,
                    StoryGraphArgumentValueKind::Integer,
                    secondArgument)})
        });
    runParallelScript.scriptCallKind =
        StoryGraphScriptCallKind::Parallel;
    result.append(runParallelScript);

    StoryGraphSemanticDefinition loadMap = definition(
        QStringLiteral("loadmap"),
        StoryGraphSemanticCategory::MapTransition,
        {ignoredTrailingSignature(1, 1, {
            roleAt(
                StoryGraphArgumentRole::MapTarget,
                StoryGraphArgumentValueKind::String,
                firstArgument)})});
    loadMap.mapContextEffect =
        StoryGraphMapContextEffect::ReplaceFromMapArgument;
    result.append(loadMap);

    StoryGraphSemanticDefinition loadGame = definition(
        QStringLiteral("loadgame"),
        StoryGraphSemanticCategory::MapContextInvalidator,
        {ignoredTrailingSignature(1, 1, {
            roleAt(
                StoryGraphArgumentRole::SaveSlot,
                StoryGraphArgumentValueKind::Integer,
                firstArgument)})});
    loadGame.mapContextEffect =
        StoryGraphMapContextEffect::Invalidate;
    result.append(loadGame);

    StoryGraphSemanticDefinition enableNpcAI = definition(
        QStringLiteral("enablenpcai"),
        StoryGraphSemanticCategory::Combat,
        {
            exactSignature(0, {}),
            ignoredTrailingSignature(1, 1, {
                roleAt(
                    StoryGraphArgumentRole::CombatNpcName,
                    StoryGraphArgumentValueKind::Any,
                    firstArgument)})
        });
    result.append(enableNpcAI);

    StoryGraphSemanticDefinition disableNpcAI = enableNpcAI;
    disableNpcAI.registeredName =
        QStringLiteral("disablenpcai");
    disableNpcAI.canonicalName =
        QStringLiteral("disablenpcai");
    result.append(disableNpcAI);

    StoryGraphSemanticDefinition setNpcAIEnabled = definition(
        QStringLiteral("setnpcaienabled"),
        StoryGraphSemanticCategory::Combat,
        {
            exactSignature(0, {}),
            exactSignature(1, {
                roleAt(
                    StoryGraphArgumentRole::CombatEnabled,
                    StoryGraphArgumentValueKind::Boolean,
                    firstArgument)}),
            ignoredTrailingSignature(2, 2, {
                roleAt(
                    StoryGraphArgumentRole::CombatNpcName,
                    StoryGraphArgumentValueKind::String,
                    firstArgument),
                roleAt(
                    StoryGraphArgumentRole::CombatEnabled,
                    StoryGraphArgumentValueKind::Boolean,
                    secondArgument)})
        });
    result.append(setNpcAIEnabled);

    StoryGraphSemanticDefinition enablePartnerCombat = definition(
        QStringLiteral("enablepartnercombat"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(0, 0, {})});
    result.append(enablePartnerCombat);

    StoryGraphSemanticDefinition disablePartnerCombat =
        enablePartnerCombat;
    disablePartnerCombat.registeredName =
        QStringLiteral("disablepartnercombat");
    disablePartnerCombat.canonicalName =
        QStringLiteral("disablepartnercombat");
    result.append(disablePartnerCombat);

    StoryGraphSemanticDefinition npcAttack = definition(
        QStringLiteral("npcattack"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(3, 3, {
            roleAt(
                StoryGraphArgumentRole::CombatNpcName,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::CombatPositionX,
                StoryGraphArgumentValueKind::Integer,
                secondArgument),
            roleAt(
                StoryGraphArgumentRole::CombatPositionY,
                StoryGraphArgumentValueKind::Integer,
                thirdArgument)})});
    result.append(npcAttack);

    const QList<StoryGraphArgumentRoleBinding>
        npcUseMagicRequiredRoles = {
            roleAt(
                StoryGraphArgumentRole::CombatNpcName,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::CombatMagicName,
                StoryGraphArgumentValueKind::String,
                secondArgument),
            roleAt(
                StoryGraphArgumentRole::CombatPositionX,
                StoryGraphArgumentValueKind::Integer,
                thirdArgument),
            roleAt(
                StoryGraphArgumentRole::CombatPositionY,
                StoryGraphArgumentValueKind::Integer,
                fourthArgument)};
    QList<StoryGraphArgumentRoleBinding> npcUseMagicLevelRoles =
        npcUseMagicRequiredRoles;
    npcUseMagicLevelRoles.append(roleAt(
        StoryGraphArgumentRole::CombatMagicLevel,
        StoryGraphArgumentValueKind::Integer,
        fifthArgument));
    StoryGraphSemanticDefinition npcUseMagic = definition(
        QStringLiteral("npcusemagic"),
        StoryGraphSemanticCategory::Combat,
        {
            exactSignature(4, npcUseMagicRequiredRoles),
            ignoredTrailingSignature(
                5, 5, npcUseMagicLevelRoles)
        });
    result.append(npcUseMagic);

    StoryGraphSemanticDefinition setNpcKind = definition(
        QStringLiteral("setnpckind"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(2, 2, {
            roleAt(
                StoryGraphArgumentRole::CombatNpcName,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::CombatNpcKind,
                StoryGraphArgumentValueKind::Integer,
                secondArgument)})});
    result.append(setNpcKind);

    StoryGraphSemanticDefinition setNpcRelation = definition(
        QStringLiteral("setnpcrelation"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(2, 2, {
            roleAt(
                StoryGraphArgumentRole::CombatNpcName,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::CombatNpcRelation,
                StoryGraphArgumentValueKind::Integer,
                secondArgument)})});
    result.append(setNpcRelation);

    StoryGraphSemanticDefinition setAllNpcIsEnemy = definition(
        QStringLiteral("setallnpcisenemy"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(0, 0, {})});
    result.append(setAllNpcIsEnemy);

    StoryGraphSemanticDefinition setKeepAttack = definition(
        QStringLiteral("setkeepattack"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(3, 3, {
            roleAt(
                StoryGraphArgumentRole::CombatNpcName,
                StoryGraphArgumentValueKind::String,
                firstArgument),
            roleAt(
                StoryGraphArgumentRole::CombatPositionX,
                StoryGraphArgumentValueKind::Integer,
                secondArgument),
            roleAt(
                StoryGraphArgumentRole::CombatPositionY,
                StoryGraphArgumentValueKind::Integer,
                thirdArgument)})});
    result.append(setKeepAttack);
    result.append(aliasDefinition(
        QStringLiteral("setnpckeepattack"), setKeepAttack));

    StoryGraphSemanticDefinition toNonFightingState = definition(
        QStringLiteral("tononfightingstate"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(0, 0, {})});
    result.append(toNonFightingState);

    StoryGraphSemanticDefinition enableFight = definition(
        QStringLiteral("enablefight"),
        StoryGraphSemanticCategory::Combat,
        {ignoredTrailingSignature(0, 0, {})});
    result.append(enableFight);

    StoryGraphSemanticDefinition disableFight = enableFight;
    disableFight.registeredName =
        QStringLiteral("disablefight");
    disableFight.canonicalName =
        QStringLiteral("disablefight");
    result.append(disableFight);

    StoryGraphSemanticDefinition setFightEnabled = definition(
        QStringLiteral("setfightenabled"),
        StoryGraphSemanticCategory::Combat,
        {
            exactSignature(0, {}),
            ignoredTrailingSignature(1, 1, {
                roleAt(
                    StoryGraphArgumentRole::CombatEnabled,
                    StoryGraphArgumentValueKind::Boolean,
                    firstArgument)})
        });
    result.append(setFightEnabled);

    StoryGraphSemanticDefinition useMagic = definition(
        QStringLiteral("usemagic"),
        StoryGraphSemanticCategory::Combat,
        {
            boundedIgnoredTrailingSignature(1, 2, 1, {
                roleAt(
                    StoryGraphArgumentRole::CombatMagicName,
                    StoryGraphArgumentValueKind::String,
                    firstArgument)}),
            ignoredTrailingSignature(3, 3, {
                roleAt(
                    StoryGraphArgumentRole::CombatMagicName,
                    StoryGraphArgumentValueKind::String,
                    firstArgument),
                roleAt(
                    StoryGraphArgumentRole::CombatPositionX,
                    StoryGraphArgumentValueKind::Integer,
                    secondArgument),
                roleAt(
                    StoryGraphArgumentRole::CombatPositionY,
                    StoryGraphArgumentValueKind::Integer,
                    thirdArgument)})
        });
    result.append(useMagic);

    return result;
}
}

int StoryGraphArgumentPosition::resolve(
    int argumentCount) const
{
    if (argumentCount <= 0 || offset < 0)
        return -1;

    const int resolvedIndex =
        anchor == StoryGraphArgumentAnchor::FromStart
        ? offset
        : argumentCount - 1 - offset;
    if (resolvedIndex < 0 || resolvedIndex >= argumentCount)
        return -1;
    return resolvedIndex;
}

bool StoryGraphArgumentRoleBinding::resolveRange(
    int argumentCount,
    int& firstArgumentIndex,
    int& lastArgumentIndex) const
{
    firstArgumentIndex = first.resolve(argumentCount);
    lastArgumentIndex = last.resolve(argumentCount);
    return firstArgumentIndex >= 0 &&
        lastArgumentIndex >= firstArgumentIndex;
}

bool StoryGraphCallSignature::acceptsArgumentCount(
    int argumentCount) const
{
    if (argumentCount < 0)
        return false;

    switch (argumentCountPolicy)
    {
    case StoryGraphArgumentCountPolicy::Exact:
        return minimumArgumentCount == maximumArgumentCount &&
            argumentCount == minimumArgumentCount;
    case StoryGraphArgumentCountPolicy::BoundedRange:
        return maximumArgumentCount >= minimumArgumentCount &&
            argumentCount >= minimumArgumentCount &&
            argumentCount <= maximumArgumentCount;
    case StoryGraphArgumentCountPolicy::Variadic:
        return maximumArgumentCount ==
                StoryGraphUnboundedArgumentCount &&
            argumentCount >= minimumArgumentCount;
    }
    return false;
}

int StoryGraphCallSignature::ignoredTrailingArgumentCount(
    int argumentCount) const
{
    if (!acceptsArgumentCount(argumentCount) ||
        firstIgnoredTrailingArgumentIndex < 0 ||
        argumentCount <= firstIgnoredTrailingArgumentIndex)
    {
        return 0;
    }
    return argumentCount -
        firstIgnoredTrailingArgumentIndex;
}

bool StoryGraphSemanticDefinition::isAlias() const
{
    return registeredName != canonicalName;
}

const StoryGraphCallSignature*
StoryGraphSemanticDefinition::signatureForArgumentCount(
    int argumentCount) const
{
    for (const StoryGraphCallSignature& signature : signatures)
    {
        if (signature.acceptsArgumentCount(argumentCount))
            return &signature;
    }
    return nullptr;
}

const QList<StoryGraphSemanticDefinition>&
StoryGraphSemanticCatalog::definitions()
{
    static const QList<StoryGraphSemanticDefinition> catalog =
        buildDefinitions();
    return catalog;
}

const StoryGraphSemanticDefinition*
StoryGraphSemanticCatalog::findExact(
    const QString& registeredName)
{
    for (const StoryGraphSemanticDefinition& definition :
         definitions())
    {
        if (definition.registeredName == registeredName)
            return &definition;
    }
    return nullptr;
}
