#pragma once
#include "../../Component/Component.h"
#include "../Data/Goods.h"
#include "../Data/Magic.h"
#include "UIFocusManager.h"

class ToolTip :
	public ConfigDrivenPanel
{
public:
	ToolTip();
	virtual ~ToolTip();

	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<Label> name = nullptr;
	std::shared_ptr<Label> cost = nullptr;
	std::shared_ptr<Label> intro1 = nullptr;
	std::shared_ptr<Label> intro2 = nullptr;
	std::shared_ptr<Label> magicIntro = nullptr;

	void setGoods(std::shared_ptr<Goods> goods, bool selling = false);
	void setMagic(std::shared_ptr<Magic> magic, int level);
	void showForOwner(PElement owner);
	void placeNearElement(const PElement& anchorElement);
	void hide();
	void runDetails();
	bool turnPage(UIAction action);
	virtual void init() override;

private:
	enum class LayoutProfile
	{
		Jxqy2,
		Yycs,
		Xjxqy,
		Qingyu,
	};

	std::weak_ptr<Element> owner;
	LayoutProfile layoutProfile = LayoutProfile::Jxqy2;
	bool currentContentIsMagic = false;
	bool expanded = false;
	int page = 0;
	std::vector<std::string> lines;
	std::shared_ptr<MemoText> body;
	UIFocusManager detailFocus;
	Rect nameLayoutRect = { 0, 0, 0, 0 };
	Rect costLayoutRect = { 0, 0, 0, 0 };
	Rect intro1LayoutRect = { 0, 0, 0, 0 };
	Rect intro2LayoutRect = { 0, 0, 0, 0 };

	void clearContent();
	void finishContentLayout();
	void reflowXjxqy();
	void updatePage();
	void placeNearMouse();
	void freeResource();
	virtual void onDraw() override;
	virtual void onUpdate() override;
	void onEvent() override;
	void onRun() override;
	bool onHandleEvent(AEvent& event) override;
	bool onHandleUIAction(UIAction action) override;
	void onWindowResize(int width, int height) override;
};
