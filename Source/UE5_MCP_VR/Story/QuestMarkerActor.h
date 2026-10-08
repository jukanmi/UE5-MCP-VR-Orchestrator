#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QuestMarkerActor.generated.h"

class UStaticMeshComponent;

/**
 * 메인 퀘스트 목표물 위에 떠 있는 월드스페이스 마커(역원뿔, 무광 단색). UStorySubsystem 이 1개만 스폰해
 * 비트 갱신마다 SetTarget 으로 대상만 바꾼다. 대상은 매 틱 따라가므로 NPC 가 걸어도 머리 위에 붙어 있고,
 * 대상이 사라지면(보스 사망·미등록) 숨는다. 레벨 배치·BP 불필요.
 */
UCLASS()
class UE5_MCP_VR_API AQuestMarkerActor : public AActor
{
    GENERATED_BODY()

public:
    AQuestMarkerActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Story")
    UStaticMeshComponent* Mesh;

    /** 대상 액터 원점 위 높이(cm). SmartNPC 캡슐 원점 기준 머리 위 ~1.2m. 마커가 커지면 꼭짓점이 같은 높이에 오도록 자동으로 더 올라간다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    float HoverHeight = 280.f;

    /** 마커(원뿔) 최소 높이(cm). 가까워도 이보다 작아지지 않는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story", meta = (ClampMin = "50.0"))
    float MinMarkerHeight = 250.f;

    /** 마커 최대 높이(cm). 아주 멀어도 이보다 커지지 않는다(건물처럼 보이지 않게). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story", meta = (ClampMin = "50.0"))
    float MaxMarkerHeight = 2500.f;

    /** 마커 높이 = 카메라와 대상 거리 × 이 값(클램프 범위 안에서). 멀수록 커져 시야각이 일정하게 유지된다.
     *  0.1 이면 어디서 봐도 화면 높이의 약 6° — 50m 에서 5m 높이. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DistanceScale = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    float BobAmplitude = 20.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    float BobSpeed = 2.f;

    /** 도/초. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    float SpinSpeed = 120.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    FLinearColor Color = FLinearColor(1.f, 0.85f, 0.1f);

    /** null 이면 숨김. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Story")
    void SetTarget(AActor* InTarget);

    UFUNCTION(BlueprintPure, Category = "MCP|Story")
    AActor* GetTarget() const { return Target.Get(); }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    TWeakObjectPtr<AActor> Target;
};
