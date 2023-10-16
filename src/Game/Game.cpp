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
	GameLog::write("Game Run!\n");
	GameLog::write("Init Game Engine\n");
	int w = DEFAULT_WINDOW_WIDTH, h = DEFAULT_WINDOW_HEIGHT;
#ifdef __MOBILE__
	w = 1200;
	h = 600;
	Config::setDefaultWindowSize(w, h);
#endif
	Config::load();
	if (Engine::getInstance() == nullptr) { return -1; }

	Config::getWindowSize(w, h);

	if (Engine::getInstance()->init(gameTitle, w, h, Config::fullScreenMode, Config::fullScreenSolutionMode) != initOK)
	{
		return -1;
	}
	GameLog::write("Init Game Font\n");

	//设置字体
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(gameFont, s);
	if (s != nullptr && len > 0)
	{
		Engine::getInstance()->setFontFromMem(s, len);
	}
	GameLog::write("Init Cursor\n");
	//设置鼠标样式
	auto cursorImage = IMP::createIMPImage("mpc\\ui\\common\\mouse.mpc", false);
	Engine::getInstance()->setMouseFromImpImage(cursorImage);

	GameLog::write("Begin Game Title\n");
	PElement title = std::make_shared<Title>();
	Element::setAsTop(title);
	int ret = title->run();
	Element::removeFromTop(title);

	//GameLog::write("Release Engine!\n");
	//Engine::getInstance()->destroyEngine();
	GameLog::write("Game End!\n");
	return ret;
}
