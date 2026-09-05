#include "NPC/Components/NPCDialogueUIComponent.h"

#include "Core/Interfaces/Entity.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/Components/NPCAudioStreamComponent.h"
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

    // 자막 싱크 — 자기 소유자의 오디오 컴포넌트 재생 시작/종료를 구독한다.
    // 여기서 실패해도(오디오 컴포넌트가 늦게 붙는 경우) 표시할 때마다 재시도한다.
    TryBindAudioSubtitle();
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

void UNPCDialogueUIComponent::TryBindAudioSubtitle()
{
    if (bAudioSubtitleBound) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    if (UNPCAudioStreamComponent* Audio = Owner->FindComponentByClass<UNPCAudioStreamComponent>())
    {
        Audio->OnAudioStarted.AddDynamic(this, &UNPCDialogueUIComponent::HandleSubtitleAudioStarted);
        Audio->OnAudioCompleted.AddDynamic(this, &UNPCDialogueUIComponent::HandleSubtitleAudioCompleted);
        bAudioSubtitleBound = true;
    }
}

void UNPCDialogueUIComponent::ShowSubtitle(const FString& Text, bool bWaitForAudio)
{
    if (Text.IsEmpty()) return;

    // 액션 dialogue 후 같은 발화의 TTS 가 뒤따라 오는 경우 — 같은 텍스트면 깜빡임 없이 이어감.
    const bool bSameText = (Text == CurrentSubtitleText);
    CurrentSubtitleText = Text;

    // 늦게 첨부된 오디오 컴포넌트 대비 바인딩 재시도.
    TryBindAudioSubtitle();

    FTimerManager& Timers = GetWorld()->GetTimerManager();

    if (bWaitForAudio)
    {
        // 음성 시작(HandleSubtitleAudioStarted)이 표시를 맡는다.
        bSubtitleWaitingForAudio = true;

        // 같은 텍스트가 폴백으로 이미 보이는 중이면 유지, 아니면 텍스트만 세팅(숨김) 후 Started 대기.
        ApplySubtitle(bSameText && IsVisible());

        // 안전 상한 — 음성이 시작/완료되지 않아도(에러·끊김) 영구 표시 방지.
        Timers.SetTimer(SubtitleHideTimer, this, &UNPCDialogueUIComponent::HideSubtitle,
                        SubtitleMaxDuration, false);
        return;
    }

    // 즉시 표시 + 길이 비례 폴백 타이머.
    bSubtitleWaitingForAudio = false;
    ApplySubtitle(true);

    const float Duration = SubtitleFallbackDuration + SubtitlePerCharDuration * Text.Len();
    Timers.ClearTimer(SubtitleHideTimer);
    Timers.SetTimer(SubtitleHideTimer, this, &UNPCDialogueUIComponent::HideSubtitle, Duration, false);
}

void UNPCDialogueUIComponent::HandleSubtitleAudioStarted()
{
    // 음성 재생 시작 — 대기 중이던 자막 표시.
    if (CurrentSubtitleText.IsEmpty()) return;
    bSubtitleWaitingForAudio = false;
    ApplySubtitle(true);

    // Completed 정상 도착 시 숨김. 누락(에러·끊김) 대비 안전 상한 갱신.
    GetWorld()->GetTimerManager().SetTimer(SubtitleHideTimer, this,
        &UNPCDialogueUIComponent::HideSubtitle, SubtitleMaxDuration, false);
}

void UNPCDialogueUIComponent::HandleSubtitleAudioCompleted()
{
    HideSubtitle();
}

void UNPCDialogueUIComponent::HideSubtitle()
{
    bSubtitleWaitingForAudio = false;
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
