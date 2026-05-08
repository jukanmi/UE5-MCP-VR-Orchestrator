#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Entity.h"
#include "../UI/ChatWidget.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "NativeGameplayTags.h"
#include "VRPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UWidgetComponent;
class UWidgetInteractionComponent;

/**
 * Meta Quest 3S 전용 VR 폰.
 * ACharacter 기반으로 캡슐 물리/네비게이션을 유지하면서 HMD + 양손 모션컨트롤러를 올립니다.
 *
 * 좌표계 규칙:
 *  - VROrigin(SceneComponent) = 트래킹 공간 원점 (바닥 기준 룸스케일)
 *  - VRCamera = HMD 위치·방향을 XR 시스템이 자동 갱신
 *  - 캡슐은 매 Tick에 HMD XY 투영 위치로 이동시켜 충돌을 유지 (SyncCapsuleToHMD)
 *
 * 이동:
 *  - 왼손 스틱 → HMD Yaw 방향 기준 스무스 이동
 *  - 오른손 스틱 X → 45° 스냅턴
 *
 * 공격 / 상호작용:
 *  - 오른손 트리거 → 오른손 컨트롤러 Forward 방향 라인트레이스
 *  - A버튼 → DetectNearbyNPC (반경 500cm 중 최근접)
 *
 * UI:
 *  - ChatWidget → 왼쪽 손목에 마운트된 WorldSpace WidgetComponent
 *  - B버튼 → 손목 위젯 토글
 */
UCLASS()
class UE5_MCP_VR_API AVRPawn : public ACharacter, public IPlayerBase
{
    GENERATED_BODY()

public:
    AVRPawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // ============================================================================
    // VR 컴포넌트
    // ============================================================================

    /** 트래킹 공간 원점 — 이 컴포넌트 자식으로 HMD·컨트롤러가 붙음 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    USceneComponent* VROrigin;

    /** HMD 위치 카메라. XR 시스템이 자동으로 위치·방향을 갱신. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    UCameraComponent* VRCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    UMotionControllerComponent* MotionControllerLeft;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    UMotionControllerComponent* MotionControllerRight;

    /** 왼손 메시 (글로브·손 스켈레탈) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    USkeletalMeshComponent* LeftHandMesh;

    /** 오른손 메시 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    USkeletalMeshComponent* RightHandMesh;

    // ============================================================================
    // UI
    // ============================================================================

    /** 왼쪽 손목에 부착된 WorldSpace 위젯 (ChatWidget 표시용) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|UI")
    UWidgetComponent* WristWidgetComp;

    /** 오른손 컨트롤러에서 발사하는 위젯 상호작용 레이 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|UI")
    UWidgetInteractionComponent* WidgetInteractor;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|UI")
    TSubclassOf<UChatWidget> ChatWidgetClass;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|UI")
    UChatWidget* ChatWidgetInstance;

    // ============================================================================
    // AI 퍼셉션 (NPC가 플레이어를 감지하기 위해 필요)
    // ============================================================================

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAIPerceptionStimuliSourceComponent* StimuliSource;

    // ============================================================================
    // 스탯
    // ============================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FPlayerAttributes CurrentStats;

    // ============================================================================
    // 입력 액션 — VR 전용 IMC에서 OpenXR 바인딩
    // ============================================================================

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputMappingContext* VRMappingContext;

    /** 왼손 스틱 XY → 이동 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Move;

    /** 오른손 스틱 X → 스냅턴 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_SnapTurn;

    /** 오른손 트리거 → 공격 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Attack;

    /** A버튼 → NPC 상호작용 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Interact;

    /** B버튼 → 손목 위젯 토글 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_ToggleWristUI;

    /** 오른손 트리거 아날로그 → 위젯 클릭 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_TriggerRight;

    // ============================================================================
    // 이동 설정
    // ============================================================================

    /** 스냅턴 각도 (기본 45°) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "15", ClampMax = "90"))
    float SnapTurnAngle = 45.f;

    /** 스냅턴 쿨다운 — 연속 회전 방지 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "0.1", ClampMax = "1.0"))
    float SnapTurnCooldown = 0.25f;

    // ============================================================================
    // 전투
    // ============================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float AttackDamage = 25.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float AttackRange = 3000.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    UAnimMontage* AttackMontage = nullptr;

    // ============================================================================
    // IPlayerBase / IEntity 구현
    // ============================================================================

    virtual FString GetEntityID_Implementation() const override { return GetName(); }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::Player; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return CurrentStats; }
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const override { return false; }
    virtual FString GetPlayerName_Implementation() const override { return GetName(); }
    virtual FPlayerAttributes GetPlayerAttributes_Implementation() const override { return CurrentStats; }

    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

    // ============================================================================
    // 공개 API
    // ============================================================================

    UFUNCTION(BlueprintCallable, Category = "Stats")
    void ApplyMovementSpeed();

    UFUNCTION(BlueprintCallable, Category = "Stats")
    void RefreshStats();

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
                             AController* EventInstigator, AActor* DamageCauser) override;

private:
    // --- 입력 핸들러 ---
    void OnMove(const FInputActionValue& Value);
    void OnSnapTurn(const FInputActionValue& Value);
    void OnAttack(const FInputActionValue& Value);
    void OnInteract(const FInputActionValue& Value);
    void OnToggleWristUI(const FInputActionValue& Value);
    void OnTriggerRight(const FInputActionValue& Value);

    // --- 로코모션 ---
    /** HMD XY 투영을 캡슐 위치와 동기화 — 매 Tick 호출 */
    void SyncCapsuleToHMD();

    bool bSnapTurnCooling = false;
    FTimerHandle SnapTurnCooldownTimer;

    // --- NPC 상호작용 ---
    void DetectNearbyNPC();
    FString CurrentTargetNPCID;

    // --- 전투 ---
    UFUNCTION()
    void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

    // --- 사망/리스폰 ---
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float RespawnDelay = 5.f;

    void HandleDeath();
    void Respawn();

    bool bHasCheckpoint = false;
    FVector CheckpointLocation;
    FRotator CheckpointRotation;
    float CheckpointHP = 0.f;
    FTimerHandle RespawnTimerHandle;

    // --- 게임플레이 태그 ---
    UPROPERTY(VisibleAnywhere, Category = "Tags")
    FGameplayTagContainer GameplayTags;

    void AddStateTag(FGameplayTag Tag);
    void RemoveStateTag(FGameplayTag Tag);
};
