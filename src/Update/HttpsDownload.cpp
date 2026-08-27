#include "HttpsDownload.h"

#include "../File/ResourcePathSafety.h"
#if defined(__APPLE__)
#include "../Platform/AppleHttpsDownload.h"
#endif
#include "OnlineUpdateCatalog.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#elif defined(__LINUX__)
#include <curl/curl.h>
#elif defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif

#if defined(__LINUX__) || defined(__ANDROID__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
bool startsWithHttps(std::string_view url) noexcept
{
	constexpr std::string_view Scheme = "https://";
	if (url.size() <= Scheme.size())
	{
		return false;
	}
	for (std::size_t index = 0; index < Scheme.size(); ++index)
	{
		char character = url[index];
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character + ('a' - 'A'));
		}
		if (character != Scheme[index])
		{
			return false;
		}
	}
	return true;
}

bool isPlainHttpsUrl(const std::string& url) noexcept
{
	if (!startsWithHttps(url) || url.size() > 4096 ||
		!ResourcePathSafety::isValidUtf8(url) ||
		url.find('?') != std::string::npos ||
		url.find('#') != std::string::npos ||
		url.find('\\') != std::string::npos)
	{
		return false;
	}
	const std::size_t authorityStart = sizeof("https://") - 1;
	const std::size_t pathStart = url.find('/', authorityStart);
	const std::string_view authority(
		url.data() + authorityStart,
		(pathStart == std::string::npos ? url.size() : pathStart) -
			authorityStart);
	if (authority.empty() || authority.find('@') != std::string_view::npos)
	{
		return false;
	}
	for (const unsigned char byte : url)
	{
		if (byte <= 0x20 || byte == 0x7F)
		{
			return false;
		}
	}
	return true;
}

bool isUnreservedUrlByte(unsigned char byte) noexcept
{
	return (byte >= 'a' && byte <= 'z') ||
		(byte >= 'A' && byte <= 'Z') ||
		(byte >= '0' && byte <= '9') ||
		byte == '-' || byte == '.' || byte == '_' || byte == '~';
}

std::string encodeArtifactPath(const std::string& path)
{
	constexpr char Hex[] = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(path.size());
	for (const unsigned char byte : path)
	{
		if (isUnreservedUrlByte(byte) || byte == '/')
		{
			encoded.push_back(static_cast<char>(byte));
		}
		else
		{
			encoded.push_back('%');
			encoded.push_back(Hex[byte >> 4]);
			encoded.push_back(Hex[byte & 0x0F]);
		}
	}
	return encoded;
}

#if defined(_WIN32)
class WinHttpHandle
{
public:
	WinHttpHandle() = default;
	explicit WinHttpHandle(HINTERNET value) noexcept
		: handle(value)
	{
	}
	~WinHttpHandle()
	{
		if (handle != nullptr)
		{
			WinHttpCloseHandle(handle);
		}
	}
	WinHttpHandle(const WinHttpHandle&) = delete;
	WinHttpHandle& operator=(const WinHttpHandle&) = delete;

	HINTERNET get() const noexcept
	{
		return handle;
	}
	bool valid() const noexcept
	{
		return handle != nullptr;
	}

private:
	HINTERNET handle = nullptr;
};

class WindowsFileHandle
{
public:
	explicit WindowsFileHandle(HANDLE value) noexcept
		: handle(value)
	{
	}
	~WindowsFileHandle()
	{
		close();
	}
	WindowsFileHandle(const WindowsFileHandle&) = delete;
	WindowsFileHandle& operator=(const WindowsFileHandle&) = delete;

	HANDLE get() const noexcept
	{
		return handle;
	}
	bool valid() const noexcept
	{
		return handle != INVALID_HANDLE_VALUE;
	}
	void close() noexcept
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
		}
	}

private:
	HANDLE handle = INVALID_HANDLE_VALUE;
};

bool utf8ToWide(const std::string& text, std::wstring& wide)
{
	wide.clear();
	if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX))
	{
		return false;
	}
	const int length = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		text.data(),
		static_cast<int>(text.size()),
		nullptr,
		0);
	if (length <= 0)
	{
		return false;
	}
	wide.resize(static_cast<std::size_t>(length));
	return MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		text.data(),
		static_cast<int>(text.size()),
		wide.data(),
		length) == length;
}

bool isSafeWindowsDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error || !std::filesystem::is_directory(status) ||
		std::filesystem::is_symlink(status))
	{
		return false;
	}
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

struct ParsedWindowsUrl
{
	std::wstring host;
	std::wstring requestTarget;
	INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
};

bool parseWindowsHttpsUrl(
	const std::string& url,
	ParsedWindowsUrl& parsed)
{
	if (!isPlainHttpsUrl(url))
	{
		return false;
	}
	std::wstring wideUrl;
	if (!utf8ToWide(url, wideUrl))
	{
		return false;
	}
	URL_COMPONENTS components{};
	components.dwStructSize = sizeof(components);
	components.dwSchemeLength = static_cast<DWORD>(-1);
	components.dwHostNameLength = static_cast<DWORD>(-1);
	components.dwUrlPathLength = static_cast<DWORD>(-1);
	components.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(
			wideUrl.c_str(),
			static_cast<DWORD>(wideUrl.size()),
			0,
			&components) ||
		components.nScheme != INTERNET_SCHEME_HTTPS ||
		components.dwHostNameLength == 0)
	{
		return false;
	}
	parsed.host.assign(
		components.lpszHostName, components.dwHostNameLength);
	parsed.requestTarget.assign(
		components.dwUrlPathLength == 0 ? L"/" : components.lpszUrlPath,
		components.dwUrlPathLength == 0 ? 1 : components.dwUrlPathLength);
	if (components.dwExtraInfoLength != 0)
	{
		parsed.requestTarget.append(
			components.lpszExtraInfo, components.dwExtraInfoLength);
	}
	parsed.port = components.nPort;
	return true;
}

template <typename WriteChunk>
OnlineUpdate::HttpsDownloadResult performWindowsDownload(
	const std::string& url,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const OnlineUpdate::HttpsDownloadProgress& progress,
	WriteChunk&& writeChunk)
{
	OnlineUpdate::HttpsDownloadResult result;
	ParsedWindowsUrl parsed;
	if (maximumBytes == 0 || !parseWindowsHttpsUrl(url, parsed))
	{
		result.status = maximumBytes == 0
			? OnlineUpdate::HttpsDownloadStatus::InvalidInput
			: OnlineUpdate::HttpsDownloadStatus::InvalidUrl;
		return result;
	}
	if (expectedBytes > maximumBytes)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		return result;
	}

	WinHttpHandle session(WinHttpOpen(
		L"jxqy-all-in-one/1.0",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0));
	if (!session.valid())
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = GetLastError();
		return result;
	}
	if (!WinHttpSetTimeouts(
			session.get(), 10000, 10000, 30000, 30000))
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = GetLastError();
		return result;
	}
	WinHttpHandle connection(WinHttpConnect(
		session.get(), parsed.host.c_str(), parsed.port, 0));
	if (!connection.valid())
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = GetLastError();
		return result;
	}
	WinHttpHandle request(WinHttpOpenRequest(
		connection.get(),
		L"GET",
		parsed.requestTarget.c_str(),
		nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE));
	if (!request.valid())
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = GetLastError();
		return result;
	}
	DWORD redirectPolicy =
		WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	if (!WinHttpSetOption(
			request.get(),
			WINHTTP_OPTION_REDIRECT_POLICY,
			&redirectPolicy,
			sizeof(redirectPolicy)) ||
		!WinHttpSendRequest(
			request.get(),
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			0) ||
		!WinHttpReceiveResponse(request.get(), nullptr))
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = GetLastError();
		return result;
	}
	DWORD statusCode = 0;
	DWORD statusSize = sizeof(statusCode);
	if (!WinHttpQueryHeaders(
			request.get(),
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&statusSize,
			WINHTTP_NO_HEADER_INDEX))
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = GetLastError();
		return result;
	}
	result.httpStatus = statusCode;
	if (statusCode != 200)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::HttpError;
		return result;
	}

	std::array<char, 64 * 1024> buffer{};
	for (;;)
	{
		DWORD readBytes = 0;
		if (!WinHttpReadData(
				request.get(),
				buffer.data(),
				static_cast<DWORD>(buffer.size()),
				&readBytes))
		{
			result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
			result.nativeErrorCode = GetLastError();
			return result;
		}
		if (readBytes == 0)
		{
			break;
		}
		if (readBytes > maximumBytes - result.transferredBytes)
		{
			result.status = OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
			return result;
		}
		if (!writeChunk(buffer.data(), static_cast<std::size_t>(readBytes)))
		{
			result.status = OnlineUpdate::HttpsDownloadStatus::WriteFailed;
			return result;
		}
		result.transferredBytes += readBytes;
		if (progress && !progress(result.transferredBytes, expectedBytes))
		{
			result.status = OnlineUpdate::HttpsDownloadStatus::Cancelled;
			return result;
		}
	}
	if (expectedBytes != 0 && result.transferredBytes != expectedBytes)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::SizeMismatch;
		return result;
	}
	result.status = OnlineUpdate::HttpsDownloadStatus::Success;
	return result;
}
#endif

#if defined(__LINUX__) || defined(__ANDROID__) || defined(__APPLE__)
using PlatformWriteChunk = std::function<bool(const char*, std::size_t)>;
#endif

#if defined(__LINUX__)

bool initializeCurl()
{
	static std::once_flag flag;
	static bool initialized = false;
	std::call_once(flag, []()
	{
		initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
	});
	return initialized;
}

struct CurlWriteContext
{
	const PlatformWriteChunk* writeChunk = nullptr;
	const OnlineUpdate::HttpsDownloadProgress* progress = nullptr;
	OnlineUpdate::HttpsDownloadResult* result = nullptr;
	std::uint64_t maximumBytes = 0;
	std::uint64_t expectedBytes = 0;
	OnlineUpdate::HttpsDownloadStatus callbackFailure =
		OnlineUpdate::HttpsDownloadStatus::Success;
};

std::size_t writeCurlBytes(
	char* bytes,
	std::size_t memberSize,
	std::size_t memberCount,
	void* userData)
{
	auto* context = static_cast<CurlWriteContext*>(userData);
	if (context == nullptr || context->writeChunk == nullptr ||
		context->result == nullptr || memberSize == 0 ||
		memberCount > (std::numeric_limits<std::size_t>::max)() / memberSize)
	{
		return 0;
	}
	const std::size_t byteCount = memberSize * memberCount;
	if (context->result->transferredBytes > context->maximumBytes ||
		static_cast<std::uint64_t>(byteCount) >
			context->maximumBytes - context->result->transferredBytes)
	{
		context->callbackFailure =
			OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		return 0;
	}
	if (!(*context->writeChunk)(bytes, byteCount))
	{
		context->callbackFailure =
			OnlineUpdate::HttpsDownloadStatus::WriteFailed;
		return 0;
	}
	context->result->transferredBytes +=
		static_cast<std::uint64_t>(byteCount);
	if (context->progress != nullptr && *context->progress &&
		!(*context->progress)(
			context->result->transferredBytes,
			context->expectedBytes))
	{
		context->callbackFailure =
			OnlineUpdate::HttpsDownloadStatus::Cancelled;
		return 0;
	}
	return byteCount;
}

int reportCurlTransferProgress(
	void* userData,
	curl_off_t,
	curl_off_t,
	curl_off_t,
	curl_off_t) noexcept
{
	auto* context = static_cast<CurlWriteContext*>(userData);
	if (context == nullptr || context->result == nullptr)
	{
		return 1;
	}
	bool continueTransfer = true;
	try
	{
		continueTransfer = context->progress == nullptr ||
			!*context->progress || (*context->progress)(
				context->result->transferredBytes,
				context->expectedBytes);
	}
	catch (...)
	{
		continueTransfer = false;
	}
	if (!continueTransfer)
	{
		context->callbackFailure =
			OnlineUpdate::HttpsDownloadStatus::Cancelled;
		return 1;
	}
	return 0;
}

OnlineUpdate::HttpsDownloadResult performLinuxDownload(
	const std::string& url,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const OnlineUpdate::HttpsDownloadProgress& progress,
	const PlatformWriteChunk& writeChunk)
{
	OnlineUpdate::HttpsDownloadResult result;
	if (maximumBytes == 0 || !isPlainHttpsUrl(url))
	{
		result.status = maximumBytes == 0
			? OnlineUpdate::HttpsDownloadStatus::InvalidInput
			: OnlineUpdate::HttpsDownloadStatus::InvalidUrl;
		return result;
	}
	if (expectedBytes > maximumBytes)
	{
		result.status =
			OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		return result;
	}
	if (!initializeCurl())
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = static_cast<std::uint32_t>(CURLE_FAILED_INIT);
		return result;
	}

	CURL* curl = curl_easy_init();
	if (curl == nullptr)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = static_cast<std::uint32_t>(CURLE_FAILED_INIT);
		return result;
	}
	struct CurlCloser
	{
		void operator()(CURL* handle) const noexcept
		{
			curl_easy_cleanup(handle);
		}
	};
	std::unique_ptr<CURL, CurlCloser> handle(curl);
	CurlWriteContext context;
	context.writeChunk = &writeChunk;
	context.progress = &progress;
	context.result = &result;
	context.maximumBytes = maximumBytes;
	context.expectedBytes = expectedBytes;

	const bool optionsAccepted =
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_USERAGENT,
			"jxqy-all-in-one/1.0") == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
			writeCurlBytes) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
			reportCurlTransferProgress) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
			static_cast<long>(CURLPROTO_HTTPS)) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
			static_cast<long>(CURLPROTO_HTTPS)) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK;
	if (!optionsAccepted)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = static_cast<std::uint32_t>(CURLE_FAILED_INIT);
		return result;
	}

	const CURLcode transferCode = curl_easy_perform(curl);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
	if (responseCode > 0 && responseCode <=
		static_cast<long>((std::numeric_limits<std::uint32_t>::max)()))
	{
		result.httpStatus = static_cast<std::uint32_t>(responseCode);
	}
	if (context.callbackFailure !=
		OnlineUpdate::HttpsDownloadStatus::Success)
	{
		result.status = context.callbackFailure;
		result.nativeErrorCode = static_cast<std::uint32_t>(transferCode);
		return result;
	}
	if (transferCode != CURLE_OK)
	{
		result.status = transferCode == CURLE_HTTP_RETURNED_ERROR &&
			result.httpStatus != 0 && result.httpStatus != 200
			? OnlineUpdate::HttpsDownloadStatus::HttpError
			: OnlineUpdate::HttpsDownloadStatus::NetworkError;
		result.nativeErrorCode = static_cast<std::uint32_t>(transferCode);
		return result;
	}
	if (result.httpStatus != 200)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::HttpError;
		return result;
	}
	if (expectedBytes != 0 && result.transferredBytes != expectedBytes)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::SizeMismatch;
		return result;
	}
	result.status = OnlineUpdate::HttpsDownloadStatus::Success;
	return result;
}
#endif

#if defined(__ANDROID__)
bool clearAndroidException(JNIEnv* environment)
{
	if (environment == nullptr || !environment->ExceptionCheck())
	{
		return false;
	}
	environment->ExceptionClear();
	return true;
}

class AndroidHttpsStream
{
public:
	~AndroidHttpsStream()
	{
		close();
	}

	AndroidHttpsStream() = default;
	AndroidHttpsStream(const AndroidHttpsStream&) = delete;
	AndroidHttpsStream& operator=(const AndroidHttpsStream&) = delete;

	bool open(const std::string& url)
	{
		environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
		if (environment == nullptr || url.empty() ||
			url.size() > static_cast<std::size_t>((std::numeric_limits<jsize>::max)()))
		{
			return false;
		}
		jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
		const bool activityException = clearAndroidException(environment);
		if (activity == nullptr || activityException)
		{
			return false;
		}
		jclass activityClass = environment->GetObjectClass(activity);
		environment->DeleteLocalRef(activity);
		const bool activityClassException =
			clearAndroidException(environment);
		if (activityClass == nullptr || activityClassException)
		{
			return false;
		}
		const jmethodID openMethod = environment->GetStaticMethodID(
			activityClass,
			"openHttpsDownload",
			"([B)Ljava/lang/Object;");
		const bool openMethodException = clearAndroidException(environment);
		if (openMethod == nullptr || openMethodException)
		{
			environment->DeleteLocalRef(activityClass);
			return false;
		}
		jbyteArray urlBytes = environment->NewByteArray(
			static_cast<jsize>(url.size()));
		const bool urlAllocationException =
			clearAndroidException(environment);
		if (urlBytes == nullptr || urlAllocationException)
		{
			environment->DeleteLocalRef(activityClass);
			return false;
		}
		environment->SetByteArrayRegion(
			urlBytes,
			0,
			static_cast<jsize>(url.size()),
			reinterpret_cast<const jbyte*>(url.data()));
		if (clearAndroidException(environment))
		{
			environment->DeleteLocalRef(urlBytes);
			environment->DeleteLocalRef(activityClass);
			return false;
		}
		object = environment->CallStaticObjectMethod(
			activityClass, openMethod, urlBytes);
		environment->DeleteLocalRef(urlBytes);
		environment->DeleteLocalRef(activityClass);
		const bool openException = clearAndroidException(environment);
		if (object == nullptr || openException)
		{
			if (object != nullptr)
			{
				environment->DeleteLocalRef(object);
				object = nullptr;
			}
			return false;
		}

		jclass streamClass = environment->GetObjectClass(object);
		const bool streamClassException =
			clearAndroidException(environment);
		if (streamClass == nullptr || streamClassException)
		{
			close();
			return false;
		}
		const auto getMethod =
			[this, streamClass](const char* name, const char* signature)
			{
				const jmethodID method = environment->GetMethodID(
					streamClass, name, signature);
				return clearAndroidException(environment)
					? static_cast<jmethodID>(nullptr)
					: method;
			};
		closeMethod = getMethod("close", "()V");
		httpStatusMethod = getMethod("getHttpStatus", "()I");
		contentLengthMethod = getMethod("getContentLength", "()J");
		readMethod = getMethod("read", "([B)I");
		environment->DeleteLocalRef(streamClass);
		if (httpStatusMethod == nullptr || contentLengthMethod == nullptr ||
			readMethod == nullptr || closeMethod == nullptr)
		{
			close();
			return false;
		}
		return true;
	}

	JNIEnv* getEnvironment() const noexcept
	{
		return environment;
	}

	bool getResponseMetadata(jint& httpStatus, jlong& contentLength)
	{
		if (environment == nullptr || object == nullptr ||
			httpStatusMethod == nullptr || contentLengthMethod == nullptr)
		{
			return false;
		}
		httpStatus = environment->CallIntMethod(
			object, httpStatusMethod);
		if (clearAndroidException(environment))
		{
			return false;
		}
		contentLength = environment->CallLongMethod(
			object, contentLengthMethod);
		return !clearAndroidException(environment);
	}

	int read(jbyteArray buffer)
	{
		if (environment == nullptr || object == nullptr ||
			readMethod == nullptr || buffer == nullptr)
		{
			return -2;
		}
		const jint count = environment->CallIntMethod(
			object, readMethod, buffer);
		return clearAndroidException(environment)
			? -2 : static_cast<int>(count);
	}

private:
	void close() noexcept
	{
		if (environment == nullptr || object == nullptr)
		{
			return;
		}
		if (closeMethod != nullptr)
		{
			environment->CallVoidMethod(object, closeMethod);
			clearAndroidException(environment);
		}
		environment->DeleteLocalRef(object);
		object = nullptr;
	}

	JNIEnv* environment = nullptr;
	jobject object = nullptr;
	jmethodID httpStatusMethod = nullptr;
	jmethodID contentLengthMethod = nullptr;
	jmethodID readMethod = nullptr;
	jmethodID closeMethod = nullptr;
};

OnlineUpdate::HttpsDownloadResult performAndroidDownload(
	const std::string& url,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const OnlineUpdate::HttpsDownloadProgress& progress,
	const PlatformWriteChunk& writeChunk)
{
	OnlineUpdate::HttpsDownloadResult result;
	if (maximumBytes == 0 || !isPlainHttpsUrl(url))
	{
		result.status = maximumBytes == 0
			? OnlineUpdate::HttpsDownloadStatus::InvalidInput
			: OnlineUpdate::HttpsDownloadStatus::InvalidUrl;
		return result;
	}
	if (expectedBytes > maximumBytes)
	{
		result.status =
			OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		return result;
	}

	AndroidHttpsStream stream;
	if (!stream.open(url))
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		return result;
	}
	jint httpStatus = 0;
	jlong contentLength = -1;
	if (!stream.getResponseMetadata(httpStatus, contentLength) ||
		httpStatus <= 0)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		return result;
	}
	result.httpStatus = static_cast<std::uint32_t>(httpStatus);
	if (httpStatus != 200)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::HttpError;
		return result;
	}
	if (contentLength >= 0 &&
		static_cast<std::uint64_t>(contentLength) > maximumBytes)
	{
		result.status =
			OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		return result;
	}

	JNIEnv* environment = stream.getEnvironment();
	constexpr std::size_t BufferSize = 64 * 1024;
	jbyteArray javaBuffer = environment->NewByteArray(
		static_cast<jsize>(BufferSize));
	const bool bufferAllocationException =
		clearAndroidException(environment);
	if (javaBuffer == nullptr || bufferAllocationException)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		return result;
	}
	std::array<char, BufferSize> buffer{};
	for (;;)
	{
		const int count = stream.read(javaBuffer);
		if (count == -1)
		{
			break;
		}
		if (count <= 0 ||
			static_cast<std::size_t>(count) > buffer.size())
		{
			environment->DeleteLocalRef(javaBuffer);
			result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
			return result;
		}
		if (result.transferredBytes > maximumBytes ||
			static_cast<std::uint64_t>(count) >
				maximumBytes - result.transferredBytes)
		{
			environment->DeleteLocalRef(javaBuffer);
			result.status =
				OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
			return result;
		}
		environment->GetByteArrayRegion(
			javaBuffer,
			0,
			static_cast<jsize>(count),
			reinterpret_cast<jbyte*>(buffer.data()));
		if (clearAndroidException(environment))
		{
			environment->DeleteLocalRef(javaBuffer);
			result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
			return result;
		}
		if (!writeChunk(buffer.data(), static_cast<std::size_t>(count)))
		{
			environment->DeleteLocalRef(javaBuffer);
			result.status = OnlineUpdate::HttpsDownloadStatus::WriteFailed;
			return result;
		}
		result.transferredBytes += static_cast<std::uint64_t>(count);
		if (progress && !progress(
				result.transferredBytes, expectedBytes))
		{
			environment->DeleteLocalRef(javaBuffer);
			result.status = OnlineUpdate::HttpsDownloadStatus::Cancelled;
			return result;
		}
	}
	environment->DeleteLocalRef(javaBuffer);
	if (expectedBytes != 0 && result.transferredBytes != expectedBytes)
	{
		result.status = OnlineUpdate::HttpsDownloadStatus::SizeMismatch;
		return result;
	}
	result.status = OnlineUpdate::HttpsDownloadStatus::Success;
	return result;
}
#endif

#if defined(__APPLE__)
OnlineUpdate::HttpsDownloadResult performAppleDownload(
	const std::string& url,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const OnlineUpdate::HttpsDownloadProgress& progress,
	const PlatformWriteChunk& writeChunk)
{
	OnlineUpdate::HttpsDownloadResult result;
	if (maximumBytes == 0 || !isPlainHttpsUrl(url))
	{
		result.status = maximumBytes == 0
			? OnlineUpdate::HttpsDownloadStatus::InvalidInput
			: OnlineUpdate::HttpsDownloadStatus::InvalidUrl;
		return result;
	}
	if (expectedBytes > maximumBytes)
	{
		result.status =
			OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		return result;
	}
	const AppleHttpsDownload::TransferResult transfer =
		AppleHttpsDownload::download(
			url,
			maximumBytes,
			expectedBytes,
			progress,
			writeChunk);
	result.httpStatus = transfer.httpStatus;
	result.transferredBytes = transfer.transferredBytes;
	switch (transfer.status)
	{
	case AppleHttpsDownload::TransferStatus::Success:
		result.status = OnlineUpdate::HttpsDownloadStatus::Success;
		break;
	case AppleHttpsDownload::TransferStatus::HttpError:
		result.status = OnlineUpdate::HttpsDownloadStatus::HttpError;
		break;
	case AppleHttpsDownload::TransferStatus::SizeLimitExceeded:
		result.status =
			OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded;
		break;
	case AppleHttpsDownload::TransferStatus::SizeMismatch:
		result.status = OnlineUpdate::HttpsDownloadStatus::SizeMismatch;
		break;
	case AppleHttpsDownload::TransferStatus::Cancelled:
		result.status = OnlineUpdate::HttpsDownloadStatus::Cancelled;
		break;
	case AppleHttpsDownload::TransferStatus::WriteFailed:
		result.status = OnlineUpdate::HttpsDownloadStatus::WriteFailed;
		break;
	case AppleHttpsDownload::TransferStatus::NetworkError:
	default:
		result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
		break;
	}
	return result;
}
#endif

#if defined(__LINUX__) || defined(__ANDROID__) || defined(__APPLE__)
bool isSafePosixDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	return !error && std::filesystem::is_directory(status) &&
		!std::filesystem::is_symlink(status);
}

bool writeAllToFileDescriptor(
	int descriptor,
	const char* bytes,
	std::size_t byteCount)
{
	std::size_t writtenBytes = 0;
	while (writtenBytes < byteCount)
	{
		const ssize_t count = ::write(
			descriptor,
			bytes + writtenBytes,
			byteCount - writtenBytes);
		if (count > 0)
		{
			writtenBytes += static_cast<std::size_t>(count);
			continue;
		}
		if (count < 0 && errno == EINTR)
		{
			continue;
		}
		return false;
	}
	return true;
}
#endif
}

namespace OnlineUpdate
{
bool buildHttpsArtifactUrl(
	const std::string& catalogUrl,
	const std::string& artifactPath,
	std::string& artifactUrl)
{
	artifactUrl.clear();
	if (!isPlainHttpsUrl(catalogUrl) ||
		!isSafeArtifactPath(artifactPath))
	{
		return false;
	}
	const std::size_t slash = catalogUrl.rfind('/');
	if (slash < sizeof("https://") - 1)
	{
		return false;
	}
	artifactUrl = catalogUrl.substr(0, slash + 1) +
		encodeArtifactPath(artifactPath);
	return artifactUrl.size() <= 4096;
}

HttpsBufferDownloadResult downloadHttpsToMemory(
	const std::string& url,
	std::size_t maximumBytes,
	const HttpsDownloadProgress& progress)
{
	HttpsBufferDownloadResult result;
	if (maximumBytes == 0)
	{
		result.status = HttpsDownloadStatus::InvalidInput;
		return result;
	}
	if (!isPlainHttpsUrl(url))
	{
		result.status = HttpsDownloadStatus::InvalidUrl;
		return result;
	}
#if defined(_WIN32)
	result.bytes.reserve(std::min<std::size_t>(maximumBytes, 64 * 1024));
	const HttpsDownloadResult transfer = performWindowsDownload(
		url,
		maximumBytes,
		0,
		progress,
		[&result](const char* bytes, std::size_t size)
		{
			result.bytes.insert(result.bytes.end(), bytes, bytes + size);
			return true;
		});
	static_cast<HttpsDownloadResult&>(result) = transfer;
	if (!result.succeeded())
	{
		result.bytes.clear();
	}
#elif defined(__LINUX__)
	result.bytes.reserve(std::min<std::size_t>(maximumBytes, 64 * 1024));
	const HttpsDownloadResult transfer = performLinuxDownload(
		url,
		maximumBytes,
		0,
		progress,
		[&result](const char* bytes, std::size_t size)
		{
			try
			{
				result.bytes.insert(result.bytes.end(), bytes, bytes + size);
				return true;
			}
			catch (const std::bad_alloc&)
			{
				return false;
			}
			catch (const std::length_error&)
			{
				return false;
			}
		});
	static_cast<HttpsDownloadResult&>(result) = transfer;
	if (!result.succeeded())
	{
		result.bytes.clear();
	}
#elif defined(__ANDROID__)
	result.bytes.reserve(std::min<std::size_t>(maximumBytes, 64 * 1024));
	const HttpsDownloadResult transfer = performAndroidDownload(
		url,
		maximumBytes,
		0,
		progress,
		[&result](const char* bytes, std::size_t size)
		{
			try
			{
				result.bytes.insert(result.bytes.end(), bytes, bytes + size);
				return true;
			}
			catch (const std::bad_alloc&)
			{
				return false;
			}
			catch (const std::length_error&)
			{
				return false;
			}
		});
	static_cast<HttpsDownloadResult&>(result) = transfer;
	if (!result.succeeded())
	{
		result.bytes.clear();
	}
#elif defined(__APPLE__)
	result.bytes.reserve(std::min<std::size_t>(maximumBytes, 64 * 1024));
	const HttpsDownloadResult transfer = performAppleDownload(
		url,
		maximumBytes,
		0,
		progress,
		[&result](const char* bytes, std::size_t size)
		{
			try
			{
				result.bytes.insert(result.bytes.end(), bytes, bytes + size);
				return true;
			}
			catch (const std::bad_alloc&)
			{
				return false;
			}
			catch (const std::length_error&)
			{
				return false;
			}
		});
	static_cast<HttpsDownloadResult&>(result) = transfer;
	if (!result.succeeded())
	{
		result.bytes.clear();
	}
#else
	(void)url;
	(void)maximumBytes;
	(void)progress;
	result.status = HttpsDownloadStatus::UnsupportedPlatform;
#endif
	return result;
}

HttpsDownloadResult downloadHttpsToNewFile(
	const std::string& url,
	const std::filesystem::path& destinationPath,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const HttpsDownloadProgress& progress)
{
	HttpsDownloadResult result;
	if (destinationPath.empty() || maximumBytes == 0)
	{
		result.status = HttpsDownloadStatus::InvalidInput;
		return result;
	}
	if (!isPlainHttpsUrl(url))
	{
		result.status = HttpsDownloadStatus::InvalidUrl;
		return result;
	}
	if (expectedBytes > maximumBytes)
	{
		result.status = HttpsDownloadStatus::SizeLimitExceeded;
		return result;
	}
#if defined(_WIN32)
	const std::filesystem::path parent = destinationPath.parent_path();
	if (parent.empty() || !isSafeWindowsDirectory(parent))
	{
		result.status = HttpsDownloadStatus::DestinationUnavailable;
		return result;
	}
	WindowsFileHandle output(CreateFileW(
		destinationPath.c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		nullptr));
	if (!output.valid())
	{
		result.nativeErrorCode = GetLastError();
		result.status = result.nativeErrorCode == ERROR_FILE_EXISTS ||
			result.nativeErrorCode == ERROR_ALREADY_EXISTS
			? HttpsDownloadStatus::DestinationAlreadyExists
			: HttpsDownloadStatus::DestinationUnavailable;
		return result;
	}
	DWORD writeError = ERROR_SUCCESS;
	result = performWindowsDownload(
		url,
		maximumBytes,
		expectedBytes,
		progress,
		[&](const char* bytes, std::size_t size)
		{
			DWORD writtenBytes = 0;
			const BOOL writeSucceeded = WriteFile(
					output.get(),
					bytes,
					static_cast<DWORD>(size),
					&writtenBytes,
					nullptr);
			if (!writeSucceeded || writtenBytes != size)
			{
				if (!writeSucceeded)
				{
					writeError = GetLastError();
					if (writeError == ERROR_SUCCESS)
					{
						writeError = ERROR_WRITE_FAULT;
					}
				}
				else
				{
					writeError = ERROR_WRITE_FAULT;
				}
				return false;
			}
			return true;
		});
	if (result.succeeded() && !FlushFileBuffers(output.get()))
	{
		result.status = HttpsDownloadStatus::WriteFailed;
		result.nativeErrorCode = GetLastError();
	}
	else if (writeError != ERROR_SUCCESS && result.nativeErrorCode == 0)
	{
		result.nativeErrorCode = writeError;
	}
	if (!result.succeeded())
	{
		// Close the file before deleting the partial download.
		output.close();
		if (!DeleteFileW(destinationPath.c_str()))
		{
			const DWORD cleanupError = GetLastError();
			if (cleanupError != ERROR_FILE_NOT_FOUND)
			{
				result.status = HttpsDownloadStatus::CleanupFailed;
				result.nativeErrorCode = cleanupError;
			}
		}
	}
#elif defined(__LINUX__) || defined(__ANDROID__) || defined(__APPLE__)
	const std::filesystem::path parent = destinationPath.parent_path();
	if (parent.empty() || !isSafePosixDirectory(parent))
	{
		result.status = HttpsDownloadStatus::DestinationUnavailable;
		return result;
	}
	const int descriptor = ::open(
		destinationPath.c_str(),
		O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
		S_IRUSR | S_IWUSR);
	if (descriptor < 0)
	{
		result.nativeErrorCode = static_cast<std::uint32_t>(errno);
		result.status = errno == EEXIST
			? HttpsDownloadStatus::DestinationAlreadyExists
			: HttpsDownloadStatus::DestinationUnavailable;
		return result;
	}
	int writeError = 0;
#if defined(__LINUX__)
	result = performLinuxDownload(
#elif defined(__ANDROID__)
	result = performAndroidDownload(
#else
	result = performAppleDownload(
#endif
		url,
		maximumBytes,
		expectedBytes,
		progress,
		[descriptor, &writeError](const char* bytes, std::size_t size)
		{
			if (!writeAllToFileDescriptor(descriptor, bytes, size))
			{
				writeError = errno;
				return false;
			}
			return true;
		});
	if (result.succeeded() && ::fsync(descriptor) != 0)
	{
		result.status = HttpsDownloadStatus::WriteFailed;
		result.nativeErrorCode = static_cast<std::uint32_t>(errno);
	}
	else if (writeError != 0)
	{
		result.nativeErrorCode = static_cast<std::uint32_t>(writeError);
	}
	const int closeResult = ::close(descriptor);
	if (result.succeeded() && closeResult != 0)
	{
		result.status = HttpsDownloadStatus::WriteFailed;
		result.nativeErrorCode = static_cast<std::uint32_t>(errno);
	}
	if (!result.succeeded() && ::unlink(destinationPath.c_str()) != 0 &&
		errno != ENOENT)
	{
		result.status = HttpsDownloadStatus::CleanupFailed;
		result.nativeErrorCode = static_cast<std::uint32_t>(errno);
	}
#else
	(void)url;
	(void)destinationPath;
	(void)maximumBytes;
	(void)expectedBytes;
	(void)progress;
	result.status = HttpsDownloadStatus::UnsupportedPlatform;
#endif
	return result;
}
}
