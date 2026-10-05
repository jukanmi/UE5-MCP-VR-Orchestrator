#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRMeleeComponent.generated.h"

class AVRPawn;

/**
 * VR 플레이어 동역학 근접 전투 — 실제 손(트래킹 앵커) 속도 추적, 손·쥔 무기로 NPC 치기(½mv²), 쥔 물건으로 막기·패링.
 * 손 속도와 J→HP 환산값은 던지기·투사체도 같이 쓴다(전투 일관성).
 * 매 틱 폰이 Update 를 부른다(컴포넌트 자체 틱 없음).
 * 데미지 = clamp(½·m·v² · KineticDamageScale, 0, MaxKineticDamage), v 는 m/s.
 */
UCLASS(ClassGroup = (VR))
class UE5_MCP_VR_API UVRMeleeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVRMeleeComponent();

    /** 근접 손 무기 질량(kg) — ½mv² 의 m. 무기를 쥐면 그 물건의 질량 오버라이드를 쓴다. */
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

    /** 맨손 타격 판정 구 반경(cm). 무기를 쥐면 무기 바운즈 상자로 바뀐다. */
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

    /** 방어 판정 — 아이템 쥔 손 방향(수평)·공격자 방향 내적이 이 이상이면 Block. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Block", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float BlockDotThreshold = 0.6f;

    /** 패링 임계(cm/s). Block 성립 + 그 손 속도가 이 이상이면 데미지 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Block")
    float ParryHandSpeed = 150.f;

    /** 단순 Block 시 남는 데미지 배율(0.2 = 80% 경감). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Block", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BlockDamageScale = 0.2f;

    /** 매 틱(손 갱신 뒤) — 실제 손 속도를 갱신하고 그 손으로 NPC 를 친다. */
    void Update(float DeltaTime);

    /** 실제 손 속도(cm/s, EMA). 던지기·패링이 쓴다. */
    FVector GetHandVelocity(bool bLeft) const { return HandVel[bLeft ? 0 : 1]; }

    /** 텔레포트(리스폰) 뒤 — 이전 손 위치를 버린다. 남으면 다음 프레임 위치 델타가 통째로 스윙 속도로 잡힌다. */
    void ResetHandVelocity();

    /** 피격 데미지(방어력 차감 뒤)에 막기·패링을 적용한 값. 아이템을 쥔 손이 공격자 쪽이면 막은 것. */
    float ApplyBlock(float Damage, const AActor* DamageCauser) const;

private:
    AVRPawn* GetPawn() const;

    /** 손(또는 쥔 무기) 위치에서 능동 오버랩 — 밀치기 임계 이상이면 밀고, 타격 임계 이상이면 데미지. */
    void TryMeleeHits(const FVector& HandLoc, const FVector& Vel, bool bRightHand);

    // 손 속도 추적 — 실제 손 위치 델타/dt. cm/s. 왼손 0·오른손 1.
    FVector PrevHandLoc[2] = { FVector::ZeroVector, FVector::ZeroVector };
    FVector HandVel[2] = { FVector::ZeroVector, FVector::ZeroVector };
    bool bHandVelInit = false;
};
