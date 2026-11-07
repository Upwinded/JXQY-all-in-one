#include "MacProgramUpdate.h"

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

@interface JxqySparkleUpdaterDelegate : NSObject <SPUUpdaterDelegate>

@property(nonatomic, copy) NSString* appcastUrl;

@end

@implementation JxqySparkleUpdaterDelegate

- (NSString*)feedURLStringForUpdater:(SPUUpdater*)updater
{
	(void)updater;
	return self.appcastUrl;
}

@end

namespace
{
JxqySparkleUpdaterDelegate* updaterDelegate = nil;
SPUStandardUpdaterController* updaterController = nil;

bool hasValidPublicKey()
{
	id configuredValue = [[NSBundle mainBundle]
		objectForInfoDictionaryKey:@"SUPublicEDKey"];
	if (![configuredValue isKindOfClass:[NSString class]])
	{
		return false;
	}
	NSData* decodedKey = [[NSData alloc]
		initWithBase64EncodedString:(NSString*)configuredValue
		options:0];
	return decodedKey != nil && decodedKey.length == 32;
}

NSString* validatedHttpsUrl(const std::string& value)
{
	NSString* text = [[NSString alloc]
		initWithBytes:value.data()
		length:value.size()
		encoding:NSUTF8StringEncoding];
	if (text == nil)
	{
		return nil;
	}
	NSURLComponents* components =
		[NSURLComponents componentsWithString:text];
	if (components == nil ||
		![components.scheme.lowercaseString isEqualToString:@"https"] ||
		components.host.length == 0 || components.user != nil ||
		components.password != nil || components.fragment != nil)
	{
		return nil;
	}
	return components.URL.absoluteString;
}
}

namespace MacProgramUpdate
{
bool cleanupDownloadCache()
{
	if (updaterController != nil && updaterController.updater.sessionInProgress)
	{
		return false;
	}
	NSFileManager* fileManager = [NSFileManager defaultManager];
	NSString* bundleIdentifier = [NSBundle mainBundle].bundleIdentifier;
	NSURL* cacheDirectory = [fileManager URLForDirectory:NSCachesDirectory
		inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:nil];
	if (bundleIdentifier.length == 0 || cacheDirectory == nil)
	{
		return false;
	}
	NSString* cacheIdentifier =
		[bundleIdentifier hasSuffix:@".app"] || [bundleIdentifier hasSuffix:@".APP"]
			? [bundleIdentifier stringByAppendingString:@".sparkle"] : bundleIdentifier;
	NSURL* downloadsDirectory = [[[[cacheDirectory URLByResolvingSymlinksInPath]
		URLByAppendingPathComponent:cacheIdentifier isDirectory:YES]
		URLByAppendingPathComponent:@"org.sparkle-project.Sparkle" isDirectory:YES]
		URLByAppendingPathComponent:@"PersistentDownloads" isDirectory:YES];
	if (![downloadsDirectory.path isEqualToString:
		[downloadsDirectory URLByResolvingSymlinksInPath].path])
	{
		return false;
	}
	if (![fileManager fileExistsAtPath:downloadsDirectory.path])
	{
		return true;
	}
	NSError* error = nil;
	if (![fileManager removeItemAtURL:downloadsDirectory error:&error])
	{
		NSLog(@"Program update: startup cache cleanup failed: %@", error);
		return false;
	}
	return true;
}

bool isConfigured()
{
	return hasValidPublicKey();
}

bool requestUpdateCheck(const std::string& appcastUrl)
{
	if (![NSThread isMainThread] || !isConfigured())
	{
		return false;
	}
	NSString* validatedUrl = validatedHttpsUrl(appcastUrl);
	if (validatedUrl == nil)
	{
		return false;
	}
	if (updaterDelegate == nil)
	{
		updaterDelegate = [[JxqySparkleUpdaterDelegate alloc] init];
	}
	updaterDelegate.appcastUrl = validatedUrl;
	if (updaterController == nil)
	{
		updaterController = [[SPUStandardUpdaterController alloc]
			initWithStartingUpdater:YES
			updaterDelegate:updaterDelegate
			userDriverDelegate:nil];
	}
	[updaterController checkForUpdates:nil];
	return true;
}
}
