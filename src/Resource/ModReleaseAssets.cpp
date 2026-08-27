#include "ModReleaseAssets.h"

#include "../File/ResourcePathSafety.h"
#include "../File/RootedResourceReader.h"
#include "../File/StrictRelativeResourcePath.h"

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
#define JXQY_PACKAGED_MOBILE_RELEASE_ASSETS
#include <SDL3/SDL_iostream.h>
#endif

namespace
{
ModRelease::AssetReadStatus toAssetReadStatus(
	RootedResourceReader::Status status)
{
	switch (status)
	{
	case RootedResourceReader::Status::Success:
		return ModRelease::AssetReadStatus::Ready;
	case RootedResourceReader::Status::InvalidRoot:
		return ModRelease::AssetReadStatus::InvalidRoot;
	case RootedResourceReader::Status::UnsafeRelativePath:
		return ModRelease::AssetReadStatus::UnsafePath;
	case RootedResourceReader::Status::EscapesRoot:
		return ModRelease::AssetReadStatus::EscapesPackRoot;
	case RootedResourceReader::Status::NotFound:
		return ModRelease::AssetReadStatus::NotFound;
	case RootedResourceReader::Status::NotRegularFile:
		return ModRelease::AssetReadStatus::NotRegularFile;
	case RootedResourceReader::Status::TooLarge:
		return ModRelease::AssetReadStatus::TooLarge;
	case RootedResourceReader::Status::ReadFailed:
	default:
		return ModRelease::AssetReadStatus::ReadFailed;
	}
}

bool containsDisallowedTextControl(std::string_view text)
{
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		const unsigned char character =
			static_cast<unsigned char>(text[index]);
		if ((character < 0x20 && character != '\r' &&
			character != '\n' && character != '\t') ||
			character == 0x7F)
		{
			return true;
		}
		if (character == 0xC2 && index + 1 < text.size())
		{
			const unsigned char nextCharacter =
				static_cast<unsigned char>(text[index + 1]);
			if (nextCharacter >= 0x80 && nextCharacter <= 0x9F)
			{
				return true;
			}
		}
	}
	return false;
}

ModRelease::PackagedAssetReadResult normalizeReaderResult(
	ModRelease::PackagedAssetReadResult result,
	std::size_t maximumBytes)
{
	if (!result.succeeded())
	{
		result.bytes.clear();
		return result;
	}
	if (result.bytes.size() > maximumBytes)
	{
		result.bytes.clear();
		result.status = ModRelease::AssetReadStatus::TooLarge;
	}
	return result;
}

ModRelease::PackagedAssetReadResult readPackagedAsset(
	std::string_view packRootUtf8,
	std::string_view declaredRelativePathUtf8,
	std::size_t maximumBytes,
	const ModRelease::PackagedAssetReader& reader)
{
	ModRelease::PackagedAssetReadResult result;
	const ResourcePathSafety::StrictRelativePathResult relativePath =
		ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
			declaredRelativePathUtf8);
	if (!relativePath.succeeded())
	{
		result.status = ModRelease::AssetReadStatus::UnsafePath;
		return result;
	}

	std::string normalizedPackRoot(packRootUtf8);
	std::replace(normalizedPackRoot.begin(), normalizedPackRoot.end(),
		'\\', '/');
	while (!normalizedPackRoot.empty() &&
		normalizedPackRoot.back() == '/')
	{
		normalizedPackRoot.pop_back();
	}

	std::string packagedPath = relativePath.normalizedPath;
	if (!normalizedPackRoot.empty())
	{
		const ResourcePathSafety::StrictRelativePathResult packRoot =
			ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
				normalizedPackRoot);
		if (!packRoot.succeeded())
		{
			result.status = ModRelease::AssetReadStatus::InvalidRoot;
			return result;
		}
		packagedPath = packRoot.normalizedPath + "/" + packagedPath;
	}

	if (!reader)
	{
		result.status = ModRelease::AssetReadStatus::ReadFailed;
		return result;
	}

	try
	{
		return normalizeReaderResult(
			reader(packagedPath, maximumBytes), maximumBytes);
	}
	catch (const std::bad_alloc&)
	{
		result.status = ModRelease::AssetReadStatus::ReadFailed;
	}
	catch (const std::length_error&)
	{
		result.status = ModRelease::AssetReadStatus::ReadFailed;
	}
	catch (...)
	{
		result.status = ModRelease::AssetReadStatus::ReadFailed;
	}
	return result;
}

ModRelease::DescriptionReadResult makeDescriptionResult(
	ModRelease::PackagedAssetReadResult file)
{
	ModRelease::DescriptionReadResult result;
	result.status = file.status;
	if (!file.succeeded())
	{
		return result;
	}

	std::size_t textStart = 0;
	if (file.bytes.size() >= 3 &&
		file.bytes[0] == 0xEF &&
		file.bytes[1] == 0xBB &&
		file.bytes[2] == 0xBF)
	{
		textStart = 3;
	}
	if (textStart < file.bytes.size())
	{
		result.utf8Text.assign(
			reinterpret_cast<const char*>(file.bytes.data() + textStart),
			file.bytes.size() - textStart);
	}

	if (!ResourcePathSafety::isValidUtf8(result.utf8Text))
	{
		result.utf8Text.clear();
		result.status = ModRelease::AssetReadStatus::InvalidUtf8;
		return result;
	}
	if (containsDisallowedTextControl(result.utf8Text))
	{
		result.utf8Text.clear();
		result.status = ModRelease::AssetReadStatus::InvalidText;
		return result;
	}

	result.status = ModRelease::AssetReadStatus::Ready;
	return result;
}

ModRelease::CoverReadResult makeCoverResult(
	ModRelease::PackagedAssetReadResult file)
{
	ModRelease::CoverReadResult result;
	result.status = file.status;
	if (!file.succeeded())
	{
		return result;
	}

	if (!EncodedImageSafety::inspectSafeDimensions(
		file.bytes.empty() ? nullptr : file.bytes.data(),
		file.bytes.size(), result.dimensions))
	{
		result.dimensions = {};
		result.status = ModRelease::AssetReadStatus::InvalidImage;
		return result;
	}

	result.encodedBytes = std::move(file.bytes);
	result.status = ModRelease::AssetReadStatus::Ready;
	return result;
}

#if defined(JXQY_PACKAGED_MOBILE_RELEASE_ASSETS)
ModRelease::PackagedAssetReadResult readSdlPackagedAsset(
	std::string_view packagedPathUtf8,
	std::size_t maximumBytes)
{
	ModRelease::PackagedAssetReadResult result;
	struct SDLIOStreamCloser
	{
		void operator()(SDL_IOStream* stream) const
		{
			if (stream != nullptr)
			{
				SDL_CloseIO(stream);
			}
		}
	};

	const std::string packagedPath(packagedPathUtf8);
	std::unique_ptr<SDL_IOStream, SDLIOStreamCloser> input(
		SDL_IOFromFile(packagedPath.c_str(), "rb"));
	if (input == nullptr)
	{
		result.status = ModRelease::AssetReadStatus::NotFound;
		return result;
	}

	const Sint64 streamSize = SDL_GetIOSize(input.get());
	if (streamSize >= 0)
	{
		const std::uint64_t unsignedStreamSize =
			static_cast<std::uint64_t>(streamSize);
		if (unsignedStreamSize >
			static_cast<std::uint64_t>(maximumBytes))
		{
			result.status = ModRelease::AssetReadStatus::TooLarge;
			return result;
		}

		try
		{
			result.bytes.resize(
				static_cast<std::size_t>(unsignedStreamSize));
		}
		catch (const std::bad_alloc&)
		{
			result.status = ModRelease::AssetReadStatus::ReadFailed;
			return result;
		}
		catch (const std::length_error&)
		{
			result.status = ModRelease::AssetReadStatus::ReadFailed;
			return result;
		}

		std::size_t bytesRead = 0;
		while (bytesRead < result.bytes.size())
		{
			const std::size_t count = SDL_ReadIO(
				input.get(),
				result.bytes.data() + bytesRead,
				result.bytes.size() - bytesRead);
			if (count == 0)
			{
				result.bytes.clear();
				result.status =
					ModRelease::AssetReadStatus::ReadFailed;
				return result;
			}
			bytesRead += count;
		}
		result.status = ModRelease::AssetReadStatus::Ready;
		return result;
	}

	// Some SDL transports cannot report a size in advance. Read them in fixed
	// chunks and probe one extra byte at the exact limit, so no unbounded
	// allocation or limit bypass is possible.
	std::array<std::uint8_t, 16U * 1024U> buffer = {};
	while (result.bytes.size() < maximumBytes)
	{
		const std::size_t readCapacity = std::min(
			buffer.size(), maximumBytes - result.bytes.size());
		const std::size_t count = SDL_ReadIO(
			input.get(), buffer.data(), readCapacity);
		if (count == 0)
		{
			if (SDL_GetIOStatus(input.get()) != SDL_IO_STATUS_EOF)
			{
				result.bytes.clear();
				result.status =
					ModRelease::AssetReadStatus::ReadFailed;
				return result;
			}
			result.status = ModRelease::AssetReadStatus::Ready;
			return result;
		}
		try
		{
			result.bytes.insert(result.bytes.end(),
				buffer.begin(), buffer.begin() + count);
		}
		catch (const std::bad_alloc&)
		{
			result.bytes.clear();
			result.status = ModRelease::AssetReadStatus::ReadFailed;
			return result;
		}
		catch (const std::length_error&)
		{
			result.bytes.clear();
			result.status = ModRelease::AssetReadStatus::ReadFailed;
			return result;
		}
	}

	std::uint8_t extraByte = 0;
	const std::size_t extraCount =
		SDL_ReadIO(input.get(), &extraByte, 1);
	if (extraCount != 0)
	{
		result.bytes.clear();
		result.status = ModRelease::AssetReadStatus::TooLarge;
		return result;
	}
	if (SDL_GetIOStatus(input.get()) != SDL_IO_STATUS_EOF)
	{
		result.bytes.clear();
		result.status = ModRelease::AssetReadStatus::ReadFailed;
		return result;
	}
	result.status = ModRelease::AssetReadStatus::Ready;
	return result;
}
#endif
}

namespace ModRelease
{
bool isValidDescriptionUtf8(std::string_view text)
{
	return ResourcePathSafety::isValidUtf8(std::string(text)) &&
		!containsDisallowedTextControl(text);
}

DescriptionReadResult readDescriptionFromPack(
	const std::filesystem::path& packRoot,
	const ModReleaseMetadata& metadata)
{
	if (metadata.descriptionFilePath.empty())
	{
		return {};
	}

#if defined(JXQY_PACKAGED_MOBILE_RELEASE_ASSETS)
	if (!packRoot.is_absolute())
	{
		try
		{
			return readDescriptionFromPackagedAssets(
				packRoot.u8string(), metadata, readSdlPackagedAsset);
		}
		catch (const std::exception&)
		{
			DescriptionReadResult result;
			result.status = AssetReadStatus::InvalidRoot;
			return result;
		}
	}
#endif
	RootedResourceReader::Result file =
		RootedResourceReader::readBoundedFileFromRoot(packRoot,
			metadata.descriptionFilePath, MaximumDescriptionBytes);
	PackagedAssetReadResult normalized;
	normalized.status = toAssetReadStatus(file.status);
	normalized.bytes = std::move(file.bytes);
	return makeDescriptionResult(std::move(normalized));
}

CoverReadResult readCoverFromPack(
	const std::filesystem::path& packRoot,
	const ModReleaseMetadata& metadata)
{
	if (metadata.coverPath.empty())
	{
		return {};
	}

#if defined(JXQY_PACKAGED_MOBILE_RELEASE_ASSETS)
	if (!packRoot.is_absolute())
	{
		try
		{
			return readCoverFromPackagedAssets(
				packRoot.u8string(), metadata, readSdlPackagedAsset);
		}
		catch (const std::exception&)
		{
			CoverReadResult result;
			result.status = AssetReadStatus::InvalidRoot;
			return result;
		}
	}
#endif
	RootedResourceReader::Result file =
		RootedResourceReader::readBoundedFileFromRoot(packRoot,
			metadata.coverPath,
			EncodedImageSafety::MaxEncodedImageBytes);
	PackagedAssetReadResult normalized;
	normalized.status = toAssetReadStatus(file.status);
	normalized.bytes = std::move(file.bytes);
	return makeCoverResult(std::move(normalized));
}

DescriptionReadResult readDescriptionFromPackagedAssets(
	std::string_view packRootUtf8,
	const ModReleaseMetadata& metadata,
	const PackagedAssetReader& reader)
{
	if (metadata.descriptionFilePath.empty())
	{
		return {};
	}
	return makeDescriptionResult(readPackagedAsset(
		packRootUtf8,
		metadata.descriptionFilePath,
		MaximumDescriptionBytes,
		reader));
}

CoverReadResult readCoverFromPackagedAssets(
	std::string_view packRootUtf8,
	const ModReleaseMetadata& metadata,
	const PackagedAssetReader& reader)
{
	if (metadata.coverPath.empty())
	{
		return {};
	}
	return makeCoverResult(readPackagedAsset(
		packRootUtf8,
		metadata.coverPath,
		EncodedImageSafety::MaxEncodedImageBytes,
		reader));
}
}
