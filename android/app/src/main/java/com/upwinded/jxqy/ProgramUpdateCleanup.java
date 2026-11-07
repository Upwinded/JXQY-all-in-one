package com.upwinded.jxqy;

import java.io.File;
import java.io.IOException;

final class ProgramUpdateCleanup
{
	static void deleteUpdatePackages(File downloadsDirectory) throws IOException
	{
		deleteCacheDirectory(downloadsDirectory, "updates");
	}

	static void deleteCacheDirectory(File parentDirectory, String directoryName) throws IOException
	{
		if (parentDirectory == null)
		{
			return;
		}
		File updateDirectory = new File(parentDirectory.getCanonicalFile(), directoryName);
		if (!updateDirectory.exists())
		{
			return;
		}
		if (!updateDirectory.isDirectory())
		{
			throw new IOException("Program update cache is not a directory");
		}
		deleteUpdateEntry(updateDirectory);
	}

	private static void deleteUpdateEntry(File entry) throws IOException
	{
		if (!entry.getAbsoluteFile().equals(entry.getCanonicalFile()))
		{
			throw new IOException("Program update cache contains a linked path");
		}
		if (entry.isDirectory())
		{
			File[] children = entry.listFiles();
			if (children == null)
			{
				throw new IOException("Cannot read program update cache directory");
			}
			for (File child : children)
			{
				deleteUpdateEntry(child);
			}
		}
		if (!entry.delete() && entry.exists())
		{
			throw new IOException("Cannot delete program update cache entry");
		}
	}
}
