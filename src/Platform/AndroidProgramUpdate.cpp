#include "AndroidProgramUpdate.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>

#include <limits>

#include "../File/log.h"

namespace
{
bool clearPendingException(JNIEnv* environment, const char* context)
{
	if (environment == nullptr || !environment->ExceptionCheck())
	{
		return false;
	}
	environment->ExceptionDescribe();
	environment->ExceptionClear();
	GameLog::write(
		"AndroidProgramUpdate: JNI exception at %s\n",
		context == nullptr ? "(unknown)" : context);
	return true;
}

struct ActivityBinding
{
	JNIEnv* environment = nullptr;
	jclass activityClass = nullptr;

	~ActivityBinding()
	{
		if (environment != nullptr && activityClass != nullptr)
		{
			environment->DeleteLocalRef(activityClass);
		}
	}

	bool initialize()
	{
		environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
		if (environment == nullptr)
		{
			return false;
		}
		jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
		if (activity == nullptr)
		{
			clearPendingException(environment, "SDL_GetAndroidActivity");
			return false;
		}
		activityClass = environment->GetObjectClass(activity);
		const bool failed = clearPendingException(
			environment, "GetObjectClass(Activity)");
		environment->DeleteLocalRef(activity);
		return !failed && activityClass != nullptr;
	}
};
}

namespace AndroidProgramUpdate
{
std::string getApplicationUpdateDirectoryPath()
{
	ActivityBinding binding;
	if (!binding.initialize())
	{
		return {};
	}
	const jmethodID method = binding.environment->GetStaticMethodID(
		binding.activityClass,
		"getApplicationProgramUpdateDirectoryPath",
		"()Ljava/lang/String;");
	if (clearPendingException(binding.environment, "GetStaticMethodID(update path)") ||
		method == nullptr)
	{
		return {};
	}
	jstring value = static_cast<jstring>(
		binding.environment->CallStaticObjectMethod(
			binding.activityClass, method));
	if (clearPendingException(binding.environment, "CallStaticObjectMethod(update path)") ||
		value == nullptr)
	{
		if (value != nullptr)
		{
			binding.environment->DeleteLocalRef(value);
		}
		return {};
	}
	const char* characters =
		binding.environment->GetStringUTFChars(value, nullptr);
	if (clearPendingException(binding.environment, "GetStringUTFChars(update path)") ||
		characters == nullptr)
	{
		binding.environment->DeleteLocalRef(value);
		return {};
	}
	std::string result(characters);
	binding.environment->ReleaseStringUTFChars(value, characters);
	binding.environment->DeleteLocalRef(value);
	return result;
}

bool requestPackageInstall(const std::string& apkPath)
{
	if (apkPath.empty() ||
		apkPath.size() > static_cast<std::size_t>(
			std::numeric_limits<jsize>::max()))
	{
		return false;
	}
	ActivityBinding binding;
	if (!binding.initialize())
	{
		return false;
	}
	const jmethodID method = binding.environment->GetStaticMethodID(
		binding.activityClass,
		"requestProgramPackageInstall",
		"([B)Z");
	if (clearPendingException(binding.environment, "GetStaticMethodID(install APK)") ||
		method == nullptr)
	{
		return false;
	}
	jbyteArray bytes = binding.environment->NewByteArray(
		static_cast<jsize>(apkPath.size()));
	if (clearPendingException(binding.environment, "NewByteArray(APK path)") ||
		bytes == nullptr)
	{
		return false;
	}
	binding.environment->SetByteArrayRegion(
		bytes,
		0,
		static_cast<jsize>(apkPath.size()),
		reinterpret_cast<const jbyte*>(apkPath.data()));
	if (clearPendingException(binding.environment, "SetByteArrayRegion(APK path)"))
	{
		binding.environment->DeleteLocalRef(bytes);
		return false;
	}
	const jboolean requested = binding.environment->CallStaticBooleanMethod(
		binding.activityClass, method, bytes);
	const bool failed = clearPendingException(
		binding.environment, "CallStaticBooleanMethod(install APK)");
	binding.environment->DeleteLocalRef(bytes);
	return !failed && requested != JNI_FALSE;
}
}

#else

namespace AndroidProgramUpdate
{
std::string getApplicationUpdateDirectoryPath() { return {}; }
bool requestPackageInstall(const std::string&) { return false; }
}

#endif
