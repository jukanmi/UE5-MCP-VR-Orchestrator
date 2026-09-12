#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

/**
 * SubsystemUtils
 *
 * GameInstance 서브시스템 조회 단일 경로. UNPCManager / UItemManager / UFurnitureManager 의
 * ::Get 이 각자 같은 본문(WorldContext → World → GameInstance → GetSubsystem)을 들고 있던 것을
 * 여기 하나로 모았다. 호출부는 각 매니저의 ::Get(this) 를 쓴다 — 인라인
 * GetGameInstance()->GetSubsystem<>() 조회를 새로 쓰지 말 것.
 */
namespace SubsystemUtils
{
    template <class T>
    T* GetGameSubsystem(const UObject* WorldContext)
    {
        if (!WorldContext || !GEngine) return nullptr;
        UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
        UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
        return GI ? GI->GetSubsystem<T>() : nullptr;
    }
}
