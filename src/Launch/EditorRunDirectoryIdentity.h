#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace EditorRun
{
struct DirectoryIdentity
{
	std::uint64_t deviceOrVolume = 0;
	std::uint64_t nodeHigh = 0;
	std::uint64_t nodeLow = 0;
	std::uint64_t linkCount = 0;
	bool valid = false;
};

enum class OutputDirectoryKind : std::size_t
{
	Overlay = 0,
	IsolatedSave,
	ApplicationState,
	Diagnostics,
	Count
};

inline constexpr std::size_t OutputDirectoryCount =
	static_cast<std::size_t>(OutputDirectoryKind::Count);

using OutputDirectoryIdentities =
	std::array<DirectoryIdentity, OutputDirectoryCount>;

inline constexpr std::size_t outputDirectoryIndex(
	OutputDirectoryKind kind) noexcept
{
	return static_cast<std::size_t>(kind);
}

inline constexpr bool sameDirectoryIdentity(
	const DirectoryIdentity& first,
	const DirectoryIdentity& second) noexcept
{
	return first.valid && second.valid &&
		first.deviceOrVolume == second.deviceOrVolume &&
		first.nodeHigh == second.nodeHigh &&
		first.nodeLow == second.nodeLow;
}

inline constexpr bool sameDirectoryGeneration(
	const DirectoryIdentity& first,
	const DirectoryIdentity& second) noexcept
{
	return sameDirectoryIdentity(first, second) &&
		first.linkCount == second.linkCount;
}
}
