#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * GameplayTagUtils
 *
 * 상태 태그(State.*) 추가/제거 자유함수. ASmartNPC / AVRPawn / AVRPlayerCharacter 가
 * 동일하게 쓰던 AddStateTag/RemoveStateTag 본문을 단일화한다.
 * (인터페이스 ICharacterBase 는 멤버 변수를 가질 수 없어 GameplayTags 컨테이너는
 *  각 구현 클래스가 보유 — 본 헬퍼는 컨테이너를 인자로 받아 로직만 공유.)
 */
namespace GameplayTagUtils
{
    inline void AddState(FGameplayTagContainer& Container, FGameplayTag Tag)
    {
        if (Tag.IsValid())
        {
            Container.AddTag(Tag);
        }
    }

    inline void RemoveState(FGameplayTagContainer& Container, FGameplayTag Tag)
    {
        if (Tag.IsValid() && Container.HasTagExact(Tag))
        {
            Container.RemoveTag(Tag);
        }
    }
}
