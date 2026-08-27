#pragma once

#include <array>
#include <string>

class DiceGambleState
{
public:
	static constexpr int BetStep = 50;
	static constexpr int DiceCount = 3;
	static constexpr int FaceCount = 6;
	static constexpr int FrameMilliseconds = 125;

	enum class HandRank
	{
		Normal,
		Pair,
		Straight,
		Triple,
	};

	enum class Result
	{
		None,
		Tie,
		PlayerWin,
		NpcWin,
	};

	enum class Phase
	{
		WaitingForBet,
		Rolling,
		Revealing,
	};

	struct Outcome
	{
		Result result = Result::None;
		HandRank playerRank = HandRank::Normal;
		HandRank npcRank = HandRank::Normal;
		std::string playerTalk;
		std::string npcTalk;
	};

	void reset(int playerMoney, int npcMoney = BetStep);
	bool addBet();
	bool start();
	bool open(const std::array<int, DiceCount>& playerDice,
		const std::array<int, DiceCount>& npcDice);
	bool update(int elapsedMilliseconds);

	static HandRank rank(const std::array<int, DiceCount>& dice);
	static Result compare(const std::array<int, DiceCount>& playerDice,
		const std::array<int, DiceCount>& npcDice);
	static Outcome evaluate(const std::array<int, DiceCount>& playerDice,
		const std::array<int, DiceCount>& npcDice);

	Phase phase() const;
	int stake() const;
	int playerMoney() const;
	int npcMoney() const;
	int lastMoneyDelta() const;
	const std::array<int, DiceCount>& playerDice() const;
	const std::array<int, DiceCount>& npcDice() const;
	int displayedFace(bool player, int diceIndex) const;
	const Outcome& outcome() const;

private:
	static int labelTimeMilliseconds(int face);
	static int stopTimeMilliseconds(bool player, int diceIndex, int face);
	void resolve();

	Phase currentPhase = Phase::WaitingForBet;
	int currentStake = 0;
	int currentPlayerMoney = 0;
	int currentNpcMoney = BetStep;
	int moneyDelta = 0;
	int rollingElapsedMilliseconds = 0;
	int revealElapsedMilliseconds = 0;
	int resolutionTimeMilliseconds = 0;
	std::array<int, DiceCount> currentPlayerDice = { 1, 1, 1 };
	std::array<int, DiceCount> currentNpcDice = { 1, 1, 1 };
	Outcome currentOutcome;
};
