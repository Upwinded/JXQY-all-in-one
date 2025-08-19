#include "MainScene.h"

MainScene::MainScene(int idx)
{
	gameIndex = idx;
	init();
}

MainScene::~MainScene()
{
	removeAllChild();
	game = nullptr;
}

void MainScene::init()
{
	game = std::make_shared<GameManager>();
	addChild(game);
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
