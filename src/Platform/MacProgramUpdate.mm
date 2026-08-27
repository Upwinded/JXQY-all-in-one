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
