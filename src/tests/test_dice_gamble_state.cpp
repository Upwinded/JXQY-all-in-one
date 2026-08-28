#include "../Game/Menu/DiceGambleState.h"

#include <iostream>

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
	using Rank = DiceGambleState::HandRank;
	using Result = DiceGambleState::Result;
	bool ok = true;

	ok = check(DiceGambleState::rank({ 1, 3, 6 }) == Rank::Normal,
		"three unrelated faces are a normal hand") && ok;
	ok = check(DiceGambleState::rank({ 5, 2, 5 }) == Rank::Pair,
		"two identical faces are a pair") && ok;
	ok = check(DiceGambleState::rank({ 4, 2, 3 }) == Rank::Straight,
		"three consecutive sorted faces are a straight") && ok;
	ok = check(DiceGambleState::rank({ 6, 6, 6 }) == Rank::Triple,
		"three identical faces are a triple") && ok;

	ok = check(DiceGambleState::compare({ 6, 6, 6 }, { 4, 5, 6 }) == Result::PlayerWin,
		"triple outranks straight") && ok;
	ok = check(DiceGambleState::compare({ 3, 4, 5 }, { 2, 3, 4 }) == Result::PlayerWin,
		"higher straight wins") && ok;
	ok = check(DiceGambleState::compare({ 4, 4, 1 }, { 3, 3, 6 }) == Result::PlayerWin,
		"pair value wins before kicker") && ok;
	ok = check(DiceGambleState::compare({ 4, 4, 6 }, { 4, 4, 2 }) == Result::PlayerWin,
		"pair kicker breaks equal pairs") && ok;
	ok = check(DiceGambleState::compare({ 6, 4, 2 }, { 6, 3, 2 }) == Result::PlayerWin,
		"normal hands compare descending faces lexicographically") && ok;
	ok = check(DiceGambleState::compare({ 1, 5, 3 }, { 5, 3, 1 }) == Result::Tie,
		"same sorted faces tie regardless of order") && ok;

	DiceGambleState state;
	state.reset(120, 50);
	ok = check(!state.start(), "round cannot start before a bet") && ok;
	ok = check(state.addBet() && state.addBet() && state.addBet(),
		"MG bet check permits the final 50 step while uncommitted money remains") && ok;
	ok = check(state.stake() == 150 && !state.addBet(),
		"MG bet step can overcommit by less than 50 but stops once stake covers money") && ok;
	ok = check(state.start(), "round starts after betting") && ok;
	ok = check(state.open({ 6, 6, 6 }, { 1, 2, 4 }), "rolling round can be opened") && ok;
	ok = check(state.update(625), "face six on the third player die resolves after five MG label intervals") && ok;
	ok = check(state.playerMoney() == 270 && state.npcMoney() == -100,
		"player win transfers the full stake from NPC to player") && ok;
	ok = check(state.lastMoneyDelta() == 150 && state.stake() == 0,
		"win exposes one economy delta and clears the stake") && ok;

	state.reset(100, 50);
	state.addBet();
	state.start();
	state.open({ 1, 2, 4 }, { 2, 2, 2 });
	ok = check(state.update(4000), "loss reveal completes") && ok;
	ok = check(state.playerMoney() == 50 && state.npcMoney() == 100
		&& state.lastMoneyDelta() == -50,
		"NPC win transfers the stake from player to NPC") && ok;

	state.reset(100, 50);
	state.addBet();
	state.start();
	state.open({ 1, 3, 5 }, { 5, 1, 3 });
	ok = check(state.update(4000), "tie reveal completes") && ok;
	ok = check(state.playerMoney() == 100 && state.npcMoney() == 50
		&& state.lastMoneyDelta() == 0,
		"tie leaves both economies unchanged") && ok;

	auto outcome = DiceGambleState::evaluate({ 4, 4, 6 }, { 2, 3, 4 });
	ok = check(outcome.playerTalk.rfind("好运连连，对子……", 0) == 0,
		"player talk includes MG pair prefix") && ok;
	ok = check(outcome.npcTalk.rfind("顺子……", 0) == 0,
		"NPC talk includes MG straight prefix") && ok;

	return ok ? 0 : 1;
}
