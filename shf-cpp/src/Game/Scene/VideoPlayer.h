#pragma once
#include "../../Component/Component.h"
class VideoPlayer :
	public VideoContainer
{
public:
	VideoPlayer();
	VideoPlayer(const std::string & fileName);
	~VideoPlayer();
};

