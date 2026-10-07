#include "RuntimeSaveGenerationPolicy.h"

SaveGenerationPolicy createRuntimeSaveGenerationPolicy()
{
	// Runtime load/save preparation parses the files it actually needs. Keep
	// only practical copy bounds here instead of duplicating every subsystem's
	// semantic validation before the real load.
	SaveGenerationPolicy policy;
	policy.limits.maximumFileCount = 2048;
	policy.limits.maximumTotalBytes =
		64ULL * 1024ULL * 1024ULL;
	policy.limits.maximumSingleFileBytes =
		16 * 1024 * 1024;
	return policy;
}
