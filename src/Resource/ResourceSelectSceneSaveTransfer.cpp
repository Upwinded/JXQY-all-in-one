#include "ResourceSelectScene.h"

#include "../File/File.h"
#include "../Game/GameManager/SaveFileManager.h"
#include <SDL3/SDL.h>

struct SaveFileSelection
{
	std::atomic<bool> ready{ false };
	std::string path;
	std::string error;
};

namespace
{
void SDLCALL saveFileSelected(void* userdata, const char* const* files, int)
{
#if defined(__ANDROID__)
	SDL_ResetHint("JXQY_SAVE_PACKAGE_EXPORT_NAME");
#endif
	std::unique_ptr<std::shared_ptr<SaveFileSelection>> owner(
		static_cast<std::shared_ptr<SaveFileSelection>*>(userdata));
	const auto& selection = *owner;
	if (files == nullptr) selection->error = u8"无法打开文件选择器：" + std::string(SDL_GetError());
	else if (*files != nullptr) selection->path = *files;
	selection->ready.store(true, std::memory_order_release);
}
}

bool ResourceSelectScene::resolveSaveTransferGame(
	const std::string& saveNamespace, const std::string& gameId)
{
	for (const auto& pack : ResourceManager::instance().getDiscoveredPacks())
	{
		if (!pack.manifest.resourceOnly && pack.isLaunchable() &&
			OnlineUpdate::foldGameId(pack.effectiveSaveNamespace) == OnlineUpdate::foldGameId(saveNamespace) &&
			(gameId.empty() || OnlineUpdate::foldGameId(pack.manifest.id) == OnlineUpdate::foldGameId(gameId)))
		{
			saveTransferGame = { pack.manifest.id, pack.getDisplayName(),
				pack.effectiveSaveNamespace, pack.manifest.releaseMetadata.displayVersion,
				pack.manifest.minimumCompatibleSaveResourceVersion };
			return true;
		}
	}
	saveTransferGame = {};
	return false;
}

std::filesystem::path ResourceSelectScene::saveTransferRoot() const
{
	return std::filesystem::u8path(File::getUserDataRoot()) / "save" /
		std::filesystem::u8path(File::sanitizeSaveNamespace(saveTransferGame.saveNamespace));
}

void ResourceSelectScene::chooseSaveExport()
{
	if (resourceInstallDialogState != ResourceInstallDialogState::BrowsingSaves ||
		selectedSaveNamespaceIndex < 0 || selectedSaveNamespaceIndex >= static_cast<int>(saveNamespaceEntries.size())) return;
	saveTransferAction = SaveTransferAction::Export;
	if (!resolveSaveTransferGame(saveNamespaceEntries[selectedSaveNamespaceIndex].saveNamespace))
	{
		resourceInstallDialogMessage = u8"请先安装该存档对应的游戏资源，再导出存档。";
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
	}
	else
	{
		saveExportSlots = SavePackage::listSlots(saveTransferRoot());
		if (!saveExportSlots.empty()) saveExportSlots.insert(saveExportSlots.begin(), SavePackage::AllSlots);
		selectedTransferSlot = 0;
		resourceInstallDialogState = ResourceInstallDialogState::ChoosingSaveExport;
	}
	resourceInstallOperation = ResourceInstallOperation::SaveTransfer;
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-primary") || focusManager.focusNode("install-secondary");
	updateFocusPresentation();
}

void ResourceSelectScene::selectSaveFile(bool exporting)
{
	if (resourceInstallRunner || saveFileSelection) return;
	if (exporting && (resourceInstallDialogState != ResourceInstallDialogState::ChoosingSaveExport || saveExportSlots.empty())) return;
	if (!exporting && resourceInstallDialogState != ResourceInstallDialogState::BrowsingSaves) return;
	cancelPointerInteraction();
	saveTransferAction = exporting ? SaveTransferAction::Export : SaveTransferAction::Inspect;
	resourceInstallOperation = ResourceInstallOperation::SaveTransfer;
	resourceInstallDialogState = ResourceInstallDialogState::SelectingSaveFile;
	resourceInstallDialogMessage = exporting ? u8"请选择存档包保存位置" : u8"请选择要导入的存档包";
	saveFileSelection = std::make_shared<SaveFileSelection>();
	// The callback owns its shared reference and never captures the scene. It
	// remains valid if the app closes the scene while the system picker is open.
	auto* context = new std::shared_ptr<SaveFileSelection>(saveFileSelection);
	static const SDL_DialogFileFilter filters[] = { { u8"剑侠情缘存档包", "zip" }, { u8"所有文件", "*" } };
	refreshResourceInstallDialogControls();
	if (exporting)
	{
		const int slot = saveExportSlots[selectedTransferSlot];
		const std::string filename = saveTransferGame.saveNamespace + "_" +
			(slot == SavePackage::AllSlots ? "all" : SavePackage::slotDirectory(slot)) + ".jxqy-save.zip";
#if defined(__ANDROID__)
		// SDL 3.4's Android picker does not pass default_location to Java.
		SDL_SetHint("JXQY_SAVE_PACKAGE_EXPORT_NAME", filename.c_str());
#endif
		SDL_ShowSaveFileDialog(saveFileSelected, context, SDL_GetKeyboardFocus(), filters, 1, filename.c_str());
	}
	else SDL_ShowOpenFileDialog(saveFileSelected, context, SDL_GetKeyboardFocus(), filters, 2, nullptr, false);
}

void ResourceSelectScene::pollSaveFileSelection()
{
	if (!saveFileSelection || !saveFileSelection->ready.load(std::memory_order_acquire)) return;
	const auto selection = std::move(saveFileSelection);
	if (!selection->error.empty())
	{
		resourceInstallDialogMessage = selection->error;
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
	}
	else if (selection->path.empty())
	{
		resourceInstallDialogState = saveTransferAction == SaveTransferAction::Export
			? ResourceInstallDialogState::ChoosingSaveExport : ResourceInstallDialogState::BrowsingSaves;
		if (saveTransferAction != SaveTransferAction::Export)
			resourceInstallOperation = ResourceInstallOperation::SaveManagement;
	}
	else
	{
		startSaveTransfer(selection->path);
		return;
	}
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-secondary");
	updateFocusPresentation();
}

void ResourceSelectScene::startSaveTransfer(const std::string& path)
{
	if (resourceInstallRunner) return;
	const SaveTransferAction action = saveTransferAction;
	const int slot = action == SaveTransferAction::Export ? saveExportSlots[selectedTransferSlot] : selectedTransferSlot;
	const auto game = saveTransferGame;
	const auto root = saveTransferRoot();
	const auto package = pendingSavePackage;
	auto result = std::make_shared<ResourceInstallWorkerResult>();
	result->operation = ResourceInstallOperation::SaveTransfer;
	resourceInstallWorkerResult = result;
	resourceInstallOperation = ResourceInstallOperation::SaveTransfer;
	resourceInstallDialogState = ResourceInstallDialogState::TransferringSaves;
	resourceInstallDialogMessage = action == SaveTransferAction::Export ? u8"正在导出存档…"
		: action == SaveTransferAction::Import ? u8"正在导入存档…" : u8"正在读取存档包…";
	refreshResourceInstallDialogControls();
	resourceInstallRunner = std::make_unique<GameLoading::ExclusiveLoadingRunner>(
		[action, slot, game, root, package, result, path](const GameLoading::LoadingCancellationToken& cancellation)
		{
			if (cancellation.isCancellationRequested()) return GameLoading::LoadingTaskResult::cancellation();
			std::string error;
			bool succeeded = false;
			if (action == SaveTransferAction::Inspect) succeeded = SavePackage::read(path, result->savePackage, error);
			else
			{
				SaveFileManager::OperationScope operation;
				succeeded = action == SaveTransferAction::Export
					? SavePackage::write(root, game, slot, path, error)
					: package && SavePackage::importTo(*package, game, root, slot, error);
			}
			return succeeded ? GameLoading::LoadingTaskResult::success() : GameLoading::LoadingTaskResult::failure(error);
		});
}

bool ResourceSelectScene::saveImportOverwrites() const
{
	if (!pendingSavePackage) return false;
	for (const auto& slot : pendingSavePackage->slots())
	{
		const int target = selectedTransferSlot == SavePackage::AllSlots ? slot.index : selectedTransferSlot;
		std::error_code error;
		if (std::filesystem::exists(saveTransferRoot() / SavePackage::slotDirectory(target), error)) return true;
	}
	return false;
}

void ResourceSelectScene::finishSaveTransfer(const GameLoading::ExclusiveLoadingCompletion& completion)
{
	resourceInstallDialogState = completion.taskResult.succeeded()
		? ResourceInstallDialogState::Completed : ResourceInstallDialogState::Failed;
	resourceInstallDialogMessage = completion.taskResult.message;
	if (completion.taskResult.succeeded() && saveTransferAction == SaveTransferAction::Inspect)
	{
		pendingSavePackage = std::make_shared<SavePackage::Package>(std::move(resourceInstallWorkerResult->savePackage));
		if (!resolveSaveTransferGame(pendingSavePackage->game().saveNamespace, pendingSavePackage->game().id))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"请先安装存档对应的游戏或 MOD：" + pendingSavePackage->game().name;
		}
		else if (!SavePackage::compatible(*pendingSavePackage, saveTransferGame, resourceInstallDialogMessage))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
		}
		else
		{
			selectedTransferSlot = SavePackage::AllSlots;
			if (pendingSavePackage->slots().size() == 1)
			{
				selectedTransferSlot = pendingSavePackage->slots().front().index;
				const auto occupied = SavePackage::listSlots(saveTransferRoot());
				for (int slot = 1; slot <= 7; ++slot)
				{
					if (std::find(occupied.begin(), occupied.end(), slot) == occupied.end())
					{
						selectedTransferSlot = slot;
						break;
					}
				}
			}
			resourceInstallDialogState = ResourceInstallDialogState::ConfirmingSaveImport;
			saveImportWillOverwrite = saveImportOverwrites();
		}
	}
	else if (completion.taskResult.succeeded())
	{
		resourceInstallDialogMessage = saveTransferAction == SaveTransferAction::Export
			? u8"存档包已保存到所选位置，可发送到其他设备导入。"
			: u8"存档导入完成，进入对应游戏后即可读取。";
		pendingSavePackage.reset();
		saveNamespaceEntries = ResourceManager::instance().listSaveNamespaces();
		selectedSaveNamespaceIndex = 0;
		for (int index = 0; index < static_cast<int>(saveNamespaceEntries.size()); ++index)
		{
			if (OnlineUpdate::foldGameId(saveNamespaceEntries[index].saveNamespace) ==
				OnlineUpdate::foldGameId(saveTransferGame.saveNamespace))
			{
				selectedSaveNamespaceIndex = index;
				break;
			}
		}
	}
	else if (resourceInstallDialogMessage.empty()) resourceInstallDialogMessage = u8"存档操作未完成，请重试。";
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-primary");
	updateFocusPresentation();
}
