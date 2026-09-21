#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DiceSystem.generated.h"

// Structure to hold detailed dice roll results
USTRUCT(BlueprintType)
struct FDiceResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Dice")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Dice")
	float RollValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Dice")
	float TargetValue = 0.0f;
};

/**
 * Probability and Dice Roll Utilities for OmniAgent System.
 * Handles RPG-like checks (Reflex, Luck, Skill) centrally.
 */
UCLASS()
class UE5_MCP_VR_API UDiceSystem : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Checks if a reflex action succeeds based on Stat (e.g. Dexterity) and Difficulty.
	 * Formula: Success Chance = Stat / Difficulty.
	 * Returns true if Random(0, 100) < SuccessChance.
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|Dice")
	static bool CheckReflex(float StatValue, int Difficulty, FDiceResult& OutResult);

};
