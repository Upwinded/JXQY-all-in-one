#include "../Launch/EditorRunRuntimeSession.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace
{
namespace fs = std::filesystem;

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << "\n";
	}
	return condition;
}

bool writeBytes(
	const fs::path& path,
	const std::string& bytes)
{
	std::ofstream output(path, std::ios::binary);
	output.write(
		bytes.data(),
		static_cast<std::streamsize>(bytes.size()));
	return output.good();
}

#ifdef _WIN32
bool renameDirectoryForAba(
	const fs::path& source,
	const fs::path& destination,
	std::string& detail)
{
	if (!MoveFileExW(
			source.c_str(),
			destination.c_str(),
			MOVEFILE_WRITE_THROUGH))
	{
		detail =
			"MoveFileExW error=" +
			std::to_string(GetLastError());
		return false;
	}
	return true;
}

bool createDirectoryJunction(
	const fs::path& targetPath,
	const fs::path& junctionPath,
	std::string& detail)
{
	std::error_code createError;
	if (!fs::create_directory(
			junctionPath, createError) ||
		createError)
	{
		detail =
			"create directory error=" +
			createError.message();
		return false;
	}

	const std::wstring nativeTarget =
		fs::absolute(targetPath).
			lexically_normal().
			wstring();
	const std::wstring substituteName =
		std::wstring(L"\\??\\") +
		nativeTarget;
	const std::wstring printName =
		nativeTarget;
	const WORD substituteBytes =
		static_cast<WORD>(
			substituteName.size() *
			sizeof(wchar_t));
	const WORD printBytes =
		static_cast<WORD>(
			printName.size() *
			sizeof(wchar_t));
	struct MountPointReparseBuffer
	{
		DWORD reparseTag;
		WORD reparseDataLength;
		WORD reserved;
		WORD substituteNameOffset;
		WORD substituteNameLength;
		WORD printNameOffset;
		WORD printNameLength;
		wchar_t pathBuffer[
			MAXIMUM_REPARSE_DATA_BUFFER_SIZE /
			sizeof(wchar_t)];
	};
	MountPointReparseBuffer buffer = {};
	buffer.reparseTag =
		IO_REPARSE_TAG_MOUNT_POINT;
	buffer.substituteNameLength =
		substituteBytes;
	buffer.printNameOffset =
		static_cast<WORD>(
			substituteBytes +
			sizeof(wchar_t));
	buffer.printNameLength =
		printBytes;
	buffer.reparseDataLength =
		static_cast<WORD>(
			4 * sizeof(WORD) +
			substituteBytes +
			sizeof(wchar_t) +
			printBytes +
			sizeof(wchar_t));
	if (8 + buffer.reparseDataLength >
		MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
	{
		detail =
			"junction target exceeds the reparse buffer";
		std::error_code removeError;
		fs::remove(junctionPath, removeError);
		return false;
	}
	std::copy(
		substituteName.begin(),
		substituteName.end(),
		buffer.pathBuffer);
	std::copy(
		printName.begin(),
		printName.end(),
		reinterpret_cast<wchar_t*>(
			reinterpret_cast<char*>(
				buffer.pathBuffer) +
			buffer.printNameOffset));

	const HANDLE handle =
		CreateFileW(
			junctionPath.c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS |
				FILE_FLAG_OPEN_REPARSE_POINT,
			nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		detail =
			"CreateFileW error=" +
			std::to_string(GetLastError());
		std::error_code removeError;
		fs::remove(junctionPath, removeError);
		return false;
	}
	DWORD returnedBytes = 0;
	const BOOL succeeded =
		DeviceIoControl(
			handle,
			FSCTL_SET_REPARSE_POINT,
			&buffer,
			8 + buffer.reparseDataLength,
			nullptr,
			0,
			&returnedBytes,
			nullptr);
	const DWORD junctionError =
		succeeded != FALSE ?
			ERROR_SUCCESS :
			GetLastError();
	CloseHandle(handle);
	if (succeeded == FALSE)
	{
		detail =
			"FSCTL_SET_REPARSE_POINT error=" +
			std::to_string(junctionError);
		std::error_code removeError;
		fs::remove(junctionPath, removeError);
		return false;
	}
	return true;
}
#endif

struct SessionFixture
{
	fs::path sessionRoot;
	fs::path descriptorPath;
	fs::path markerPath;
	fs::path resourceRoutingContractPath;
	fs::path diagnosticsPath;
	fs::path logPath;
	fs::path runtimeTracePath;
	EditorRun::Descriptor descriptor;
	std::string descriptorBytes;
	std::string resourceRoutingContractBytes;
};

const char* resourceRoutingRootPath()
{
#ifdef _WIN32
	return "C:/formal-root";
#else
	return "/formal-root";
#endif
}

std::string makeResourceRoutingContract(
	const std::string& rootVariant = "1")
{
	return
		"{\"schemaVersion\":1,"
		"\"roots\":[{"
			"\"path\":\"" +
			std::string(resourceRoutingRootPath()) +
			"-" +
			rootVariant +
			"\","
			"\"roles\":[\"resource\",\"save\"]"
		"}]}";
}

SessionFixture makeFixture(
	const fs::path& base,
	const std::string& sessionId)
{
	SessionFixture fixture;
	fixture.sessionRoot =
		base / fs::u8path(sessionId);
	fs::create_directories(
		fixture.sessionRoot / "overlay");
	fs::create_directories(
		fixture.sessionRoot / "save");
	fs::create_directories(
		fixture.sessionRoot / "application-state");
	fs::create_directories(
		fixture.sessionRoot / "diagnostics");
	fixture.descriptorPath =
		fixture.sessionRoot /
		"launch-descriptor.json";
	fixture.markerPath =
		fixture.sessionRoot /
		"session-marker.json";
	fixture.resourceRoutingContractPath =
		fixture.sessionRoot /
		"resource-routing-contract.json";
	fixture.diagnosticsPath =
		fixture.sessionRoot /
		"diagnostics/diagnostics.jsonl";
	fixture.logPath =
		fixture.sessionRoot /
		"diagnostics/game.log";
	fixture.runtimeTracePath =
		fixture.sessionRoot /
		"diagnostics/runtime-trace.jsonl";

	const fs::path assetsRoot =
		base / fs::u8path(u8"正式 assets");
	fs::create_directories(assetsRoot);
	fixture.descriptor.sessionId = sessionId;
	fixture.descriptor.assetsCollectionRoot =
		fs::absolute(assetsRoot);
	fixture.descriptor.activeResourcePackId =
		"JXQY2";
	fixture.descriptor.target.sceneId =
		"saved-scene";
	fixture.descriptor.target.sceneName =
		u8"已保存场景";
	fixture.descriptor.target.mapPath =
		u8"map/中都.map";
	fixture.descriptor.target.playerX = 12;
	fixture.descriptor.target.playerY = 34;
	fixture.descriptor.overlayRoot =
		fs::absolute(
			fixture.sessionRoot / "overlay");
	fixture.descriptor.isolatedSaveRoot =
		fs::absolute(
			fixture.sessionRoot / "save");
	fixture.descriptor.applicationStateRoot =
		fs::absolute(
			fixture.sessionRoot /
			"application-state");
	fixture.descriptor.diagnosticsPath =
		fs::absolute(fixture.diagnosticsPath);
	fixture.descriptor.logPath =
		fs::absolute(fixture.logPath);

	const EditorRun::DescriptorSerializationResult serialized =
		EditorRun::serializeEditorRunDescriptor(
			fixture.descriptor);
	if (serialized.succeeded())
	{
		fixture.descriptorBytes =
			serialized.bytes;
		writeBytes(
			fixture.descriptorPath,
			fixture.descriptorBytes);
	}
	writeBytes(
		fixture.markerPath,
		"{\"schemaVersion\":1,"
		"\"descriptorSchemaVersion\":" +
			std::to_string(
				EditorRun::Descriptor::SchemaVersion) +
			","
		"\"sessionId\":\"" +
			sessionId +
			"\"}");
	fixture.resourceRoutingContractBytes =
		makeResourceRoutingContract();
	writeBytes(
		fixture.resourceRoutingContractPath,
		fixture.resourceRoutingContractBytes);
	return fixture;
}

bool rewriteFixtureAsLegacyDescriptor(
	SessionFixture& fixture,
	std::int32_t markerDescriptorSchemaVersion)
{
	const std::string currentVersion =
		"\"schemaVersion\":" +
		std::to_string(
			EditorRun::Descriptor::SchemaVersion);
	const std::string legacyVersion =
		"\"schemaVersion\":" +
		std::to_string(
			EditorRun::Descriptor::LegacySchemaVersion);
	const std::size_t versionOffset =
		fixture.descriptorBytes.find(currentVersion);
	if (versionOffset == std::string::npos)
	{
		return false;
	}
	fixture.descriptorBytes.replace(
		versionOffset,
		currentVersion.size(),
		legacyVersion);
	return writeBytes(
			fixture.descriptorPath,
			fixture.descriptorBytes) &&
		writeBytes(
			fixture.markerPath,
			"{\"schemaVersion\":1,"
			"\"descriptorSchemaVersion\":" +
				std::to_string(
					markerDescriptorSchemaVersion) +
				",\"sessionId\":\"" +
				fixture.descriptor.sessionId +
				"\"}");
}

bool writeDescriptorForSessionRoot(
	SessionFixture& fixture,
	const fs::path& descriptorSessionRoot)
{
	const fs::path absoluteSessionRoot =
		fs::absolute(
			descriptorSessionRoot).
				lexically_normal();
	fixture.descriptor.overlayRoot =
		absoluteSessionRoot / "overlay";
	fixture.descriptor.isolatedSaveRoot =
		absoluteSessionRoot / "save";
	fixture.descriptor.applicationStateRoot =
		absoluteSessionRoot /
		"application-state";
	fixture.descriptor.diagnosticsPath =
		absoluteSessionRoot /
		"diagnostics" /
		"diagnostics.jsonl";
	fixture.descriptor.logPath =
		absoluteSessionRoot /
		"diagnostics" /
		"game.log";
	const EditorRun::DescriptorSerializationResult
		serialized =
		EditorRun::serializeEditorRunDescriptor(
			fixture.descriptor);
	if (!serialized.succeeded())
	{
		return false;
	}
	fixture.descriptorBytes =
		serialized.bytes;
	return writeBytes(
		fixture.descriptorPath,
		fixture.descriptorBytes);
}

bool testValidSession(const fs::path& root)
{
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97649";
	const SessionFixture fixture =
		makeFixture(
			root / fs::u8path(u8"会话 基目录"),
			sessionId);
	const EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(fixture.descriptorPath));
	const bool outputIdentitiesPublished =
		std::all_of(
			result.session.
				outputDirectoryIdentities.begin(),
			result.session.
				outputDirectoryIdentities.end(),
			[](const EditorRun::DirectoryIdentity&
				identity)
			{
				return identity.valid &&
					identity.linkCount > 0;
			});
	return check(
		result.succeeded() &&
		result.session.descriptor.sessionId ==
			sessionId &&
		result.session.descriptorPath ==
			fs::absolute(
				fixture.descriptorPath).
				lexically_normal() &&
		result.session.sessionRoot ==
			fs::canonical(fixture.sessionRoot) &&
		result.session.diagnosticsRoot ==
			fs::canonical(fixture.sessionRoot) /
				"diagnostics" &&
		outputIdentitiesPublished &&
		result.session.traceOverlayOrigins.empty(),
		"valid CJK-and-space editor session publishes private output identities and routing metadata");
}

bool testLegacyDescriptorSession(const fs::path& root)
{
	const std::string acceptedSessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e9766a";
	SessionFixture accepted =
		makeFixture(
			root / "legacy-descriptor-accepted",
			acceptedSessionId);
	bool ok = check(
		rewriteFixtureAsLegacyDescriptor(
			accepted,
			EditorRun::Descriptor::LegacySchemaVersion),
		"legacy-session fixture rewrites descriptor and matching marker");
	const EditorRun::RuntimeSessionResult acceptedResult =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(accepted.descriptorPath));
	ok = check(
		acceptedResult.succeeded() &&
			acceptedResult.session.descriptor.
				sourceSchemaVersion ==
				EditorRun::Descriptor::
					LegacySchemaVersion,
		"legacy v1 descriptor session loads with its declared marker schema") &&
		ok;

	SessionFixture custom =
		makeFixture(
			root / "custom-invocation",
			"editor-preview-1");
	const fs::path customDescriptorPath =
		custom.sessionRoot / "invocation.json";
	std::error_code renameError;
	fs::rename(
		custom.descriptorPath,
		customDescriptorPath,
		renameError);
	ok = check(
		!renameError &&
		EditorRun::loadEditorRunRuntimeSession(
			customDescriptorPath).succeeded(),
		"runtime accepts an editor invocation without UUID or fixed descriptor leaf requirements") &&
		ok;
	return ok;
}

#ifdef _WIN32
bool testWindowsGenericSeparatorDescriptorPath(
	const fs::path& root)
{
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e9764a";
	const SessionFixture fixture =
		makeFixture(
			root / fs::u8path(
				u8"Qt 正斜杠会话"),
			sessionId);
	std::wstring descriptorText =
		fs::absolute(
			fixture.descriptorPath).
				wstring();
	std::replace(
		descriptorText.begin(),
		descriptorText.end(),
		L'\\',
		L'/');
	const fs::path genericDescriptorPath(
		descriptorText);
	const EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			genericDescriptorPath);
	return check(
		result.succeeded() &&
			result.session.descriptorPath ==
				fs::absolute(
					fixture.descriptorPath).
					lexically_normal(),
		"runtime accepts the forward-slash absolute descriptor path emitted by the Qt editor on Windows");
}

bool testWindowsCanonicalSpellingProjection(
	const fs::path& root)
{
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e9764b";
	const fs::path physicalBase =
		root / "Canonical-Spelling-Base";
	const fs::path requestedBase =
		root / "canonical-spelling-base";
	SessionFixture fixture =
		makeFixture(
			physicalBase,
			sessionId);
	const fs::path requestedSessionRoot =
		requestedBase /
		fs::u8path(sessionId);
	const fs::path requestedDescriptorPath =
		fs::absolute(
			requestedSessionRoot /
			"launch-descriptor.json").
				lexically_normal();
	const DWORD physicalBaseAttributes =
		GetFileAttributesW(
			physicalBase.c_str());

	std::error_code equivalentError;
	bool ok = check(
		physicalBase != requestedBase &&
		physicalBaseAttributes !=
			INVALID_FILE_ATTRIBUTES &&
		(physicalBaseAttributes &
			FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
		fs::equivalent(
			physicalBase,
			requestedBase,
			equivalentError) &&
		!equivalentError &&
		requestedSessionRoot.filename() ==
			fs::u8path(sessionId) &&
		requestedSessionRoot.parent_path().
				parent_path() ==
			fixture.sessionRoot.parent_path().
				parent_path(),
		"canonical-spelling fixture changes only a non-reparse ancestor while preserving the exact lowercase UUID leaf");
	ok = check(
		writeDescriptorForSessionRoot(
			fixture,
			requestedSessionRoot),
		"canonical-spelling fixture serializes the requested logical output topology") &&
		ok;

	std::error_code canonicalError;
	const fs::path canonicalSessionRoot =
		fs::canonical(
			fixture.sessionRoot,
			canonicalError);
	ok = check(
		!canonicalError &&
		canonicalSessionRoot !=
			requestedSessionRoot.
				lexically_normal(),
		"canonical-spelling fixture exposes a distinct canonical spelling for the same native session directory") &&
		ok;

	const EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			requestedDescriptorPath);
	ok = check(
		result.succeeded() &&
		result.session.descriptorPath ==
			requestedDescriptorPath &&
		result.session.sessionRoot ==
			requestedSessionRoot &&
		result.session.diagnosticsRoot ==
			requestedSessionRoot /
				"diagnostics" &&
		result.session.descriptor.overlayRoot ==
			requestedSessionRoot /
				"overlay" &&
		result.session.descriptor.isolatedSaveRoot ==
			requestedSessionRoot /
				"save" &&
		result.session.descriptor.applicationStateRoot ==
			requestedSessionRoot /
				"application-state" &&
		result.session.descriptor.diagnosticsPath ==
			requestedSessionRoot /
				"diagnostics" /
				"diagnostics.jsonl" &&
		result.session.descriptor.logPath ==
			requestedSessionRoot /
				"diagnostics" /
				"game.log" &&
		result.session.runtimeTracePath ==
			requestedSessionRoot /
				"diagnostics" /
				"runtime-trace.jsonl",
		"runtime preserves editor-provided path spelling while verifying the native directories") &&
		ok;
	return ok;
}

bool testWindowsAncestorJunctionRejected(
	const fs::path& root)
{
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e9764d";
	const fs::path physicalAncestor =
		root / "junction-target";
	const fs::path physicalBase =
		physicalAncestor / "sessions";
	SessionFixture fixture =
		makeFixture(
			physicalBase,
			sessionId);
	const fs::path junctionAncestor =
		root / "junction-route";
	std::string junctionDetail;
	bool ok = check(
		createDirectoryJunction(
			physicalAncestor,
			junctionAncestor,
			junctionDetail),
		"ancestor-junction fixture creates a real Windows directory junction (" +
			junctionDetail + ")");
	if (!ok)
	{
		return false;
	}

	const fs::path requestedSessionRoot =
		junctionAncestor /
		"sessions" /
		fs::u8path(sessionId);
	const fs::path requestedDescriptorPath =
		requestedSessionRoot /
		"launch-descriptor.json";
	ok = check(
		writeDescriptorForSessionRoot(
			fixture,
			requestedSessionRoot),
		"ancestor-junction fixture serializes the junction-routed logical topology") &&
		ok;
	std::error_code equivalentError;
	ok = check(
		fs::equivalent(
			requestedDescriptorPath,
			fixture.descriptorPath,
			equivalentError) &&
		!equivalentError,
		"ancestor-junction fixture confirms that the routed descriptor resolves to the physical descriptor") &&
		ok;

	const EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(
				requestedDescriptorPath));
	ok = check(
		result.succeeded(),
		"runtime accepts an existing editor session reached through an explicit absolute junction path") &&
		ok;

	std::error_code removeError;
	fs::remove(junctionAncestor, removeError);
	ok = check(
		!removeError,
		"ancestor-junction fixture removes the junction without touching its target") &&
		ok;
	return ok;
}
#endif

bool testDescriptorReadFailures(const fs::path& root)
{
	bool ok = true;
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97650";
	SessionFixture fixture =
		makeFixture(root / "read-failures", sessionId);

	fs::remove(fixture.descriptorPath);
	EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				DescriptorOpenFailed &&
		result.failureCategory ==
			EditorRun::RuntimeSessionFailureCategory::
				DescriptorRead,
		"missing descriptor maps to the read failure category") &&
		ok;

	writeBytes(
		fixture.descriptorPath,
		std::string(
			EditorRun::MaximumDescriptorBytes + 1,
			'x'));
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				DescriptorTooLarge &&
		result.failureCategory ==
			EditorRun::RuntimeSessionFailureCategory::
				DescriptorRead,
		"oversized descriptor maps to the bounded read failure category") &&
		ok;
	return ok;
}

bool testDescriptorValidationFailures(
	const fs::path& root)
{
	bool ok = true;
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97651";
	SessionFixture fixture =
		makeFixture(root / "descriptor-invalid", sessionId);

	const fs::path alternateDescriptorPath =
		fixture.sessionRoot / "other.json";
	std::error_code renameError;
	fs::rename(
		fixture.descriptorPath,
		alternateDescriptorPath,
		renameError);
	EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			alternateDescriptorPath);
	ok = check(
		!renameError && result.succeeded(),
		"descriptor validation does not depend on a fixed file name") &&
		ok;

	fixture = makeFixture(
		root / "descriptor-invalid-json",
		sessionId);
	writeBytes(
		fixture.descriptorPath,
		"{\"schemaVersion\":1");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				DescriptorInvalid &&
		result.descriptorError ==
			EditorRun::DescriptorError::InvalidJson &&
		result.failureCategory ==
			EditorRun::RuntimeSessionFailureCategory::
				DescriptorValidation,
		"invalid JSON preserves descriptor parser evidence") &&
		ok;

	std::string futureDescriptor =
		fixture.descriptorBytes;
	const std::string currentVersion =
		"\"schemaVersion\":" +
		std::to_string(
			EditorRun::Descriptor::SchemaVersion);
	const std::string futureVersion =
		"\"schemaVersion\":" +
		std::to_string(
			EditorRun::Descriptor::SchemaVersion + 1);
	const std::size_t versionOffset =
		futureDescriptor.find(currentVersion);
	ok = check(
		versionOffset != std::string::npos,
		"future-version fixture locates the serialized schema field") &&
		ok;
	if (versionOffset != std::string::npos)
	{
		futureDescriptor.replace(
			versionOffset,
			currentVersion.size(),
			futureVersion);
		writeBytes(
			fixture.descriptorPath,
			futureDescriptor);
		result =
			EditorRun::loadEditorRunRuntimeSession(
				fs::absolute(
					fixture.descriptorPath));
		ok = check(
			result.error ==
				EditorRun::RuntimeSessionError::
					DescriptorInvalid &&
			result.descriptorError ==
				EditorRun::DescriptorError::
					UnsupportedVersion &&
			result.diagnosticCode ==
				"editor_run.descriptor.unsupported_version",
			"future descriptor versions preserve the stable unsupported-version diagnostic code") &&
			ok;
	}

	fixture = makeFixture(
		root / "session-id-mismatch",
		sessionId);
	fixture.descriptor.sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97652";
	const EditorRun::DescriptorSerializationResult serialized =
		EditorRun::serializeEditorRunDescriptor(
			fixture.descriptor);
	writeBytes(
		fixture.descriptorPath,
		serialized.bytes);
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded() &&
		result.session.descriptor.sessionId ==
			"c7b3fe3a-7a51-4a72-86d7-eab017e97652",
		"session identifier is invocation metadata rather than a directory naming rule") &&
		ok;

	fixture = makeFixture(
		root / "descriptor-hard-link",
		sessionId);
	const fs::path secondLink =
		fixture.sessionRoot / "descriptor-second-link.json";
	std::error_code hardLinkError;
	fs::create_hard_link(
		fixture.descriptorPath,
		secondLink,
		hardLinkError);
	ok = check(
		!hardLinkError,
		"descriptor hard-link fixture executes") &&
		ok;
	if (!hardLinkError)
	{
		result =
			EditorRun::loadEditorRunRuntimeSession(
				fs::absolute(
					fixture.descriptorPath));
		ok = check(
			result.error ==
				EditorRun::RuntimeSessionError::
					DescriptorUnsafe,
			"multi-link descriptor is rejected by the rooted reader") &&
			ok;
	}
	return ok;
}

bool testIsolationFailures(const fs::path& root)
{
	bool ok = true;
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97653";
	SessionFixture fixture =
		makeFixture(root / "topology", sessionId);
	fixture.descriptor.overlayRoot =
		fixture.sessionRoot / "save";
	EditorRun::DescriptorSerializationResult serialized =
		EditorRun::serializeEditorRunDescriptor(
			fixture.descriptor);
	writeBytes(
		fixture.descriptorPath,
		serialized.bytes);
	EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				OutputTopologyInvalid &&
		result.failureCategory ==
			EditorRun::RuntimeSessionFailureCategory::
				Isolation,
		"private output directories cannot overlap") &&
		ok;

	fixture = makeFixture(
		root / "existing-diagnostics",
		sessionId);
	writeBytes(
		fixture.diagnosticsPath,
		"pre-existing");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded(),
		"existing diagnostics output can be reused for one editor invocation") &&
		ok;

	fixture = makeFixture(
		root / "existing-runtime-trace",
		sessionId);
	writeBytes(
		fixture.runtimeTracePath,
		"pre-existing");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded(),
		"existing runtime trace can be reused for one editor invocation") &&
		ok;

	fixture = makeFixture(
		root / "marker-mismatch",
		sessionId);
	writeBytes(
		fixture.markerPath,
		"{\"schemaVersion\":1}");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded(),
		"session marker content is not part of runtime launch validation") &&
		ok;

	fixture = makeFixture(
		root / "resource-routing-contract-missing",
		sessionId);
	fs::remove(fixture.resourceRoutingContractPath);
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				ResourceRoutingContractUnavailable,
		"resource-routing contract is required before runtime launch") &&
		ok;

	fixture = makeFixture(
		root / "marker-hard-link",
		sessionId);
	const fs::path markerSecondLink =
		fixture.sessionRoot /
		"marker-second-link.json";
	std::error_code hardLinkError;
	fs::create_hard_link(
		fixture.markerPath,
		markerSecondLink,
		hardLinkError);
	ok = check(
		!hardLinkError,
		"marker hard-link fixture executes") &&
		ok;
	if (!hardLinkError)
	{
		result =
			EditorRun::loadEditorRunRuntimeSession(
				fs::absolute(
					fixture.descriptorPath));
		ok = check(
			result.succeeded(),
			"runtime does not depend on the editor bookkeeping marker") &&
			ok;
	}

	fixture = makeFixture(
		root / "outside-output",
		sessionId);
	const fs::path outsideOutput =
		fixture.sessionRoot.parent_path() / "outside-overlay";
	fs::create_directories(outsideOutput);
	fixture.descriptor.overlayRoot =
		fs::absolute(outsideOutput);
	serialized = EditorRun::serializeEditorRunDescriptor(
		fixture.descriptor);
	writeBytes(fixture.descriptorPath, serialized.bytes);
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				OutputDirectoryUnavailable,
		"private output directories remain contained by the invocation directory") &&
		ok;
	return ok;
}

bool testResourceRoutingContractFailures(
	const fs::path& root)
{
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97654";
	const std::pair<const char*, std::string>
		invalidContracts[] =
	{
		{
			"missing schemaVersion",
			"{\"roots\":[]}"
		},
		{
			"unsupported schemaVersion",
			"{\"schemaVersion\":2,\"roots\":[]}"
		},
		{
			"duplicated schemaVersion",
			"{\"schemaVersion\":1,"
			"\"schemaVersion\":1}"
		},
		{
			"malformed roots JSON",
			"{\"schemaVersion\":1,"
			"\"roots\":[}"
		},
		{
			"malformed traceOverlayOrigins",
			"{\"schemaVersion\":1,"
			"\"traceOverlayOrigins\":\"invalid\"}"
		}
	};

	bool ok = true;
	for (std::size_t index = 0;
		index < std::size(invalidContracts);
		++index)
	{
		const SessionFixture fixture =
			makeFixture(
				root /
					("resource-routing-contract-" +
						std::to_string(index)),
				sessionId);
		ok = check(
			writeBytes(
				fixture.resourceRoutingContractPath,
				invalidContracts[index].second),
			std::string(
				"resource-routing contract fixture writes: ") +
				invalidContracts[index].first) &&
			ok;
		const EditorRun::RuntimeSessionResult result =
			EditorRun::loadEditorRunRuntimeSession(
				fs::absolute(
					fixture.descriptorPath));
		ok = check(
			result.error ==
				EditorRun::RuntimeSessionError::
					ResourceRoutingContractUnavailable &&
			result.failureCategory ==
				EditorRun::
					RuntimeSessionFailureCategory::
						Isolation &&
			result.fieldPath ==
				"resourceRoutingContract",
			std::string(
				"strict resource-routing contract rejects ") +
				invalidContracts[index].first) &&
			ok;
	}

	const std::pair<const char*, std::string>
		acceptedMetadata[] =
	{
		{
			"no formal-root metadata",
			"{\"schemaVersion\":1}"
		},
		{
			"empty formal-root metadata",
			"{\"schemaVersion\":1,\"roots\":[]}"
		},
		{
			"legacy identity and content metadata",
			"{\"schemaVersion\":1,"
			"\"hashAlgorithm\":\"sha256\","
			"\"roots\":[{"
				"\"path\":\"relative-or-stale\","
				"\"roles\":[],"
				"\"device\":\"1\","
				"\"nodeHigh\":\"0\","
				"\"nodeLow\":\"1\","
				"\"linkCount\":\"1\","
				"\"entries\":[{\"sha256\":\"stale\"}]"
			"}]}"
		},
		{
			"opaque formal-root metadata",
			"{\"schemaVersion\":1,"
			"\"roots\":{\"editorOnly\":true}}"
		}
	};
	for (std::size_t index = 0;
		index < std::size(acceptedMetadata);
		++index)
	{
		const SessionFixture fixture =
			makeFixture(
				root /
					("resource-routing-metadata-" +
						std::to_string(index)),
				sessionId);
		ok = check(
			writeBytes(
				fixture.resourceRoutingContractPath,
				acceptedMetadata[index].second),
			std::string(
				"resource-routing metadata fixture writes: ") +
				acceptedMetadata[index].first) &&
			ok;
		const EditorRun::RuntimeSessionResult result =
			EditorRun::loadEditorRunRuntimeSession(
				fs::absolute(
					fixture.descriptorPath));
		ok = check(
			result.succeeded(),
			std::string(
				"runtime ignores ") +
				acceptedMetadata[index].first) &&
			ok;
	}
	return ok;
}

bool testInvocationDoesNotRetainFilesystemGeneration(
	const fs::path& root)
{
	const SessionFixture fixture =
		makeFixture(
			root / "single-invocation",
			"editor-preview");
	const EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(fixture.descriptorPath));
	if (!check(
			result.succeeded(),
			"runtime loads one editor invocation"))
	{
		return false;
	}

	const fs::path overlayBackup =
		fixture.sessionRoot / "overlay-old";
	std::error_code error;
	fs::rename(
		fixture.descriptor.overlayRoot,
		overlayBackup,
		error);
	if (error)
	{
		return check(
			false,
			"loaded invocation releases its filesystem handles");
	}
	return check(
		fs::create_directory(
			fixture.descriptor.overlayRoot,
			error) &&
		!error,
		"loaded invocation does not retain a long-lived filesystem generation");
}

enum class CaseSensitiveFixtureStatus
{
	Enabled,
	Unsupported
};

CaseSensitiveFixtureStatus enableCaseSensitiveDirectory(
	const fs::path& directory,
	std::string& detail)
{
#ifdef _WIN32
	using SetFileInformationByHandleFunction =
		BOOL (WINAPI*)(
			HANDLE,
			int,
			LPVOID,
			DWORD);
	using GetFileInformationByHandleExFunction =
		BOOL (WINAPI*)(
			HANDLE,
			int,
			LPVOID,
			DWORD);
	struct CaseSensitiveInformation
	{
		ULONG flags;
	};
	constexpr int fileCaseSensitiveInformationClass = 23;
	constexpr ULONG caseSensitiveDirectoryFlag = 1;

	const HANDLE handle =
		CreateFileW(
			directory.c_str(),
			FILE_READ_ATTRIBUTES |
				FILE_WRITE_ATTRIBUTES,
			FILE_SHARE_READ |
				FILE_SHARE_WRITE |
				FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS,
			nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		detail =
			"CreateFileW error=" +
			std::to_string(GetLastError());
		return CaseSensitiveFixtureStatus::
			Unsupported;
	}
	const HMODULE kernel =
		GetModuleHandleW(L"kernel32.dll");
	const auto setInformation =
		reinterpret_cast<
			SetFileInformationByHandleFunction>(
				GetProcAddress(
					kernel,
					"SetFileInformationByHandle"));
	const auto getInformation =
		reinterpret_cast<
			GetFileInformationByHandleExFunction>(
				GetProcAddress(
					kernel,
					"GetFileInformationByHandleEx"));
	CaseSensitiveInformation information{
		caseSensitiveDirectoryFlag};
	const bool setSucceeded =
		setInformation != nullptr &&
		setInformation(
			handle,
			fileCaseSensitiveInformationClass,
			&information,
			sizeof(information)) != FALSE;
	const DWORD setError =
		setSucceeded ? ERROR_SUCCESS :
			GetLastError();
	information.flags = 0;
	const bool querySucceeded =
		setSucceeded &&
		getInformation != nullptr &&
		getInformation(
			handle,
			fileCaseSensitiveInformationClass,
			&information,
			sizeof(information)) != FALSE;
	const DWORD queryError =
		querySucceeded ? ERROR_SUCCESS :
			GetLastError();
	CloseHandle(handle);
	if (!setSucceeded || !querySucceeded ||
		(information.flags &
			caseSensitiveDirectoryFlag) == 0)
	{
		detail =
			"set error=" +
			std::to_string(setError) +
			", query error=" +
			std::to_string(queryError);
		return CaseSensitiveFixtureStatus::
			Unsupported;
	}
	return CaseSensitiveFixtureStatus::Enabled;
#else
	(void)directory;
	(void)detail;
	return CaseSensitiveFixtureStatus::Enabled;
#endif
}

bool testCaseSensitiveCustomTopology(
	const fs::path& root)
{
	const std::string sessionId =
		"c7b3fe3a-7a51-4a72-86d7-eab017e97656";
	const fs::path base =
		root / "case-sensitive-topology";
	const fs::path sessionRoot =
		base / sessionId;
	std::error_code createError;
	fs::create_directories(
		sessionRoot,
		createError);
	if (!check(
			!createError,
			"case-sensitive fixture creates the session root"))
	{
		return false;
	}
	std::string detail;
	if (enableCaseSensitiveDirectory(
			sessionRoot,
			detail) ==
		CaseSensitiveFixtureStatus::Unsupported)
	{
		std::cout <<
			"SKIP: real per-directory case-sensitive fixture unavailable (" <<
			detail << ")\n";
		return true;
	}

	SessionFixture fixture =
		makeFixture(base, sessionId);
	const fs::path wrongCaseOverlay =
		fixture.sessionRoot / "OVERLAY";
	std::error_code upperError;
	const bool upperCreated =
		fs::create_directory(
			wrongCaseOverlay,
			upperError);
	bool ok = check(
		upperCreated && !upperError,
		"case-sensitive fixture creates physically distinct overlay and OVERLAY children");
	if (!ok)
	{
		return false;
	}
	std::error_code equivalentError;
	ok = check(
		!fs::equivalent(
			fixture.sessionRoot / "overlay",
			wrongCaseOverlay,
			equivalentError) &&
		!equivalentError,
		"case-sensitive fixture confirms the differently cased leaves have distinct native identities") &&
		ok;

	fixture.descriptor.overlayRoot =
		fs::absolute(wrongCaseOverlay);
	const EditorRun::DescriptorSerializationResult
		serialized =
		EditorRun::serializeEditorRunDescriptor(
			fixture.descriptor);
	ok = check(
		serialized.succeeded() &&
		writeBytes(
			fixture.descriptorPath,
			serialized.bytes),
		"case-sensitive fixture redirects descriptor to the distinct wrong-case leaf") &&
		ok;
	const EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded() &&
		result.session.descriptor.overlayRoot ==
			fs::absolute(wrongCaseOverlay),
		"runtime accepts an explicit distinct private output directory without imposing a fixed leaf name") &&
		ok;
	return ok;
}

bool testTraceOverlayOriginContract(
	const fs::path& root)
{
	const auto writeOrigins =
		[](SessionFixture& fixture,
			std::string_view origins)
		{
			if (fixture.
					resourceRoutingContractBytes.
						empty() ||
				fixture.
					resourceRoutingContractBytes.
						back() != '}')
			{
				return false;
			}
			fixture.resourceRoutingContractBytes.
				pop_back();
			fixture.resourceRoutingContractBytes.
				append(
				",\"traceOverlayOrigins\":");
			fixture.resourceRoutingContractBytes.
				append(origins);
			fixture.resourceRoutingContractBytes.
				push_back('}');
			return writeBytes(
				fixture.resourceRoutingContractPath,
				fixture.
					resourceRoutingContractBytes);
		};

	bool ok = true;
	SessionFixture fixture = makeFixture(
		root / "trace-overlay-origins-valid",
		"c7b3fe3a-7a51-4a72-86d7-eab017e97001");
	ok = check(
		writeOrigins(
			fixture,
			u8"[{\"virtualPath\":\"script/a.lua\",\"rootOrdinal\":0},"
			u8"{\"virtualPath\":\"script/中.lua\",\"rootOrdinal\":0}]"),
		"trace overlay fixture writes the optional resource-routing contract field") &&
		ok;
	EditorRun::RuntimeSessionResult result =
		EditorRun::loadEditorRunRuntimeSession(
			fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded() &&
		result.session.traceOverlayOrigins.size() == 2 &&
		result.session.traceOverlayOrigins[0].virtualPath ==
			"script/a.lua" &&
		result.session.traceOverlayOrigins[1].virtualPath ==
			u8"script/中.lua" &&
		result.session.traceOverlayOrigins[1].rootOrdinal == 0,
		"runtime strictly reads the sorted portable overlay-origin mapping") &&
		ok;

	fixture = makeFixture(
		root / "trace-overlay-origin-logical-root",
		"c7b3fe3a-7a51-4a72-86d7-eab017e97005");
	const std::string logicalOrigin =
		"[{\"virtualPath\":\"script/a.lua\","
		"\"rootOrdinal\":255,"
		"\"rootKind\":\"active\","
		"\"resourcePackId\":\"MOD\","
		"\"rootPath\":\"" +
			std::string(resourceRoutingRootPath()) +
			"\"}]";
	ok = check(
		writeOrigins(fixture, logicalOrigin),
		"trace overlay fixture writes stable logical root metadata") &&
		ok;
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.succeeded() &&
			result.session.traceOverlayOrigins.size() == 1 &&
			result.session.traceOverlayOrigins[0].rootOrdinal ==
				255 &&
			result.session.traceOverlayOrigins[0].rootKind ==
				EditorRun::RuntimeTraceRootKind::Active &&
			result.session.traceOverlayOrigins[0].
				resourcePackId ==
					std::optional<std::string>("MOD") &&
			result.session.traceOverlayOrigins[0].rootPath ==
				fs::u8path(
					resourceRoutingRootPath()).
						lexically_normal(),
		"runtime bounded parser retains stable logical overlay provenance independently of the stale ordinal") &&
		ok;

	fixture = makeFixture(
		root / "trace-overlay-origins-unsorted",
		"c7b3fe3a-7a51-4a72-86d7-eab017e97002");
	(void)writeOrigins(
		fixture,
		"[{\"virtualPath\":\"script/b.lua\",\"rootOrdinal\":0},"
		"{\"virtualPath\":\"script/a.lua\",\"rootOrdinal\":0}]");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				ResourceRoutingContractUnavailable,
		"runtime rejects unsorted overlay-origin paths") &&
		ok;

	fixture = makeFixture(
		root / "trace-overlay-origin-root-out-of-range",
		"c7b3fe3a-7a51-4a72-86d7-eab017e97003");
	(void)writeOrigins(
		fixture,
		"[{\"virtualPath\":\"script/a.lua\",\"rootOrdinal\":256}]");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				ResourceRoutingContractUnavailable,
		"runtime rejects an overlay-origin ordinal outside trace content-root bounds") &&
		ok;

	fixture = makeFixture(
		root / "trace-overlay-origin-unsafe-path",
		"c7b3fe3a-7a51-4a72-86d7-eab017e97004");
	(void)writeOrigins(
		fixture,
		"[{\"virtualPath\":\"../script/a.lua\",\"rootOrdinal\":0}]");
	result = EditorRun::loadEditorRunRuntimeSession(
		fs::absolute(fixture.descriptorPath));
	ok = check(
		result.error ==
			EditorRun::RuntimeSessionError::
				ResourceRoutingContractUnavailable,
		"runtime rejects unsafe overlay-origin virtual paths") &&
		ok;
	return ok;
}
}

int main()
{
	const fs::path temporaryRoot =
		makeUniqueTestDirectory(
			"jxqy_editor_run_runtime_session");
	std::error_code error;
	fs::remove_all(temporaryRoot, error);
	error.clear();
	if (!fs::create_directories(
			temporaryRoot, error) ||
		error)
	{
		std::cerr <<
			"FAILED: could not create runtime-session test root\n";
		return 1;
	}

	bool ok = true;
	ok = testValidSession(temporaryRoot) && ok;
	ok = testLegacyDescriptorSession(
		temporaryRoot) && ok;
#ifdef _WIN32
	ok = testWindowsGenericSeparatorDescriptorPath(
		temporaryRoot) && ok;
	ok = testWindowsCanonicalSpellingProjection(
		temporaryRoot) && ok;
	ok = testWindowsAncestorJunctionRejected(
		temporaryRoot) && ok;
#endif
	ok = testDescriptorReadFailures(temporaryRoot) && ok;
	ok = testDescriptorValidationFailures(
		temporaryRoot) && ok;
	ok = testIsolationFailures(temporaryRoot) && ok;
	ok = testResourceRoutingContractFailures(
		temporaryRoot) && ok;
	ok = testInvocationDoesNotRetainFilesystemGeneration(
		temporaryRoot) && ok;
	ok = testTraceOverlayOriginContract(
		temporaryRoot) && ok;
	ok = testCaseSensitiveCustomTopology(
		temporaryRoot) && ok;
	error.clear();
	fs::remove_all(temporaryRoot, error);
	ok = check(!error,
		"runtime-session fixture root is removable") &&
		ok;
	return ok ? 0 : 1;
}
