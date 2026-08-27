#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <functional>
#include "../../Types/CommonTypes.h"
#include "../../File/RootedResourceReader.h"
#include "../Loading/ExclusiveLoadingRunner.h"

class GameManager;
class Engine;
struct ExactScriptExecutionResult;
struct SaveGenerationLimits;
struct SaveGenerationPreflightPolicy;
namespace EditorRun
{
struct SearchRoot;
}
namespace NewYearPeriod
{
struct LocalDate;
}

class ScriptAPI
{
public:
	ScriptAPI(GameManager* _gameManager);
	~ScriptAPI();

	void setGameManager(GameManager* _gameManager) { gameManager = _gameManager; }

	int getVar(const std::string& varName);
	void assign(const std::string& varName, int value);
	void add(const std::string& varName, int value);

	void talk(const std::string& part);
	void talk(int fromIdx, int toIdx);
	void say(const std::string& str, int index = -1);

	void fadeInEx();
	void fadeIn();
	void fadeOut();
	void setFadeLum(int lum);
	void setMainLum(int lum);
	void playMusic(const std::string& fileName);
	void playRandomMusic(const std::string& fileNameA, const std::string& fileNameB, const std::string& fileNameC);
	void stopMusic();
	void playSound(const std::string& fileName);
	void stopSound();
	void runScript(const std::string& fileName);
	void runScript(const std::string& fileName, const std::string& mapName);
	ExactScriptExecutionResult runScriptFromExactRoot(
		const EditorRun::SearchRoot& root,
		const std::string& virtualPath);
	ExactScriptExecutionResult runScriptFromEditorRunRoots(
		const std::vector<EditorRun::SearchRoot>& roots,
		const std::string& virtualPath);
	void runParallelScript(const std::string& fileName, int delayMilliseconds);
	void moveScreen(int direction, int distance, int speed);
	void moveScreenForFrameCount(int direction, int frameCount, int speed);
	void sleep(int time);
	void playMovie(const std::string& fileName);
	void stopMovie();

	void loadMapAsync(const std::string& fileName);
	void freeMap();
	void playerChange(int index);
	void mergeNpc(const std::string& fileName);
	bool loadMap(const std::string& fileName, bool resetCamera = true);
	bool loadMapFromExactRoot(
		const EditorRun::SearchRoot& root,
		const std::string& virtualPath,
		bool resetCamera = true);
	bool loadMapFromEditorRunRoots(
		const std::vector<EditorRun::SearchRoot>& roots,
		const std::string& virtualPath,
		bool resetCamera = true);
	bool loadGameAsync(int index);
	bool loadGame(int index);
	bool setEditorRunPlayerPositionAndCamera(
		std::int32_t x,
		std::int32_t y);
	void setMapPos(int x, int y);
	void setMapTrap(int idx, const std::string& trapFile);
	void saveMapTrap();
	void setMapTime(int time);
	void changeASFColor(uint8_t r, uint8_t g, uint8_t b);
	void changeMapColor(uint8_t r, uint8_t g, uint8_t b);

	void loadObjectAsync(const std::string& fileName);
	bool loadObject(const std::string& fileName);
	bool loadObjectFromExactRoot(
		const EditorRun::SearchRoot& root,
		const std::string& virtualPath);
	bool loadObjectFromEditorRunRoots(
		const std::vector<EditorRun::SearchRoot>& roots,
		const std::string& virtualPath);
	void saveObject(const std::string& fileName = "");
	void addObject(const std::string& iniName, int x, int y, int dir);
	void deleteObject(const std::string& name);
	void setObjectPosition(const std::string& name, int x, int y);
	void setObjectOffset(const std::string& name, int x, int y);
	void setObjectKind(const std::string& name, int kind);
	void setObjectScript(const std::string& name, const std::string& scriptFile);
	void setObjectScript(const std::string& name, const std::string& scriptFile, const std::string& objFileName);
	void runObjectScript(const std::string& name, bool useRightScript = false, bool allowPrimaryFallback = true);
	bool interactNearestObject(bool useRightScript = false, bool running = false, int radius = 2);
	void clearBody();
	void openBox(const std::string& name = "");
	void closeBox(const std::string& name = "");
	void getObjectState(const std::string& name, const std::string& stateName, const std::string& varName);

	void loadNPCAsync(const std::string& fileName);
	bool loadNPC(const std::string& fileName);
	bool loadNPCFromExactRoot(
		const EditorRun::SearchRoot& root,
		const std::string& virtualPath);
	bool loadNPCFromEditorRunRoots(
		const std::vector<EditorRun::SearchRoot>& roots,
		const std::string& virtualPath);
	void loadOneNpc(const std::vector<std::string>& fileNames);
	void saveNPC(const std::string& fileName = "");
	void addNPC(const std::string& iniName, int x, int y, int dir);
	void deleteNPC(const std::string& name);
	void setNPCRes(const std::string& name, const std::string& resName);
	void setNPCScript(const std::string& name, const std::string& scriptName);
	void setNPCScript(const std::string& name, const std::string& scriptName, const std::string& npcFileName);
	void setNPCDeathScript(const std::string& name, const std::string& scriptName);
	void setNPCDeathScript(const std::string& name, const std::string& scriptName, const std::string& npcFileName);
	void setAllNPCScript(const std::string& name, const std::string& scriptName);
	void setAllNPCDeathScript(const std::string& name, const std::string& scriptName);
	bool interactNearestNPC(bool useRightScript = false, bool running = false, int radius = 2);
	void goTo(const std::string& name, int x, int y);
	void goToEx(const std::string& name, int x, int y);
	void goToDir(const std::string& name, int dir, int distance);
	void setNpcDestination(const std::string& name, int x, int y);
	void followNPC(const std::string& follower, const std::string& leader);
	void followPlayer(const std::string& follower);
	void enableNPCAI();
	void disableNPCAI();
	void setNpcAIEnabled(const std::string& name, bool enabled);
	void enablePartnerCombat();
	void disablePartnerCombat();
	void attackTo(const std::string& name, int x, int y);
	void npcUseMagic(const std::string& name, const std::string& magicFileName, int x, int y, int level);
	void setNPCPosition(const std::string& name, int x, int y);
	void setNPCDir(const std::string& name, int dir);
	void setNPCKind(const std::string& name, int kind);
	void setNPCLevel(const std::string& name, int level);
	void setNPCAction(const std::string& name, int action, int x = 0, int y = 0);
	void setNPCRelation(const std::string& name, int relation);
	void setNPCActionType(const std::string& name, int strollIntent);
	void setNPCActionFile(const std::string& name, int action, const std::string& fileName);
	void npcSpecialAction(const std::string& name, const std::string& fileName);
	void npcSpecialActionNonBlocking(const std::string& name, const std::string& fileName);
	void npcSpecialActionEx(const std::string& name, const std::string& fileName);
	void changeLife(const std::string& name, int value);
	void changeMana(const std::string& name, int value);
	void changeThew(const std::string& name, int value);
	void getNpcState(const std::string& name, const std::string& stateName, const std::string& varName);
	void addNpcProperty(const std::string& name, const std::string& propertyName, int value);
	void addKindValue(const std::string& name, int value);
	void setMapNpcAttr(const std::string& name, const std::string& attributes, const std::string& npcFileName);
	void setNpcTalkContent(const std::string& name, const std::string& content);
	void setNpcTalkContent(const std::string& name, const std::string& content, const std::string& npcFileName);
	void talkSelfTip(const std::string& name, const std::string& message, const std::string& appendText = "");
	void setAllNpcIsEnemy();
	void setDropIni(const std::string& name, const std::string& fileName);
	void enableDrop();
	void disableDrop();
	void changeFlyIni(const std::string& name, const std::string& magicName);
	void changeFlyIni2(const std::string& name, const std::string& magicName);
	void addFlyInis(const std::string& name, const std::string& magicName, int distance);
	void addNpcMagic(const std::string& name, const std::string& magicName);
	void setKeepAttack(const std::string& name, int x, int y);
	void showSignalTip(const std::string& name, int signalIndex, const std::string& signalType);
	void setSignalTipHidden(const std::string& name);

	void loadPlayer(int index);
	void loadPlayer(const std::string& snapshotKey);
	void savePlayer(int index);
	void savePlayer(const std::string& snapshotKey);
	void setPlayerPosition(int x, int y);
	void setPlayerPosition(const std::string& name, int x, int y);
	void setPlayerDir(int dir);
	void setPlayerScn(bool snapCamera = true);
	void setPlayerLum(unsigned char lum);
	void setLevelFile(const std::string& fileName);
	void setMagicLevel(const std::string& magicName, int level);
	int getPlayerMagicLevel(const std::string& magicName);
	void getPlayerMagicLevel(const std::string& magicName, const std::string& varName);
	void getMagicState(const std::string& magicName, const std::string& stateName, const std::string& varName, int level = 0);
	void getEffectState(const std::string& magicName, const std::string& stateName, const std::string& varName);
	void getMapState(int x, int y, const std::string& stateName, const std::string& varName);
	void getLeechcraftDifference(const std::string& npcName, const std::string& varName);
	void moveMagic(const std::string& magicName, int position);
	void setPlayerLevel(int level);
	void setPlayerState(int state);
	void enableRun();
	void disableRun();
	void enableJump();
	void disableJump();
	void enableFight();
	void disableFight();
	void playerGoto(int x, int y);
	void playerGotoEx(int x, int y);
	void playerRunTo(int x, int y);
	void playerRunToEx(int x, int y);
	void playerJumpTo(int x, int y);
	void playerGotoDir(int dir, int distance);
	void setWalkIsRun(int value);
	void addMoveSpeedPercent(int percent);
	void useMagic(const std::string& magicName, int x, int y, bool hasDestination);
	void petrifyMillisecond(int milliseconds);
	void poisonMillisecond(int milliseconds);
	void frozenMillisecond(int milliseconds);

	void addLife(int value);
	void addLifeMax(int value);
	void addThew(int value);
	void addThewMax(int value);
	void addMana(int value);
	void addManaMax(int value);
	void addAttack(int value, int type = 1);
	void addDefend(int value, int type = 1);
	void addEvade(int value);
	void addExp(int value);
	void addMoney(int value);
	void equipGoods(int listIndex, int partIndex);
	void addRandMoney(int mMin, int mMax);
	void addGoods(const std::string& name, int count = 1);
	void addRandGoods(const std::string& fileName);
	void deleteGoods();
	void deleteGoods(const std::string& name);
	void deleteGoodsByName(const std::string& name, int count);
	void addMagic(const std::string& name);
	void addTalent(const std::string& name);
	void addOneMagic(const std::string& playerName, const std::string& magicName);
	void deleteMagic(const std::string& name);
	void addMagicExp(const std::string& name, int addexp);
	void fullLife();
	void fullThew();
	void fullMana();
	void updateState();
	void saveGoods(int index);
	void saveGoods(const std::string& snapshotKey);
	void loadGoods(int index);
	void loadGoods(const std::string& snapshotKey);
	void clearGoods();
	void clearMagic();
	void getGoodsNum(const std::string& name);
	void getGoodsNumByName(const std::string& name);
	void getGoodsState(const std::string& goodsName, const std::string& stateName, const std::string& varName);
	void getExp(const std::string& varName);
	void clearAllVar(const std::vector<std::string>& keepNames);
	void checkFreeGoodsSpace(const std::string& varName);
	void checkFreeMagicSpace(const std::string& varName);
	void getPlayerState(const std::string& stateName, const std::string& varName);
	void isEquipWeapon(const std::string& varName);
	void getMoneyNum();
	void getMoneyNum(const std::string& varName);
	void setMoneyNum(int value);
	bool showGamble(int cost, int npcType);
	void gamble(int cost, int npcType, const std::string& varName);
	void showDiceGame(const std::string& npcName);
	void showFishGame();
	void showStealWin(const std::string& npcName, const std::string& successScript = "", const std::string& failScript = "");
	void showGiveGoodsWin(const std::string& targetGoodsName, const std::string& successScript, const std::string& failScript);

	void showMessage(const std::string& str);
	void showSystemMessage(const std::string& str, int stayTime);
	void addToMemo(const std::string& str);
	void deleteMemo(const std::string& str);
	void clearMemo();
	void buyGoods(const std::string& fileName);
	void buyGoodsOnly(const std::string& fileName);
	void sellGoods(const std::string& fileName = "");
	void returnToTitle();
	void enableInput();
	void disableInput();
	void hideInterface();
	void hideBottomWnd();
	void showBottomWnd();
	void hideMouseCursor();
	void showMouseCursor();

	void showSnow(int bsnow);
	void showRandomSnow();
	void showRain(int brain);
	void beginRain(const std::string& configFileName);
	void endRain();

	void checkYear(const std::string& varName);
	void checkYear(const std::string& varName, const NewYearPeriod::LocalDate& localDate);

	void getRandNum(const std::string& varName, int minVal, int maxVal);
	void randRun(const std::string& varName, const std::string& successScript, const std::string& failScript);
	void getPlayerLevel(const std::string& varName);
	void getNpcCount(int kind, int relation);
	void delCurObj();
	void showInterface();
	void drawBackground();
	void clearEffect();
	void saveGame();
	void clearAllSave();
	void enableSave();
	void disableSave();
	void limitMana(int limit);
	void showNpc(const std::string& name, int isShow);
	void openWaterEffect();
	void closeWaterEffect();
	void watch(const std::string& name1, const std::string& name2, int watchType);
	void setTrap(const std::string& mapName, int idx, const std::string& trapFile);
	void setNpcMagicFile(const std::string& name, const std::string& fileName);
	void setNpcMagicLevel(const std::string& name, int level);
	void setPlayerMagicToUseWhenBeAttacked(const std::string& fileName, int direction);
	void setNpcMagicToUseWhenBeAttacked(const std::string& name, const std::string& fileName, int direction);
	void setNpcClickScript(const std::string& name, const std::string& scriptFile);
	void setNpcPartner(const std::string& name);
	void setPartnerLevel(int level);
	void setPartnerLevel(const std::string& name, int level);
	void playerAddEmotion(int value);
	void playerAddJustice(int value);
	void getPartnerIdx(const std::string& varName);
	void moveScreenEx(int x, int y, int speed);
	void displayMessage(const std::string& text);
	void disableMapScroll();
	void enableMapScroll();
	void setShowMapPos(int show);
	void openObj(const std::string& name = "");

	void openTimeLimit(int seconds);
	void closeTimeLimit();
	void hideTimerWnd();
	void setTimeScript(int seconds, const std::string& scriptFile);
	void choose(const std::string& message, const std::string& optionA, const std::string& optionB, const std::string& varName);
	void chooseEx(const std::string& message, const std::vector<std::string>& options, const std::string& varName);
	void chooseMultiple(int columnCount, int selectionCount, const std::string& varName, const std::string& message, const std::vector<std::string>& options);
	void choosePlus(const std::string& speakerName, int portraitIndex, int dialogPosition, const std::string& message, const std::vector<std::string>& options, const std::string& varName);
	void select(int messageIdx, int optionAIdx, int optionBIdx, const std::string& varName);

private:
	friend class GameManager;
	friend class CoreLifecycleTestAccess;
	struct PreparedSaveLoadCallbacks
	{
		std::string mapFolderName;
		std::function<bool(
			const std::function<void()>& beforeMutation,
			const std::function<bool()>& preparationCheckpoint)>
				commitMap;
		std::function<bool(
			const std::function<bool()>& preparationCheckpoint)>
				commitNpc;
		std::function<bool(
			const std::function<bool()>& preparationCheckpoint)>
				commitObject;
	};
	enum class MapActorResetMode
	{
		PreservePartners,
		ReplaceAllForSaveLoad,
	};
	GameManager* gameManager = nullptr;
	Engine* engine = nullptr;

	void runScriptWithCapturedParent(
		const std::string& fileName,
		const std::string& mapName,
		std::uint64_t capturedParentExecutionId,
		bool parentWasCaptured);
	ExactScriptExecutionResult runLoadedEditorRunScript(
		const EditorRun::SearchRoot& root,
		const std::string& virtualPath,
		RootedResourceReader::Result source);
	bool loadMapFromEditorRunBytes(
		const std::string& virtualPath,
		std::vector<std::uint8_t> bytes,
		bool resetCamera);
	bool loadNPCFromEditorRunBytes(
		const std::string& virtualPath,
		std::vector<std::uint8_t> bytes);
	bool loadObjectFromEditorRunBytes(
		const std::string& virtualPath,
		std::vector<std::uint8_t> bytes);
	static unsigned int loadingPresentationWaitMilliseconds(
		UTime currentTime,
		UTime lastPresentationTime);
	void presentSynchronousLoadingStatusFrame(
		const std::string& statusText) noexcept;

	GameLoading::LoadingTaskResult runExclusiveLoadingTask(
		const std::string& statusText,
		GameLoading::ExclusiveLoadingRunner::Worker worker,
		std::function<GameLoading::LoadingTaskResult(
			const std::function<bool()>& ownerCheckpoint)>
			successFinalizer = {},
		const std::function<void()>&
			loadingPresentationPumpObserver = {});
	bool runOwnerWorldCommit(
		const char* operationName,
		const std::function<bool(
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)>&
				commit,
		bool failCloseOnPartialFailure = true);
	bool loadMapWithFailurePolicy(
		const std::string& fileName,
		bool resetCamera,
		bool failCloseOnPartialFailure,
		const std::function<bool()>&
			preparationCheckpoint = {},
		const std::function<bool(
			const std::function<void()>& beforeMutation,
			const std::function<bool()>& preparationCheckpoint)>&
				preparedLoadCommit = {},
		bool rebuildDataMap = true,
		const std::string& preparedMapFolderName = {},
		MapActorResetMode actorResetMode =
			MapActorResetMode::PreservePartners);
	bool loadNPCWithPreparationCheckpoint(
		const std::string& fileName,
		const std::function<bool()>& preparationCheckpoint,
		const std::function<bool(
			const std::function<void()>& beforeMutation,
			const std::function<bool()>& preparationCheckpoint)>&
				preparedLoadCommit = {});
	bool loadObjectWithPreparationCheckpoint(
		const std::string& fileName,
		const std::function<bool()>& preparationCheckpoint,
		const std::function<bool(
			const std::function<void()>& beforeMutation,
			const std::function<bool()>& preparationCheckpoint)>&
				preparedLoadCommit = {});
	void recoverFromPartialWorldFailure(
		const char* operationName) noexcept;
	void discardPartialWorldAfterFailedCommit() noexcept;
	GameLoading::LoadingTaskResult commitPreparedSaveGeneration(
		const std::string& preparedDirectory,
		const SaveGenerationPreflightPolicy& policy,
		const std::function<bool()>& ownerCheckpoint = {},
		const std::function<bool(
			const std::string& generationDirectory,
			const std::function<bool()>& ownerCheckpoint)>&
				generationLoadOverride = {});
	bool loadGameFromGeneration(
		const std::string& generationDirectory,
		const std::function<bool()>& ownerCheckpoint = {},
		bool allowMissingNpcList = false,
		bool allowMissingObjectList = false,
		const PreparedSaveLoadCallbacks& preparedCallbacks = {});
	bool loadCurrentGame(
		const std::function<bool()>& ownerCheckpoint = {},
		bool allowMissingNpcList = false,
		bool allowMissingObjectList = false,
		const PreparedSaveLoadCallbacks& preparedCallbacks = {});
};
