// ChatWidget — NPC 채팅 입력창 + 채팅 기록.
//
// PlayerHUDWidget 에서 분리됨: 손 패널이 아니라 폰의 카메라 부착 WidgetComponent(ChatWidgetComp)에
// 실려 Enter 키로 열리고, 포커스를 잃으면 폰이 다시 숨긴다.
//
// WBP 사용법:
//   - 이 클래스를 부모로 하는 WBP 작성.
//   - "ChatInput"(UEditableTextBox) · "ChatLog"(UScrollBox) 이름 위젯 배치 → 자동 바인딩.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChatWidget.generated.h"

class UEditableTextBox;
class UScrollBox;

UCLASS()
class UE5_MCP_VR_API UChatWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

    /** NPC 채팅 입력 — WBP 에 "ChatInput" 이름 UEditableTextBox 배치 시 자동 바인딩(선택).
     *  Enter 로 커밋하면 소유 폰 근처 최근접 NPC 에게 전송하고 칸을 비운다. */
    UPROPERTY(meta = (BindWidgetOptional))
    UEditableTextBox* ChatInput;

    /** 채팅 기록 — WBP 에 "ChatLog" 이름 UScrollBox 배치 시 자동 바인딩(선택).
     *  보낸 메시지와 NPCManager 의 NPC 응답을 한 줄씩 쌓고 오래된 줄부터 버린다. */
    UPROPERTY(meta = (BindWidgetOptional))
    UScrollBox* ChatLog;

    /** ChatLog 최대 줄 수 — 넘치면 맨 위부터 제거. */
    UPROPERTY(EditDefaultsOnly, Category = "Chat", meta = (ClampMin = "1"))
    int32 ChatLogMaxLines = 8;

    /** ChatLog 글자 크기. */
    UPROPERTY(EditDefaultsOnly, Category = "Chat")
    int32 ChatLogFontSize = 12;

    /** 채팅 대상 탐색 반경(cm) — 폰의 A버튼 NPC 탐지와 같은 값. */
    UPROPERTY(EditDefaultsOnly, Category = "Chat")
    float ChatTargetRadius = 500.f;

    /** ChatLog 에 "화자: 내용" 한 줄 추가. ChatLog 없으면 무시. */
    UFUNCTION(BlueprintCallable, Category = "Chat")
    void AppendChatLine(const FString& Speaker, const FString& Text);

    /** 채팅 칸에 키보드 포커스 — 폰이 Enter 키에서 호출. ChatInput 없으면 false. */
    UFUNCTION(BlueprintCallable, Category = "Chat")
    bool FocusChatInput();

    /** 채팅 입력 중인지 — 폰이 패널 표시 여부 판단에 쓴다. */
    UFUNCTION(BlueprintPure, Category = "Chat")
    bool IsChatFocused() const;

protected:
    /** 소유 폰 캐시 (NativeConstruct 에서 1회 해석). */
    UPROPERTY(BlueprintReadOnly, Category = "Chat")
    TObjectPtr<APawn> OwnerPawn;

    /** ChatInput 커밋 — Enter 만 처리: 텍스트 있으면 전송, 있든 없든 포커스를 게임으로 돌려 닫는다. */
    UFUNCTION()
    void HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    /** NPCManager::OnNPCResponseReceived → ChatLog 한 줄. */
    UFUNCTION()
    void HandleNPCResponse(const FString& NPCName, const FString& Message);

    virtual void NativeDestruct() override;
};
