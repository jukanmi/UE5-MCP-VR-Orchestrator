#include "Checkpoint.h"
#include "VRPawn.h"
#include "VRPlayerCharacter.h"

ACheckpoint::ACheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	RootComponent = TriggerVolume;
	TriggerVolume->SetBoxExtent(FVector(200.f, 200.f, 200.f));
	TriggerVolume->SetCollisionProfileName(TEXT("Trigger"));
}

void ACheckpoint::BeginPlay()
{
	Super::BeginPlay();
	TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ACheckpoint::OnPlayerEntered);
}

void ACheckpoint::OnPlayerEntered(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (AVRPawn* VRPlayer = Cast<AVRPawn>(OtherActor))
	{
		VRPlayer->SaveCheckpoint(GetActorLocation(), GetActorRotation());
		return;
	}
	if (AVRPlayerCharacter* Player = Cast<AVRPlayerCharacter>(OtherActor))
	{
		Player->SaveCheckpoint(GetActorLocation(), GetActorRotation());
	}
}
