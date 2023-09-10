#pragma once
#include "../../Component/Component.h"
class VideoPage :
	public VideoPlayer
{
public:
	VideoPage();
	VideoPage(const std::string & fileName);
	virtual ~VideoPage();
};

