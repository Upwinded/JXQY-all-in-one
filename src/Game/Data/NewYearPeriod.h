#pragma once

#include <chrono>
#include <ctime>
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
#include <string>
#endif

namespace NewYearPeriod
{
struct LocalDate
{
	int year = 0;
	int month = 0;
	int day = 0;
};

bool isLeapYear(int year);
bool isValidLocalDate(const LocalDate& date);
bool contains(const LocalDate& date);
bool tryGetLocalDate(std::time_t time, LocalDate& date);
bool contains(std::chrono::system_clock::time_point time);
bool containsCurrentLocalDate();
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
bool tryParseLocalDate(const std::string& text, LocalDate& date);
bool setAutomationLocalDate(const LocalDate& date);
void clearAutomationLocalDate();
#endif
}
