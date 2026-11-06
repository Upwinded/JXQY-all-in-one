#pragma once
#include "../../Types/ElementTypes.h"
#include "../../Component/TextLayout.h"
#include <algorithm>
#include <string>
#include <vector>

// These two display modes keep their code defaults when an optional field is absent.
struct SystemNoticeLayout
{
    Rect panel{};
    int font=18, padding=14, lineGap=3;
    unsigned int color=0xFFF4F4F4, background=0xE8181B20, topBorder=0xFF9AA2AE, bottomBorder=0xFF525862;
    static const char* menuFile(bool script)
    {
        return script?"ini/ui/scriptmessages/scriptmessages.menu.ini":"ini/ui/systemnotice/systemnotice.menu.ini";
    }
    static int integer(const std::string& text,int fallback,int minimum=-100000,int maximum=100000)
    {
        if(text.empty()) return fallback;
        try
        {
            std::size_t end=0;const auto value=std::stoll(text,&end,0);
            return text.find_first_not_of(" \t\r\n",end)==std::string::npos&&value>=minimum&&value<=maximum?static_cast<int>(value):fallback;
        }
        catch(const std::exception&) {return fallback;}
    }
    static SystemNoticeLayout defaults(int width,int height,bool script,int font=18,int gap=3)
    {
        SystemNoticeLayout value;value.font=font;value.lineGap=gap;
        const int available=std::max(1,width-32),noticeWidth=std::min(720,available);
        value.panel={std::max(0,(width-noticeWidth)/2),20,noticeWidth,72};
        if(script)
        {
            const int bottom=std::max(1,height-110),top=std::min(bottom-1,104),stack=std::min(15*(font+gap),bottom-top);
            value.panel={std::min(16,std::max(0,width-1)),bottom-stack,available,stack};
        }
        return value;
    }
    int contentWidth() const {return std::max(1,panel.w-padding*2);}
    int lineHeight(bool script) const {return font+(script?lineGap:0);}
    std::vector<TextLayout::ColorTaggedTextLine> lines(const std::vector<std::string>& messages,bool script) const
    {
        std::vector<TextLayout::ColorTaggedTextLine> result;
        const int capacity=TextLayout::charactersPerLineForWidth(contentWidth(),font);
        for(const auto& text:messages)
        {
            auto next=TextLayout::wrapColorTaggedUtf8Text(text,capacity,color);
            result.insert(result.end(),next.begin(),next.end());
        }
        const auto visible=static_cast<std::size_t>(std::max(0,panel.h/lineHeight(script)));
        if(script&&result.size()>visible) result.erase(result.begin(),result.end()-visible);
        return result;
    }
};
