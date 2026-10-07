#pragma once

#include "ResourceCatalog.h"
#include "miniz.h"

#include <new>
#include <set>
#include <stdexcept>
#include <string_view>

namespace RuntimeResource
{
inline CatalogDirectoryListResult listApkAssetDirectories(
	const std::string& apkPath)
{
	CatalogDirectoryListResult result;
	mz_zip_archive archive{};
	if (!mz_zip_reader_init_file(
			&archive, apkPath.c_str(),
			MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY))
	{
		return result;
	}
	struct ArchiveCloser
	{
		mz_zip_archive* archive;
		~ArchiveCloser()
		{
			mz_zip_reader_end(archive);
		}
	} closeArchive{ &archive };

	try
	{
		std::set<std::string> directories;
		std::vector<char> filename;
		const mz_uint count = mz_zip_reader_get_num_files(&archive);
		for (mz_uint index = 0; index < count; ++index)
		{
			const mz_uint size = mz_zip_reader_get_filename(
				&archive, index, nullptr, 0);
			if (size == 0)
			{
				return {};
			}
			filename.resize(size);
			if (mz_zip_reader_get_filename(
					&archive, index, filename.data(), size) != size)
			{
				return {};
			}
			// Names come from the in-memory central directory, without opening entries.
			std::string_view path(filename.data(), size - 1);
			if (path.substr(0, 7) != "assets/")
			{
				continue;
			}
			path.remove_prefix(7);
			const std::size_t separator = path.find('/');
			if (separator == std::string_view::npos)
			{
				continue;
			}
			const std::string_view name = path.substr(0, separator);
			if (name.empty() || name == "." || name == ".." ||
				name.find('\\') != std::string_view::npos ||
				name.find('\0') != std::string_view::npos)
			{
				continue;
			}
			directories.emplace(name);
		}
		result.childDirectoryNames.assign(directories.begin(), directories.end());
		result.status = CatalogDirectoryListStatus::Success;
	}
	catch (const std::bad_alloc&)
	{
		return {};
	}
	catch (const std::length_error&)
	{
		return {};
	}
	return result;
}
}
