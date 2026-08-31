#pragma once

#include "../../Element/Element.h"

#include <memory>
#include <string>

class SystemNotice :
	public Element
{
public:
	SystemNotice();
	virtual ~SystemNotice();

	void showMessage(
		const std::string& message,
		UTime duration = 7000);
	void dismiss();
	bool hasFont() const;

	std::string currentMessage;

private:
	virtual void onDraw() override;
	virtual void onUpdate() override;
	virtual void onWindowResize(int width, int height) override;

	void updateLayout(int width, int height);
	void refreshTextImage();

	std::unique_ptr<char[]> fontData;
	int fontLength = 0;
	_shared_image textImage = nullptr;
	UTime beginTime = 0;
	UTime showDuration = 7000;
	bool showing = false;
};
