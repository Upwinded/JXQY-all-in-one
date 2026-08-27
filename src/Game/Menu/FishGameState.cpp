#include "FishGameState.h"

#include <algorithm>

void FishGameState::reset()
{
	caughtFishCount = 0;
	escapedFishCount = 0;
	rippleStarted = false;
	resetRound();
}

void FishGameState::resetRound()
{
	currentPhase = Phase::Idle;
	currentTension = 100;
	lifeCount = 0;
	elapsedInPhase = 0;
	waitDuration = 0;
	struggleDuration = 0;
	reelAvailable = false;
	continueTip = false;
}

unsigned int FishGameState::startCast()
{
	if (currentPhase != Phase::Idle)
	{
		return EventNone;
	}
	currentPhase = Phase::Casting;
	elapsedInPhase = 0;
	return EventNone;
}

unsigned int FishGameState::update(int elapsedMilliseconds, const RandomInclusive& randomInclusive)
{
	if (elapsedMilliseconds <= 0 || !randomInclusive)
	{
		return EventNone;
	}

	unsigned int events = EventNone;
	int remaining = elapsedMilliseconds;
	while (remaining > 0)
	{
		int duration = 0;
		switch (currentPhase)
		{
		case Phase::Casting:
			duration = CastDurationMilliseconds;
			break;
		case Phase::Waiting:
			duration = waitDuration;
			break;
		case Phase::Biting:
			duration = BiteDurationMilliseconds;
			break;
		case Phase::Struggling:
			duration = struggleDuration;
			break;
		default:
			return events;
		}

		int untilTransition = std::max(0, duration - elapsedInPhase);
		if (remaining < untilTransition)
		{
			elapsedInPhase += remaining;
			return events;
		}
		remaining -= untilTransition;
		elapsedInPhase = 0;

		switch (currentPhase)
		{
		case Phase::Casting:
			currentPhase = Phase::Waiting;
			waitDuration = randomInclusive(2, 9) * 1000;
			rippleStarted = true;
			events |= EventWaitStarted;
			break;
		case Phase::Waiting:
			currentPhase = Phase::Biting;
			events |= EventBiteStarted;
			break;
		case Phase::Biting:
			currentPhase = Phase::Pulling;
			currentTension = randomInclusive(18, 79);
			events |= EventFishReady;
			break;
		case Phase::Struggling:
			currentPhase = Phase::Pulling;
			continueTip = true;
			events |= EventStruggleEnded;
			break;
		default:
			return events;
		}
	}
	return events;
}

unsigned int FishGameState::pull(const RandomInclusive& randomInclusive)
{
	if (currentPhase != Phase::Pulling || !randomInclusive)
	{
		return EventNone;
	}

	if (currentTension <= ReelThreshold)
	{
		reelAvailable = true;
		continueTip = false;
	}
	currentTension = std::max(0, currentTension - PullAmount);
	if (randomInclusive(0, 100) < 10)
	{
		currentPhase = Phase::Struggling;
		elapsedInPhase = 0;
		struggleDuration = randomInclusive(2, 4) * 1000;
		return EventStruggleStarted;
	}
	return EventNone;
}

unsigned int FishGameState::makeMistake()
{
	if (currentPhase != Phase::Struggling)
	{
		return EventNone;
	}

	lifeCount++;
	if (lifeCount < MaximumLives)
	{
		return EventLifeLost;
	}
	escapedFishCount++;
	resetRound();
	return EventLifeLost | EventEscaped;
}

unsigned int FishGameState::reel()
{
	if (!reelAvailable
		|| (currentPhase != Phase::Pulling && currentPhase != Phase::Struggling))
	{
		return EventNone;
	}
	caughtFishCount++;
	resetRound();
	return EventCaught;
}

FishGameState::Phase FishGameState::phase() const
{
	return currentPhase;
}

int FishGameState::tension() const
{
	return currentTension;
}

int FishGameState::lostLives() const
{
	return lifeCount;
}

int FishGameState::catchCount() const
{
	return caughtFishCount;
}

int FishGameState::escapeCount() const
{
	return escapedFishCount;
}

int FishGameState::phaseElapsedMilliseconds() const
{
	return elapsedInPhase;
}

bool FishGameState::canReel() const
{
	return reelAvailable;
}

bool FishGameState::showFishBar() const
{
	return currentPhase == Phase::Pulling || currentPhase == Phase::Struggling;
}

bool FishGameState::showCastButton() const
{
	return currentPhase == Phase::Idle;
}

bool FishGameState::showPullButton() const
{
	return currentPhase == Phase::Pulling && (!reelAvailable || continueTip);
}

bool FishGameState::showStruggleButton() const
{
	return currentPhase == Phase::Struggling;
}

bool FishGameState::showReelButton() const
{
	return reelAvailable;
}

bool FishGameState::waterRippleStarted() const
{
	return rippleStarted;
}

std::string FishGameState::tip() const
{
	switch (currentPhase)
	{
	case Phase::Pulling:
		if (continueTip)
		{
			return "继续拉线~~~";
		}
		return reelAvailable ? "可以提竿~~" : "有鱼咬饵，快拉线~~~";
	case Phase::Struggling:
		return "鱼挣扎过猛，此时切勿轻举妄动!";
	default:
		return "";
	}
}
