#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StoryZoneTrigger.generated.h"

class UBoxComponent;

/**
 * 구역 진입 트리거 — 플레이어(IPlayerBase)가 박스에 들어오면 story_event {zone_enter, ZoneName} 을 보낸다.
 *
 * Python 은 이를 플래그 "zone_enter:<ZoneName>" 으로 기록한다(StoryEventPayload.flag_name). 비트 complete_when
 * {type: flag, name: "zone_enter:citadel"} 로 쓰거나, 디렉터 컨텍스트(PLAYER HISTORY flags)로만 써도 된다.
 * 레벨에 직접 배치(BP 불필요). 기본은 1회만(bOnce) — 같은 구역을 들락날락해도 이벤트 폭주 안 함.
 */
UCLASS()
class UE5_MCP_VR_API AStoryZoneTrigger : public AActor
{
    GENERATED_BODY()

public:
    AStoryZoneTrigger();

    /** story_event.name 으로 나가는 구역 id. 비트 시트 플래그와 철자 일치. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    FString ZoneName;

    /** 이 구역이 가리키는 POI 의 PoiId(선택). 비면 종전 동작 그대로. 구역 이름을 POI 어휘와 맞추는 참조용이며,
     *  송신하는 zone_enter 플래그명(ZoneName)에는 영향을 주지 않는다. 존재 확인은 첫 진입 시 1회(POI 등록 순서 무관). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    FString PoiId;

    /** true 면 첫 진입 1회만 송신. false 면 진입마다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Story")
    bool bOnce = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Story")
    UBoxComponent* Box;

protected:
    UFUNCTION()
    void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                            int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    bool bFired = false;
    bool bPoiChecked = false;
};
