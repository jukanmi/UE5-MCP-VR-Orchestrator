#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BehaviorTree/BehaviorTree.h"
#include "../Utils/MCPJsonUtils.h" // For FGameAction struct
#include "NPCActionTypes.h" // For AI Enums and Structs
#include "CharacterAttributes.h"
#include "SmartNPC.generated.h"

class UNPCInteractionDataAsset;
class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;

/**
 * SmartNPC: Lightweight Facade & Identity Container.
 * 
 * [설계 원칙]
 * - SmartNPC는 NPC의 "정체성(Identity)"과 "중재자(Mediator)" 역할만 수행합니다.
 * - 실제 로직은 아래 3개의 컴포넌트에 위임됩니다:
 *   1. UNPCStateComponent   → 스탯, 표정, 데미지, 반사 판정
 *   2. UNPCActionComponent  → 행동 큐, Execute* 함수들, 상호작용
 *   3. UNPCInventoryComponent → 인벤토리, 장비
 *   내부적으로 컴포넌트에 위임하는 Facade 패턴입니다.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API ASmartNPC : public ACharacter
{
    GENERATED_BODY()

public:
    ASmartNPC();

    UFUNCTION(BlueprintCallable, Category = "MCP|Components")
    UNPCStateComponent* GetStateComponent() const { return StateComponent; }

    UFUNCTION(BlueprintCallable, Category = "MCP|Components")
    UNPCActionComponent* GetActionComponent() const { return ActionComponent; }

    UFUNCTION(BlueprintCallable, Category = "MCP|Components")
    UNPCInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

    // === Components ===

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCStateComponent* StateComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCActionComponent* ActionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCInventoryComponent* InventoryComponent;

    // === Identity ===

    /** NPC 고유 ID (예: "Guard_1", "Merchant_A") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    FString AgentID;

    /** 이 NPC가 사용할 Behavior Tree */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    UBehaviorTree* BehaviorTreeAsset;

    // === Vision & Hearing Config (AIController에 적용됨) ===

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightRadius = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float LoseSightRadius = 3500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightAngle = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Hearing")
    float HearingRange = 3000.0f;

    // === Lifecycle ===

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // === Facade API (컴포넌트로 위임) ===

    /** 액션 배치 실행 → ActionComponent에 위임 */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void ExecuteActionBatch(const struct FActionBatch& Batch);

    /** 물리 상태 초기화 (애니메이션 중지, 이동 정지) */
    virtual void ClearPhysicalState();

    // === Damage Hook (UE5 Actor Override) ===

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;

    // === Convenience Accessors (자주 호출되는 것들의 Shortcut) ===

    /** Stats에 직접 접근하기 위한 편의 함수 */
    FCharacterAttributes& GetStats() const;



    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void OnActionCompleted();

    // === Facade: Execute* Wrappers ===

    // ============================================================================
    // [기본 함수 (Base Functions)] - 기존(Legacy) 구현 유지 및 핵심 로직으로 활용
    // ============================================================================
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk, float Distance = 300.f);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteWait(float Duration);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion = EFacialState::Neutral);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed = 5.f);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecutePerformAttack(AActor* TargetActor, const EAttackType AttackType = EAttackType::Melee);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDefend(bool bStartDefend);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDodge(); // 기본 회피 함수

    // TODO: SignalName을 추후 UEnum으로 대체 고려 (일단은 FString 유지)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteHandSignal(const FString& SignalName);

    // TODO: EmoteName을 추후 UEnum으로 대체 고려 (일단은 FString 유지)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteEmote(const FString& EmoteName);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteEquip(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteUnequip(const FString& ItemID);

    // 공통 상호작용 라우터 (SitDown, PickUp, Eat 등)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteInteraction(const EAction InteractionType, AActor* TargetActor, const FString& TargetID);


    // ============================================================================
    // [EAction 래퍼 함수 (Action Wrappers)] - Blueprint 64:104 명세 구현부
    // 각 함수는 위 Base Functions 들을 내부적으로 조합, 호출하여 구현될 예정임
    // ============================================================================

    // ----------------------------------------------------------------------------
    // [1] Common Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteIdle(); // TODO: ClearPhysicalState() 활용

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteMove(FVector Location, AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk); // TODO: Target 우선 판별 후 ExecuteMoveToLocation 활용

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteFollow(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk); // TODO: ExecuteKeepDistance 활용

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteTurnTo(FVector Location, AActor* TargetActor); // TODO: Target 우선 판별 후 ExecuteFaceRotate 활용

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteStop(); // TODO: StopAllActions() 활용

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteScan(FVector Location, AActor* TargetActor); // TODO: TargetActor 판단 후 RandomPoint에 ExecuteFaceRotate 및 ExecuteWait 조합

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteUseItem(const FString& ItemID); // TODO: ExecuteInteraction("Eat"...) 활용

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteEquipAction(const FString& ItemID); // TODO: 기본 함수 ExecuteEquip 직접 호출/래핑

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteUnequipAction(const FString& ItemID); // TODO: 기본 함수 ExecuteUnequip 직접 호출/래핑

    // ----------------------------------------------------------------------------
    // [2] Combat Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteAttackAction(AActor* TargetActor); // TODO: TargetActor 타겟팅 후 기본 함수 ExecutePerformAttack 래핑

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteBlock(AActor* TargetActor); // TODO: 타겟을 향해 ExecuteFaceRotate 이후 ExecuteDefend(true) 기반 로직

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDodgeAction(FVector Direction); // TODO: 인자로 받은 Direction 방향에 맞춰 기본 함수 ExecuteDodge 연계 다이내믹 래핑

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteFlee(FVector Location); // TODO: ExecuteMoveToLocation(Run) 형태로 구현

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteSignalAllies(const FString& HandSign); // TODO: ExecuteHandSignal 래핑

    // ----------------------------------------------------------------------------
    // [3] Social Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, const FString& GetItemID); // TODO: ExecuteInteraction 활용 (Get 조건 검사)
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteGiveItem(AActor* TargetActor, const FString& ItemID); // TODO: ExecuteInteraction 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteComfort(AActor* TargetActor); // TODO: 쓰다듬기 인터랙션, 일단은 빈 구현체 혹은 로그 처리
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteHandObject(const FString& ItemID); // TODO: 인벤토리 검사 후 ExecuteEquip 활용

    // ----------------------------------------------------------------------------
    // [4] Task Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecutePickUp(FVector Location); // TODO: Location 반경 내 아이템 오브젝트 탐색 후 ExecuteInteraction("PickUp"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDrop(const FString& ItemID); // TODO: ExecuteInteraction("Drop"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteCraft(const TArray<FString>& ItemIDs); // TODO: ExecuteInteraction("Craft"...) 형태 래핑
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteRepair(const FString& ItemID); // TODO: ExecuteInteraction("Repair"...) 활용 및 스탯 복구

    // ----------------------------------------------------------------------------
    // [5] Investigation Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteInvestigate(FVector Location); // TODO: ExecuteMoveToLocation 후 ExecuteScan 래핑
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteScout(FVector StartLocation, FVector EndLocation); // TODO: 두 장소를 왕복 순찰하도록 ExecuteMoveToLocation 연계

    // ----------------------------------------------------------------------------
    // [6] Lifestyle Behaviors
    // ----------------------------------------------------------------------------
    // TODO: DanceName, SingName은 추후 UEnum 대체 고려 (일단은 FString 파라미터 활용)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteSit(AActor* TargetEntity); // TODO: ExecuteInteraction("SitDown"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteSleep(AActor* TargetEntity); // TODO: ExecuteInteraction("LieDown"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteClean(FVector Location, float Radius); // TODO: ExecuteInteraction("Clean"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteRead(AActor* TargetEntity); // TODO: ExecuteInteraction("Read"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecutePray(FVector Location); // TODO: ExecuteInteraction("Pray"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDance(const FString& DanceName); // TODO: ExecuteInteraction("Dance"...) 우선 활용
    
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteSing(const FString& SingName); // TODO: ExecuteInteraction("Sing"...) 우선 활용

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Social_Dialogue();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Common_Move();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Combat_Attack();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Orchestra_Pipeline();

    UFUNCTION(CallInEditor, Category = "MCP|Debug|Interaction")
    void Debug_Test_Interaction(EAction Action, FString ExtraParams);
};
