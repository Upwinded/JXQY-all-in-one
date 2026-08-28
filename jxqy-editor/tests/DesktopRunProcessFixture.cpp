#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace
{
std::string descriptorMode(const std::string& bytes)
{
    if (bytes == "success" ||
        bytes == "nonzero" ||
        bytes == "crash" ||
        bytes == "hang")
    {
        return bytes;
    }

    static constexpr const char* Prefix =
        "\"sceneId\":\"fixture-";
    const std::size_t prefix =
        bytes.find(Prefix);
    if (prefix == std::string::npos)
        return {};
    const std::size_t value =
        prefix + std::char_traits<char>::length(Prefix);
    const std::size_t end = bytes.find('"', value);
    if (end == std::string::npos)
        return {};
    return bytes.substr(value, end - value);
}

void writeFragmented(
    std::FILE* stream,
    const std::string& text,
    std::size_t firstSize)
{
    std::fwrite(text.data(), 1, firstSize, stream);
    std::fflush(stream);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(80));
    std::fwrite(
        text.data() + firstSize,
        1,
        text.size() - firstSize,
        stream);
    std::fflush(stream);
}

int runFixture(
    const std::filesystem::path& descriptorPath)
{
    std::ifstream descriptor(
        descriptorPath,
        std::ios::binary);
    if (!descriptor)
        return 98;
    const std::string descriptorBytes{
        std::istreambuf_iterator<char>(descriptor),
        std::istreambuf_iterator<char>()};
    const std::string mode =
        descriptorMode(descriptorBytes);

    if (mode == "success")
    {
        const std::string output =
            u8"标准输出：中文分片\n";
        const std::string error =
            u8"错误输出：中文分片\n";
        writeFragmented(stdout, output, 1);
        writeFragmented(stderr, error, 2);
        return 0;
    }
    if (mode == "nonzero")
        return 70;
    if (mode == "crash")
    {
#ifdef _WIN32
        TerminateProcess(
            GetCurrentProcess(),
            static_cast<UINT>(0xC0000005UL));
        return 3;
#else
        std::abort();
#endif
    }
    if (mode == "hang")
    {
#ifndef _WIN32
        std::signal(SIGTERM, SIG_IGN);
#endif
        std::this_thread::sleep_for(
            std::chrono::seconds(60));
        return 0;
    }
    return 97;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (argc != 3)
    {
        return 99;
    }
#ifdef _WIN32
    if (std::wstring(argv[1]) != L"--editor-run")
        return 99;
    return runFixture(
        std::filesystem::path(argv[2]));
#else
    if (std::string(argv[1]) != "--editor-run")
        return 99;
    return runFixture(
        std::filesystem::u8path(argv[2]));
#endif
}
