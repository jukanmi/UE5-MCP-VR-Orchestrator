#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "POIActor.generated.h"

/**
 * 이름 있는 장소(POI) 레벨 액터. 위치는 에디터에서 배치한 액터 위치가 유일한 원본이다.
 *
 * BeginPlay/EndPlay 에서 UPOIManager 에 등록·해제하므로 런타임 스폰 POI 도 조회된다.
 * World Partition 레벨에서 공간 로딩되면 등록소에 안 보이므로 생성자에서 항상 로드로 고정한다.
 */
UCLASS()
class UE5_MCP_VR_API APOIActor : public ATargetPoint
{
    GENERATED_BODY()

public:
    APOIActor();

    /** 영문 식별자(예 Lake). 등록소 키이자 LLM 이 장소를 가리킬 때 쓰는 id. 월드 안에서 유일해야 한다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
    FString PoiId;

    /** 한국어 표시명(예 호수). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
    FText DisplayName;

    /** 같은 장소를 가리키는 다른 말(예 연못). 별칭 조회·LLM 프롬프트 단서용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
    TArray<FString> Aliases;

    /** 장소 종류. 아직 어떤 로직도 이 값으로 거르지 않는다(필터가 필요해질 때 어휘 확정). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
    FString Type;

    /** 장소 설명 한 줄. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
    FString Description;

    /** 도착으로 인정하는 반경(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI", meta = (ClampMin = "0"))
    float ArrivalRadius = 150.f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
