#include "../File/File.h"
#include "../Game/GameManager/GameManager.h"
#include "../Launch/EditorRunSceneApplication.h"
#include "../Launch/EditorRunRuntimeTraceWriter.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class EditorRunSceneRuntimeTestAccess
{
public:
	static EditorRun::SceneApplicationResult apply(
		GameManager& gameManager)
	{
		return gameManager.applyEditorRunSceneTarget();
	}

	static bool initializePlayerBaseline(
		GameManager& gameManager,
		std::string& failureMessage,
		std::string& templateVirtualPath,
		std::string& isolatedPlayerVirtualPath,
		int& characterIndex)
	{
		bool resourceUnavailable = false;
		return gameManager.initializeEditorRunPlayerBaseline(
			failureMessage,
			templateVirtualPath,
			isolatedPlayerVirtualPath,
			characterIndex,
			resourceUnavailable);
	}

	static int characterIndex(
		const GameManager& gameManager)
	{
		return gameManager.global.data.characterIndex;
	}

	static ExactScriptExecutionResult runExactScript(
		GameManager& gameManager,
		const EditorRun::SearchRoot& root,
		const std::string& virtualPath)
	{
		return gameManager.scriptAPI.
			runScriptFromExactRoot(
				root, virtualPath);
	}
};

namespace
{
namespace fs = std::filesystem;

constexpr const char* PlayerTemplateVirtualPath =
	"ini/save/player0.ini";
constexpr const char* DefaultPlayerTemplateVirtualPath =
	"ini/save/player.ini";
constexpr const char* IsolatedPlayerPath =
	"game/player0.ini";
constexpr const char* IsolatedDefaultPlayerPath =
	"game/player.ini";

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool writeTextFile(
	const fs::path& path,
	const std::string& content)
{
	std::error_code error;
	fs::create_directories(path.parent_path(), error);
	if (error)
	{
		return false;
	}
	std::ofstream stream(
		path,
		std::ios::binary | std::ios::trunc);
	stream.write(
		content.data(),
		static_cast<std::streamsize>(content.size()));
	return stream.good();
}

bool copyFixtureFile(
	const fs::path& source,
	const fs::path& destination)
{
	std::error_code error;
	fs::create_directories(destination.parent_path(), error);
	if (error)
	{
		return false;
	}
	fs::copy_file(
		source,
		destination,
		fs::copy_options::overwrite_existing,
		error);
	return !error;
}

std::string lowerAscii(std::string value)
{
	std::transform(
		value.begin(),
		value.end(),
		value.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(
				std::tolower(character));
		});
	return value;
}

bool copyPlayerRuntimeResources(
	const fs::path& sourceRoot,
	const fs::path& destinationRoot,
	const std::string& npcResourceFileName)
{
	const fs::path sourceNpcResource =
		sourceRoot / "ini" / "npcres" /
		fs::u8path(npcResourceFileName);
	if (!copyFixtureFile(
			sourceNpcResource,
			destinationRoot / "ini" / "npcres" /
				fs::u8path(npcResourceFileName)) ||
		!copyFixtureFile(
			sourceRoot / "ini" / "level" /
				"level-hard.ini",
			destinationRoot / "ini" / "level" /
				"level-hard.ini") ||
		!copyFixtureFile(
			sourceRoot / "ini" / "magic" /
				fs::u8path(
					u8"player-magic-长剑.ini"),
			destinationRoot / "ini" / "magic" /
				fs::u8path(
					u8"player-magic-长剑.ini")))
	{
		return false;
	}
	const fs::path sourceNpcLevel =
		sourceRoot / "ini" / "level" /
		"level-npc.ini";
	std::error_code npcLevelError;
	if (fs::is_regular_file(
			sourceNpcLevel,
			npcLevelError) &&
		!copyFixtureFile(
			sourceNpcLevel,
			destinationRoot / "ini" / "level" /
				"level-npc.ini"))
	{
		return false;
	}
	if (npcLevelError &&
		npcLevelError !=
			std::errc::no_such_file_or_directory)
	{
		return false;
	}

	std::ifstream stream(sourceNpcResource);
	if (!stream)
	{
		return false;
	}
	std::string line;
	while (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		const std::size_t separator = line.find('=');
		if (separator == std::string::npos)
		{
			continue;
		}
		const std::string key =
			lowerAscii(line.substr(0, separator));
		if (key != "image" && key != "shade")
		{
			continue;
		}
		const std::string resourceName =
			line.substr(separator + 1);
		if (resourceName.empty())
		{
			continue;
		}
		const std::string extension =
			lowerAscii(
				fs::u8path(resourceName).
					extension().u8string());
		const char* packageDirectory =
			extension == ".asf"
			? "asf"
			: (extension == ".mpc" ||
				extension == ".shd")
				? "mpc"
				: nullptr;
		if (packageDirectory == nullptr ||
			resourceName.empty())
		{
			return false;
		}
		const fs::path sourcePackage =
			sourceRoot / packageDirectory /
			"character" /
			fs::u8path(resourceName);
		std::error_code packageError;
		const fs::file_status packageStatus =
			fs::symlink_status(
				sourcePackage,
				packageError);
		if (packageError ==
				std::errc::no_such_file_or_directory ||
			(!packageError &&
				packageStatus.type() ==
					fs::file_type::not_found))
		{
			continue;
		}
		if (packageError ||
			!fs::is_regular_file(packageStatus))
		{
			return false;
		}
		if (!copyFixtureFile(
				sourcePackage,
				destinationRoot / packageDirectory /
					"character" /
					fs::u8path(resourceName)))
		{
			return false;
		}
	}
	return stream.eof();
}

bool readFileBytes(
	const fs::path& path,
	std::vector<std::uint8_t>& bytes)
{
	bytes.clear();
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		return false;
	}
	stream.seekg(0, std::ios::end);
	const std::streamoff length = stream.tellg();
	if (length < 0)
	{
		return false;
	}
	stream.seekg(0, std::ios::beg);
	bytes.resize(static_cast<std::size_t>(length));
	if (!bytes.empty())
	{
		stream.read(
			reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	}
	return stream.good() || stream.eof();
}

bool fileHash(
	const fs::path& path,
	std::uint64_t& hash)
{
	std::vector<std::uint8_t> bytes;
	if (!readFileBytes(path, bytes))
	{
		return false;
	}
	hash = 1469598103934665603ULL;
	for (std::uint8_t byte : bytes)
	{
		hash ^= byte;
		hash *= 1099511628211ULL;
	}
	return true;
}

fs::path rootedPath(
	const fs::path& root,
	const std::string& virtualPath)
{
	return root / fs::u8path(virtualPath);
}

struct RuntimeFixture
{
	RuntimeFixture() :
		temporaryRoot(
			makeUniqueTestDirectory(
				"jxqy-editor-run-scene-runtime")),
		repositoryRoot(
			fs::path(__FILE__).parent_path().
				parent_path().parent_path()),
		formalRoot(repositoryRoot / "assets" / "yycs"),
		formalJxqy2Root(
			repositoryRoot / "assets" / "jxqy2"),
		earlierRoot(temporaryRoot / "earlier-root"),
		selectedRoot(temporaryRoot / "selected-root"),
		jxqy2FixtureRoot(
			temporaryRoot / "jxqy2-fixture-root"),
		overlayRoot(temporaryRoot / "session" / "overlay"),
		isolatedSaveRoot(temporaryRoot / "session" / "save"),
		applicationStateRoot(
			temporaryRoot / "session" / "application-state"),
		diagnosticsRoot(
			temporaryRoot / "session" / "diagnostics"),
		diagnosticsPath(
			diagnosticsRoot / "events.jsonl"),
		logPath(diagnosticsRoot / "game.log"),
		jxqy2OverlayRoot(
			temporaryRoot / "jxqy2-session" / "overlay"),
		jxqy2IsolatedSaveRoot(
			temporaryRoot / "jxqy2-session" / "save"),
		jxqy2ApplicationStateRoot(
			temporaryRoot / "jxqy2-session" /
				"application-state"),
		jxqy2DiagnosticsRoot(
			temporaryRoot / "jxqy2-session" /
				"diagnostics"),
		jxqy2DiagnosticsPath(
			jxqy2DiagnosticsRoot / "events.jsonl"),
		jxqy2LogPath(
			jxqy2DiagnosticsRoot / "game.log")
	{
	}

	~RuntimeFixture()
	{
		File::resetEditorRunFileLayout();
		std::error_code error;
		fs::remove_all(temporaryRoot, error);
	}

	bool initialize()
	{
		File::resetEditorRunFileLayout();
		std::error_code error;
		for (const fs::path& directory :
			{
				earlierRoot,
				selectedRoot,
				jxqy2FixtureRoot,
				overlayRoot,
				isolatedSaveRoot,
				applicationStateRoot,
				diagnosticsRoot,
				jxqy2OverlayRoot,
				jxqy2IsolatedSaveRoot,
				jxqy2ApplicationStateRoot,
				jxqy2DiagnosticsRoot
			})
		{
			fs::create_directories(directory, error);
			if (error)
			{
				std::cerr
					<< "Fixture directory creation failed: "
					<< directory.u8string() << '\n';
				return false;
			}
		}

		formalPlayerTemplate =
			formalRoot / "ini" / "save" / "player0.ini";
		formalPlayerSave =
			formalRoot / "save" / "game" / "player0.ini";
		if (!fileHash(
			formalPlayerTemplate,
			formalTemplateHash))
		{
			std::cerr
				<< "Fixture formal player template is unavailable\n";
			return false;
		}
		formalSaveExisted =
			fileHash(formalPlayerSave, formalSaveHash);

		if (!prepareJxqy2Fixture())
		{
			std::cerr
				<< "Fixture JXQY2 player resources could not be copied\n";
			return false;
		}
		if (!installYycsFileLayout())
		{
			std::cerr
				<< "Fixture YYCS editor-run layout could not be installed\n";
			return false;
		}
		if (!prepareValidSceneFiles())
		{
			std::cerr
				<< "Fixture YYCS scene resources could not be copied\n";
			return false;
		}
		return true;
	}

	bool installFileLayout(
		const fs::path& activeRoot,
		const std::vector<fs::path>& fallbackRoots,
		const fs::path& selectedOverlayRoot,
		const fs::path& selectedIsolatedSaveRoot,
		const fs::path& selectedApplicationStateRoot,
		const fs::path& selectedDiagnosticsRoot,
		const fs::path& selectedDiagnosticsPath,
		const fs::path& selectedLogPath)
	{
		File::resetEditorRunFileLayout();
		File::setAssetsCollectionRoot(
			(repositoryRoot / "assets").u8string());
		File::setActiveResourceRoot(
			activeRoot.u8string());
		File::setCommonResourceRoot("");
		std::vector<std::string> fallbackRootStrings;
		for (const fs::path& root : fallbackRoots)
		{
			fallbackRootStrings.push_back(root.u8string());
		}
		File::setResourceFallbackRoots(
			fallbackRootStrings);
		File::setUiResourceFallbackRoots({});
		File::setActiveSaveNamespace(
			"editor-run-scene-runtime");

		const File::EditorRunFileLayout layout =
		{
			selectedOverlayRoot.u8string(),
			selectedIsolatedSaveRoot.u8string(),
			selectedApplicationStateRoot.u8string(),
			selectedDiagnosticsRoot.u8string(),
			selectedDiagnosticsPath.u8string(),
			selectedLogPath.u8string(),
			(selectedDiagnosticsRoot /
				"runtime-trace.jsonl").u8string()
		};
		if (!File::installEditorRunFileLayoutForTests(layout))
		{
			return false;
		}
		return true;
	}

	bool installYycsFileLayout()
	{
		return installFileLayout(
			selectedRoot,
			{ formalRoot },
			overlayRoot,
			isolatedSaveRoot,
			applicationStateRoot,
			diagnosticsRoot,
			diagnosticsPath,
			logPath);
	}

	bool installJxqy2FileLayout()
	{
		return installFileLayout(
			jxqy2FixtureRoot,
			{},
			jxqy2OverlayRoot,
			jxqy2IsolatedSaveRoot,
			jxqy2ApplicationStateRoot,
			jxqy2DiagnosticsRoot,
			jxqy2DiagnosticsPath,
			jxqy2LogPath);
	}

	bool prepareJxqy2Fixture()
	{
		return copyFixtureFile(
				repositoryRoot / "src" / "tests" /
					"fixtures" /
					"editor_run_jxqy2_initial_player.ini",
				jxqy2FixtureRoot / "ini" / "save" /
					"player.ini") &&
			copyPlayerRuntimeResources(
				formalJxqy2Root,
				jxqy2FixtureRoot,
				u8"z-南宫飞云.ini");
	}

	bool prepareValidSceneFiles()
	{
		if (!copyFixtureFile(
				formalPlayerTemplate,
				rootedPath(
					selectedRoot,
					PlayerTemplateVirtualPath)) ||
			!copyFixtureFile(
				formalRoot / "map" /
					fs::u8path(
						u8"map_001_凌绝峰连接地图.map"),
				rootedPath(selectedRoot, mapVirtualPath)))
		{
			return false;
		}
		if (!copyPlayerRuntimeResources(
				formalRoot,
				selectedRoot,
				u8"z-杨影枫.ini"))
		{
			return false;
		}

		const std::string npcFile =
			"[Head]\n"
			"Map=中都.map\n"
			"Count=1\n"
			"\n"
			"[NPC000]\n"
			"Name=相对等级测试角色\n"
			"Kind=1\n"
			"NpcIni=z-杨影枫.ini\n"
			"Dir=0\n"
			"MapX=2\n"
			"MapY=2\n"
			"Action=0\n"
			"Relation=2\n"
			"Life=350\n"
			"LifeMax=350\n"
			"Thew=100\n"
			"ThewMax=100\n"
			"Mana=100\n"
			"ManaMax=100\n"
			"Attack=1\n"
			"Defend=1\n"
			"Evade=1\n"
			"Level=-1\n";
		const std::string objectFile =
			"[Head]\n"
			"Map=中都.map\n"
			"Count=0\n";
		const std::string scriptFile =
			"assign('EditorRunLuaApplied', 77)\n";
		if (!writeTextFile(
				rootedPath(selectedRoot, npcVirtualPath),
				npcFile) ||
			!writeTextFile(
				rootedPath(selectedRoot, objectVirtualPath),
				objectFile) ||
			!writeTextFile(
				rootedPath(selectedRoot, scriptVirtualPath),
				scriptFile))
		{
			return false;
		}

		for (const std::string& virtualPath :
			{
				mapVirtualPath,
				npcVirtualPath,
				objectVirtualPath,
				scriptVirtualPath
			})
		{
			if (!writeTextFile(
				rootedPath(earlierRoot, virtualPath),
				"corrupt earlier-root shadow"))
			{
				return false;
			}
		}
		return true;
	}

	bool restoreMap()
	{
		return copyFixtureFile(
			formalRoot / "map" /
				fs::u8path(
					u8"map_001_凌绝峰连接地图.map"),
			rootedPath(selectedRoot, mapVirtualPath));
	}

	bool restoreNpc()
	{
		return prepareValidSceneFiles();
	}

	bool restoreObject()
	{
		return writeTextFile(
			rootedPath(selectedRoot, objectVirtualPath),
			"[Head]\nMap=中都.map\nCount=0\n");
	}

	bool restoreScript()
	{
		return writeTextFile(
			rootedPath(selectedRoot, scriptVirtualPath),
			"assign('EditorRunLuaApplied', 77)\n");
	}

	bool restorePlayerTemplate()
	{
		return copyFixtureFile(
			formalPlayerTemplate,
			rootedPath(
				selectedRoot,
				PlayerTemplateVirtualPath));
	}

	std::vector<EditorRun::SearchRoot> searchRoots(
		bool includeFormalRoot = true) const
	{
		const RootedResourceReader::RootAnchorResult
			earlierAnchor =
				RootedResourceReader::openRootAnchor(
					earlierRoot);
		std::vector<EditorRun::SearchRoot> roots =
		{
			{
				EditorRun::SearchRootKind::Overlay,
				earlierRoot,
				"",
				earlierAnchor.anchor
			},
			{
				EditorRun::SearchRootKind::Active,
				selectedRoot,
				"runtime-selected",
				{}
			}
		};
		roots[1].traceContentRoot =
			EditorRun::TraceContentRootIdentity{
				EditorRun::RuntimeTraceRootKind::Active,
				0,
				std::string("runtime-selected")
			};
		if (includeFormalRoot)
		{
			roots.push_back(
				{
					EditorRun::SearchRootKind::DependencyId,
					formalRoot,
					"yycs",
					{}
				});
			roots.back().traceContentRoot =
				EditorRun::TraceContentRootIdentity{
					EditorRun::RuntimeTraceRootKind::
						DependencyId,
					1,
					std::string("yycs")
				};
		}
		return roots;
	}

	EditorRun::SceneTarget target() const
	{
		EditorRun::SceneTarget value;
		value.sceneId = "runtime-scene";
		value.sceneName = u8"中都运行时场景";
		value.mapPath = mapVirtualPath;
		value.npcPath = npcVirtualPath;
		value.objectPath = objectVirtualPath;
		value.entryScriptPath = scriptVirtualPath;
		value.playerX = 1;
		value.playerY = 1;
		value.integerVariables =
		{
			{ "EditorRunTargetApplied", 31 }
		};
		return value;
	}

	EditorRun::PreparedResourcePhase prepared(
		bool includeFormalRoot = true) const
	{
		const EditorRun::SceneTarget sceneTarget = target();
		EditorRun::PreparedResourcePhase value;
		value.orderedSearchRoots =
			searchRoots(includeFormalRoot);
		value.target.map =
		{
			EditorRun::TargetFileKind::Map,
			sceneTarget.mapPath,
			1
		};
		value.target.npc =
			EditorRun::ResolvedTargetFile
			{
				EditorRun::TargetFileKind::Npc,
				sceneTarget.npcPath,
				1
			};
		value.target.object =
			EditorRun::ResolvedTargetFile
			{
				EditorRun::TargetFileKind::Object,
				sceneTarget.objectPath,
				1
			};
		value.target.entryScript =
			EditorRun::ResolvedTargetFile
			{
				EditorRun::TargetFileKind::EntryScript,
				sceneTarget.entryScriptPath,
				1
			};
		return value;
	}

	EditorRun::PreparedResourcePhase preparedWithDeferredLookup(
		bool includeFormalRoot = true) const
	{
		EditorRun::PreparedResourcePhase value;
		value.orderedSearchRoots = searchRoots(includeFormalRoot);
		value.target = EditorRun::prepareSceneTarget(target());
		return value;
	}

	bool formalFilesUnchanged() const
	{
		std::uint64_t currentTemplateHash = 0;
		if (!fileHash(
			formalPlayerTemplate,
			currentTemplateHash) ||
			currentTemplateHash != formalTemplateHash)
		{
			return false;
		}
		if (!formalSaveExisted)
		{
			return !fs::exists(formalPlayerSave);
		}
		std::uint64_t currentSaveHash = 0;
		return fileHash(
				formalPlayerSave,
				currentSaveHash) &&
			currentSaveHash == formalSaveHash;
	}

	fs::path temporaryRoot;
	fs::path repositoryRoot;
	fs::path formalRoot;
	fs::path formalJxqy2Root;
	fs::path earlierRoot;
	fs::path selectedRoot;
	fs::path jxqy2FixtureRoot;
	fs::path overlayRoot;
	fs::path isolatedSaveRoot;
	fs::path applicationStateRoot;
	fs::path diagnosticsRoot;
	fs::path diagnosticsPath;
	fs::path logPath;
	fs::path jxqy2OverlayRoot;
	fs::path jxqy2IsolatedSaveRoot;
	fs::path jxqy2ApplicationStateRoot;
	fs::path jxqy2DiagnosticsRoot;
	fs::path jxqy2DiagnosticsPath;
	fs::path jxqy2LogPath;
	fs::path formalPlayerTemplate;
	fs::path formalPlayerSave;
	std::uint64_t formalTemplateHash = 0;
	std::uint64_t formalSaveHash = 0;
	bool formalSaveExisted = false;
	const std::string mapVirtualPath =
		u8"map/嵌套场景/中都.map";
	const std::string npcVirtualPath =
		u8"ini/save/嵌套场景/中都.npc";
	const std::string objectVirtualPath =
		u8"ini/save/嵌套场景/中都.obj";
	const std::string scriptVirtualPath =
		u8"script/map/嵌套场景/入口.lua";
};

bool runOrdinaryModeBoundaryTest(
	const RuntimeFixture& fixture)
{
	const fs::path isolatedPlayer =
		fixture.isolatedSaveRoot / IsolatedPlayerPath;
	bool ok = check(
		!fs::exists(isolatedPlayer),
		"ordinary mode starts without an isolated player baseline");
	{
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		std::string ordinaryString = "ordinary";
		gameManager.varList.set(
			"OrdinaryMixedCase",
			ordinaryString);
		gameManager.varList.setInteger(
			"OrdinaryInteger",
			17);
		gameManager.varList.setReal(
			"OrdinaryReal",
			1.25f);
		gameManager.varList.setBoolean(
			"OrdinaryBoolean",
			true);
		ok = check(
			!gameManager.isEditorRunMode() &&
				gameManager.player != nullptr &&
				gameManager.player->npcIni.empty() &&
				!gameManager.player->visible &&
				!gameManager.player->needEvents &&
				gameManager.varList.get(
					"ordinarymixedcase") ==
					ordinaryString &&
				gameManager.varList.getInteger(
					"OrdinaryInteger") == 17 &&
				gameManager.varList.getReal(
					"OrdinaryReal") == 1.25f &&
				gameManager.varList.getBoolean(
					"OrdinaryBoolean"),
			"ordinary no-writer mode keeps its existing player and variable setter semantics") &&
			ok;
	}
	ok = check(
		!fs::exists(isolatedPlayer),
		"ordinary constructor does not materialize editor-run player state") &&
		ok;
	return ok;
}

bool runProductionSuccessTest(
	RuntimeFixture& fixture)
{
	const EditorRun::SceneTarget target = fixture.target();
	const EditorRun::PreparedResourcePhase prepared =
		fixture.prepared();
	std::unique_ptr<char[]> playerResourceProbe;
	int playerResourceProbeLength = 0;
	bool ok = check(
		File::readFile(
			"ini/npcres/z-杨影枫.ini",
			playerResourceProbe,
			playerResourceProbeLength) &&
			playerResourceProbe != nullptr &&
			playerResourceProbeLength > 0,
		"installed editor-run routing exposes the formal player resource fixture");
	GameManager gameManager(target, prepared);
	const EditorRun::SceneApplicationResult result =
		EditorRunSceneRuntimeTestAccess::apply(gameManager);

	ok = check(
		result.succeeded(),
		"production editor-run applicator succeeds with valid pinned targets: " +
			result.diagnosticCode + " " + result.message);
	ok = check(
		gameManager.player != nullptr &&
			EditorRunSceneRuntimeTestAccess::
				characterIndex(gameManager) == 0 &&
			gameManager.player->npcIni == "z-杨影枫.ini" &&
			gameManager.player->level == 4 &&
			gameManager.player->getLifeMax() > 0 &&
			gameManager.player->getThewMax() > 0 &&
			gameManager.player->getManaMax() > 0 &&
			!gameManager.player->res.stand.imageFile.empty() &&
			gameManager.player->res.stand.imagePackage != nullptr,
		"player0 template initializes runnable attributes and stand resources: npcIni=" +
			(gameManager.player != nullptr
				? gameManager.player->npcIni
				: "<null>") +
			" level=" +
			std::to_string(
				gameManager.player != nullptr
					? gameManager.player->level
					: -1) +
			" life=" +
			std::to_string(
				gameManager.player != nullptr
					? gameManager.player->getLifeMax()
					: -1) +
			" thew=" +
			std::to_string(
				gameManager.player != nullptr
					? gameManager.player->getThewMax()
					: -1) +
			" mana=" +
			std::to_string(
				gameManager.player != nullptr
					? gameManager.player->getManaMax()
					: -1) +
			" stand=" +
			(gameManager.player != nullptr
				? gameManager.player->res.stand.imageFile
				: "<null>") +
			" package=" +
			std::to_string(
				gameManager.player != nullptr &&
				gameManager.player->res.stand.imagePackage != nullptr)) &&
		ok;
	ok = check(
		gameManager.player->visible &&
			!gameManager.player->needEvents,
		"successful editor placement restores visibility without changing the established event owner") &&
		ok;
	ok = check(
		gameManager.global.data.mapName ==
				u8"中都.map" &&
			gameManager.mapFolderName == u8"中都",
		"nested Chinese MAP keeps a legacy basename while exact loading uses the full virtual path") &&
		ok;
	ok = check(
		gameManager.player->getPosition() ==
			Point{ 1, 1 },
		"production position callback places the player in the exact map") &&
		ok;
	ok = check(
		gameManager.npcManager->npcList.size() == 1 &&
			gameManager.npcManager->npcList.front() != nullptr &&
			gameManager.npcManager->npcList.front()->attack == 122 &&
			gameManager.npcManager->npcList.front()->lifeMax == 420,
		"negative-relative-level NPC is constructed after the level-4 player baseline") &&
		ok;
	ok = check(
		gameManager.objectManager->objectList.empty(),
		"production exact OBJ callback accepts a valid empty object list") &&
		ok;
	ok = check(
		gameManager.varList.getInteger(
			"EditorRunTargetApplied") == 31 &&
			gameManager.varList.getInteger(
				"EditorRunLuaApplied") == 77,
		"target variables and exact entry script execute through production callbacks") &&
		ok;

	std::vector<std::uint8_t> formalTemplate;
	std::vector<std::uint8_t> isolatedTemplate;
	ok = check(
		readFileBytes(
			fixture.formalPlayerTemplate,
			formalTemplate) &&
			readFileBytes(
				fixture.isolatedSaveRoot /
					IsolatedPlayerPath,
				isolatedTemplate) &&
			formalTemplate == isolatedTemplate,
		"player baseline is materialized only in isolated save/game/player0.ini") &&
		ok;
	ok = check(
		fixture.formalFilesUnchanged(),
		"successful production application preserves formal template and save hashes") &&
		ok;
	return ok;
}

bool runDeferredMissingOptionalResourcesTest(
	RuntimeFixture& fixture)
{
	bool ok = true;
	std::error_code error;
	for (const std::string& virtualPath :
		{
			fixture.mapVirtualPath,
			fixture.npcVirtualPath,
			fixture.objectVirtualPath,
			fixture.scriptVirtualPath
		})
	{
		fs::remove(rootedPath(fixture.earlierRoot, virtualPath), error);
		error.clear();
	}
	for (const std::string& virtualPath :
		{
			fixture.mapVirtualPath,
			fixture.npcVirtualPath,
			fixture.objectVirtualPath,
			fixture.scriptVirtualPath
		})
	{
		fs::remove(rootedPath(fixture.selectedRoot, virtualPath), error);
		error.clear();
	}

	const EditorRun::SceneTarget target = fixture.target();
	const EditorRun::PreparedResourcePhase prepared =
		fixture.preparedWithDeferredLookup();
	GameManager gameManager(target, prepared);
	const EditorRun::SceneApplicationResult result =
		EditorRunSceneRuntimeTestAccess::apply(gameManager);
	ok = check(
		result.succeeded() &&
			!gameManager.inEvent &&
			gameManager.map->data == nullptr &&
			gameManager.global.data.mapName.empty() &&
			gameManager.global.data.npcName.empty() &&
			gameManager.global.data.objName.empty() &&
			gameManager.varList.getInteger(
				"EditorRunLuaApplied") == 0,
		"deferred runtime lookup treats every missing scene resource as empty without failing startup") &&
		ok;
	ok = check(
		fixture.prepareValidSceneFiles(),
		"deferred missing-resource fixture is restored") && ok;
	return ok;
}

bool runDeferredCorruptResourcesTest(
	RuntimeFixture& fixture)
{
	const EditorRun::SceneTarget target = fixture.target();
	const EditorRun::PreparedResourcePhase prepared =
		fixture.preparedWithDeferredLookup();
	GameManager gameManager(target, prepared);
	const EditorRun::SceneApplicationResult result =
		EditorRunSceneRuntimeTestAccess::apply(gameManager);
	return check(
		result.succeeded() &&
			!gameManager.inEvent &&
			gameManager.map->data == nullptr &&
			gameManager.global.data.mapName.empty() &&
			gameManager.global.data.npcName.empty() &&
			gameManager.global.data.objName.empty() &&
			gameManager.varList.getInteger(
				"EditorRunLuaApplied") == 0,
		"the first corrupt resource is not replaced from a lower root and only its scene feature becomes empty");
}

bool runDeferredMissingPlayerTest(
	RuntimeFixture& fixture)
{
	bool ok = true;
	std::error_code error;
	for (const std::string& virtualPath :
		{
			fixture.mapVirtualPath,
			fixture.npcVirtualPath,
			fixture.objectVirtualPath,
			fixture.scriptVirtualPath
		})
	{
		fs::remove(rootedPath(fixture.earlierRoot, virtualPath), error);
		error.clear();
	}
	for (const char* virtualPath :
		{
			PlayerTemplateVirtualPath,
			DefaultPlayerTemplateVirtualPath
		})
	{
		fs::remove(rootedPath(fixture.selectedRoot, virtualPath), error);
		error.clear();
	}

	const EditorRun::SceneTarget target = fixture.target();
	const EditorRun::PreparedResourcePhase prepared =
		fixture.preparedWithDeferredLookup(false);
	GameManager gameManager(target, prepared);
	const EditorRun::SceneApplicationResult result =
		EditorRunSceneRuntimeTestAccess::apply(gameManager);
	ok = check(
		result.succeeded() &&
			!gameManager.inEvent &&
			gameManager.map->data != nullptr &&
			gameManager.varList.getInteger(
				"EditorRunLuaApplied") == 77,
		"a missing player template skips only the player baseline while the map and entry script still run") &&
		ok;
	ok = check(
		fixture.prepareValidSceneFiles(),
		"deferred missing-player fixture is restored") && ok;
	return ok;
}

bool runFormalLogicalRootRebindTest(
	RuntimeFixture& fixture)
{
	const fs::path generationA =
		fixture.temporaryRoot / "formal-generation-a";
	const fs::path generationB =
		fixture.temporaryRoot / "formal-generation-b";
	const fs::path logicalRoot =
		fixture.temporaryRoot / "formal-current";
	std::error_code copyError;
	fs::copy(
		fixture.selectedRoot,
		generationA,
		fs::copy_options::recursive,
		copyError);
	if (!check(
			!copyError,
			"copy formal generation A: " +
				copyError.message()))
	{
		return false;
	}
	copyError.clear();
	fs::copy(
		fixture.selectedRoot,
		generationB,
		fs::copy_options::recursive,
		copyError);
	if (!check(
			!copyError,
			"copy formal generation B: " +
				copyError.message()))
	{
		return false;
	}

	const std::string generationBPlayerTemplate =
		"[Init]\n"
		"Name=Logical Generation B\n"
		"Kind=2\n"
		"NpcIni=z-杨影枫.ini\n"
		"Level=7\n"
		"Life=140\n"
		"LifeMax=140\n"
		"Thew=110\n"
		"ThewMax=110\n"
		"Mana=90\n"
		"ManaMax=90\n";
	bool ok = check(
		writeTextFile(
			rootedPath(
				generationA,
				fixture.mapVirtualPath),
			"invalid generation A map") &&
			writeTextFile(
				rootedPath(
					generationA,
					fixture.scriptVirtualPath),
				"assign('EditorRunLuaApplied', 41)\n") &&
			writeTextFile(
				rootedPath(
					generationB,
					fixture.scriptVirtualPath),
				"assign('EditorRunLuaApplied', 88)\n") &&
			writeTextFile(
				rootedPath(
					generationB,
				PlayerTemplateVirtualPath),
				generationBPlayerTemplate) &&
			writeTextFile(
				generationB /
					"config/current-generation.txt",
				"generation-b"),
		"write distinguishable MAP, Lua, player, and File generations");
	if (!ok)
	{
		return false;
	}

	std::error_code linkError;
	fs::create_directory_symlink(
		generationA,
		logicalRoot,
		linkError);
	ok = check(
		!linkError,
		"create required formal logical link to generation A: " +
			linkError.message()) &&
		ok;
	if (linkError)
	{
		return false;
	}

	EditorRun::PreparedResourcePhase prepared =
		fixture.prepared();
	prepared.orderedSearchRoots[1].root =
		logicalRoot;
	prepared.orderedSearchRoots[1].anchor = {};
	const fs::path rebindSessionRoot =
		fixture.temporaryRoot /
			"formal-rebind-session";
	std::error_code privateRootError;
	for (const fs::path& privateRoot :
		{
			rebindSessionRoot / "overlay",
			rebindSessionRoot / "save",
			rebindSessionRoot / "application-state",
			rebindSessionRoot / "diagnostics"
		})
	{
		fs::create_directories(
			privateRoot,
			privateRootError);
		if (privateRootError)
		{
			break;
		}
	}
	ok = check(
		!privateRootError,
		"create isolated private roots for the formal-link routing fixture") &&
		ok;
	ok = check(
		!privateRootError &&
		fixture.installFileLayout(
			logicalRoot,
			{ fixture.formalRoot },
			rebindSessionRoot / "overlay",
			rebindSessionRoot / "save",
			rebindSessionRoot / "application-state",
			rebindSessionRoot / "diagnostics",
			rebindSessionRoot / "diagnostics/events.jsonl",
			rebindSessionRoot / "diagnostics/game.log"),
		"install one editor-run route while the logical link names generation A") &&
		ok;

	std::error_code removeLinkError;
	fs::remove(logicalRoot, removeLinkError);
	linkError.clear();
	if (!removeLinkError)
	{
		fs::create_directory_symlink(
			generationB,
			logicalRoot,
			linkError);
	}
	ok = check(
		!removeLinkError && !linkError,
		"repoint the installed formal logical link from A to B without preparing or installing again") &&
		ok;

	std::unique_ptr<char[]> currentGeneration;
	int currentGenerationLength = 0;
	const bool fileReadCurrentGeneration =
		!removeLinkError &&
		!linkError &&
		File::readFile(
			"config/current-generation.txt",
			currentGeneration,
			currentGenerationLength) &&
		currentGeneration != nullptr &&
		std::string(
			currentGeneration.get(),
			static_cast<std::size_t>(
				currentGenerationLength)) ==
			"generation-b";

	EditorRun::SceneApplicationResult applied;
	std::string playerName;
	int playerLevel = -1;
	int luaValue = -1;
	if (!removeLinkError && !linkError)
	{
		GameManager gameManager(
			fixture.target(), prepared);
		applied =
			EditorRunSceneRuntimeTestAccess::apply(
				gameManager);
		if (gameManager.player != nullptr)
		{
			playerName = gameManager.player->npcName;
			playerLevel = gameManager.player->level;
		}
		luaValue = gameManager.varList.getInteger(
			"EditorRunLuaApplied");
	}
	ok = check(
		fileReadCurrentGeneration &&
			applied.succeeded() &&
			playerName == "Logical Generation B" &&
			playerLevel == 7 &&
			luaValue == 88,
		"one prepared and installed run reads generation B through MAP, Lua, player, and File after the formal link is repointed: " +
			applied.diagnosticCode + " " +
			applied.message) &&
		ok;

	std::error_code cleanupError;
	fs::remove(logicalRoot, cleanupError);
	ok = check(
		!cleanupError,
		"remove only the formal logical link") &&
		ok;
	for (const fs::path& exactOutput :
		{
			fixture.diagnosticsPath,
			fixture.logPath,
			fixture.diagnosticsRoot / "runtime-trace.jsonl"
		})
	{
		cleanupError.clear();
		fs::remove(exactOutput, cleanupError);
		if (cleanupError)
		{
			break;
		}
	}
	ok = check(
		!cleanupError,
		"remove closed private diagnostic outputs before reinstalling the test layout") &&
		ok;
	ok = check(
		!cleanupError &&
			fixture.installYycsFileLayout(),
		"restore the original private editor-run layout after the formal-link fixture") &&
		ok;
	return ok;
}

EditorRun::SceneApplicationResult applyFresh(
	const EditorRun::SceneTarget& target,
	const EditorRun::PreparedResourcePhase& prepared);

bool runJxqy2CanonicalPlayerTemplateTest(
	RuntimeFixture& fixture)
{
	const fs::path trackedPlayerTemplate =
		fixture.repositoryRoot / "src" / "tests" /
			"fixtures" /
			"editor_run_jxqy2_initial_player.ini";
	const fs::path jxqy2Root =
		fixture.formalJxqy2Root;
	const fs::path formalPlayerTemplate =
		rootedPath(
			jxqy2Root,
			DefaultPlayerTemplateVirtualPath);
	std::uint64_t formalTemplateHash = 0;
	const bool formalTemplateExists =
		fileHash(
			formalPlayerTemplate,
			formalTemplateHash);
	std::vector<std::uint8_t> trackedTemplate;
	if (!check(
		readFileBytes(
			trackedPlayerTemplate,
			trackedTemplate),
		"tracked JXQY2 player template fixture is available"))
	{
		return false;
	}

	if (!check(
		fixture.installJxqy2FileLayout(),
		"JXQY2 editor-run file layout is installed"))
	{
		return false;
	}

	EditorRun::PreparedResourcePhase prepared;
	prepared.orderedSearchRoots.push_back(
		{
			EditorRun::SearchRootKind::Active,
			fixture.jxqy2FixtureRoot,
			"JXQY2",
			{}
		});

	std::string failureMessage;
	std::string templateVirtualPath;
	std::string isolatedPlayerVirtualPath;
	int characterIndex = 0;
	bool initialized = false;
	bool runnablePlayer = false;
	{
		GameManager gameManager(
			fixture.target(),
			prepared);
		initialized =
			EditorRunSceneRuntimeTestAccess::
				initializePlayerBaseline(
					gameManager,
					failureMessage,
					templateVirtualPath,
					isolatedPlayerVirtualPath,
					characterIndex);
		runnablePlayer =
			gameManager.player != nullptr &&
			!gameManager.player->npcIni.empty() &&
			gameManager.player->res.stand.imagePackage !=
				nullptr &&
			EditorRunSceneRuntimeTestAccess::
				characterIndex(gameManager) == -1;
	}

	bool ok = check(
		initialized &&
			failureMessage.empty() &&
			templateVirtualPath ==
				DefaultPlayerTemplateVirtualPath &&
			isolatedPlayerVirtualPath ==
				"save/game/player.ini" &&
			characterIndex == -1 &&
			runnablePlayer,
		"JXQY2 ini/save template materializes the unnumbered player with character index -1");

	std::vector<std::uint8_t> fixtureTemplate;
	std::vector<std::uint8_t> isolatedTemplate;
	ok = check(
		readFileBytes(
				rootedPath(
					fixture.jxqy2FixtureRoot,
					DefaultPlayerTemplateVirtualPath),
				fixtureTemplate) &&
			readFileBytes(
				fixture.jxqy2IsolatedSaveRoot /
					IsolatedDefaultPlayerPath,
				isolatedTemplate) &&
			trackedTemplate == fixtureTemplate &&
			fixtureTemplate == isolatedTemplate,
		"JXQY2 resource-template bytes are copied only to isolated save/game/player.ini") &&
		ok;

	ok = check(
		!fs::exists(
			fixture.jxqy2FixtureRoot /
				"save" / "game" / "player.ini") &&
			!fs::exists(
				fixture.jxqy2FixtureRoot /
					"save" / "game" /
					"player0.ini"),
		"JXQY2 prepared input root remains read-only") &&
		ok;

	if (formalTemplateExists)
	{
		std::vector<std::uint8_t> currentFormalTemplate;
		std::uint64_t currentFormalHash = 0;
		ok = check(
			readFileBytes(
				formalPlayerTemplate,
				currentFormalTemplate) &&
				currentFormalTemplate ==
					trackedTemplate &&
				fileHash(
					formalPlayerTemplate,
					currentFormalHash) &&
				currentFormalHash ==
					formalTemplateHash,
			"local JXQY2 formal ini/save player matches the tracked fixture and remains unchanged") &&
			ok;
	}
	return ok;
}

std::size_t traceSubstringCount(
	const std::string& trace,
	const std::string& needle)
{
	std::size_t count = 0;
	for (std::size_t offset = 0;
		(offset = trace.find(needle, offset)) !=
			std::string::npos;
		offset += needle.size())
	{
		++count;
	}
	return count;
}

bool traceLineContains(
	const std::string& trace,
	const std::string& first,
	const std::string& second)
{
	std::size_t lineStart = 0;
	while (lineStart < trace.size())
	{
		const std::size_t lineEnd =
			trace.find('\n', lineStart);
		const std::size_t length =
			lineEnd == std::string::npos
				? trace.size() - lineStart
				: lineEnd - lineStart;
		const std::string_view line(
			trace.data() + lineStart,
			length);
		if (line.find(first) != std::string_view::npos &&
			line.find(second) != std::string_view::npos)
		{
			return true;
		}
		if (lineEnd == std::string::npos)
		{
			break;
		}
		lineStart = lineEnd + 1;
	}
	return false;
}

bool runProductionRuntimeTraceTest(
	RuntimeFixture& fixture)
{
	bool ok = true;
	const std::string childVirtualPath =
		u8"script/map/中都/upperchild.lua";
	const std::string parallelChildVirtualPath =
		u8"script/map/中都/upperparallel.lua";
	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				childVirtualPath),
			"printf('lowercase child')\n") &&
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				parallelChildVirtualPath),
			"printf('lowercase parallel child')\n") &&
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.scriptVirtualPath),
			"runscript('upperchild.lua')\n"
			"runparallelscript('upperparallel.lua', 0)\n"
			"assign('TraceEntryValue', 77)\n"),
		"runtime trace lowercase child and entry fixtures are written") &&
		ok;

	std::mutex traceMutex;
	std::string trace;
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			"123e4567-e89b-12d3-a456-426614174001",
			[&traceMutex, &trace](
				std::string_view batch)
			{
				std::lock_guard<std::mutex> lock(
					traceMutex);
				trace.append(
					batch.data(),
					batch.size());
				return true;
			});
	ok = check(
		writer != nullptr && writer->valid(),
		"production runtime trace writer starts") &&
		ok;
	if (writer == nullptr || !writer->valid())
	{
		fixture.restoreScript();
		return false;
	}

	const EditorRun::SceneTarget target =
		fixture.target();
	const EditorRun::PreparedResourcePhase prepared =
		fixture.prepared();
	EditorRun::SceneApplicationResult result;
	bool storedVariableSemanticsPreserved = false;
	bool missingOverlayOriginStillRuns = false;
	bool variableLoadContractPreserved = false;
	{
		GameManager gameManager(
			target,
			prepared,
			writer.get());
		result =
			EditorRunSceneRuntimeTestAccess::apply(
				gameManager);
		gameManager.runScriptTaskList();
		ok = check(
			writeTextFile(
				rootedPath(
					fixture.earlierRoot,
					fixture.scriptVirtualPath),
				"assign('MissingOverlayOriginApplied', 91)\n"),
			"overlay script without trace provenance is written") &&
			ok;
		const ExactScriptExecutionResult
			overlayScriptResult =
				EditorRunSceneRuntimeTestAccess::
					runExactScript(
						gameManager,
						prepared.orderedSearchRoots.front(),
						fixture.scriptVirtualPath);
		missingOverlayOriginStillRuns =
			overlayScriptResult.succeeded() &&
			gameManager.varList.getInteger(
				"MissingOverlayOriginApplied") == 91;

		std::string emptyValue;
		gameManager.varList.set(
			"MixedCaseEmpty",
			emptyValue);
		gameManager.varList.setInteger(
			"MiXeDInteger",
			7);
		storedVariableSemanticsPreserved =
			gameManager.varList.getInteger(
				"MiXeDInteger") == 7 &&
			gameManager.varList.getInteger(
				"mixedinteger") == 7;
		gameManager.varList.setReal(
			"FineReal",
			1.234567f);
		gameManager.varList.setReal(
			"FineReal",
			1.234568f);
		gameManager.varList.setReal(
			"LargeReal",
			1.0e20f);
		gameManager.varList.setReal(
			"SmallReal",
			1.0e-5f);
		gameManager.varList.setReal(
			"NegativeZero",
			-0.0f);
		gameManager.varList.setReal(
			"NonFiniteReal",
			std::numeric_limits<float>::quiet_NaN());
		gameManager.varList.setReal(
			"NonFiniteReal",
			std::numeric_limits<float>::infinity());
		gameManager.varList.setBoolean(
			"MixedBoolean",
			true);
		gameManager.varList.clearExcept(
			{"MiXeDInteger"});

		ok = check(
			writeTextFile(
				fixture.isolatedSaveRoot /
					"game" / "variable.ini",
				"[Variable]\n"
				"LoadedEmpty=\n"
				"LoadedMixed=9\n"),
			"runtime trace variable load fixture is written") &&
			ok;
		const bool validVariablesLoaded =
			gameManager.varList.load();
		const bool emptyVariablesWritten =
			writeTextFile(
				fixture.isolatedSaveRoot /
					"game" / "variable.ini",
				"");
		const bool emptyVariablesLoaded =
			emptyVariablesWritten &&
			gameManager.varList.load() &&
			gameManager.varList.getInteger("LoadedMixed") == 0;
		gameManager.varList.setInteger("RetainedAfterFailure", 37);
		const bool malformedVariablesWritten =
			writeTextFile(
				fixture.isolatedSaveRoot /
					"game" / "variable.ini",
				"[Variable\nBroken=1\n");
		std::string variableFailureReason;
		variableLoadContractPreserved =
			validVariablesLoaded &&
			emptyVariablesLoaded &&
			malformedVariablesWritten &&
			!gameManager.varList.load(&variableFailureReason) &&
			gameManager.varList.getInteger("RetainedAfterFailure") == 37 &&
			variableFailureReason.find("variable.ini") !=
				std::string::npos;
	}
	ok = check(
		result.succeeded(),
		"production traced scene application succeeds") &&
		ok;
	ok = check(
		missingOverlayOriginStillRuns,
		"missing overlay trace provenance does not block script execution") &&
		ok;
	ok = check(
		storedVariableSemanticsPreserved,
		"trace observation preserves existing case-insensitive variable storage semantics") &&
		ok;
	ok = check(
		variableLoadContractPreserved,
		"variable loading accepts an empty state and rejects malformed data without mutation") &&
		ok;
	ok = check(
		writer->finish(
			EditorRun::RuntimeTraceSessionFinishStatus::
				Completed),
		"production runtime trace writer finishes") &&
		ok;

	{
		std::lock_guard<std::mutex> lock(traceMutex);
		ok = check(
			trace.find(
				u8"\"target\":\"嵌套场景/中都.map\"") !=
				std::string::npos &&
			trace.find("\"target\":\"map/") ==
				std::string::npos,
			"successful map.change stores the strict API-facing target without a map prefix") &&
			ok;
		ok = check(
			trace.find(
				u8"\"virtualPath\":\"script/map/中都/upperchild.lua\"") !=
				std::string::npos &&
			trace.find(
				u8"\"virtualPath\":\"script/map/中都/upperparallel.lua\"") !=
				std::string::npos &&
			traceLineContains(
				trace,
				u8"\"virtualPath\":\"script/map/中都/upperparallel.lua\"",
				"\"parentExecutionId\":1"),
			"serial and queued editor-run scripts retain canonical lowercase paths and captured parent execution") &&
			ok;
		ok = check(
			traceLineContains(
				trace,
				u8"\"virtualPath\":\"script/map/嵌套场景/入口.lua\"",
				"\"rootOrdinal\":9007199254740991") &&
			traceLineContains(
				trace,
				u8"\"virtualPath\":\"script/map/嵌套场景/入口.lua\"",
				"\"sourceLayer\":\"overlay\""),
			"overlay script without provenance remains traceable as an unknown logical root") &&
			ok;
		ok = check(
			trace.find("\"apiName\":\"runscript\"") !=
				std::string::npos &&
			trace.find("\"apiName\":\"assign\"") !=
				std::string::npos &&
			trace.find("\"apiName\":\"printf\"") !=
				std::string::npos,
			"production API instrumentation records the precise lowercase registered alias") &&
			ok;
		ok = check(
			traceSubstringCount(
				trace,
				"\"variableName\":\"finereal\"") == 3 &&
			trace.find(
				"\"variableName\":\"largereal\"") !=
				std::string::npos &&
			trace.find(
				"\"variableName\":\"smallreal\"") !=
				std::string::npos &&
			trace.find(
				"\"variableName\":\"negativezero\"") !=
				std::string::npos &&
			trace.find("e+") == std::string::npos &&
			trace.find("\"afterValue\":\"-0\"") ==
				std::string::npos &&
			!traceLineContains(
				trace,
				"\"variableName\":\"nonfinitereal\"",
				"\"valueType\":\"real\""),
			"real changes use persisted round-trip values with canonical exponent and zero spelling") &&
			ok;
		ok = check(
			traceSubstringCount(
				trace,
				"\"variableName\":\"mixedcaseempty\","
				"\"valueType\":\"string\","
				"\"beforeValue\":\"\","
				"\"afterValue\":\"\"") == 2 &&
			trace.find(
				"\"variableName\":\"loadedempty\","
				"\"valueType\":\"string\","
				"\"beforeValue\":\"\","
				"\"afterValue\":\"\"") !=
				std::string::npos,
			"missing-versus-empty setter, clear, and load changes remain observable as equal-valued events") &&
			ok;
		ok = check(
			writer->error() ==
				EditorRun::RuntimeTraceWriterError::None,
			"all production instrumentation events satisfy the runtime trace serializer") &&
			ok;
	}

	ok = check(
		fixture.restoreScript(),
		"runtime trace entry fixture is restored") &&
		ok;
	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.mapVirtualPath),
			"corrupt map"),
		"runtime trace corrupt map fixture is written") &&
		ok;

	std::mutex failureTraceMutex;
	std::string failureTrace;
	std::unique_ptr<EditorRun::RuntimeTraceWriter>
		failureWriter =
			EditorRun::RuntimeTraceWriter::create(
				"123e4567-e89b-12d3-a456-426614174002",
				[&failureTraceMutex, &failureTrace](
					std::string_view batch)
				{
					std::lock_guard<std::mutex> lock(
						failureTraceMutex);
					failureTrace.append(
						batch.data(),
						batch.size());
					return true;
				});
	if (failureWriter == nullptr ||
		!failureWriter->valid())
	{
		fixture.restoreMap();
		return check(
			false,
			"map failure trace writer starts") &&
			ok;
	}
	{
		GameManager gameManager(
			target,
			prepared,
			failureWriter.get());
		result =
			EditorRunSceneRuntimeTestAccess::apply(
				gameManager);
	}
	ok = check(
		result.error ==
			EditorRun::SceneApplicationError::
				MapLoadFailed,
		"traced corrupt map fails scene application") &&
		ok;
	ok = check(
		failureWriter->finish(
			EditorRun::RuntimeTraceSessionFinishStatus::
				SceneFailure),
		"map failure runtime trace writer finishes") &&
		ok;
	{
		std::lock_guard<std::mutex> lock(
			failureTraceMutex);
		ok = check(
			failureTrace.find(
				"\"eventType\":\"map.change\"") ==
				std::string::npos,
			"failed map load emits no map.change event") &&
			ok;
	}
	ok = check(
		fixture.restoreMap(),
		"runtime trace map fixture is restored") &&
		ok;
	return ok;
}

EditorRun::SceneApplicationResult applyFresh(
	const EditorRun::SceneTarget& target,
	const EditorRun::PreparedResourcePhase& prepared)
{
	GameManager gameManager(target, prepared);
	return EditorRunSceneRuntimeTestAccess::apply(gameManager);
}

bool checkFailure(
	const EditorRun::SceneApplicationResult& result,
	EditorRun::SceneApplicationError expectedError,
	const std::string& expectedCode,
	const std::string& label)
{
	return check(
			result.error == expectedError &&
				result.diagnosticCode == expectedCode,
			label);
}

bool runProductionFailureTests(
	RuntimeFixture& fixture)
{
	bool ok = true;
	const EditorRun::SceneTarget target = fixture.target();

	EditorRun::PreparedResourcePhase prepared =
		fixture.prepared();
	prepared.target.map.searchRootIndex =
		prepared.orderedSearchRoots.size();
	ok = checkFailure(
		applyFresh(target, prepared),
		EditorRun::SceneApplicationError::MapLoadFailed,
		"editor_run.target.map_load_failed",
		"production applicator rejects an out-of-range prepared root index") &&
		ok;

	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.mapVirtualPath),
			"corrupt map"),
		"corrupt MAP fixture is written") &&
		ok;
	ok = checkFailure(
		applyFresh(target, fixture.prepared()),
		EditorRun::SceneApplicationError::MapLoadFailed,
		"editor_run.target.map_load_failed",
		"production exact MAP parser rejects corrupt bytes") &&
		ok;
	ok = check(
		fixture.restoreMap(),
		"valid MAP fixture is restored") &&
		ok;

	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.npcVirtualPath),
			"[Head]\nCount=1\n"),
		"corrupt NPC fixture is written") &&
		ok;
	ok = checkFailure(
		applyFresh(target, fixture.prepared()),
		EditorRun::SceneApplicationError::NpcLoadFailed,
		"editor_run.target.npc_load_failed",
		"production exact NPC parser rejects a missing section") &&
		ok;
	ok = check(
		fixture.restoreNpc(),
		"valid NPC fixture is restored") &&
		ok;

	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.objectVirtualPath),
			"[Head]\nCount=1\n"),
		"corrupt OBJ fixture is written") &&
		ok;
	ok = checkFailure(
		applyFresh(target, fixture.prepared()),
		EditorRun::SceneApplicationError::ObjectLoadFailed,
		"editor_run.target.object_load_failed",
		"production exact OBJ parser rejects a missing section") &&
		ok;
	ok = check(
		fixture.restoreObject(),
		"valid OBJ fixture is restored") &&
		ok;

	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.scriptVirtualPath),
			u8"local text = '中文'; local broken = )\n"),
		"Lua load-error fixture is written") &&
		ok;
	EditorRun::SceneApplicationResult result =
		applyFresh(target, fixture.prepared());
	ok = check(
		result.error ==
				EditorRun::SceneApplicationError::
					EntryScriptLoadFailed &&
			result.diagnosticCode ==
				"editor_run.target.script_load_failed" &&
			result.virtualPath ==
				fixture.scriptVirtualPath &&
			result.line == 1 &&
			result.column == 35 &&
			result.message ==
				"unexpected symbol near ')'",
		"production Lua load error preserves a Unicode code-point column and canonical message") &&
		ok;

	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				fixture.scriptVirtualPath),
			"local value = 1\n"
			"error('12: runtime marker')\n"),
		"Lua runtime-error fixture is written") &&
		ok;
	result = applyFresh(target, fixture.prepared());
	ok = check(
		result.error ==
				EditorRun::SceneApplicationError::
					EntryScriptRuntimeFailed &&
			result.diagnosticCode ==
				"editor_run.target.script_runtime_failed" &&
			result.virtualPath ==
			fixture.scriptVirtualPath &&
			result.line == 2 &&
			result.column == 0 &&
			result.message == "12: runtime marker",
		"production Lua runtime message numeric prefix is not misread as a column") &&
		ok;
	ok = check(
		fixture.restoreScript(),
		"valid Lua fixture is restored") &&
		ok;

	std::error_code removeError;
	fs::remove(
		rootedPath(
			fixture.selectedRoot,
			PlayerTemplateVirtualPath),
		removeError);
	fs::remove(
		rootedPath(
			fixture.selectedRoot,
			DefaultPlayerTemplateVirtualPath),
		removeError);
	ok = check(
		fs::is_regular_file(
			fixture.isolatedSaveRoot /
				IsolatedPlayerPath),
		"stale isolated player output exists before the missing-template retry") &&
		ok;
	result = applyFresh(
		target,
		fixture.prepared(false));
	ok = checkFailure(
		result,
		EditorRun::SceneApplicationError::
			PlayerInitializationFailed,
		"editor_run.target.player_initialization_failed",
		"production application fails closed when no prepared root contains a supported player baseline") &&
		ok;

	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				PlayerTemplateVirtualPath),
			"[Init\ninvalid"),
		"malformed player template fixture is written") &&
		ok;
	result = applyFresh(target, fixture.prepared());
	ok = check(
		checkFailure(
			result,
			EditorRun::SceneApplicationError::
				PlayerInitializationFailed,
			"editor_run.target.player_initialization_failed",
			"first existing malformed player template fails without falling back") &&
			result.virtualPath ==
				PlayerTemplateVirtualPath,
		"malformed primary player template reports the selected primary path") &&
		ok;
	fs::remove(
		rootedPath(
			fixture.selectedRoot,
			PlayerTemplateVirtualPath),
		removeError);

	const std::string unrunnablePlayer =
		"[Init]\n"
		"Name=EditorRunPlayer\n"
		"Kind=2\n"
		"NpcIni=missing-editor-run-player.ini\n"
		"Level=4\n"
		"Life=100\n"
		"LifeMax=100\n"
		"Thew=100\n"
		"ThewMax=100\n"
		"Mana=100\n"
		"ManaMax=100\n";
	ok = check(
		writeTextFile(
			rootedPath(
				fixture.selectedRoot,
				PlayerTemplateVirtualPath),
			unrunnablePlayer),
		"unrunnable player template fixture is written") &&
		ok;
	result = applyFresh(target, fixture.prepared());
	ok = checkFailure(
		result,
		EditorRun::SceneApplicationError::
			PlayerInitializationFailed,
		"editor_run.target.player_initialization_failed",
		"post-load player resource validation fails closed") &&
		ok;

	ok = check(
		fixture.restorePlayerTemplate(),
		"valid player template is restored") &&
		ok;
	ok = check(
		fixture.formalFilesUnchanged(),
		"all failure paths preserve formal template and save hashes") &&
		ok;
	return ok;
}
}

bool runEditorRunSceneRuntimeTests()
{
	RuntimeFixture fixture;
	if (!check(
		fixture.initialize(),
		"editor-run runtime integration fixture initializes"))
	{
		return false;
	}

	bool ok = true;
	ok = runOrdinaryModeBoundaryTest(fixture) && ok;
	ok = runProductionSuccessTest(fixture) && ok;
	ok = runDeferredMissingOptionalResourcesTest(fixture) && ok;
	ok = runDeferredCorruptResourcesTest(fixture) && ok;
	ok = runDeferredMissingPlayerTest(fixture) && ok;
	ok = runFormalLogicalRootRebindTest(
		fixture) && ok;
	ok = runProductionRuntimeTraceTest(fixture) &&
		ok;
	ok = runProductionFailureTests(fixture) && ok;
	ok = runJxqy2CanonicalPlayerTemplateTest(fixture) && ok;
	return ok;
}
