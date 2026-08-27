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

	class ScratchGenerationScope
	{
	public:
		explicit ScratchGenerationScope(
			const std::string& generationDirectory);
		~ScratchGenerationScope();

		ScratchGenerationScope(
			const ScratchGenerationScope&) = delete;
		ScratchGenerationScope& operator=(
			const ScratchGenerationScope&) = delete;
		ScratchGenerationScope(
			ScratchGenerationScope&&) = delete;
		ScratchGenerationScope& operator=(
			ScratchGenerationScope&&) = delete;

		bool valid() const
		{
			return active;
		}

		// Preserve the generation only when automated rollback is incomplete
		// and the files may be needed for manual recovery.
		void preserve()
		{
			active = false;
		}

	private:
		std::string directory;
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
	static bool CopySaveFileTo(int index);
	static bool CopySaveFileFrom(int index);
	static bool CopySaveFileToAuto();
	static bool CopySaveFileFromAuto();
	static bool HasSaveFile(int index);
	static bool ClearAllSaveData();
	static std::string CurrentPath()
	{
		std::lock_guard<std::recursive_mutex> lock(
			_currentPathMutex);
		return _currentPath;
	}
	static void AppendFile(const std::string & fileName);

	// Preflight is confined to a virtual save/** generation. It recovers any
	// interrupted File directory-copy transaction, applies caller-supplied
	// compatibility rules, and never copies into the live save directory.
	static SaveGenerationResult PreflightSaveGeneration(
		const std::string& sourceDirectory,
		const SaveGenerationPreflightPolicy& policy)
	{
		return SaveGeneration::Preflight(
			sourceDirectory, policy);
	}

	// Publish replaces destinationDirectory through File::copyDirectoryFiles().
	// Both directories must remain below the virtual save root so editor-run
	// sessions cannot escape their isolated save directory.
	static SaveGenerationResult PublishSaveGeneration(
		const std::string& sourceDirectory,
		const std::string& destinationDirectory,
		const SaveGenerationPreflightPolicy& policy,
		const std::vector<std::string>& excludedFileNames = {})
	{
		return SaveGeneration::Publish(
			sourceDirectory,
			destinationDirectory,
			policy,
			excludedFileNames);
	}

	static const char* DescribeSaveGenerationError(
		SaveGenerationError error)
	{
		return SaveGeneration::DescribeError(error);
	}

	// Clone a flat generation into an internal scratch directory while enforcing
	// the configured resource ceilings. Callers prepare any resources they need
	// from the stable scratch snapshot before publishing it.
	static bool CopySaveGenerationWithinLimits(
		const std::string& sourceDirectory,
		const std::string& destinationDirectory,
		const SaveGenerationLimits& limits,
		const std::vector<std::string>& excludedFileNames = {},
		const std::function<bool()>& cancellationRequested = {});

	// Consumes and atomically promotes the already prepared internal load
	// candidate to save/game without a second full read/write pass. Candidate
	// creation is responsible for excluding the legacy list.ini before commit.
	// Resource parsing belongs to the prepare stage; this narrow commit path
	// checks bounded filesystem/transaction requirements, game.ini presence and
	// the absence of the already excluded legacy list, then replaces the current
	// generation.
	static SaveGenerationResult
		PublishPreparedLoadCandidateToCurrent(
			const SaveGenerationLimits& limits,
			const std::function<bool()>&
				cancellationRequested = {});

	// Publishes a generated draft to one save destination. Each destination is
	// replaced independently by File's ordinary directory transaction; a slot
	// failure never makes save/game part of a larger cross-directory rollback.
	static SaveGenerationResult PublishPreparedSaveGeneration(
			const std::string& draftDirectory,
			const std::string& destinationDirectory,
			const SaveGenerationLimits& limits,
			const std::vector<std::string>& excludedFileNames = {},
			const std::function<bool()>&
				cancellationRequested = {});

	// Recovers File's per-directory copy transactions. Failure is reported to
	// the caller but is not a reason to terminate the whole application; the
	// affected slot can be retried or ignored independently.
	static bool RecoverInterruptedSaveOperations();

	//按优先级读取 NPC/OBJ INI 文件内容
	//优先 save\game\，失败回退 ini\save\
	//返回 true 表示成功读取，data 和 len 为文件内容
	static bool ReadNpcObjFile(const std::string& fileName,
	                           std::unique_ptr<char[]>& data,
	                           int& len,
	                           std::string* loadedPath = nullptr,
	                           int maximumBytes = INT_MAX);
};
