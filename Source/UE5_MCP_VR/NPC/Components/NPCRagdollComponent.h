// NPCRagdollComponent — 액티브 래그돌(트리거형 hit-react) 전담.
//
// 약타=Flinch(상체 PD 복귀), 강타=Knockdown(전신 래그돌 → 안착 → 기상), 사망=전신 래그돌 유지.
// 소유 ACharacter 의 메시·캡슐·이동 컴포넌트를 직접 다루고, AI 정지/재개와 동시 넉다운 카운터
// (UNPCManager)까지 여기서 처리한다. 액터는 피격 정보를 NoteHit 로 넘기고 ReactToHit 만 부른다.
//
// 분리 전에는 이 로직 375줄이 ASmartNPC 에 있어 액터 틱이 래그돌 진행에 묶여 있었다.
// 지금은 이 컴포넌트가 자기 틱만 켜고 끈다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPC/Struct/NPCActionTypes.h"
#include "NPCRagdollComponent.generated.h"

class UAnimMontage;
class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (MCP), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCRagdollComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCRagdollComponent();

    /** 직전 피격의 본·방향 — TakeDamage 가 채우고 EnterRagdoll/Flinch 가 임펄스에 쓴다. */
    void NoteHit(FName Bone, const FVector& Direction);

    /** 피격 강도(=½mv² 데미지)로 반응 분기. >= KnockdownImpulseThreshold → Knockdown, else → Flinch. */
    void ReactToHit(float HitStrength);

    /** 약타 반응 — 상체(FlinchRootBone 이하) 물리 블렌드 + 임펄스 → Tick 램프로 애니 복귀. 넉다운/기상 중이면 무시. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Ragdoll")
    void Flinch();

    /** 강타 반응 — 전신 래그돌 + AI 정지 + 안착 후 기상. 넉다운/기상 중 재호출 시 재진입(저글). */
    UFUNCTION(BlueprintCallable, Category = "MCP|Ragdoll")
    void Knockdown();

    /** 사망 — 진행 중 넉다운/기상 정리(타이머·동시 카운터) 후 치사 임펄스로 전신 래그돌. 되돌리지 않는다. */
    void EnterDeathRagdoll();

    UFUNCTION(BlueprintPure, Category = "MCP|Ragdoll")
    bool IsKnockedDown() const { return KnockdownPhase != EKnockdownPhase::None; }

    // --- 분기 임계 ---

    /** 이 데미지(=½mv² 에너지 스케일) 이상이면 넉다운, 미만이면 Flinch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float KnockdownImpulseThreshold = 40.f;

    /** 래그돌에 가할 타격 방향 임펄스 강도(본 단위, bVelChange=false → 질량 의존). 0 이면 순수 중력. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float DeathImpulseStrength = 20000.0f;

    // --- Flinch (약타) ---

    /** Flinch 물리 블렌드 시작 본(이 본 이하만 시뮬, 하체는 애니 유지). Mixamo=Spine. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    FName FlinchRootBone = TEXT("Spine");

    /** Flinch PD — 애니 포즈로 당기는 방향 강도(클수록 빨리 복귀·뻣뻣). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchOrientationStrength = 1000.f;

    /** Flinch PD — 각속도 감쇠 강도. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchAngularVelStrength = 100.f;

    /** Flinch 피격 임펄스 크기. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchImpulse = 30000.f;

    /** 물리→애니 블렌드 복귀 속도(weight/초). Flinch·기상 블렌드 램프 공용. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchRecoverSpeed = 3.0f;

    // --- Knockdown / 기상 ---

    /** 넉다운 래그돌 임펄스 배율(DeathImpulseStrength 대비). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float KnockdownImpulseScale = 1.0f;

    /** 안착 판정 본(루트/골반 선속도 측정). Mixamo=Hips. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    FName KnockdownPelvisBone = TEXT("Hips");

    /** 안착 판정 — 골반 선속도가 이 값(cm/s) 미만이어야 안착 카운트. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float SettleSpeedThreshold = 150.f;

    /** 안착 유지 시간(초) — 저속이 이만큼 지속되면 기상 시작. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float SettleHoldTime = 0.6f;

    /** 누운(등 바닥) 상태 기상 몽타주. 미할당 시 즉시 블렌드 복귀 폴백. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    UAnimMontage* GetUpMontage_FaceUp = nullptr;

    /** 엎드린(얼굴 바닥) 상태 기상 몽타주. 미할당 시 즉시 블렌드 복귀 폴백. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    UAnimMontage* GetUpMontage_FaceDown = nullptr;

    /** 동시 넉다운 상한(멀티 NPC). 초과분은 Flinch 폴백. 트리거형이라 평소 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    int32 MaxConcurrentKnockdown = 3;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

private:
    /** 전신 패시브 래그돌 진입(사망·넉다운 공유). 캡슐 NoCollision·CMC 정지·메시 시뮬 ON·LastHitDirection 임펄스. */
    void EnterRagdoll(bool bFatal);

    void TickFlinchRamp(float DeltaSeconds);
    void TickSettleDetection(float DeltaSeconds);
    void BeginGetUp();
    void TickGetUpBlend(float DeltaSeconds);
    void FinishGetUp();

    /** 기상 몽타주 종료 델리게이트 — 정상 완료 시 FinishGetUp(인터럽트는 무시). */
    void OnGetUpMontageEnded(UAnimMontage* Montage, bool bInterrupted);

    /** 넉다운 단계 종료 — 동시 카운터 반환 + Phase=None. 기상 완료·사망·EndPlay 공통. */
    void LeaveKnockdownPhase();

    /** Flinch 램프·넉다운 진행 중에만 틱. */
    void RefreshTickEnabled();

    USkeletalMeshComponent* GetOwnerMesh() const;
    class ACharacter* GetOwnerCharacter() const;
    bool IsOwnerDead() const;
    FString GetOwnerAgentID() const;

    /** Flinch 상체 PD 구동. 소유 메시에 바인딩해 BeginPlay 에서 만든다. PA_SmartNPC(Physics Asset) 필요. */
    UPROPERTY(Transient)
    TObjectPtr<UPhysicalAnimationComponent> PhysicalAnim;

    bool bFlinching = false;
    float FlinchBlendWeight = 0.f;          // Flinch 상체 블렌드 추적(읽기 API 없어 자체 보관)
    EKnockdownPhase KnockdownPhase = EKnockdownPhase::None;
    float SettleTimer = 0.f;                // 안착 지속 누적
    float GetUpBlendWeight = 0.f;           // 기상 전신 블렌드 추적
    FName OriginalMeshProfile;              // BeginPlay 캡처 — 기상 후 메시 콜리전 프로파일 복원용
    ECollisionEnabled::Type OriginalMeshCollision = ECollisionEnabled::QueryOnly;  // BeginPlay 캡처 — 활성화 상태 복원용

    // BeginPlay 캡처 — 기상 후 메시를 캡슐 기준 제자리로 되돌리는 데 쓴다.
    // 래그돌 동안 메시 트랜스폼은 물리 바디가 덮어쓰므로, 시뮬을 끄면 누운 자세가 컴포넌트에
    // 그대로 남는다. 캡슐만 세워도 메시가 누워 있어 "누운 채로 일어나는" 그림이 된다.
    FTransform DefaultMeshRelativeTransform;
    FTimerHandle GetUpMontageTimer;

    // 직전 피격 정보 (NoteHit 가 채움, EnterRagdoll/Flinch 가 소비)
    FName LastHitBone = NAME_None;
    FVector LastHitDirection = FVector::ZeroVector;  // ShotDirection (피격→방향, 정규화)
};
