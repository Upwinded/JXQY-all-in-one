#pragma once
#include "../../Types/ElementTypes.h"

// Templates use parent-local units; level offsets are relative to the head's bottom-right.
struct PartnerHeadLayout
{
    static constexpr const char* MenuFile = "ini/ui/partnerhead/partnerhead.menu.ini";
    Rect head = {5, 5, 32, 32};
    Rect level = {3, -12, 64, 12};
    int spacingY = 2;
    int font = 10;
    unsigned int color = 0xFFFFFFFF;
    bool stretch = false;

    Rect headRectangle(int nativeWidth, int nativeHeight, int y) const
    {
        return {head.x, y, stretch ? head.w : nativeWidth, stretch ? head.h : nativeHeight};
    }
    Rect levelRectangle(const Rect& portrait) const
    {
        return {portrait.x + portrait.w + level.x, portrait.y + portrait.h + level.y, level.w, level.h};
    }
};
