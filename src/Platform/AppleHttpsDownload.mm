#include "AppleHttpsDownload.h"

#import <Foundation/Foundation.h>

#include <condition_variable>
#include <mutex>

namespace
{
constexpr NSUInteger MaximumRedirectCount = 5;
}

@interface JxqyHttpsDownloadDelegate : NSObject
	<NSURLSessionDataDelegate, NSURLSessionTaskDelegate>
{
@private
	std::mutex _completionMutex;
	std::condition_variable _completionCondition;
	bool _completed;
	bool _hasPendingStatus;
	NSUInteger _redirectCount;
	std::uint64_t _maximumBytes;
	std::uint64_t _expectedBytes;
	AppleHttpsDownload::ProgressCallback _progress;
	AppleHttpsDownload::WriteCallback _write;
	AppleHttpsDownload::TransferResult _result;
}

- (instancetype)initWithMaximumBytes:(std::uint64_t)maximumBytes
	expectedBytes:(std::uint64_t)expectedBytes
	progress:(const AppleHttpsDownload::ProgressCallback&)progress
	write:(const AppleHttpsDownload::WriteCallback&)write;
- (AppleHttpsDownload::TransferResult)waitForCompletion;

@end

@implementation JxqyHttpsDownloadDelegate

- (instancetype)initWithMaximumBytes:(std::uint64_t)maximumBytes
	expectedBytes:(std::uint64_t)expectedBytes
	progress:(const AppleHttpsDownload::ProgressCallback&)progress
	write:(const AppleHttpsDownload::WriteCallback&)write
{
	self = [super init];
	if (self != nil)
	{
		_completed = false;
		_hasPendingStatus = false;
		_redirectCount = 0;
		_maximumBytes = maximumBytes;
		_expectedBytes = expectedBytes;
		_progress = progress;
		_write = write;
	}
	return self;
}

- (void)completeWithStatus:(AppleHttpsDownload::TransferStatus)status
{
	{
		std::lock_guard<std::mutex> lock(_completionMutex);
		if (_completed)
		{
			return;
		}
		_result.status = status;
		_completed = true;
	}
	_completionCondition.notify_all();
}

- (void)setPendingStatus:(AppleHttpsDownload::TransferStatus)status
{
	if (!_hasPendingStatus)
	{
		_result.status = status;
		_hasPendingStatus = true;
	}
}

- (AppleHttpsDownload::TransferResult)waitForCompletion
{
	std::unique_lock<std::mutex> lock(_completionMutex);
	_completionCondition.wait(lock, [self]
	{
		return self->_completed;
	});
	return _result;
}

- (void)URLSession:(NSURLSession*)session
	task:(NSURLSessionTask*)task
	willPerformHTTPRedirection:(NSHTTPURLResponse*)response
	newRequest:(NSURLRequest*)request
	completionHandler:(void (^)(NSURLRequest* _Nullable))completionHandler
{
	(void)session;
	(void)task;
	(void)response;
	NSURL* redirectUrl = request.URL;
	if (_redirectCount >= MaximumRedirectCount || redirectUrl == nil ||
		![redirectUrl.scheme.lowercaseString isEqualToString:@"https"])
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::NetworkError];
		completionHandler(nil);
		return;
	}
	++_redirectCount;
	completionHandler(request);
}

- (void)URLSession:(NSURLSession*)session
	dataTask:(NSURLSessionDataTask*)dataTask
	didReceiveResponse:(NSURLResponse*)response
	completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
	(void)session;
	(void)dataTask;
	if (![response isKindOfClass:[NSHTTPURLResponse class]])
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::NetworkError];
		completionHandler(NSURLSessionResponseCancel);
		return;
	}
	NSHTTPURLResponse* httpResponse =
		(NSHTTPURLResponse*)response;
	_result.httpStatus = static_cast<std::uint32_t>(
		httpResponse.statusCode);
	if (httpResponse.statusCode != 200)
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::HttpError];
		completionHandler(NSURLSessionResponseCancel);
		return;
	}
	const long long contentLength = response.expectedContentLength;
	if (contentLength >= 0 &&
		static_cast<std::uint64_t>(contentLength) > _maximumBytes)
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::SizeLimitExceeded];
		completionHandler(NSURLSessionResponseCancel);
		return;
	}
	completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session
	dataTask:(NSURLSessionDataTask*)dataTask
	didReceiveData:(NSData*)data
{
	(void)session;
	if (_hasPendingStatus)
	{
		return;
	}
	const std::size_t size = data.length;
	if (_result.transferredBytes > _maximumBytes ||
		size > _maximumBytes - _result.transferredBytes)
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::SizeLimitExceeded];
		[dataTask cancel];
		return;
	}
	if (size != 0 && !_write(
		static_cast<const char*>(data.bytes), size))
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::WriteFailed];
		[dataTask cancel];
		return;
	}
	_result.transferredBytes += static_cast<std::uint64_t>(size);
	if (_progress && !_progress(
		_result.transferredBytes, _expectedBytes))
	{
		[self setPendingStatus:
			AppleHttpsDownload::TransferStatus::Cancelled];
		[dataTask cancel];
	}
}

- (void)URLSession:(NSURLSession*)session
	task:(NSURLSessionTask*)task
	didCompleteWithError:(NSError*)error
{
	(void)session;
	(void)task;
	{
		std::lock_guard<std::mutex> lock(_completionMutex);
		if (_completed)
		{
			return;
		}
	}
	if (_hasPendingStatus)
	{
		[self completeWithStatus:_result.status];
		return;
	}
	if (error != nil)
	{
		[self completeWithStatus:
			AppleHttpsDownload::TransferStatus::NetworkError];
		return;
	}
	if (_expectedBytes != 0 &&
		_result.transferredBytes != _expectedBytes)
	{
		[self completeWithStatus:
			AppleHttpsDownload::TransferStatus::SizeMismatch];
		return;
	}
	[self completeWithStatus:
		AppleHttpsDownload::TransferStatus::Success];
}

@end

AppleHttpsDownload::TransferResult AppleHttpsDownload::download(
	const std::string& url,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const ProgressCallback& progress,
	const WriteCallback& write)
{
	TransferResult invalidResult;
	if (url.empty() || maximumBytes == 0 || expectedBytes > maximumBytes ||
		!write)
	{
		return invalidResult;
	}
	@autoreleasepool
	{
		NSString* urlText = [[NSString alloc]
			initWithBytes:url.data()
			length:url.size()
			encoding:NSUTF8StringEncoding];
		NSURL* requestUrl = urlText == nil ? nil :
			[NSURL URLWithString:urlText];
		if (requestUrl == nil ||
			![requestUrl.scheme.lowercaseString isEqualToString:@"https"])
		{
			return invalidResult;
		}

		JxqyHttpsDownloadDelegate* delegate =
			[[JxqyHttpsDownloadDelegate alloc]
				initWithMaximumBytes:maximumBytes
				expectedBytes:expectedBytes
				progress:progress
				write:write];
		if (delegate == nil)
		{
			return invalidResult;
		}
		NSOperationQueue* delegateQueue = [[NSOperationQueue alloc] init];
		delegateQueue.maxConcurrentOperationCount = 1;
		NSURLSessionConfiguration* configuration =
			[NSURLSessionConfiguration ephemeralSessionConfiguration];
		configuration.timeoutIntervalForRequest = 30.0;
		configuration.timeoutIntervalForResource = 24.0 * 60.0 * 60.0;
		configuration.HTTPMaximumConnectionsPerHost = 1;
		NSURLSession* session = [NSURLSession
			sessionWithConfiguration:configuration
			delegate:delegate
			delegateQueue:delegateQueue];
		if (session == nil)
		{
			return invalidResult;
		}
		NSMutableURLRequest* request =
			[NSMutableURLRequest requestWithURL:requestUrl];
		request.HTTPMethod = @"GET";
		[request setValue:@"jxqy-all-in-one/1.0"
			forHTTPHeaderField:@"User-Agent"];
		NSURLSessionDataTask* task = [session dataTaskWithRequest:request];
		if (task == nil)
		{
			[session invalidateAndCancel];
			return invalidResult;
		}
		[task resume];
		const TransferResult result = [delegate waitForCompletion];
		[session finishTasksAndInvalidate];
		return result;
	}
}
