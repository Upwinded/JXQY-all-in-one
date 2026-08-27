#include "log.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include "../Engine/Engine.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

#define logFileName "log.txt"

bool GameLog::use_log_file = false;

namespace
{
std::string g_logFilePath;
#if defined(JXQY_ENABLE_TEST_HOOKS)
std::mutex g_editorRunLogWriteTestHookMutex;
GameLog::EditorRunLogWriteTestHook
    g_editorRunLogWriteTestHook;
#endif

struct HeldEditorRunLog
{
    std::mutex mutex;
    std::FILE* file = nullptr;
    std::intptr_t parentToken = -1;
    std::string path;
    uint64_t generation = 0;

    ~HeldEditorRunLog()
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
        File::closeEditorRunLogParent(parentToken);
    }
};

HeldEditorRunLog g_editorRunLog;

void closeHeldEditorRunLogUnlocked()
{
    if (g_editorRunLog.file != nullptr)
    {
        std::fclose(g_editorRunLog.file);
        g_editorRunLog.file = nullptr;
    }
    File::closeEditorRunLogParent(
        g_editorRunLog.parentToken);
    g_editorRunLog.parentToken = -1;
    g_editorRunLog.path.clear();
    g_editorRunLog.generation = 0;
}

void closeHeldEditorRunLog()
{
    std::lock_guard<std::mutex> lock(g_editorRunLog.mutex);
    closeHeldEditorRunLogUnlocked();
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
void invokeEditorRunLogWriteTestHook()
{
    GameLog::EditorRunLogWriteTestHook hook;
    {
        std::lock_guard<std::mutex> lock(
            g_editorRunLogWriteTestHookMutex);
        hook = g_editorRunLogWriteTestHook;
    }
    if (hook)
    {
        hook();
    }
}
#endif

bool heldEditorRunLogMatchesCurrentPath(
    std::FILE* file,
    const std::string& fileName)
{
    return File::editorRunLogHandleIsCurrent(
        file, g_editorRunLog.parentToken,
        fileName, g_editorRunLog.generation);
}

bool openHeldEditorRunLog(
    const std::string& fileName,
    uint64_t generation)
{
    if (!File::openEditorRunLog(
            fileName, generation,
            g_editorRunLog.file,
            g_editorRunLog.parentToken))
    {
        return false;
    }
    g_editorRunLog.path = fileName;
    g_editorRunLog.generation = generation;
    if (!heldEditorRunLogMatchesCurrentPath(
            g_editorRunLog.file, fileName))
    {
        closeHeldEditorRunLogUnlocked();
        return false;
    }
    return true;
}

bool appendHeldEditorRunLog(const std::string& fileName,
    uint64_t generation,
    const std::string& info)
{
    if (fileName.empty())
    {
        return false;
    }

    File::EditorRunFileLayoutUse layoutUse(generation);
    if (!layoutUse.valid())
    {
        return false;
    }

    const auto snapshotIsCurrent =
        [&fileName, generation]()
        {
            std::string currentPath;
            uint64_t currentGeneration = 0;
            return File::getEditorRunLogPath(
                    currentPath, currentGeneration) ==
                    File::EditorRunFileLayoutState::Valid &&
                currentPath == fileName &&
                currentGeneration == generation;
        };
    if (!snapshotIsCurrent())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_editorRunLog.mutex);
    if (g_editorRunLog.file != nullptr &&
        (g_editorRunLog.path != fileName ||
         g_editorRunLog.generation != generation))
    {
        closeHeldEditorRunLogUnlocked();
    }
    if (g_editorRunLog.file != nullptr &&
        !heldEditorRunLogMatchesCurrentPath(
            g_editorRunLog.file, fileName))
    {
        // Keep the verified descriptor and parent anchor on POSIX while the
        // lexical leaf is displaced. Writes remain suppressed, and restoring
        // the exact inode lets the held logger resume without reopening an
        // untrusted existing leaf.
        return false;
    }
    if (g_editorRunLog.file == nullptr &&
        (!snapshotIsCurrent() ||
         !openHeldEditorRunLog(fileName, generation) ||
         !snapshotIsCurrent()))
    {
        closeHeldEditorRunLogUnlocked();
        return false;
    }

    if (!snapshotIsCurrent() ||
        !heldEditorRunLogMatchesCurrentPath(
            g_editorRunLog.file, fileName))
    {
        return false;
    }
#if defined(JXQY_ENABLE_TEST_HOOKS)
    invokeEditorRunLogWriteTestHook();
#endif
    if (!snapshotIsCurrent() ||
        !heldEditorRunLogMatchesCurrentPath(
            g_editorRunLog.file, fileName))
    {
        return false;
    }
    const std::size_t written = std::fwrite(
        info.data(), 1, info.size(), g_editorRunLog.file);
    const bool succeeded = written == info.size() &&
        std::fflush(g_editorRunLog.file) == 0 &&
        snapshotIsCurrent() &&
        heldEditorRunLogMatchesCurrentPath(
            g_editorRunLog.file, fileName);
    if (!succeeded)
    {
        closeHeldEditorRunLogUnlocked();
    }
    return succeeded;
}

void appendExplicitLogFile(const std::string& fileName, const std::string& info)
{
    if (fileName.empty())
    {
        return;
    }

    std::filesystem::path path = std::filesystem::u8path(fileName);
    std::filesystem::path parentPath = path.parent_path();
    if (!parentPath.empty())
    {
        std::error_code errorCode;
        std::filesystem::create_directories(parentPath, errorCode);
    }

    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (out)
    {
        out.write(info.c_str(), static_cast<std::streamsize>(info.size()));
    }
}
}

namespace GameLog
{
void editorRunFileLayoutGenerationChanged(uint64_t generation)
{
    std::lock_guard<std::mutex> lock(g_editorRunLog.mutex);
    if (g_editorRunLog.file != nullptr &&
        g_editorRunLog.generation < generation)
    {
        closeHeldEditorRunLogUnlocked();
    }
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
void setEditorRunLogWriteTestHookForTests(
    const EditorRunLogWriteTestHook& hook)
{
    std::lock_guard<std::mutex> lock(
        g_editorRunLogWriteTestHookMutex);
    g_editorRunLogWriteTestHook = hook;
}

bool editorRunLogOwnsOpenFileForTests()
{
    std::lock_guard<std::mutex> lock(
        g_editorRunLog.mutex);
    return g_editorRunLog.file != nullptr;
}
#endif
}

void GameLog::setLogFilePath(const std::string& fileName)
{
	g_logFilePath = fileName;
}

bool GameLog::initializeEditorRunLog()
{
	std::string editorRunLogPath;
	uint64_t editorRunGeneration = 0;
	if (File::getEditorRunLogPath(
			editorRunLogPath,
			editorRunGeneration) !=
			File::EditorRunFileLayoutState::Valid)
	{
		return false;
	}
	return appendHeldEditorRunLog(
		editorRunLogPath,
		editorRunGeneration,
		"[editor-run]: Log output initialized\n");
}

void GameLog::write(const char* format, ...)
{
    std::string editorRunLogPath;
    uint64_t editorRunGeneration = 0;
    const File::EditorRunFileLayoutState editorRunState =
        File::getEditorRunLogPath(
            editorRunLogPath, editorRunGeneration);
    if (editorRunState == File::EditorRunFileLayoutState::Invalid)
    {
        return;
    }
    const bool editorRunLogEnabled =
        editorRunState == File::EditorRunFileLayoutState::Valid;
    if (editorRunState == File::EditorRunFileLayoutState::NotInstalled)
    {
        closeHeldEditorRunLog();
    }

#if !defined(USE_LOG_FILE) && (!defined(USE_LOG_FILE_PARAM))
#if (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
    if (!editorRunLogEnabled)
    {
        return;
    }
#endif // (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
#endif // !defined(USE_LOG_FILE) && !defined(USE_LOG_FILE_PARAM)

#if !defined(USE_LOG_FILE) && defined(USE_LOG_FILE_PARAM)
#if (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
    if (!use_log_file && !editorRunLogEnabled)
    {
        //return;
    }
#endif // (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
#endif // !defined(USE_LOG_FILE) && !defined(USE_LOG_FILE_PARAM)

    va_list arguments;
    va_start(arguments, format);
    std::string info = convert::vformatString(format, arguments);
    va_end(arguments);
    info = "[" + std::to_string(((float)SDL_GetTicks()) / 1000.0) + "s]:" + info;
    if (info.empty() || info.back() != '\n')
    {
        info += "\n";
    }
    SDL_Log("%s", info.c_str());

    //#ifdef __ANDROID__
    //    __android_log_print(ANDROID_LOG_INFO, "native-log",  "%s", info.c_str());
    //#endif // __ANDROID__

#if defined(USE_LOG_FILE_PARAM) && !defined(USE_LOG_FILE)
    if (!use_log_file && !editorRunLogEnabled)
    {
        return;
    }
#elif !defined(USE_LOG_FILE)
    if (!editorRunLogEnabled)
    {
        return;
    }
#endif
    if (editorRunLogEnabled)
    {
        (void)appendHeldEditorRunLog(
            editorRunLogPath, editorRunGeneration, info);
    }
    else if (!g_logFilePath.empty())
    {
        const std::filesystem::path explicitPath =
            std::filesystem::u8path(g_logFilePath);
        if (explicitPath.is_absolute())
        {
            appendExplicitLogFile(g_logFilePath, info);
        }
        else
        {
            File::appendSharedApplicationFile(
                g_logFilePath, info.data(),
                static_cast<int>(info.size()));
        }
    }
    else
    {
        File::appendSharedApplicationFile(
            logFileName, info.data(),
            static_cast<int>(info.size()));
    }
}
