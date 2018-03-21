/*
	created by Upwinded@www.upwinded.com.

	Special thanks to Scarsty(SunTY, Weyl, BT, SB500), XiaoShiDaoJian,DaWuXiaLunTan(www.dawuxia.net),JianXiaQingYuanTieBa@tieba.baidu.com.

	The source codes are distributed under zlib license, with two additional clauses:
	1.Full right of the codes is granted if they are used in non-KYS related and non-SHF related games.
	2.If the codes are used in KYS related or SHF related games, the game itself shall not involve any sort of profit making aspect.
	(KYS means Kam Yung's Stories, and SHF means Sword Heroes' Fate.)
*/

#include "shf-cpp.h"

#ifdef _WIN32
#ifndef _CONSOLE_MODE
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif // _CONSOLE_MODE
#endif // _WIN32

int main(int argc, char* argv[])
{
	Game game;
	return game.run();
}
