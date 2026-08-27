#pragma once
#include "../../Component/VideoPlayer.h"
class VideoPage :
	public VideoPlayer
{
public:
	VideoPage();
	VideoPage(const std::string & fileName);
	virtual ~VideoPage();
};


