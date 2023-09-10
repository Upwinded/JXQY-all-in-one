#include "Game.h"
#include "Config/Config.h"


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
#ifdef _MOBILE
	w = 1200;
	h = 600;
	Config::setDefaultWindowSize(w, h);
#endif
	Config::load();
	if (Engine::getInstance() == nullptr) { return -1; }

	Engine::getInstance()->setWindowDisplayMode(Config::canChangeDisplayMode);

	Config::getWindowSize(w, h);

	if (Engine::getInstance()->init(gameTitle, w, h, Config::fullScreen) != initOK)
	{
		return -1;
	}
	printf("Init Game Font\n");
	//设置字体，s的空间交给引擎处理，无需delete
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(gameFont, s);
	if (s != nullptr && len > 0)
	{
		Engine::getInstance()->setFontFromMem(s, len);
	}
	printf("Init Cursor\n");
	//设置鼠标样式
	auto cursorImage = IMP::createIMPImage("mpc\\ui\\common\\mouse.mpc");
	Engine::getInstance()->setMouseFromImpImage(cursorImage);
	IMP::clearIMPImage(cursorImage);
	//delete cursorImage;

	printf("Begin Game Title\n");
	Title * title = new Title;
	int ret = title->run();
	delete title;
	//printf("Release Engine!\n");
	//Engine::getInstance()->destroyEngine();
	printf("Game End!\n");
	return ret;
}
