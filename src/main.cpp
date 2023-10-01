/*
	created by Upwinded@www.upwinded.com.

	Special thanks to Scarsty(SunTY, Weyl, BT, SB500), XiaoShiDaoJian, DaWuXiaLunTan(http://www.txdx.net), JianXiaQingYuanTieBa@tieba.baidu.com(https://tieba.baidu.com/f?kw=%E5%89%91%E4%BE%A0%E6%83%85%E7%BC%98&ie=utf-8).

	The source codes are distributed under zlib license, with two additional clauses:
	1.Full right of the codes is granted if they are used in non-KYS related and non-SHF related games.
	2.If the codes are used in KYS related or SHF related games, the game itself shall not involve any sort of profit making aspect.
	(KYS means Kam Yung's Stories, and SHF means Sword Heroes' Fate.)
*/
#include "Game/Game.h"


#if defined( __ANDROID__ )
#include <jni.h>
int SDL_main(int argc, char* argv[])
#elif defined(__IPHONEOS__)
int main(int argc, char* argv[])
{
    return SDL_UIKitRunApp(argc, argv, SDL_main);
}
int SDL_main(int argc, char* argv[])
#elif defined(_WIN32) && !defined(_DEBUG)
int SDL_main(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
	std::string log_on = "-lf";
	for (int i = 0; i < argc; i++)
	{
		std::string par = argv[i];
		if (par.compare(log_on) == 0)
		{
			GameLog::use_log_file = true;
			GameLog::write("Use Log File");
		}
	}
	Game game;
    auto ret = game.run();
#ifdef __IPHONEOS__
    exit(ret);
#else
    return ret;
#endif
}
