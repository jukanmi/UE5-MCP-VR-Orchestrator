// NPCDialogueWidget — NPC 머리 위 WorldSpace 말풍선 자막 위젯.
//
// 구 ChatWidget(제거됨) 대체. 입력은 ASR 이 담당하므로 본 위젯은 출력(대사 표시) 전용.
// ASmartNPC 의 UWidgetComponent 가 이 클래스를 자식 WBP 로 띄우고, ShowSubtitle 경로에서
// SetDialogue 로 텍스트를 채운다. 표시/숨김 타이밍·빌보드는 ASmartNPC 가 관리.
//
// WBP 사용법:
//   - 이 클래스를 부모로 하는 WBP 작성(WBP_NPCDialogue).
//   - (선택) "SpeakerName"/"DialogueText"(UTextBlock) 이름 위젯 배치 → 자동 바인딩.
//   - 페이드 등 연출은 OnDialogueSet 이벤트에서.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPCDialogueWidget.generated.h"

class UTextBlock;

UCLASS()
class UE5_MCP_VR_API UNPCDialogueWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 발화자 이름 + 대사 적용. WBP 에 같은 이름 UTextBlock 배치 시 자동 바인딩(선택). */
    UFUNCTION(BlueprintCallable, Category = "NPC|Dialogue")
    void SetDialogue(const FString& Speaker, const FString& Text);

    /** 발화자 이름 텍스트(선택 바인딩). */
    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* SpeakerName;

    /** 대사 본문 텍스트(선택 바인딩). */
    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* DialogueText;

    /** WBP 연출 훅(페이드 인 등) — SetDialogue 시 호출. */
    UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Dialogue")
    void OnDialogueSet(const FString& Speaker, const FString& Text);
};
