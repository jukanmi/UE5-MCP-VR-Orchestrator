#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Entity.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "NativeGameplayTags.h"
#include "PlayerGameplayTags.h"
#include "VRPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UAnimMontage;
class USkeletalMeshComponent;
class UVoiceInputComponent;
class UInventoryComponent;
class UPlayerHUDWidget;

/** VR 자세 — HMD Z 높이 비율로 판정. AnimBP/FBIK가 이 값으로 스테이트·이동속도를 결정. */
UENUM(BlueprintType)
enum class EVRPosture : uint8
{
    Standing  UMETA(DisplayName = "Standing"),
    Crouching UMETA(DisplayName = "Crouching"),
    Prone     UMETA(DisplayName = "Prone")
};

/** 자세 전이 알림용 델리게이트 — AnimBP·UI 등이 폴링 대신 이 이벤트로 반응. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVRPostureChanged, EVRPosture, NewPosture);

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

    /** 왼손 Aim 포즈 — 조준·포인터용. Grip 포즈는 손 메시 부착용이라 축이
     *  자연 조준 방향에서 ~30° 기울어져 있어 트레이스에 그대로 못 쓴다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    UMotionControllerComponent* MotionControllerLeftAim;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    UMotionControllerComponent* MotionControllerRight;

    /** 오른손 Aim 포즈 — 조준·발사용. Grip 포즈는 손 메시 부착용이라 축이
     *  자연 조준 방향에서 ~30° 기울어져 있어 트레이스에 그대로 못 쓴다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
    UMotionControllerComponent* MotionControllerRightAim;

    // 손 메시 제거됨 — 풀바디 FBIK 손이 컨트롤러를 향해 역산. 별도 손 메시 중복.
    // 무기·아이템은 X_Bot hand 본 소켓(GetMesh())에 부착.

    /** 음성 입력 — push-to-talk 마이크 캡처 → ASR → transcript → 대화. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASR")
    UVoiceInputComponent* VoiceInput;

    /** 인벤토리 — 슬롯/장비/무게. 기존 UInventoryComponent 재사용. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
    UInventoryComponent* Inventory;

    /** HUD 위젯 클래스 — BP_VRPawn 에서 WBP 지정. 미지정 시 HUD 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

    /** 생성된 HUD 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Category = "UI")
    UPlayerHUDWidget* HUDWidget;

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

    /** Push-to-talk — 누름(Started) StartTalking, 뗌(Completed) StopTalking */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_VoiceInput;

    // ============================================================================
    // 이동 설정
    // ============================================================================

    /** 부드러운 회전 속도(초당 도). 오른 조이스틱 X 로 연속 회전. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "30", ClampMax = "180"))
    float SmoothTurnRate = 90.f;

    /** 조이스틱 회전 입력 데드존. 이 미만은 무시. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "0.05", ClampMax = "0.5"))
    float TurnInputDeadzone = 0.15f;

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
    // 자세 시스템 (HMD Z 높이 기반)
    // ============================================================================

    /** 현재 자세. AnimBP가 Property Access로 워커 스레드에서 읽음. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    EVRPosture CurrentPosture = EVRPosture::Standing;

    /** 캘리브레이션된 사용자 정자세 HMD 높이(cm). 0이면 미보정 상태. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    float CalibratedStandingHeight = 0.f;

    /** 캘리브레이션 완료 여부. UpdatePosture()는 이 플래그가 true여야 작동. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    bool bCalibrated = false;

    /** Stand → Crouch 진입 비율 (H_max 대비). 기본 0.75 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "0.95"))
    float StandingRatioDown = 0.75f;

    /** Crouch → Stand 복귀 비율. StandingRatioDown 보다 커야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "0.95"))
    float StandingRatioUp = 0.80f;

    /** Crouch → Prone 진입 비율. 기본 0.40 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.2", ClampMax = "0.6"))
    float ProneRatioDown = 0.40f;

    /** Prone → Crouch 복귀 비율. ProneRatioDown 보다 커야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.2", ClampMax = "0.6"))
    float ProneRatioUp = 0.45f;

    /** 캡슐 절반 높이 최소값(cm) — 포복 시 적용. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "10.0", ClampMax = "60.0"))
    float MinCapsuleHalfHeight = 22.f;

    /** 캡슐·VR Origin Z 보간 속도. 높을수록 빠르게 따라감. PDF §VInterp 권장 8 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "1.0", ClampMax = "20.0"))
    float HeightInterpSpeed = 8.f;

    /** 캘리브레이션 샘플링 지속시간(초). BeginPlay 직후 이 시간동안 HMD Z 평균. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "5.0"))
    float CalibrationDuration = 2.f;

    /** 시야 높이 오프셋(cm, 음수=내림). VROrigin Z에 더해 카메라+컨트롤러 트래킹
     *  공간을 통째로 내림 — 아바타 머리 본이 시야보다 위로 뜰 때 시야를 머리로 맞춤.
     *  부모(VROrigin)에 적용해 HMD 레이트업데이트가 안 덮음. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "-40.0", ClampMax = "10.0"))
    float CameraHeightOffset = -30.f;

    /** 자세 전이 시 브로드캐스트. AnimBP / UI 가 바인딩. */
    UPROPERTY(BlueprintAssignable, Category = "VR|Posture")
    FOnVRPostureChanged OnPostureChanged;

    /** 외부 트리거(예: 양손 그립 동시 입력)로 캘리브레이션 재시작. */
    UFUNCTION(BlueprintCallable, Category = "VR|Posture")
    void StartCalibration();

    /** 현재 HMD가 바닥(=캡슐 발) 기준으로 얼마나 높이 있는지(cm). */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VR|Posture")
    float GetCurrentHMDHeight() const;

    // ============================================================================
    // FBIK Effector — Control Rig 입력용. 모두 몸체 메시(GetMesh()) 컴포넌트 공간.
    // Control Rig가 컴포넌트 공간에서 풀므로, AnimBP는 이 값을 Target 핀에 직결만 하면 됨.
    // (World→Component 변환·축 정렬을 여기서 일원화 — 블루프린트 invert/multiply 불필요)
    // ============================================================================

    /** 손 그립 축 보정 — 컨트롤러 그립 포즈 축과 메시 손 본 축이 달라서 생기는
     *  손목 회전 오차를 상쇄. 손 로컬 공간에 적용되므로 손이 움직여도 유지됨.
     *  에디터 Details 에서 라이브 튜닝(리빌드 불필요). 좌우 미러라 값이 다름. */
    // X_Bot 본 축과 HMD/컨트롤러 축 차이 보정. 손 로컬 공간 우측곱(손 회전해도 유지).
    // FRotator(Pitch, Yaw, Roll). 에디터 Details 라이브 튜닝(리빌드 불필요).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK")
    FRotator LeftHandGripOffset = FRotator(180.f, 0.f, 90.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK")
    FRotator RightHandGripOffset = FRotator(0.f, 0.f, -90.f);

    /** 머리 본 축 보정 — HMD 카메라 축과 head 본 축 차이 상쇄(머리 꺾임 교정). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK")
    FRotator HeadEffectorOffset = FRotator(0.f, -90.f, 90.f);

    /** 캘리브레이션 시 아바타를 플레이어 키 비율로 스케일할지. 끄면 네이티브 크기.
     *  켜면 아바타 팔길이도 같이 줄어 손 IK 타겟에 자연히 닿음(팔꿈치 과접힘 방지). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK")
    bool bScaleAvatarToPlayer = true;

    /** 아바타 네이티브 정자세 눈높이(cm). 스케일 = CalibratedStandingHeight / 이 값.
     *  X_Bot 기준 실측해 조정(팔 길이가 맞을 때까지). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK", meta = (ClampMin = "120.0", ClampMax = "200.0"))
    float AvatarReferenceHeight = 170.f;

    /** HMD(VRCamera)를 몸체 메시 공간으로 변환한 Head Effector Transform. */
    UFUNCTION(BlueprintPure, Category = "VR|IK")
    FTransform GetHeadEffectorCS() const;

    /** 왼손 컨트롤러를 몸체 메시 공간으로 변환한 Left Hand Effector Transform. */
    UFUNCTION(BlueprintPure, Category = "VR|IK")
    FTransform GetLeftHandEffectorCS() const;

    /** 오른손 컨트롤러를 몸체 메시 공간으로 변환한 Right Hand Effector Transform. */
    UFUNCTION(BlueprintPure, Category = "VR|IK")
    FTransform GetRightHandEffectorCS() const;

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

    /** 체크포인트 액터가 호출 — 현재 위치/HP를 저장 */
    void SaveCheckpoint(const FVector& Location, const FRotator& Rotation);

private:
    // --- 입력 핸들러 ---
    void OnMove(const FInputActionValue& Value);
    void OnTurn(const FInputActionValue& Value);
    void OnTurnReleased(const FInputActionValue& Value);
    void OnAttack(const FInputActionValue& Value);
    void OnInteract(const FInputActionValue& Value);
    void OnVoiceStart(const FInputActionValue& Value);
    void OnVoiceStop(const FInputActionValue& Value);

    /** ASR transcript 확정 → NPCManager::SendPlayerDialogue 로 전달. */
    void HandleVoiceTranscript(const FString& PlayerId, const FString& TargetNpc, const FString& Transcript);

    // --- 로코모션 ---
    /** HMD XY 투영을 캡슐 위치와 동기화 — 매 Tick 호출 */
    void SyncCapsuleToHMD();

    /** 오른 조이스틱 X 입력만큼 액터를 매 프레임 연속 회전 — 매 Tick 호출 */
    void UpdateSmoothTurn(float DeltaTime);

    /** 현재 회전 조이스틱 X 입력값(-1~1). 입력 핸들러가 갱신, Tick이 소비. */
    float TurnAxisInput = 0.f;

    // --- 자세 시스템 내부 상태 ---

    /** 생성자에서 캐싱한 기본 캡슐 절반 높이. 동적 리사이즈의 상한·역보정 기준. */
    float BaseCapsuleHalfHeight = 88.f;

    /** 현재 보간된 캡슐 절반 높이 — Tick에서 타겟값으로 InterpTo */
    float InterpedCapsuleHalfHeight = 88.f;

    /** 캘리브레이션 샘플링 누적값·횟수 */
    float CalibrationAccum = 0.f;
    int32 CalibrationSampleCount = 0;
    FTimerHandle CalibrationSampleTimer;
    FTimerHandle CalibrationFinishTimer;

    /** 캘리브레이션 샘플 1회 (타이머에서 호출) */
    void SampleCalibration();

    /** 캘리브레이션 종료 — 평균값 확정 + bCalibrated=true */
    void FinishCalibration();

    /** 매 Tick — HMD Z 비율로 자세 전이 판정 (히스테리시스) */
    void UpdatePosture();

    /** 자세 전이 — Enum/태그 갱신 + 이동속도 적용 + OnPostureChanged 브로드캐스트 */
    void TransitionTo(EVRPosture NewPosture);

    /** 매 Tick — 동적 캡슐 리사이즈 + Rising Floor 역보정 (VInterp 스무딩) */
    void UpdateDynamicCapsule(float DeltaTime);

    // --- NPC 상호작용 ---
    void DetectNearbyNPC();

    /** DetectNearbyNPC 가 지정한 발화 대상 NPC. (이후 음성 입력 단계에서 사용) */
    UPROPERTY(BlueprintReadWrite, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
    FString CurrentTargetNPCID;

    /** 콘솔에서 플레이어 발화를 최근접 NPC로 전송 (단순 대화). 예: SendNPCDialogue "안녕" */
    UFUNCTION(Exec)
    void SendNPCDialogue(const FString& Text);

    /** 콘솔 진단 — 아바타 팔길이 vs 컨트롤러 도달거리 + 현재 스케일 로그/화면 출력.
     *  팔 뻗은 자세에서 호출해 비율 확인. Reach > ArmLen 이면 아바타 팔이 짧음. */
    UFUNCTION(Exec)
    void LogIKMetrics();

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
