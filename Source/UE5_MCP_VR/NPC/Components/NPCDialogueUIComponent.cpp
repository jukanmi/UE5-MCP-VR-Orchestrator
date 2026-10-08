#include "NPC/Components/NPCDialogueUIComponent.h"

#include "Core/Interfaces/Entity.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/Widgets/NPCDialogueWidget.h"

UNPCDialogueUIComponent::UNPCDialogueUIComponent()
{
    // 머리 위 말풍선 — 좌우만 카메라를 향해 글자를 세워 둔다. 틱은 보일 때만(베이스).
    Facing = EWorldUIFacing::FaceCameraYaw;

    // 아래 값들은 BP_SmartNPC 가 오버라이드하고 있던 것을 C++ 기본값으로 확정한 것이다.
    // 튜닝값이 바이너리 에셋에만 있으면 새 NPC BP 를 만들 때마다 다시 맞춰야 한다.
    SetWidgetClass(UNPCDialogueWidget::StaticClass());
    SetDrawAtDesiredSize(true);
    SetDrawSize(FVector2D(500.f, 500.f));
    SetRelativeLocation(FVector(0.f, 0.f, 210.f));   // 머리 위
    // 위젯 1px = 1cm 라 스케일 1 이면 글자가 NPC 키만 해진다. 0.25 로 줄이면 한 줄이 약 10cm 다.
    SetRelativeScale3D(FVector(0.25f));
    // 피벗을 아래 변으로 잡아 대사가 길어져도 위로만 자라게 한다(중앙 피벗이면 아래 절반이 머리·몸을 가린다).
    SetPivot(FVector2D(0.5f, 1.f));
    SetVisibility(false);
}

FString UNPCDialogueUIComponent::GetSpeakerName() const
{
    // AgentID 는 INPC 로 꺼낸다 — 구체 NPC 클래스에 묶이지 않게. 주민(INPC 미구현)은 ICharacterBase 의 EntityID(=VillagerID).
    AActor* Owner = GetOwner();
    if (!Owner) return FString();
    if (Owner->GetClass()->ImplementsInterface(UNPC::StaticClass()))
    {
        return INPC::Execute_GetAgentID(Owner);
    }
    if (Owner->GetClass()->ImplementsInterface(UCharacterBase::StaticClass()))
    {
        return ICharacterBase::Execute_GetEntityID(Owner);
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
    // 위젯 오브젝트가 아직 생성 전이면(최초 표시) 생성.
    if (UNPCDialogueWidget* W = GetWidgetAs<UNPCDialogueWidget>())
    {
        W->SetDialogue(GetSpeakerName(), CurrentSubtitleText);
    }

    // 틱(카메라 정렬)은 베이스가 보일 때만 켠다.
    SetVisibility(bShow);
}
