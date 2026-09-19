#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StoryDirectorSettings.generated.h"

class AEnemyCharacter;

/**
 * 스토리 디렉터 관련 글로벌 설정 (Project Settings -> Game -> Story Director)
 */
UCLASS(Config=Game, defaultconfig, meta=(DisplayName="Story Director Settings"))
class UE5_MCP_VR_API UStoryDirectorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UStoryDirectorSettings()
    {
        CategoryName = TEXT("Game");
        SectionName  = TEXT("Story Director");
    }

    /** 스토리 이벤트 spawn_enemy 요청 시 참조할 적 클래스 맵 (Key: enemy_id) */
    UPROPERTY(EditAnywhere, config, Category = "Spawning")
    TMap<FString, TSoftClassPtr<AEnemyCharacter>> EnemyClassMap;

    /** spawn_item 요청 시 참조할 클래스 맵 (Key: item_id) */
    UPROPERTY(EditAnywhere, config, Category = "Spawning")
    TMap<FString, TSoftClassPtr<class ADroppedItemBase>> ItemClassMap;

    /** spawn_npc 요청 시 참조할 클래스 맵 (Key: npc_id) */
    UPROPERTY(EditAnywhere, config, Category = "Spawning")
    TMap<FString, TSoftClassPtr<class ASmartNPC>> NPCClassMap;


    /** spawn_enemy 시 위치 태그를 가진 액터를 찾은 뒤, 이 반경 안의 네비메시에 스폰합니다. */
    UPROPERTY(EditAnywhere, config, Category = "Spawning")
    float SpawnRadius = 300.0f;
};

