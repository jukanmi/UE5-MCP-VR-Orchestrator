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
#include "VRPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UAnimMontage;
class USkeletalMeshComponent;
class UInventoryComponent;
class UPlayerHUDWidget;
class USphereComponent;
class AKineticProjectile;
class UWidgetComponent;
class UWidgetInteractionComponent;
class UStaticMeshComponent;
class ADroppedItemBase;
class UMaterialInstanceDynamic;
struct FItemData;

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

class AFurnitureActor;

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

    // 동역학 근접 — 손 본 소켓에 부착된 타격 구체. NPC overlap 시 손 속도로 ½mv² 데미지.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Kinetic")
    USphereComponent* MeleeSphereLeft;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Kinetic")
    USphereComponent* MeleeSphereRight;

    /** 인벤토리 — 슬롯/장비/무게. 기존 UInventoryComponent 재사용. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
    UInventoryComponent* Inventory;

    /** HUD 위젯 클래스 — BP_VRPawn 에서 WBP 지정. 미지정 시 HUD 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

    /** 생성된 HUD 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Category = "UI")
    UPlayerHUDWidget* HUDWidget;

    /** HUD 를 3D 공간에 띄우는 위젯 컴포넌트 — 왼손 컨트롤러 부착.
     *  AddToViewport 는 VR 에서 쓰면 안 된다: OpenXR 은 양안을 한 장의 스테레오 타깃에
     *  렌더하고 Slate 오버레이는 그 위에 한 번만 합성되므로, 화면 공간 위젯은 한쪽 눈에만
     *  뜨거나 좌우로 늘어져 보인다. 월드 공간 위젯은 씬과 같이 양안 렌더되어 정상. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    UWidgetComponent* HUDWidgetComp;

    /** 오른손 UI 포인터 — 왼손 패널의 슬롯을 조준·클릭. 월드 공간 위젯은 마우스가 없으므로
     *  이 컴포넌트가 광선을 쏴 가상 포인터 이벤트로 변환한다. 없으면 패널이 보이기만 하고
     *  아무것도 눌리지 않는다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    UWidgetInteractionComponent* HUDInteractor;

    /** 포인터 광선 메시 — 조준 방향으로 뻗는 가는 원통. WidgetInteraction 의 bShowDebug 는
     *  DrawDebug 라 Shipping 빌드에서 통째로 컴파일 제외되므로, 출시본에도 남는 실메시로 그린다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    UStaticMeshComponent* PointerBeam;

    /** 광선이 실제로 맞은 지점에 놓이는 작은 구. 맞은 게 없으면 숨는다 —
     *  광선 끝이 허공이면 "지금 아무것도 안 겨눴다"가 그 자체로 표시된다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    UStaticMeshComponent* PointerDot;

    /** 아이템 이름표 — 월드에 떨어진 아이템 위에 뜬다. 아이템마다 위젯을 달면 개수만큼
     *  틱이 늘어나므로, 폰이 하나만 들고 대상만 바꿔 옮겨 쓴다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    UWidgetComponent* ItemTooltipComp;

    /** 패널의 왼손 컨트롤러 기준 위치(cm). 손등 위쪽에 얹히는 값이 기본. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FVector HUDPanelLocation = FVector(4.f, 0.f, 12.f);

    /** 패널 회전 추가 오프셋. 매 Tick 계산되는 HMD 정면 회전 위에 얹힌다.
     *  0 이면 정확히 카메라를 마주본다 — 살짝 눕히고 싶을 때만 Pitch 를 준다.
     *  컨트롤러 회전을 그대로 쓰지 않는 이유: Grip 포즈 축이 손등 방향과 30~40° 어긋나 있어
     *  고정 오프셋으로는 손목 각도가 바뀔 때마다 패널이 틀어진다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FRotator HUDPanelRotation = FRotator::ZeroRotator;

    /** 위젯 가상 캔버스 해상도(px). 실제 월드 크기는 이 값 × HUDPanelScale(1px=1cm 기준).
     *  세로는 인벤토리 패널(350px)과 상태 패널(게이지+채팅 로그+입력창, 292px)이 함께 들어갈
     *  만큼 필요하다 — 모자라면 인벤토리를 연 순간 아래쪽이 캔버스 밖으로 잘려 나간다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FVector2D HUDPanelDrawSize = FVector2D(600.f, 660.f);

    /** 패널 월드 스케일. 기본값은 600x660px → 약 24x26cm (손에 들린 태블릿 크기). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float HUDPanelScale = 0.04f;

    /** UI 포인터 광선 길이(cm). 손 패널까지만 닿으면 되므로 짧게. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "20.0", ClampMax = "500.0"))
    float HUDInteractionDistance = 150.f;

    /** 패널이 보이기 시작하는 시선 일치도. HMD 정면 벡터와 패널 방향의 내적 임계.
     *  0.9 ≈ 시야 중심에서 26° 안. 낮출수록 곁눈질에도 켜진다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.0", ClampMax = "0.99"))
    float HUDGazeDotThreshold = 0.9f;

    /** 패널 페이드 보간 속도. 임계 경계에서 손이 미세하게 떨리면 켜짐/꺼짐이 반복되므로
     *  즉시 토글하지 않고 보간으로 완충한다. 클수록 빠르게 나타난다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "1.0", ClampMax = "30.0"))
    float HUDGazeFadeSpeed = 8.f;

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

    // ============================================================================
    // 전투
    // ============================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    UAnimMontage* AttackMontage = nullptr;

    // ── 동역학 데미지 튜닝 (Damage = clamp(½·m·v² · Scale, 0, Cap), v 는 m/s) ──

    /** 근접 손 무기 질량(kg) — ½mv² 의 m. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float WeaponMass = 2.0f;

    /** 운동에너지(J) → HP 데미지 환산 계수. PIE 에서 체감 맞춰 튜닝. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float KineticDamageScale = 1.0f;

    /** 밀치기 임계(m/s). 이 미만 접촉은 무시. 이상~MeleeStrikeSpeed 미만은 밀침만(데미지·공격인지 없음). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float MinImpactSpeed = 1.0f;

    /** 데미지(=공격 인지) 임계(m/s). 이 이상 스윙만 TakeDamage → SmartNPC 가 공격으로 인지.
     *  미만(밀치기 구간)은 NPC 밀려나되 LLM 이 공격으로 안 봄. MinImpactSpeed ≤ 이 값 권장. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float MeleeStrikeSpeed = 2.0f;

    /** 1회 타격 데미지 상한. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float MaxKineticDamage = 100.f;

    /** 손 타격 구체 반경(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float MeleeSphereRadius = 12.f;

    /** 같은 NPC 재타격 최소 간격(초) — 한 스윙 다중 overlap 폭주 방지. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float MeleeHitCooldown = 0.4f;

    /** 손 속도 EMA 스무딩(0~1, 1=무스무딩) — 트래킹 스파이크 억제. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float HandVelSmoothing = 0.5f;

    /** 근접 밀치기 강도(LaunchCharacter cm/s = 스윙속도 m/s × 이 값). 0=밀치기 끔. 살아있는 NPC만. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float KnockbackScale = 150.f;

    /** 밀치기 속도 상한(cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Kinetic")
    float MaxKnockbackSpeed = 600.f;

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

    virtual void ApplyResourceDelta_Implementation(float DeltaHealth, float DeltaMana, float DeltaStamina) override
    { CurrentStats.Resources.ApplyDelta(DeltaHealth, DeltaMana, DeltaStamina); }
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

    /** 손에 쥔 아이템을 인벤토리에 집어넣고 월드 액터를 파괴한다.
     *  인벤토리가 가득 찼거나 데이터가 없으면 물리를 되살려 그 자리에 떨군다(증발 방지). */
    UFUNCTION(Exec, BlueprintCallable, Category = "VR|Interaction")
    bool StoreHeldItemInInventory();

    /** 치트/콘솔 명령: 손 장비 해제. 인자 0 = 오른손 무기, 1 = 왼손 방패. */
    UFUNCTION(Exec)
    void Cheat_Unequip(bool bOffHand);

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
    void TransitionTo(EVRPosture NewPosture);

    /** 매 Tick — 동적 캡슐 리사이즈 + Rising Floor 역보정 (VInterp 스무딩) */
    void UpdateDynamicCapsule(float DeltaTime);

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

    /** 원점 반경 내 최근접 드랍 아이템(거래 접시에 잠긴 것 제외). 픽업·손 쥐기·이름표가 같은 판정을 쓴다. */
    ADroppedItemBase* FindNearestItem(const FVector& Origin, float Radius) const;

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

    /** 그립 누름 본체. 왼손이면 OffHand, 오른손이면 MainHand 슬롯을 대상으로 같은 일을 한다. */
    void HandleGrabStart(bool bLeft);

    /** 인벤토리 열림 중 오른손 스틱 = 슬롯 이동. 한 번 기울일 때 한 칸만 가고,
     *  중립으로 돌아와야 다시 먹는다(계속 기울이면 목록이 순식간에 흘러가 버린다). */
    void UpdateInventorySelection(const FVector2D& Stick);

    /** 슬롯 이동 재장전 플래그 — 스틱이 중립으로 돌아왔는지. */
    bool bSlotNavArmed = true;

    /** 슬롯 그리드 열 수. 스틱 상하 이동이 몇 칸 건너뛸지 결정한다(WBP SlotGrid 열 수와 맞출 것). */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
    int32 InventoryGridColumns = 5;

    /** 선택·꺼내기 결과를 화면에 띄운다. 슬롯 강조 UI 가 없는 동안의 임시 피드백. */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (AllowPrivateAccess = "true"))
    bool bDebugInventorySelection = true;

    /** 그립 뗌 — 인벤토리가 열려 있으면 회수, 닫혀 있으면 거래 접시·NPC 를 차례로 보고 던진다. */
    void OnGrabReleaseRight(const FInputActionValue& Value);
    void OnGrabReleaseLeft(const FInputActionValue& Value);

    /** 그립 뗌 본체 — 인벤토리 열림이면 회수, 닫힘이면 거래접시→NPC 건네기→던지기. */
    void HandleGrabRelease(bool bLeft);

    /** 오른손 근처 반경 내 최근접 드랍 아이템. 쥐기와 이름표가 같은 판정을 쓰도록 한 곳에 둔다. */
    ADroppedItemBase* FindNearestItemNearHand(float Radius, bool bLeft) const;

    /** 매 Tick — 손 근처 아이템 이름표를 띄우고 카메라를 향하게 돌린다. */
    void UpdateItemTooltip();

    /** 이름표가 뜨는 손-아이템 거리(cm). 쥐기 반경보다 넓어야 "잡을 수 있다"를 미리 알려준다. */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (AllowPrivateAccess = "true", ClampMin = "10.0", ClampMax = "300.0"))
    float TooltipRange = 70.f;

    /** 아이템 위로 이름표를 띄우는 높이(cm). */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (AllowPrivateAccess = "true"))
    float TooltipHeightOffset = 15.f;

    /** 직전에 이름표를 그린 아이템 — 대상이 바뀔 때만 텍스트를 다시 만든다(매 틱 SetText 는 비싸다). */
    UPROPERTY(Transient)
    TObjectPtr<ADroppedItemBase> TooltipTarget;

    // --- 인벤토리 HUD ---
    /** 인벤토리 패널 열림 상태 — HUD 위젯과 동기. */
    UPROPERTY(BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
    bool bInventoryOpen = false;

    /** 매 Tick — 손목을 쳐다볼 때만 패널이 서서히 나타나도록 불투명도 보간.
     *  상시 완전 불투명이면 왼손이 시야에 들어올 때마다 패널이 앞을 가린다. */
    void UpdateHUDPanelGaze(float DeltaTime);

    /** 현재 패널 불투명도(0~1). 시작은 투명 — 쳐다보기 전엔 안 보인다. */
    float HUDPanelOpacity = 0.f;

    /** 매 Tick — 패널이 HMD 를 마주보도록 월드 회전 갱신(열림 중에만).
     *  WidgetComponent 의 가시면은 +X 쪽이므로 X 축을 카메라로 향하게 한다. */
    void UpdateHUDPanelFacing();

    /** 패널·포인터 표시 동기 — 열림일 때만 위젯 컴포넌트와 광선을 켠다.
     *  닫힘 상태에서 포인터를 켜두면 손을 흔들 때 슬롯이 호버되어 오작동한다. */
    void ApplyInventoryPresentation(bool bOpen);

    /** 트리거로 UI 를 누른 상태인지 — 열림 중에만 true. 닫을 때 강제 릴리즈에 쓴다. */
    bool bPointerPressed = false;

    /** 매 Tick — 포인터 광선·히트점을 실제 조준 결과에 맞춰 갱신(열림 중에만). */
    void UpdatePointerVisual();

    /** 광선 굵기(cm 지름). 얇을수록 조준점을 가리지 않지만 멀리서 안 보인다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (AllowPrivateAccess = "true", ClampMin = "0.05", ClampMax = "5.0"))
    float PointerBeamThickness = 0.4f;

    /** 히트점 구 지름(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (AllowPrivateAccess = "true", ClampMin = "0.2", ClampMax = "10.0"))
    float PointerDotSize = 1.2f;

    /** 광선·히트점 색. 알파는 무시된다(불투명 이미시브). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (AllowPrivateAccess = "true"))
    FLinearColor PointerColor = FLinearColor(0.15f, 0.75f, 1.f, 1.f);

    /** 위젯을 실제로 겨눴을 때 색 — 슬롯 위에 올라갔는지 색으로 구분된다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (AllowPrivateAccess = "true"))
    FLinearColor PointerHitColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

    /** 광선·히트점 공용 머티리얼 인스턴스. 색을 런타임에 바꾸려면 인스턴스가 필요하다. */
    UPROPERTY(Transient)
    UMaterialInstanceDynamic* PointerMID = nullptr;

    /** 직전 프레임 히트 여부 — 색이 바뀔 때만 파라미터를 쓴다. */
    bool bPointerWasHitting = false;

public:
    /** 손(모션 컨트롤러) 월드 위치. 거래 패널의 물리 버튼처럼 외부에서 손 근접을 재는 곳이 쓴다. */
    UFUNCTION(BlueprintPure, Category = "VR")
    FVector GetHandLocation(bool bRightHand) const;

private:

    /** 쥔 아이템의 손안 자세를 델타로 밀어보고 절대값을 CSV 표기로 찍는다. 예: TuneGrab 0 0 1 0 15 0
     *  전부 0 을 넣으면 밀지 않고 현재 값만 출력한다.
     *  헤드셋을 쓴 채로는 수치를 읽을 수 없으므로, 찍힌 값을 DT_ItemRegistry 에 옮겨 확정한다. */
    UFUNCTION(Exec)
    void TuneGrab(float DX, float DY, float DZ, float DPitch, float DYaw, float DRoll);

    /** 콘솔 진단 — 아바타 팔길이 vs 컨트롤러 도달거리 + 현재 스케일 로그/화면 출력.
     *  팔 뻗은 자세에서 호출해 비율 확인. Reach > ArmLen 이면 아바타 팔이 짧음. */
    UFUNCTION(Exec)
    void LogIKMetrics();

    /** 콘솔 진단 — 인벤토리 실제 내용 + HUD 위젯 연결 상태 덤프.
     *  픽업이 안 먹은 건지, 먹었는데 UI 가 안 그려진 건지 한 번에 갈라준다. */
    UFUNCTION(Exec)
    void DumpInventoryHUD();

    /** 콘솔에서 인벤토리 패널 열기/닫기 토글 (에디터 디버그용). 콘솔창에 ToggleInventory 입력. */
    UFUNCTION(Exec)
    void ToggleInventory();

    // --- 전투 ---
    UFUNCTION()
    void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

    // 동역학 근접 — 매 틱 손(컨트롤러) 위치에서 능동 스피어 오버랩(ECC_Pawn). 빠른 스윙이 NPC 닿으면 ½mv².
    // 본 소켓 패시브 overlap 은 애니 본에서 이벤트 누락이 잦아 능동 쿼리로 대체.
    void TryMeleeHits(const FVector& HandLoc, const FVector& HandVel, bool bRightHand);

    // 손 속도 추적(Tick) — 컨트롤러 위치 델타/dt. cm/s. ½mv² 의 v 산출.
    FVector PrevHandLocLeft  = FVector::ZeroVector;
    FVector PrevHandLocRight = FVector::ZeroVector;
    FVector HandVelLeft      = FVector::ZeroVector;
    FVector HandVelRight     = FVector::ZeroVector;
    bool bHandVelInit = false;

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
