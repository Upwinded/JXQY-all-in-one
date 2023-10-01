#include "MainScene.h"

MainScene::MainScene(int idx)
{
	game = std::make_shared<GameManager>();
	addChild(game);
	gameIndex = idx;
}

MainScene::~MainScene()
{
	removeAllChild();
	game = nullptr;
}

bool MainScene::onInitial()
{
	if (game == nullptr)
	{
		return true;
	}
	game->gameIndex = gameIndex;

	return true; 
}

void MainScene::onUpdate()
{
	if (game == nullptr)
	{
		running = false;
		return;
	}
	unsigned int ret = game->getResult();
	if (((ret & erExit) != 0) || ((ret & erOK) != 0))
	{
		running = false;
	}
}
