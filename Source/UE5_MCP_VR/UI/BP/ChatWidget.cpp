#include "UI/BP/ChatWidget.h"

#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Core/Utils/PlayerInteractionUtils.h"
#include "Villager/VillagerCharacter.h"
#include "Blueprint/WidgetTree.h"

void UChatWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 소유 폰 캐시 — 채팅 대상 탐색에 쓴다.
    OwnerPawn = GetOwningPlayerPawn();

    // 채팅 입력 — WBP 에 ChatInput 이 있을 때만. 위젯 재구성 시 중복 바인딩 방지로 Unique.
    if (ChatInput)
    {
        ChatInput->OnTextCommitted.AddUniqueDynamic(this, &UChatWidget::HandleChatCommitted);
    }
    // NPC 응답 → 채팅 기록. GameInstance 서브시스템이라 위젯보다 오래 살므로 Destruct 에서 해제.
    if (ChatLog)
    {
        if (UNPCManager* Manager = UNPCManager::Get(this))
        {
            Manager->OnNPCResponseReceived.AddUniqueDynamic(this, &UChatWidget::HandleNPCResponse);
        }
    }
}

void UChatWidget::NativeDestruct()
{
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->OnNPCResponseReceived.RemoveDynamic(this, &UChatWidget::HandleNPCResponse);
    }
    Super::NativeDestruct();
}

void UChatWidget::AppendChatLine(const FString& Speaker, const FString& Text)
{
    if (!ChatLog || !WidgetTree) return;

    UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Line->SetText(FText::FromString(FString::Printf(TEXT("%s: %s"), *Speaker, *Text)));
    Line->SetAutoWrapText(true);
    FSlateFontInfo Font = Line->GetFont();
    Font.Size = ChatLogFontSize;
    Line->SetFont(Font);
    ChatLog->AddChild(Line);

    while (ChatLog->GetChildrenCount() > ChatLogMaxLines)
    {
        ChatLog->RemoveChildAt(0);
    }
    ChatLog->ScrollToEnd();
}

void UChatWidget::HandleNPCResponse(const FString& NPCName, const FString& Message)
{
    AppendChatLine(NPCName, Message);
}

bool UChatWidget::FocusChatInput()
{
    if (!ChatInput) return false;
    ChatInput->SetKeyboardFocus();
    return true;
}

bool UChatWidget::IsChatFocused() const
{
    return ChatInput && ChatInput->HasKeyboardFocus();
}

void UChatWidget::HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod != ETextCommit::OnEnter) return;

    const FString Msg = Text.ToString().TrimStartAndEnd();
    if (!Msg.IsEmpty() && OwnerPawn)
    {
        // 최근접이 주민이면 로컬 규칙 응답(서버 미전송). SmartNPC 가 더 가까우면 서버로.
        FString Target;
        if (AVillagerCharacter* V = PlayerInteractionUtils::FindNearestTalkTarget(OwnerPawn, ChatTargetRadius, Target))
        {
            AppendChatLine(TEXT("나"), Msg);
            const FString Reply = V->RespondToChat(Msg);
            if (!Reply.IsEmpty()) AppendChatLine(V->VillagerID, Reply);
        }
        else if (Target.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Chat] 채팅 폐기 — 반경 %.0fcm 내 NPC 없음"), ChatTargetRadius);
        }
        else
        {
            PlayerInteractionUtils::SendDialogueToNpc(this, TEXT("Player"), Target, Msg);
            AppendChatLine(TEXT("나"), Msg);
        }
    }

    // 빈 Enter 는 닫기, 전송 후에도 닫기 — 포커스가 남아 있으면 WASD 가 글자로 들어간다.
    // SetFocusToGameViewport 는 Slate 포커스만 옮기고 뷰포트 입력 캡처를 안 돌려줘 이동이 죽는다 —
    // 입력 모드 GameOnly 가 포커스·캡처를 함께 복구한다.
    ChatInput->SetText(FText::GetEmpty());
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetInputMode(FInputModeGameOnly());
    }
}
