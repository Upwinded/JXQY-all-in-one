#pragma once
#include "../../Component/Panel.h"
class MemoMenu :
	public Panel
{
public:
	MemoMenu();
	virtual ~MemoMenu();

	virtual void init() override;

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<MemoText> memoText = nullptr;
	std::shared_ptr<Scrollbar> scrollbar = nullptr;

	void reFresh();
	void reset();
	void reRange(int max);

private:
	int position = -1;

	void freeResource();

	virtual void onEvent();
};

