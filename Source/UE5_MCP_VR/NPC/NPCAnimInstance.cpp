#include "NPCAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UNPCAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    ACharacter* Owner = Cast<ACharacter>(GetOwningActor());
    if (!Owner) return;

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
