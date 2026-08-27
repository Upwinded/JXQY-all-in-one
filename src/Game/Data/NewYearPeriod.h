#pragma once

#include <chrono>
#include <ctime>

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
}
