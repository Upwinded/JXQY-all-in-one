#pragma once

#include "../../Types/ElementTypes.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// Design coordinates for the existing DiceGame manifest, not game rules.
struct DiceGameLayout
{
    static constexpr const char* File = "ini/ui/dicegame/window.ini";
    enum class Geometry { None, Point, Rectangle };
    struct Role
    {
        const char* id;
        const char* label;
        int group;
        Geometry geometry;
        Rect rectangle;
        const char* imageKey = "";
        const char* imageSection = "Window";
        int font = 0;
        unsigned int color = 0xffffffff;
        bool talk = false;
    };
    static const std::vector<Role>& roles()
    {
        static const std::vector<Role> entries = {
            {"Frame", "背景", 0, Geometry::Rectangle, {0,0,549,300}, "Frame"},
            {"PlayerPortrait", "玩家头像", 1, Geometry::Rectangle, {16,48,70,70}, "PlayerPortrait"},
            {"NpcPortrait", "对方头像", 1, Geometry::Rectangle, {460,46,70,70}, "NpcPortrait"},
            {"PlayerTalk", "玩家对话底图", 1, Geometry::Rectangle, {84,51,180,61}, "PlayerTalk"},
            {"NpcTalk", "对方对话底图", 1, Geometry::Rectangle, {295,51,180,61}, "NpcTalk"},
            {"Nameplate", "双方骰盘共用图片", 2, Geometry::None, {}, "Nameplate"},
            {"PlayerTray", "玩家骰盘", 2, Geometry::Rectangle, {15,167,167,37}},
            {"NpcTray", "对方骰盘", 2, Geometry::Rectangle, {369,170,167,37}},
            {"Silver", "银两图", 0, Geometry::Rectangle, {15,273,18,11}, "Silver"},
            {"Versus", "对局中图案", 2, Geometry::Rectangle, {218,151,120,74}, "Versus"},
            {"ResultWin", "获胜图案", 2, Geometry::Rectangle, {222,133,106,106}, "ResultWin"},
            {"ResultLose", "落败图案", 2, Geometry::Rectangle, {222,133,106,106}, "ResultLose"},
            {"ResultTie", "平局图案", 2, Geometry::Rectangle, {222,133,106,106}, "ResultTie"},
            {"PlayerDice1", "玩家骰子一", 2, Geometry::Rectangle, {35,167,37,37}},
            {"PlayerDice2", "玩家骰子二", 2, Geometry::Rectangle, {81,167,37,37}},
            {"PlayerDice3", "玩家骰子三", 2, Geometry::Rectangle, {127,167,37,37}},
            {"NpcDice1", "对方骰子一", 2, Geometry::Rectangle, {388,169,37,37}},
            {"NpcDice2", "对方骰子二", 2, Geometry::Rectangle, {434,169,37,37}},
            {"NpcDice3", "对方骰子三", 2, Geometry::Rectangle, {480,169,37,37}},
            {"Face1", "一点骰面", 2, Geometry::None, {}, "Face1", "Dice"},
            {"Face2", "二点骰面", 2, Geometry::None, {}, "Face2", "Dice"},
            {"Face3", "三点骰面", 2, Geometry::None, {}, "Face3", "Dice"},
            {"Face4", "四点骰面", 2, Geometry::None, {}, "Face4", "Dice"},
            {"Face5", "五点骰面", 2, Geometry::None, {}, "Face5", "Dice"},
            {"Face6", "六点骰面", 2, Geometry::None, {}, "Face6", "Dice"},
            {"Title", "标题（居中偏移）", 0, Geometry::Point, {0,2,0,0}, "", "Window", 18},
            {"CloseSymbol", "关闭符号", 3, Geometry::Point, {525,5,0,0}, "", "Window", 18},
            {"PlayerName", "玩家姓名", 1, Geometry::Point, {17,100,0,0}, "", "Window", 12, 0xff333333},
            {"NpcName", "对方姓名", 1, Geometry::Point, {471,100,0,0}, "", "Window", 12, 0xff333333},
            {"Stake", "注额文字", 0, Geometry::Point, {36,267,0,0}, "", "Window", 12},
            {"PlayerText", "玩家对话正文", 1, Geometry::Rectangle, {100,60,153,42}, "", "Window", 12, 0xff666666, true},
            {"NpcText", "对方对话正文", 1, Geometry::Rectangle, {308,60,153,42}, "", "Window", 12, 0xff666666, true},
            {"Start", "开始按钮", 3, Geometry::Rectangle, {443,263,82,28}},
            {"Bet", "下注按钮", 3, Geometry::Rectangle, {351,263,82,28}},
            {"Open", "开盅按钮", 3, Geometry::Rectangle, {260,262,82,29}},
            {"Close", "关闭按钮", 3, Geometry::Rectangle, {521,4,24,24}},
        };
        return entries;
    }
    static const Role& role(const std::string& id)
    {
        for (const auto& entry : roles()) if (entry.id == id) return entry;
        return roles().front();
    }
    static std::vector<std::string> fields(const Role& entry)
    {
        std::vector<std::string> result;
        if (*entry.imageKey) result.push_back("Image");
        if (entry.geometry != Geometry::None) result.insert(result.end(), {"Left", "Top"});
        if (entry.geometry == Geometry::Rectangle) result.insert(result.end(), {"Width", "Height"});
        if (entry.font) result.insert(result.end(), {"Font", "Color"});
        if (entry.talk) result.push_back("LineGap");
        return result;
    }
    static std::string key(const Role& entry, const std::string& field)
    {
        return field == "Image" ? std::string(entry.imageSection) + "/" + entry.imageKey
                                : std::string(entry.id) + "/" + field;
    }
    static bool integer(const std::string& text, int& value, int minimum = INT_MIN)
    {
        try
        {
            std::size_t end = 0;
            const auto parsed = std::stoll(text, &end, 0);
            if (text.find_first_not_of(" \t\r\n", end) != std::string::npos || parsed < minimum || parsed > INT_MAX) return false;
            value = static_cast<int>(parsed);
            return true;
        }
        catch (const std::exception&) { return false; }
    }
    static bool color(const std::string& text, unsigned int& value)
    {
        try
        {
            if (text.find('-') != std::string::npos) return false;
            std::size_t end = 0;
            const auto parsed = std::stoull(text, &end, 0);
            if (text.find_first_not_of(" \t\r\n", end) != std::string::npos || parsed > 0xffffffffULL) return false;
            value = static_cast<unsigned int>(parsed);
            return true;
        }
        catch (const std::exception&) { return false; }
    }
    static bool valid(const std::string& field, const std::string& text)
    {
        if (text.empty() || field == "Image") return true;
        if (field == "Color") { unsigned int value; return color(text, value); }
        int value;
        return integer(text, value, field == "Font" || field == "Width" || field == "Height" ? 1 : field == "LineGap" ? 0 : INT_MIN);
    }
    std::map<std::string, std::string> values;
    std::string raw(const Role& entry, const std::string& field) const
    {
        const auto it = values.find(key(entry, field));
        return it == values.end() ? std::string() : it->second;
    }
    int number(const std::string& id, const std::string& field, int fallback) const
    {
        const auto text = raw(role(id), field);
        int value;
        return !text.empty() && valid(field, text) && integer(text, value) ? value : fallback;
    }
    Rect rectangle(const std::string& id, bool mini = false) const
    {
        Rect value = role(id).rectangle;
        if (mini)
        {
            if (id == "Start") value = {443,247,82,44};
            if (id == "Bet") value = {351,247,82,44};
            if (id == "Open") value = {260,247,82,44};
            if (id == "Close") value = {505,0,44,44};
        }
        value.x = number(id, "Left", value.x); value.y = number(id, "Top", value.y);
        value.w = number(id, "Width", value.w); value.h = number(id, "Height", value.h);
        return value;
    }
    int font(const std::string& id, float scale) const
    {
        const auto& entry = role(id);
        return std::max(entry.font == 18 ? 13 : 10, static_cast<int>(std::min<double>(INT_MAX, std::round(number(id, "Font", entry.font) * static_cast<double>(scale)))));
    }
    unsigned int tint(const std::string& id) const
    {
        const auto& entry = role(id); unsigned int value;
        return color(raw(entry, "Color"), value) ? value : entry.color;
    }
};
