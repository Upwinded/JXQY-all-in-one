#pragma once

#include "../Launch/EditorRunDirectoryIdentity.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace RootedResourceReader
{
enum class Status
{
	Success,
	InvalidRoot,
	UnsafeRelativePath,
	EscapesRoot,
	NotFound,
	NotRegularFile,
	TooLarge,
	ReadFailed
};

struct Result
{
	Status status = Status::ReadFailed;
	std::vector<std::uint8_t> bytes;

	bool succeeded() const noexcept
	{
		return status == Status::Success;
	}
};

struct ProbeResult
{
	Status status = Status::ReadFailed;

	bool succeeded() const noexcept
	{
		return status == Status::Success;
	}
};

struct RootAnchorState;
struct RootAnchorResult;

class RootAnchor
{
public:
	bool valid() const noexcept;
	const std::filesystem::path& path() const noexcept;
	EditorRun::DirectoryIdentity identity() const noexcept;

private:
	std::shared_ptr<const RootAnchorState> state;

	friend struct RootAnchorResult;
	friend RootAnchorResult openRootAnchor(
		const std::filesystem::path& root);
	friend ProbeResult probeRegularFileFromRoot(
		const RootAnchor& root,
		std::string_view relativePathUtf8);
	friend Result readBoundedFileFromRoot(
		const RootAnchor& root,
		std::string_view relativePathUtf8,
		std::size_t maximumBytes);
};

struct RootAnchorResult
{
	Status status = Status::InvalidRoot;
	RootAnchor anchor;

	bool succeeded() const noexcept
	{
		return status == Status::Success && anchor.valid();
	}
};

#if defined(JXQY_ENABLE_TEST_HOOKS)
enum class ReadTestPhase
{
	AfterRead
};

using ReadTestHook = std::function<void(ReadTestPhase)>;
#endif

// Opens and retains one no-follow native directory generation. Copies of the
// returned anchor share the same held handle until the last owner is released.
RootAnchorResult openRootAnchor(const std::filesystem::path& root);

// Path overloads are for open author resources. They keep only the strict
// relative spelling and follow the root, descendant directory, and file path's
// current symlink or reparse-point targets for every operation. Anchor
// overloads are for private editor-run data and retain an exact no-follow root.
//
// Probes the supplied relative path but does not read the file payload.
ProbeResult probeRegularFileFromRoot(const std::filesystem::path& root,
	std::string_view relativePathUtf8);
ProbeResult probeRegularFileFromRoot(const RootAnchor& root,
	std::string_view relativePathUtf8);

// Reads only from the supplied root. No active, dependency, UI, or common
// resource fallback is consulted.
Result readBoundedFileFromRoot(const std::filesystem::path& root,
	std::string_view relativePathUtf8, std::size_t maximumBytes);
Result readBoundedFileFromRoot(const RootAnchor& root,
	std::string_view relativePathUtf8, std::size_t maximumBytes);

#if defined(JXQY_ENABLE_TEST_HOOKS)
void setReadTestHookForTests(const ReadTestHook& hook);
#endif
}
