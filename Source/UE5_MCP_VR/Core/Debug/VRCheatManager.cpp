#include "Core/Debug/VRCheatManager.h"

#include "Core/BP/VRPawn.h"
#include "Core/BP/VRPlayerUIComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"

AVRPawn* UVRCheatManager::GetVRPawn() const
{
    const APlayerController* PC = GetOuterAPlayerController();
    return PC ? Cast<AVRPawn>(PC->GetPawn()) : nullptr;
}

void UVRCheatManager::Cheat_Unequip(bool bOffHand)
{
    const AVRPawn* Pawn = GetVRPawn();
    if (Pawn && Pawn->Inventory)
    {
        Pawn->Inventory->UnequipItem(bOffHand ? EEquipmentSlot::OffHand : EEquipmentSlot::MainHand);
    }
}

void UVRCheatManager::TuneGrab(float DX, float DY, float DZ, float DPitch, float DYaw, float DRoll)
{
    const AVRPawn* Pawn = GetVRPawn();

    // 오른손을 먼저 본다. 오른손이 비어 있으면 왼손에 쥔 것을 튜닝 대상으로 삼는다.
    ADroppedItemBase* Held = nullptr;
    if (Pawn && Pawn->Inventory)
    {
        Held = Pawn->Inventory->GetHeldItem(EEquipmentSlot::MainHand);
        if (!Held) Held = Pawn->Inventory->GetHeldItem(EEquipmentSlot::OffHand);
    }
    if (!IsValid(Held))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Cheat] TuneGrab — 쥔 아이템이 없습니다."));
        return;
    }

    // 델타로 밀고 절대값을 찍는다. VR 을 쓴 채로는 수치를 못 읽으니, 찍힌 값을 그대로 붙여넣어 확정하는 흐름.
    Held->AddActorLocalOffset(FVector(DX, DY, DZ));
    Held->AddActorLocalRotation(FRotator(DPitch, DYaw, DRoll));

    const FVector Loc = Held->GetRootComponent()->GetRelativeLocation();
    const FRotator Rot = Held->GetRootComponent()->GetRelativeRotation();

    // CSV 에 그대로 붙일 수 있는 표기로 찍는다.
    const FString Line = FString::Printf(
        TEXT("[Cheat] %s HoldOffset=\"(X=%.2f,Y=%.2f,Z=%.2f)\" HoldRotation=\"(Pitch=%.2f,Yaw=%.2f,Roll=%.2f)\""),
        *Held->ItemData.ItemTemplateID, Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);

    UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
    if (GEngine) GEngine->AddOnScreenDebugMessage(8813, 8.f, FColor::Yellow, Line);
}

void UVRCheatManager::DumpInventoryHUD()
{
    const AVRPawn* Pawn = GetVRPawn();
    if (Pawn && Pawn->PlayerUI) Pawn->PlayerUI->DumpInventoryHUD();
}

void UVRCheatManager::ToggleInventory()
{
    const AVRPawn* Pawn = GetVRPawn();
    if (Pawn && Pawn->PlayerUI) Pawn->PlayerUI->ToggleInventory();
}
