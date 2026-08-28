#include "../Game/Menu/FishGameState.h"

#include <iostream>
#include <utility>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

int main()
{
	bool ok = true;
	FishGameState state;
	state.reset();
	std::vector<std::pair<int, int>> ranges;
	std::vector<int> values = { 2, 18, 10, 10, 9, 2 };
	auto random = [&ranges, &values](int minimum, int maximum)
	{
		ranges.emplace_back(minimum, maximum);
		int value = values.front();
		values.erase(values.begin());
		return value;
	};

	state.startCast();
	ok = check(state.phase() == FishGameState::Phase::Casting, "cast enters the casting phase") && ok;
	unsigned int events = state.update(FishGameState::CastDurationMilliseconds - 1, random);
	ok = check(events == FishGameState::EventNone && ranges.empty(),
		"wait duration is not sampled before the cast animation finishes") && ok;
	events = state.update(1, random);
	ok = check((events & FishGameState::EventWaitStarted) != 0
		&& ranges.back() == std::pair<int, int>(2, 9),
		"cast completion samples the MG-exclusive 2..9 second wait range") && ok;
	events = state.update(2000, random);
	ok = check((events & FishGameState::EventBiteStarted) != 0
		&& state.phase() == FishGameState::Phase::Biting,
		"wait completion starts the two-frame bite animation") && ok;
	events = state.update(FishGameState::BiteDurationMilliseconds, random);
	ok = check((events & FishGameState::EventFishReady) != 0
		&& ranges.back() == std::pair<int, int>(18, 79)
		&& state.tension() == 18,
		"bite completion samples the MG-exclusive 18..79 tension range") && ok;

	events = state.pull(random);
	ok = check(events == FishGameState::EventNone
		&& ranges.back() == std::pair<int, int>(0, 100)
		&& state.tension() == 16 && !state.canReel(),
		"pull decrements tension after checking reel eligibility") && ok;
	events = state.pull(random);
	ok = check(events == FishGameState::EventNone && state.tension() == 14 && !state.canReel(),
		"a pull starting at sixteen does not expose the reel button") && ok;
	events = state.pull(random);
	ok = check((events & FishGameState::EventStruggleStarted) != 0
		&& state.tension() == 12 && state.canReel()
		&& ranges[ranges.size() - 2] == std::pair<int, int>(0, 100)
		&& ranges.back() == std::pair<int, int>(2, 4),
		"roll nine starts a 2..4 second struggle after preserving reel availability") && ok;

	events = state.update(1999, random);
	ok = check(events == FishGameState::EventNone && state.showStruggleButton(),
		"struggle remains active until the full pause duration") && ok;
	events = state.update(1, random);
	ok = check((events & FishGameState::EventStruggleEnded) != 0
		&& state.showPullButton() && state.showReelButton()
		&& state.tip() == "继续拉线~~~",
		"doing nothing during struggle resumes pulling without hiding the available reel") && ok;
	events = state.reel();
	ok = check((events & FishGameState::EventCaught) != 0
		&& state.phase() == FishGameState::Phase::Idle
		&& state.catchCount() == 1,
		"successful reeling resets the same window for another round") && ok;

	FishGameState escapeState;
	escapeState.reset();
	std::vector<int> escapeValues = { 2, 18, 0, 4 };
	auto escapeRandom = [&escapeValues](int, int)
	{
		int value = escapeValues.front();
		escapeValues.erase(escapeValues.begin());
		return value;
	};
	escapeState.startCast();
	escapeState.update(FishGameState::CastDurationMilliseconds + 2000
		+ FishGameState::BiteDurationMilliseconds, escapeRandom);
	escapeState.pull(escapeRandom);
	ok = check(escapeState.showStruggleButton(), "roll zero enters struggle") && ok;
	ok = check((escapeState.makeMistake() & FishGameState::EventEscaped) == 0
		&& escapeState.lostLives() == 1, "first mistake marks one life") && ok;
	ok = check((escapeState.makeMistake() & FishGameState::EventEscaped) == 0
		&& escapeState.lostLives() == 2, "second mistake marks two lives") && ok;
	events = escapeState.makeMistake();
	ok = check((events & FishGameState::EventEscaped) != 0
		&& escapeState.phase() == FishGameState::Phase::Idle
		&& escapeState.escapeCount() == 1,
		"third mistake reports escape and resets the same window") && ok;

	return ok ? 0 : 1;
}
