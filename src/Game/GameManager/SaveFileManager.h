#pragma once
#include <vector>
#include "../../File/INIReader.h"
#include "../GameTypes.h"
#include "../../libconvert/libconvert.h"
#include "SaveGeneration.h"
#include <functional>
#include <memory>
#include <climits>
#include <mutex>
#include <string>

class SaveFileManager
{
public:
	class OperationScope
	{
	public:
		OperationScope() :
			operationLock(_operationMutex)
		{
		}

		OperationScope(const OperationScope&) = delete;
		OperationScope& operator=(
			const OperationScope&) = delete;
		OperationScope(OperationScope&&) = delete;
		OperationScope& operator=(
			OperationScope&&) = delete;

	private:
		std::unique_lock<std::recursive_mutex> operationLock;
	};

	class CurrentPathScope
	{
	public:
		explicit CurrentPathScope(
			const std::string& generationDirectory);
		~CurrentPathScope();

		CurrentPathScope(const CurrentPathScope&) = delete;
		CurrentPathScope& operator=(
			const CurrentPathScope&) = delete;
		CurrentPathScope(CurrentPathScope&&) = delete;
		CurrentPathScope& operator=(
			CurrentPathScope&&) = delete;

		bool valid() const
		{
			return active;
		}

	private:
		std::unique_lock<std::recursive_mutex> pathLock;
		std::string previousPath;
		bool active = false;
	};

	SaveFileManager() {}
	virtual ~SaveFileManager() {}

private:
	static std::string calculateFolderName(int index);
	inline static std::recursive_mutex _operationMutex;
	inline static std::string _currentPath = SAVE_CURRENT_FOLDER;
	inline static std::recursive_mutex _currentPathMutex;
public:
	// CopySaveFileTo仅接受1至7，对应手动存档槽。
	// CopySaveFileFrom接受0至7；0读取资源包的ini/save模板，1至7读取手动槽。
	static bool CopySaveFileTo(int index, const std::function<bool()>& cancellationRequested = {});
	static bool CopySaveFileFrom(int index);
	static bool CopySaveFileToAuto(const std::function<bool()>& cancellationRequested = {});
	static bool CopySaveFileFromAuto();
	static bool HasSaveFile(int index);
	static bool ClearAllSaveData();
	static bool IsSafeEntityListFileName(
		const std::string& fileName);
	static bool AreEntityListFileNamesDistinct(
		const std::string& npcFileName,
		const std::string& objectFileName);
	static std::string CurrentPath()
	{
		std::lock_guard<std::recursive_mutex> lock(
			_currentPathMutex);
		return _currentPath;
	}
	static void AppendFile(const std::string & fileName);

	static const char* DescribeSaveGenerationError(
		SaveGenerationError error)
	{
		return SaveGeneration::DescribeError(error);
	}

	// Called at game initialization to recover transactions left by older builds.
	// Normal save/load operations and slot queries do not perform recovery.
	static bool RecoverInterruptedSaveOperations();

	//按优先级读取 NPC/OBJ INI 文件内容
	//优先 save\game\，失败回退 ini\save\
	//返回 true 表示文件存在且读取成功；空文件的 len 为 0，data 仍有效
	static bool ReadNpcObjFile(const std::string& fileName,
	                           std::unique_ptr<char[]>& data,
	                           int& len,
	                           std::string* loadedPath = nullptr,
	                           int maximumBytes = INT_MAX);
};
