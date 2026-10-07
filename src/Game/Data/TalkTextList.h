#pragma once
#include <string>
#include <vector>
#include <regex>
#include "../../File/File.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"

struct TalkTextDetail
{
	int index = 0;
	int portraitIndex = 0;
	std::string text = "";
};

class TalkTextList
{
public:
	void load(const std::string& fileName = "")
	{
		list.clear();
		std::unique_ptr<char[]> s;
		int len = 0;
		if (!fileName.empty())
		{
			len = File::readFile(fileName, s);
		}
		else
		{
			// Keep overlay/MOD precedence before choosing a path within each root.
			File::visitReadableResources({ "script/common/talkindex.txt", "talkindex.txt" },
				[&](const std::string& path, std::unique_ptr<char[]>& data, int length)
				{
					s = std::move(data);
					len = length;
					if (path == "talkindex.txt")
					{
						GameLog::write("TalkTextList: using legacy table %s", path.c_str());
					}
					return true;
				});
		}
		if (s == nullptr || len <= 0)
		{
			return;
		}
		std::string content(s.get(), len);
		auto lines = convert::splitString(content, "\n");
		std::regex reg(R"(^\[(\d+),(\d+)\](.*)$)");
		for (auto& line : lines)
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			std::smatch match;
			if (!std::regex_search(line, match, reg))
			{
				continue;
			}
			TalkTextDetail detail;
			if (!convert::parseInteger(match[1].str(), detail.index) ||
				!convert::parseInteger(match[2].str(), detail.portraitIndex))
			{
				continue;
			}
			detail.text = match[3].str();
			list.push_back(detail);
		}
	}

	TalkTextDetail* getTextDetail(int index)
	{
		for (size_t i = 0; i < list.size(); i++)
		{
			if (list[i].index == index)
			{
				return &list[i];
			}
			if (list[i].index > index)
			{
				break;
			}
		}
		return nullptr;
	}

	std::string getText(int index)
	{
		auto* detail = getTextDetail(index);
		if (detail != nullptr)
		{
			return detail->text;
		}
		return "";
	}

	std::vector<TalkTextDetail> getTextDetails(int from, int to)
	{
		std::vector<TalkTextDetail> result;
		size_t i = 0;
		for (; i < list.size(); i++)
		{
			if (list[i].index == from)
			{
				break;
			}
		}
		if (i >= list.size())
		{
			return result;
		}
		for (size_t j = i; j < list.size(); j++)
		{
			if (list[j].index <= to)
			{
				result.push_back(list[j]);
			}
			else
			{
				break;
			}
		}
		return result;
	}

	std::vector<TalkTextDetail> list;
};
