#include "DiceSystem.h"

/**
 * Difficulty: make by SLM
 * 1: Normal
 * 2: Hard
*/
bool UDiceSystem::CheckReflex(float StatValue, int Difficulty, FDiceResult& OutResult)
{
	// Formula: Success Chance = StatValue / Difficulty.
	// e.g. Stat: 60, Difficulty: 2 -> Chance: 60/2 = 30%
	
	float SuccessChance = StatValue / Difficulty;

	float Roll = FMath::RandRange(0.0f, 100.0f);

	OutResult.TargetValue = SuccessChance;
	OutResult.RollValue = Roll;
	OutResult.bSuccess = (Roll < SuccessChance);

	return OutResult.bSuccess;
}
