#pragma once

#include <string>

// Android 外部存储（公开目录）访问桥。
// 封装 JNI 调用，供 C++ 业务层查询/申请"所有文件访问权限"及获取公开存储根路径。
// 非 Android 平台提供空实现，调用处无需 #ifdef。
namespace AndroidExternalStorage
{
	enum class ResourcePackageImportSelectionStatus
	{
		Pending,
		Selected,
		Cancelled,
		Failed
	};

	struct ResourcePackageImportSelection
	{
		ResourcePackageImportSelectionStatus status =
			ResourcePackageImportSelectionStatus::Pending;
		std::string archivePath;
		std::string errorMessage;
	};

	// 查询当前应用是否已获得访问公开外部存储的权限
	// （Android 11+ 即 MANAGE_EXTERNAL_STORAGE / isExternalStorageManager）。
	// 非 Android 平台始终返回 false。
	bool isAllFilesAccessGranted();

	// 跳转系统"所有文件访问权限"设置页，由用户手动授权。
	// 非 Android 平台为空操作。
	void requestAllFilesAccess();

	// 消费一次授权流程完成信号。返回 true 只表示权限页或权限对话框已返回；
	// 调用方仍需通过 isAllFilesAccessGranted() 获取最终授权结果。
	// 非 Android 平台始终返回 false。
	bool consumeAllFilesAccessRequestCompleted();

	// 获取公开外部存储根目录（如 /storage/emulated/0/），失败返回空。
	// 注意：访问该根下的公开子目录在 Android 11+ 需要上述权限。
	// 非 Android 平台返回空。
	std::string getPublicStorageRoot();

	// 获取固定外部资源目录的完整路径：getPublicStorageRoot() + "Download/jxqy/assets/"。
	// 供 UI 提示用户资源放置位置。非 Android 平台返回空。
	std::string getExternalResourceDirectoryPath();

	// 获取无需公共存储权限的应用专属资源集合目录：
	// getExternalFilesDir(null) + "/assets/"。在线更新和游戏内导入使用该目录；
	// 非 Android 平台返回空。
	std::string getApplicationResourceDirectoryPath();

	// 打开 Android SAF 文件选择器并将所选 ZIP 复制到应用私有缓存。
	// 返回 false 表示选择流程未能启动；非 Android 平台始终返回 false。
	bool requestResourcePackageImport();

	// 消费一次资源包选择结果。Pending 表示 Java 仍在等待用户选择或复制；
	// Selected 的 archivePath 是应用私有缓存中的普通文件路径。
	ResourcePackageImportSelection consumeResourcePackageImportSelection();
}
