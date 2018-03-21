#include "MainScene.h"

MainScene::MainScene(int idx)
{
	game = new GameManager;
	addChild(game);
	gameIndex = idx;
}

MainScene::~MainScene()
{
	removeAllChild();
	delete game;
	game = NULL;
}

bool MainScene::onInitial()
{
	game->gameIndex = gameIndex;
	//game->loadGameWithThread(gameIndex);
	return true; 
}

void MainScene::onUpdate()
{
	if (game == NULL)
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
