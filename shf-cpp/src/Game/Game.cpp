#include "Game.h"
#include "Config\Config.h"


Game::Game()
{
}

Game::~Game()
{
}

int Game::run()
{	
	printf("Game Run!\n");

	printf("Init Game Engine\n");
	int w = DEFAULT_WINDOW_WIDTH, h = DEFAULT_WINDOW_HEIGHT;
	Engine::getInstance()->setWindowDisplayMode(Config::getInstance()->canChangeDisplayMode);
	Config::getInstance()->getWindowSize(&w, &h);
	if (Engine::getInstance()->init(gameTitle, w, h, Config::getInstance()->fullScreen) != initOK)
	{
		return -1;
	}
	printf("Init Game Font\n");
	//设置字体，s的空间交给引擎处理，无需delete
	char * s = NULL;
	int len = PakFile::readFile(gameFont, &s);
	if (s != NULL && len > 0)
	{
		Engine::getInstance()->setFontFromMem(s, len);
	}
	printf("Init Cursor\n");
	//设置鼠标样式
	IMPImage * mouseImage = IMP::createIMPImage("mpc\\ui\\common\\mouse.mpc");
	Engine::getInstance()->setMouseFromImpImage(mouseImage);
	IMP::clearIMPImage(mouseImage);
	delete mouseImage;

	printf("Begin Game Title\n");
	Title * title = new Title;
	int ret = title->run();
	delete title;
	printf("Release Engine!\n");
	Engine::getInstance()->destroyEngine();
	printf("Game End!\n");
	return ret;
}
