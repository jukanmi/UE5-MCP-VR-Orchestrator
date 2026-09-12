#include "NPC/BP/SmartNPC.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "NPC/Subsystems/NPCManager.h"
#include "NPC/Action/SmartNPCAIController.h"
#include "NPC/Struct/NPCActionKeys.h"
#include "NPC/Components/NPCStateComponent.h"
#include "NPC/Action/NPCActionComponent.h"
#include "NPC/BP/NPCActionDataAsset.h"
#include "NPC/Components/NPCInventoryComponent.h"
#include "NPC/Components/NPCRagdollComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Engine/Engine.h"
#include "Components/WidgetComponent.h"
#include "NPC/Components/NPCDialogueUIComponent.h"
#include "UI/Widgets/NPCDialogueWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DamageEvents.h"
#include "Core/Physics/KineticDamage.h"   // 공격 판정 — NPC 타겟 데미지 일괄(ApplyToNPC)

// 본 이름 → 부위. 본 미식별(None/캡슐 히트)은 Torso 폴백.
// Mixamo X_Bot(RightArm/RightUpLeg/Hips…)·UE Mannequin(upperarm_r/thigh_r/pelvis…) 양 네이밍 수용.
// 좌우: Mixamo 는 Right/Left 접두, Mannequin 은 _r/_l 접미. 미식별 시 Left 폴백.
static EBodyPartType BoneToBodyPart(FName Bone)
{
    const FString B = Bone.ToString().ToLower();
    if (B.IsEmpty()) return EBodyPartType::Torso;
    // 머리·목 (Head/Neck/HeadTop_End)
    if (B.Contains(TEXT("head")) || B.Contains(TEXT("neck"))) return EBodyPartType::Head;
    // 몸통 — 척추·골반·쇄골/어깨 (Mannequin: spine/pelvis/clavicle, Mixamo: spine/hips/shoulder)
    if (B.Contains(TEXT("spine")) || B.Contains(TEXT("pelvis")) || B.Contains(TEXT("hip"))
        || B.Contains(TEXT("clavicle")) || B.Contains(TEXT("shoulder")))
        return EBodyPartType::Torso;
    const bool bRight = B.Contains(TEXT("right")) || B.EndsWith(TEXT("_r"));
    // 팔·손 (Mannequin: upperarm/lowerarm/hand, Mixamo: arm/forearm/hand)
    if (B.Contains(TEXT("arm")) || B.Contains(TEXT("hand")))
        return bRight ? EBodyPartType::ArmRight : EBodyPartType::ArmLeft;
    // 다리·발 (Mannequin: thigh/calf/foot/ball, Mixamo: upleg/leg/foot/toe)
    if (B.Contains(TEXT("leg")) || B.Contains(TEXT("thigh")) || B.Contains(TEXT("calf"))
        || B.Contains(TEXT("foot")) || B.Contains(TEXT("ball")) || B.Contains(TEXT("toe")))
        return bRight ? EBodyPartType::LegRight : EBodyPartType::LegLeft;
    return EBodyPartType::Torso;
}

static float BodyPartMultiplier(EBodyPartType P)
{
    switch (P)
    {
        case EBodyPartType::Head:  return 2.0f;
        case EBodyPartType::Torso: return 1.0f;
        default:                   return 0.75f;  // ArmLeft/ArmRight/LegLeft/LegRight
    }
}

ASmartNPC::ASmartNPC()
{
    // 액터 자체는 틱하지 않는다 — 래그돌·말풍선은 각자 컴포넌트가 필요할 때만 자기 틱을 켠다.
    PrimaryActorTick.bCanEverTick = false;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    StateComponent     = CreateDefaultSubobject<UNPCStateComponent>(TEXT("StateComponent"));
    ActionComponent    = CreateDefaultSubobject<UNPCActionComponent>(TEXT("ActionComponent"));
    InventoryComponent = CreateDefaultSubobject<UNPCInventoryComponent>(TEXT("InventoryComponent"));

    RagdollComponent   = CreateDefaultSubobject<UNPCRagdollComponent>(TEXT("Ragdoll"));

    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
        StimuliSource->RegisterForSense(UAISense_Hearing::StaticClass());
        StimuliSource->RegisterWithPerceptionSystem();
    }

    // 머리 위 대사 말풍선 (WorldSpace). WBP 클래스·정밀 위치는 BP 에서.
    // 위젯 공간·위치·크기·위젯 클래스는 컴포넌트 자신의 기본값이다(UNPCDialogueUIComponent 생성자).
    DialogueWidgetComp = CreateDefaultSubobject<UNPCDialogueUIComponent>(TEXT("DialogueWidget"));
    DialogueWidgetComp->SetupAttachment(GetMesh());

    // --- AI NPC 회전 설정 ---
    // 기본 Character는 컨트롤러 Yaw를 따라 회전(bUseControllerRotationYaw=true)하고
    // 이동 방향을 향하지 않아(bOrientRotationToMovement=false) MoveTo 시 옆걸음/뒷걸음 발생.
    // AI는 컨트롤러 회전 무시 + 이동 방향으로 자동 회전.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;

    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->bOrientRotationToMovement = true;
        CMC->RotationRate = FRotator(0.f, 540.f, 0.f); // Yaw 540°/s — 부드럽고 빠른 선회
        CMC->bUseControllerDesiredRotation = false;
    }
}

void ASmartNPC::BeginPlay()
{
    Super::BeginPlay();

    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->RegisterNPC(AgentID, this);
    }

    AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Idle")));
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->UnregisterNPC(AgentID);
    }

    Super::EndPlay(EndPlayReason);
}

// === Facade: 외부 호출을 컴포넌트로 전달 ===

// [의도(Why)] 기존 직접 호출(ExecuteActionBatch)은 INPC 인터페이스 구현체로 통합합니다.
void ASmartNPC::ExecuteActionBatch(const FActionBatch& Batch)
{
    if (ActionComponent)
    {
        ActionComponent->ExecuteActionBatch(Batch);
    }
}

void ASmartNPC::OnActionCompleted()
{
    if (ActionComponent)
    {
        ActionComponent->OnActionCompleted();
    }
}

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (bIsDead) return 0.f;

    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    if (StateComponent)
    {
        // [의도(Why)] 히트스캔이 채운 본 이름으로 부위를 식별해 부위별 데미지 배율을 적용.
        float Multiplier = 1.0f;
        if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
        {
            const FPointDamageEvent& Pt = static_cast<const FPointDamageEvent&>(DamageEvent);
            Multiplier = BodyPartMultiplier(BoneToBodyPart(Pt.HitInfo.BoneName));
            // 래그돌 임펄스용: 맞은 본 + 발사 방향(사망·넉다운·Flinch 가 소비).
            if (RagdollComponent) RagdollComponent->NoteHit(Pt.HitInfo.BoneName, Pt.ShotDirection);
        }
        else if (RagdollComponent)
        {
            // 비-PointDamage(폭발·일반 데미지) — 이전 피격 방향 잔존 방지. 가해자→대상 방향으로 폴백.
            RagdollComponent->NoteHit(NAME_None, DamageCauser
                ? (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal()
                : -GetActorForwardVector());
        }
        StateComponent->ApplyDamage(ActualDamage, Multiplier);

        if (!StateComponent->GetAttributes().Resources.IsAlive())
        {
            HandleDeath();
            return ActualDamage;
        }

        // 비치사 피격 → 동역학 반응. 데미지·인지는 이미 적용됨 — 어느 반응으로 갈라지든 데미지 적용은 상시다.
        // 강타=Knockdown(전신 래그돌→기상) / 약타=Flinch(상체 PD 복귀).
        if (RagdollComponent) RagdollComponent->ReactToHit(ActualDamage);

        // [의도(Why)] 피격 정보를 인지 이벤트 배칭 시스템으로 전송하여 즉각적인 상황 인지 및 전략적 판단(도주, 반격 등)을 유도합니다.
        // 가해자 불명이면 자기 위치(거리 0).
        const FPerceptionData DamageEventPerc(
            DamageCauser ? DamageCauser->GetName() : TEXT("Unknown"), ESenseType::Hit,
            DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation(), GetActorLocation(), 1.0f);
        StateComponent->RequestEventCognition(DamageEventPerc);
    }

    return ActualDamage;
}

// ============================================================================
// 공격 판정 (NPC→타겟) — AM_Attack 의 AnimNotifyState_NPCAttackHit 윈도우가 매 틱 호출.
// LLM 지정 단일 타겟만 거리·arc 게이트로 확인 후 1회 데미지(친선사격 없음).
// ============================================================================
void ASmartNPC::PerformAttackHit()
{
    if (bAttackHitConsumed || bIsDead) return;

    AActor* Target = CurrentAttackTarget.Get();
    // 자가 공격 방지 — 타겟팅 오작동으로 this 지정 시 자해 버그 차단.
    if (!Target || Target == this) return;

    // 거리 게이트 — 아직 안 닿았으면 다음 틱 재시도(윈도우 동안 타겟이 들어올 수 있음).
    const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
    const float Dist = ToTarget.Size();
    if (Dist > AttackHitRange) return;

    // 정면 arc 게이트 — 등 뒤·옆 타겟 무시.
    const FVector Dir = ToTarget.GetSafeNormal();
    if (FVector::DotProduct(GetActorForwardVector(), Dir) < AttackHitArcCos) return;

    const float Damage = FMath::Max(0.f, NPCAttributes.Combat.AttackPower * AttackDamageScale);
    const FVector Impact = Target->GetActorLocation();

    if (ASmartNPC* TargetNPC = Cast<ASmartNPC>(Target))
    {
        if (TargetNPC->bIsDead) return;  // 이미 죽은 NPC 재타격 방지 — 가드 소비 안 함
        // 부위 배율·래그돌·LLM 인지까지 일괄(플레이어→NPC 와 동일 규약).
        KineticDamage::ApplyToNPC(TargetNPC, Damage, Impact, Dir, GetController(), this);
    }
    else
    {
        // 플레이어 — 단순 HP 감소. 방향 정보용 FPointDamageEvent.
        FPointDamageEvent Ev;
        Ev.Damage              = Damage;
        Ev.ShotDirection       = Dir;
        Ev.HitInfo.ImpactPoint = Impact;
        Ev.HitInfo.Location    = Impact;
        Target->TakeDamage(Damage, Ev, GetController(), this);

        // 넉백.
        if (ACharacter* TargetChar = Cast<ACharacter>(Target))
        {
            if (NPCKnockbackSpeed > 0.f)
                TargetChar->LaunchCharacter(Dir * NPCKnockbackSpeed, /*bXYOverride=*/true, /*bZOverride=*/false);
        }
    }

    bAttackHitConsumed = true;

    if (bDebugAttackHit && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red,
            FString::Printf(TEXT("[NPCAttack] %s → %s dmg=%.1f dist=%.0f"),
                *GetName(), *Target->GetName(), Damage, Dist));
    }
}

void ASmartNPC::HandleDeath()
{
    if (bIsDead) return;
    bIsDead = true;

    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] %s 사망 처리 시작"), *AgentID);

    // 1. 게임플레이 태그: 기존 상태 전부 제거 후 Dead 태그 부착
    GameplayTags.Reset();
    AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Condition.Dead")));

    // 2. 진행 중인 모든 액션 즉시 중지
    if (ActionComponent)
    {
        ActionComponent->StopAllActions();
    }

    // 3. NPCMap에서 즉시 퇴출 — 이후 어떤 LLM 응답도 이 NPC로 전달되지 않음
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->UnregisterNPC(AgentID);
    }

    // 4. AI 컨트롤러 해제 — BT 완전 중단
    if (AController* C = GetController())
    {
        C->UnPossess();
    }

    // 5. 패시브 래그돌 — 사망 몽타주 대신 물리 시뮬로 자연 붕괴(VR 실감 우선). 전제: 메시 Physics Asset 필수.
    if (RagdollComponent) RagdollComponent->EnterDeathRagdoll();

    // 6. 사망 이벤트 브로드캐스트 — BP에서 VFX 등 추가 연결 가능
    OnNPCDied.Broadcast(this);

    // 7. 일정 시간 후 Actor 제거 (래그돌 안착·시신 노출 여유 시간)
    constexpr float DestroyDelay = 3.f;
    if (UWorld* World = GetWorld())
    {
        FTimerHandle DestroyTimer;
        World->GetTimerManager().SetTimer(DestroyTimer, this, &ASmartNPC::DestroyAfterDeath, DestroyDelay, false);
    }

    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] %s NPCMap 퇴출 완료. %.1f초 후 Actor 제거."), *AgentID, DestroyDelay);
}

void ASmartNPC::DestroyAfterDeath()
{
    Destroy();
}


void ASmartNPC::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = GameplayTags;
}

void ASmartNPC::AddStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::AddState(GameplayTags, Tag);
}

void ASmartNPC::RemoveStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::RemoveState(GameplayTags, Tag);
}

bool ASmartNPC::IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const
{
    if (!StateComponent || !Other.GetObject()) return false;

    const FString OtherID = ICharacterBase::Execute_GetEntityID(Other.GetObject());
    // AffinityHostileThreshold 이하면 적대 관계
    return StateComponent->GetAffinityMultiplier(OtherID) >= 1.0f;
}

// === Dialogue Subtitle (머리 위 WorldSpace 말풍선) ===

void ASmartNPC::ShowSubtitle(const FString& Text)
{
    if (DialogueWidgetComp) DialogueWidgetComp->ShowSubtitle(Text);
}

void ASmartNPC::HideSubtitle()
{
    if (DialogueWidgetComp) DialogueWidgetComp->HideSubtitle();
}
