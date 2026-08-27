#pragma once

#include <string>

namespace AndroidProgramUpdate
{
	// Returns getExternalFilesDir(DIRECTORY_DOWNLOADS) + "/updates".
	// The application-specific directory needs no public-storage permission.
	std::string getApplicationUpdateDirectoryPath();

	// Asks Android PackageInstaller to install the already downloaded APK.
	// The system still presents its normal user confirmation UI.
	bool requestPackageInstall(const std::string& apkPath);
}
