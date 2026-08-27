#pragma once

#include <cstdio>
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <stdint.h>
#include "../Types/Types.h"
#include "../Launch/EditorRunDirectoryIdentity.h"

extern "C"
{
#include <SDL3/SDL.h>
}

namespace File
{
	struct EditorRunFileLayout
	{
		std::string overlayRoot;
		std::string isolatedSaveRoot;
		std::string applicationStateRoot;
		std::string diagnosticsRoot;
		std::string diagnosticsPath;
		std::string logPath;
		std::string runtimeTracePath;
	};

	struct EditorRunFileLayoutIdentityProof
	{
		EditorRun::OutputDirectoryIdentities outputRoots;
	};

	enum class EditorRunFileLayoutState
	{
		NotInstalled,
		Valid,
		Invalid
	};

	// Holds one installed layout generation stable while an exact output writer
	// validates and writes through its verified handle. resetEditorRunFileLayout()
	// waits for all live guards before closing registered generation-owned sinks.
	class EditorRunFileLayoutUse final
	{
	public:
		explicit EditorRunFileLayoutUse(uint64_t generation);
		~EditorRunFileLayoutUse();

		EditorRunFileLayoutUse(const EditorRunFileLayoutUse&) = delete;
		EditorRunFileLayoutUse& operator=(
			const EditorRunFileLayoutUse&) = delete;
		EditorRunFileLayoutUse(EditorRunFileLayoutUse&&) = delete;
		EditorRunFileLayoutUse& operator=(
			EditorRunFileLayoutUse&&) = delete;

		bool valid() const;

	private:
		struct State;
		std::unique_ptr<State> state;
	};

	enum class DirectoryCopyPhase
	{
		BeforeBackup,
		BeforePublish
	};

	struct DirectoryCopyLimits
	{
		std::size_t maximumFileCount = 0;
		std::uint64_t maximumTotalBytes = 0;
		int maximumSingleFileBytes = 0;
		std::function<bool()> cancellationRequested;
	};

	enum class EditorRunFileOperationPhase
	{
		AfterLayoutOverlayIdentityCapture,
		BeforeReadRootOpen,
		BeforeWriteRootOpen,
		BeforeLogParentOpen,
		BeforeDiagnosticsParentOpen,
		BeforeRuntimeTraceParentOpen,
		BeforeTransactionMutation
	};

	using ResourceReadVisitor = std::function<bool(
		const std::string& resourceName, std::unique_ptr<char[]>& data, int size)>;
	using DirectoryCopyFailureInjector = std::function<bool(DirectoryCopyPhase phase)>;
#if defined(JXQY_ENABLE_TEST_HOOKS)
	using EditorRunFileOperationTestHook =
		std::function<void(EditorRunFileOperationPhase phase)>;
#endif
	using EditorRunFileLayoutResetHook = std::function<void()>;

    // Resource names are virtual paths below the active/fallback roots. A single
    // leading separator is accepted for legacy INI/script references, but parent
    // traversal, drive-qualified paths, UNC paths and alternate data streams are
    // never valid resource names.
    bool isSafeResourcePath(const std::string& fileName);

    bool fileExist(const std::string& fileName);
    // Resolve candidate names with resource roots as the outer priority:
    // active package first, then fallback packages; format/extension candidates
    // are considered only within the same root.
    std::string resolveFirstExistingResource(const std::vector<std::string>& fileNames);
    // Visit readable files in root-major order. Returning false from the visitor
    // continues to the next format candidate (and then the next dependency root),
    // which lets decoders fall back after a corrupt-but-existing candidate.
    bool visitReadableResources(const std::vector<std::string>& fileNames,
        const ResourceReadVisitor& visitor);
    bool visitReadableResources(const std::vector<std::string>& fileNames,
        int maximumBytes, const ResourceReadVisitor& visitor);
    int readFile(const std::string& fileName, std::unique_ptr<char[]>& s);
    bool readFile(const std::string& fileName, std::unique_ptr<char[]>& s, int& len);
    bool readFile(const std::string& fileName, std::unique_ptr<char[]>& s, int& len,
        int maximumBytes);
    // Read-only access to files owned by the current active package. These APIs
    // never search content dependencies, UI fallbacks, or collection common roots.
    bool activeResourceFileExist(const std::string& fileName);
    bool readActiveResourceFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len);
    bool readActiveResourceFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len, int maximumBytes);
    // Collection-level common resources are independent of the active package
    // and its dependency chain. Mobile builds can install a writable common
    // override and then fall back to the application-bundled common directory.
    bool readCommonResourceFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len);
    bool readCommonResourceFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len, int maximumBytes);
    // Bundled application assets come from the configured base collection and
    // never consult the active MOD, dependencies, collection CommonPath,
    // editor overlay, or user-writable state. Use this only for engine-owned
    // presentation assets that must remain consistent.
    bool readBundledApplicationFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len);
    bool readBundledApplicationFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len, int maximumBytes);
    // Shared application files are independent of the active resource package.
    // Every platform writes them below its user-writable application directory
    // and falls back to the bundled collection for legacy reads. Paths use the
    // same traversal-safe virtual path rules as resources.
    bool readSharedApplicationFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len);
    bool readSharedApplicationFile(const std::string& fileName,
        std::unique_ptr<char[]>& s, int& len, int maximumBytes);
    bool writeSharedApplicationFile(const std::string& fileName, const void* s, int len);
    void appendSharedApplicationFile(
        const std::string& fileName, const void* s, int len);

	// Install the immutable desktop editor-run file layout before the first file
	// log, Config::load(), or Engine::init(). Installation validates existing
	// absolute output roots and their separation, but never creates a directory.
	// While installed, ordinary writes use overlayRoot, save/** is rooted below
	// isolatedSaveRoot without a second save/ component, shared application state
	// uses applicationStateRoot exclusively, structured diagnostics use the
	// exact diagnosticsPath, and logs use the exact logPath.
	bool installEditorRunFileLayout(
		const EditorRunFileLayout& layout,
		const EditorRunFileLayoutIdentityProof& proof);
#if defined(JXQY_ENABLE_TEST_HOOKS)
	bool installEditorRunFileLayoutForTests(
		const EditorRunFileLayout& layout);
#endif
	void resetEditorRunFileLayout();
	bool hasEditorRunFileLayout();
	EditorRunFileLayoutState getEditorRunLogPath(std::string& logPath);
	EditorRunFileLayoutState getEditorRunLogPath(
		std::string& logPath, uint64_t& generation);
	EditorRunFileLayoutState getEditorRunDiagnosticsPath(
		std::string& diagnosticsPath);
	EditorRunFileLayoutState getEditorRunDiagnosticsPath(
		std::string& diagnosticsPath, uint64_t& generation);
	EditorRunFileLayoutState getEditorRunRuntimeTracePath(
		std::string& runtimeTracePath);
	EditorRunFileLayoutState getEditorRunRuntimeTracePath(
		std::string& runtimeTracePath, uint64_t& generation);
	// Exact-output owners register a weak reset callback while holding an
	// EditorRunFileLayoutUse. The callback must only close owned handles and
	// must not query or mutate the installed layout.
	uint64_t addEditorRunFileLayoutResetHook(
		const EditorRunFileLayoutResetHook& hook);
	void removeEditorRunFileLayoutResetHook(uint64_t hookId);
	// Internal bridge used by GameLog. The returned parent token remains owned by
	// the caller until closeEditorRunLogParent() and keeps the verified log
	// directory anchored while the FILE stream is held.
	bool openEditorRunLog(
		const std::string& logPath, uint64_t generation,
		std::FILE*& file, std::intptr_t& parentToken);
	bool editorRunLogHandleIsCurrent(
		std::FILE* file, std::intptr_t parentToken,
		const std::string& logPath, uint64_t generation);
	void closeEditorRunLogParent(std::intptr_t parentToken);
	// Internal bridge used by the structured editor-run diagnostics sink. It
	// has the same held-parent and exact-leaf contract as the log bridge.
	bool openEditorRunDiagnostics(
		const std::string& diagnosticsPath, uint64_t generation,
		std::FILE*& file, std::intptr_t& parentToken);
	bool editorRunDiagnosticsHandleIsCurrent(
		std::FILE* file, std::intptr_t parentToken,
		const std::string& diagnosticsPath, uint64_t generation);
	void closeEditorRunDiagnosticsParent(std::intptr_t parentToken);
	// Independent exact-output bridge used by the batched runtime-trace sink.
	bool openEditorRunRuntimeTrace(
		const std::string& runtimeTracePath, uint64_t generation,
		std::FILE*& file, std::intptr_t& parentToken);
	bool editorRunRuntimeTraceHandleIsCurrent(
		std::FILE* file, std::intptr_t parentToken,
		const std::string& runtimeTracePath, uint64_t generation);
	void closeEditorRunRuntimeTraceParent(std::intptr_t parentToken);

#if defined(JXQY_ENABLE_TEST_HOOKS)
	void setEditorRunFileOperationTestHook(
		const EditorRunFileOperationTestHook& hook);
	bool editorRunFileLayoutResetLockIsAvailableForTests();
	// Keeps shared application state tests away from the real user profile.
	// Passing an empty root restores the platform user-writable directory.
	void setSharedApplicationRootForTests(const std::string& root);
	// Simulates a platform API that cannot provide any writable application
	// directory. Shared writes must fail closed instead of using assets.
	void setSharedApplicationRootUnavailableForTests(bool unavailable);
	// Overrides the config/save parent without changing formal resource roots.
	// Passing an empty root restores the platform path policy.
	void setPlatformStateParentForTests(const std::string& root);
#endif

	// Freezes the ordinary runtime config/save collection root before Config is
	// loaded. userDataRoot is the optional --user-data-root state root. Without
	// an override, desktop builds use the directory containing assets/ so a
	// portable release contains sibling bin/, assets/, and save/ directories;
	// Android and Apple builds use the platform writable application root.
	// assetsRoot does not change save identity and neither value changes formal
	// resource lookup. Relative overrides resolve from the executable directory
	// on desktop platforms.
	bool configureUserDataRoot(
		const std::string& userDataRoot,
		const std::string& assetsRoot);
	// Returns the effective state root that directly contains save/. Successful
	// runtime configuration creates this root but not the save directory.
	std::string getUserDataRoot();

    //void readFile(const std::string& fileName, void* s, int len);
    bool writeFileChecked(const std::string& fileName, const void* s, int len);
    bool writeFileChecked(const std::string& fileName, const std::unique_ptr<char[]>& s, int len);
    void writeFile(const std::string& fileName, const void* s, int len);
    void writeFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len);
    void appendFile(const std::string& fileName, const void* s, int len);
    void appendFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len);

    void copy(const std::string & src, const std::string & dst);
    std::vector<std::string> listFiles(const std::string& directoryName);
    // Enumerates routed files while rejecting names that differ only by ASCII
    // case. Save generations must be portable between case-sensitive and
    // case-insensitive filesystems without silently dropping one file.
    bool listFilesRejectingCaseCollisions(
        const std::string& directoryName,
        std::vector<std::string>& files,
        std::string* collidingFileName = nullptr,
        std::size_t maximumFileCount = 0,
        bool* fileCountLimitExceeded = nullptr);
    bool removeFile(const std::string& fileName);
    bool clearDirectoryFiles(const std::string& directoryName);
    // Recover an interrupted sibling staging/backup transaction for a directory.
    // A verified first-save staging directory may be published; incomplete staging
    // is discarded, and an existing backup wins when the destination is missing.
    bool recoverDirectoryCopy(const std::string& destinationDirectoryName);
    // Replace a flat directory only after all source files have been staged and
    // checked. The optional injector is used by deterministic rollback tests.
    bool copyDirectoryFiles(const std::string& srcDirectoryName,
        const std::string& dstDirectoryName,
        const std::vector<std::string>& excludedFileNames = {},
        const DirectoryCopyFailureInjector& failureInjector = {},
        const DirectoryCopyLimits& limits = {});
    // Replace a sibling directory by consuming an already prepared scratch
    // directory without reading and rewriting its file contents. The scratch
    // directory is checked after it is moved to the private staging leaf. The
    // destination retains the ordinary backup/recovery guarantees, while the
    // derived scratch itself may be discarded after a process interruption.
    bool promotePreparedScratchDirectory(const std::string& srcDirectoryName,
        const std::string& dstDirectoryName,
        const DirectoryCopyFailureInjector& failureInjector = {},
        const DirectoryCopyLimits& limits = {});

    std::string getAssetsName(const std::string& fileName);

    // 设置资源集合根（--assets 参数或默认路径）。
    void setAssetsCollectionRoot(const std::string& root);

    // 设置当前活动资源包根目录（用户选择后）。
    void setActiveResourceRoot(const std::string& root);

    // 设置资源集合级公共资源根，不受当前资源包及其依赖覆盖。
    void setCommonResourceRoot(const std::string& root);

    // 设置公共资源的只读回退根。Android/Apple 使用它在可写 common 缺失
    // 文件时读取安装包内置 common；Windows/Linux 的可写根与内置根相同，
    // 因而保持为空。
    void setCommonResourceFallbackRoots(
        const std::vector<std::string>& roots);

    // 设置当前资源包的依赖资源根。普通资源读取会按顺序回退到这些根；
    // save/ 路径不会使用这些 fallback，避免 Mod 和原版存档混读。
    void setResourceFallbackRoots(const std::vector<std::string>& roots);

    // 设置 UI 资源专用回退根。ini/ui、asf/ui、mpc/ui 等 UI 路径使用此链，
    // 不会混入普通内容依赖链。preferLocal 只控制当前包与 UI 基底的先后；
    // commonRoot 始终作为最后一级兜底。
    void setUiResourceFallbackRoots(const std::vector<std::string>& roots,
        bool preferLocal = true,
        const std::string& commonRoot = "");

    // 设置当前资源包在全平台统一 save 根下使用的存档命名空间。
    // 为空时 File 层会根据 active root 推导。
    void setActiveSaveNamespace(const std::string& saveNamespace);

    // 获取当前活动资源包根目录（未设置时返回空串）。
    std::string getActiveResourceRoot();

    // 获取当前存档命名空间（未设置时返回空串）。
    std::string getActiveSaveNamespace();

    // 转换为移动端/Apple 平台实际使用的可移植目录名；保留 UTF-8 名称，
    // 并将路径分隔含义字符折叠为下划线。
    std::string sanitizeSaveNamespace(const std::string& saveNamespace);

    // 获取当前资源集合根（未设置时返回空串，File 层回退到编译期默认）。
    std::string getAssetsCollectionRoot();

    // 获取平台默认 assets 集合根目录。
    std::string getDefaultAssetsCollectionRoot();

//    template <class T> static void readDataToVector(char* data, int length, std::vector<T>& v)
//    {
//        readDataToVector(data, length, v, sizeof(T));
//    }
//
//    template <class T> static void readDataToVector(char* data, int length, std::vector<T>& v, int length_one)
//    {
//        int count = length / length_one;
//        v.resize(count);
//        for (int i = 0; i < count; i++)
//        {
//            memcpy(&v[i], data + length_one * i, length_one);
//        }
//    }
//
//    template <class T> static void readFileToVector(std::string filename, std::vector<T>& v)
//    {
//        std::unique_ptr<char[]> buffer;
//        int length;
//        if (readFile(filename, buffer, length))
//        {
//            readDataToVector(buffer, length, v);
//        }
//    }
//
//    template <class T> static void writeVectorToData(char* data, int length, std::vector<T>& v, int length_one)
//    {
//        int count = length / length_one;
//        v.resize(count);
//        for (int i = 0; i < count; i++)
//        {
//            memcpy(data + length_one * i, &v[i], length_one);
//        }
//    }

    //static std::unique_ptr<char[]> getIdxContent(std::string filename_idx, std::string filename_grp, std::vector<int>* offset, std::vector<int>* length);
};
