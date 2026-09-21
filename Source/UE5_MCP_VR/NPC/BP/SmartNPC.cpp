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
            Multiplier = BodyPartMultiplierForBone(Pt.HitInfo.BoneName);
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
            PerceptionIdFor(DamageCauser), ESenseType::Hit,
            DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation(), GetActorLocation(), 1.0f);
        StateComponent->RequestEventCognition(DamageEventPerc);
    }

    return ActualDamage;
}

FString ASmartNPC::PerceptionIdFor(const AActor* Actor)
{
    if (!Actor) return TEXT("Unknown");
    if (Actor->Implements<UPlayerBase>()) return PlayerIds::Player;
    const ACombatCharacter* C = Cast<ACombatCharacter>(Actor);
    const FString Id = C ? C->GetCombatId() : FString();
    return Id.IsEmpty() ? Actor->GetName() : Id;
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

    // 3. 스토리 트리거 + NPCMap 퇴출 — 이후 어떤 LLM 응답도 이 NPC로 전달되지 않음.
    //    npc_died 는 boss_killed 판정용. 아군 NPC 의 combat_victory 는 그 NPC 가 이 대상과 교전 중일 때만
    //    오므로, 플레이어가 단독으로 보스를 잡는 경로는 죽는 쪽이 직접 알린다.
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->SendStoryEvent(TEXT("npc_died"), AgentID, AgentID);
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
