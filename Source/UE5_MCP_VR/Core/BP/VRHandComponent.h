#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeadMountedDisplayTypes.h"
#include "InputCoreTypes.h"
#include "Inventory/BP/ItemDataAsset.h"   // EEquipmentSlot
#include "VRHandComponent.generated.h"

class UBoxComponent;
class UCapsuleComponent;
class UPhysicsConstraintComponent;
class UInventoryComponent;
class UVRPawnAnimInstance;
class USkeletalMeshComponent;
class ADroppedItemBase;
class ACombatCharacter;

/**
 * VR 손 하나 — 트래킹 목표(이 컴포넌트 = 앵커)와 그 목표를 드라이브로 쫓는 물리 손바닥을 분리해 벽·물건에 손이 막히게 한다.
 * 모션 컨트롤러 Grip 포즈에 붙는다. 핸드트래킹이 잡히면 손바닥 관절로 옮겨 가고, 아니면 컨트롤러를 따른다.
 *
 * 소유: 물리 손바닥(바디) + 용접된 손바닥 상자·손가락 캡슐 + 손바닥↔월드 드라이브 제약 + 쥐기 제약.
 * 판정: 핀치·주먹 제스처, 접촉 쥐기(손가락 사이에 낀 물건), NPC 뼈 캡슐 밀어내기·밀기.
 * 잡기 입력을 합치고 쥔 물건을 어디로 보낼지(던지기·건네기·수납)는 폰이 정한다.
 *
 * 매 틱 폰이 UpdateTracking → UpdateGesture → UpdateContactGrab → SyncGrab 순서로 직접 부른다(컴포넌트 자체 틱 없음).
 */
UCLASS(ClassGroup = (VR))
class UE5_MCP_VR_API UVRHandComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UVRHandComponent();

    /** 어느 손인가. 폰 생성자가 정한다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Hand")
    EControllerHand Hand = EControllerHand::Left;

    /** 컨트롤러 그립 축 → 메시 손 본 축 보정. 손 로컬 공간 우측곱(손 회전해도 유지). 좌우 미러라 값이 다르다. PIE 중 즉시 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand")
    FRotator GripOffset = FRotator::ZeroRotator;

    // ── 드라이브 ──

    /** 선형 드라이브 강성 — 손바닥이 앵커를 따르는 스프링. 빈손·접촉·쥐기 모두 이 드라이브 하나로 움직인다.
     *  목표 속도 앞먹임(실제 손 속도)이 있어 움직이는 동안 지연이 거의 없다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float DriveStiffness = 22500.f;

    /** 선형 드라이브 감쇠 — 앵커 도달 시 튕김(오버슈트) 억제. 임계감쇠 = 2√강성 = 300. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float DriveDamping = 300.f;

    /** 선형 드라이브 최대 힘 — 벽·물건을 미는 힘의 상한. 가속도 모드라 실제로는 가속도 상한(cm/s²) —
     *  10000 이면 100m/s²(손바닥 0.9kg 면 약 90N). 작으면 빠른 손을 늦게 따라가고 늦게 멈춘다(1500 이면 2m/s 손이 13cm 더 감). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float DriveMaxForce = 10000.f;

    /** 회전 드라이브 강성 — 손바닥이 손 회전을 따라가는 속도. 가속도 모드라 질량 무관. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float AngularStiffness = 1500.f;

    /** 회전 드라이브 감쇠 — 임계감쇠 ≈ 2√강성. 작으면 손목이 흔들린다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float AngularDamping = 80.f;

    /** 회전 드라이브 최대 토크. 0 = 제한 없음. 벽에 댄 손이 표면을 따라 비틀려야 하면 낮춘다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float AngularMaxForce = 0.f;

    /** 손바닥이 목표에서 이만큼(cm) 밀려나면 반대 손 충돌을 잠시 끈다. 양손이 반대편에 끼는 교착을 푼다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Drive")
    float PassThroughDistance = 6.f;

    /** 손 콜라이더 두께 배율 — 손끝 캡슐 반지름·손바닥 두께에 곱한다. 치수 자체는 보이는 손 메시 정점에 맞춘 값.
     *  메시가 파묻히면 올리고, 닿기 전에 막히면 내린다. PIE 중 바로 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand", meta = (ClampMin = "0.1"))
    float ColliderRadiusScale = 1.f;

    // ── NPC 밀기 ──

    /** 실제 손이 NPC 뼈 캡슐 표면보다 이만큼(cm) 넘게 들어가야 NPC 를 밀기 시작한다(살짝 닿기만 한 건 밀지 않게). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Push")
    float PushMinError = 3.f;

    /** 지속 밀기 강도(1/s) — 밀림 속도(cm/s) = (들어간 깊이−최소)cm × 이 값. 0 = 지속 밀기 끔. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Push")
    float PushGain = 8.f;

    /** 지속 밀기 속도 상한(cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Push")
    float MaxPushSpeed = 120.f;

    // ── 핸드트래킹 제스처 — 인벤토리가 열렸을 때 슬롯 발동에 쓴다 ──

    /** 핀치 판정 거리(cm) — 엄지 끝↔검지 끝이 이보다 가까우면 잡는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Gesture")
    float PinchDistanceThreshold = 3.f;

    /** 해제 여유(cm) — 잡은 뒤엔 임계 + 이 값을 넘어야 놓는다. 트래킹 떨림에 잡기/놓기가 반복되지 않게. 주먹 판정에도 같이 쓴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Gesture")
    float PinchHysteresis = 1.f;

    /** 주먹 판정 거리(cm) — 중지·약지·새끼 끝이 모두 손바닥 관절에서 이보다 가까우면 잡는다(편 손은 약 9cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Gesture")
    float FistDistanceThreshold = 5.f;

    // ── 쥐기 ──

    /** 물리 쥐기 제약이 끊어지는 힘(kg·cm/s²). 무거운 물건·세게 흔들면 놓친다. 1.2kg 물건의 무게 ≈ 1,180.
     *  ponytail: 전역값 하나 — 물건별로 달라야 하면 FItemData 에 오버라이드 추가(SPEC_vr_grip_pose 미결). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Grab", meta = (ClampMin = "0.0"))
    float GrabBreakForce = 6000.f;

    /** 물리 쥐기 제약이 끊어지는 토크(kg·cm²/s²). 긴 물건 끝을 한 손으로 들면 무게 × 손~무게중심 거리가 이를 넘어 놓친다.
     *  60,000 = 2.5kg 양동이를 손잡이(무게중심 위 17cm, 약 41,700)로 들면 버티고, 2.5kg 을 25cm 넘게 떨어진 끝으로 들면 놓친다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Grab", meta = (ClampMin = "0.0"))
    float GrabBreakTorque = 60000.f;

    /** 접촉 쥐기 — 엄지와 다른 손끝 캡슐이 이 거리(cm) 안으로 같은 물건에 닿고, 둘 사이에 물건이 있으면 쥔다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Grab", meta = (ClampMin = "0.0"))
    float ContactGrabMargin = 0.5f;

    /** 접촉 쥐기 놓기 — 엄지나 짝 손끝이 표면에서 이 거리(cm) 넘게 떨어지면 놓는다. 쥐기 여유보다 커야 트래킹 떨림에 안 흔들린다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Grab", meta = (ClampMin = "0.0"))
    float ContactReleaseMargin = 1.5f;

    /** 손 드라이브가 온전히 받쳐 주는 쥔 물건 질량 상한(kg). 이보다 무거우면 힘이 모자라 잘 안 들린다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Hand|Grab", meta = (ClampMin = "0.0"))
    float MaxCarryMass = 5.f;

    // ── 폰이 부르는 것 ──

    /** BeginPlay — 손바닥 바디·손바닥 상자·손가락 캡슐·드라이브 제약·쥐기 제약을 만들고 물리 시뮬레이션을 건다. */
    void InitPhysics();

    /** 매 틱 — 입력 소스(핸드트래킹/컨트롤러)로 앵커를 옮기고, NPC 뼈 캡슐 밖으로 투영한 목표로 손바닥을 끌고, 손끝 모양을 배치한다. */
    void UpdateTracking(float DeltaTime);

    /** 매 틱(UpdateTracking 뒤) — 관절 거리로 핀치·주먹 판정. 바뀌었으면 true. */
    bool UpdateGesture();

    /** 매 틱 — 접촉 쥐기 판정. bAllowNewGrab 이 false 면 새로 쥐지 않고 이미 쥔 것의 유지만 본다. 바뀌었으면 true. */
    bool UpdateContactGrab(bool bAllowNewGrab);

    /** 물리 손바닥 ↔ 아이템을 끊어지는 제약으로 잇고 인벤토리 손 슬롯에 쥔 것으로 기록한다. 물리 손이 아직 없으면(시뮬레이션 전) false.
     *  bKeepCollision = 손과 쥔 물건의 충돌을 유지(접촉 쥐기 — 닿은 순간이라 겹침이 없다). 컨트롤러 쥐기는 손이 물건에 박혀 있을 수 있어 끈다. */
    bool GrabWithPhysics(ADroppedItemBase* Item, bool bKeepCollision);

    /** 쥐기 제약을 푼다(놓을 때). 끊김 이벤트는 나가지 않는다. */
    void ReleaseGrab();

    /** 매 틱 — 쥔 아이템이 다른 경로(수납·소모)로 손을 떠났으면 남은 제약을 푼다. */
    void SyncGrab();

    // ── 읽기 ──

    UBoxComponent* GetPalm() const { return Palm; }
    bool IsPhysicsActive() const;
    bool IsLeft() const { return Hand == EControllerHand::Left; }
    EEquipmentSlot GetHandSlot() const { return IsLeft() ? EEquipmentSlot::OffHand : EEquipmentSlot::MainHand; }

    /** 이번 틱 핸드트래킹 관절 상태. 추적 중이 아니면 bValid=false. */
    const FXRHandTrackingState& GetTrackState() const { return TrackState; }

    /** 이번 틱 잡기 제스처(핀치 또는 주먹). 트래킹이 끊기면 false. */
    bool IsGesturing() const { return bGesture; }

    /** 접촉 쥐기 중인가와 그 아이템. */
    bool IsContactHeld() const { return bContactHeld; }
    ADroppedItemBase* GetContactItem() const { return ContactItem.Get(); }

    /** 보이는 손(물리 손바닥) 월드 위치. 시뮬레이션 전엔 앵커. */
    FVector GetVisibleLocation() const;

    /** 손 이펙터 — 몸 메시 컴포넌트 공간. 핸드트래킹 중이면 손목 관절 위치·손바닥 관절 보정, 아니면 컨트롤러 그립 보정. */
    FTransform GetEffectorCS(const USkeletalMeshComponent* BodyMesh) const;

    /** 손바닥 20cm 안 드랍 아이템 메시(쥔 것 포함). 손가락 감싸기(애님 인스턴스)가 마디 고정 판정에 쓴다. */
    void GetNearbyItemMeshes(TArray<const UPrimitiveComponent*, TInlineAllocator<4>>& Out) const;

private:
    UVRPawnAnimInstance* GetAnim() const;
    UInventoryComponent* GetInventory() const;

    /** 바디를 목표 위치·회전으로 끄는 드라이브 목표 갱신(목표 속도 앞먹임 포함). 너무 멀어지면(대쉬·리스폰) 목표로 순간이동.
     *  @return 갱신 전 바디↔목표 거리(cm) */
    float DriveBody(const FVector& Location, const FQuat& Rotation, float DeltaTime, float MassScale);

    /** 쥔 물건 몫까지 드라이브를 키우는 배율 = (손바닥 + min(쥔 물건, MaxCarryMass)) / 손바닥. 빈손이면 1. */
    float HeldMassScale() const;

    /** 목표에서 PassThroughDistance 넘게 밀려나면 반대 손 충돌을 끄고, 절반 안으로 돌아오면 다시 켠다 — 양손 끼임 해소. */
    void UpdatePassThrough(float Error);

    /** 손 드라이브 목표에서 손 모양이 근처 NPC 뼈 캡슐에 들어가면 목표를 표면 밖으로 옮긴 위치. 안 겹치면 그대로.
     *  OutTouched = 실제 손 위치에서 가장 깊이 들어간 NPC(없으면 nullptr). */
    FVector ProjectOutOfNPCs(const FVector& Location, const FQuat& Rotation, ACombatCharacter*& OutTouched) const;

    /** 물리 손이 NPC 뼈 캡슐 표면(Surface)에 와 있고 실제 손이 그 안으로 들어가 있으면, 들어간 깊이 비례 속도로 그 방향(수평)으로 민다. */
    void PushTouchedNPC(ACombatCharacter* NPC, const FVector& Surface, const FVector& RealHand, float DeltaTime);

    /** 손바닥에 용접된 손바닥 상자·손끝 캡슐을 메시 손에 맞춰 놓는다. 핸드트래킹이 아니면 손끝 충돌을 끄고 손바닥 안에 둔다. */
    void UpdateFingertipShapes();

    /** 손바닥 상자·손끝 캡슐 치수를 손 메시에 맞춘 값 × 두께 배율로 적용. 배율이 바뀔 때만 다시 적용. */
    void ApplyColliderSizes();

    /** 손바닥 상자의 손바닥 바디 기준 상대 변환. 핸드트래킹이면 손목 관절 오프셋, 컨트롤러면 그립 보정 기준. */
    FTransform ComputePalmShapeRelative(int32 Part) const;

    /** 손바닥 중심 = 손바닥 상자들의 가운데. 손바닥 모양이 없으면 false. */
    bool GetPalmShapeCenter(FVector& OutCenter) const;

    /** 핀치 또는 주먹으로 사이에 낀 아이템. OutPartner = 핀치면 엄지의 짝 손가락(1~4), 주먹이면 손바닥(5). */
    ADroppedItemBase* FindGraspedItem(int32& OutPartner) const;

    /** 손가락 하나의 캡슐(끝마디 먼저, 가운데 마디) 중 물건 표면에서 Margin(cm) 안에 있는 것. 없으면 null. */
    const UCapsuleComponent* FingerTouching(int32 Finger, const UPrimitiveComponent* Item, float Margin) const;

    /** 손바닥 모양이 물건 표면에서 Margin(cm) 안에 있는지. */
    bool IsPalmTouching(const UPrimitiveComponent* Item, float Margin) const;

    /** 힘·토크 임계를 넘어 제약이 끊겼다 = 놓쳤다. 던지지 않고 그 자리에 떨어뜨린다. */
    UFUNCTION()
    void OnGrabConstraintBroken(int32 ConstraintIndex);

    /** 물리 손바닥 — 시뮬레이션 바디. 자체는 손바닥 관절에 중심을 둔 1cm 핵이고 실제 손 모양은 용접된 상자·캡슐이 맡는다. */
    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Palm;

    /** 손바닥 ↔ 월드 제약. 선형·회전 드라이브가 앵커 쪽으로 끌어당긴다. */
    UPROPERTY(Transient)
    TObjectPtr<UPhysicsConstraintComponent> PalmConstraint;

    /** 손바닥에 용접된 손바닥 모양 — 손목 쪽·손가락 쪽 상자(UVRPawnAnimInstance::NumPalmShapes). */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBoxComponent>> PalmShapes;

    /** 손바닥에 용접된 손가락 캡슐 — 손가락마다 가운데 마디·끝마디(UVRPawnAnimInstance::ShapeIndex 순). */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UCapsuleComponent>> FingerBodies;

    /** 물리 쥐기 제약과 제약으로 쥔 아이템. */
    UPROPERTY(Transient)
    TObjectPtr<UPhysicsConstraintComponent> GrabConstraint;
    TWeakObjectPtr<ADroppedItemBase> GrabbedItem;

    FXRHandTrackingState TrackState;
    bool bGesture = false;

    /** 손가락 캡슐 충돌이 켜져 있는가. 켜고 끌 때만 충돌 설정을 바꾼다. */
    bool bFingertipsActive = false;
    float AppliedColliderScale = -1.f;

    /** 직전 틱 드라이브 목표 — 목표 속도(앞먹임) 계산용. */
    FVector PrevTarget = FVector::ZeroVector;
    bool bHasPrevTarget = false;

    /** 접촉 쥐기 상태와 사이에 낀 아이템·짝(엄지의 짝 손가락 1~4, 주먹 5). 쥐기가 실패·끊겨도 손을 펼 때까지 유지해 매 틱 다시 쥐지 않는다. */
    bool bContactHeld = false;
    TWeakObjectPtr<ADroppedItemBase> ContactItem;
    int32 ContactPartner = INDEX_NONE;
};
