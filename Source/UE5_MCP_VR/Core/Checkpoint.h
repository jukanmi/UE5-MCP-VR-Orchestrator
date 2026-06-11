#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Checkpoint.generated.h"

/**
 * 레벨 작가가 배치하는 체크포인트 액터.
 * 플레이어가 트리거 볼륨에 진입하면 현재 위치와 HP를 자동 저장한다.
 */
UCLASS()
class UE5_MCP_VR_API ACheckpoint : public AActor
{
	GENERATED_BODY()

public:
	ACheckpoint();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Checkpoint")
	UBoxComponent* TriggerVolume;

	UFUNCTION()
	void OnPlayerEntered(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
