#pragma once

#include "CoreMinimal.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"

// 적·주민 AI 컨트롤러가 함께 쓰는 시야 감지 설정.
namespace SightPerception
{
    /**
     * 시야 반경·각도를 감각 설정에 넣고 감지 컴포넌트에 다시 등록한다.
     * bDefaults 면 생성자에서만 넣는 기본값(기억 3초, 소속 무관 감지, 주 감각 지정)도 함께 넣는다 —
     * 팀 개념이 없어(기본 Attitude=Neutral) 중립까지 감지해야 한다.
     * OnPossess 에서는 bDefaults=false 로 불러 BP 에서 바꾼 반경·각도만 반영한다(생성자 값은 CDO 기본).
     */
    inline void Configure(UAIPerceptionComponent* Perception, UAISenseConfig_Sight* Sight,
                          float Radius, float LoseRadius, float Angle, bool bDefaults)
    {
        if (!Perception || !Sight) return;
        Sight->SightRadius = Radius;
        Sight->LoseSightRadius = LoseRadius;
        Sight->PeripheralVisionAngleDegrees = Angle;
        if (bDefaults)
        {
            Sight->SetMaxAge(3.0f);
            Sight->DetectionByAffiliation.bDetectEnemies = true;
            Sight->DetectionByAffiliation.bDetectNeutrals = true;
            Sight->DetectionByAffiliation.bDetectFriendlies = true;
        }
        Perception->ConfigureSense(*Sight);
        if (bDefaults) Perception->SetDominantSense(Sight->GetSenseImplementation());
    }
}
