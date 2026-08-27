#pragma once

#include "SaveGeneration.h"

class GameManager;

enum class RuntimeSaveGenerationPolicyMode
{
	CompatibleLoad,
	GeneratedSave
};

SaveGenerationPreflightPolicy createRuntimeSaveGenerationPolicy(
	GameManager& gameManager,
	RuntimeSaveGenerationPolicyMode mode);
