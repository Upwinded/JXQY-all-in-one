#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace MenuSurfaceCatalog
{
	enum class SurfaceId
	{
		StartupResourceSelect,
		StartupTitle,
		TitleTeam,
		VideoPlayback,
		ControllerHelp,
		System,
		Option,
		SaveLoadTitle,
		SaveLoadSystem,
		GlobalYesNo,
		Dialog,
		Choice,
		BuySell,
		GambleNormal,
		GambleDice,
		GambleFish,
		PartnerEquipment,
		RpgState,
		RpgEquipment,
		RpgPractice,
		RpgGoods,
		RpgMagic,
		RpgMemo,
		HudBottom,
		HudTop,
		HudColumn,
		PartnerHead,
		MapThumbnail,
		SystemNotice,
		Message,
		Timer,
		Tooltip,
		NpcInfo,
		MobileJoystick,
		MobileSkills,
		LoadingTextOverlay,
		Count
	};

	enum class SurfaceScope
	{
		Startup,
		Title,
		Global,
		InGame,
		TouchControls,
		Overlay
	};

	enum class ModalKind
	{
		RootScene,
		Modal,
		NonModal,
		PassiveOverlay
	};

	enum class WorldPointerPolicy
	{
		BlockAll,
		HitTestOnly,
		PassThrough
	};

	enum class WorldSemanticPolicy
	{
		Block,
		Allow
	};

	enum class FocusPolicy
	{
		Scoped,
		VisibleNonModalSet,
		PointerOnly,
		None
	};

	enum class DefaultFocusPolicy
	{
		MenuDefined,
		FirstAvailable,
		None
	};

	enum class FocusRestorePolicy
	{
		PreviousValidOrDefault,
		ExistingOrDefault,
		None
	};

	enum class ControllerInteractionKind
	{
		// Owns discrete controller candidates and a directional focus graph.
		FocusGraph,
		// Handles semantic controller actions without a discrete focus target.
		ActionOnly,
		// Has no controller target; input blocking remains a surface policy.
		Passive,
		// Participates in pointer/touch input but never gamepad focus.
		PointerOnly
	};

	struct SurfacePolicy
	{
		SurfaceId id;
		std::string_view key;
		SurfaceScope scope;
		ModalKind modalKind;
		WorldPointerPolicy worldPointerPolicy;
		WorldSemanticPolicy worldSemanticPolicy;
		FocusPolicy focusPolicy;
		DefaultFocusPolicy defaultFocusPolicy;
		FocusRestorePolicy focusRestorePolicy;
		ControllerInteractionKind controllerInteractionKind;
		std::string_view dynamicSource;
	};

	inline constexpr std::array<SurfacePolicy,
		static_cast<std::size_t>(SurfaceId::Count)> kSurfacePolicies =
	{{
		{ SurfaceId::StartupResourceSelect, "startup.resource-select",
			SurfaceScope::Startup, ModalKind::RootScene,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::None,
			ControllerInteractionKind::FocusGraph,
			"cxx:ResourceSelectScene; children:ResourcePackList,ResourcePackCard" },
		{ SurfaceId::StartupTitle, "startup.title",
			SurfaceScope::Startup, ModalKind::RootScene,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::None,
			ControllerInteractionKind::FocusGraph,
			"cxx:Title; aggregate:ini/ui/title/*.menu.ini; profile:title,title_newyear" },
		{ SurfaceId::TitleTeam, "title.team",
			SurfaceScope::Title, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::ActionOnly,
			"cxx:TitleTeam; surface actions:scroll,close; opened-by:Title::openTeamPage" },
		{ SurfaceId::VideoPlayback, "video.playback",
			SurfaceScope::Global, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::ActionOnly,
			"cxx:VideoPage,VideoPlayer; surface action:skip; script:PlayMovie,StopMovie" },
		{ SurfaceId::ControllerHelp, "controller-help",
			SurfaceScope::Global, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::ActionOnly,
			"cxx:ControllerHelpOverlay; runtime:showControllerHelp" },
		{ SurfaceId::System, "system",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:System; aggregate:ini/ui/system/*.menu.ini" },
		{ SurfaceId::Option, "option",
			SurfaceScope::Global, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:Option; aggregate:ini/ui/option/*.menu.ini; parent:System" },
		{ SurfaceId::SaveLoadTitle, "save-load.title",
			SurfaceScope::Title, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:SaveLoad(false,*); aggregate:ini/ui/saveload/*.menu.ini; parent:Title" },
		{ SurfaceId::SaveLoadSystem, "save-load.system",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:SaveLoad(true,*); aggregate:ini/ui/saveload/*.menu.ini; parent:System" },
		{ SurfaceId::GlobalYesNo, "global.yes-no",
			SurfaceScope::Global, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:YesNo; aggregate:ini/ui/yesno/*.menu.ini" },
		{ SurfaceId::Dialog, "dialog",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::ActionOnly,
			"cxx:Dialog; surface actions:advance,close; aggregate:ini/ui/dialog/*.menu.ini; script:Talk,ShowTalk,Say" },
		{ SurfaceId::Choice, "choice",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:ChooseMenu; aggregate:ini/ui/choose/*.menu.ini; script:Choose*" },
		{ SurfaceId::BuySell, "buy-sell",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:BuySellMenu; aggregate:ini/ui/buysell/*.menu.ini; script:BuyGoods*,SellGoods" },
		{ SurfaceId::GambleNormal, "gamble.normal",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:GambleMenu; profile:ini/ui/littlegame/*; script:ShowGamble" },
		{ SurfaceId::GambleDice, "gamble.dice",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::ActionOnly,
			"cxx:GambleMenu; profile:ini/ui/dicegame/*; script:ShowDiceGame" },
		{ SurfaceId::GambleFish, "gamble.fish",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::ActionOnly,
			"cxx:GambleMenu; profile:ini/ui/fishgame/*; script:ShowFishGame" },
		{ SurfaceId::PartnerEquipment, "partner-equipment",
			SurfaceScope::InGame, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::Scoped, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::PreviousValidOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:PartnerEquipMenu; dynamic-controls:partner equipment slots" },
		{ SurfaceId::RpgState, "rpg.state",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::Passive,
			"cxx:StateMenu; aggregate:ini/ui/state/*.menu.ini; display-only" },
		{ SurfaceId::RpgEquipment, "rpg.equipment",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:EquipMenu; aggregate:ini/ui/equip/*.menu.ini" },
		{ SurfaceId::RpgPractice, "rpg.practice",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:PracticeMenu; aggregate:ini/ui/xiulian/*.menu.ini" },
		{ SurfaceId::RpgGoods, "rpg.goods",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:GoodsMenu; aggregate:ini/ui/goods/*.menu.ini" },
		{ SurfaceId::RpgMagic, "rpg.magic",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:MagicMenu; aggregate:ini/ui/magic/*.menu.ini" },
		{ SurfaceId::RpgMemo, "rpg.memo",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:MemoMenu; aggregate:ini/ui/memo/*.menu.ini" },
		{ SurfaceId::HudBottom, "hud.bottom",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Allow,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:BottomMenu; aggregate:ini/ui/bottom/*.menu.ini; script:*Interface,*BottomWnd" },
		{ SurfaceId::HudTop, "hud.top",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Allow,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:TopMenu; aggregate:ini/ui/top/*.menu.ini" },
		{ SurfaceId::HudColumn, "hud.column",
			SurfaceScope::InGame, ModalKind::PassiveOverlay,
			WorldPointerPolicy::PassThrough, WorldSemanticPolicy::Allow,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"cxx:ColumnMenu; aggregate:ini/ui/column/*.menu.ini; display-only" },
		{ SurfaceId::PartnerHead, "partner-head",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Allow,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:PartnerHeadMenu; dynamic-controls:partner head entries" },
		{ SurfaceId::MapThumbnail, "map-thumbnail",
			SurfaceScope::InGame, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Block,
			FocusPolicy::VisibleNonModalSet, DefaultFocusPolicy::MenuDefined,
			FocusRestorePolicy::ExistingOrDefault,
			ControllerInteractionKind::FocusGraph,
			"cxx:MapThumbnailMenu; aggregate:ini/ui/mapthumbnail,ini/ui/littlemap" },
		{ SurfaceId::SystemNotice, "system-notice",
			SurfaceScope::Overlay, ModalKind::PassiveOverlay,
			WorldPointerPolicy::PassThrough, WorldSemanticPolicy::Allow,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"cxx:SystemNotice; runtime:MenuController::showSystemNotice" },
		{ SurfaceId::Message, "message",
			SurfaceScope::Overlay, ModalKind::PassiveOverlay,
			WorldPointerPolicy::PassThrough, WorldSemanticPolicy::Allow,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"cxx:MsgBox; aggregate:ini/ui/message/*.menu.ini; script:*Message,TalkSelfTip" },
		{ SurfaceId::Timer, "timer",
			SurfaceScope::Overlay, ModalKind::PassiveOverlay,
			WorldPointerPolicy::PassThrough, WorldSemanticPolicy::Allow,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"cxx:TimerMenu; aggregate:ini/ui/timer/*.menu.ini; script:*TimeLimit,*TimerWnd" },
		{ SurfaceId::Tooltip, "tooltip",
			SurfaceScope::Overlay, ModalKind::PassiveOverlay,
			WorldPointerPolicy::PassThrough, WorldSemanticPolicy::Allow,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"cxx:ToolTip; aggregate:ini/ui/tooltip/*.menu.ini" },
		{ SurfaceId::NpcInfo, "npc-info",
			SurfaceScope::Overlay, ModalKind::PassiveOverlay,
			WorldPointerPolicy::PassThrough, WorldSemanticPolicy::Allow,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"cxx:NpcInfoPanel; runtime:NPC selection" },
		{ SurfaceId::MobileJoystick, "mobile.joystick",
			SurfaceScope::TouchControls, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Allow,
			FocusPolicy::PointerOnly, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::PointerOnly,
			"cxx:JoystickPanel; aggregate:common/ini/ui/mobile/joystick/*.menu.ini" },
		{ SurfaceId::MobileSkills, "mobile.skills",
			SurfaceScope::TouchControls, ModalKind::NonModal,
			WorldPointerPolicy::HitTestOnly, WorldSemanticPolicy::Allow,
			FocusPolicy::PointerOnly, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::PointerOnly,
			"cxx:SkillsPanel; aggregate:common/ini/ui/mobile/skills/*.menu.ini" },
		{ SurfaceId::LoadingTextOverlay, "loading-text-overlay",
			SurfaceScope::Overlay, ModalKind::Modal,
			WorldPointerPolicy::BlockAll, WorldSemanticPolicy::Block,
			FocusPolicy::None, DefaultFocusPolicy::None,
			FocusRestorePolicy::None,
			ControllerInteractionKind::Passive,
			"exclusive-loading-loop:"
			"ScriptAPI::runExclusiveLoadingTask" }
	}};

	struct TypeBinding
	{
		std::string_view typeName;
		SurfaceId surfaceId;
	};

	inline constexpr std::array<TypeBinding, 37> kTypeBindings =
	{{
		{ "ResourceSelectScene", SurfaceId::StartupResourceSelect },
		{ "Title", SurfaceId::StartupTitle },
		{ "TitleTeam", SurfaceId::TitleTeam },
		{ "VideoPage", SurfaceId::VideoPlayback },
		{ "VideoPlayer", SurfaceId::VideoPlayback },
		{ "ControllerHelpOverlay", SurfaceId::ControllerHelp },
		{ "System", SurfaceId::System },
		{ "Option", SurfaceId::Option },
		{ "SaveLoad", SurfaceId::SaveLoadTitle },
		{ "SaveLoad", SurfaceId::SaveLoadSystem },
		{ "YesNo", SurfaceId::GlobalYesNo },
		{ "Dialog", SurfaceId::Dialog },
		{ "ChooseMenu", SurfaceId::Choice },
		{ "BuySellMenu", SurfaceId::BuySell },
		{ "GambleMenu", SurfaceId::GambleNormal },
		{ "GambleMenu", SurfaceId::GambleDice },
		{ "GambleMenu", SurfaceId::GambleFish },
		{ "PartnerEquipMenu", SurfaceId::PartnerEquipment },
		{ "StateMenu", SurfaceId::RpgState },
		{ "EquipMenu", SurfaceId::RpgEquipment },
		{ "PracticeMenu", SurfaceId::RpgPractice },
		{ "GoodsMenu", SurfaceId::RpgGoods },
		{ "MagicMenu", SurfaceId::RpgMagic },
		{ "MemoMenu", SurfaceId::RpgMemo },
		{ "BottomMenu", SurfaceId::HudBottom },
		{ "TopMenu", SurfaceId::HudTop },
		{ "ColumnMenu", SurfaceId::HudColumn },
		{ "PartnerHeadMenu", SurfaceId::PartnerHead },
		{ "MapThumbnailMenu", SurfaceId::MapThumbnail },
		{ "SystemNotice", SurfaceId::SystemNotice },
		{ "MsgBox", SurfaceId::Message },
		{ "TimerMenu", SurfaceId::Timer },
		{ "ToolTip", SurfaceId::Tooltip },
		{ "NpcInfoPanel", SurfaceId::NpcInfo },
		{ "JoystickPanel", SurfaceId::MobileJoystick },
		{ "SkillsPanel", SurfaceId::MobileSkills },
		{ "ScriptAPI::runExclusiveLoadingTask", SurfaceId::LoadingTextOverlay }
	}};

	struct ResourceBinding
	{
		std::string_view resourceDirectory;
		SurfaceId surfaceId;
	};

	inline constexpr std::array<ResourceBinding, 28> kResourceBindings =
	{{
		{ "title", SurfaceId::StartupTitle },
		{ "system", SurfaceId::System },
		{ "option", SurfaceId::Option },
		{ "saveload", SurfaceId::SaveLoadTitle },
		{ "saveload", SurfaceId::SaveLoadSystem },
		{ "yesno", SurfaceId::GlobalYesNo },
		{ "dialog", SurfaceId::Dialog },
		{ "choose", SurfaceId::Choice },
		{ "buysell", SurfaceId::BuySell },
		{ "littlegame", SurfaceId::GambleNormal },
		{ "dicegame", SurfaceId::GambleDice },
		{ "fishgame", SurfaceId::GambleFish },
		{ "state", SurfaceId::RpgState },
		{ "equip", SurfaceId::RpgEquipment },
		{ "xiulian", SurfaceId::RpgPractice },
		{ "goods", SurfaceId::RpgGoods },
		{ "magic", SurfaceId::RpgMagic },
		{ "memo", SurfaceId::RpgMemo },
		{ "bottom", SurfaceId::HudBottom },
		{ "top", SurfaceId::HudTop },
		{ "column", SurfaceId::HudColumn },
		{ "mapthumbnail", SurfaceId::MapThumbnail },
		{ "littlemap", SurfaceId::MapThumbnail },
		{ "message", SurfaceId::Message },
		{ "timer", SurfaceId::Timer },
		{ "tooltip", SurfaceId::Tooltip },
		{ "mobile/joystick", SurfaceId::MobileJoystick },
		{ "mobile/skills", SurfaceId::MobileSkills }
	}};

	struct ScriptBinding
	{
		std::string_view registration;
		std::string_view policyKeyOrExemption;
	};

	inline constexpr std::array<ScriptBinding, 52> kScriptBindings =
	{{
		{ "Talk", "dialog" },
		{ "ShowTalk", "dialog" },
		{ "Say", "dialog" },
		{ "PlayMovie", "video.playback" },
		{ "StopMovie", "video.playback" },
		{ "LoadMap", "loading-text-overlay" },
		{ "LoadGame", "loading-text-overlay" },
		{ "LoadObj", "loading-text-overlay" },
		{ "LoadNpc", "exempt:no-interface-created" },
		{ "ReturnToTitle", "startup.title" },
		{ "ShowGamble", "gamble.normal" },
		{ "Gamble", "gamble.normal" },
		{ "ShowDiceGame", "gamble.dice" },
		{ "ShowFishGame", "gamble.fish" },
		{ "ShowStealWin", "choice" },
		{ "ShowGiveGoodsWin", "exempt:no-interface-created" },
		{ "ShowMessage", "message" },
		{ "ShowSystemMsg", "message" },
		{ "DisplayMessage", "message" },
		{ "MessageBox", "message" },
		{ "Message", "message" },
		{ "ShowSystemMessage", "message" },
		{ "TalkSelfTip", "message" },
		{ "BuyGoods", "buy-sell" },
		{ "BuyGoodsOnly", "buy-sell" },
		{ "SellGoods", "buy-sell" },
		{ "HideInterface", "hud.bottom" },
		{ "SetInterfaceVisible", "hud.bottom" },
		{ "ShowInterface", "hud.bottom" },
		{ "HideBottomWnd", "hud.bottom" },
		{ "ShowBottomWnd", "hud.bottom" },
		{ "OpenTimeLimit", "timer" },
		{ "CloseTimeLimit", "timer" },
		{ "HideTimerWnd", "timer" },
		{ "OpenTimer", "timer" },
		{ "CloseTimer", "timer" },
		{ "HideTimer", "timer" },
		{ "HideBottomWindow", "hud.bottom" },
		{ "ShowBottomWindow", "hud.bottom" },
		{ "Choose", "choice" },
		{ "ChooseEx", "choice" },
		{ "ChooseMultiple", "choice" },
		{ "ChoosePlus", "choice" },
		{ "Select", "choice" },
		{ "SetNpcTalkContent", "exempt:npc-data-only" },
		{ "ShowSignalTip", "exempt:npc-world-marker" },
		{ "SetSignalTipHidden", "exempt:npc-world-marker" },
		{ "SetSaveEnabled", "exempt:save-state-only" },
		{ "Memo", "exempt:memo-data-only" },
		{ "AddToMemo", "exempt:memo-data-only" },
		{ "DelMemo", "exempt:memo-data-only" },
		{ "ClearMemo", "exempt:memo-data-only" }
	}};

	struct Exemption
	{
		std::string_view category;
		std::string_view name;
		std::string_view reason;
	};

	inline constexpr std::array<Exemption, 51> kExemptions =
	{{
		{ "type", "MenuController", "surface container and input dispatcher" },
		{ "type", "Panel", "upMenu container, not an independently displayed surface" },
		{ "type", "ConfigDrivenPanel", "abstract resource-driven container" },
		{ "type", "BaseComponent", "abstract child-control base" },
		{ "type", "Button", "child control, not an independently displayed surface" },
		{ "type", "CheckBox", "child control, not an independently displayed surface" },
		{ "type", "ChooseTextButton", "child control of ChooseMenu" },
		{ "type", "ColumnImage", "display child of ColumnMenu" },
		{ "type", "DragButton", "child control, not an independently displayed surface" },
		{ "type", "DragRoundButton", "child control, not an independently displayed surface" },
		{ "type", "FadeMask", "display child, not an independently displayed surface" },
		{ "type", "FlatScrollbar", "child control, not an independently displayed surface" },
		{ "type", "ImageContainer", "child-control container base" },
		{ "type", "Item", "child control, not an independently displayed surface" },
		{ "type", "Joystick", "child control of mobile.joystick" },
		{ "type", "Label", "display child, not an independently displayed surface" },
		{ "type", "ListBox", "child control, not an independently displayed surface" },
		{ "type", "MemoText", "display child, not an independently displayed surface" },
		{ "type", "RoundButton", "child control, not an independently displayed surface" },
		{ "type", "Scrollbar", "child control, not an independently displayed surface" },
		{ "type", "TalkLabel", "dialog display child, not an independently displayed surface" },
		{ "type", "TextButton", "child control, not an independently displayed surface" },
		{ "type", "TransImage", "display child, not an independently displayed surface" },
		{ "type", "ControllerTransferParticipant", "input interface, not a surface" },
		{ "type", "SlotGridController", "focus state helper, not a surface" },
		{ "type", "ControllerPaneRouter", "focus routing helper, not a surface" },
		{ "type", "SlotInteractionController", "focus state helper, not a surface" },
		{ "type", "ResourcePackList", "child control of ResourceSelectScene" },
		{ "type", "ResourcePackCard", "child control of ResourcePackList" },
		{ "type", "FlatTextButton", "child control, not a surface" },
		{ "type", "MinimapToggleButton", "child control of mobile.skills" },
		{ "type", "ChooseMultipleSelection", "choice state, rendered by ChooseMenu" },
		{ "type", "DiceGambleState", "state, rendered by GambleMenu" },
		{ "type", "FishGameState", "state, rendered by GambleMenu" },
		{ "type", "Nurturance", "unreachable empty historical scene" },
		{ "type", "MainScene", "gameplay scene container" },
		{ "type", "GameManager", "gameplay container" },
		{ "type", "GameController", "world input container" },
		{ "type", "Camera", "world component" },
		{ "type", "Effect", "world element" },
		{ "type", "GameElement", "world-element base" },
		{ "type", "Weather", "world visual component" },
		{ "type", "Map", "world component" },
		{ "type", "NPC", "world element" },
		{ "type", "NPCManager", "world component" },
		{ "type", "Object", "world element" },
		{ "type", "ObjectManager", "world component" },
		{ "type", "EffectManager", "world component" },
		{ "type", "Player", "world component" },
		{ "script", "ShowGiveGoodsWin", "branches to a script and creates no interface" },
		{ "dynamic", "ConfigDrivenPanel::loadMenuDefinition(menuFile)",
			"recursive submenu inherits its owning surface policy" }
	}};

	constexpr const SurfacePolicy* find(SurfaceId id)
	{
		const std::size_t index = static_cast<std::size_t>(id);
		return index < kSurfacePolicies.size() ? &kSurfacePolicies[index] : nullptr;
	}

	constexpr bool policiesMatchSurfaceIds()
	{
		for (std::size_t index = 0; index < kSurfacePolicies.size(); index++)
		{
			if (static_cast<std::size_t>(kSurfacePolicies[index].id) != index)
			{
				return false;
			}
		}
		return true;
	}

	static_assert(policiesMatchSurfaceIds(),
		"kSurfacePolicies must remain in SurfaceId order");

	constexpr const SurfacePolicy* find(std::string_view key)
	{
		for (const SurfacePolicy& policy : kSurfacePolicies)
		{
			if (policy.key == key)
			{
				return &policy;
			}
		}
		return nullptr;
	}

	constexpr bool blocksWorldPointerInput(SurfaceId id)
	{
		const SurfacePolicy* policy = find(id);
		return policy != nullptr
			&& policy->worldPointerPolicy == WorldPointerPolicy::BlockAll;
	}

	constexpr bool blocksWorldSemanticInput(SurfaceId id)
	{
		const SurfacePolicy* policy = find(id);
		return policy != nullptr
			&& policy->worldSemanticPolicy == WorldSemanticPolicy::Block;
	}

	constexpr bool blocksWorldKeyboardInput(SurfaceId id)
	{
		const SurfacePolicy* policy = find(id);
		return policy != nullptr
			&& (policy->modalKind == ModalKind::Modal
				|| policy->modalKind == ModalKind::RootScene);
	}

	constexpr bool hasConsistentPolicies()
	{
		for (std::size_t index = 0; index < kSurfacePolicies.size(); index++)
		{
			const SurfacePolicy& policy = kSurfacePolicies[index];
			if (static_cast<std::size_t>(policy.id) != index
				|| policy.key.empty() || policy.dynamicSource.empty())
			{
				return false;
			}
			if (policy.focusPolicy == FocusPolicy::None
				&& policy.defaultFocusPolicy != DefaultFocusPolicy::None)
			{
				return false;
			}
			if (policy.modalKind == ModalKind::PassiveOverlay
				&& (policy.worldPointerPolicy
						!= WorldPointerPolicy::PassThrough
					|| policy.worldSemanticPolicy
						!= WorldSemanticPolicy::Allow
					|| policy.focusPolicy != FocusPolicy::None
					|| policy.focusRestorePolicy
						!= FocusRestorePolicy::None))
			{
				return false;
			}
			if (policy.modalKind == ModalKind::NonModal
				&& policy.worldPointerPolicy
					== WorldPointerPolicy::BlockAll)
			{
				return false;
			}
			if ((policy.modalKind == ModalKind::Modal
					|| policy.modalKind == ModalKind::RootScene)
				&& policy.worldPointerPolicy
					!= WorldPointerPolicy::BlockAll)
			{
				return false;
			}
		}
		return true;
	}

	constexpr bool hasConsistentControllerInteractionPolicies()
	{
		for (const SurfacePolicy& policy : kSurfacePolicies)
		{
			switch (policy.controllerInteractionKind)
			{
			case ControllerInteractionKind::FocusGraph:
				if ((policy.focusPolicy != FocusPolicy::Scoped
						&& policy.focusPolicy
							!= FocusPolicy::VisibleNonModalSet)
					|| policy.defaultFocusPolicy
						== DefaultFocusPolicy::None)
				{
					return false;
				}
				break;
			case ControllerInteractionKind::ActionOnly:
			case ControllerInteractionKind::Passive:
				if (policy.focusPolicy != FocusPolicy::None
					|| policy.defaultFocusPolicy
						!= DefaultFocusPolicy::None)
				{
					return false;
				}
				break;
			case ControllerInteractionKind::PointerOnly:
				if (policy.focusPolicy != FocusPolicy::PointerOnly
					|| policy.defaultFocusPolicy
						!= DefaultFocusPolicy::None)
				{
					return false;
				}
				break;
			}
		}
		return true;
	}

	static_assert(hasConsistentPolicies(),
		"Every menu surface must have a complete and coherent input policy");
	static_assert(hasConsistentControllerInteractionPolicies(),
		"Every controller interaction kind must match its surface focus policy");
}
