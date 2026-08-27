#include "MobileNetwork.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>

namespace
{
bool clearPendingException(JNIEnv* environment)
{
	if (environment == nullptr || !environment->ExceptionCheck())
	{
		return false;
	}
	environment->ExceptionClear();
	return true;
}
}
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>
#endif
#endif

namespace MobileNetwork
{
ConnectionCost getActiveConnectionCost()
{
#if defined(__ANDROID__)
	JNIEnv* environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
	if (environment == nullptr)
	{
		return ConnectionCost::Unknown;
	}
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (activity == nullptr)
	{
		clearPendingException(environment);
		return ConnectionCost::Unknown;
	}
	jclass activityClass = environment->GetObjectClass(activity);
	environment->DeleteLocalRef(activity);
	if (clearPendingException(environment) || activityClass == nullptr)
	{
		return ConnectionCost::Unknown;
	}
	const jmethodID method = environment->GetStaticMethodID(
		activityClass, "getActiveNetworkCost", "()I");
	if (clearPendingException(environment) || method == nullptr)
	{
		environment->DeleteLocalRef(activityClass);
		return ConnectionCost::Unknown;
	}
	const jint result = environment->CallStaticIntMethod(activityClass, method);
	const bool failed = clearPendingException(environment);
	environment->DeleteLocalRef(activityClass);
	if (failed)
	{
		return ConnectionCost::Unknown;
	}
	return result == 2
		? ConnectionCost::Metered
		: result == 1 ? ConnectionCost::Unmetered : ConnectionCost::Unknown;
#elif defined(__APPLE__) && TARGET_OS_IOS
	sockaddr_in address = {};
	address.sin_len = sizeof(address);
	address.sin_family = AF_INET;
	SCNetworkReachabilityRef reachability =
		SCNetworkReachabilityCreateWithAddress(
			nullptr, reinterpret_cast<const sockaddr*>(&address));
	if (reachability == nullptr)
	{
		return ConnectionCost::Unknown;
	}
	SCNetworkReachabilityFlags flags = 0;
	const bool available =
		SCNetworkReachabilityGetFlags(reachability, &flags);
	CFRelease(reachability);
	if (!available ||
		(flags & kSCNetworkReachabilityFlagsReachable) == 0 ||
		(flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0)
	{
		return ConnectionCost::Unknown;
	}
	return (flags & kSCNetworkReachabilityFlagsIsWWAN) != 0
		? ConnectionCost::Metered : ConnectionCost::Unmetered;
#else
	return ConnectionCost::Unknown;
#endif
}
}
