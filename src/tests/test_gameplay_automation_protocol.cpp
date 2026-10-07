#include "GameplayAutomation/AutomationProtocol.h"
#include "Launch/GameLaunchArguments.h"
#include <iostream>

using namespace GameplayAutomation;

int main()
{
	int failures = 0;
	auto check = [&](bool result, const char* description)
	{
		if (!result) { std::cerr << description << '\n'; ++failures; }
	};
	for (const auto& text : {"{\"id\":1,\"id\":2}", "{\"id\":01}", "[true,]", "\"\\ud800\""})
	{
		Value value;
		StrictJson::StrictJsonParser parser(text);
		check(!parser.parse(value), "malformed/duplicate JSON must be rejected");
	}
	auto sample = object();
	sample.objectValues = {{"text", string(u8"主角家\n确认\"\\")}, {"number", number(9007199254740991LL)}};
	const auto bytes = serialize(sample);
	Value decoded;
	StrictJson::StrictJsonParser parser(bytes);
	check(parser.parse(decoded), "serialized state parses");
	check(textMember(decoded, "text") == u8"主角家\n确认\"\\", "UTF-8 text round trips");
	check(integer(member(decoded, "number")) == 9007199254740991LL, "integer precision is preserved");
	try { fields(decoded, {"text"}); check(false, "unknown fields rejected"); } catch (const std::exception&) {}
	try { integer(string("1")); check(false, "string is not an integer"); } catch (const std::exception&) {}
	const char* unauthorized[]{"game", "--automation-pipe", "probe", "--user-data-root", "isolated"};
	check(!GameLaunch::parseArguments(5, unauthorized, true).succeeded(), "pipe requires runtime authorization");
	const char* noRoot[]{"game", "--enable-automation-hooks", "--automation-pipe", "probe"};
	check(!GameLaunch::parseArguments(4, noRoot, true).succeeded(), "pipe requires explicit user root");
	const char* injection[]{"game", "--enable-automation-hooks", "--automation-pipe", "probe", "--user-data-root", "isolated", "--startup-int", "x=1"};
	check(!GameLaunch::parseArguments(8, injection, true).succeeded(), "playthrough cannot mix scenario injection");
	const char* valid[]{"game", "--automation-pipe", "probe", "--user-data-root", "isolated", "--enable-automation-hooks"};
	auto dateArguments = [&](const char* date)
	{
		const char* arguments[]{"game", "--automation-local-date", date, "--automation-pipe", "probe",
			"--user-data-root", "isolated", "--enable-automation-hooks"};
		return GameLaunch::parseArguments(8, arguments, true);
	};
#if defined(_WIN32) && defined(JXQY_ENABLE_AUTOMATION_HOOKS)
	check(GameLaunch::parseArguments(6, valid, true).succeeded(), "authorization order is independent");
	const auto dated = dateArguments("2026-01-01");
	check(dated.succeeded() && dated.legacy.automationLocalDate.year == 2026 &&
		dated.legacy.automationLocalDate.month == 1 && dated.legacy.automationLocalDate.day == 1,
		"isolated authorized session accepts an exact process date");
	check(dateArguments("2024-02-29").succeeded(), "leap day accepted");
	for (const auto* date : {"2026-02-29", "0000-01-01", "2026-13-01", "2026-04-31", "2026-1-01", "2026-01-01x", "2026-+1-01"})
	{
		check(!dateArguments(date).succeeded(), "invalid dates rejected");
	}
	const char* noPipeDate[]{"game", "--automation-local-date", "2026-01-01", "--enable-automation-hooks", "--user-data-root", "isolated"};
	check(!GameLaunch::parseArguments(6, noPipeDate, true).succeeded(), "date requires isolated pipe session");
	const char* unauthorizedDate[]{"game", "--automation-local-date", "2026-01-01", "--automation-pipe", "probe", "--user-data-root", "isolated"};
	check(!GameLaunch::parseArguments(7, unauthorizedDate, true).succeeded(), "date requires hooks authorization");
	const char* duplicateDate[]{"game", "--automation-local-date", "2026-01-01", "--automation-local-date", "2026-01-02", "--enable-automation-hooks", "--automation-pipe", "probe", "--user-data-root", "isolated"};
	check(!GameLaunch::parseArguments(10, duplicateDate, true).succeeded(), "duplicate dates rejected");
	const char* noRootDate[]{"game", "--automation-local-date", "2026-01-01", "--enable-automation-hooks", "--automation-pipe", "probe"};
	check(!GameLaunch::parseArguments(6, noRootDate, true).succeeded(), "date requires explicit root");
	const char* missingDate[]{"game", "--automation-local-date"};
	check(!GameLaunch::parseArguments(2, missingDate, true).succeeded(), "missing date rejected");
	using namespace NewYearPeriod;
	check(setAutomationLocalDate({2026, 1, 1}) && containsCurrentLocalDate(), "process override enables festival predicate");
	check(!setAutomationLocalDate({2026, 2, 29}) && containsCurrentLocalDate(), "invalid override preserves valid date");
	check(setAutomationLocalDate({2026, 3, 1}) && !containsCurrentLocalDate(), "process override disables festival predicate");
	clearAutomationLocalDate();
	check(containsCurrentLocalDate() == contains(std::chrono::system_clock::now()), "cleared override restores system date");
#else
	check(!GameLaunch::parseArguments(6, valid, true).succeeded(), "production or unsupported platform cannot start pipe");
	check(!dateArguments("2026-01-01").succeeded(), "production or unsupported platform rejects process date");
#endif
	return failures ? 1 : 0;
}
