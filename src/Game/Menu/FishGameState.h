#pragma once

#include <functional>
#include <string>

class FishGameState
{
public:
	enum class Phase
	{
		Idle,
		Casting,
		Waiting,
		Biting,
		Pulling,
		Struggling,
	};

	enum Event : unsigned int
	{
		EventNone = 0,
		EventWaitStarted = 1 << 0,
		EventBiteStarted = 1 << 1,
		EventFishReady = 1 << 2,
		EventStruggleStarted = 1 << 3,
		EventStruggleEnded = 1 << 4,
		EventLifeLost = 1 << 5,
		EventCaught = 1 << 6,
		EventEscaped = 1 << 7,
	};

	using RandomInclusive = std::function<int(int minimum, int maximum)>;

	static constexpr int CastDurationMilliseconds = 1375;
	static constexpr int BiteDurationMilliseconds = 250;
	static constexpr int PullAmount = 2;
	static constexpr int ReelThreshold = 15;
	static constexpr int MaximumLives = 3;

	void reset();
	unsigned int startCast();
	unsigned int update(int elapsedMilliseconds, const RandomInclusive& randomInclusive);
	unsigned int pull(const RandomInclusive& randomInclusive);
	unsigned int makeMistake();
	unsigned int reel();

	Phase phase() const;
	int tension() const;
	int lostLives() const;
	int catchCount() const;
	int escapeCount() const;
	int phaseElapsedMilliseconds() const;
	bool canReel() const;
	bool showFishBar() const;
	bool showCastButton() const;
	bool showPullButton() const;
	bool showStruggleButton() const;
	bool showReelButton() const;
	bool waterRippleStarted() const;
	std::string tip() const;

private:
	Phase currentPhase = Phase::Idle;
	int currentTension = 100;
	int lifeCount = 0;
	int caughtFishCount = 0;
	int escapedFishCount = 0;
	int elapsedInPhase = 0;
	int waitDuration = 0;
	int struggleDuration = 0;
	bool reelAvailable = false;
	bool rippleStarted = false;
	bool continueTip = false;

	void resetRound();
};
