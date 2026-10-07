#pragma once
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
#include "AutomationPipe.h"
#include "../Launch/EditorRunRuntimeTraceWriter.h"
#include "../Types/ElementTypes.h"
#include <filesystem>
#include <map>
#include <memory>
#include <fstream>

class Element;
class EngineBase;
class GameElement;
class GameManager;
class NPC;
class Magic;
class UIFocusManager;

// Only the SDL owner thread uses this class, except for AutomationPipe's queue.
class GameplayAutomationSession final
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
	friend class GameplayAutomationTestAccess;
#endif
public:
	GameplayAutomationSession(const std::string& pipeName, const std::filesystem::path& userRoot);
	~GameplayAutomationSession();
	static void onFrame();
	static void frameStarted();
	static void manualInput();
	static void beforePresent(EngineBase& engine);
	static void contextChanged();
	static void worldChanged();
	static void interactionStarted(const std::shared_ptr<Element>& target);
	static void targetDefeated(const std::shared_ptr<NPC>& target);
	static void skillUsed(const std::shared_ptr<Magic>& magic);
	static EditorRun::RuntimeTraceWriter* traceWriter();
	static bool enabled();
	static std::string currentScript;
private:
	using Value = GameplayAutomation::Value;
	struct Action
	{
		std::uint64_t id = 0;
		std::string command;
		std::string status = "running";
		std::string reason;
		Value arguments;
		std::uint64_t generation = 0;
		std::uint64_t started = 0;
		std::uint64_t lastProgress = 0;
		std::uint64_t lastMoveAttempt = 0;
		Point lastPosition{0, 0};
		std::weak_ptr<NPC> target;
		std::weak_ptr<Magic> magic;
		int lastTargetLife = -1;
		int kills = 0;
		bool executing = false;
		bool completingDispatch = false;
		bool observedExecution = false;
		std::uint64_t dispatchFrame = 0;
	};
	static GameplayAutomationSession* active;
	GameplayAutomation::AutomationPipe pipe;
	std::filesystem::path outputRoot;
	std::shared_ptr<std::ofstream> traceFile;
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer;
	std::ofstream events;
	bool eventOutputFailed = false;
	std::uint64_t frame = 0;
	std::uint64_t context = 1;
	std::uint64_t generation = 1;
	std::uint64_t nextEntity = 1;
	std::uint64_t nextAction = 1;
	std::uint64_t worldAction = 0;
	std::uint64_t manualInputConnection = 0;
	std::uint64_t captureAction = 0;
	std::uint64_t presentedContext = 0;
	std::uint64_t lastDialogueAdvance = 0;
	bool autoDialogue = false;
	int dialogueInterval = 100;
	std::string uiSignature;
	std::string lastMap;
	std::map<std::uint64_t, Action> actions;
	std::map<std::weak_ptr<Element>, std::uint64_t, std::owner_less<std::weak_ptr<Element>>> entityIds;
	std::map<std::uint64_t, std::weak_ptr<Element>> entities;
	std::uint64_t identify(const std::shared_ptr<Element>& element);
	std::shared_ptr<Element> resolve(std::uint64_t id);
	Element* owner() const;
	bool ownsVisibleElement(const std::shared_ptr<Element>& element) const;
	bool worldInputAllowed() const;
	void requireWorld(const Value& arguments) const;
	void requireContext(const Value& arguments) const;
	Value observe(const Value& arguments);
	Value uiState();
	Value actionState(const Action& action) const;
	void processRequest(const std::shared_ptr<GameplayAutomation::AutomationPipe::Request>& request);
	void execute(Action& action);
	void updateWorldAction();
	void updateContext();
	void finish(Action& action, std::string status, std::string reason);
	void cancelWorld(const std::string& reason);
	void stopQueuedWorldInput();
	bool queueSkill(int slot, const std::shared_ptr<NPC>& target, bool requireReach = false,
		std::string* unavailableReason = nullptr);
	void record(const std::string& event, Value data);
	void tick();
};
#endif
