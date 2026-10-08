#pragma once

#include "CoreMinimal.h"
#include "Core/Utils/MovementUtils.h"   // FSavedFriction
#include "Core/Utils/PawnDeathUtils.h"  // FCheckpoint
#include "GameFramework/Character.h"
#include "Core/Interfaces/Entity.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "NativeGameplayTags.h"
#include "Core/Types/PlayerGameplayTags.h"
#include "HeadMountedDisplayTypes.h"
#include "VRPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UAnimMontage;
class USkeletalMeshComponent;
class UInventoryComponent;
class UVRPlayerUIComponent;
class UVRMeleeComponent;
class UVRBodyMeasureComponent;
class USphereComponent;
class UVRHandComponent;
class AKineticProjectile;
class UWidgetComponent;
class UStaticMeshComponent;
class ADroppedItemBase;
struct FItemData;

/** VR 자세 — HMD 높이 비율(서기·쭈그리기·엎드리기) + HMD 피치와 높이 구간(허리 숙이기)으로 판정.
 *  AnimBP/FBIK가 이 값으로 스테이트·이동속도를 결정. 기존 값 번호 유지를 위해 Bending 은 끝에 추가. */
UENUM(BlueprintType)
enum class EVRPosture : uint8
{
    Standing  UMETA(DisplayName = "Standing"),
    Crouching UMETA(DisplayName = "Crouching"),
    Prone     UMETA(DisplayName = "Prone"),
    Bending   UMETA(DisplayName = "Bending")
};

/** 자세 전이 알림용 델리게이트 — AnimBP·UI 등이 폴링 대신 이 이벤트로 반응. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVRPostureChanged, EVRPosture, NewPosture);

class AFurnitureActor;
class UCapsuleComponent;

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

    /** 동역학 근접 전투 — 손 속도 추적·손/무기로 치기(½mv²)·막기/패링·J→HP 환산(던지기·투사체도 사용). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Kinetic")
    UVRMeleeComponent* MeleeCombat;

    // 손 위치 표시용 구(충돌 없음, 손 본 소켓). 실제 타격 판정은 UVRMeleeComponent 의 능동 오버랩.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Kinetic")
    USphereComponent* MeleeSphereLeft;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Kinetic")
    USphereComponent* MeleeSphereRight;

    // ── 손 — 트래킹 목표(앵커)와 물리 손바닥을 분리해 벽에 손이 막히게 한다(UVRHandComponent) ──
    // FBIK 손과 GetHandLocation(거래·막기 판정)은 물리 손바닥을 따르고, 잡기·던지기는 실제 손(앵커)을 쓴다.

    /** 왼손 — 컨트롤러 Grip 포즈에 붙은 앵커이자 물리 손바닥·손가락·쥐기 제약의 주인. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Hand")
    UVRHandComponent* HandLeft;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Hand")
    UVRHandComponent* HandRight;

    UVRHandComponent* GetHand(EControllerHand Hand) const { return Hand == EControllerHand::Left ? HandLeft : HandRight; }
    UVRHandComponent* GetHand(bool bLeft) const { return bLeft ? HandLeft : HandRight; }

    /** 이번 틱 핸드트래킹 관절 상태. 추적 중이 아니면 bValid=false. */
    const FXRHandTrackingState& GetHandTrackState(EControllerHand Hand) const;

    /** 인벤토리 — 슬롯/장비/무게. 기존 UInventoryComponent 재사용. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
    UInventoryComponent* Inventory;

    // ── UI — 위젯 패널·포인터·이름표의 생성·상태·튜닝값은 전부 UVRPlayerUIComponent 가 갖는다. 폰은 입력을 넘기고 질의만 한다 ──

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    UVRPlayerUIComponent* PlayerUI;

    /** 신체 측정 상태기계(설정 화면의 측정 버튼으로 시작, 왼손 X 로 단계 진행) — 저장한 키가 있으면 시작 때 자동 캘리브레이션 대신 쓴다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    UVRBodyMeasureComponent* BodyMeasure;

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

    /** 왼손 Y버튼 → 인벤토리 HUD 열기/닫기 토글 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_InventoryToggle;

    /** 왼손 Menu 버튼 → 메뉴 열기/닫기 토글 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_MenuToggle;

    /** 왼손 X버튼 → 신체 측정 중 지금 단계 값을 잰다. 측정 중이 아니면 무시. 메뉴 입력 차단과 무관하게 항상 받는다. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_BodyMeasureCapture;

    /** 오른손 B버튼 → 대쉬. 왼손 스틱을 밀고 있으면 그 방향, 중립이면 HMD 정면. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Dash;

    /** 오른손 그립 → 손 근처 월드 아이템 직접 쥐기. 놓으면 던지거나(속도) NPC 에게 건넨다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Grab;

    /** 왼손 그립 — 오른손과 같은 동작을 왼손(OffHand)에 대해 수행한다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_GrabLeft;

    // ============================================================================
    // 이동 설정
    // ============================================================================

    /** 부드러운 회전 속도(초당 도). 오른 조이스틱 X 로 연속 회전. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "30", ClampMax = "180"))
    float SmoothTurnRate = 90.f;

    /** 조이스틱 회전 입력 데드존. 이 미만은 무시. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "0.05", ClampMax = "0.5"))
    float TurnInputDeadzone = 0.15f;

    /** 달리기 진입 스틱 magnitude 임계. 별도 입력 액션 없이 스틱을 끝까지 밀면 Sprint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float SprintThreshold = 0.9f;

    /** 현재 달리는 중인지. Standing 자세에서만 SprintSpeed 가 적용됨(ApplyMovementSpeed). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Locomotion")
    bool bIsSprinting = false;

    // ============================================================================
    // 스태미나 — Sprint 소모·고갈·회복
    // ============================================================================

    /** Sprint 지속 시 초당 스태미나 소모량. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stamina", meta = (ClampMin = "0.0"))
    float SprintStaminaCostPerSec = 12.0f;

    /** Sprint 중단 후 회복이 시작되기까지의 지연(초). 회복 속도 자체는 신규 값을 만들지 않고
     *  스탯 파생값인 Resources.StaminaRegen 을 그대로 쓴다(캐릭터 능력치가 회복력에 반영됨). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stamina", meta = (ClampMin = "0.0"))
    float StaminaRegenDelaySec = 1.5f;

    /** 고갈 후 Sprint 재허용 임계 — MaxStamina 대비 비율. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stamina", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SprintUnlockStaminaRatio = 0.25f;

    /** 고갈로 강제 해제된 상태. 회복이 SprintUnlockStaminaRatio 를 넘을 때까지 Sprint 재진입 차단.
     *  이게 없으면 고갈→해제→즉시재시도 가 매 틱 반복되며 덜덜 떨린다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Stamina")
    bool bStaminaExhausted = false;

    /** 마지막으로 스태미나를 소모한 뒤 흐른 시간(초). StaminaRegenDelaySec 비교용. */
    float TimeSinceSprintStopped = 0.f;

    // ============================================================================
    // 대쉬 — 짧은 등속 회피 이동
    // ============================================================================

    /** 1회 대쉬 이동 거리(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Dash", meta = (ClampMin = "50.0"))
    float DashDistance = 300.f;

    /** 대쉬 지속 시간(초). 속도는 DashDistance/DashDuration 으로 파생된다.
     *  VR 멀미는 빠른 직선 이동이 만드는 시야 광류에서 오므로 노출 시간이 짧을수록 덜하다.
     *  이 값을 하한(0.02)까지 낮추면 사실상 순간이동이 된다 — 대쉬가 불편하면 코드가 아니라
     *  이 값을 먼저 줄인다. 화면 터널 비네트로 완화하는 통상적인 방법은 이 프로젝트 렌더
     *  경로에서 PostProcess 가 동작하지 않아 쓸 수 없다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Dash", meta = (ClampMin = "0.02", ClampMax = "1.0"))
    float DashDuration = 0.15f;

    /** 연속 대쉬 최소 간격(초). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Dash", meta = (ClampMin = "0.0"))
    float DashCooldownSec = 0.8f;

    /** 1회 대쉬 스태미나 소모량. 모자라면 발동하지 않는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Dash", meta = (ClampMin = "0.0"))
    float DashStaminaCost = 20.f;

    /** 디버그 전용 — true 면 대쉬가 스태미나를 소모/요구하지 않는다(쿨다운은 그대로). 기본 false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Dash|Debug")
    bool bDebugDashFreeStamina = false;

    // ============================================================================
    // 전투
    // ============================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    UAnimMontage* AttackMontage = nullptr;

    // 동역학 데미지 튜닝(½·m·v²·환산·상한)·막기/패링은 UVRMeleeComponent.

    /** IA_Attack 으로 발사할 투사체 클래스. BP_KineticProjectile 지정. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Kinetic")
    TSubclassOf<AKineticProjectile> ProjectileClass;

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

    /** Standing → Bending 진입 HMD 피치 하향 각도(도). 이 각도 이상 숙이고 높이도 허리 숙이기 구간이어야 진입. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "10.0", ClampMax = "80.0"))
    float BendPitchDownDeg = 30.f;

    /** Bending → Standing 복귀 피치 하향 각도(도). BendPitchDownDeg 보다 작아야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "5.0", ClampMax = "70.0"))
    float BendPitchUpDeg = 20.f;

    /** Standing → Bending 진입 높이 비율 상한. 쭈그리기 진입(StandingRatioDown) 이상 ~ 이 값 미만에서만 허리 숙이기.
     *  서서 고개만 숙이면 비율이 거의 안 내려가 이 값 이상에 머물러 Standing 으로 남는다.
     *  StandingRatioDown < BendRatioMax < BendRatioMaxUp 이어야 함. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.8", ClampMax = "1.0"))
    float BendRatioMax = 0.93f;

    /** Bending → Standing 복귀 높이 비율. BendRatioMax 보다 커야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.8", ClampMax = "1.0"))
    float BendRatioMaxUp = 0.95f;

    /** 캡슐 절반 높이 최소값(cm) — 포복 시 적용. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "10.0", ClampMax = "60.0"))
    float MinCapsuleHalfHeight = 22.f;

    /** 캡슐·VR Origin Z 보간 속도. 높을수록 빠르게 따라감. 권장값 8 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "1.0", ClampMax = "20.0"))
    float HeightInterpSpeed = 8.f;

    /** 캘리브레이션 샘플링 지속시간(초). BeginPlay 직후 이 시간동안 HMD Z 평균. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "5.0"))
    float CalibrationDuration = 2.f;

    /** 시야 높이 미세 오프셋(cm). VROrigin Z에 더함. **기본 0 — 비워둘 것.**
     *  ⚠️ 0이 아니면 카메라 월드Z가 시프트되고, GetCurrentHMDHeight()가 그 카메라Z를
     *  역산하므로 보고 높이가 오염→캡슐(아바타 몸)이 오프셋만큼 짧아짐→카메라가 짧은 몸
     *  위로 떠 '시야 너무 높음'. 과거 -30 이 정확히 이 버그를 유발(아바타 머리 뜸을
     *  카메라 침몰로 가리려다 역효과). 머리 본이 뜨면 여기 말고 HeadEffectorOffset/
     *  FBIK head effector 에서 교정할 것. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "-20.0", ClampMax = "20.0"))
    float CameraHeightOffset = 0.f;

    /** 자세 전이 시 브로드캐스트. AnimBP / UI 가 바인딩. */
    UPROPERTY(BlueprintAssignable, Category = "VR|Posture")
    FOnVRPostureChanged OnPostureChanged;

    /** 외부 트리거(예: 양손 그립 동시 입력)로 캘리브레이션 재시작. */
    UFUNCTION(BlueprintCallable, Category = "VR|Posture")
    void StartCalibration();

    /** 현재 HMD가 바닥(=캡슐 발) 기준으로 얼마나 높이 있는지(cm). */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VR|Posture")
    float GetCurrentHMDHeight() const;

    /** 월드 Z 를 바닥(=캡슐 발) 기준 높이(cm)로 바꾼다. GetCurrentHMDHeight 와 같은 좌표계 — 신체 측정이 컨트롤러 높이를 잴 때 쓴다. */
    float HeightAboveFloor(float WorldZ) const;

    /** 자세 판정 기준 높이를 확정한다(자동 샘플링 중단 + bCalibrated). 저장된 신체 측정값 적용·측정 직후 갱신 공통 경로. */
    void SetStandingHeight(float Height);

    // ============================================================================
    // FBIK Effector — Control Rig 입력용. 모두 몸체 메시(GetMesh()) 컴포넌트 공간.
    // Control Rig가 컴포넌트 공간에서 풀므로, AnimBP는 이 값을 Target 핀에 직결만 하면 됨.
    // (World→Component 변환·축 정렬을 여기서 일원화 — 블루프린트 invert/multiply 불필요)
    // ============================================================================

    // 손 그립 축 보정은 손 컴포넌트(UVRHandComponent::GripOffset).

    /** 머리 본 축 보정 — HMD 카메라 축과 head 본 축 차이 상쇄(머리 꺾임 교정). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK")
    FRotator HeadEffectorOffset = FRotator(0.f, -90.f, 90.f);

    /** 눈(HMD) → 머리 본 위치 오프셋(HMD 로컬, X=앞 Z=위, cm). 머리 이펙터 위치 = HMD + 머리 회전 × 이 값.
     *  머리 본은 눈보다 뒤·아래(목 위)에 있다 — 눈 위치를 그대로 넘기면 FBIK 가 척추를 앞으로 당긴다. PIE 중 즉시 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|IK")
    FVector EyeToHeadOffset = FVector(-9.f, 0.f, -10.f);

    /** 눈(HMD) → 목 위치 오프셋(HMD 로컬, cm). 몸 메시를 눈이 아니라 이 목 위치 아래에 놓는다 —
     *  고개를 숙이거나 돌리면 눈은 앞으로 나가지만 몸통은 제자리라, 눈 아래에 두면 몸 전체가 끌려간다. PIE 중 즉시 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body")
    FVector EyeToNeckOffset = FVector(-10.f, 0.f, -20.f);

    /** 메시 정면 보정 오프셋 (Mixamo X_Bot 등 표준 스켈레탈 메시는 -90도 회전 시 캐릭터 전방 정렬). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body")
    float BodyMeshYawOffset = -90.f;

    /** 몸(메시) 회전 추종 보간 속도 (0이면 HMD 시선과 즉시 1:1 동기화, >0이면 부드럽게 추종). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body", meta = (ClampMin = "0.0"))
    float BodyRotationInterpSpeed = 0.f;

    // 아바타 키 스케일 기능 제거 — 항상 네이티브 1:1(머리=HMD·손=컨트롤러 실위치).
    // 스케일은 짧게 적용 시 FBIK 가 머리를 HMD 까지 못 늘려 '머리 낮음' 버그만 유발했음.

    /** HMD(VRCamera)를 몸체 메시 공간으로 변환한 Head Effector Transform. */
    UFUNCTION(BlueprintPure, Category = "VR|IK")
    FTransform GetHeadEffectorCS() const;

    /** 왼손 Effector — 위치·회전 모두 물리 손바닥(벽에 막힘). 몸체 메시 공간. */
    UFUNCTION(BlueprintPure, Category = "VR|IK")
    FTransform GetLeftHandEffectorCS() const;

    /** 오른손 Effector — 위치·회전 모두 물리 손바닥(벽에 막힘). 몸체 메시 공간. */
    UFUNCTION(BlueprintPure, Category = "VR|IK")
    FTransform GetRightHandEffectorCS() const;

    // ============================================================================
    // IPlayerBase / IEntity 구현
    // ============================================================================

    virtual FString GetEntityID_Implementation() const override { return TEXT("Player"); }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::Player; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return CurrentStats; }

    virtual void ApplyResourceDelta_Implementation(float DeltaHealth, float DeltaMana, float DeltaStamina) override
    { CurrentStats.Resources.ApplyDelta(DeltaHealth, DeltaMana, DeltaStamina); }
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const override { return false; }
    virtual FString GetPlayerName_Implementation() const override { return TEXT("Player"); }
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

    /** 손에 쥔 아이템을 인벤토리에 집어넣고 월드 액터를 파괴한다.
     *  인벤토리가 가득 찼거나 데이터가 없으면 물리를 되살려 그 자리에 떨군다(증발 방지). */
    UFUNCTION(Exec, BlueprintCallable, Category = "VR|Interaction")
    bool StoreHeldItemInInventory();

    /** 콘솔 채팅: 현재 타겟 NPC(없으면 근접 탐지)에게 텍스트 발화. 띄어쓰기 포함 시 따옴표 —
     *  `SayToNpc "안녕 뭐해"`. HUD ChatInput 과 같은 전송 경로. */
    UFUNCTION(Exec, BlueprintCallable, Category = "VR|Interaction")
    void SayToNpc(const FString& Text);

private:
    // --- 입력 핸들러 ---
    void OnMove(const FInputActionValue& Value);

    /** 스틱 놓음(Completed/Canceled) — Sprint 해제. OnMove 는 Triggered 전용이라
     *  입력이 0이 되는 순간 호출되지 않으므로 해제는 반드시 여기서. */
    void OnMoveReleased(const FInputActionValue& Value);

    /** 이동 종료 시 Move 태그를 회수하고 Idle 로 되돌린다. */
    void StopMoveState();

    void OnTurn(const FInputActionValue& Value);
    void OnTurnReleased(const FInputActionValue& Value);
    void OnAttack(const FInputActionValue& Value);

    /** 트리거 뗌 — 인벤토리 열림 중 눌렀던 UI 포인터를 놓는다. 닫힘 상태에선 no-op. */
    void OnAttackReleased(const FInputActionValue& Value);
    void OnInteract(const FInputActionValue& Value);
    /** Enter 키 — HUD 채팅 칸에 포커스. 입력 중 Enter 는 텍스트박스가 먼저 먹으므로 여기 안 온다. */
    void OnChatKey();
    void OnInventoryToggle(const FInputActionValue& Value);
    void OnMenuToggle(const FInputActionValue& Value);
    void OnBodyMeasureCapture(const FInputActionValue& Value);

    // --- 로코모션 ---
    /** HMD XY 투영을 캡슐 위치와 동기화 — 매 Tick 호출 */
    void SyncCapsuleToHMD();

    /** 오른 조이스틱 X 입력만큼 액터를 매 프레임 연속 회전 — 매 Tick 호출 */
    void UpdateSmoothTurn(float DeltaTime);

    /** 아바타 몸(스켈레탈 메시)이 HMD(헤드셋) 시선 Yaw를 항상 바라보도록 정렬 — 매 Tick 호출 */
    void UpdateBodyRotation(float DeltaTime);

    /** 현재 회전 조이스틱 X 입력값(-1~1). 입력 핸들러가 갱신, Tick이 소비. */
    float TurnAxisInput = 0.f;

    /** Sprint 상태 갱신 — 값이 바뀔 때만 ApplyMovementSpeed() 재적용(매 Triggered 마다 쓰기 방지). */
    void SetSprinting(bool bNewSprinting);

    /** 매 Tick — Sprint 스태미나 소모 / 지연 후 회복 / 고갈 해제 판정 */
    void UpdateStamina(float DeltaTime);

    /** 대쉬 입력(B버튼) — 쿨다운·스태미나·자세 검사 후 등속 이동 시작 */
    void OnDash(const FInputActionValue& Value);

    /** 매 Tick — 대쉬 잔여 시간 소진 시 StopDash */
    void UpdateDash(float DeltaTime);

    // --- 손 ---
    /** 매 Tick — 손마다 트래킹·드라이브 → 제스처 → 접촉 쥐기 → 쥐기 제약 동기화. 바뀐 제스처·접촉 쥐기는 잡기 입력으로 넘긴다. */
    void UpdateHands(float DeltaTime);

    /** 마찰·제동 원복 + 수평 잔류 속도 제거. 정상 종료·중단 공통 경로. */
    void StopDash();

    bool bDashActive = false;
    float DashTimeRemaining = 0.f;

    /** 마지막 대쉬 시각(초). 쿨다운 비교용. 첫 대쉬가 막히지 않게 충분히 과거로 초기화. */
    float LastDashTime = -1000.f;

    /** 대쉬 중 0 으로 덮어쓰는 이동 파라미터 원본 — StopDash 가 되돌린다. */
    FSavedFriction SavedDashFriction;

    /** 직전 왼손 스틱 입력. 대쉬 방향 산출용 — 입력 핸들러가 갱신한다. */
    FVector2D LastMoveInput = FVector2D::ZeroVector;

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
    void TransitionTo(EVRPosture NewPosture, float Ratio, float PitchDownDeg);

    /** 매 Tick — 동적 캡슐 리사이즈 + Rising Floor 역보정 (VInterp 스무딩) */
    void UpdateDynamicCapsule(float DeltaTime);

    /** 매 Tick — 몸 메시를 발은 바닥에, 수평은 목 아래에 둔다. 캡슐이 줄어도 메시가 바닥 아래로 가라앉지 않게. */
    void UpdateBodyPlacement();

    /** BeginPlay 시점 메시 상대 위치(기본 캡슐 높이 기준). 캡슐 높이 변화만큼 Z 를 보정한다. */
    FVector MeshBaseRelativeLocation = FVector::ZeroVector;

    // --- NPC 상호작용 ---
    void DetectNearbyNPC();

    /** DetectNearbyNPC 가 지정한 발화 대상 NPC. (이후 음성 입력 단계에서 사용) */
    UPROPERTY(BlueprintReadWrite, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
    FString CurrentTargetNPCID;

    // --- 가구 착석 (Interact 토글) ---
    /** Interact 시 착석 판정 반경(cm) — 이내 최근접 빈 가구가 있으면 NPC 감지보다 착석 우선. */
    UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = "50.0", ClampMax = "500.0"))
    float FurnitureInteractRange = 150.f;

    /** 착석 중 가구 — 유효하면 스틱 이동 잠금, Interact 재입력 시 기상(토글). */
    TWeakObjectPtr<AFurnitureActor> SeatedFurniture;

    /** 반경 내 최근접 빈 가구 착석 시도(점유 + SeatPoint 스냅). 성공 시 true — OnInteract 가 NPC 감지 생략. */
    bool TrySitOnNearbyFurniture();

    /** 기상 — 점유 해제 + 좌석 전방 반보 이탈(의자 콜리전 끼임 방지). */
    void StandUpFromFurniture();

    // --- 월드 아이템 픽업 (Interact) ---
    /** Interact 시 픽업 판정 반경(cm) — 이내 최근접 ADroppedItemBase 를 줍는다. */
    UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = "30.0", ClampMax = "300.0"))
    float PickupInteractRange = 150.f;

    /** 반경 내 최근접 드랍 아이템을 인벤토리로 획득. 성공 시 true — OnInteract 가 착석·NPC 감지 생략. */
    bool TryPickupNearby();

    /** 원점 반경 내 최근접 드랍 아이템. 거래 접시 잠금품은 항상 제외, 진열품은 bIncludeDisplayed 일 때만(그랩·이름표) — Interact 픽업은 공짜 획득이라 제외. */
    ADroppedItemBase* FindNearestItem(const FVector& Origin, float Radius, bool bIncludeDisplayed = false) const;

    // --- 물리 손 쥐기 (Grip) ---
    // 쥔 아이템 자체와 손안 자세 보정은 InventoryComponent 가 들고 있다 — 장착 슬롯과 같은
    // 보유 상태라서, 폰에 두면 NPC·상자 등 다른 소유자가 같은 걸 다시 구현해야 한다.
    // 폰에는 입력·손 속도·던지기처럼 VR 컨트롤러가 있어야만 되는 것만 남긴다.

    /** 그립 시 손 위치 기준 쥐기 반경(cm). 실제로 손을 뻗어야 잡히도록 짧게 둔다. */
    UPROPERTY(EditAnywhere, Category = "Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "5.0", ClampMax = "100.0"))
    float GrabRadius = 40.f;

    /** 던질 때 손 속도에 곱하는 배율. 1 = 실제 손 속도. VR 은 팔 스윙이 짧아 살짝 키우는 편이 자연스럽다. */
    UPROPERTY(EditAnywhere, Category = "Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "0.1", ClampMax = "5.0"))
    float ThrowVelocityScale = 1.3f;

    /** 건네기 판정 거리(cm) — 이 안에 NPC 가 있으면 놓아도 던지지 않고 NPC 인벤토리로 들어간다. */
    UPROPERTY(EditAnywhere, Category = "Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "30.0", ClampMax = "300.0"))
    float HandOverRange = 120.f;

    /** 그립 누름 — 인벤토리가 열려 있으면 고른 슬롯을 꺼내고, 아니면 손 근처 아이템을 쥔다. */
    void OnGrabStartRight(const FInputActionValue& Value);
    void OnGrabStartLeft(const FInputActionValue& Value);

    /** 그립 누름 본체. 왼손이면 OffHand, 오른손이면 MainHand 슬롯을 대상으로 같은 일을 한다.
     *  Target 이 있으면(접촉 쥐기) 그 아이템을, 없으면 손 근처 최근접 아이템을 쥔다. */
    void HandleGrabStart(bool bLeft, ADroppedItemBase* Target = nullptr);

    /** PIE 시작 때 콜리전 표시(show collision)를 켠다 — 손 콜라이더와 메시 맞춤을 볼 때 매번 콘솔에 치지 않게. 개발용. */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (AllowPrivateAccess = "true"))
    bool bShowCollisionOnStart = false;

    /** 그립 뗌 — 인벤토리가 열려 있으면 회수, 닫혀 있으면 거래 접시·NPC 를 차례로 보고 던진다. */
    void OnGrabReleaseRight(const FInputActionValue& Value);
    void OnGrabReleaseLeft(const FInputActionValue& Value);

    /** 그립 뗌 본체 — 인벤토리 열림이면 회수, 닫힘이면 거래접시→NPC 건네기→던지기. */
    void HandleGrabRelease(bool bLeft);

    /** 그립 아날로그값(누르는 동안 매 프레임) — 손의 마찰 한계로 넘기고, 약하게 쥐어 못 든 아이템은 더 세게 쥐면 다시 쥔다. */
    void OnGrabValueRight(const FInputActionValue& Value);
    void OnGrabValueLeft(const FInputActionValue& Value);
    void HandleGripValue(bool bLeft, float Value);

    /** 잡기 입력 = 컨트롤러 그립 OR 접촉 쥐기. 합친 값이 바뀔 때만 HandleGrabStart/Release 를 부른다 —
     *  한쪽이 쥔 채 다른 쪽이 떨어져도 놓지 않는다. */
    void UpdateGrabInput(bool bLeft, ADroppedItemBase* Target = nullptr);

    /** 컨트롤러 그립 누름 상태(왼손 0·오른손 1). */
    bool bGripHeld[2] = { false, false };

    /** 합친 잡기 입력의 직전 값(왼손 0·오른손 1). */
    bool bGrabInputActive[2] = { false, false };

public:
    /** 인벤토리 패널이 열려 있는가 — 열림 중엔 트리거·스틱·그립의 뜻이 바뀐다(UI 클릭·슬롯 이동·슬롯 발동). */
    bool IsInventoryOpen() const;

    /** UI(메뉴)가 게임 입력을 막고 있는지 — 이동·회전·공격·잡기·대시·상호작용을 무시한다. */
    bool IsUIBlockingInput() const;

    /** 손 근처 반경 내 최근접 드랍 아이템. 쥐기와 이름표가 같은 판정을 쓰도록 한 곳에 둔다. */
    ADroppedItemBase* FindNearestItemNearHand(float Radius, bool bLeft) const;

    /** 보이는 손(물리 손바닥) 월드 위치.public:
    /** 보이는 손(물리 손바닥) 월드 위치. 시뮬레이션 전엔 모션 컨트롤러. 거래 패널 물리 버튼 등 손 근접 판정용. */
    UFUNCTION(BlueprintPure, Category = "VR")
    FVector GetHandLocation(bool bRightHand) const;

private:

    // --- 전투 ---
    UFUNCTION()
    void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

    // --- 사망/리스폰 ---
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float RespawnDelay = 5.f;

    void HandleDeath();
    void Respawn();

    FCheckpoint Checkpoint;
    FTimerHandle RespawnTimerHandle;

    // --- 게임플레이 태그 ---
    UPROPERTY(VisibleAnywhere, Category = "Tags")
    FGameplayTagContainer GameplayTags;

    void AddStateTag(FGameplayTag Tag);
    void RemoveStateTag(FGameplayTag Tag);
};
