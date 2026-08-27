#pragma once

#include "ModReleaseMetadata.h"
#include "../Image/EncodedImageSafety.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ModRelease
{
inline constexpr std::size_t MaximumDescriptionBytes = 64U * 1024U;

enum class AssetReadStatus
{
	Ready,
	NotDeclared,
	UnsafePath,
	InvalidRoot,
	EscapesPackRoot,
	NotFound,
	NotRegularFile,
	TooLarge,
	ReadFailed,
	InvalidUtf8,
	InvalidText,
	InvalidImage
};

struct DescriptionReadResult
{
	AssetReadStatus status = AssetReadStatus::NotDeclared;
	std::string utf8Text;

	bool succeeded() const noexcept
	{
		return status == AssetReadStatus::Ready;
	}
};

struct CoverReadResult
{
	AssetReadStatus status = AssetReadStatus::NotDeclared;
	std::vector<std::uint8_t> encodedBytes;
	EncodedImageSafety::Dimensions dimensions;

	bool readyForDecode() const noexcept
	{
		return status == AssetReadStatus::Ready;
	}
};

struct PackagedAssetReadResult
{
	AssetReadStatus status = AssetReadStatus::ReadFailed;
	std::vector<std::uint8_t> bytes;

	bool succeeded() const noexcept
	{
		return status == AssetReadStatus::Ready;
	}
};

using PackagedAssetReader = std::function<PackagedAssetReadResult(
	std::string_view packagedPathUtf8, std::size_t maximumBytes)>;

DescriptionReadResult readDescriptionFromPack(
	const std::filesystem::path& packRoot,
	const ModReleaseMetadata& metadata);

CoverReadResult readCoverFromPack(
	const std::filesystem::path& packRoot,
	const ModReleaseMetadata& metadata);

// Android and iOS packaged resources do not have a native directory handle
// that RootedResourceReader can anchor. These entry points keep both the pack
// root and declared asset path inside one strict virtual namespace, then call
// exactly the supplied bounded reader without dependency/Common fallback.
// Production mobile builds inject SDL's packaged-asset transport internally
// for relative bundle/APK roots; explicit absolute roots keep using
// RootedResourceReader. Native tests can inject an in-memory reader without
// pretending to be mobile.
DescriptionReadResult readDescriptionFromPackagedAssets(
	std::string_view packRootUtf8,
	const ModReleaseMetadata& metadata,
	const PackagedAssetReader& reader);

CoverReadResult readCoverFromPackagedAssets(
	std::string_view packRootUtf8,
	const ModReleaseMetadata& metadata,
	const PackagedAssetReader& reader);

// Uses the same UTF-8 and control-character contract as the runtime
// description reader so editors cannot publish text that the game rejects.
bool isValidDescriptionUtf8(std::string_view text);
}
