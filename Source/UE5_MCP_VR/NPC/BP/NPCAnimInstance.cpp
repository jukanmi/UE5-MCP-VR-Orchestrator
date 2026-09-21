#include "NPC/BP/NPCAnimInstance.h"
#include "NPC/Components/NPCStateComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UNPCAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    if (AActor* Owner = GetOwningActor())
    {
        CachedStateComponent = Owner->FindComponentByClass<UNPCStateComponent>();
    }
}

void UNPCAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    ACharacter* Owner = Cast<ACharacter>(GetOwningActor());
    if (!Owner) return;

    // 자세는 NPCStateComponent 가 단일 소스 — 여기선 읽어서 미러만 한다.
    // Initialize 시점에 소유자가 미완성이었을 수 있어(스폰 순서) 캐시 실패 시 한 번 더 시도.
    if (!CachedStateComponent.IsValid())
    {
        CachedStateComponent = Owner->FindComponentByClass<UNPCStateComponent>();
    }
    if (const UNPCStateComponent* State = CachedStateComponent.Get())
    {
        bIsSit = State->bIsSit;
        bIsLie = State->bIsLie;
    }

    UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();

    const FVector Velocity = Owner->GetVelocity();
    Speed = Velocity.Size2D();

    const FVector LocalVelocity = Owner->GetActorTransform().InverseTransformVectorNoScale(Velocity);
    Direction = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));

    const bool bCurrentlyInAir = Movement->IsFalling();
    bJustLanded = bWasInAir && !bCurrentlyInAir;
    bIsInAir = bCurrentlyInAir;
    bWasInAir = bCurrentlyInAir;
}
