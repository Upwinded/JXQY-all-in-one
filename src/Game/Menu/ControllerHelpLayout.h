#pragma once

#include "../../Types/ElementTypes.h"
#include <algorithm>
#include <climits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct ControllerHelpLayout
{
    static constexpr const char* File = "ini/ui/controller_help/window.ini";
    static constexpr const char* DefaultImage = "image/ui/controller_help/controller-help.png";
    static std::vector<std::string> keys()
    {
        return {"image","outermargin","contentpadding","titlefontsize","textfontsize","linespacing","columngap",
            "backgroundcolor","panelcolor","accentcolor","titlecolor","sectioncolor","textcolor","mutedtextcolor"};
    }
    static bool isColor(const std::string& key) { return key.find("color") != std::string::npos; }
    static bool integer(const std::string& text, int& result, int minimum, int maximum)
    {
        try
        {
            std::size_t end=0;const auto value=std::stoll(text,&end,0);
            if(text.find_first_not_of(" \t\r\n",end)!=std::string::npos||value<minimum||value>maximum)return false;
            result=static_cast<int>(value);return true;
        }
        catch(const std::exception&) { return false; }
    }
    static bool color(const std::string& text, unsigned int& result)
    {
        if(text.empty())return false;
        if(text.find(',')==std::string::npos)
        {
            try
            {
                if(text.find('-')!=std::string::npos)return false;
                std::size_t end=0;const auto value=std::stoull(text,&end,0);
                if(text.find_first_not_of(" \t\r\n",end)!=std::string::npos||value>0xffffffffULL)return false;
                result=static_cast<unsigned int>(value);if(value<=0xffffff)result|=0xff000000;return true;
            }
            catch(const std::exception&) { return false; }
        }
        std::istringstream stream(text);std::string part;int values[4]={0,0,0,255},count=0;
        while(std::getline(stream,part,','))
        {
            if(count==4||!integer(part,values[count],0,255))return false;++count;
        }
        if((count!=3&&count!=4)||text.back()==',')return false;
        result=(static_cast<unsigned int>(values[3])<<24)|(values[0]<<16)|(values[1]<<8)|values[2];return true;
    }
    static bool valid(const std::string& key,const std::string& text)
    {
        if(text.empty()||key=="image")return true;
        if(isColor(key)){unsigned int value=0;return color(text,value);}
        int value=0;const bool font=key=="titlefontsize"||key=="textfontsize";
        return integer(text,value,font?1:0,font?256:100000);
    }
    std::map<std::string,std::string> values;
    std::vector<std::string> invalid() const
    {
        std::vector<std::string> result;for(const auto& key:keys())
        {auto it=values.find(key);if(it!=values.end()&&!valid(key,it->second))result.push_back(key);}return result;
    }
    std::string image() const
    {
        const auto it=values.find("image");return it==values.end()||it->second.empty()?DefaultImage:it->second;
    }
    int number(const char* key,int fallback) const
    {
        const auto it=values.find(key);int result=0;if(it==values.end()||it->second.empty()||!valid(key,it->second))return fallback;
        integer(it->second,result,0,100000);return result;
    }
    unsigned int tint(const char* key,unsigned int fallback) const
    {
        const auto it=values.find(key);unsigned int result=0;return it!=values.end()&&color(it->second,result)?result:fallback;
    }
    struct Geometry
    {
        int width=1,height=1,outer=12,padding=14,titleFont=20,textFont=13,lineHeight=16,columnGap=20;
        int contentLeft=26,contentTop=64,titleTop=22,subtitleTop=46,footerTop=64,menuLeft=26,menuTop=64;
        bool twoColumns=false;
        Rect panel{};
        unsigned int background=0xff090c12,panelColor=0xff191f2b,accent=0xffdaae59,title=0xffffd88a,section=0xffffc76a,text=0xfff2f2f2,muted=0xffbfc5ce;
        Rect imageRectangle(int sourceWidth,int sourceHeight) const
        {
            if(sourceWidth<=0||sourceHeight<=0)return {};
            const int maximumWidth=std::max(1,width-contentLeft*2),maximumHeight=std::max(1,footerTop-contentTop-8);
            const int imageWidth=std::max(1,static_cast<int>(std::min<long long>(maximumWidth,static_cast<long long>(maximumHeight)*sourceWidth/sourceHeight)));
            const int imageHeight=std::max(1,static_cast<int>(std::min<long long>(INT_MAX,static_cast<long long>(imageWidth)*sourceHeight/sourceWidth)));
            return {(width-imageWidth)/2,contentTop+std::max(0,(maximumHeight-imageHeight)/2),imageWidth,imageHeight};
        }
    };
    Geometry resolve(int width,int height) const
    {
        Geometry g;g.width=std::max(1,width);g.height=std::max(1,height);g.twoColumns=g.width>=640;
        g.outer=number("outermargin",std::max(12,std::min(g.width,g.height)/28));g.padding=number("contentpadding",std::max(14,g.outer/2));
        g.titleFont=number("titlefontsize",std::clamp(g.height/20,20,30));
        g.textFont=number("textfontsize",g.twoColumns?std::clamp(g.height/36,13,18):std::clamp(g.height/48,11,16));
        g.lineHeight=g.textFont+number("linespacing",std::max(3,g.textFont/3));g.columnGap=number("columngap",std::max(20,g.width/28));
        g.panel={g.outer,g.outer,std::max(1,g.width-g.outer*2),std::max(1,g.height-g.outer*2)};
        g.contentLeft=g.outer+g.padding;g.contentTop=g.outer+g.titleFont+g.lineHeight*2;g.titleTop=g.outer+std::max(10,g.outer/3);
        g.subtitleTop=g.titleTop+g.titleFont+4;g.footerTop=std::max(g.contentTop,g.height-g.outer-g.lineHeight-6);
        g.menuLeft=g.twoColumns?g.contentLeft+std::max(1,(g.width-g.contentLeft*2-g.columnGap)/2)+g.columnGap:g.contentLeft;
        g.menuTop=g.twoColumns?g.contentTop:g.contentTop+g.lineHeight*20;
        g.background=tint("backgroundcolor",g.background);g.panelColor=tint("panelcolor",g.panelColor);g.accent=tint("accentcolor",g.accent);
        g.title=tint("titlecolor",g.title);g.section=tint("sectioncolor",g.section);g.text=tint("textcolor",g.text);g.muted=tint("mutedtextcolor",g.muted);return g;
    }
};
