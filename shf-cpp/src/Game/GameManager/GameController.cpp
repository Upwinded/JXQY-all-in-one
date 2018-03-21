#include "GameController.h"



GameController::GameController()
{
	priority = epMap;
	result = erNone;
}


GameController::~GameController()
{
	removeAllChild();
}
