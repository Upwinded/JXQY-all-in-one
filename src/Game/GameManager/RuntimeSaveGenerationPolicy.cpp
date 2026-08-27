#include "RuntimeSaveGenerationPolicy.h"

#include "GameManager.h"

SaveGenerationPreflightPolicy createRuntimeSaveGenerationPolicy(
	GameManager& gameManager,
	RuntimeSaveGenerationPolicyMode mode)
{
	(void)gameManager;
	(void)mode;

	// Runtime load/save preparation parses the files it actually needs. Keep
	// only practical copy bounds here instead of duplicating every subsystem's
	// semantic validation before the real load.
	SaveGenerationPreflightPolicy policy;
	policy.limits.maximumFileCount = 2048;
	policy.limits.maximumTotalBytes =
		64ULL * 1024ULL * 1024ULL;
	policy.limits.maximumSingleFileBytes =
		16 * 1024 * 1024;
	return policy;
}
