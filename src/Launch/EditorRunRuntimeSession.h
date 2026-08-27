#pragma once

#include "EditorRunDescriptor.h"
#include "EditorRunRuntimeTrace.h"
#include "EditorRunDirectoryIdentity.h"
#include "../File/RootedResourceReader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace EditorRun
{
enum class RuntimeSessionFailureCategory
{
	None,
	DescriptorRead,
	DescriptorValidation,
	Isolation
};

enum class RuntimeSessionError
{
	None,
	DescriptorPathInvalid,
	DescriptorOpenFailed,
	DescriptorReadFailed,
	DescriptorTooLarge,
	DescriptorUnsafe,
	DescriptorInvalid,
	OutputTopologyInvalid,
	OutputDirectoryUnavailable,
	ResourceRoutingContractUnavailable
};

struct RuntimeSessionOverlayOrigin
{
	std::string virtualPath;
	std::uint64_t rootOrdinal = 0;
	std::optional<RuntimeTraceRootKind> rootKind;
	std::optional<std::string> resourcePackId;
	std::filesystem::path rootPath;
};

struct RuntimeSession
{
	Descriptor descriptor;
	std::filesystem::path descriptorPath;
	std::filesystem::path sessionRoot;
	std::filesystem::path diagnosticsRoot;
	std::filesystem::path runtimeTracePath;
	OutputDirectoryIdentities outputDirectoryIdentities;
	std::vector<RuntimeSessionOverlayOrigin>
		traceOverlayOrigins;
};

struct RuntimeSessionResult
{
	RuntimeSession session;
	RuntimeSessionFailureCategory failureCategory =
		RuntimeSessionFailureCategory::None;
	RuntimeSessionError error = RuntimeSessionError::None;
	DescriptorError descriptorError = DescriptorError::None;
	std::string diagnosticCode;
	std::string fieldPath;
	std::filesystem::path problemPath;
	std::string message;
	std::size_t line = 0;
	std::size_t column = 0;

	bool succeeded() const noexcept
	{
		return error == RuntimeSessionError::None;
	}
};

// Loads one editor-created invocation descriptor. Descriptor and contract
// reads are bounded; private output directories must be absolute, distinct,
// existing directories contained by the descriptor's session directory.
// This function never creates a file or directory.
RuntimeSessionResult loadEditorRunRuntimeSession(
	const std::filesystem::path& descriptorPath);
}
