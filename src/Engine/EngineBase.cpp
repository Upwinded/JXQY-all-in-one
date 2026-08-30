#include <algorithm>
#include <map>
#include <iostream>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <new>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#include <resource.h>
#define USE_LOGO_RESOURCE
#endif

#include "EngineBase.h"
#include "AspectFitLayout.h"
#include "SaveShotSafety.h"
#include "AudioDecodeSafety.h"
#include "DesktopCursorDpiPolicy.h"
#include "LogicalResolutionPolicy.h"
#include "../Image/SafeImageDecoder.h"
#include "../Image/PngImageEncoder.h"
#include "../Input/PhysicalInputManager.h"

#ifdef __ANDROID__
#include "../File/INIReader.h"
#include "../Game/GameTypes.h"
#endif

#include "stdio.h"
#include "Engine.h"

#ifdef SHF_USE_AUDIO
std::vector<SoundAutoRelease_t> EngineBase::soundList;
#ifdef SHF_USE_VIDEO
std::vector<_video> EngineBase::videoList;
#endif // SHF_USE_VIDEO
#endif // SHF_USE_AUDIO

std::atomic<uint32_t> EngineBase::ImageCount(0);
std::mutex EngineBase::_mutex;
std::atomic<bool> EngineBase::multiThreadedMode(false);
AppEventHandler EngineBase::_externalEventHandler = NULL;
std::atomic<SDL_Renderer*> EngineBase::renderer = nullptr;
std::atomic<bool> EngineBase::isBackGround = false;
Timer EngineBase::timer;

#if !defined(__MOBILE__)
namespace
{
SDL_DisplayID resolveDesktopDisplayID(int displayIndex)
{
	int displayCount = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
	if (displays == nullptr || displayCount <= 0)
	{
		SDL_free(displays);
		return SDL_GetPrimaryDisplay();
	}
	const int resolvedIndex =
		displayIndex >= 0 && displayIndex < displayCount ? displayIndex : 0;
	const SDL_DisplayID displayID = displays[resolvedIndex];
	SDL_free(displays);
	return displayID;
}

int resolveDesktopDisplayIndex(SDL_DisplayID displayID)
{
	int displayCount = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
	if (displays == nullptr || displayCount <= 0)
	{
		SDL_free(displays);
		return 0;
	}
	int displayIndex = 0;
	for (int index = 0; index < displayCount; index++)
	{
		if (displays[index] == displayID)
		{
			displayIndex = index;
			break;
		}
	}
	SDL_free(displays);
	return displayIndex;
}
}
#endif

#if defined(SHF_USE_AUDIO) && defined(SHF_USE_VIDEO)
int AudioDecodeSafety::readPacket(void* opaque, uint8_t* buffer, int bufferSize)
{
	auto* reader = static_cast<MemoryReader*>(opaque);
	if (reader == nullptr || reader->data == nullptr || buffer == nullptr || bufferSize <= 0 ||
		reader->size < 0 || reader->position < 0 || reader->position > reader->size)
	{
		return AVERROR_EOF;
	}
	int64_t remaining = reader->size - reader->position;
	if (remaining <= 0)
	{
		return AVERROR_EOF;
	}
	int readSize = static_cast<int>(remaining < bufferSize ? remaining : bufferSize);
	memcpy(buffer, reader->data + reader->position, readSize);
	reader->position += readSize;
	return readSize;
}

int64_t AudioDecodeSafety::seekPacket(void* opaque, int64_t offset, int whence)
{
	auto* reader = static_cast<MemoryReader*>(opaque);
	if (reader == nullptr || reader->size < 0 || reader->position < 0 ||
		reader->position > reader->size)
	{
		return -1;
	}
	if (whence == AVSEEK_SIZE)
	{
		return reader->size;
	}
#ifdef AVSEEK_FORCE
	whence &= ~AVSEEK_FORCE;
#endif

	int64_t base = 0;
	switch (whence)
	{
	case SEEK_SET:
		break;
	case SEEK_CUR:
		base = reader->position;
		break;
	case SEEK_END:
		base = reader->size;
		break;
	default:
		return -1;
	}
	if (offset < -base || offset > reader->size - base)
	{
		return -1;
	}
	reader->position = base + offset;
	return reader->position;
}
#endif

namespace
{
constexpr UTime LogoPhaseDurationMilliseconds = 750;
constexpr UTime LogoFadeDurationMilliseconds = 400;
constexpr UTime LogoFullOpacityDurationMilliseconds =
	LogoPhaseDurationMilliseconds - LogoFadeDurationMilliseconds;

constexpr unsigned char calculateLogoFadeInAlpha(UTime elapsedMilliseconds)
{
	const UTime fadeElapsed = (std::min)(
		elapsedMilliseconds,
		LogoFadeDurationMilliseconds);
	return static_cast<unsigned char>(
		255ULL * fadeElapsed / LogoFadeDurationMilliseconds);
}

constexpr unsigned char calculateLogoFadeOutAlpha(UTime elapsedMilliseconds)
{
	if (elapsedMilliseconds <= LogoFullOpacityDurationMilliseconds)
	{
		return 255;
	}
	const UTime fadeElapsed = (std::min)(
		elapsedMilliseconds - LogoFullOpacityDurationMilliseconds,
		LogoFadeDurationMilliseconds);
	return static_cast<unsigned char>(
		255ULL * (LogoFadeDurationMilliseconds - fadeElapsed) /
		LogoFadeDurationMilliseconds);
}

static_assert(calculateLogoFadeInAlpha(0) == 0);
static_assert(calculateLogoFadeInAlpha(400) == 255);
static_assert(calculateLogoFadeInAlpha(750) == 255);
static_assert(calculateLogoFadeOutAlpha(0) == 255);
static_assert(calculateLogoFadeOutAlpha(350) == 255);
static_assert(calculateLogoFadeOutAlpha(750) == 0);

void retainTextureUntilProcessExit(
	_shared_image texture)
{
	if (texture == nullptr)
	{
		return;
	}
	// If SDL refuses even a null render target, destroying the still-bound
	// texture would be unsafe. This path represents a broken renderer; retain
	// the texture for the remaining process lifetime instead of risking UAF.
	static auto* retainedTextures =
		new std::vector<_shared_image>();
	retainedTextures->push_back(
		std::move(texture));
}

SDL_ScaleMode logicalScreenScaleMode()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	return SDL_SCALEMODE_PIXELART;
#else
	// SDL_SCALEMODE_PIXELART was added in SDL 3.4. Keep older platform
	// dependencies buildable while preserving crisp pixel edges.
	return SDL_SCALEMODE_NEAREST;
#endif
}

bool configureLogicalScreenTexture(SDL_Texture* texture)
{
	if (texture == nullptr)
	{
		return false;
	}
	if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE))
	{
		GameLog::write("SDL_SetTextureBlendMode Error(logical screen): %s", SDL_GetError());
		return false;
	}
	if (!SDL_SetTextureScaleMode(texture, logicalScreenScaleMode()))
	{
		GameLog::write("SDL_SetTextureScaleMode Error(logical screen): %s", SDL_GetError());
		return false;
	}
	return true;
}

#if defined(__LINUX__)
constexpr const char* LinuxApplicationId = "com.jxqy.jxqy-all-in-one";
constexpr const char* LinuxIconName = "jxqy-all-in-one";
constexpr const char* LinuxWindowIconFileName = "jxqy-all-in-one.png";

void configureLinuxApplicationMetadata(const std::string& applicationName)
{
	if (!SDL_SetAppMetadata(
		applicationName.c_str(), nullptr, LinuxApplicationId))
	{
		GameLog::write(
			"SDL application metadata setup failed: %s\n",
			SDL_GetError());
	}
	SDL_SetHint(SDL_HINT_APP_ID, LinuxApplicationId);
	SDL_SetHint(SDL_HINT_APP_NAME, applicationName.c_str());
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_ICON_NAME, LinuxIconName);
}

void setLinuxWindowIcon(SDL_Window* window)
{
	if (window == nullptr)
	{
		return;
	}
	const char* basePath = SDL_GetBasePath();
	if (basePath == nullptr || *basePath == '\0')
	{
		GameLog::write(
			"SDL application icon base path is unavailable: %s\n",
			SDL_GetError());
		return;
	}
	std::string iconPath(basePath);
	if (!iconPath.empty() && iconPath.back() != '/')
	{
		iconPath.push_back('/');
	}
	iconPath += LinuxWindowIconFileName;
	SDL_Surface* icon = IMG_Load(iconPath.c_str());
	if (icon == nullptr)
	{
		GameLog::write(
			"SDL application icon load failed (%s): %s\n",
			iconPath.c_str(), SDL_GetError());
		return;
	}
	if (!SDL_SetWindowIcon(window, icon))
	{
		GameLog::write(
			"SDL application window icon setup failed: %s\n",
			SDL_GetError());
	}
	SDL_DestroySurface(icon);
}
#endif

#if defined(SHF_USE_AUDIO)
bool sdlMixerInitialized = false;
MIX_Mixer* sdlMixer = nullptr;
#endif

#if defined(SHF_USE_AUDIO) && defined(SHF_USE_VIDEO)
int findStreamInfoWithinDecodeBudgets(AVFormatContext* formatContext)
{
	if (formatContext == nullptr)
	{
		return AVERROR(EINVAL);
	}
	std::vector<AVDictionary*> codecOptions(formatContext->nb_streams, nullptr);
	for (size_t streamIndex = 0; streamIndex < formatContext->nb_streams; streamIndex++)
	{
		AVStream* stream = formatContext->streams[streamIndex];
		if (stream == nullptr || stream->codecpar == nullptr)
		{
			continue;
		}
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			av_dict_set_int(&codecOptions[streamIndex], "max_pixels",
				static_cast<int64_t>(VideoStruct::MaxDecodedVideoPixels), 0);
		}
#if (!defined USE_FFMPEG4)
		else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			av_dict_set_int(&codecOptions[streamIndex], "max_samples",
				AudioDecodeSafety::MaxAudioFrameSamples, 0);
		}
#endif
	}
	int result = avformat_find_stream_info(formatContext,
		codecOptions.empty() ? nullptr : codecOptions.data());
	for (auto& options : codecOptions)
	{
		av_dict_free(&options);
	}
	return result;
}

int getAudioBytesPerMs(const SDL_AudioSpec& spec)
{
	int bytesPerFrame = SDL_AUDIO_BYTESIZE(spec.format) * spec.channels;
	if (bytesPerFrame <= 0 || spec.freq <= 0)
	{
		return 0;
	}
	return (std::max)(1, (spec.freq * bytesPerFrame) / 1000);
}

int getAudioDurationMs(const SDL_AudioSpec& spec, int bytes)
{
	int bytesPerMs = getAudioBytesPerMs(spec);
	if (bytesPerMs <= 0)
	{
		return 0;
	}
	return bytes / bytesPerMs;
}

int getSafeAudioChannelCount(int channels)
{
	if (channels <= 1)
	{
		return 1;
	}
	return 2;
}

#if (defined USE_FFMPEG4)
int64_t getDefaultAudioChannelLayout(int channels)
{
	switch (getSafeAudioChannelCount(channels))
	{
	case 1:
		return AV_CH_LAYOUT_MONO;
	default:
		return AV_CH_LAYOUT_STEREO;
	}
}
#else
int getCodecAudioChannelCount(AVCodecContext* codecContext)
{
	if (codecContext == nullptr || codecContext->ch_layout.nb_channels <= 0)
	{
		return 2;
	}
	return codecContext->ch_layout.nb_channels;
}

void setDefaultAudioChannelLayout(AVChannelLayout& layout, int channels)
{
	std::memset(&layout, 0, sizeof(layout));
	av_channel_layout_default(&layout, getSafeAudioChannelCount(channels));
}
#endif

bool appendConvertedFrame(SwrContext* swrContext, AVCodecContext* codecContext, AVFrame* frame, AudioBuffer& audio)
{
	if (swrContext == nullptr || codecContext == nullptr || frame == nullptr ||
		codecContext->sample_rate <= 0 ||
		codecContext->sample_rate > AudioDecodeSafety::MaxAudioSampleRate ||
		frame->nb_samples <= 0 ||
		frame->nb_samples > AudioDecodeSafety::MaxAudioFrameSamples)
	{
		return false;
	}

	int64_t resamplerDelay = swr_get_delay(swrContext, codecContext->sample_rate);
	if (resamplerDelay < 0 || resamplerDelay > INT64_MAX - frame->nb_samples)
	{
		return false;
	}
	int64_t outputSampleCount = av_rescale_rnd(
		resamplerDelay + frame->nb_samples,
		audio.spec.freq,
		codecContext->sample_rate,
		AV_ROUND_UP);
	if (outputSampleCount <= 0 || outputSampleCount > INT_MAX)
	{
		return false;
	}
	int outputSamples = static_cast<int>(outputSampleCount);

	int maxOutputSize = av_samples_get_buffer_size(nullptr, audio.spec.channels, outputSamples, AV_SAMPLE_FMT_S16, 1);
	if (maxOutputSize <= 0)
	{
		return false;
	}

	size_t offset = audio.data.size();
	if (!AudioDecodeSafety::canAppendDecodedBytes(offset,
		static_cast<std::size_t>(maxOutputSize)))
	{
		return false;
	}
	try
	{
		audio.data.resize(offset + static_cast<std::size_t>(maxOutputSize));
	}
	catch (const std::bad_alloc&)
	{
		return false;
	}
	catch (const std::length_error&)
	{
		return false;
	}
	uint8_t* outputData[1] = { audio.data.data() + offset };
	int convertedSamples = swr_convert(
		swrContext,
		outputData,
		outputSamples,
		const_cast<const uint8_t**>(frame->data),
		frame->nb_samples);
	if (convertedSamples <= 0)
	{
		audio.data.resize(offset);
		return false;
	}

	int convertedSize = av_samples_get_buffer_size(nullptr, audio.spec.channels, convertedSamples, AV_SAMPLE_FMT_S16, 1);
	if (convertedSize <= 0 || convertedSize > maxOutputSize)
	{
		audio.data.resize(offset);
		return false;
	}
	audio.data.resize(offset + static_cast<std::size_t>(convertedSize));
	return true;
}

bool decodeAudioFromMemory(const uint8_t* data, int size, bool loop, bool positional, AudioBuffer& audio)
{
	audio = {};
	if (data == nullptr || size <= 0 ||
		static_cast<std::size_t>(size) > AudioDecodeSafety::MaxEncodedAudioBytes)
	{
		return false;
	}
	AudioDecodeSafety::MemoryReader reader;
	reader.data = data;
	reader.size = size;

	AVFormatContext* formatContext = avformat_alloc_context();
	AVIOContext* avioContext = nullptr;
	unsigned char* avioBuffer = static_cast<unsigned char*>(av_malloc(32768));
	AVCodecContext* codecContext = nullptr;
	SwrContext* swrContext = nullptr;
	AVPacket* packet = nullptr;
	AVFrame* frame = nullptr;
	int streamIndex = -1;
	AVStream* stream = nullptr;
	const AVCodec* codec = nullptr;
	bool success = false;

	if (formatContext == nullptr || avioBuffer == nullptr)
	{
		goto cleanup;
	}

	avioContext = avio_alloc_context(avioBuffer, 32768, 0, &reader,
		AudioDecodeSafety::readPacket, nullptr, AudioDecodeSafety::seekPacket);
	if (avioContext == nullptr)
	{
		goto cleanup;
	}
	avioBuffer = nullptr;
	formatContext->pb = avioContext;
	formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

	if (avformat_open_input(&formatContext, nullptr, nullptr, nullptr) < 0)
	{
		goto cleanup;
	}
	if (findStreamInfoWithinDecodeBudgets(formatContext) < 0)
	{
		goto cleanup;
	}

	streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (streamIndex < 0)
	{
		goto cleanup;
	}

	{
		stream = formatContext->streams[streamIndex];
		codec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (codec == nullptr)
		{
			goto cleanup;
		}
		codecContext = avcodec_alloc_context3(codec);
		if (codecContext == nullptr)
		{
			goto cleanup;
		}
		if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0)
		{
			goto cleanup;
		}
#if (!defined USE_FFMPEG4)
		codecContext->max_samples = AudioDecodeSafety::MaxAudioFrameSamples;
#endif
		if (avcodec_open2(codecContext, codec, nullptr) < 0)
		{
			goto cleanup;
		}

		audio.spec.format = SDL_AUDIO_S16;
		audio.spec.channels = 2;
		if (codecContext->sample_rate <= 0 ||
			codecContext->sample_rate > AudioDecodeSafety::MaxAudioSampleRate)
		{
			goto cleanup;
		}
		audio.spec.freq = codecContext->sample_rate;
		audio.loop = loop;
		audio.positional = positional;

#if (defined USE_FFMPEG4)
		swrContext = swr_alloc();
		if (swrContext == nullptr)
		{
			goto cleanup;
		}
		int64_t inputChannelLayout = codecContext->channel_layout;
		if (inputChannelLayout == 0)
		{
			inputChannelLayout = av_get_default_channel_layout(codecContext->channels > 0 ? codecContext->channels : 2);
		}
		inputChannelLayout = getDefaultAudioChannelLayout(codecContext->channels > 0 ? codecContext->channels : 2);
		av_opt_set_int(swrContext, "in_channel_layout", inputChannelLayout, 0);
		av_opt_set_int(swrContext, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
#else
		AVChannelLayout inputChannelLayout;
		AVChannelLayout outputChannelLayout;
		setDefaultAudioChannelLayout(inputChannelLayout, getCodecAudioChannelCount(codecContext));
		setDefaultAudioChannelLayout(outputChannelLayout, audio.spec.channels);
		if (swr_alloc_set_opts2(&swrContext,
			&outputChannelLayout, AV_SAMPLE_FMT_S16, audio.spec.freq,
			&inputChannelLayout, codecContext->sample_fmt, codecContext->sample_rate,
			0, nullptr) < 0)
		{
			av_channel_layout_uninit(&inputChannelLayout);
			av_channel_layout_uninit(&outputChannelLayout);
			goto cleanup;
		}
#endif
#if (defined USE_FFMPEG4)
		av_opt_set_int(swrContext, "in_sample_rate", codecContext->sample_rate, 0);
		av_opt_set_int(swrContext, "out_sample_rate", audio.spec.freq, 0);
		av_opt_set_sample_fmt(swrContext, "in_sample_fmt", codecContext->sample_fmt, 0);
		av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
#endif

		if (swr_init(swrContext) < 0)
		{
#if (!defined USE_FFMPEG4)
			av_channel_layout_uninit(&inputChannelLayout);
			av_channel_layout_uninit(&outputChannelLayout);
#endif
			goto cleanup;
		}
#if (!defined USE_FFMPEG4)
		av_channel_layout_uninit(&inputChannelLayout);
		av_channel_layout_uninit(&outputChannelLayout);
#endif
	}

	packet = av_packet_alloc();
	frame = av_frame_alloc();
	if (packet == nullptr || frame == nullptr)
	{
		goto cleanup;
	}

	while (av_read_frame(formatContext, packet) >= 0)
	{
		if (packet->stream_index == streamIndex && avcodec_send_packet(codecContext, packet) == 0)
		{
			while (avcodec_receive_frame(codecContext, frame) == 0)
			{
				if (!appendConvertedFrame(swrContext, codecContext, frame, audio))
				{
					av_frame_unref(frame);
					goto cleanup;
				}
				av_frame_unref(frame);
			}
		}
		av_packet_unref(packet);
	}
	avcodec_send_packet(codecContext, nullptr);
	while (avcodec_receive_frame(codecContext, frame) == 0)
	{
		if (!appendConvertedFrame(swrContext, codecContext, frame, audio))
		{
			av_frame_unref(frame);
			goto cleanup;
		}
		av_frame_unref(frame);
	}

	audio.durationMs = getAudioDurationMs(audio.spec, static_cast<int>(audio.data.size()));
	success = !audio.data.empty();

cleanup:
	if (packet != nullptr)
	{
		av_packet_free(&packet);
	}
	if (frame != nullptr)
	{
		av_frame_free(&frame);
	}
	if (swrContext != nullptr)
	{
		swr_free(&swrContext);
	}
	if (codecContext != nullptr)
	{
		avcodec_free_context(&codecContext);
	}
	if (formatContext != nullptr)
	{
		avformat_close_input(&formatContext);
	}
	if (avioContext != nullptr)
	{
		av_freep(&avioContext->buffer);
		avio_context_free(&avioContext);
	}
	if (avioBuffer != nullptr)
	{
		av_free(avioBuffer);
	}
	if (!success)
	{
		audio = {};
	}
	return success;
}

bool loadRawMixerAudio(AudioBuffer& audio)
{
	if (sdlMixer == nullptr || audio.data.empty())
	{
		return false;
	}

	audio.audio = MIX_LoadRawAudio(sdlMixer, audio.data.data(), audio.data.size(), &audio.spec);
	if (audio.audio == nullptr)
	{
		return false;
	}

	std::vector<uint8_t>().swap(audio.data);
	return true;
}

float getChannelGain(const AudioChannel* channel)
{
	if (channel == nullptr)
	{
		return 0.0f;
	}
	float gain = channel->volume;
	if (channel->music != nullptr && channel->music->positional)
	{
		float distance = std::sqrt(channel->positionX * channel->positionX + channel->positionY * channel->positionY);
		gain *= 1.0f / (1.0f + distance / 5000.0f);
	}
	return (std::max)(0.0f, gain);
}

void destroyChannelStream(AudioChannel* channel)
{
	if (channel == nullptr)
	{
		return;
	}
	if (channel->track != nullptr)
	{
		MIX_StopTrack(channel->track, 0);
		MIX_DestroyTrack(channel->track);
		channel->track = nullptr;
	}
	channel->playing = false;
	channel->paused = false;
	channel->stopped = true;
}

void updateChannelTrackGain(AudioChannel* channel)
{
	if (channel == nullptr || channel->track == nullptr)
	{
		return;
	}
	MIX_SetTrackGain(channel->track, getChannelGain(channel));
}

bool startChannel(AudioChannel* channel)
{
	if (channel == nullptr || channel->music == nullptr || channel->music->audio == nullptr || sdlMixer == nullptr)
	{
		return false;
	}
	channel->track = MIX_CreateTrack(sdlMixer);
	if (channel->track == nullptr)
	{
		return false;
	}
	if (!MIX_SetTrackAudio(channel->track, channel->music->audio))
	{
		destroyChannelStream(channel);
		return false;
	}
	updateChannelTrackGain(channel);

	SDL_PropertiesID options = 0;
	if (channel->loop)
	{
		options = SDL_CreateProperties();
		if (options != 0)
		{
			SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
		}
	}

	bool played = MIX_PlayTrack(channel->track, options);
	if (options != 0)
	{
		SDL_DestroyProperties(options);
	}
	if (!played)
	{
		destroyChannelStream(channel);
		return false;
	}

	channel->playing = true;
	channel->paused = false;
	channel->stopped = false;
	return true;
}
#endif

bool isTextureWithinImageBudget(SDL_Texture* texture)
{
	if (texture == nullptr)
	{
		return false;
	}
	float width = 0.0f;
	float height = 0.0f;
	if (!SDL_GetTextureSize(texture, &width, &height) ||
		!std::isfinite(width) || !std::isfinite(height) ||
		width < 1.0f || height < 1.0f ||
		width > static_cast<float>(EncodedImageSafety::MaxDecodedImagePixels) ||
		height > static_cast<float>(EncodedImageSafety::MaxDecodedImagePixels))
	{
		return false;
	}
	return EncodedImageSafety::isDecodedPixelCountSafe(
		static_cast<std::uint64_t>(std::ceil(width)),
		static_cast<std::uint64_t>(std::ceil(height)));
}
}

#if defined(SHF_USE_AUDIO) && defined(SHF_USE_VIDEO)
bool AudioDecodeSafety::decodeFromMemory(const uint8_t* data, int size, bool loop,
	bool positional, AudioBuffer& audio)
{
	return decodeAudioFromMemory(data, size, loop, positional, audio);
}
#endif

_shared_image EngineBase::createImageFromPixelData(const uint8_t* pixelData, int width, int height)
{
	if (pixelData == nullptr || width <= 0 || height <= 0 ||
		!EncodedImageSafety::isDecodedPixelCountSafe(
			static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height)))
	{
		return nullptr;
	}

	auto texture = make_safe_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height));
	if (texture == nullptr)
	{
		return nullptr;
	}

	int pitch = width * 4;
	if (!SDL_UpdateTexture(texture.get(), nullptr, pixelData, pitch))
	{
		GameLog::write("SDL_UpdateTexture Error(image): %s", SDL_GetError());
		return nullptr;
	}
	SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
	return texture;
}

EngineBase::EngineBase()
{
	realMousePosX = -1;
	realMousePosY = -1;
	physicalInputManager =
		std::make_unique<GameInput::PhysicalInputManager>(
#ifdef __MOBILE__
			true
#else
			false
#endif
		);
}

EngineBase::~EngineBase()
{
	destroyEngineBase();
}

const GameInput::PhysicalInputManager& EngineBase::inputActions() const
{
	return *physicalInputManager;
}

bool EngineBase::consumeInputAction(GameInput::InputAction inputAction)
{
	return physicalInputManager->consumePressed(inputAction);
}

void EngineBase::releasePhysicalInputsForContextTransition()
{
	physicalInputManager->releaseForContextTransition();
}

void EngineBase::applyPhysicalInputLifecycleRequest()
{
	const PhysicalInputLifecycleRequest request =
		physicalInputLifecycleRequest.exchange(PhysicalInputLifecycleRequest::None);
	switch (request)
	{
	case PhysicalInputLifecycleRequest::Suspend:
		physicalInputManager->suspendInput();
		break;
	case PhysicalInputLifecycleRequest::Resume:
		physicalInputManager->resumeInput();
		break;
	case PhysicalInputLifecycleRequest::None:
	default:
		break;
	}
}

void EngineBase::clearCursor()
{
	cursorImage.nativeAnimatedCursor = nullptr;
	cursorImage.image.resize(0);
	cursorImage.interval = 0;
	CursorImageIndex = -1;
}

bool EngineBase::activateNativeAnimatedCursor()
{
	if (cursorImage.nativeAnimatedCursor == nullptr)
	{
		return false;
	}
	if (SDL_SetCursor(cursorImage.nativeAnimatedCursor.get()))
	{
		return true;
	}
	GameLog::write(
		"Engine: failed to activate native animated cursor: %s\n",
		SDL_GetError());
	cursorImage.nativeAnimatedCursor = nullptr;
	return false;
}

void EngineBase::drawCursor()
{
	if (softwareCursorHidden)
	{
		return;
	}
	if (hardwareCursor &&
		cursorImage.nativeAnimatedCursor != nullptr)
	{
		return;
	}
	int frameIndex = -1;
	if (cursorImage.interval <= 0)
	{
		if (cursorImage.image.size() >= 1)
		{
			frameIndex = 0;
		}
	}
	else
	{
		if (cursorImage.image.size() >= 1)
		{
			frameIndex = ((int)(getTime() / cursorImage.interval)) % cursorImage.image.size();
		}	
	}
	if (CursorImageIndex == frameIndex && hardwareCursor)
	{
		return;
	}
	if (frameIndex >= 0 && frameIndex < (int)cursorImage.image.size())
	{
		if (hardwareCursor)
		{
			if (SDL_SetCursor(
					cursorImage.image[frameIndex].frame.get()))
			{
				CursorImageIndex = frameIndex;
			}
			else
			{
				CursorImageIndex = -1;
			}
		}
		else
		{
            auto drawMouseX = realMousePosX;
            auto drawMouseY = realMousePosY;
#ifdef __APPLE__
            int w, h, rw, rh;
            SDL_GetWindowSize(window, &w, &h);
            SDL_GetWindowSizeInPixels(window, &rw, &rh);
            drawMouseX *= rw / w;
            drawMouseY *= rh / h;
			Rect dst;
			dst.x = drawMouseX - cursorImage.image[frameIndex].xOffset;
			dst.y = drawMouseY - cursorImage.image[frameIndex].yOffset;
			getImageSize(cursorImage.image[frameIndex].softwareFrame, dst.w, dst.h);
			dst.w *= rw / w;
			dst.h *= rh / h;
			drawImage(cursorImage.image[frameIndex].softwareFrame, nullptr, &dst);
#else
			drawImage(cursorImage.image[frameIndex].softwareFrame, drawMouseX - cursorImage.image[frameIndex].xOffset, drawMouseY - cursorImage.image[frameIndex].yOffset);
#endif
			CursorImageIndex = frameIndex;
		}
	}
	
}

void EngineBase::setCursorImage(CursorImage * mouse)
{
	if (hardwareCursor && !softwareCursorHidden)
	{
		SDL_SetCursor(SDL_GetDefaultCursor());
	}
	clearCursor();
	if (mouse == nullptr)
	{
		return;
	}
	cursorImage.nativeAnimatedCursor =
		mouse->nativeAnimatedCursor;
	cursorImage.image.resize(mouse->image.size());
	for (size_t i = 0; i < cursorImage.image.size(); i++)
	{
		cursorImage.image[i].xOffset = mouse->image[i].xOffset;
		cursorImage.image[i].yOffset = mouse->image[i].yOffset;
		cursorImage.image[i].frame = mouse->image[i].frame;	
		cursorImage.image[i].softwareFrame = mouse->image[i].softwareFrame;
	}
	cursorImage.interval = mouse->interval;

	if (hardwareCursor && !softwareCursorHidden && !cursorImage.image.empty())
	{
		if (!activateNativeAnimatedCursor())
		{
			CursorImageIndex = SDL_SetCursor(
				cursorImage.image[0].frame.get())
				? 0
				: -1;
		}
	}

}

void EngineBase::showCursor()
{
	softwareCursorHidden = false;
	if (hardwareCursor)
	{
		if (activateNativeAnimatedCursor())
		{
			SDL_ShowCursor();
			return;
		}
		int frameIndex = CursorImageIndex;
		if (frameIndex < 0 || frameIndex >= static_cast<int>(cursorImage.image.size()))
		{
			frameIndex = cursorImage.image.empty() ? -1 : 0;
		}
		if (frameIndex >= 0 && cursorImage.image[frameIndex].frame != nullptr)
		{
			CursorImageIndex = SDL_SetCursor(
				cursorImage.image[frameIndex].frame.get())
				? frameIndex
				: -1;
		}
		SDL_ShowCursor();
	}
}

void EngineBase::hideCursor()
{
	if (hiddenCursor == nullptr)
	{
		const Uint8 transparentCursorData[8] = {};
		const Uint8 transparentCursorMask[8] = {};
		hiddenCursor = make_shared_cursor(SDL_CreateCursor(transparentCursorData,
			transparentCursorMask, 8, 8, 0, 0));
	}
	if (hiddenCursor != nullptr)
	{
		SDL_SetCursor(hiddenCursor.get());
	}
	SDL_HideCursor();
	softwareCursorHidden = true;
	CursorImageIndex = -1;
}

bool EngineBase::getCursorVisible() const
{
	return !softwareCursorHidden;
}

void EngineBase::drawImage(_shared_image image, SDL_Rect * src, SDL_Rect * dst)
{
	if (image == nullptr ||
		!canPrepareRenderFrame())
	{
		return;
	}
	SDL_FRect fsrc, fdst, *pfsrc = nullptr, *pfdst = nullptr;
	
	if (src != nullptr)
	{
		pfsrc = &fsrc;
		SDL_RectToFRect(src, pfsrc);
	}
	if (dst != nullptr)
	{
		pfdst = &fdst;
		SDL_RectToFRect(dst, pfdst);
	}
	if (!SDL_RenderTexture(renderer, image.get(), pfsrc, pfdst))
	{
		GameLog::write("SDL_RenderTexture Error: %s", SDL_GetError());
	}
}

void EngineBase::drawImage(_shared_image image, SDL_Rect * rect)
{
	if (image == nullptr)
	{
		return;
	}
	drawImage(image, nullptr, rect);
}

bool EngineBase::pointInImage(_shared_image image, int x, int y)
{
	if (image == nullptr ||
		!canPrepareRenderFrame())
	{
		return false;
	}
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer == nullptr ||
		!canPrepareRenderFrame())
	{
		return false;
	}

	SDL_Texture * from = image.get();
	int w = 0;
	int h = 0;
	getImageSize(image, w, h);
	if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h)
	{
		return false;
	}

	_shared_image temporaryTarget = make_shared_image(
		SDL_CreateTexture(
			activeRenderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_TARGET,
			w,
			h));
	if (temporaryTarget == nullptr)
	{
		return false;
	}
	auto originalTarget =
		SDL_GetRenderTarget(activeRenderer);
	if (SetRenderTarget(
		activeRenderer,
		temporaryTarget.get()) == 0)
	{
		return false;
	}

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	const bool rendered = SDL_RenderTexture(
		activeRenderer,
		from,
		nullptr,
		nullptr);
	SDL_SetTextureBlendMode(from, bm);

	auto surface = make_shared_surface(
		rendered
			? SDL_RenderReadPixels(
				activeRenderer,
				nullptr)
			: nullptr);
	if (!restoreAcceptedRenderTarget(
		activeRenderer,
		originalTarget,
		temporaryTarget) ||
		surface == nullptr)
	{
		return false;
	}
	uint8_t alpha = 0;
	if (!SDL_ReadSurfacePixel(
		surface.get(),
		x,
		y,
		nullptr,
		nullptr,
		nullptr,
		&alpha))
	{
		return false;
	}

	return alpha != 0;
}

void EngineBase::initTime()
{
	timer.setParent(nullptr);
	timer.reInit();
}


UTime EngineBase::getTime()
{
	return timer.get();
	/*if (time.paused)
	{
		return time.pauseBeginTime - time.beginTime;
	}
	else
	{
		return SDL_GetTicks() - time.beginTime;
	}*/
}

int EngineBase::getRand(int max, int min)
{
	if (min > max) {
		std::swap(min, max);
	}

	// 线程局部存储：每个线程独立维护随机引擎和分布
	static thread_local std::random_device rd;          // 硬件熵源（真随机种子）
	static thread_local std::mt19937 mtEngine(rd());     // Mersenne Twister 引擎
	std::uniform_int_distribution<int> dist(min, max);

	return dist(mtEngine);
}

//void EngineBase::setTimePaused(bool paused)
//{
//	if (paused == time.paused)
//	{
//		return;
//	}
//	if (paused)
//	{
//		time.paused = true;
//		time.pauseBeginTime = SDL_GetTicks();
//	}
//	else
//	{
//		time.paused = false;
//		time.beginTime += SDL_GetTicks() - time.pauseBeginTime;
//	}
//}
//
//unsigned int EngineBase::initTime(Timer * t)
//{
//	unsigned int now = 0;
//	t->beginTime = getTime();
//	t->paused = false;
//	return t->beginTime;
//}
//
//unsigned int EngineBase::getTime(Timer * t)
//{
//	if (t->paused)
//	{
//		return t->pauseBeginTime - t->beginTime;
//	}
//	else
//	{
//		return getTime() - t->beginTime;
//	}
//}
//
//void EngineBase::setTime(Timer * t, unsigned int time)
//{
//	unsigned int tm = getTime(t);
//	if (tm > time)
//	{
//		t->beginTime += tm - time;
//	}
//	else
//	{
//		t->beginTime -= time - tm;
//	}
//}
//
//void EngineBase::setTimePaused(Timer * t, bool paused)
//{
//	if (paused == t->paused)
//	{
//		return;
//	}
//	if (paused)
//	{
//		t->paused = true;
//		t->pauseBeginTime = getTime();
//	}
//	else
//	{
//		t->paused = false;
//		t->beginTime += getTime() - t->pauseBeginTime;
//	}
//}

void EngineBase::delay(unsigned int t)
{
	SDL_Delay(t);
}

int EngineBase::getFPS()
{
	return FPS;
}

_shared_image EngineBase::createNewImageFromImage(_shared_image image)
{
	if (image == nullptr ||
		!canPrepareRenderFrame())
	{
		return nullptr;
	}
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer == nullptr ||
		!canPrepareRenderFrame())
	{
		return nullptr;
	}
	SDL_Texture* from = image.get();
	int w, h; 
	getImageSize(image, w, h);
	if ((w <= 0) || (h <= 0))
	{
		return nullptr;
	}

	auto originalTarget =
		SDL_GetRenderTarget(activeRenderer);
	if (SetRenderTarget(
		activeRenderer,
		image.get()) == 0)
	{
		return nullptr;
	}

	auto surface = make_shared_surface(
		SDL_RenderReadPixels(
			activeRenderer,
			nullptr));
	if (!restoreAcceptedRenderTarget(
		activeRenderer,
		originalTarget,
		image) ||
		surface == nullptr ||
		!canPrepareRenderFrame())
	{
		return nullptr;
	}

	auto to = make_safe_shared_image(
		SDL_CreateTextureFromSurface(
			activeRenderer,
			surface.get()));
	if (to == nullptr ||
		!SDL_SetTextureBlendMode(
			to.get(),
			SDL_BLENDMODE_BLEND))
	{
		return nullptr;
	}
	return to;
}

_shared_image EngineBase::createGrayscaleImage(_shared_image image)
{
	if (image == nullptr)
	{
		return nullptr;
	}

	int width = 0;
	int height = 0;
	if (!getImageSize(image, width, height) || width <= 0 || height <= 0)
	{
		return nullptr;
	}

	std::unique_ptr<char[]> pixelData;
	int pixelDataLength = saveImageToPixels(image, width, height, pixelData);
	if (pixelData == nullptr || pixelDataLength < width * height * SaveBMPPixelBytes)
	{
		return nullptr;
	}

	auto pixels = reinterpret_cast<uint32_t*>(pixelData.get());
	int pixelCount = width * height;
	for (int index = 0; index < pixelCount; index++)
	{
		uint32_t pixel = pixels[index];
		uint8_t alpha = (pixel >> 24) & 0xFF;
		uint8_t red = (pixel >> 16) & 0xFF;
		uint8_t green = (pixel >> 8) & 0xFF;
		uint8_t blue = pixel & 0xFF;
		uint8_t gray = static_cast<uint8_t>((red * 54 + green * 183 + blue * 19) >> 8);
		pixels[index] = (alpha << 24) | (gray << 16) | (gray << 8) | gray;
	}

	return createImageFromPixelData(reinterpret_cast<const uint8_t*>(pixelData.get()), width, height);
}

_shared_image EngineBase::getGrayscaleImage(_shared_image image)
{
	if (image == nullptr)
	{
		return nullptr;
	}

	Image_t* key = image.get();
	auto iter = grayscaleImageCache.find(key);
	if (iter != grayscaleImageCache.end())
	{
		auto source = iter->second.source.lock();
		if (source != nullptr && source.get() == key && iter->second.image != nullptr)
		{
			return iter->second.image;
		}
		grayscaleImageCache.erase(iter);
	}

	if (grayscaleImageCache.size() > 4096)
	{
		grayscaleImageCache.clear();
	}

	_shared_image grayscaleImage = createGrayscaleImage(image);
	if (grayscaleImage != nullptr)
	{
		grayscaleImageCache[key] = { image, grayscaleImage };
	}
	return grayscaleImage;
}

void EngineBase::clearGrayscaleImageCache()
{
	grayscaleImageCache.clear();
}

_shared_image EngineBase::loadImageFromMem(std::unique_ptr<char[]>& data, int size)
{
	auto surface = loadSurfaceFromMem(data, size);
	return createImageFromSurface(surface.get());
}

_shared_image EngineBase::createImageFromSurface(Surface_t* surface)
{
	if (surface == nullptr)
	{
		return nullptr;
	}
	_shared_surface convertedSurface;
	if (SDL_BYTESPERPIXEL(surface->format) != 4)
	{
		convertedSurface = make_shared_surface(
			SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32));
		if (convertedSurface == nullptr)
		{
			return nullptr;
		}
		surface = convertedSurface.get();
	}
	SDL_Texture* image = SDL_CreateTextureFromSurface(
		renderer.load(), surface);
	if (!isTextureWithinImageBudget(image))
	{
		if (image != nullptr)
		{
			SDL_DestroyTexture(image);
		}
		return nullptr;
	}
	return make_safe_shared_image(image);
}

_shared_image EngineBase::loadImageFromFile(const std::string & fileName)
{
	std::unique_ptr<char[]> data;
	int size = 0;
	if (!File::readFile(fileName, data, size,
		static_cast<int>(EncodedImageSafety::MaxEncodedImageBytes)))
	{
		GameLog::write("Image File Readed Error\n");
		return nullptr;
	}
	if (data == nullptr || size <= 0)
	{
		GameLog::write("Image File Readed Error\n");
		return nullptr;
	}
	SDL_Surface* surface = SafeImageDecoder::loadSurface(data.get(), size);
	if (surface == nullptr)
	{
		GameLog::write("Image dimensions exceed runtime budget\n");
		return nullptr;
	}
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_DestroySurface(surface);
	if (!isTextureWithinImageBudget(texture))
	{
		if (texture != nullptr)
		{
			SDL_DestroyTexture(texture);
		}
		return nullptr;
	}
	return make_safe_shared_image(texture);
}

int EngineBase::saveImageToFile(_shared_image image, int w, int h, const std::string & fileName)
{
	if (image == nullptr)
	{
		return -1;
	}
	std::unique_ptr<char[]> data = nullptr;
	int len = saveImageToMem(image, w, h, data);
	if (len > 0 && data != nullptr)
	{
		File::writeFile(fileName, data, len);
		return len;
	}
	else
	{
		return -1;
	}
}

int EngineBase::saveImageToFile(_shared_image image, const std::string & fileName)
{
	if (image == nullptr)
	{
		return -1;
	}
	int w, h;
	getImageSize(image, w, h);
	return saveImageToFile(image, w, h, fileName);
}

int EngineBase::saveImageToMem(_shared_image image, int w, int h, std::unique_ptr<char[]>& data)
{
	if (image == nullptr ||
		w <= 0 ||
		h <= 0 ||
		!canPrepareRenderFrame())
	{
		return -1;
	}

	_shared_surface surface = nullptr;
	{
		std::lock_guard<std::recursive_mutex> locker(
			renderTargetSessionMutex);
		SDL_Renderer* activeRenderer = renderer.load();
		if (activeRenderer == nullptr ||
			!canPrepareRenderFrame())
		{
			return -1;
		}
		auto temporaryTarget = make_shared_image(
			SDL_CreateTexture(
				activeRenderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET,
				w,
				h));
		if (temporaryTarget == nullptr)
		{
			return -1;
		}
		SDL_Texture* originalTarget =
			SDL_GetRenderTarget(activeRenderer);
		if (SetRenderTarget(
			activeRenderer,
			temporaryTarget.get()) == 0)
		{
			return -1;
		}

		SDL_BlendMode blendMode;
		SDL_GetTextureBlendMode(
			image.get(),
			&blendMode);
		SDL_SetTextureBlendMode(
			image.get(),
			SDL_BLENDMODE_NONE);
		const bool rendered = SDL_RenderTexture(
			activeRenderer,
			image.get(),
			nullptr,
			nullptr);
		SDL_SetTextureBlendMode(
			image.get(),
			blendMode);
		surface = make_shared_surface(
			rendered
				? SDL_RenderReadPixels(
					activeRenderer,
					nullptr)
				: nullptr);
		if (!restoreAcceptedRenderTarget(
			activeRenderer,
			originalTarget,
			temporaryTarget) ||
			surface == nullptr)
		{
			GameLog::write(
				"reading pixels error\n");
			return -1;
		}
	}

	int rwSize = w * h * 4 + 256;
	std::unique_ptr<char[]> rwData = std::make_unique<char[]>(rwSize);
	
	SDL_IOStream * bmp = SDL_IOFromMem(rwData.get(), rwSize);

	if (bmp == nullptr)
	{
		return -1;
	}
	SDL_SeekIO(bmp, 0, SDL_IO_SEEK_SET);
	if (!SDL_SaveBMP_IO(
		surface.get(),
		bmp,
		false))
	{
		SDL_CloseIO(bmp);
		return -1;
	}

	SDL_SeekIO(bmp, 0, SDL_IO_SEEK_END);
	auto size = SDL_TellIO(bmp);
	SDL_CloseIO(bmp);

	if (size > rwSize)
	{
		return -1;
	}

	data = std::make_unique<char[]>(size);
	memcpy(data.get(), rwData.get(), size);

	return size;
}

int EngineBase::saveImageToMem(_shared_image image, std::unique_ptr<char[]>& data)
{
	if (image == nullptr)
	{
		return -1;
	}
	int w, h;
	getImageSize(image, w, h);
	return saveImageToMem(image, w, h, data);
}

int EngineBase::saveImageToPngMemory(_shared_image image, int width, int height,
	std::unique_ptr<char[]>& data)
{
	data.reset();
	if (image == nullptr || width <= 0 || height <= 0 ||
		!EncodedImageSafety::isDecodedPixelCountSafe(
			static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height)))
	{
		return -1;
	}

	std::unique_ptr<char[]> pixels;
	int pixelBytes = saveImageToPixels(image, width, height, pixels);
	std::uint64_t requiredPixelBytes = static_cast<std::uint64_t>(width) *
		static_cast<std::uint64_t>(height) * SaveBMPPixelBytes;
	if (pixels == nullptr || pixelBytes < 0 ||
		static_cast<std::uint64_t>(pixelBytes) < requiredPixelBytes)
	{
		GameLog::write("save PNG pixel read failed: %s\n", SDL_GetError());
		return -1;
	}
	return PngImageEncoder::encodeBgra8888(
		reinterpret_cast<const uint8_t*>(pixels.get()), width, height, pixelBytes, data);
}

_shared_surface EngineBase::loadSurfaceFromMem(std::unique_ptr<char[]>& data, int size)
{
	return make_shared_surface(SafeImageDecoder::loadSurface(data.get(), size));
}

_shared_cursor EngineBase::loadCursorImageFromMem(std::unique_ptr<char[]>& data, int size, int x, int y)
{
	auto s = loadSurfaceFromMem(data, size);
	return createCursorImageFromSurface(s.get(), x, y);
}

_shared_cursor EngineBase::createCursorImageFromSurface(
	Surface_t* surface,
	int x,
	int y)
{
	if (surface == nullptr || x < 0 || y < 0 ||
		x >= surface->w || y >= surface->h)
	{
		return nullptr;
	}
	auto cursor = make_shared_cursor(
		SDL_CreateColorCursor(surface, x, y));
	return cursor;
}

_shared_surface EngineBase::createCursorSurfaceFromPixelData(
	const uint8_t* pixelData,
	int width,
	int height)
{
	if (pixelData == nullptr || width <= 0 || height <= 0 ||
		!EncodedImageSafety::isDecodedPixelCountSafe(
			static_cast<std::uint64_t>(width),
			static_cast<std::uint64_t>(height)))
	{
		return nullptr;
	}
	SDL_Surface* borrowedSurface = SDL_CreateSurfaceFrom(
		width,
		height,
		SDL_PIXELFORMAT_ARGB8888,
		const_cast<uint8_t*>(pixelData),
		width * 4);
	if (borrowedSurface == nullptr)
	{
		return nullptr;
	}
	_shared_surface ownedSurface = make_shared_surface(
		SDL_DuplicateSurface(borrowedSurface));
	SDL_DestroySurface(borrowedSurface);
	return ownedSurface;
}

_shared_cursor EngineBase::createCursorImageFromPixelData(const uint8_t* pixelData,
	int width, int height, int x, int y)
{
	auto surface = createCursorSurfaceFromPixelData(
		pixelData, width, height);
	return createCursorImageFromSurface(surface.get(), x, y);
}

_shared_cursor EngineBase::createNativeAnimatedCursor(
	const std::vector<_shared_surface>& frameSurfaces,
	int interval,
	int x,
	int y)
{
#if defined(_WIN32) && SDL_VERSION_ATLEAST(3, 4, 0)
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: native animated cursor creation must run on the SDL main thread\n");
		return nullptr;
	}
	if (frameSurfaces.size() <= 1 ||
		frameSurfaces.size() > static_cast<std::size_t>(INT_MAX) ||
		interval <= 0 || frameSurfaces.front() == nullptr)
	{
		return nullptr;
	}
	const int width = frameSurfaces.front()->w;
	const int height = frameSurfaces.front()->h;
	if (width <= 0 || height <= 0 ||
		x < 0 || y < 0 || x >= width || y >= height)
	{
		return nullptr;
	}
	std::vector<SDL_CursorFrameInfo> frames;
	frames.reserve(frameSurfaces.size());
	for (const auto& surface : frameSurfaces)
	{
		if (surface == nullptr || surface->w != width ||
			surface->h != height)
		{
			return nullptr;
		}
		frames.push_back(
			{ surface.get(), static_cast<Uint32>(interval) });
	}
	_shared_cursor animatedCursor = make_shared_cursor(
		SDL_CreateAnimatedCursor(
			frames.data(),
			static_cast<int>(frames.size()),
			x,
			y));
	if (animatedCursor == nullptr)
	{
		GameLog::write(
			"Engine: native animated cursor creation failed: %s\n",
			SDL_GetError());
	}
	return animatedCursor;
#else
	(void)frameSurfaces;
	(void)interval;
	(void)x;
	(void)y;
	return nullptr;
#endif
}

void EngineBase::drawImage(_shared_image image, int x, int y)
{
	if (image == nullptr)
	{
		return;
	}
	int w, h;
	getImageSize(image, w, h);
	SDL_Rect s, d;
	s.x = 0;
	s.y = 0;
	s.w = w;
	s.h = h;
	d.x = x;
	d.y = y;
	d.w = w;
	d.h = h;
	drawImage(image, nullptr, &d);
}

void EngineBase::drawImage(_shared_image image, Rect * rect)
{
	drawImage(image, nullptr, rect);
}

void EngineBase::drawImage(_shared_image image, Rect * src, Rect * dst)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_Rect s;
	SDL_Rect d;
	SDL_Rect * pSourceRect = nullptr;
	SDL_Rect * pDestRect = nullptr;
	if (src != nullptr)
	{
		s.x = src->x;
		s.y = src->y;
		s.w = src->w;
		s.h = src->h;
		pSourceRect = &s;
	}
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pDestRect = &d;
	}
	drawImage(image, pSourceRect, pDestRect);
}

void EngineBase::drawImageEx(_shared_image image, Rect* src, Rect* dst, float angle, Point* center)
{
	if (image == nullptr ||
		!canPrepareRenderFrame())
	{
		return;
	}
	int sw = 0, sh = 0;
	if (src != nullptr || center != nullptr)
	{
		getImageSize(image, sw, sh);
	}
	SDL_FRect fsrc, fdst, * pfsrc = nullptr, * pfdst = nullptr;
	SDL_FPoint p;
	SDL_FPoint* pCenterPoint = nullptr;
	if (center != nullptr)
	{
		p.x = ((float)center->x) ;
		p.y = ((float)center->y) ;
		pCenterPoint = &p;
	}

	if (src != nullptr)
	{
		pfsrc = &fsrc;
		fsrc.x = ((float)src->x) ;
		fsrc.y = ((float)src->y) ;
		fsrc.w = ((float)src->w) ;
		fsrc.h = ((float)src->h) ;
	}
	if (dst != nullptr)
	{
		pfdst = &fdst;
		fdst.x = ((float)dst->x) ;
		fdst.y = ((float)dst->y) ;
		fdst.w = ((float)dst->w) ;
		fdst.h = ((float)dst->h) ;
	}

	SDL_RenderTextureRotated(renderer, image.get(), pfsrc, pfdst, angle, pCenterPoint, SDL_FLIP_NONE);
}

void EngineBase::drawAspectFitImage(
	_shared_image image,
	const Rect& sourceRect,
	const Rect& destinationRect,
	bool fadeMirroredBars,
	std::uint8_t alpha,
	UTime mirroredBarsAnimationTime,
	const std::vector<AspectFitPointerRipple>* pointerRipples)
{
	if (image == nullptr ||
		sourceRect.w <= 0 || sourceRect.h <= 0 ||
		destinationRect.w <= 0 || destinationRect.h <= 0)
	{
		return;
	}

	Rect fittedRect = AspectFitLayout::calculateFittedRect(
		sourceRect.w,
		sourceRect.h,
		destinationRect.w,
		destinationRect.h);
	if (fittedRect.w <= 0 || fittedRect.h <= 0)
	{
		return;
	}
	fittedRect.x += destinationRect.x;
	fittedRect.y += destinationRect.y;

	const bool hasPointerRipples =
		pointerRipples != nullptr && !pointerRipples->empty();
	int textureWidth = 0;
	int textureHeight = 0;
	const bool hasGeometryTexture =
		(mirroredBarsAnimationTime != 0 || hasPointerRipples) &&
		getImageSize(image, textureWidth, textureHeight) &&
		textureWidth > 0 && textureHeight > 0;
	auto calculatePointerSample = [&] (
		float normalizedX,
		float normalizedY,
		UTime sampleTime)
	{
		AspectFitPointerRippleSample sample;
		if (!hasGeometryTexture || !hasPointerRipples)
		{
			return sample;
		}
		return AspectFitLayout::calculateCombinedPointerRippleSample(
			normalizedX,
			normalizedY,
			destinationRect.w,
			destinationRect.h,
			fittedRect.h,
			sampleTime,
			*pointerRipples);
	};

	if (fadeMirroredBars)
	{
		constexpr unsigned char DimColor = 210;
		const bool animateMirroredBars =
			hasGeometryTexture &&
			(mirroredBarsAnimationTime != 0 || hasPointerRipples);
		if (animateMirroredBars)
		{
			constexpr float MirrorSeamOverlapPixels = 1.0f;
			std::vector<Vertex> vertices;
			std::vector<int> indices;
			vertices.reserve(2400);
			indices.reserve(13200);

			auto appendMirrorBar = [&] (
				int gap,
				bool leading,
				bool sideBar,
				unsigned int disturbanceSeed)
			{
				if (gap <= 0)
				{
					return;
				}

				const int sourceDepth = sideBar
					? sourceRect.w
					: sourceRect.h;
				const int fittedDepth = sideBar
					? fittedRect.w
					: fittedRect.h;
				const int fittedLength = sideBar
					? fittedRect.h
					: fittedRect.w;
				if (sourceDepth <= 0 || fittedDepth <= 0 ||
					fittedLength <= 0)
				{
					return;
				}

				const float sourceMinimumX = sourceRect.x + 0.5f;
				const float sourceMaximumX =
					sourceRect.x + sourceRect.w - 0.5f;
				const float sourceMinimumY = sourceRect.y + 0.5f;
				const float sourceMaximumY =
					sourceRect.y + sourceRect.h - 0.5f;
				const float sourceSampleWidth = (std::max)(
					0.0f,
					sourceMaximumX - sourceMinimumX);
				const float sourceSampleHeight = (std::max)(
					0.0f,
					sourceMaximumY - sourceMinimumY);
				const float sourceVisibleDepth = (std::min)(
					sideBar ? sourceSampleWidth : sourceSampleHeight,
					static_cast<float>(sourceDepth) * gap /
						fittedDepth);
				const int depthDivisions = std::clamp(
					gap / 16,
					8,
					28);
				const int lengthDivisions = std::clamp(
					fittedLength / 24,
					16,
					40);
				std::vector<float> physicalDistances;
				physicalDistances.reserve(depthDivisions + 1);
				physicalDistances.push_back(0.0f);
				for (int depthIndex = 1;
					depthIndex <= depthDivisions;
					++depthIndex)
				{
					physicalDistances.push_back(
						static_cast<float>(gap) * depthIndex /
						depthDivisions);
				}
				const int depthVertexCount =
					static_cast<int>(physicalDistances.size());
				const int depthCellCount = depthVertexCount - 1;
				const int baseVertex = static_cast<int>(vertices.size());

				for (int lengthIndex = 0;
					lengthIndex <= lengthDivisions;
					++lengthIndex)
				{
					const float along =
						static_cast<float>(lengthIndex) /
						lengthDivisions;
					for (int depthIndex = 0;
						depthIndex < depthVertexCount;
						++depthIndex)
					{
						const float physicalDistance =
							physicalDistances[depthIndex];
						const float seamDistance = std::clamp(
							physicalDistance / gap,
							0.0f,
							1.0f);

						float x = 0.0f;
						float y = 0.0f;
						float textureX = 0.0f;
						float textureY = 0.0f;
						const float seamOverlap = depthIndex == 0
							? MirrorSeamOverlapPixels
							: 0.0f;
						if (sideBar)
						{
							x = leading
								? fittedRect.x - physicalDistance +
									seamOverlap
								: fittedRect.x + fittedRect.w +
									physicalDistance - seamOverlap;
							y = fittedRect.y + fittedRect.h * along;
							textureX = leading
								? sourceMinimumX +
									sourceVisibleDepth * seamDistance
								: sourceMaximumX -
									sourceVisibleDepth * seamDistance;
							textureY = sourceMinimumY +
								sourceSampleHeight * along;
						}
						else
						{
							x = fittedRect.x + fittedRect.w * along;
							y = leading
								? fittedRect.y - physicalDistance +
									seamOverlap
								: fittedRect.y + fittedRect.h +
									physicalDistance - seamOverlap;
							textureX = sourceMinimumX +
								sourceSampleWidth * along;
							textureY = leading
								? sourceMinimumY +
									sourceVisibleDepth * seamDistance
								: sourceMaximumY -
									sourceVisibleDepth * seamDistance;
						}

						const float normalizedX =
							(x - destinationRect.x) /
							destinationRect.w;
						const float normalizedY =
							(y - destinationRect.y) /
							destinationRect.h;
						float normalOffset =
							AspectFitLayout::
							calculateMirrorWaveNormalOffset(
								normalizedX,
								normalizedY,
								fittedRect.h,
								mirroredBarsAnimationTime,
								disturbanceSeed);
						const AspectFitPointerRippleSample pointerSample =
							calculatePointerSample(
								normalizedX,
								normalizedY,
								mirroredBarsAnimationTime);
						normalOffset += sideBar
							? pointerSample.offset.x
							: pointerSample.offset.y;
						const float edgeTransition = std::clamp(
							seamDistance / 0.18f,
							0.0f,
							1.0f);
						const float edgeAnchor =
							edgeTransition * edgeTransition *
							(3.0f - 2.0f * edgeTransition);
						normalOffset *= edgeAnchor;
						if (leading)
						{
							normalOffset = (std::min)(
								normalOffset,
								physicalDistance);
						}
						else
						{
							normalOffset = (std::max)(
								normalOffset,
								-physicalDistance);
						}
						if (sideBar)
						{
							x += normalOffset;
						}
						else
						{
							y += normalOffset;
						}

						const float fade = (std::max)(
							0.0f,
							1.0f - seamDistance * seamDistance);
						Vertex vertex;
						vertex.position = { x, y };
						vertex.color = {
							pointerSample.brightness,
							pointerSample.brightness,
							pointerSample.brightness,
							fade * static_cast<float>(alpha) / 255.0f
						};
						vertex.tex_coord = {
							textureX / textureWidth,
							textureY / textureHeight
						};
						vertices.push_back(vertex);
					}
				}

				for (int lengthIndex = 0;
					lengthIndex < lengthDivisions;
					++lengthIndex)
				{
					for (int depthIndex = 0;
						depthIndex < depthCellCount;
						++depthIndex)
					{
						const int topLeft = baseVertex +
							lengthIndex * depthVertexCount + depthIndex;
						const int topRight = topLeft + 1;
						const int bottomLeft =
							topLeft + depthVertexCount;
						const int bottomRight = bottomLeft + 1;
						indices.push_back(topLeft);
						indices.push_back(bottomLeft);
						indices.push_back(topRight);
						indices.push_back(topRight);
						indices.push_back(bottomLeft);
						indices.push_back(bottomRight);
					}
				}
			};

			const int leftGap = (std::max)(
				0,
				fittedRect.x - destinationRect.x);
			const int rightGap = (std::max)(
				0,
				destinationRect.x + destinationRect.w -
					(fittedRect.x + fittedRect.w));
			const int topGap = (std::max)(
				0,
				fittedRect.y - destinationRect.y);
			const int bottomGap = (std::max)(
				0,
				destinationRect.y + destinationRect.h -
					(fittedRect.y + fittedRect.h));
			appendMirrorBar(leftGap, true, true, 1U);
			appendMirrorBar(rightGap, false, true, 2U);
			appendMirrorBar(topGap, true, false, 3U);
			appendMirrorBar(bottomGap, false, false, 4U);

			if (!vertices.empty() && !indices.empty())
			{
				setImageAlpha(image, 255);
				setImageColorMode(image, 255, 255, 255);
				drawGeometry(image, vertices, indices);
			}
		}
		else
		{
			const auto slices = AspectFitLayout::calculateMirroredSlices(
				sourceRect,
				destinationRect,
				fittedRect);
			if (!slices.empty())
			{
				setImageColorMode(
					image,
					DimColor,
					DimColor,
					DimColor);
			}
			for (const AspectFitMirrorSlice& slice : slices)
			{
				const std::uint8_t sliceAlpha =
					static_cast<std::uint8_t>(
						static_cast<unsigned int>(alpha) *
						slice.alpha / 255U);
				setImageAlpha(image, sliceAlpha);
				Rect source = slice.source;
				Rect destination = slice.destination;
				drawImage(image, &source, &destination);
			}
			if (!slices.empty())
			{
				setImageColorMode(image, 255, 255, 255);
			}
		}
	}

	if (hasGeometryTexture && hasPointerRipples)
	{
		const int horizontalDivisions = std::clamp(
			fittedRect.w / 24,
			16,
			48);
		const int verticalDivisions = std::clamp(
			fittedRect.h / 24,
			12,
			40);
		std::vector<Vertex> vertices;
		std::vector<int> indices;
		vertices.reserve(
			(horizontalDivisions + 1) * (verticalDivisions + 1));
		indices.reserve(horizontalDivisions * verticalDivisions * 6);
		for (int verticalIndex = 0;
			verticalIndex <= verticalDivisions;
			++verticalIndex)
		{
			const float vertical = static_cast<float>(verticalIndex) /
				verticalDivisions;
			for (int horizontalIndex = 0;
				horizontalIndex <= horizontalDivisions;
				++horizontalIndex)
			{
				const float horizontal =
					static_cast<float>(horizontalIndex) /
					horizontalDivisions;
				const float x = fittedRect.x + fittedRect.w * horizontal;
				const float y = fittedRect.y + fittedRect.h * vertical;
				const float normalizedX =
					(x - destinationRect.x) / destinationRect.w;
				const float normalizedY =
					(y - destinationRect.y) / destinationRect.h;
				const AspectFitPointerRippleSample pointerSample =
					calculatePointerSample(
						normalizedX,
						normalizedY,
						mirroredBarsAnimationTime);
				AspectFitPointerRippleOffset pointerOffset =
					pointerSample.offset;
				const float nearestEdge = (std::min)(
					(std::min)(horizontal, 1.0f - horizontal),
					(std::min)(vertical, 1.0f - vertical));
				const float edgeTransition = std::clamp(
					nearestEdge / 0.05f,
					0.0f,
					1.0f);
				const float edgeAnchor = edgeTransition * edgeTransition *
					(3.0f - 2.0f * edgeTransition);
				pointerOffset.x *= edgeAnchor;
				pointerOffset.y *= edgeAnchor;
				const float textureX = std::clamp(
					sourceRect.x + sourceRect.w * horizontal -
						pointerOffset.x * sourceRect.w / fittedRect.w,
					static_cast<float>(sourceRect.x),
					static_cast<float>(sourceRect.x + sourceRect.w));
				const float textureY = std::clamp(
					sourceRect.y + sourceRect.h * vertical -
						pointerOffset.y * sourceRect.h / fittedRect.h,
					static_cast<float>(sourceRect.y),
					static_cast<float>(sourceRect.y + sourceRect.h));

				Vertex vertex;
				vertex.position = { x, y };
				vertex.color = {
					pointerSample.brightness,
					pointerSample.brightness,
					pointerSample.brightness,
					static_cast<float>(alpha) / 255.0f
				};
				vertex.tex_coord = {
					textureX / textureWidth,
					textureY / textureHeight
				};
				vertices.push_back(vertex);
			}
		}
		const int rowLength = horizontalDivisions + 1;
		for (int verticalIndex = 0;
			verticalIndex < verticalDivisions;
			++verticalIndex)
		{
			for (int horizontalIndex = 0;
				horizontalIndex < horizontalDivisions;
				++horizontalIndex)
			{
				const int topLeft =
					verticalIndex * rowLength + horizontalIndex;
				const int topRight = topLeft + 1;
				const int bottomLeft = topLeft + rowLength;
				const int bottomRight = bottomLeft + 1;
				indices.push_back(topLeft);
				indices.push_back(bottomLeft);
				indices.push_back(topRight);
				indices.push_back(topRight);
				indices.push_back(bottomLeft);
				indices.push_back(bottomRight);
			}
		}
		setImageAlpha(image, 255);
		setImageColorMode(image, 255, 255, 255);
		drawGeometry(image, vertices, indices);
	}
	else
	{
		setImageAlpha(image, alpha);
		Rect source = sourceRect;
		drawImage(image, &source, &fittedRect);
	}
	setImageAlpha(image, 255);
}

//void EngineBase::freeImage(Image_t* image)
//{
//	if (image != nullptr)
//	{
//		SDL_DestroyTexture(image);
//		image = nullptr;
//	}
//}

_shared_image EngineBase::createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool safe)
{
	if (!canPrepareRenderFrame())
	{
		return nullptr;
	}
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer == nullptr)
	{
		return nullptr;
	}

	auto sur = make_shared_surface(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_ARGB8888));
	if (sur == nullptr)
	{
		return nullptr;
	}

	if (!SDL_WriteSurfacePixel(sur.get(), 0, 0, r, g, b, 0xFF))
	{
		return nullptr;
	}
	_shared_image tex = nullptr;
	if (safe)
	{
		tex = make_safe_shared_image(
			SDL_CreateTextureFromSurface(
				activeRenderer,
				sur.get()));
	}
	else
	{
		tex = make_shared_image(
			SDL_CreateTextureFromSurface(
				activeRenderer,
				sur.get()));
	}
	
	if (tex.get() == nullptr)
	{
		return nullptr;
	}

	SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
	setImageAlpha(tex, a);
	return tex;
}

_shared_image EngineBase::createLumMask()
{
	SDL_Surface * s = SDL_CreateSurface(LUM_MASK_WIDTH, LUM_MASK_HEIGHT, SDL_PIXELFORMAT_ARGB8888);
	for (int i = 0; i < LUM_MASK_HEIGHT; i++)
	{
		for (int j = 0; j < LUM_MASK_WIDTH; j++)
		{
			float distance = std::abs(hypot(float(i - LUM_MASK_HEIGHT / 2) / (LUM_MASK_HEIGHT / 2), float(j - LUM_MASK_WIDTH / 2) / (LUM_MASK_WIDTH / 2)));
			if (distance >= 1.0f)
			{
				SDL_WriteSurfacePixel(s, j, i, 0xFF, 0xFF, 0xFF, 0);
			}
			else
			{
				uint8_t a = (uint8_t)((1.0f - distance) * 150);
				SDL_WriteSurfacePixel(s, j, i, 0xFF, 0xFF, 0xFF, a);
			}
		}
	}
	auto t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, s));
	SDL_DestroySurface(s);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_ADD);
	return t;
}

void EngineBase::setImageAlpha(_shared_image image, unsigned char a)
{
	SDL_SetTextureBlendMode(image.get(), SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(image.get(), a);	
}

void EngineBase::setImageColorMode(_shared_image image, unsigned char r, unsigned char g, unsigned char b)
{
	SDL_SetTextureColorMod(image.get(), r, g, b);
}

void EngineBase::drawImageWithColor(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_SetTextureColorMod(image.get(), r, g, b);
	drawImage(image, x, y);
	SDL_SetTextureColorMod(image.get(), 255, 255, 255);
}

void EngineBase::drawImageWithBlendAlpha(_shared_image image, int x, int y, unsigned char alpha, SDL_BlendMode blendMode)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_BlendMode oldBlendMode;
	SDL_GetTextureBlendMode(image.get(), &oldBlendMode);
	uint8_t oldAlpha;
	SDL_GetTextureAlphaMod(image.get(), &oldAlpha);

	SDL_SetTextureBlendMode(image.get(), blendMode);
	SDL_SetTextureAlphaMod(image.get(), alpha);
	drawImage(image, x, y);

	SDL_SetTextureBlendMode(image.get(), oldBlendMode);
	SDL_SetTextureAlphaMod(image.get(), oldAlpha);
}

bool EngineBase::getImageSize(_shared_image image, int& w, int& h)
{
	if (image == nullptr)
	{
		return false;
	}
	float fw = (float)w, fh = (float)h;
	auto ret = SDL_GetTextureSize(image.get(), &fw, &fh);
	w = (int)fw;
	h = (int)fh;
	return ret;
}

bool EngineBase::getImageContentBounds(_shared_image image, Rect& bounds, bool ignoreBlack)
{
	bounds = { 0, 0, 0, 0 };
	if (image == nullptr ||
		!canPrepareRenderFrame())
	{
		return false;
	}

	int w = 0;
	int h = 0;
	if (!getImageSize(image, w, h) || w <= 0 || h <= 0)
	{
		return false;
	}

	_shared_surface surface = nullptr;
	{
		std::lock_guard<std::recursive_mutex> locker(
			renderTargetSessionMutex);
		SDL_Renderer* activeRenderer = renderer.load();
		if (activeRenderer == nullptr ||
			!canPrepareRenderFrame())
		{
			return false;
		}
		_shared_image temporaryTarget =
			make_shared_image(
				SDL_CreateTexture(
					activeRenderer,
					SDL_PIXELFORMAT_ARGB8888,
					SDL_TEXTUREACCESS_TARGET,
					w,
					h));
		if (temporaryTarget == nullptr)
		{
			return false;
		}

		SDL_Texture* originalTarget =
			SDL_GetRenderTarget(activeRenderer);
		if (SetRenderTarget(
			activeRenderer,
			temporaryTarget.get()) == 0)
		{
			return false;
		}

		uint8_t oldRed = 0;
		uint8_t oldGreen = 0;
		uint8_t oldBlue = 0;
		uint8_t oldAlpha = 0;
		SDL_GetRenderDrawColor(
			activeRenderer,
			&oldRed,
			&oldGreen,
			&oldBlue,
			&oldAlpha);
		const bool cleared =
			SDL_SetRenderDrawColor(
				activeRenderer,
				0,
				0,
				0,
				0) &&
			SDL_RenderClear(activeRenderer);
		SDL_SetRenderDrawColor(
			activeRenderer,
			oldRed,
			oldGreen,
			oldBlue,
			oldAlpha);

		SDL_BlendMode blendMode;
		SDL_GetTextureBlendMode(
			image.get(),
			&blendMode);
		SDL_SetTextureBlendMode(
			image.get(),
			SDL_BLENDMODE_NONE);
		const bool rendered =
			cleared &&
			SDL_RenderTexture(
				activeRenderer,
				image.get(),
				nullptr,
				nullptr);
		SDL_SetTextureBlendMode(
			image.get(),
			blendMode);

		surface = make_shared_surface(
			rendered
				? SDL_RenderReadPixels(
					activeRenderer,
					nullptr)
				: nullptr);
		if (!restoreAcceptedRenderTarget(
			activeRenderer,
			originalTarget,
			temporaryTarget) ||
			surface == nullptr)
		{
			return false;
		}
	}

	if (surface == nullptr)
	{
		return false;
	}

	int left = w;
	int top = h;
	int right = -1;
	int bottom = -1;
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			uint8_t r = 0;
			uint8_t g = 0;
			uint8_t b = 0;
			uint8_t a = 0;
			if (!SDL_ReadSurfacePixel(
				surface.get(),
				x,
				y,
				&r,
				&g,
				&b,
				&a))
			{
				continue;
			}
			if (a == 0)
			{
				continue;
			}
			if (ignoreBlack && r == 0 && g == 0 && b == 0)
			{
				continue;
			}
			if (x < left)
			{
				left = x;
			}
			if (y < top)
			{
				top = y;
			}
			if (x > right)
			{
				right = x;
			}
			if (y > bottom)
			{
				bottom = y;
			}
		}
	}

	if (right < left || bottom < top)
	{
		return false;
	}

	bounds = { left, top, right - left + 1, bottom - top + 1 };
	return true;
}

_shared_image EngineBase::createCanvasImage(int w, int h)
{
	if (!canPrepareRenderFrame())
	{
		return nullptr;
	}
	if (w < 0)
	{
		w = width;
	}
	if (h < 0)
	{
		h = height;
	}

	return make_safe_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h));
}

bool EngineBase::setImageAsRenderTarget(_image image)
{
	return SetRenderTarget(renderer, image) != 0;
}

bool EngineBase::setSharedImageAsRenderTarget(_shared_image image)
{
	return SetRenderTarget(renderer, image.get()) != 0;
}

bool EngineBase::restoreImageRenderTargetAfterAcceptedOperation(
	_image originalTarget,
	const _shared_image& activeTarget)
{
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	return restoreAcceptedRenderTarget(
		renderer.load(),
		originalTarget,
		activeTarget);
}

_image EngineBase::getRenderTarget()
{
	return SDL_GetRenderTarget(renderer);
}

void EngineBase::renderClear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if (!canPrepareRenderFrame())
	{
		return;
	}
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderClear(renderer);
}

void EngineBase::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if (!canPrepareRenderFrame())
	{
		return;
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderFillRect(renderer, &rect);
}

void EngineBase::drawGeometry(_shared_image image, const std::vector<Vertex>& vertices, const std::vector<int>& indices)
{
	if (!canPrepareRenderFrame())
	{
		return;
	}
	SDL_RenderGeometry(renderer, image.get(), vertices.data(), vertices.size(), indices.data(), indices.size());
}

bool EngineBase::beginRenderTargetSession(
	RenderTargetSessionKind kind,
	int targetWidth,
	int targetHeight)
{
	SDL_Renderer* activeRenderer = renderer.load();
	if (kind == RenderTargetSessionKind::none ||
		targetWidth <= 0 ||
		targetHeight <= 0 ||
		activeRenderer == nullptr ||
		!canPrepareRenderFrame())
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	if (renderTargetSession.kind !=
		RenderTargetSessionKind::none ||
		!canPrepareRenderFrame())
	{
		return false;
	}

	auto temporaryTarget = make_shared_image(
		SDL_CreateTexture(
			activeRenderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_TARGET,
			targetWidth,
			targetHeight));
	if (temporaryTarget == nullptr ||
		!SDL_SetTextureBlendMode(
			temporaryTarget.get(),
			SDL_BLENDMODE_BLEND) ||
		!SDL_SetTextureAlphaMod(
			temporaryTarget.get(),
			255) ||
		!canPrepareRenderFrame())
	{
		return false;
	}

	renderTargetSession.kind = kind;
	renderTargetSession.ownerThread =
		SDL_GetCurrentThreadID();
	renderTargetSession.sessionRenderer =
		activeRenderer;
	renderTargetSession.originalTarget =
		SDL_GetRenderTarget(activeRenderer);
	renderTargetSession.temporaryTarget =
		std::move(temporaryTarget);
	if (SetRenderTarget(
		activeRenderer,
		renderTargetSession.temporaryTarget.get()) == 0)
	{
		renderTargetSession =
			RenderTargetSessionState();
		return false;
	}
	return true;
}

bool EngineBase::restoreAcceptedRenderTarget(
	SDL_Renderer* activeRenderer,
	SDL_Texture* originalTarget,
	const _shared_image& temporaryTarget)
{
	if (activeRenderer != nullptr &&
		SDL_SetRenderTarget(
			activeRenderer,
			originalTarget))
	{
		return true;
	}

	GameLog::write(
		"SDL_SetRenderTarget Error(accepted operation restore): %s",
		SDL_GetError());
	if (activeRenderer != nullptr &&
		SDL_SetRenderTarget(
			activeRenderer,
			nullptr))
	{
		return false;
	}

	if (temporaryTarget != nullptr)
	{
		retainedRenderTargets.push_back(
			{ activeRenderer, temporaryTarget });
	}
	return false;
}

_shared_image EngineBase::endRenderTargetSession(
	RenderTargetSessionKind kind)
{
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	if (kind == RenderTargetSessionKind::none ||
		renderTargetSession.kind != kind ||
		renderTargetSession.ownerThread !=
			SDL_GetCurrentThreadID() ||
		renderTargetSession.sessionRenderer == nullptr ||
		renderTargetSession.temporaryTarget == nullptr)
	{
		return nullptr;
	}

	// This is cleanup for a session that was admitted while the application
	// was active. It must not go through the lifecycle gate: that gate can
	// close between begin/end, but the bound texture must be detached before
	// its shared ownership is released.
	if (!restoreAcceptedRenderTarget(
		renderTargetSession.sessionRenderer,
		renderTargetSession.originalTarget,
		renderTargetSession.temporaryTarget))
	{
		// Either the renderer is detached to nullptr, or the temporary target
		// has been copied into retainedRenderTargets. In both cases the public
		// session can close without destroying a still-bound texture.
		renderTargetSession =
			RenderTargetSessionState();
		return nullptr;
	}

	auto completedTarget =
		std::move(renderTargetSession.temporaryTarget);
	renderTargetSession =
		RenderTargetSessionState();
	if (!canPrepareRenderFrame())
	{
		// The target is now safely detached, but no new GPU work may be
		// admitted while suspended or terminating.
		return nullptr;
	}
	return completedTarget;
}

bool EngineBase::abortRenderTargetSession(
	RenderTargetSessionKind kind)
{
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	if (renderTargetSession.kind != kind ||
		renderTargetSession.ownerThread !=
			SDL_GetCurrentThreadID() ||
		renderTargetSession.sessionRenderer == nullptr)
	{
		return false;
	}
	if (!restoreAcceptedRenderTarget(
		renderTargetSession.sessionRenderer,
		renderTargetSession.originalTarget,
		renderTargetSession.temporaryTarget))
	{
		renderTargetSession =
			RenderTargetSessionState();
		return false;
	}
	renderTargetSession =
		RenderTargetSessionState();
	return true;
}

void EngineBase::resetRenderTargetSessionForShutdown()
{
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	if (renderTargetSession.sessionRenderer != nullptr &&
		!SDL_SetRenderTarget(
			renderTargetSession.sessionRenderer,
			nullptr))
	{
		GameLog::write(
			"SDL_SetRenderTarget Error(render session shutdown): %s",
			SDL_GetError());
		retainTextureUntilProcessExit(
			std::move(
				renderTargetSession.temporaryTarget));
	}
	renderTargetSession =
		RenderTargetSessionState();
	for (auto& retained : retainedRenderTargets)
	{
		if (retained.renderer != nullptr &&
			retained.texture != nullptr &&
			SDL_GetRenderTarget(
				retained.renderer) ==
				retained.texture.get() &&
			!SDL_SetRenderTarget(
				retained.renderer,
				nullptr))
		{
			retainTextureUntilProcessExit(
				std::move(retained.texture));
		}
	}
	retainedRenderTargets.clear();
}

bool EngineBase::beginDrawTalk(int w, int h)
{
	if (!beginRenderTargetSession(
		RenderTargetSessionKind::talk,
		w,
		h))
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	SDL_Renderer* activeRenderer =
		renderTargetSession.sessionRenderer;
	uint8_t previousRed = 0;
	uint8_t previousGreen = 0;
	uint8_t previousBlue = 0;
	uint8_t previousAlpha = 0;
	(void)SDL_GetRenderDrawColor(
		activeRenderer,
		&previousRed,
		&previousGreen,
		&previousBlue,
		&previousAlpha);
	const bool initialized =
		SDL_SetRenderDrawColor(
			activeRenderer,
			0,
			0,
			0,
			0) &&
		SDL_RenderClear(activeRenderer);
	(void)SDL_SetRenderDrawColor(
		activeRenderer,
		previousRed,
		previousGreen,
		previousBlue,
		previousAlpha);
	if (!initialized)
	{
		(void)abortRenderTargetSession(
			RenderTargetSessionKind::talk);
		return false;
	}
	return true;
}

_shared_image EngineBase::endDrawTalk()
{
	auto target = endRenderTargetSession(
		RenderTargetSessionKind::talk);
	if (target == nullptr)
	{
		return nullptr;
	}
	return createNewImageFromImage(target);
}

_shared_image EngineBase::loadSaveShotFromPixels(int w, int h, const char* data, int size)
{
	if (data == nullptr || !SaveShotSafety::isPixelBufferValid(w, h, size))
	{
		GameLog::write("save shot null\n");
		return nullptr;
	}
	return createImageFromPixelData(reinterpret_cast<const uint8_t*>(data), w, h);
	//_shared_surface sur = make_shared_surface(SDL_CreateRGBSurfaceWithFormat(0, w, h, SaveBMPPixelBytes * 8, SaveBMPFormat));
	//if (sur == nullptr)
	//{
	//	return nullptr;
	//}
	/*if (w % SaveBMPPixelBytes == 0)
	{
		memcpy(sur->pixels, data.get(), w * SaveBMPPixelBytes * h);
	}
	else
	{
		for (int i = 0; i < h; i++)
		{
			if (i < w)
			{
				memcpy(((char *)sur->pixels) + w * i * SaveBMPPixelBytes, data.get() + i * w * SaveBMPPixelBytes + w * SaveBMPPixelBytes - i * SaveBMPPixelBytes, i * SaveBMPPixelBytes);
				memcpy(((char *)sur->pixels) + w * i * SaveBMPPixelBytes + i * SaveBMPPixelBytes, data.get() + i * w * SaveBMPPixelBytes, w * SaveBMPPixelBytes - i * SaveBMPPixelBytes);
			}
			else
			{
				memcpy(((char *)sur->pixels) + w * i * SaveBMPPixelBytes, data.get() + i * w * SaveBMPPixelBytes, w * SaveBMPPixelBytes);
			}
		}
	}*/
	/*return make_shared_image(SDL_CreateTextureFromSurface(renderer, sur.get()));*/
	//SDL_DestroySurface(sur);
}

bool EngineBase::beginSaveScreen()
{
	return beginRenderTargetSession(
		RenderTargetSessionKind::saveScreen,
		width,
		height);
}

_shared_image EngineBase::endSaveScreen()
{
	auto target = endRenderTargetSession(
		RenderTargetSessionKind::saveScreen);
	if (target == nullptr)
	{
		return nullptr;
	}
	return createNewImageFromImage(target);
}

int EngineBase::saveImageToPixels(_shared_image image, int w, int h, std::unique_ptr<char[]>& s)
{
	if (w <= 0 ||
		h <= 0 ||
		image == nullptr ||
		!canPrepareRenderFrame())
	{
		return -1;
	}

	int pitch = SaveBMPPixelBytes * w;
	int size = h * pitch;
	_shared_surface surface = nullptr;
	{
		std::lock_guard<std::recursive_mutex> locker(
			renderTargetSessionMutex);
		SDL_Renderer* activeRenderer = renderer.load();
		if (activeRenderer == nullptr ||
			!canPrepareRenderFrame())
		{
			return -1;
		}
		auto temporaryTarget = make_shared_image(
			SDL_CreateTexture(
				activeRenderer,
				SaveBMPFormat,
				SDL_TEXTUREACCESS_TARGET,
				w,
				h));
		if (temporaryTarget == nullptr)
		{
			return -1;
		}
		SDL_Texture* originalTarget =
			SDL_GetRenderTarget(activeRenderer);
		if (SetRenderTarget(
			activeRenderer,
			temporaryTarget.get()) == 0)
		{
			return -1;
		}

		SDL_BlendMode blendMode;
		SDL_GetTextureBlendMode(
			image.get(),
			&blendMode);
		SDL_SetTextureBlendMode(
			image.get(),
			SDL_BLENDMODE_NONE);
		const bool rendered = SDL_RenderTexture(
			activeRenderer,
			image.get(),
			nullptr,
			nullptr);
		SDL_SetTextureBlendMode(
			image.get(),
			blendMode);
		surface = make_shared_surface(
			rendered
				? SDL_RenderReadPixels(
					activeRenderer,
					nullptr)
				: nullptr);
		if (!restoreAcceptedRenderTarget(
			activeRenderer,
			originalTarget,
			temporaryTarget) ||
			surface == nullptr)
		{
			return -1;
		}
	}

	auto buffer = std::make_unique<char[]>(size);
	if (!SDL_LockSurface(surface.get()))
	{
		return -1;
	}

	for (int y = 0; y < h; y++)
	{
		memcpy(
			buffer.get() + y * pitch,
			(static_cast<char*>(
				surface.get()->pixels)) +
				y * surface.get()->pitch,
			pitch);
	}

	SDL_UnlockSurface(surface.get());

	s = std::move(buffer);

	return size;
}

_shared_image EngineBase::createRaindrop()
{
	const int w = 2;
	const int h = 115;

	SDL_Surface * s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
	if (s == nullptr)
	{
		return nullptr;
	}
	unsigned int col = 0xFFFFFFFF;
	SDL_LockSurface(s);
	memset(s->pixels, 0, h * s->pitch);
	for (int i = 0; i < h; i++)
	{
		col = 0xFFFFFF | (((unsigned int)(i * 1.3)) << 24);
		for (int j = 0; j < w; j++)
		{
			memcpy(((char *)s->pixels) + j * 4 + i * s->pitch, &col, 4);			
		}
	}
	SDL_UnlockSurface(s);
	auto t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, s));
	SDL_DestroySurface(s);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND);
	return t;
}

_shared_image EngineBase::createSnowflake()
{
	SDL_Surface * s = SDL_CreateSurface(3, 3, SDL_PIXELFORMAT_ARGB8888);
	if (s == nullptr)
	{
		return nullptr;
	}
	unsigned int col = 0xFFFFFFFF;
	SDL_LockSurface(s);
	memset(s->pixels, 0, 3 * s->pitch);
	memcpy(((char *)s->pixels) + 4, &col, 4);
	memcpy(((char *)s->pixels) + s->pitch * 1, &col, 4);
	memcpy(((char *)s->pixels) + 4 + s->pitch * 1, &col, 4);
	memcpy(((char *)s->pixels) + 8 + s->pitch * 1, &col, 4);
	memcpy(((char *)s->pixels) + 4 + s->pitch * 2, &col, 4);
	SDL_UnlockSurface(s);
	auto t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, s));
	SDL_DestroySurface(s);
	setImageAlpha(t, 0xB0);
	return t;
}

void EngineBase::loadLogo()
{
	//if (logo != nullptr)
	//{
	//	freeImage(logo);
	//	logo = nullptr;
	//}
	std::string logoFileName = "engine\\config\\logo.png";
	std::string legacyLogoFileName = "config\\logo.png";
	auto loadLogoFromFile = [&]() {
		logo = loadImageFromFile(logoFileName);
		if (logo == nullptr)
		{
			logo = loadImageFromFile(legacyLogoFileName);
		}
	};

#ifdef USE_LOGO_RESOURCE
	HRSRC hRsrc = FindResource(nullptr, MAKEINTRESOURCE(IDB_PNG1), "PNG");
	if (hRsrc == nullptr)
	{
		loadLogoFromFile();
		GameLog::write("Logo Loaded From File\n");
		return;
	}
	GameLog::write("Loading Logo From Resource\n");

	unsigned int size = SizeofResource(nullptr, hRsrc);
	if (size == 0)
	{
		loadLogoFromFile();
		return;
	}
	HGLOBAL hGlobal = LoadResource(nullptr, hRsrc);
	if (hGlobal == nullptr)
	{
		loadLogoFromFile();
		return;
	}
	LPVOID pBuffer = LockResource(hGlobal);
	if (pBuffer == nullptr)
	{
		loadLogoFromFile();
		return;
	}
	std::unique_ptr<char[]> data = std::make_unique<char[]>(size);
	memcpy(data.get(), pBuffer, size);
	logo = loadImageFromMem(data, size);
	UnlockResource(hGlobal);
	FreeResource(hGlobal);
	GameLog::write("Logo loaded from resource!");
#else
	loadLogoFromFile();
#endif // USE_LOGO_RESOURCE
}

void EngineBase::fadeInLogo()
{
	if (logo == nullptr)
	{
		return;
	}
	int w, h;
	if (getImageSize(logo, w, h))
	{
		const Rect source = { 0, 0, w, h };
		const Rect viewport = { 0, 0, width, height };
		unsigned char r, g, b;
		r = (clLogoBG & 0xFF0000) >> 16;
		g = (clLogoBG & 0xFF00) >> 8;
		b = clLogoBG & 0xFF;
		setScreenMask(r, g, b, 255);
		Timer t(&timer);
		t.reInit();
		auto now = t.get();
		while (now < LogoPhaseDurationMilliseconds)
		{
			frameBegin();
			if (!isFrameReady())
			{
				delay(16);
				now = t.get();
				continue;
			}
			drawScreenMask();
			const unsigned char alpha =
				calculateLogoFadeInAlpha(now);
			drawAspectFitImage(
				logo, source, viewport, true, alpha);
			frameEnd();
			now = t.get();
		}


		frameBegin();
		if (isFrameReady())
		{
			drawScreenMask();
			drawAspectFitImage(
				logo, source, viewport, true);
			frameEnd();
		}


		frameBegin();
		if (isFrameReady())
		{
			drawScreenMask();
			drawAspectFitImage(
				logo, source, viewport, true);
			frameEnd();
		}
	}
	return;
}

void EngineBase::fadeOutLogo()
{
	if (logo == nullptr)
	{
		return;
	}
	int w, h;
	if (getImageSize(logo, w, h))
	{
		const Rect source = { 0, 0, w, h };
		const Rect viewport = { 0, 0, width, height };
		Timer t(&timer);
		t.reInit();
		auto now = t.get();
		while (now < LogoPhaseDurationMilliseconds)
		{
			frameBegin();
			if (!isFrameReady())
			{
				delay(16);
				now = t.get();
				continue;
			}
			drawScreenMask();
			const unsigned char alpha =
				calculateLogoFadeOutAlpha(now);
			drawAspectFitImage(
				logo, source, viewport, true, alpha);
			frameEnd();
			now = t.get();
		}

		frameBegin();
		if (isFrameReady())
		{
			drawScreenMask();
			frameEnd();
		}
	}
}

void EngineBase::drawImageWithMask(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMask(image, x, y, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMask(_shared_image image, int x, int y, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	int w, h;
	getImageSize(image, w, h);
	drawImage(image, x, y);
	SDL_Rect sdlrect;
	sdlrect.x = x;
	sdlrect.y = y;
	sdlrect.w = w;
	sdlrect.h = h;
	drawImage(mask, nullptr, &sdlrect);
}

void EngineBase::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMask(image, src, dst, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	SDL_Rect d;
	SDL_Rect * pd = nullptr;
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	drawImage(image, src, dst);
	drawImage(mask, nullptr, pd);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMaskEx(image, x, y, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, int x, int y, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	int w, h;
	getImageSize(image, w, h);
	if (w <= 0 || h <= 0)
	{
		return;
	}
	Rect dst;
	dst.x = x;
	dst.w = w;
	dst.y = y;
	dst.h = h;
	drawImageWithMaskEx(image, nullptr, &dst, mask);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMaskEx(image, src, dst, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	if (image == nullptr ||
		mask == nullptr ||
		!canPrepareRenderFrame())
	{
		return;
	}
	int w, h;
	getImageSize(image, w, h);
	if (w <= 0 || h <= 0)
	{
		return;
	}

	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer == nullptr ||
		!canPrepareRenderFrame())
	{
		return;
	}
	auto temporaryTarget = make_shared_image(
		SDL_CreateTexture(
			activeRenderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_TARGET,
			w,
			h));
	if (temporaryTarget == nullptr)
	{
		return;
	}
	SDL_Texture* originalTarget =
		SDL_GetRenderTarget(activeRenderer);
	if (SetRenderTarget(
		activeRenderer,
		temporaryTarget.get()) == 0)
	{
		return;
	}

	SDL_BlendMode imageBlendMode;
	SDL_BlendMode maskBlendMode;
	SDL_GetTextureBlendMode(
		image.get(),
		&imageBlendMode);
	SDL_GetTextureBlendMode(
		mask.get(),
		&maskBlendMode);
	SDL_SetTextureBlendMode(
		image.get(),
		SDL_BLENDMODE_NONE);
	const bool imageRendered =
		SDL_RenderTexture(
			activeRenderer,
			image.get(),
			nullptr,
			nullptr);
	SDL_SetTextureBlendMode(
		mask.get(),
		SDL_BLENDMODE_ADD);
	const bool maskRendered =
		imageRendered &&
		SDL_RenderTexture(
			activeRenderer,
			mask.get(),
			nullptr,
			nullptr);
	SDL_SetTextureBlendMode(
		image.get(),
		imageBlendMode);
	SDL_SetTextureBlendMode(
		mask.get(),
		maskBlendMode);
	if (!restoreAcceptedRenderTarget(
		activeRenderer,
		originalTarget,
		temporaryTarget) ||
		!maskRendered ||
		!canPrepareRenderFrame())
	{
		return;
	}
	SDL_SetTextureBlendMode(
		temporaryTarget.get(),
		SDL_BLENDMODE_BLEND);
	drawImage(temporaryTarget, src, dst);
}

void EngineBase::setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (screenMask == nullptr)
	{
		return;
	}
	SDL_WriteSurfacePixel(screenMask.get(), 0, 0, r, g, b, 0xFF);
	SDL_SetSurfaceAlphaMod(screenMask.get(), a);
}

void EngineBase::drawScreenMask()
{
	if (screenMask == nullptr)
	{
		return;
	}
	auto t = make_shared_image(SDL_CreateTextureFromSurface(renderer, screenMask.get()));
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND);
	drawMask(t);
	//freeImage(t);
}

void EngineBase::drawMask(_shared_image mask)
{
	drawMask(mask, nullptr);
}

void EngineBase::drawMask(_shared_image mask, Rect* dst)
{
	if (mask == nullptr)
	{
		return;
	}
	SDL_Rect d;
	SDL_Rect * pd = nullptr;
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	drawImage(mask, nullptr, pd);
}

void EngineBase::handleEvent()
{
	applyApplicationLifecycleRequest();
	clearEventList();
	physicalInputManager->beginFrame();
	applyPhysicalInputLifecycleRequest();
	SDL_Event e;
	bool resizeEventGenerated = false;
	std::uint32_t queuedResizeGeneration = 0;
	timer.setPaused(true);
	while (SDL_PollEvent(&e))
	{
		if (physicalInputManager->processEvent(e))
		{
			continue;
		}
		switch (e.type)
		{
			case SDL_EVENT_QUIT:
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			{
				eventList.event.push(AEvent(ET_WINDOWCLOSE, 0, 0, 0));
				break;
			}
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			{
				physicalInputManager->setWindowFocused(false);
				break;
			}
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
			{
				physicalInputManager->setWindowFocused(true);
				break;
			}
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
			case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
			{
				int tempWidth = 0;
				int tempHeight = 0;
				SDL_GetWindowSize(window, &tempWidth, &tempHeight);
				if (handleWindowSizeChanged(tempWidth, tempHeight))
				{
					resizeEventGenerated = true;
					queuedResizeGeneration =
						recordLogicalResizeEvent();
				}
				break;
			}
			case SDL_EVENT_KEY_DOWN:
			{
				eventList.event.push(AEvent(ET_KEYDOWN, e.key.scancode, 0, 0, e.key.repeat));
				break;
			}
			case SDL_EVENT_KEY_UP:
			{
				eventList.event.push(AEvent(ET_KEYUP, e.key.scancode, 0, 0, e.key.repeat));
				break;
			}
			case SDL_EVENT_MOUSE_MOTION:
			{
                realMousePosX = (int)round(e.motion.x);
                realMousePosY = (int)round(e.motion.y);
//#ifndef __MOBILE__
				int tempX = -1, tempY = -1;
				calculateCursorReferencePosition(realMousePosX, realMousePosY, &tempX, &tempY);

				if (tempX >= 0 && tempY >= 0)
				{
					mouseX = tempX;
					mouseY = tempY;
					eventList.event.push(AEvent(EventType::ET_MOUSEMOTION, (int)TOUCH_MOUSEID, tempX, tempY));
				}
//#endif // (!defined __MOBILE__)
				break;
			}
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			{
                realMousePosX = (int)round(e.button.x);
                realMousePosY = (int)round(e.button.y);
//#ifndef __MOBILE__
				//鼠标点击时增加一个鼠标移动事件
				int tempX = -1, tempY = -1;
				calculateCursorReferencePosition(realMousePosX, realMousePosY, &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					mouseX = tempX;
					mouseY = tempY;
					eventList.event.push(AEvent((EventType)EventType::ET_MOUSEMOTION, (int)TOUCH_MOUSEID, tempX, tempY));
				}
				eventList.event.push(AEvent(ET_MOUSEDOWN, e.button.button, tempX, tempY));
//#endif // (!defined __MOBILE__)
				break;
			}
			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
                realMousePosX = (int)round(e.button.x);
                realMousePosY = (int)round(e.button.y);
//#ifndef __MOBILE__
				int tempX = -1, tempY = -1;
				calculateCursorReferencePosition(realMousePosX, realMousePosY, &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					mouseX = tempX;
					mouseY = tempY;
					eventList.event.push(AEvent((EventType)EventType::ET_MOUSEMOTION, (int)TOUCH_MOUSEID, tempX, tempY));
				}
				eventList.event.push(AEvent(ET_MOUSEUP, e.button.button, tempX, tempY));
//#endif // (!defined __MOBILE__)
				break;
			}
			case SDL_EVENT_MOUSE_WHEEL:
			{
				realMousePosX = (int)round(e.wheel.mouse_x);
				realMousePosY = (int)round(e.wheel.mouse_y);
				int tempX = -1, tempY = -1;
				calculateCursorReferencePosition(realMousePosX, realMousePosY, &tempX, &tempY);
				int wheelSteps = (int)round(e.wheel.y);
				if (wheelSteps == 0 && e.wheel.y != 0.0f)
				{
					wheelSteps = e.wheel.y > 0.0f ? 1 : -1;
				}
				if (wheelSteps != 0)
				{
					eventList.event.push(AEvent(ET_MOUSEWHEEL, -wheelSteps, tempX, tempY));
				}
				break;
			}
			case SDL_EVENT_FINGER_DOWN:
			{
				int tempWidth = 0;
				int tempHeight = 0;
				SDL_GetWindowSize(window, &tempWidth, &tempHeight);
				int tempX = -1;
				int tempY = -1;
				calculateCursorReferencePosition((int)round(e.tfinger.x * tempWidth), (int)round(e.tfinger.y * tempHeight), &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					if (e.tfinger.fingerID == TOUCH_MOUSEID)
					{
						mouseX = tempX;
						mouseY = tempY;
					}
					eventList.event.push(AEvent(ET_FINGERMOTION,
						static_cast<EventTouchID>(e.tfinger.fingerID), tempX, tempY));
				}
				eventList.event.push(AEvent(ET_FINGERDOWN,
					static_cast<EventTouchID>(e.tfinger.fingerID), tempX, tempY));
				break;
			}
			case SDL_EVENT_FINGER_UP:
			{
				int tempWidth = 0;
				int tempHeight = 0;
				SDL_GetWindowSize(window, &tempWidth, &tempHeight);
				int tempX = -1;
				int tempY = -1;
				calculateCursorReferencePosition((int)round(e.tfinger.x * tempWidth), (int)round(e.tfinger.y * tempHeight), &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					if (e.tfinger.fingerID == TOUCH_MOUSEID)
					{
						mouseX = tempX;
						mouseY = tempY;
					}
					eventList.event.push(AEvent(ET_FINGERMOTION,
						static_cast<EventTouchID>(e.tfinger.fingerID), tempX, tempY));
				}
				eventList.event.push(AEvent(ET_FINGERUP,
					static_cast<EventTouchID>(e.tfinger.fingerID), tempX, tempY));
				break;
			}
			case SDL_EVENT_FINGER_MOTION:
			{
				int tempWidth = 0;
				int tempHeight = 0;
				SDL_GetWindowSize(window, &tempWidth, &tempHeight);
				int tempX = -1;
				int tempY = -1;
				calculateCursorReferencePosition((int)round(e.tfinger.x * tempWidth), (int)round(e.tfinger.y * tempHeight), &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					if (e.tfinger.fingerID == TOUCH_MOUSEID)
					{
						mouseX = tempX;
						mouseY = tempY;
					}
					eventList.event.push(AEvent(ET_FINGERMOTION,
						static_cast<EventTouchID>(e.tfinger.fingerID), tempX, tempY));
				}
				break;
			}
			case SDL_EVENT_FINGER_CANCELED:
			{
				// SDL does not guarantee a meaningful final position for a canceled
				// contact. Preserve identity and route it through the non-committing
				// cancellation path instead of synthesizing an ordinary finger-up.
				eventList.event.push(AEvent(
					ET_FINGERCANCEL,
					static_cast<EventTouchID>(e.tfinger.fingerID), -1, -1));
				break;
			}
			default:
			{
				break;
			}			
		}
	}
	// Lifecycle events can be delivered while SDL_PollEvent pumps the queue.
	// Apply the latest state before polling controller state for this frame.
	applyPhysicalInputLifecycleRequest();
	physicalInputManager->update(SDL_GetTicks());
//#ifndef __MOBILE__
	int tempX = -1, tempY = -1;
	float mX, mY;
	SDL_GetMouseState(&mX, &mY);
	calculateCursorReferencePosition((int)round(mX), (int)round(mY), &tempX, &tempY);
	if (tempX >= 0 && tempY >= 0)
	{
		mouseX = tempX;
		mouseY = tempY;
		eventList.event.push(AEvent(
			EventType::ET_MOUSEMOTION,
			(int)TOUCH_MOUSEID,
			tempX,
			tempY,
			false,
			true));
	}
//#endif // !__MOBILE__

	finalizeLogicalResizeEventPump(
		resizeEventGenerated,
		queuedResizeGeneration);

	timer.setPaused(false);
}

std::uint32_t EngineBase::markLogicalResizePending()
{
	std::uint32_t generation =
		logicalResizeGeneration.fetch_add(
			1,
			std::memory_order_acq_rel) + 1;
	if (generation == 0)
	{
		generation =
			logicalResizeGeneration.fetch_add(
				1,
				std::memory_order_acq_rel) + 1;
	}
	return generation;
}

std::uint32_t EngineBase::recordLogicalResizeEvent()
{
	const std::uint32_t generation =
		markLogicalResizePending();
	if (canPrepareRenderFrame())
	{
		eventList.event.push(
			AEvent(
				ET_WINDOWRESIZE,
				static_cast<EventTouchID>(
					generation),
				width,
				height));
		return generation;
	}
	return 0;
}

void EngineBase::finalizeLogicalResizeEventPump(
	bool resizeEventGenerated,
	std::uint32_t queuedResizeGeneration)
{
	if (!canPrepareRenderFrame())
	{
		(void)resizeEventGenerated;
	}
	else if (hasPendingLogicalResizeEvent())
	{
		const std::uint32_t generation =
			logicalResizeGeneration.load(
				std::memory_order_acquire);
		if (queuedResizeGeneration != generation)
		{
			// Publication is not consumption. Repeat the latest generation
			// until the UI tree acknowledges it after a complete dispatch.
			eventList.event.push(
				AEvent(
					ET_WINDOWRESIZE,
					static_cast<EventTouchID>(
						generation),
					width,
					height));
		}
	}
}

bool EngineBase::hasPendingLogicalResizeEvent() const
{
	return logicalResizeGeneration.load(
		std::memory_order_acquire) !=
		acknowledgedLogicalResizeGeneration.load(
			std::memory_order_acquire);
}

void EngineBase::acknowledgeLogicalResizeEvent(
	std::uint32_t generation,
	int logicalWidth,
	int logicalHeight)
{
	if (generation == 0 ||
		generation != logicalResizeGeneration.load(
			std::memory_order_acquire) ||
		logicalWidth != width ||
		logicalHeight != height ||
		!canPrepareRenderFrame())
	{
		return;
	}
	acknowledgedLogicalResizeGeneration.store(
		generation,
		std::memory_order_release);
}

void EngineBase::pumpEvents()
{
	handleEvent();
}

void EngineBase::copyEvent(AEvent& s, AEvent& d)
{
	d = s;
}

void EngineBase::clearEventList()
{
	std::queue<AEvent> empty;
	std::swap(eventList.event, empty);
	// eventList.event = std::queue<AEvent>();
	// while (!eventList.event.empty()) { eventList.event.pop(); }
}

int EngineBase::getEventCount()
{
	return (int)eventList.event.size();
}

int EngineBase::getEvent(AEvent& event)
{
	if (eventList.event.size() == 0)
	{
		return 0;
	}
	copyEvent(eventList.event.front(), event);
	eventList.event.pop();
	return (int)eventList.event.size() + 1;
}

void EngineBase::pushEvent(AEvent& event)
{
	eventList.event.push(event);
}

bool EngineBase::getKeyPress(KeyCode key)
{
	return (SDL_GetKeyboardState(nullptr)[key] != 0);
}

bool EngineBase::getMousePress(MouseButtonCode button)
{
	return ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(button)) != 0);
}

void EngineBase::getMouse(int& x, int& y)
{
	x = mouseX;
	y = mouseY;
}

std::vector<AEvent> EngineBase::getAllFingersPosition()
{
	std::vector<AEvent> ret;
	int touchDeviceCount = 0;
	SDL_TouchID* ids = SDL_GetTouchDevices(&touchDeviceCount);
	if (ids == nullptr || touchDeviceCount == 0)
	{
		return ret;
	}

	for (int i = 0; i < touchDeviceCount; i++)
	{
		int fingerCount = 0;
		auto fingers = SDL_GetTouchFingers(ids[i], &fingerCount);
		if (fingers == nullptr || fingerCount == 0)
		{
			continue;
		}
		for (int j = 0; j < fingerCount; j++)
		{
			auto finger = (*fingers)[j];
			int tempWidth = 0;
			int tempHeight = 0;
			SDL_GetWindowSize(window, &tempWidth, &tempHeight);
			int tempX = -1;
			int tempY = -1;
			calculateCursorReferencePosition((int)round(finger.x * tempWidth), (int)round(finger.y * tempHeight), &tempX, &tempY);
			ret.emplace_back(ET_FINGERMOTION, finger.id, tempX, tempY);
		}
		SDL_free(fingers);
	}
	SDL_free(ids);
	return ret;
}

void EngineBase::resetEvent()
{
	handleEvent();
	clearEventList();
}

void EngineBase::setCursorHardware(bool isHardware)
{
	hardwareCursor = isHardware;
	if (hardwareCursor && !softwareCursorHidden)
	{
		CursorImageIndex = -1;
		if (!activateNativeAnimatedCursor() &&
			!cursorImage.image.empty() &&
			cursorImage.image[0].frame != nullptr)
		{
			CursorImageIndex = SDL_SetCursor(
				cursorImage.image[0].frame.get())
				? 0
				: -1;
		}
		SDL_ShowCursor();
	}
	else
	{
		SDL_HideCursor();
	}
}

void EngineBase::setFontName(const std::string & fontName)
{
	font = fontName;
	if (fontData != nullptr)
	{
		SDL_CloseIO(fontData);
		fontData = nullptr;
	}
	fontData = SDL_IOFromFile(fontName.c_str(), "rb");
	if (!fontData)
	{
		GameLog::write("there is no fontData\n");
	}
}

void EngineBase::drawTalk(const std::string& text, int x, int y, int size, unsigned int color)
{
	_shared_image t = createText(text, size, color);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_NONE);
	drawImage(t, x, y);
	//freeImage(t);
}

void EngineBase::setFontFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (data == nullptr || size <= 0)
	{
		return;
	}
	if (fontData != nullptr)
	{
		SDL_CloseIO(fontData);
		fontData = nullptr;
	}
	if (fontBuffer != nullptr)
	{
		fontBuffer = nullptr;
	}
	fontData = SDL_IOFromMem(data.get(), size);
	fontBuffer = std::move(data);
}

_shared_image EngineBase::createText(const std::string& text, int size, unsigned int color, bool safe)
{
	TTF_Font * _font = nullptr;
	if (!fontData)
	{
		_font = TTF_OpenFont(font.c_str(), size);
	}
	else
	{
		SDL_SeekIO(fontData, 0, SDL_IO_SEEK_SET);
		_font = TTF_OpenFontIO(fontData, 0, size);	
	}	
	if (!_font) 
	{ 
		return nullptr; 
	}

	SDL_Color c;
	c.b = (color & 0xFF);
	c.g = (color & 0xFF00) >> 8;
	c.r = (color & 0xFF0000) >> 16;	
	c.a = (color & 0xFF000000) >> 24;

	auto text_s = TTF_RenderText_Blended(_font, text.c_str(), text.size(), c);
	if (text_s == nullptr)
	{
		TTF_CloseFont(_font);
		return nullptr;
	}
	_shared_image text_t;
	if (safe)
	{
		text_t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, text_s));
	}
	else
	{
		text_t = make_shared_image(SDL_CreateTextureFromSurface(renderer, text_s));
	}

	setImageAlpha(text_t, c.a);

	SDL_DestroySurface(text_s);
	TTF_CloseFont(_font);
	return text_t;
}

_shared_image EngineBase::createTextWithFontData(
	const void* data,
	std::size_t dataSize,
	const std::string& text,
	int size,
	unsigned int color,
	int wrapWidth,
	bool safe)
{
	if (data == nullptr || dataSize == 0 || text.empty() || size <= 0)
	{
		return nullptr;
	}
	SDL_IOStream* stream = SDL_IOFromConstMem(data, dataSize);
	if (stream == nullptr)
	{
		return nullptr;
	}
	TTF_Font* textFont = TTF_OpenFontIO(stream, 0, size);
	if (textFont == nullptr)
	{
		SDL_CloseIO(stream);
		return nullptr;
	}

	SDL_Color textColor;
	textColor.b = (color & 0xFF);
	textColor.g = (color & 0xFF00) >> 8;
	textColor.r = (color & 0xFF0000) >> 16;
	textColor.a = (color & 0xFF000000) >> 24;
	SDL_Surface* surface = wrapWidth > 0
		? TTF_RenderText_Blended_Wrapped(
			textFont, text.c_str(), text.size(), textColor, wrapWidth)
		: TTF_RenderText_Blended(
			textFont, text.c_str(), text.size(), textColor);
	if (surface == nullptr)
	{
		TTF_CloseFont(textFont);
		SDL_CloseIO(stream);
		return nullptr;
	}
	_shared_image image;
	if (safe)
	{
		image = make_safe_shared_image(
			SDL_CreateTextureFromSurface(renderer, surface));
	}
	else
	{
		image = make_shared_image(
			SDL_CreateTextureFromSurface(renderer, surface));
	}
	setImageAlpha(image, textColor.a);
	SDL_DestroySurface(surface);
	TTF_CloseFont(textFont);
	SDL_CloseIO(stream);
	return image;
}

void EngineBase::drawText(const std::string & text, int x, int y, int size, unsigned int color)
{
	_shared_image t = createText(text, size, color);
	drawImage(t, x, y);	
	//freeImage(t);
}

bool EngineBase::enginebaseAppEventHandler(void* userdata, SDL_Event* event)
{
	EngineBase* engine = static_cast<EngineBase*>(userdata);
	if (_externalEventHandler != NULL)
	{
		_externalEventHandler(event);
	}

	switch (event->type)
	{
	case SDL_EVENT_TERMINATING:
		/* Terminate the app.
			Shut everything down before returning from this function.
		*/
		if (engine != nullptr)
		{
			engine->queueApplicationLifecycleRequest(
				event->type);
		}
		return false;
	case SDL_EVENT_LOW_MEMORY:
		/* You will get this when your app is paused and iOS wants more memory.
			Release as much memory as possible.
		*/
		return false;
	case SDL_EVENT_WILL_ENTER_BACKGROUND:
		/* Prepare your app to go into the background.  Stop loops, etc.
			This gets called when the user hits the home button, or gets a call.
		*/
		if (engine != nullptr)
		{
			engine->queueApplicationLifecycleRequest(
				event->type);
		}
		return false;
	case SDL_EVENT_DID_ENTER_BACKGROUND:
		/* This will get called if the user accepted whatever sent your app to the background.
			If the user got a phone call and canceled it, you'll instead get an SDL_EVENT_DIDENTERFOREGROUND event and restart your loops.
			When you get this, you have 5 seconds to save all your state or the app will be terminated.
			Your app is NOT active at this point.
		*/
		if (engine != nullptr)
		{
			engine->queueApplicationLifecycleRequest(
				event->type);
		}
		return false;
	case SDL_EVENT_WILL_ENTER_FOREGROUND:
		/* This call happens when your app is coming back to the foreground.
			Restore all your state here.
		*/
		// The renderer is not usable until DID_ENTER_FOREGROUND. Keep the base
		// render gate closed so frameBegin() cannot recreate or clear textures
		// in the WILL/DID transition window.
		if (engine != nullptr)
		{
			engine->queueApplicationLifecycleRequest(
				event->type);
		}
		return false;
	case SDL_EVENT_DID_ENTER_FOREGROUND:
		/* Restart your loops here.
			Your app is interactive and getting CPU again.
		*/
		if (engine != nullptr)
		{
			engine->queueApplicationLifecycleRequest(
				event->type);
		}
		return false;
	default:
		/* No special processing, add it to the event queue */
		return true;
	}
}

void EngineBase::queueApplicationLifecycleRequest(
	Uint32 eventType)
{
	bool backgroundRequested = true;
	PhysicalInputLifecycleRequest inputRequest =
		PhysicalInputLifecycleRequest::Suspend;
	switch (eventType)
	{
	case SDL_EVENT_TERMINATING:
	case SDL_EVENT_WILL_ENTER_BACKGROUND:
	case SDL_EVENT_DID_ENTER_BACKGROUND:
	case SDL_EVENT_WILL_ENTER_FOREGROUND:
		break;
	case SDL_EVENT_DID_ENTER_FOREGROUND:
		backgroundRequested = false;
		inputRequest =
			PhysicalInputLifecycleRequest::Resume;
		break;
	default:
		return;
	}

	std::uint64_t currentState =
		applicationLifecycleRequestState.load(
			std::memory_order_relaxed);
	std::uint64_t requestedState = 0;
	do
	{
		const std::uint64_t nextRevision =
			((currentState &
				~(LifecycleBackgroundBit |
					LifecycleRenderClosedBit)) +
				LifecycleRevisionIncrement);
		requestedState =
			nextRevision |
			LifecycleRenderClosedBit |
			(backgroundRequested
				? LifecycleBackgroundBit
				: 0ULL);
	}
	while (!applicationLifecycleRequestState.
		compare_exchange_weak(
			currentState,
			requestedState,
			std::memory_order_release,
			std::memory_order_relaxed));

	currentFrameReady.store(
		false,
		std::memory_order_release);
	physicalInputLifecycleRequest.store(
		inputRequest,
		std::memory_order_release);
}

void EngineBase::applyApplicationLifecycleRequest()
{
	std::uint64_t requestedState =
		applicationLifecycleRequestState.load(
			std::memory_order_acquire);
	const std::uint64_t requestedRevision =
		requestedState &
		~(LifecycleBackgroundBit |
			LifecycleRenderClosedBit);
	if (requestedRevision ==
		appliedApplicationLifecycleRevision)
	{
		return;
	}
	appliedApplicationLifecycleRevision =
		requestedRevision;
	const bool backgroundRequested =
		(requestedState &
			LifecycleBackgroundBit) != 0;
	isBackGround.store(
		backgroundRequested,
		std::memory_order_release);
	if (backgroundRequested ||
		(requestedState &
			LifecycleRenderClosedBit) == 0)
	{
		return;
	}

	const std::uint64_t reopenedState =
		requestedState &
		~LifecycleRenderClosedBit;
	(void)applicationLifecycleRequestState.
		compare_exchange_strong(
			requestedState,
			reopenedState,
			std::memory_order_acq_rel,
			std::memory_order_acquire);
}

bool EngineBase::isRenderAdmissionClosed() const
{
	return (applicationLifecycleRequestState.load(
		std::memory_order_acquire) &
		LifecycleRenderClosedBit) != 0;
}

int EngineBase::SetRenderTarget(SDL_Renderer* r, SDL_Texture* t)
{
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	if (!canPrepareRenderFrame())
	{
		return 0;
	}
	const int result = SDL_SetRenderTarget(r, t);
	return result;
}

InitErrorType EngineBase::init(const std::string & windowCaption, int & wWidth, int & wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, int display, AppEventHandler eventHandler)
{
	currentFrameReady.store(false);
	isBackGround.store(false);
	applicationLifecycleRequestState.store(
		0,
		std::memory_order_release);
	appliedApplicationLifecycleRevision = 0;
	physicalInputLifecycleRequest.store(PhysicalInputLifecycleRequest::None);
	logicalResizeGeneration.store(
		0,
		std::memory_order_release);
	acknowledgedLogicalResizeGeneration.store(
		0,
		std::memory_order_release);
	pendingLogicalScreenTextureResize = false;
	pendingWindowResize = false;
	width = wWidth;
	height = wHeight;
	if (initSDL(windowCaption, wWidth, wHeight, fullScreenMode, fullScreenSolutionMode, display) != initOK)
	{
		GameLog::write("Init SDL Error!\n");
		return sdlError;
	}
	
    wWidth = width;
    wHeight = height;
    
	_externalEventHandler = eventHandler;
	SDL_SetEventFilter(enginebaseAppEventHandler, this);

#ifdef SHF_USE_AUDIO
	if (initSoundSystem() != 0)
	{
		GameLog::write("Init Sound Error!\n");
		return soundError;
	}
#endif

#ifdef SHF_USE_VIDEO
	if (initVideo() != 0)
	{
		GameLog::write("Init Video Error!\n");
		return videoError;
	}
#endif

	if (lzo_init() != LZO_E_OK)
	{
		GameLog::write("Init miniLZO Error!\n");
		return LZOError;
	}
	SDL_StopTextInput(window);
	fadeOutLogo();

	resetEvent();

	if (hardwareCursor)
	{
		showCursor();
	}
	return initOK;
}

void EngineBase::destroyEngineBase()
{
	if (lzoMem != nullptr)
	{
		freeMem(lzoMem);
		lzoMem = nullptr;
	}

	//freeImage(logo);

#ifdef SHF_USE_VIDEO
	destroyVideo();
#endif
#ifdef SHF_USE_AUDIO
	destroySoundSystem();
#endif
    SDL_SetEventFilter(nullptr, nullptr);
	destroySDL();
}

void EngineBase::setFullScreen(FullScreenMode mode)
{
	if (mode == _fullScreenMode)
	{
		return;
	}
#if defined(__MOBILE__)
	_fullScreenMode = mode;
	if (mode == FullScreenMode::window)
	{
		(void)SDL_SetWindowFullscreen(window, false);
		(void)SDL_SetWindowSize(window, width, height);
	}
	else
	{
		(void)SDL_SetWindowFullscreenMode(window, nullptr);
		(void)SDL_SetWindowFullscreen(window, true);
	}
	updateState();
#else
	DesktopDisplaySettings settings = getDesktopDisplaySettings();
	settings.fullScreenMode = mode;
	(void)applyDesktopDisplaySettings(settings);
#endif
}

std::vector<DesktopDisplayInfo> EngineBase::getDesktopDisplays() const
{
	std::vector<DesktopDisplayInfo> result;
#if defined(__MOBILE__)
	return result;
#else
	int displayCount = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
	if (displays == nullptr || displayCount <= 0)
	{
		SDL_free(displays);
		return result;
	}
	result.reserve(displayCount);
	for (int displayIndex = 0; displayIndex < displayCount; displayIndex++)
	{
		DesktopDisplayInfo info;
		info.index = displayIndex;
		const SDL_DisplayID displayID = displays[displayIndex];
		const char* displayName = SDL_GetDisplayName(displayID);
		if (displayName != nullptr)
		{
			info.name = displayName;
		}
		const SDL_DisplayMode* desktopMode =
			SDL_GetDesktopDisplayMode(displayID);
		if (desktopMode != nullptr)
		{
			info.desktopWidth = desktopMode->w;
			info.desktopHeight = desktopMode->h;
		}
		SDL_Rect usableBounds = {};
		if (SDL_GetDisplayUsableBounds(displayID, &usableBounds))
		{
			info.usableWidth = usableBounds.w;
			info.usableHeight = usableBounds.h;
		}
		if (info.usableWidth <= 0 || info.usableHeight <= 0)
		{
			info.usableWidth = info.desktopWidth;
			info.usableHeight = info.desktopHeight;
		}

		int modeCount = 0;
		SDL_DisplayMode** displayModes =
			SDL_GetFullscreenDisplayModes(displayID, &modeCount);
		for (int modeIndex = 0; displayModes != nullptr &&
			modeIndex < modeCount; modeIndex++)
		{
			const SDL_DisplayMode* displayMode = displayModes[modeIndex];
			if (displayMode == nullptr ||
				displayMode->w < LogicalResolutionPolicy::MinimumWidth ||
				displayMode->h < LogicalResolutionPolicy::MinimumHeight)
			{
				continue;
			}
			info.fullscreenResolutions.push_back(
				{ displayMode->w, displayMode->h });
		}
		SDL_free(displayModes);
		std::sort(info.fullscreenResolutions.begin(),
			info.fullscreenResolutions.end(),
			[](const DesktopDisplayResolution& left,
				const DesktopDisplayResolution& right)
			{
				return left.width != right.width
					? left.width < right.width
					: left.height < right.height;
			});
		info.fullscreenResolutions.erase(
			std::unique(info.fullscreenResolutions.begin(),
				info.fullscreenResolutions.end()),
			info.fullscreenResolutions.end());
		result.push_back(std::move(info));
	}
	SDL_free(displays);
	return result;
#endif
}

DesktopDisplaySettings EngineBase::getDesktopDisplaySettings() const
{
	DesktopDisplaySettings settings;
	settings.width = requestedLogicalWidth > 0
		? requestedLogicalWidth : width;
	settings.height = requestedLogicalHeight > 0
		? requestedLogicalHeight : height;
	settings.fullScreenMode = _fullScreenMode;
	settings.fullScreenSolutionMode = _fullScreenSolutionMode;
#if !defined(__MOBILE__)
	if (window != nullptr)
	{
		settings.displayIndex = resolveDesktopDisplayIndex(
			SDL_GetDisplayForWindow(window));
		if (_fullScreenMode == FullScreenMode::window)
		{
			int windowWidth = 0;
			int windowHeight = 0;
			if (SDL_GetWindowSize(window, &windowWidth, &windowHeight))
			{
				settings.width = windowWidth;
				settings.height = windowHeight;
			}
		}
	}
#endif
	LogicalResolutionPolicy::constrain(settings.width, settings.height);
	return settings;
}

bool EngineBase::applyDesktopDisplaySettings(
	const DesktopDisplaySettings& requestedSettings)
{
#if defined(__MOBILE__)
	(void)requestedSettings;
	return false;
#else
	if (window == nullptr)
	{
		return false;
	}
	DesktopDisplaySettings settings = requestedSettings;
	settings.displayIndex = settings.displayIndex < 0
		? 0 : settings.displayIndex;
	if (settings.fullScreenMode != FullScreenMode::window &&
		settings.fullScreenSolutionMode ==
			FullScreenSolutionMode::original)
	{
		settings.fullScreenSolutionMode =
			FullScreenSolutionMode::adjust;
	}
	LogicalResolutionPolicy::constrain(settings.width, settings.height);
	const DesktopDisplaySettings previousSettings =
		getDesktopDisplaySettings();

	auto applySettings = [this](const DesktopDisplaySettings& value)
	{
		const SDL_DisplayID displayID =
			resolveDesktopDisplayID(value.displayIndex);
		if (displayID == 0 || !SDL_SetWindowFullscreen(window, false))
		{
			return false;
		}
		if (!SDL_SetWindowPosition(window,
			SDL_WINDOWPOS_CENTERED_DISPLAY(displayID),
			SDL_WINDOWPOS_CENTERED_DISPLAY(displayID)))
		{
			return false;
		}
		(void)SDL_SyncWindow(window);

		if (value.fullScreenMode == FullScreenMode::window)
		{
			if (!SDL_SetWindowFullscreenMode(window, nullptr) ||
				!SDL_SetWindowSize(window, value.width, value.height))
			{
				return false;
			}
			if (!SDL_SetWindowPosition(window,
				SDL_WINDOWPOS_CENTERED_DISPLAY(displayID),
				SDL_WINDOWPOS_CENTERED_DISPLAY(displayID)))
			{
				return false;
			}
		}
		else if (value.fullScreenMode == FullScreenMode::windowFullScreen)
		{
			if (!SDL_SetWindowFullscreenMode(window, nullptr) ||
				!SDL_SetWindowFullscreen(window, true))
			{
				return false;
			}
		}
		else
		{
			SDL_DisplayMode closestMode = {};
			if (!SDL_GetClosestFullscreenDisplayMode(
				displayID, value.width, value.height, 0.0f, false,
				&closestMode) ||
				!SDL_SetWindowFullscreenMode(window, &closestMode) ||
				!SDL_SetWindowFullscreen(window, true))
			{
				return false;
			}
		}

		(void)SDL_SyncWindow(window);
		_fullScreenMode = value.fullScreenMode;
		_fullScreenSolutionMode = value.fullScreenSolutionMode;
		requestedLogicalWidth = value.width;
		requestedLogicalHeight = value.height;
		int actualWidth = value.width;
		int actualHeight = value.height;
		(void)SDL_GetWindowSize(window, &actualWidth, &actualHeight);
		(void)handleWindowSizeChanged(actualWidth, actualHeight);
		updateState();
		return true;
	};

	if (applySettings(settings))
	{
		return true;
	}
	GameLog::write("Unable to apply desktop display settings: %s\n",
		SDL_GetError());
	if (!applySettings(previousSettings))
	{
		GameLog::write("Unable to restore previous display settings: %s\n",
			SDL_GetError());
	}
	return false;
#endif
}

void EngineBase::getScreenInfo(int& w, int& h)
{	
	const SDL_DisplayMode* sdl_dm = nullptr;
	if (window == nullptr)
	{
		sdl_dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
	}
	else
	{
		sdl_dm = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
	}
	
	if (sdl_dm == nullptr)
	{
		GameLog::write("SDL_GetCurrentDisplayMode Error: %s", SDL_GetError()); 
		return;
	}
	w = sdl_dm->w;
	h = sdl_dm->h;
	
}

void EngineBase::setWindowSize(int w, int h)
{
	LogicalResolutionPolicy::constrain(w, h);
	if (resizeLogicalScreen(w, h, true))
	{
		(void)markLogicalResizePending();
	}
}

void EngineBase::getLogicalWindowSize(int& w, int& h) const
{
	w = width;
	h = height;
}

void EngineBase::calculateLogicalSizeForScreen(int screenWidth, int screenHeight, int& logicalWidth, int& logicalHeight) const
{
	logicalWidth = requestedLogicalWidth > 0 ? requestedLogicalWidth : width;
	logicalHeight = requestedLogicalHeight > 0 ? requestedLogicalHeight : height;

	if (screenWidth <= 0 || screenHeight <= 0 || logicalWidth <= 0 || logicalHeight <= 0)
	{
		return;
	}

	if (_fullScreenSolutionMode == FullScreenSolutionMode::forceToUseSetting)
	{
		return;
	}

	if (_fullScreenMode == FullScreenMode::window)
	{
		if (((float)screenWidth) / screenHeight > ((float)logicalWidth) / logicalHeight)
		{
			logicalWidth = (int)round(((float)logicalHeight) * screenWidth / screenHeight);
		}
		else if (((float)screenWidth) / screenHeight < ((float)logicalWidth) / logicalHeight)
		{
			logicalHeight = (int)round(((float)logicalWidth) * screenHeight / screenWidth);
		}
		return;
	}

	if (_fullScreenSolutionMode == FullScreenSolutionMode::original)
	{
		logicalWidth = screenWidth;
		logicalHeight = screenHeight;
	}
	else if (_fullScreenSolutionMode == FullScreenSolutionMode::adjust)
	{
		if (((float)screenWidth) / screenHeight > ((float)logicalWidth) / logicalHeight)
		{
			logicalWidth = (int)round(((float)logicalHeight) * screenWidth / screenHeight);
		}
		else if (((float)screenWidth) / screenHeight < ((float)logicalWidth) / logicalHeight)
		{
			logicalHeight = (int)round(((float)logicalWidth) * screenHeight / screenWidth);
		}
	}
}

bool EngineBase::resizeLogicalScreen(int logicalWidth, int logicalHeight, bool resizeWindow)
{
	LogicalResolutionPolicy::constrain(logicalWidth, logicalHeight);

	bool sizeChanged = logicalWidth != width || logicalHeight != height;
	if (resizeWindow && window != nullptr)
	{
		if (canPrepareRenderFrame())
		{
			SDL_SetWindowSize(
				window,
				logicalWidth,
				logicalHeight);
			pendingWindowResize = false;
		}
		else
		{
			pendingWindowResize = true;
		}
	}
	if (sizeChanged)
	{
		width = logicalWidth;
		height = logicalHeight;
		pendingLogicalScreenTextureResize = renderer != nullptr;
		if (!canPrepareRenderFrame())
		{
			return true;
		}
		if (pendingLogicalScreenTextureResize)
		{
			pendingLogicalScreenTextureResize = !recreateLogicalScreenTexture();
		}
		updateState();
		return true;
	}

	if (!canPrepareRenderFrame())
	{
		return false;
	}
	updateState();
	return false;
}

bool EngineBase::recreateLogicalScreenTexture()
{
	std::lock_guard<std::recursive_mutex> locker(
		renderTargetSessionMutex);
	if (renderTargetSession.kind !=
		RenderTargetSessionKind::none)
	{
		// A persistent talk/save session owns the renderer's current target
		// across calls and stores realScreen as its raw restore target. Keep
		// that texture alive until the session ends; frameBegin will retry the
		// pending logical-screen recreation afterwards.
		return false;
	}
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer == nullptr || !canPrepareRenderFrame())
	{
		return false;
	}

	SDL_Texture* previousTarget = SDL_GetRenderTarget(activeRenderer);
	SDL_Texture* previousLogicalScreen = realScreen.get();
	auto newScreen = make_shared_image(SDL_CreateTexture(activeRenderer,
		SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height));
	if (newScreen == nullptr)
	{
		return false;
	}
	if (!configureLogicalScreenTexture(newScreen.get()))
	{
		return false;
	}
	if (SetRenderTarget(activeRenderer, newScreen.get()) == 0)
	{
		return false;
	}
	SDL_SetRenderDrawColor(activeRenderer, 0, 0, 0, 0);
	SDL_RenderClear(activeRenderer);
	SDL_Texture* restoreTarget = previousTarget == previousLogicalScreen
		? newScreen.get()
		: previousTarget;
	const bool targetRestored =
		restoreAcceptedRenderTarget(
			activeRenderer,
			restoreTarget,
			newScreen);
	realScreen = newScreen;
	clearGrayscaleImageCache();
	return targetRestored;
}

bool EngineBase::handleWindowSizeChanged(int screenWidth, int screenHeight)
{
	if (screenWidth <= 0 || screenHeight <= 0)
	{
		return false;
	}

	int logicalWidth = width;
	int logicalHeight = height;
	calculateLogicalSizeForScreen(screenWidth, screenHeight, logicalWidth, logicalHeight);
	return resizeLogicalScreen(logicalWidth, logicalHeight, false);
}

void EngineBase::countFPS()
{
	if (SDL_GetTicks() - FPSTime > 1000)
	{
		FPS = FPSCounter;
		FPSTime = SDL_GetTicks();
		FPSCounter = 0;
	}
	else
	{
		FPSCounter++;
	}	
}

InitErrorType EngineBase::initSDL(const std::string & windowCaption, int wWidth, int wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, int display)
{
	LogicalResolutionPolicy::constrain(wWidth, wHeight);
	requestedLogicalWidth = wWidth;
	requestedLogicalHeight = wHeight;

#if defined(__LINUX__)
	configureLinuxApplicationMetadata(windowCaption);
#endif

	if (!DesktopCursorDpiPolicy::configure())
	{
		GameLog::write(
			"SDL desktop cursor DPI scaling could not be enabled; "
			"custom cursors may use their source pixel size\n");
	}

#ifdef __MOBILE__
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

#ifdef __MOBILE__
    fullScreenMode = FullScreenMode::fullScreen;
    fullScreenSolutionMode = FullScreenSolutionMode::adjust;
#endif
    _fullScreenMode = fullScreenMode;
    _fullScreenSolutionMode = fullScreenSolutionMode;

	if (!SDL_Init( SDL_INIT_AUDIO | SDL_INIT_VIDEO  | SDL_INIT_EVENTS))
	{
		GameLog::write("SDL error: %s \n", SDL_GetError());
		return sdlError;
	}
	if (SDL_InitSubSystem(SDL_INIT_GAMEPAD))
	{
		gamepadSubsystemInitialized = true;
		if (!physicalInputManager->initialize())
		{
			GameLog::write("SDL Gamepad input manager initialization failed\n");
		}
	}
	else
	{
		GameLog::write("SDL Gamepad subsystem unavailable: %s\n", SDL_GetError());
	}
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
	//SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

	SDL_DisplayID initialDisplayID = SDL_GetPrimaryDisplay();
#if !defined(__MOBILE__)
	initialDisplayID = resolveDesktopDisplayID(display);
#endif
    int screenWidth = wWidth;
    int screenHeight = wHeight;
	if (fullScreenMode != FullScreenMode::window)
	{
		const SDL_DisplayMode* desktopMode =
			SDL_GetDesktopDisplayMode(initialDisplayID);
		if (desktopMode != nullptr)
		{
			screenWidth = desktopMode->w;
			screenHeight = desktopMode->h;
		}
	}
    

	calculateLogicalSizeForScreen(screenWidth, screenHeight, width, height);
	LogicalResolutionPolicy::constrain(width, height);

	SDL_HideCursor();
	uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	uint32_t platformGraphicsFlag = 0;
#ifdef __APPLE__
	platformGraphicsFlag = SDL_WINDOW_METAL;
#elif defined(__ANDROID__)
	platformGraphicsFlag = 0;
#else
	platformGraphicsFlag = SDL_WINDOW_VULKAN;
#endif

#if defined(__MOBILE__)
	flags |= SDL_WINDOW_FULLSCREEN;
#else
	flags |= SDL_WINDOW_HIDDEN;
#endif
	GameLog::write("SDL Creating Window and Renderer");
	SDL_Renderer* tempRenderer = nullptr;
	uint32_t createFlags = flags | platformGraphicsFlag;
	if (!SDL_CreateWindowAndRenderer(windowCaption.c_str(), width, height, createFlags, &window, &tempRenderer))
	{
		GameLog::write("SDL Create Window and Renderer Error : %s", SDL_GetError());
#ifndef __APPLE__
		if (platformGraphicsFlag != 0)
		{
			GameLog::write("Retry SDL Create Window and Renderer without Vulkan flag");
			if (!SDL_CreateWindowAndRenderer(windowCaption.c_str(), width, height, flags, &window, &tempRenderer))
			{
				GameLog::write("SDL Create Window and Renderer Retry Error : %s", SDL_GetError());
				return sdlError;
			}
		}
		else
		{
			return sdlError;
		}
#else
		return sdlError;
#endif
	}

	SDL_SetWindowPosition(window,
		SDL_WINDOWPOS_CENTERED_DISPLAY(initialDisplayID),
		SDL_WINDOWPOS_CENTERED_DISPLAY(initialDisplayID));
#if !defined(__MOBILE__)
	if (!SDL_SetWindowMinimumSize(window,
		LogicalResolutionPolicy::MinimumWidth,
		LogicalResolutionPolicy::MinimumHeight))
	{
		GameLog::write("SDL_SetWindowMinimumSize Error: %s\n", SDL_GetError());
	}
#endif

	renderer.store(tempRenderer); //SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!SDL_SetRenderVSync(renderer.load(), 1))
	{
		GameLog::write("SDL_SetRenderVSync Error : %s", SDL_GetError());
		return sdlError;
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	realScreen = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height));
	if (realScreen == nullptr)
	{
		GameLog::write("RealScreen Creation Error : %s", SDL_GetError());
		return sdlError;
	}
	if (!configureLogicalScreenTexture(realScreen.get()))
	{
		return sdlError;
	}

#if !defined(__MOBILE__)
	// The configured display value is an ordinal. Resolve it to SDL's current
	// process-local display ID only while applying the settings.
	_fullScreenMode = FullScreenMode::window;
	DesktopDisplaySettings initialSettings;
	initialSettings.displayIndex = display;
	initialSettings.width = wWidth;
	initialSettings.height = wHeight;
	initialSettings.fullScreenMode = fullScreenMode;
	initialSettings.fullScreenSolutionMode = fullScreenSolutionMode;
	if (!applyDesktopDisplaySettings(initialSettings))
	{
		GameLog::write(
			"Configured desktop display mode was unavailable; using a window\n");
	}
#elif defined(__MOBILE__)
	if (fullScreenMode == FullScreenMode::windowFullScreen)
	{
		SDL_SetWindowFullscreenMode(window, nullptr);
	}
#endif
	screenMask = make_shared_surface(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_ARGB8888));
	setScreenMask(0, 0, 0, 0);

	int actualWindowWidth = 0;
	int actualWindowHeight = 0;
	SDL_GetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
	handleWindowSizeChanged(actualWindowWidth, actualWindowHeight);

	initTime();
	
	updateState();

	loadLogo();

	SDL_ShowWindow(window);
	SDL_RaiseWindow(window);

	fadeInLogo();

	TTF_Init();

	clearCursor();

	initTime();

#if defined(__LINUX__)
	// Some Linux window managers replace icon properties while the initially
	// hidden SDL window is being mapped. Apply the icon after startup rendering
	// has completed so the final mapped window retains it.
	setLinuxWindowIcon(window);
#endif

	return initOK;
}

void EngineBase::destroySDL()
{
	currentFrameReady.store(false);
	GameLog::write("Begin destroy SDL\n");
	physicalInputManager->shutdown();
	if (gamepadSubsystemInitialized)
	{
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
		gamepadSubsystemInitialized = false;
	}
	resetRenderTargetSessionForShutdown();
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer != nullptr &&
		!SDL_SetRenderTarget(activeRenderer, nullptr))
	{
		GameLog::write(
			"SDL_SetRenderTarget Error(renderer shutdown): %s",
			SDL_GetError());
	}
	clearGrayscaleImageCache();
	clearCursor();
	hiddenCursor.reset();
	logo.reset();
	realScreen.reset();
	ImageThreadSafety::flushPendingTextureDestructions();
	if (fontBuffer != nullptr)
	{
		fontBuffer = nullptr;
	}
	if (fontData != nullptr)
	{
		SDL_CloseIO(fontData);
		fontData = nullptr;
	}
	/*if (screenMask)
	{
		SDL_DestroySurface((SDL_Surface *)screenMask);
		screenMask = nullptr;
	}*/
    GameLog::write("Begin destroy SDL Renderer \n");
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    
    GameLog::write("Begin destroy SDL Window \n");
    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
     
	SDL_Quit();
	GameLog::write("Destroy SDL done!\n");
}

void EngineBase::updateState()
{
    int tempWidth = 0;
    int tempHeight = 0;
    SDL_GetWindowSize(window, &tempWidth, &tempHeight);
	updateRect(tempWidth, tempHeight, rect);
#ifdef __APPLE__
    SDL_GetWindowSizeInPixels(window, &tempWidth, &tempHeight);
    updateRect(tempWidth, tempHeight, displayRect);
#else
    displayRect = rect;
#endif
}
#ifdef SHF_USE_AUDIO
int EngineBase::initSoundSystem()
{
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	if (!SDL_WasInit(SDL_INIT_AUDIO))
	{
		return soundError;
	}
	if (!sdlMixerInitialized && !MIX_Init())
	{
		GameLog::write("Init SDL3_mixer Error: %s\n", SDL_GetError());
		return soundError;
	}
	sdlMixerInitialized = true;
	if (sdlMixer == nullptr)
	{
		sdlMixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
		if (sdlMixer == nullptr)
		{
			GameLog::write("Create SDL3_mixer Device Error: %s\n", SDL_GetError());
			MIX_Quit();
			sdlMixerInitialized = false;
			return soundError;
		}
	}
	return 0;
}

_channel EngineBase::registerAudioChannel(std::unique_ptr<Channel_t> channel)
{
	if (channel == nullptr)
	{
		return nullptr;
	}
	for (std::size_t slotIndex = 0; slotIndex < channelSlots.size(); slotIndex++)
	{
		auto& slot = channelSlots[slotIndex];
		if (slot.channel == nullptr)
		{
			slot.channel = std::move(channel);
			return AudioChannelHandle(slotIndex, slot.generation);
		}
	}

	AudioChannelSlot slot;
	slot.channel = std::move(channel);
	channelSlots.push_back(std::move(slot));
	return AudioChannelHandle(channelSlots.size() - 1, channelSlots.back().generation);
}

Channel_t* EngineBase::resolveAudioChannel(_channel handle)
{
	if (handle.slotIndex >= channelSlots.size())
	{
		return nullptr;
	}
	auto& slot = channelSlots[handle.slotIndex];
	if (slot.channel == nullptr || slot.generation != handle.generation)
	{
		return nullptr;
	}
	return slot.channel.get();
}

void EngineBase::releaseAudioChannelSlot(std::size_t slotIndex)
{
	if (slotIndex >= channelSlots.size())
	{
		return;
	}
	auto& slot = channelSlots[slotIndex];
	if (slot.channel == nullptr)
	{
		return;
	}
	destroyChannelStream(slot.channel.get());
	slot.channel.reset();
	slot.generation++;
	if (slot.generation == 0)
	{
		slot.generation = 1;
	}
}

void EngineBase::releaseAudioChannel(_channel handle)
{
	if (resolveAudioChannel(handle) == nullptr)
	{
		return;
	}
	releaseAudioChannelSlot(handle.slotIndex);
}

void EngineBase::clearAudioChannels()
{
	for (std::size_t slotIndex = 0; slotIndex < channelSlots.size(); slotIndex++)
	{
		releaseAudioChannelSlot(slotIndex);
	}
}

void EngineBase::destroySoundSystem()
{
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	GameLog::write("Begin destroy sound system\n");
	for (auto iter = soundList.begin(); iter != soundList.end(); iter++)
	{
		//stopMusic(iter->c);
		if (iter->m != nullptr)
		{
			freeMusic(iter->m);
		}
	}
	soundList.resize(0);
	clearAudioChannels();
	if (sdlMixer != nullptr)
	{
		MIX_DestroyMixer(sdlMixer);
		sdlMixer = nullptr;
	}
	if (sdlMixerInitialized)
	{
		MIX_Quit();
		sdlMixerInitialized = false;
	}
	GameLog::write("Destroy sound system done!\n");
}

void EngineBase::updateSoundSystem()
{
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	for (std::size_t slotIndex = 0; slotIndex < channelSlots.size(); slotIndex++)
	{
		auto* channel = channelSlots[slotIndex].channel.get();
		if (channel == nullptr)
		{
			continue;
		}
		if (channel->track == nullptr || !channel->playing || channel->stopped)
		{
			releaseAudioChannelSlot(slotIndex);
			continue;
		}
		if (MIX_TrackPaused(channel->track))
		{
			channel->paused = true;
			continue;
		}
		channel->paused = false;
		if (!MIX_TrackPlaying(channel->track))
		{
			releaseAudioChannelSlot(slotIndex);
		}
	}
}
#endif

_music EngineBase::createMusic(const std::unique_ptr<char[]>& data, int size, bool loop, bool music3d, unsigned char priority)
{
#ifdef SHF_USE_AUDIO
	(void)priority;
	if (data == nullptr || size <= 0 ||
		static_cast<std::size_t>(size) > AudioDecodeSafety::MaxEncodedAudioBytes)
	{
		return nullptr;
	}
	auto* audio = new (std::nothrow) AudioBuffer;
	if (audio == nullptr)
	{
		return nullptr;
	}
	audio->loop = loop;
	audio->positional = music3d;
#ifdef SHF_USE_VIDEO
	if (!AudioDecodeSafety::decodeFromMemory(reinterpret_cast<const uint8_t*>(data.get()), size,
		loop, music3d, *audio))
	{
		delete audio;
		return nullptr;
	}
	{
		std::lock_guard<std::recursive_mutex> locker(soundMutex);
		if (!loadRawMixerAudio(*audio))
		{
			delete audio;
			return nullptr;
		}
	}
	return audio;
#else
	delete audio;
	return nullptr;
#endif
#else
    return nullptr;
#endif

}

void EngineBase::freeMusic(_music music)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	if (music == nullptr)
	{
		GameLog::write("music to release is nullptr \n");
		return;
	}
	for (std::size_t slotIndex = 0; slotIndex < channelSlots.size(); slotIndex++)
	{
		auto* channel = channelSlots[slotIndex].channel.get();
		if (channel != nullptr && channel->music == music)
		{
			releaseAudioChannelSlot(slotIndex);
		}
	}
	if (music->audio != nullptr)
	{
		MIX_DestroyAudio(music->audio);
		music->audio = nullptr;
	}
	delete music;
#endif
}

_channel EngineBase::playMusic(_music music, float volume)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	if (music == nullptr)
	{
		return nullptr;
	}
	auto channel = std::make_unique<Channel_t>();
	channel->music = music;
	channel->loop = music->loop;
	channel->volume = volume;
	if (!startChannel(channel.get()))
	{
		GameLog::write("Play Sound Error!\n");
		return nullptr;
	}
	return registerAudioChannel(std::move(channel));
#else
	return nullptr;
#endif

}
_channel EngineBase::playMusic(_music music, float x, float y, float volume)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	if (music == nullptr)
	{
		return nullptr;
	}
	_channel channel = playMusic(music, volume);
	setMusicPosition(channel, x, y);
	return channel;
#else
	return nullptr;
#endif
}

void EngineBase::stopMusic(_channel channel)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	releaseAudioChannel(channel);
#endif
}

void EngineBase::stopSoundsExcept(_channel keepChannelA, _channel keepChannelB)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	Channel_t* keepChannelPointerA = resolveAudioChannel(keepChannelA);
	Channel_t* keepChannelPointerB = resolveAudioChannel(keepChannelB);
	for (std::size_t slotIndex = 0; slotIndex < channelSlots.size(); slotIndex++)
	{
		auto* channel = channelSlots[slotIndex].channel.get();
		if (channel == keepChannelPointerA || channel == keepChannelPointerB)
		{
			continue;
		}
		releaseAudioChannelSlot(slotIndex);
	}
#endif
}

void EngineBase::pauseMusic(_channel channel)
{
#ifdef SHF_USE_AUDIO
	pauseMusicIfPlaying(channel);
#endif
}

bool EngineBase::pauseMusicIfPlaying(_channel handle)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	auto* channel = resolveAudioChannel(handle);
	if (channel == nullptr || channel->track == nullptr || channel->paused || !channel->playing)
	{
		return false;
	}
	if (!MIX_PauseTrack(channel->track))
	{
		return false;
	}
	channel->paused = true;
	return true;
#else
	return false;
#endif
}

void EngineBase::resumeMusic(_channel handle)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	auto* channel = resolveAudioChannel(handle);
	if (channel == nullptr || channel->track == nullptr || !channel->paused || !channel->playing)
	{
		return;
	}
	if (MIX_ResumeTrack(channel->track))
	{
		channel->paused = false;
	}
#endif
}

void EngineBase::setMusicPosition(_channel handle, float x, float y)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	auto* channel = resolveAudioChannel(handle);
	if (channel == nullptr)
	{
		return;
	}
	channel->positionX = x;
	channel->positionY = y;
	updateChannelTrackGain(channel);
#endif
}

void EngineBase::setMusicVolume(_channel handle, float volume)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	auto* channel = resolveAudioChannel(handle);
	if (channel == nullptr)
	{
		return;
	}
	channel->volume = volume;
	updateChannelTrackGain(channel);
#endif
}

bool EngineBase::getMusicPlaying(_channel handle)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	auto* channel = resolveAudioChannel(handle);
	if (channel == nullptr || channel->stopped || !channel->playing)
	{
		releaseAudioChannel(handle);
		return false;
	}
	if (channel->track == nullptr)
	{
		releaseAudioChannel(handle);
		return false;
	}
	if (MIX_TrackPaused(channel->track))
	{
		channel->paused = true;
		return true;
	}
	channel->paused = false;
	if (MIX_TrackPlaying(channel->track))
	{
		return true;
	}
	releaseAudioChannel(handle);
	return false;
#else
	return false;
#endif
}

bool EngineBase::soundAutoRelease(_music music, _channel channel)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	if (music == nullptr || resolveAudioChannel(channel) == nullptr)
	{
		return false;
	}
	for (auto iter = soundList.begin(); iter != soundList.end(); iter++)
	{
		if (iter->c == channel || iter->m == music)
		{
			return false;
		}
	}
	soundList.push_back({ channel, music, false });
#endif
	return true;
}

void EngineBase::checkSoundRelease()
{
#ifdef SHF_USE_AUDIO

	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	auto iter = soundList.begin();
	//for (size_t i = soundList.size(); i > 0; --i)
	while (iter != soundList.end())
	{
		bool playing = getMusicPlaying(iter->c);
		if (!playing || iter->stopped)
		{
			auto m = iter->m;
			freeMusic(m);
			iter = soundList.erase(iter);
		}
		else
		{
			iter++;
		}
	}

#endif
}

#ifdef SHF_USE_VIDEO
int EngineBase::initVideo()
{
    avformat_network_init();
	clearVideoList();
	return 0;
}

void EngineBase::destroyVideo()
{
	GameLog::write("Begin to destroy video\n");
	clearVideoList();
	GameLog::write("Destroy video done!\n");
}
#endif

#ifdef SHF_USE_VIDEO
void EngineBase::freeMediaStream(MediaStream * mediaStream)
{
	if (mediaStream == nullptr)
	{
		return;
	}
	if (mediaStream->frame != nullptr)
	{
		av_frame_free(&mediaStream->frame);
	}
	if (mediaStream->packet != nullptr)
	{
		av_packet_free(&mediaStream->packet);
	}

	if (mediaStream->codecCtx != nullptr)
	{
        avcodec_close(mediaStream->codecCtx);
		avcodec_free_context(&mediaStream->codecCtx);
	}

	if (mediaStream->formatCtx != nullptr)
	{
		if (mediaStream->inputOpened)
		{
			avformat_close_input(&mediaStream->formatCtx);
		}
		else
		{
			avformat_free_context(mediaStream->formatCtx);
			mediaStream->formatCtx = nullptr;
		}
	}
	mediaStream->inputOpened = false;

	if (mediaStream->customIoContext != nullptr)
	{
		av_freep(&mediaStream->customIoContext->buffer);
		avio_context_free(&mediaStream->customIoContext);
	}

    if (mediaStream->rWops != nullptr)
    {
        SDL_CloseIO(mediaStream->rWops);
        mediaStream->rWops = nullptr;
    }
}
#endif

#ifdef SHF_USE_VIDEO
int EngineBase::openVideoFile(_video video)
{
	int result = -1;
	if (video == nullptr)
	{
		return result;
	}
	if (!File::fileExist(video->fileName))
	{
		return result;
	}

	setMediaStream(&video->videoStream, video->fileName, AVMEDIA_TYPE_VIDEO);
	setMediaStream(&video->audioStream, video->fileName, AVMEDIA_TYPE_AUDIO);
	if (video->videoStream.exists)
	{
		video->pixelFormat = video->videoStream.codecCtx->pix_fmt;
	}
	if (video->audioStream.exists)
	{
		video->totalTime = video->audioStream.totalTime;
		result = 1;
	}
	else if (video->videoStream.exists)
	{
		video->totalTime = video->videoStream.totalTime;
		result = 1;
	}
	else
	{
		video->totalTime = 0;
	}
	return result;

}

int EngineBase::read_packet(void *opaque, uint8_t *buf, int buf_size)
{
	auto mediaStream = static_cast<MediaStream*>(opaque);
	if (mediaStream == nullptr || mediaStream->rWops == nullptr ||
		buf == nullptr || buf_size <= 0)
	{
		return AVERROR(EIO);
	}
	int64_t nowPosition = SDL_TellIO(mediaStream->rWops);
	if (nowPosition < 0 || mediaStream->rWops_length < nowPosition)
	{
		return AVERROR(EIO);
	}
	int64_t remaining = mediaStream->rWops_length - nowPosition;
	if (remaining <= 0)
	{
		if (mediaStream->customIoContext != nullptr)
		{
			mediaStream->customIoContext->eof_reached = 1;
		}
		return AVERROR_EOF;
	}
	int readSize = static_cast<int>(std::min<int64_t>(remaining, buf_size));
	size_t bytesRead = SDL_ReadIO(mediaStream->rWops, buf, static_cast<size_t>(readSize));
	if (bytesRead == 0)
	{
		return AVERROR_EOF;
	}
	return static_cast<int>(bytesRead);
}

int64_t EngineBase::seek_packet(void *opaque, int64_t offset, int whence)
{
	auto mediaStream = static_cast<MediaStream*>(opaque);
	if (mediaStream == nullptr || mediaStream->rWops == nullptr)
	{
		return -1;
	}
	if (whence == AVSEEK_SIZE)
	{
		return mediaStream->rWops_length;
	}

#ifdef AVSEEK_FORCE
	whence &= ~AVSEEK_FORCE;
#endif

	SDL_IOWhence sdlWhence = SDL_IO_SEEK_SET;
	switch (whence)
	{
	case SEEK_SET:
		sdlWhence = SDL_IO_SEEK_SET;
		break;
	case SEEK_CUR:
		sdlWhence = SDL_IO_SEEK_CUR;
		break;
	case SEEK_END:
		sdlWhence = SDL_IO_SEEK_END;
		break;
	default:
		return -1;
	}
	return SDL_SeekIO(mediaStream->rWops, offset, sdlWhence);
}

void EngineBase::setMediaStream(MediaStream * mediaStream, std::string& fileName, AVMediaType mediaType)
{
	if (mediaStream == nullptr)
	{
		return;
	}
	auto newFileName = fileName;

	if (!File::fileExist(newFileName))
	{
		return;
	}
    int ret = 0;

#if defined(__ANDROID__) || defined(__APPLE__)
	convert::replaceAllString(newFileName, "\\", "/");
	std::string resolvedFileName = File::getAssetsName(newFileName);
	auto *pFile = SDL_IOFromFile(resolvedFileName.c_str(), "rb");
	if (!pFile)
	{
		GameLog::write("video %s open error: Cannot open input file\n", resolvedFileName.c_str());
		return;
	}
	mediaStream->rWops = pFile;
	SDL_SeekIO(pFile, 0, SDL_IO_SEEK_END);
	mediaStream->rWops_length = SDL_TellIO(pFile);
	if (mediaStream->rWops_length < 0)
	{
		GameLog::write("video %s open error: Cannot determine input size\n", resolvedFileName.c_str());
		SDL_CloseIO(mediaStream->rWops);
		mediaStream->rWops = nullptr;
		return;
	}
	SDL_SeekIO(pFile, 0, SDL_IO_SEEK_SET);

	size_t buff_size = 10 * 1024;
	auto buff = av_malloc(buff_size);
	if (buff == nullptr)
	{
		SDL_CloseIO(mediaStream->rWops);
		mediaStream->rWops = nullptr;
		return;
	}
	AVIOContext *avio_ctx = avio_alloc_context(static_cast<unsigned char*>(buff),
		static_cast<int>(buff_size), 0, mediaStream, read_packet, nullptr, seek_packet);
	if (!avio_ctx)
	{
		av_free(buff);
		SDL_CloseIO(mediaStream->rWops);
		mediaStream->rWops = nullptr;
		return;
	}

	mediaStream->customIoContext = avio_ctx;
	mediaStream->formatCtx->pb = mediaStream->customIoContext;
	mediaStream->formatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
	ret = avformat_open_input(&mediaStream->formatCtx, nullptr, nullptr, nullptr);
#else
    ret = avformat_open_input(&mediaStream->formatCtx, File::getAssetsName(newFileName).c_str(), nullptr, nullptr);
#endif
	mediaStream->inputOpened = ret == 0;
	if (ret != 0)
	{
		char buf[1024];
		av_strerror(ret, buf, 1024);
		GameLog::write("video %s open error: %s\n", File::getAssetsName(newFileName).c_str(), buf);
	}
	if (ret == 0)
	{
		ret = findStreamInfoWithinDecodeBudgets(mediaStream->formatCtx);
		if (ret >= 0)
		{
			for (size_t i = 0; i < mediaStream->formatCtx->nb_streams; i++)
			{
				auto stream = mediaStream->formatCtx->streams[i];
				if (mediaStream->formatCtx->streams[i]->codecpar != nullptr && mediaStream->formatCtx->streams[i]->codecpar->codec_type == mediaType)
				{
					auto codec = avcodec_find_decoder(stream->codecpar->codec_id);
					if (codec == nullptr)
					{
						GameLog::write("video %s decoder not found: %s\n",
							File::getAssetsName(newFileName).c_str(),
							avcodec_get_name(stream->codecpar->codec_id));
						break;
					}

					AVCodecContext* codecContext = avcodec_alloc_context3(codec);
					if (codecContext == nullptr)
					{
						GameLog::write("video %s decoder context alloc failed: %s\n",
							File::getAssetsName(newFileName).c_str(),
							avcodec_get_name(stream->codecpar->codec_id));
						break;
					}

					ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
					if (ret < 0)
					{
						char buf[1024];
						av_strerror(ret, buf, 1024);
						GameLog::write("video %s decoder parameter error: %s\n",
							File::getAssetsName(newFileName).c_str(), buf);
						avcodec_free_context(&codecContext);
						break;
					}
					if (mediaType == AVMEDIA_TYPE_VIDEO)
					{
						codecContext->max_pixels = static_cast<int64_t>(
							VideoStruct::MaxDecodedVideoPixels);
					}
					else if (mediaType == AVMEDIA_TYPE_AUDIO)
					{
#if (!defined USE_FFMPEG4)
						codecContext->max_samples = VideoStruct::MaxAudioFrameSamples;
#endif
					}

					ret = avcodec_open2(codecContext, codec, nullptr);
					if (ret < 0)
					{
						char buf[1024];
						av_strerror(ret, buf, 1024);
						GameLog::write("video %s decoder open error for %s: %s\n",
							File::getAssetsName(newFileName).c_str(),
							avcodec_get_name(stream->codecpar->codec_id),
							buf);
						avcodec_free_context(&codecContext);
						break;
					}

					mediaStream->exists = true;
					mediaStream->stream = stream;
					double frameRate = av_q2d(mediaStream->stream->r_frame_rate);
					if (mediaStream->stream->r_frame_rate.den > 0 &&
						mediaStream->stream->r_frame_rate.num > 0 &&
						std::isfinite(frameRate) && frameRate > 0.0)
					{
						mediaStream->timePerFrame = static_cast<float>(1e3 / frameRate);
					}
					double packetTimeBase = av_q2d(mediaStream->stream->time_base);
					mediaStream->timeBasePacket = std::isfinite(packetTimeBase) && packetTimeBase > 0.0
						? static_cast<float>(1e3 * packetTimeBase)
						: 0.0f;
					if (mediaStream->formatCtx->duration != AV_NOPTS_VALUE &&
						mediaStream->formatCtx->duration > 0)
					{
						mediaStream->totalTime = static_cast<float>(mediaStream->formatCtx->duration) *
							1e3f / AV_TIME_BASE;
					}
					mediaStream->timelineStartTimestamp = mediaStream->stream->start_time;
					mediaStream->index = i;
					mediaStream->codecCtx = codecContext;
					break;
				}
			}			
		}
		else
		{
			char buf[1024];
			av_strerror(ret, buf, 1024);
			GameLog::write("video %s stream info error: %s\n", File::getAssetsName(newFileName).c_str(), buf);
		}
	}	
}
#endif

float EngineBase::getVideoTime(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return 0.0;
	}
	if (video->time.paused)
	{
		return video->time.pauseBeginTime - video->time.beginTime;
	}
	return static_cast<float>(SDL_GetTicks()) - video->time.beginTime;
#endif
	return 0.0;
}

#ifdef SHF_USE_VIDEO
float EngineBase::initVideoTime(_video video)
{
	if (video == nullptr)
	{
		return 0.0;
	}
	video->time.beginTime = static_cast<float>(SDL_GetTicks());
	video->time.pauseBeginTime = 0.0;
	video->time.paused = false;
	return video->time.beginTime;
}

void EngineBase::setVideoTimePaused(_video video, bool paused)
{
	if (video == nullptr)
	{
		return;
	}
	if (video->time.paused == paused)
	{
		return;
	}
	float now = static_cast<float>(SDL_GetTicks());
	if (paused)
	{
		video->time.pauseBeginTime = now;
		video->time.paused = true;
	}
	else
	{
		video->time.beginTime += now - video->time.pauseBeginTime;
		video->time.paused = false;
	}
}

float EngineBase::setVideoTime(_video video, float timer)
{
	if (video == nullptr)
	{
		return 0.0;
	}
	video->time.beginTime += getVideoTime(video) - timer;
	return getVideoTime(video);
}

float EngineBase::getVideoSoundRate(_video video)
{
	if (video != nullptr && video->audioOutputSpec.freq > 0)
	{
		return static_cast<float>(video->audioOutputSpec.freq) / 1000.0f;
	}
	return 48.0f;
}

void EngineBase::enqueueDecodedAudioFrame(_video video)
{
#ifdef SHF_USE_AUDIO
	if (video == nullptr || video->audioStream.frame == nullptr)
	{
		return;
	}
	int sampleRate = video->audioStream.codecCtx->sample_rate;
	if (sampleRate <= 0 || sampleRate > VideoStruct::MaxAudioSampleRate)
	{
		sampleRate = 44100;
	}
	constexpr int OutputChannelCount = 2;
	int dataLength = convert(video->audioStream.codecCtx, video->audioStream.frame,
		AV_SAMPLE_FMT_S16, sampleRate, OutputChannelCount, video->audioBuffer);
	if (dataLength <= 0)
	{
		return;
	}
	if (video->audioOutputStream == nullptr)
	{
		video->audioOutputSpec.format = SDL_AUDIO_S16;
		video->audioOutputSpec.freq = sampleRate;
		video->audioOutputSpec.channels = OutputChannelCount;
		video->audioOutputStream = SDL_OpenAudioDeviceStream(
			SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &video->audioOutputSpec, nullptr, nullptr);
		if (video->audioOutputStream != nullptr)
		{
			SDL_SetAudioStreamGain(video->audioOutputStream, video->videoVolume);
			SDL_PauseAudioStreamDevice(video->audioOutputStream);
		}
	}
	if (video->audioOutputStream != nullptr)
	{
		if (SDL_PutAudioStreamData(video->audioOutputStream,
			video->audioBuffer.data(), dataLength))
		{
			if (video->running && !video->time.paused)
			{
				SDL_ResumeAudioStreamDevice(video->audioOutputStream);
			}
		}
		else
		{
			GameLog::write("SDL_PutAudioStreamData Error : %s", SDL_GetError());
		}
	}
	float bytesPerMillisecond = (static_cast<float>(sampleRate) / 1000.0f) *
		static_cast<float>(OutputChannelCount) * SDL_AUDIO_BYTESIZE(SDL_AUDIO_S16);
	if (bytesPerMillisecond > 0.0f)
	{
		video->soundDelay += static_cast<float>(dataLength) / bytesPerMillisecond;
		video->totalTime = (std::max)(video->totalTime, video->soundDelay);
	}
#endif
}

void EngineBase::decodeNextAudio(_video video)
{
#ifdef SHF_USE_AUDIO
	if (video == nullptr || !video->audioStream.exists)
	{
		return;
	}
	bool decodedFrame = false;
	while (!decodedFrame)
	{
		video->audioStream.packet = av_packet_alloc();
		if (video->audioStream.packet == nullptr)
		{
			video->audioStream.decodeEnd = true;
			break;
		}
		int readResult = av_read_frame(video->audioStream.formatCtx, video->audioStream.packet);
		if (readResult < 0)
		{
			av_packet_free(&video->audioStream.packet);
			avcodec_send_packet(video->audioStream.codecCtx, nullptr);
			while (avcodec_receive_frame(video->audioStream.codecCtx,
				video->audioStream.frame) == 0)
			{
				decodedFrame = true;
				enqueueDecodedAudioFrame(video);
			}
			video->audioStream.decodeEnd = true;
			if (video->audioOutputStream != nullptr && !video->audioOutputFlushed)
			{
				SDL_FlushAudioStream(video->audioOutputStream);
				video->audioOutputFlushed = true;
			}
			break;
		}

		if (video->audioStream.packet->stream_index == video->audioStream.index &&
			avcodec_send_packet(video->audioStream.codecCtx, video->audioStream.packet) == 0)
		{
			while (avcodec_receive_frame(video->audioStream.codecCtx,
				video->audioStream.frame) == 0)
			{
				decodedFrame = true;
				enqueueDecodedAudioFrame(video);
			}
		}
		av_packet_free(&video->audioStream.packet);
	}
#endif
}

void EngineBase::enqueueDecodedVideoFrame(_video video)
{
	if (video == nullptr || video->videoStream.frame == nullptr)
	{
		return;
	}
	AVFrame* frame = video->videoStream.frame;
	if (frame->width <= 0 || frame->height <= 0 ||
		static_cast<std::uint64_t>(frame->width) >
			VideoStruct::MaxDecodedVideoPixels /
			static_cast<std::uint64_t>(frame->height))
	{
		GameLog::write("video frame dimensions exceed the decoded pixel budget\n");
		video->videoStream.decodeEnd = true;
		return;
	}
	float frameDuration = video->videoStream.timePerFrame;
	if (frame->pkt_duration > 0 && video->videoStream.timeBasePacket > 0.0f)
	{
		frameDuration = static_cast<float>(frame->pkt_duration) *
			video->videoStream.timeBasePacket;
	}
	if (!std::isfinite(frameDuration) || frameDuration <= 0.0f)
	{
		frameDuration = 20.0f;
	}

	int64_t timestamp = frame->best_effort_timestamp;
	if (timestamp == AV_NOPTS_VALUE)
	{
		timestamp = frame->pts;
	}
	float frameTime = video->lastDecodedVideoTime >= 0.0f
		? video->lastDecodedVideoTime + frameDuration
		: 0.0f;
	if (timestamp != AV_NOPTS_VALUE)
	{
		if (video->videoStream.timelineStartTimestamp == AV_NOPTS_VALUE)
		{
			video->videoStream.timelineStartTimestamp = timestamp;
		}
		double relativeTimestamp = static_cast<double>(timestamp) -
			static_cast<double>(video->videoStream.timelineStartTimestamp);
		double candidateTime = relativeTimestamp *
			static_cast<double>(video->videoStream.timeBasePacket);
		if (std::isfinite(candidateTime) && candidateTime >= 0.0 &&
			candidateTime <= static_cast<double>((std::numeric_limits<float>::max)()))
		{
			frameTime = static_cast<float>(candidateTime);
			if (video->lastDecodedVideoTime >= 0.0f &&
				frameTime < video->lastDecodedVideoTime)
			{
				frameTime = video->lastDecodedVideoTime;
			}
		}
	}
	video->decodedVideoFrameCount++;
	if (video->firstDecodedVideoTime < 0.0f)
	{
		video->firstDecodedVideoTime = frameTime;
	}
	video->lastDecodedVideoTime = frameTime;
	float frameEndTime = frameTime + frameDuration;
	video->totalTime = (std::max)(video->totalTime, frameEndTime);
	SDL_Renderer* currentRenderer = renderer.load();
	if (currentRenderer == nullptr)
	{
		return;
	}
	SDL_PixelFormat sdlPixelFormat = getVideoPixelFormat(frame->format);
	SDL_PixelFormat texturePixelFormat = sdlPixelFormat == SDL_PIXELFORMAT_UNKNOWN
		? SDL_PIXELFORMAT_BGRA8888
		: sdlPixelFormat;
	auto texture = make_shared_image(SDL_CreateTexture(currentRenderer, texturePixelFormat,
		SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height));
	if (texture == nullptr)
	{
		GameLog::write("SDL_CreateTexture Error(video): %s", SDL_GetError());
		return;
	}

	switch (sdlPixelFormat)
	{
	case SDL_PIXELFORMAT_UNKNOWN:
		video->swsContext = sws_getCachedContext(video->swsContext,
			frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
			frame->width, frame->height, AV_PIX_FMT_BGRA, SWS_BICUBIC,
			nullptr, nullptr, nullptr);
		if (video->swsContext != nullptr)
		{
			uint8_t* pixels[4] = {};
			int pitches[4] = {};
			if (SDL_LockTexture(texture.get(), nullptr,
				reinterpret_cast<void**>(pixels), pitches))
			{
				sws_scale(video->swsContext,
					const_cast<const uint8_t* const*>(frame->data), frame->linesize,
					0, frame->height, pixels, pitches);
				SDL_UnlockTexture(texture.get());
			}
		}
		break;
	case SDL_PIXELFORMAT_IYUV:
		if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0)
		{
#ifdef __ANDROID__
			SDL_UpdateYUVTexture(texture.get(), nullptr,
				frame->data[0], frame->linesize[0],
				frame->data[2], frame->linesize[2],
				frame->data[1], frame->linesize[1]);
#else
			if (!SDL_UpdateYUVTexture(texture.get(), nullptr,
				frame->data[0], frame->linesize[0],
				frame->data[1], frame->linesize[1],
				frame->data[2], frame->linesize[2]))
			{
				GameLog::write("SDL_UpdateYUVTexture Error(1): %s", SDL_GetError());
			}
#endif
		}
		else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0)
		{
			if (!SDL_UpdateYUVTexture(texture.get(), nullptr,
				frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0],
				frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
				frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]))
			{
				GameLog::write("SDL_UpdateYUVTexture Error(2): %s", SDL_GetError());
			}
		}
		else
		{
			GameLog::write("Mixed negative and positive line sizes are not supported.");
		}
		break;
	default:
		if (frame->linesize[0] < 0)
		{
			SDL_UpdateTexture(texture.get(), nullptr,
				frame->data[0] + frame->linesize[0] * (frame->height - 1),
				-frame->linesize[0]);
		}
		else
		{
			SDL_UpdateTexture(texture.get(), nullptr, frame->data[0], frame->linesize[0]);
		}
		break;
	}

	VideoImage videoImage;
	videoImage.t = frameTime;
	videoImage.image = texture;
	video->videoImage.push_back(videoImage);
}

void EngineBase::decodeNextVideo(_video video)
{
	if (video == nullptr || !video->videoStream.exists)
	{
		return;
	}
	bool decodedFrame = false;
	while (!decodedFrame)
	{
		video->videoStream.packet = av_packet_alloc();
		if (video->videoStream.packet == nullptr)
		{
			video->videoStream.decodeEnd = true;
			break;
		}
		int readResult = av_read_frame(video->videoStream.formatCtx, video->videoStream.packet);
		if (readResult < 0)
		{
			av_packet_free(&video->videoStream.packet);
			avcodec_send_packet(video->videoStream.codecCtx, nullptr);
			while (avcodec_receive_frame(video->videoStream.codecCtx,
				video->videoStream.frame) == 0)
			{
				decodedFrame = true;
				video->drainedVideoFrameCount++;
				enqueueDecodedVideoFrame(video);
			}
			video->videoStream.decodeEnd = true;
			break;
		}

		if (video->videoStream.packet->stream_index == video->videoStream.index &&
			avcodec_send_packet(video->videoStream.codecCtx, video->videoStream.packet) == 0)
		{
			while (avcodec_receive_frame(video->videoStream.codecCtx,
				video->videoStream.frame) == 0)
			{
				decodedFrame = true;
				enqueueDecodedVideoFrame(video);
			}
		}
		av_packet_free(&video->videoStream.packet);
	}
}

void EngineBase::checkVideoDecodeEnd(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	if ((video->audioStream.exists && !video->audioStream.decodeEnd) || (video->videoStream.exists && !video->videoStream.decodeEnd))
	{
		video->decodeEnd = false;
	}
	else
	{
		video->decodeEnd = true;
	}
}
#endif

bool EngineBase::getVideoStopped(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return true;
	}
	return video->stopped;
#else
    return true;
#endif
}

#ifdef SHF_USE_VIDEO
void EngineBase::pauseAllVideo()
{
	for (size_t i = 0; i < videoList.size(); i++)
	{
		if (videoList[i] == nullptr)
		{
			continue;
		}
		if (videoList[i]->time.paused)
		{
			videoList[i]->pausedBeforePause = true;
		}
		else
		{
			videoList[i]->pausedBeforePause = false;
			pauseVideo(videoList[i]);
		}
	}
}

void EngineBase::resumeAllVideo()
{
	for (size_t i = 0; i < videoList.size(); i++)
	{
		if (videoList[i] == nullptr)
		{
			continue;
		}
		if (!videoList[i]->pausedBeforePause)
		{
			resumeVideo(videoList[i]);
		}
	}
}

void EngineBase::clearVideo(_video video)
{
	if (video == nullptr)
	{
		return;
	}

	if (video->audioOutputStream != nullptr)
	{
		SDL_PauseAudioStreamDevice(video->audioOutputStream);
		SDL_ClearAudioStream(video->audioOutputStream);
		video->audioOutputFlushed = false;
	}
	//for (size_t i = 0; i < video->videoImage.size(); i++)
	//{
	//	freeImage(video->videoImage[i]);
	//}
	video->videoImage.clear();
}

void EngineBase::rearrangeVideoFrame(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	std::stable_sort(video->videoImage.begin(), video->videoImage.end(),
		[](const VideoImage& left, const VideoImage& right)
		{
			return left.t < right.t;
		});
}

SDL_PixelFormat EngineBase::getVideoPixelFormat(int originalFormat)
{
	std::map<int, SDL_PixelFormat> pix_ffmpeg_sdl =
	{
		{ AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
		{ AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_XRGB4444 },
		{ AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_XRGB1555 },
		{ AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_XBGR1555 },
		{ AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
		{ AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
		{ AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
		{ AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
		{ AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_XRGB8888 },
		{ AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_XBGR8888 },
		{ AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
		{ AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
		{ AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
		{ AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
		{ AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
		{ AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
		{ AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
		{ AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
		{ AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
		{ AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
	};
	if (pix_ffmpeg_sdl.count(originalFormat) > 0)
	{
		return pix_ffmpeg_sdl[originalFormat];
	}
	else
	{
		return SDL_PIXELFORMAT_UNKNOWN;
	}	
}
#endif

void EngineBase::resetVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	initVideoTime(video);
	setVideoTimePaused(video, true);
	clearVideo(video);

	if (video->videoStream.exists)
	{
		avcodec_flush_buffers(video->videoStream.codecCtx);
		av_seek_frame(video->videoStream.formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
		video->videoStream.decodeEnd = false;
		video->videoStream.setTS = false;
		video->videoStream.timelineStartTimestamp = video->videoStream.stream != nullptr
			? video->videoStream.stream->start_time
			: AV_NOPTS_VALUE;
	}
	if (video->audioStream.exists)
	{
		avcodec_flush_buffers(video->audioStream.codecCtx);
		av_seek_frame(video->audioStream.formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
		video->audioStream.decodeEnd = false;
		video->audioStream.setTS = false;
		video->audioStream.timelineStartTimestamp = video->audioStream.stream != nullptr
			? video->audioStream.stream->start_time
			: AV_NOPTS_VALUE;
	}

	video->soundDelay = 0.0;
	video->soundRate = getVideoSoundRate(video);
	video->lastTime = 0.0f;
	video->decodedVideoFrameCount = 0;
	video->drainedVideoFrameCount = 0;
	video->firstDecodedVideoTime = -1.0f;
	video->lastDecodedVideoTime = -1.0f;
	video->audioOutputFlushed = false;
	video->pausedBeforePause = false;
	video->decodeEnd = false;
	video->stopped = false;
	if (video->running)
	{
		video->running = false;
		runVideo(video);
	}
#endif
}

void EngineBase::setVideoLoop(_video video, int loop)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	video->loop = loop;
#endif
}

#ifdef SHF_USE_VIDEO
int EngineBase::convert(AVCodecContext * codecCtx, AVFrame * frame, int out_sample_format,
	int out_sample_rate, int out_channels, std::vector<uint8_t>& outBuffer)
{
	outBuffer.clear();
	if (codecCtx == nullptr || frame == nullptr || out_channels <= 0 ||
		out_sample_rate <= 0 || out_sample_rate > VideoStruct::MaxAudioSampleRate)
	{
		return -1;
	}
	SwrContext* swr_ctx = nullptr;
	int ret = 0;
	int result = -1;
#if (defined USE_FFMPEG4)
	int64_t src_ch_layout = AV_CH_LAYOUT_STEREO;
	int64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;
#else
	AVChannelLayout src_ch_layout = {};
	AVChannelLayout dst_ch_layout = {};
	bool channelLayoutsInitialized = false;
#endif
	int dst_nb_channels = 0;
	int dst_linesize = 0;
	int src_nb_samples = 0;
	int dst_nb_samples = 0;
	int max_dst_nb_samples = 0;
	uint8_t** dst_data = nullptr;
	int resampled_data_size = 0;
	int inputSampleRate = 0;
	int maximumBufferSize = 0;

	swr_ctx = swr_alloc();
	if (!swr_ctx)
	{
		GameLog::write("swr_alloc error \n");
		goto cleanup;
	}
#if (defined USE_FFMPEG4)
	src_ch_layout = getDefaultAudioChannelLayout(codecCtx->channels > 0 ? codecCtx->channels : 2);
	dst_ch_layout = getDefaultAudioChannelLayout(out_channels);
#else
	setDefaultAudioChannelLayout(src_ch_layout, getCodecAudioChannelCount(codecCtx));
	setDefaultAudioChannelLayout(dst_ch_layout, out_channels);
	channelLayoutsInitialized = true;
#endif
	src_nb_samples = frame->nb_samples;
	if (src_nb_samples <= 0 || src_nb_samples > VideoStruct::MaxAudioFrameSamples)
	{
		GameLog::write("src_nb_samples error \n");
		goto cleanup;
	}
	inputSampleRate = codecCtx->sample_rate;
	if (inputSampleRate <= 0 || inputSampleRate > VideoStruct::MaxAudioSampleRate)
	{
		inputSampleRate = out_sample_rate;
	}
#if (defined USE_FFMPEG4)
	av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
#else
	av_opt_set_chlayout(swr_ctx, "in_channel_layout", &src_ch_layout, 0);
#endif
	av_opt_set_int(swr_ctx, "in_sample_rate", inputSampleRate, 0);
	av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codecCtx->sample_fmt, 0);
#if (defined USE_FFMPEG4)
	av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
#else
	av_opt_set_chlayout(swr_ctx, "out_channel_layout", &dst_ch_layout, 0);
#endif
	av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", (AVSampleFormat)out_sample_format, 0);

	if ((ret = swr_init(swr_ctx)) < 0)
	{
		GameLog::write("Failed to initialize the resampling context\n");
		goto cleanup;
	}

	max_dst_nb_samples = dst_nb_samples = (int)av_rescale_rnd(src_nb_samples, out_sample_rate, inputSampleRate, AV_ROUND_UP);
	if (max_dst_nb_samples <= 0)
	{
		GameLog::write("av_rescale_rnd error \n");
		goto cleanup;
	}
#if (defined USE_FFMPEG4)
	dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
#else
	dst_nb_channels = dst_ch_layout.nb_channels;
#endif
	maximumBufferSize = av_samples_get_buffer_size(nullptr, dst_nb_channels,
		dst_nb_samples, static_cast<AVSampleFormat>(out_sample_format), 1);
	if (maximumBufferSize <= 0 || maximumBufferSize > VideoStruct::MaxAudioBufferBytes)
	{
		GameLog::write("video audio conversion buffer too large\n");
		goto cleanup;
	}
	ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, (AVSampleFormat)out_sample_format, 0);
	if (ret < 0)
	{
		GameLog::write("av_samples_alloc_array_and_samples error \n");
		goto cleanup;
	}

	dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, inputSampleRate) + src_nb_samples, out_sample_rate, inputSampleRate, AV_ROUND_UP);
	if (dst_nb_samples <= 0)
	{
		GameLog::write("av_rescale_rnd error \n");
		goto cleanup;
	}
	maximumBufferSize = av_samples_get_buffer_size(nullptr, dst_nb_channels,
		dst_nb_samples, static_cast<AVSampleFormat>(out_sample_format), 1);
	if (maximumBufferSize <= 0 || maximumBufferSize > VideoStruct::MaxAudioBufferBytes)
	{
		GameLog::write("video audio conversion buffer too large\n");
		goto cleanup;
	}
	if (dst_nb_samples > max_dst_nb_samples)
	{
		av_freep(&dst_data[0]);
		ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, (AVSampleFormat)out_sample_format, 1);
		if (ret < 0)
		{
			GameLog::write("av_samples_alloc error \n");
			goto cleanup;
		}
		max_dst_nb_samples = dst_nb_samples;
	}

	if (swr_ctx)
	{
		ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
		if (ret < 0)
		{
			GameLog::write("swr_convert error \n");
			goto cleanup;
		}

		resampled_data_size = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels, ret, (AVSampleFormat)out_sample_format, 1);
		if (resampled_data_size < 0)
		{
			GameLog::write("av_samples_get_buffer_size error \n");
			goto cleanup;
		}
		if (resampled_data_size > VideoStruct::MaxAudioBufferBytes)
		{
			GameLog::write("video audio conversion output too large\n");
			goto cleanup;
		}
	}
	else
	{
		GameLog::write("swr_ctx null error \n");
		goto cleanup;
	}

	try
	{
		outBuffer.assign(dst_data[0], dst_data[0] + resampled_data_size);
	}
	catch (const std::bad_alloc&)
	{
		outBuffer.clear();
		goto cleanup;
	}
	result = resampled_data_size;

cleanup:
	if (swr_ctx)
	{
		swr_close(swr_ctx);
		swr_free(&swr_ctx);
	}
#if (!defined USE_FFMPEG4)
	if (channelLayoutsInitialized)
	{
		av_channel_layout_uninit(&src_ch_layout);
		av_channel_layout_uninit(&dst_ch_layout);
	}
#endif
	if (dst_data)
	{
		av_freep(&dst_data[0]);
		av_freep(&dst_data);
	}
	return result;
}
#endif

#ifdef SHF_USE_VIDEO
void EngineBase::clearVideoList()
{
	while (!videoList.empty())
	{
		auto video = videoList.back();
		if (video == nullptr)
		{
			videoList.pop_back();
			continue;
		}
		freeVideo(video);
	}
}

void EngineBase::addVideoToList(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	videoList.push_back(video);
}

void EngineBase::deleteVideoFromList(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	int index = -1;
	for (int i = 0; i < (int)videoList.size(); i++)
	{
		if (videoList[i] == video)
		{
			index = i;
			break;
		}
	}
	if (index >= 0)
	{
		deleteVideoFromList(index);
	}
}

void EngineBase::deleteVideoFromList(int index)
{
	if (index < 0 || index >= (int)videoList.size())
	{
		return;
	}
	videoList.erase(videoList.begin() + index);
}

void EngineBase::updateAllVideoVolume(float volume)
{
#ifdef SHF_USE_VIDEO
	for (auto video : videoList)
	{
		if (video != nullptr)
		{
			video->videoVolume = volume;
			if (video->audioOutputStream != nullptr)
			{
				SDL_SetAudioStreamGain(video->audioOutputStream, volume);
			}
		}
	}
#endif
}
#endif

_video EngineBase::loadVideo(const std::string& fileName)
{
#ifdef SHF_USE_VIDEO
	GameLog::write("Open video %s\n", fileName.c_str());
	if (!File::fileExist(fileName))
	{
		GameLog::write("Video:%s not exists\n", fileName.c_str());
		return nullptr;
	}

	auto video = new Video_t;
	video->videoImage.resize(0);
	video->videoStream.formatCtx = avformat_alloc_context();
	video->audioStream.formatCtx = avformat_alloc_context();
	video->videoStream.frame = av_frame_alloc();
	video->audioStream.frame = av_frame_alloc();
	video->sFrame = av_frame_alloc();
	if (video->videoStream.formatCtx == nullptr || video->audioStream.formatCtx == nullptr ||
		video->videoStream.frame == nullptr || video->audioStream.frame == nullptr ||
		video->sFrame == nullptr)
	{
		GameLog::write("Open video:%s allocation error\n", fileName.c_str());
		freeVideo(video);
		return nullptr;
	}
	video->soundDelay = 0;
	video->soundRate = getVideoSoundRate(video);
	initVideoTime(video);
	setVideoTimePaused(video, true);
	video->fileName = fileName;
	setVideoRect((_video)video, nullptr);
	video->decodeEnd = false;
	video->videoVolume = 1;
	if (openVideoFile(video) < 0)
	{
		GameLog::write("Open video:%s error\n", fileName.c_str());
		freeVideo(video);
		return nullptr;
	}

	addVideoToList(video);

	return (_video)video;
#else
	return nullptr;
#endif
}

void EngineBase::setVideoRect(_video video, Rect * rect)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	if (rect == nullptr)
	{
		video->fullScreen = true;
	}
	else
	{
		video->fullScreen = false;
		video->rect.x = rect->x;
		video->rect.y = rect->y;
		video->rect.w = rect->w;
		video->rect.h = rect->h;
	}
#endif
}

void EngineBase::freeVideo(Video_t* video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	deleteVideoFromList(video);
	stopVideo(video);
	std::vector<uint8_t>().swap(video->audioBuffer);
	if (video->sFrame != nullptr)
	{
		av_frame_free(&video->sFrame);
		video->sFrame = nullptr;
	}
	if (video->swsContext != nullptr)
	{
		sws_freeContext(video->swsContext);
		video->swsContext = nullptr;
	}
	if (video->b != nullptr)
	{
		av_free(video->b);
		video->b = nullptr;
	}
    freeMediaStream(&video->videoStream);
	freeMediaStream(&video->audioStream);
	if (video->audioOutputStream != nullptr)
	{
		SDL_DestroyAudioStream(video->audioOutputStream);
		video->audioOutputStream = nullptr;
	}
	delete video;
#endif
}

void EngineBase::runVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr || video->running)
	{
		return;
	}
	video->stopped = false;
	initVideoTime(video);
	setVideoTimePaused(video, true);
	tryDecodeVideo(video);
	video->running = true;

	initVideoTime(video);
	if (video->audioOutputStream != nullptr)
	{
		SDL_ResumeAudioStreamDevice(video->audioOutputStream);
	}
#endif
}

bool EngineBase::updateVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return false;
	}
	tryDecodeVideo(video);
	if (video->stopped)
	{
		if (video->loop > 0)
		{
			video->loop -= 1;
			resetVideo(video);
		}
		else if (video->loop < 0)
		{
			resetVideo(video);
		}
	}
#endif
	return true;
}

#ifdef SHF_USE_VIDEO
void EngineBase::tryDecodeVideo(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	if (video->stopped)
	{
		return;
	}
	if (video->running && video->time.paused)
	{
		return;
	}
	auto video_time = getVideoTime(video);
	if (video_time > video->lastTime + 200)
	{
		setVideoTime(video, video->lastTime + 200);
		video_time = video->lastTime + 200;
	}
	video->lastTime = video_time;
	if (video->audioStream.exists)
	{		
		if (video->videoStream.exists)
		{			
			while ((!video->audioStream.decodeEnd) && (video->soundDelay < video_time + 500))
			{
				decodeNextAudio(video);
				checkVideoDecodeEnd(video);
			}
			while ((!video->videoStream.decodeEnd) && ((video->videoImage.size() < (unsigned int)2) || (video->videoImage.size() > 0 && (video->videoImage[video->videoImage.size() - 1].t < video_time + 100))))
			{
				decodeNextVideo(video);
				checkVideoDecodeEnd(video);
			}
		}
		else
		{
			while ((!video->audioStream.decodeEnd) && (video->soundDelay < video_time + 500))
			{
				decodeNextAudio(video);
				checkVideoDecodeEnd(video);
			}
		}
	}
	else if (video->videoStream.exists)
	{
		while ((!video->videoStream.decodeEnd) && ((video->videoImage.size() < (unsigned int)2) || (video->videoImage.size() > 0 && (video->videoImage[video->videoImage.size() - 1].t < video_time + 100))))
		{
			decodeNextVideo(video);
			checkVideoDecodeEnd(video);
		}
	}
	if (video->decodeEnd &&
		(video->totalTime <= 0.0f || video_time >= video->totalTime))
	{
		video->stopped = true;
		clearVideo(video);
	}
}
#endif

Rect EngineBase::calculateAspectFitVideoRect(int sourceWidth, int sourceHeight,
	int destinationWidth, int destinationHeight)
{
	return AspectFitLayout::calculateFittedRect(
		sourceWidth,
		sourceHeight,
		destinationWidth,
		destinationHeight);
}

EngineBase::FullScreenVideoLayout
EngineBase::calculateFullScreenVideoLayout(
	int sourceWidth,
	int sourceHeight,
	int destinationWidth,
	int destinationHeight)
{
	FullScreenVideoLayout layout;
	if (sourceWidth <= 0 || sourceHeight <= 0 ||
		destinationWidth <= 0 || destinationHeight <= 0)
	{
		return layout;
	}

	layout.source = { 0, 0, sourceWidth, sourceHeight };
	layout.destination = { 0, 0, destinationWidth, destinationHeight };
	constexpr double MaximumCropPerSide = 0.25;
	const double sourceAspect =
		static_cast<double>(sourceWidth) / sourceHeight;
	const double destinationAspect =
		static_cast<double>(destinationWidth) / destinationHeight;
	if (destinationAspect > sourceAspect)
	{
		const double visibleHeight = sourceWidth / destinationAspect;
		const double cropPerSide =
			(sourceHeight - visibleHeight) / (2.0 * sourceHeight);
		if (cropPerSide <= MaximumCropPerSide)
		{
			layout.source.h = std::clamp(
				static_cast<int>(std::lround(visibleHeight)),
				1,
				sourceHeight);
			layout.source.y = (sourceHeight - layout.source.h) / 2;
			return layout;
		}
	}
	else if (destinationAspect < sourceAspect)
	{
		const double visibleWidth = sourceHeight * destinationAspect;
		const double cropPerSide =
			(sourceWidth - visibleWidth) / (2.0 * sourceWidth);
		if (cropPerSide <= MaximumCropPerSide)
		{
			layout.source.w = std::clamp(
				static_cast<int>(std::lround(visibleWidth)),
				1,
				sourceWidth);
			layout.source.x = (sourceWidth - layout.source.w) / 2;
			return layout;
		}
	}
	else
	{
		return layout;
	}

	layout.destination = calculateAspectFitVideoRect(
		sourceWidth,
		sourceHeight,
		destinationWidth,
		destinationHeight);
	layout.needsBlackBackground = true;
	return layout;
}

void EngineBase::drawVideoFrame(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}

	auto drawFrame = [this, video](
		const _shared_image& image)
	{
		if (!video->fullScreen)
		{
			drawImage(image, &video->rect);
			return;
		}

		int sourceWidth = 0;
		int sourceHeight = 0;
		if (!getImageSize(image, sourceWidth, sourceHeight))
		{
			return;
		}
		if (width <= 0 || height <= 0)
		{
			return;
		}

		const FullScreenVideoLayout layout =
			calculateFullScreenVideoLayout(
				sourceWidth,
				sourceHeight,
				width,
				height);
		if (layout.source.w <= 0 || layout.source.h <= 0 ||
			layout.destination.w <= 0 || layout.destination.h <= 0)
		{
			return;
		}
		if (layout.needsBlackBackground)
		{
			fillRect(0, 0, width, height, 0, 0, 0, 255);
		}
		Rect source = layout.source;
		Rect destination = layout.destination;
		drawImage(image, &source, &destination);
	};
	//rearrangeVideoFrame(v);
	float t = getVideoTime(video);
	if (video->videoImage.size() == 0)
	{
		_shared_image image = createMask(0, 0, 0, 255);
		if (video->fullScreen)
		{
			drawImage(image, static_cast<SDL_Rect*>(nullptr));
		}
		else
		{
			drawImage(image, &video->rect);
		}
		//freeImage(image);
	}
	else if (video->videoImage.size() == 1)
	{
		drawFrame(video->videoImage[0].image);
	}
	else if (video->videoImage.size() > 1)
	{
		int index = 0;
		for (size_t i = 0; i < video->videoImage.size(); i++)
		{
			if ((int)t >= video->videoImage[i].t)
			{
				index = i;
			}
		}

		drawFrame(video->videoImage[index].image);

		// TODO: 优化代码
		if (index > 0)
		{
			video->videoImage.erase(video->videoImage.begin(), video->videoImage.begin() + index - 1);
		}	
	}
#endif
}

bool EngineBase::onVideoFrame(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return false;
	}
	if (updateVideo(video))
	{
		drawVideoFrame(video);
		return true;
	}
#endif
	return false;
}

void EngineBase::pauseVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	if (video->audioOutputStream != nullptr)
	{
		SDL_PauseAudioStreamDevice(video->audioOutputStream);
	}
	setVideoTimePaused(video, true);
#endif
}

void EngineBase::resumeVideo(_video video)
{
#ifdef SHF_USE_VIDEO

	if (video == nullptr)
	{
		return;
	}
	setVideoTimePaused(video, false);
	if (video->audioOutputStream != nullptr)
	{		
		SDL_ResumeAudioStreamDevice(video->audioOutputStream);
	}
#endif
}

void EngineBase::stopVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	video->running = false;
	video->stopped = true;
	setVideoTimePaused(video, true);
	clearVideo(video);
#endif
}

bool EngineBase::canPrepareRenderFrame() const
{
	return !isBackGround.load() &&
		!isRenderAdmissionClosed();
}

void EngineBase::frameBegin()
{
	currentFrameReady.store(false);
	handleEvent();
	if (!canPrepareRenderFrame())
	{
		return;
	}
	if (pendingWindowResize && window != nullptr)
	{
		SDL_SetWindowSize(window, width, height);
		pendingWindowResize = false;
		if (!canPrepareRenderFrame())
		{
			return;
		}
	}
	if (pendingLogicalScreenTextureResize)
	{
		pendingLogicalScreenTextureResize = !recreateLogicalScreenTexture();
		if (pendingLogicalScreenTextureResize)
		{
			// The resize generation remains unacknowledged until a ready frame
			// can dispatch it to the complete UI tree.
			return;
		}
	}
	if (!canPrepareRenderFrame())
	{
		return;
	}
	updateState();
	if (!canPrepareRenderFrame())
	{
		return;
	}
	clearScreen();
	currentFrameReady.store(canPrepareRenderFrame());
}

void EngineBase::frameEnd()
{
	if (!currentFrameReady.exchange(false))
	{
		return;
	}
#ifdef SHF_USE_AUDIO
	updateSoundSystem();
	checkSoundRelease();
#endif
	if (!canPrepareRenderFrame())
	{
		return;
	}
	displayScreen();
	countFPS();
}

void EngineBase::clearScreen()
{
	SDL_Renderer* activeRenderer = renderer.load();
	if (activeRenderer == nullptr ||
		SetRenderTarget(
			activeRenderer,
			realScreen.get()) == 0 ||
		!canPrepareRenderFrame())
	{
		return;
	}
	SDL_SetRenderDrawColor(
		activeRenderer,
		0,
		0,
		0,
		0);
	if (!canPrepareRenderFrame())
	{
		return;
	}
	SDL_RenderClear(activeRenderer);
}

void EngineBase::displayScreen()
{	
	unsigned int engineBaseBackCol = clBG;
	SDL_Renderer* activeRenderer = renderer.load();

	if (activeRenderer == nullptr ||
		SetRenderTarget(activeRenderer, nullptr) == 0 ||
		!canPrepareRenderFrame())
	{
		return;
	}
	SDL_SetRenderDrawColor(activeRenderer, (engineBaseBackCol & 0xFF0000) >> 16, (engineBaseBackCol & 0xFF00) >> 8, engineBaseBackCol & 0xFF, 0);
	if (!canPrepareRenderFrame())
	{
		return;
	}
	SDL_RenderClear(activeRenderer);
	SDL_Rect s, d;
	s.x = 0;
	s.y = 0;
	s.h = height;
	s.w = width;
	d.x = displayRect.x;
	d.y = displayRect.y;
	d.w = displayRect.w;
	d.h = displayRect.h;
	if (!canPrepareRenderFrame())
	{
		return;
	}
	drawImage(realScreen, &s, &d);
	if (!canPrepareRenderFrame())
	{
		return;
	}
    drawCursor();
	if (!canPrepareRenderFrame())
	{
		return;
	}
	SDL_RenderPresent(activeRenderer);
}

void EngineBase::updateRect(int tempWidth, int tempHeight, Rect & rect)
{
	if (tempWidth <= 0 || tempHeight <= 0 || width <= 0 || height <= 0)
	{
		return;
	}

	if (_fullScreenMode != FullScreenMode::window || _fullScreenSolutionMode != FullScreenSolutionMode::forceToUseSetting)
	{
		rect.x = 0;
		rect.w = tempWidth;
		rect.y = 0;
		rect.h = tempHeight;
	}
	else if ((float)tempWidth / (float)width < (float)tempHeight / (float)height)
	{
		rect.x = 0;
		rect.w = tempWidth;
		rect.y = (int)floor(((float)tempHeight - (float)height * (float)tempWidth / (float)width) / 2 + 0.5);
		rect.h = (int)floor((float)height * (float)tempWidth / (float)width + 0.5);
	}
	else
	{
		rect.y = 0;
		rect.h = tempHeight;
		rect.x = (int)floor(((float)tempWidth - (float)width * (float)tempHeight / (float)height) / 2 + 0.5);
		rect.w = (int)floor((float)width * (float)tempHeight / (float)height + 0.5);
	}
}

void * EngineBase::getMem(int size)
{
	if (size > 0)
	{
		return malloc((size_t)size);
	}
	return nullptr;
}

void EngineBase::freeMem(void * mem)
{
	if (mem != nullptr)
	{
		free(mem);
	}
}

int EngineBase::getLZOOutLen(int inLen)
{
	return inLen + inLen / 16 + 64 + 3;
}

int EngineBase::lzoCompress(const void * src, unsigned int srcLen, void * dst, lzo_uint * dstLen)
{
	if (lzoMem == nullptr)
	{
		lzoMem = getMem(LZO1X_1_MEM_COMPRESS);
	}
	if (lzoMem == nullptr)
	{
		return -1;
	}
	int ret = lzo1x_1_compress((const unsigned char *)src, srcLen, (unsigned char *)dst, (lzo_uint *)dstLen, lzoMem);
	freeMem(lzoMem);
	if (ret == LZO_E_OK)
	{
		return 0;
	}
	return -1;
}

int EngineBase::lzoDecompress(const void * src, unsigned int srcLen, void * dst, lzo_uint * dstLen)
{
	if (lzo1x_decompress((const unsigned char *)src, srcLen, (unsigned char *)dst, (lzo_uint *)dstLen, nullptr) == LZO_E_OK)
	{
		return 0;
	}
	return -1;
}

void EngineBase::calculateCursorReferencePosition(int inX, int inY, int* outX, int* outY)
{
	if (inX >= rect.x && inX < rect.x + rect.w && inY >= rect.y && inY < rect.y + rect.h)
	{
		if (outX != nullptr)
		{
			*outX = (int)round((float)(inX - rect.x) / ((float)rect.w) * (float)width );
		}
		if (outY != nullptr)
		{
			*outY = (int)round((float)(inY - rect.y) / ((float)rect.h) * (float)height );
		}
	}
}
