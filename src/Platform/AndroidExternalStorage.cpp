#include "AndroidExternalStorage.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#include <atomic>
#include <mutex>
#include "../File/log.h"

namespace
{
// 缓存的 JNI 绑定：jclass 为全局引用（进程级，不释放），jmethodID 在类生命周期内有效。
// 只有所有方法都成功解析后才发布缓存；初始化失败时后续调用会重试。
struct CachedBindings
{
	jclass activityClass = nullptr;
	jmethodID isGrantedMethod = nullptr;
	jmethodID requestMethod = nullptr;
	jmethodID consumeRequestCompletedMethod = nullptr;
	jmethodID getRootMethod = nullptr;
	jmethodID getApplicationResourceDirectoryMethod = nullptr;
	jmethodID requestResourcePackageImportMethod = nullptr;
	jmethodID consumeResourcePackageImportResultMethod = nullptr;
	bool valid = false;
};

std::mutex bindingsMutex;
CachedBindings cachedBindings;
std::atomic<bool> nativeRequestCompleted{false};

// 获取当前线程的 JNIEnv；SDL_GetAndroidJNIEnv 会自动 attach 当前线程。
JNIEnv* getJNIEnv()
{
	return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
}

// 清理待处理的 Java 异常，避免后续 JNI 调用被污染。
bool clearPendingException(JNIEnv* env, const char* context)
{
	if (env == nullptr)
	{
		return false;
	}
	if (env->ExceptionCheck())
	{
		env->ExceptionDescribe();
		env->ExceptionClear();
		GameLog::write("AndroidExternalStorage: JNI exception at %s\n",
			context != nullptr ? context : "(unknown)");
		return true;
	}
	return false;
}

jmethodID getStaticMethod(
	JNIEnv* env,
	jclass activityClass,
	const char* methodName,
	const char* signature)
{
	jmethodID method = env->GetStaticMethodID(
		activityClass, methodName, signature);
	if (clearPendingException(env, methodName))
	{
		return nullptr;
	}
	return method;
}

bool ensureBindings(CachedBindings& result)
{
	std::lock_guard<std::mutex> lock(bindingsMutex);
	if (cachedBindings.valid)
	{
		result = cachedBindings;
		return true;
	}

	JNIEnv* env = getJNIEnv();
	if (env == nullptr)
	{
		return false;
	}

	// SDL 返回的是当前 Activity 的局部引用。由实际对象取类可避免非 Java
	// 线程上的类加载器不一致，也不依赖硬编码包名。
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (activity == nullptr)
	{
		clearPendingException(env, "SDL_GetAndroidActivity");
		return false;
	}
	jclass localClass = env->GetObjectClass(activity);
	bool classLookupFailed = clearPendingException(env, "GetObjectClass(Activity)");
	env->DeleteLocalRef(activity);
	if (classLookupFailed || localClass == nullptr)
	{
		return false;
	}

	CachedBindings candidate;
	candidate.isGrantedMethod = getStaticMethod(
		env, localClass, "isAllFilesAccessGranted", "()Z");
	candidate.requestMethod = getStaticMethod(
		env, localClass, "requestAllFilesAccess", "()V");
	candidate.consumeRequestCompletedMethod = getStaticMethod(
		env, localClass, "consumeAllFilesAccessRequestCompleted", "()Z");
	candidate.getRootMethod = getStaticMethod(
		env, localClass, "getPublicStorageRoot", "()Ljava/lang/String;");
	candidate.getApplicationResourceDirectoryMethod = getStaticMethod(
		env, localClass, "getApplicationResourceDirectoryPath",
		"()Ljava/lang/String;");
	candidate.requestResourcePackageImportMethod = getStaticMethod(
		env, localClass, "requestResourcePackageImport", "()Z");
	candidate.consumeResourcePackageImportResultMethod = getStaticMethod(
		env, localClass, "consumeResourcePackageImportResult",
		"()Ljava/lang/String;");
	if (candidate.isGrantedMethod == nullptr
		|| candidate.requestMethod == nullptr
		|| candidate.consumeRequestCompletedMethod == nullptr
		|| candidate.getRootMethod == nullptr
		|| candidate.getApplicationResourceDirectoryMethod == nullptr
		|| candidate.requestResourcePackageImportMethod == nullptr
		|| candidate.consumeResourcePackageImportResultMethod == nullptr)
	{
		env->DeleteLocalRef(localClass);
		return false;
	}

	candidate.activityClass =
		static_cast<jclass>(env->NewGlobalRef(localClass));
	bool globalReferenceFailed = clearPendingException(env, "NewGlobalRef(Activity class)");
	env->DeleteLocalRef(localClass);
	if (globalReferenceFailed || candidate.activityClass == nullptr)
	{
		if (candidate.activityClass != nullptr)
		{
			env->DeleteGlobalRef(candidate.activityClass);
		}
		return false;
	}

	candidate.valid = true;
	cachedBindings = candidate;
	result = cachedBindings;
	return true;
}

std::string callStaticStringMethod(
	const CachedBindings& bindings,
	jmethodID method,
	const char* context)
{
	if (method == nullptr)
	{
		return "";
	}
	JNIEnv* env = getJNIEnv();
	if (env == nullptr)
	{
		return "";
	}
	jstring result = static_cast<jstring>(env->CallStaticObjectMethod(
		bindings.activityClass, method));
	if (clearPendingException(env, context))
	{
		if (result != nullptr)
		{
			env->DeleteLocalRef(result);
		}
		return "";
	}
	if (result == nullptr)
	{
		return "";
	}
	const char* chars = env->GetStringUTFChars(result, nullptr);
	if (clearPendingException(env, "GetStringUTFChars") || chars == nullptr)
	{
		env->DeleteLocalRef(result);
		return "";
	}
	std::string value(chars);
	env->ReleaseStringUTFChars(result, chars);
	env->DeleteLocalRef(result);
	return value;
}
}

namespace AndroidExternalStorage
{
bool isAllFilesAccessGranted()
{
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		return false;
	}
	JNIEnv* env = getJNIEnv();
	if (env == nullptr)
	{
		return false;
	}
	jboolean result = env->CallStaticBooleanMethod(
		bindings.activityClass, bindings.isGrantedMethod);
	if (clearPendingException(
		env, "CallStaticBooleanMethod(isAllFilesAccessGranted)"))
	{
		return false;
	}
	return result != JNI_FALSE;
}

void requestAllFilesAccess()
{
	nativeRequestCompleted.store(false, std::memory_order_release);
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		nativeRequestCompleted.store(true, std::memory_order_release);
		return;
	}
	JNIEnv* env = getJNIEnv();
	if (env == nullptr)
	{
		nativeRequestCompleted.store(true, std::memory_order_release);
		return;
	}
	env->CallStaticVoidMethod(
		bindings.activityClass, bindings.requestMethod);
	if (clearPendingException(
		env, "CallStaticVoidMethod(requestAllFilesAccess)"))
	{
		nativeRequestCompleted.store(true, std::memory_order_release);
	}
}

bool consumeAllFilesAccessRequestCompleted()
{
	if (nativeRequestCompleted.exchange(false, std::memory_order_acq_rel))
	{
		return true;
	}
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		return false;
	}
	JNIEnv* env = getJNIEnv();
	if (env == nullptr)
	{
		return false;
	}
	jboolean result = env->CallStaticBooleanMethod(
		bindings.activityClass, bindings.consumeRequestCompletedMethod);
	if (clearPendingException(
		env,
		"CallStaticBooleanMethod(consumeAllFilesAccessRequestCompleted)"))
	{
		// JNI 调用已失败，等待方无法再从 Java 取得可靠状态；结束本轮等待，
		// 由调用方重新查询实际授权结果。
		return true;
	}
	return result != JNI_FALSE;
}

std::string getPublicStorageRoot()
{
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		return "";
	}
	return callStaticStringMethod(
		bindings,
		bindings.getRootMethod,
		"CallStaticObjectMethod(getPublicStorageRoot)");
}

std::string getExternalResourceDirectoryPath()
{
	std::string root = getPublicStorageRoot();
	if (root.empty())
	{
		return "";
	}
	if (root.back() != '/')
	{
		root += '/';
	}
	root += "Download/jxqy/assets/";
	return root;
}

std::string getApplicationResourceDirectoryPath()
{
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		return "";
	}
	std::string root = callStaticStringMethod(
		bindings,
		bindings.getApplicationResourceDirectoryMethod,
		"CallStaticObjectMethod(getApplicationResourceDirectoryPath)");
	if (!root.empty() && root.back() != '/')
	{
		root += '/';
	}
	return root;
}

bool requestResourcePackageImport()
{
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		return false;
	}
	JNIEnv* env = getJNIEnv();
	if (env == nullptr)
	{
		return false;
	}
	const jboolean requested = env->CallStaticBooleanMethod(
		bindings.activityClass,
		bindings.requestResourcePackageImportMethod);
	if (clearPendingException(
		env, "CallStaticBooleanMethod(requestResourcePackageImport)"))
	{
		return false;
	}
	return requested != JNI_FALSE;
}

ResourcePackageImportSelection consumeResourcePackageImportSelection()
{
	ResourcePackageImportSelection selection;
	CachedBindings bindings;
	if (!ensureBindings(bindings))
	{
		return selection;
	}
	const std::string result = callStaticStringMethod(
		bindings,
		bindings.consumeResourcePackageImportResultMethod,
		"CallStaticObjectMethod(consumeResourcePackageImportResult)");
	if (result.empty())
	{
		return selection;
	}
	if (result == "cancelled")
	{
		selection.status = ResourcePackageImportSelectionStatus::Cancelled;
		return selection;
	}
	constexpr const char SelectedPrefix[] = "selected\n";
	constexpr const char ErrorPrefix[] = "error\n";
	if (result.compare(0, sizeof(SelectedPrefix) - 1, SelectedPrefix) == 0)
	{
		selection.archivePath = result.substr(sizeof(SelectedPrefix) - 1);
		selection.status = selection.archivePath.empty()
			? ResourcePackageImportSelectionStatus::Failed
			: ResourcePackageImportSelectionStatus::Selected;
		if (selection.status == ResourcePackageImportSelectionStatus::Failed)
		{
			selection.errorMessage = u8"Android 未返回资源包路径";
		}
		return selection;
	}
	selection.status = ResourcePackageImportSelectionStatus::Failed;
	selection.errorMessage =
		result.compare(0, sizeof(ErrorPrefix) - 1, ErrorPrefix) == 0
			? result.substr(sizeof(ErrorPrefix) - 1)
			: std::string(u8"Android 返回了无效的资源包选择结果");
	return selection;
}
}

#else // 非 Android 平台：空实现。

namespace AndroidExternalStorage
{
bool isAllFilesAccessGranted() { return false; }
void requestAllFilesAccess() {}
bool consumeAllFilesAccessRequestCompleted() { return false; }
std::string getPublicStorageRoot() { return ""; }
std::string getExternalResourceDirectoryPath() { return ""; }
std::string getApplicationResourceDirectoryPath() { return ""; }
bool requestResourcePackageImport() { return false; }
ResourcePackageImportSelection consumeResourcePackageImportSelection()
{
	return {};
}
}

#endif
