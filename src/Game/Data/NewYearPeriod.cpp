#include "NewYearPeriod.h"

namespace
{
int getDaysInMonth(int year, int month)
{
	static constexpr int DaysInMonth[] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31,
	};
	if (month < 1 || month > 12)
	{
		return 0;
	}
	if (month == 2 && NewYearPeriod::isLeapYear(year))
	{
		return 29;
	}
	return DaysInMonth[month - 1];
}
}

namespace NewYearPeriod
{
bool isLeapYear(int year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool isValidLocalDate(const LocalDate& date)
{
	if (date.year < 1)
	{
		return false;
	}
	const int daysInMonth = getDaysInMonth(date.year, date.month);
	return date.day >= 1 && date.day <= daysInMonth;
}

bool contains(const LocalDate& date)
{
	return isValidLocalDate(date) && (date.month == 1 || date.month == 2);
}

bool tryGetLocalDate(std::time_t time, LocalDate& date)
{
	std::tm localTime = {};
#ifdef _WIN32
	if (localtime_s(&localTime, &time) != 0)
	{
		return false;
	}
#else
	if (localtime_r(&time, &localTime) == nullptr)
	{
		return false;
	}
#endif
	date.year = localTime.tm_year + 1900;
	date.month = localTime.tm_mon + 1;
	date.day = localTime.tm_mday;
	return isValidLocalDate(date);
}

bool contains(std::chrono::system_clock::time_point time)
{
	LocalDate date;
	return tryGetLocalDate(std::chrono::system_clock::to_time_t(time), date)
		&& contains(date);
}

bool containsCurrentLocalDate()
{
	return contains(std::chrono::system_clock::now());
}
}
