#include "DiceGambleState.h"

#include <algorithm>
#include <map>

namespace
{
	const char* PlayerWinTalk = "在命运的赌桌上，不在乎输赢的人，运气总不会太差！";
	const char* PlayerLoseTalk = "一时的输赢并不重要，重要的是在重重的博弈中，不要迷失自己。";
	const char* PlayerTieTalk = "来，决战到天亮";
	const char* NpcLoseTalk = "辛辛苦苦二十年，一赌回到解放前！";
	const char* NpcWinTalk = "没银两了我这有，再来一局怎么样";
	const char* NpcTieTalk = "太可惜了，再来一局";

	std::string playerRankPrefix(DiceGambleState::HandRank rank)
	{
		switch (rank)
		{
		case DiceGambleState::HandRank::Pair:
			return "好运连连，对子……";
		case DiceGambleState::HandRank::Straight:
			return "顺水顺风，顺子……";
		case DiceGambleState::HandRank::Triple:
			return "鸿运当头，豹子……";
		default:
			return "";
		}
	}

	std::string npcRankPrefix(DiceGambleState::HandRank rank)
	{
		switch (rank)
		{
		case DiceGambleState::HandRank::Pair:
			return "对子……";
		case DiceGambleState::HandRank::Straight:
			return "顺子……";
		case DiceGambleState::HandRank::Triple:
			return "豹子……";
		default:
			return "";
		}
	}

	std::pair<int, int> pairAndSingle(const std::array<int, DiceGambleState::DiceCount>& dice)
	{
		std::map<int, int> counts;
		for (int face : dice)
		{
			counts[face]++;
		}
		int pair = 0;
		int single = 0;
		for (const auto& [face, count] : counts)
		{
			if (count == 2)
			{
				pair = face;
			}
			else
			{
				single = face;
			}
		}
		return { pair, single };
	}
}

void DiceGambleState::reset(int playerMoney, int npcMoney)
{
	currentPhase = Phase::WaitingForBet;
	currentStake = 0;
	currentPlayerMoney = playerMoney;
	currentNpcMoney = npcMoney;
	moneyDelta = 0;
	rollingElapsedMilliseconds = 0;
	revealElapsedMilliseconds = 0;
	resolutionTimeMilliseconds = 0;
	currentPlayerDice = { 1, 1, 1 };
	currentNpcDice = { 1, 1, 1 };
	currentOutcome = Outcome();
}

bool DiceGambleState::addBet()
{
	if (currentPhase != Phase::WaitingForBet
		|| currentPlayerMoney - currentStake <= 0)
	{
		return false;
	}
	currentStake += BetStep;
	return true;
}

bool DiceGambleState::start()
{
	if (currentPhase != Phase::WaitingForBet || currentStake <= 0)
	{
		return false;
	}
	currentPhase = Phase::Rolling;
	rollingElapsedMilliseconds = 0;
	currentOutcome = Outcome();
	return true;
}

bool DiceGambleState::open(
	const std::array<int, DiceCount>& playerDice,
	const std::array<int, DiceCount>& npcDice)
{
	if (currentPhase != Phase::Rolling)
	{
		return false;
	}
	for (int face : playerDice)
	{
		if (face < 1 || face > FaceCount)
		{
			return false;
		}
	}
	for (int face : npcDice)
	{
		if (face < 1 || face > FaceCount)
		{
			return false;
		}
	}
	currentPlayerDice = playerDice;
	currentNpcDice = npcDice;
	currentOutcome = evaluate(currentPlayerDice, currentNpcDice);
	currentPhase = Phase::Revealing;
	revealElapsedMilliseconds = 0;
	resolutionTimeMilliseconds = stopTimeMilliseconds(true, 2, currentPlayerDice[2]);
	return true;
}

bool DiceGambleState::update(int elapsedMilliseconds)
{
	elapsedMilliseconds = std::max(0, elapsedMilliseconds);
	if (currentPhase == Phase::Rolling)
	{
		rollingElapsedMilliseconds += elapsedMilliseconds;
		return false;
	}
	if (currentPhase != Phase::Revealing)
	{
		return false;
	}
	revealElapsedMilliseconds += elapsedMilliseconds;
	if (revealElapsedMilliseconds < resolutionTimeMilliseconds)
	{
		return false;
	}
	resolve();
	return true;
}

DiceGambleState::HandRank DiceGambleState::rank(
	const std::array<int, DiceCount>& dice)
{
	std::array<int, DiceCount> sorted = dice;
	std::sort(sorted.begin(), sorted.end());
	if (sorted[0] == sorted[2])
	{
		return HandRank::Triple;
	}
	if (sorted[0] + 1 == sorted[1] && sorted[1] + 1 == sorted[2])
	{
		return HandRank::Straight;
	}
	if (sorted[0] == sorted[1] || sorted[1] == sorted[2])
	{
		return HandRank::Pair;
	}
	return HandRank::Normal;
}

DiceGambleState::Result DiceGambleState::compare(
	const std::array<int, DiceCount>& playerDice,
	const std::array<int, DiceCount>& npcDice)
{
	std::array<int, DiceCount> sortedPlayer = playerDice;
	std::array<int, DiceCount> sortedNpc = npcDice;
	std::sort(sortedPlayer.begin(), sortedPlayer.end());
	std::sort(sortedNpc.begin(), sortedNpc.end());
	if (sortedPlayer == sortedNpc)
	{
		return Result::Tie;
	}

	HandRank playerRank = rank(playerDice);
	HandRank npcRank = rank(npcDice);
	if (playerRank != npcRank)
	{
		return playerRank > npcRank ? Result::PlayerWin : Result::NpcWin;
	}
	if (playerRank == HandRank::Straight || playerRank == HandRank::Triple)
	{
		return sortedPlayer[2] > sortedNpc[2] ? Result::PlayerWin : Result::NpcWin;
	}
	if (playerRank == HandRank::Pair)
	{
		auto playerValues = pairAndSingle(playerDice);
		auto npcValues = pairAndSingle(npcDice);
		return playerValues > npcValues ? Result::PlayerWin : Result::NpcWin;
	}
	for (int index = DiceCount - 1; index >= 0; index--)
	{
		if (sortedPlayer[index] != sortedNpc[index])
		{
			return sortedPlayer[index] > sortedNpc[index]
				? Result::PlayerWin : Result::NpcWin;
		}
	}
	return Result::Tie;
}

DiceGambleState::Outcome DiceGambleState::evaluate(
	const std::array<int, DiceCount>& playerDice,
	const std::array<int, DiceCount>& npcDice)
{
	Outcome result;
	result.playerRank = rank(playerDice);
	result.npcRank = rank(npcDice);
	result.result = compare(playerDice, npcDice);
	result.playerTalk = playerRankPrefix(result.playerRank);
	result.npcTalk = npcRankPrefix(result.npcRank);
	switch (result.result)
	{
	case Result::PlayerWin:
		result.playerTalk += PlayerWinTalk;
		result.npcTalk += NpcLoseTalk;
		break;
	case Result::NpcWin:
		result.playerTalk += PlayerLoseTalk;
		result.npcTalk += NpcWinTalk;
		break;
	case Result::Tie:
		result.playerTalk += PlayerTieTalk;
		result.npcTalk += NpcTieTalk;
		break;
	default:
		break;
	}
	return result;
}

DiceGambleState::Phase DiceGambleState::phase() const
{
	return currentPhase;
}

int DiceGambleState::stake() const
{
	return currentStake;
}

int DiceGambleState::playerMoney() const
{
	return currentPlayerMoney;
}

int DiceGambleState::npcMoney() const
{
	return currentNpcMoney;
}

int DiceGambleState::lastMoneyDelta() const
{
	return moneyDelta;
}

const std::array<int, DiceGambleState::DiceCount>& DiceGambleState::playerDice() const
{
	return currentPlayerDice;
}

const std::array<int, DiceGambleState::DiceCount>& DiceGambleState::npcDice() const
{
	return currentNpcDice;
}

int DiceGambleState::displayedFace(bool player, int diceIndex) const
{
	if (diceIndex < 0 || diceIndex >= DiceCount)
	{
		return 1;
	}
	if (currentPhase == Phase::WaitingForBet)
	{
		return currentOutcome.result == Result::None
			? 1
			: (player ? currentPlayerDice[diceIndex] : currentNpcDice[diceIndex]);
	}
	if (currentPhase == Phase::Revealing)
	{
		const auto& finalDice = player ? currentPlayerDice : currentNpcDice;
		if (revealElapsedMilliseconds >= stopTimeMilliseconds(player, diceIndex, finalDice[diceIndex]))
		{
			return finalDice[diceIndex];
		}
	}
	int elapsed = currentPhase == Phase::Rolling
		? rollingElapsedMilliseconds : rollingElapsedMilliseconds + revealElapsedMilliseconds;
	return (elapsed / FrameMilliseconds + diceIndex * 2 + (player ? 0 : 1)) % FaceCount + 1;
}

const DiceGambleState::Outcome& DiceGambleState::outcome() const
{
	return currentOutcome;
}

int DiceGambleState::labelTimeMilliseconds(int face)
{
	static constexpr std::array<int, FaceCount> LabelTimes = { 0, 625, 500, 375, 250, 125 };
	return face >= 1 && face <= FaceCount ? LabelTimes[face - 1] : 0;
}

int DiceGambleState::stopTimeMilliseconds(bool player, int diceIndex, int face)
{
	static constexpr std::array<int, DiceCount> PlayerCycles = { 1, 2, 5 };
	static constexpr std::array<int, DiceCount> NpcCycles = { 1, 2, 3 };
	const auto& cycles = player ? PlayerCycles : NpcCycles;
	return labelTimeMilliseconds(face) * cycles[diceIndex];
}

void DiceGambleState::resolve()
{
	moneyDelta = 0;
	if (currentOutcome.result == Result::PlayerWin)
	{
		moneyDelta = currentStake;
		currentPlayerMoney += currentStake;
		currentNpcMoney -= currentStake;
	}
	else if (currentOutcome.result == Result::NpcWin)
	{
		moneyDelta = -currentStake;
		currentPlayerMoney -= currentStake;
		currentNpcMoney += currentStake;
	}
	currentStake = 0;
	currentPhase = Phase::WaitingForBet;
}
