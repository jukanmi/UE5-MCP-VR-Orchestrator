#include "NPC/Components/NPCDialogueUIComponent.h"

#include "Core/Interfaces/Entity.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/Widgets/NPCDialogueWidget.h"

UNPCDialogueUIComponent::UNPCDialogueUIComponent()
{
    // 빌보드 갱신용 틱. 말풍선이 보일 때만 켜지므로(ApplySubtitle) 평소엔 돌지 않는다.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    // 아래 값들은 BP_SmartNPC 가 오버라이드하고 있던 것을 C++ 기본값으로 확정한 것이다.
    // 튜닝값이 바이너리 에셋에만 있으면 새 NPC BP 를 만들 때마다 다시 맞춰야 한다.
    SetWidgetSpace(EWidgetSpace::World);
    SetWidgetClass(UNPCDialogueWidget::StaticClass());
    SetDrawAtDesiredSize(true);
    SetDrawSize(FVector2D(500.f, 500.f));
    SetRelativeLocation(FVector(0.f, 0.f, 210.f));   // 머리 위
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetVisibility(false);
}

void UNPCDialogueUIComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UNPCDialogueUIComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // 말풍선 빌보드 — 플레이어 카메라를 향해 Yaw 만 정렬(텍스트 직립 유지).
    if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        const FVector ToCam = Cam->GetCameraLocation() - GetComponentLocation();
        const float Yaw = ToCam.Rotation().Yaw + 180.f;   // 위젯 정면이 카메라를 향하도록
        SetWorldRotation(FRotator(0.f, Yaw, 0.f));
    }
}

FString UNPCDialogueUIComponent::GetSpeakerName() const
{
    // AgentID 는 INPC 로 꺼낸다 — 구체 NPC 클래스에 묶이지 않게.
    AActor* Owner = GetOwner();
    if (Owner && Owner->GetClass()->ImplementsInterface(UNPC::StaticClass()))
    {
        return INPC::Execute_GetAgentID(Owner);
    }
    return FString();
}

void UNPCDialogueUIComponent::ShowSubtitle(const FString& Text)
{
    if (Text.IsEmpty()) return;

    CurrentSubtitleText = Text;
    ApplySubtitle(true);

    // 길이 비례 타이머 후 숨김. 재요청은 타이머만 연장한다.
    const float Duration = SubtitleFallbackDuration + SubtitlePerCharDuration * Text.Len();
    FTimerManager& Timers = GetWorld()->GetTimerManager();
    Timers.ClearTimer(SubtitleHideTimer);
    Timers.SetTimer(SubtitleHideTimer, this, &UNPCDialogueUIComponent::HideSubtitle, Duration, false);
}

void UNPCDialogueUIComponent::HideSubtitle()
{
    GetWorld()->GetTimerManager().ClearTimer(SubtitleHideTimer);
    CurrentSubtitleText.Reset();
    ApplySubtitle(false);
}

void UNPCDialogueUIComponent::ApplySubtitle(bool bShow)
{
    // 위젯 오브젝트가 아직 생성 전이면(최초 표시) 강제 초기화.
    if (!GetUserWidgetObject())
    {
        InitWidget();
    }

    if (UNPCDialogueWidget* W = Cast<UNPCDialogueWidget>(GetUserWidgetObject()))
    {
        W->SetDialogue(GetSpeakerName(), CurrentSubtitleText);
    }

    SetVisibility(bShow);

    // 빌보드 틱은 보일 때만. 분리 전에는 이 판단이 액터 전체 틱(플린치·넉다운과 공유)에 얹혀 있었다.
    SetComponentTickEnabled(bShow);
}
