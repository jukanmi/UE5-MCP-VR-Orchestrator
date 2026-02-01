#pragma once

#include "CoreMinimal.h"
#include "CharacterAttributes.generated.h"

USTRUCT(BlueprintType)
struct FCharacterAttributes
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Hp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float MaxHp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Agility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Perception;

    FCharacterAttributes()
        : Hp(100.0f), MaxHp(100.0f), Agility(0.5f), Perception(0.5f)
    {}
};
