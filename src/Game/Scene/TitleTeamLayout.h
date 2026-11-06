#pragma once
#include "../../Types/ElementTypes.h"
#include <algorithm>

// Fixed display roles; omitted fields keep the responsive legacy layout.
struct TitleTeamLayout
{
    static constexpr const char* MenuFile = "ini/ui/titleteam/titleteam.menu.ini";
    Rect panel{}, body{}, hint{}, close{};
    int font = 26, lineGap = 6, hintFont = 18, closeFont = 20;
    unsigned int panelColor = 0xF5080A0E, color = 0xFFFFFFFF;
    unsigned int hintColor = 0xFFFFFFFF, closeColor = 0xFFFFFFFF, closeBackground = 0xDC232B37;

    static TitleTeamLayout defaults(int width, int height, bool video, const Rect* panelOverride = nullptr)
    {
        TitleTeamLayout value;
        const int panelLeft = video ? std::max(0, width - 440) : 0;
        value.panel = panelOverride ? *panelOverride : Rect{panelLeft, 0, width - panelLeft, height};
        value.font = height < 540 ? 22 : 26;
        value.lineGap = std::max(4, value.font / 4);
        const int left = video ? std::max(20, width - 440 + 30) - panelLeft : std::max(24, width / 14);
        const int right = std::max(20, width / 30);
        const int top = std::max(28, height / 14);
        value.body = {left, top, std::max(1, value.panel.w - left - right),
            std::max(1, value.panel.h - top - std::max(66, height / 7))};
        value.close = {std::max(10, value.panel.w - right - 116), std::max(10, value.panel.h - 48), 116, 34};
        value.hint = {value.body.x, value.close.y + 7, 0, 0};
        return value;
    }
};
