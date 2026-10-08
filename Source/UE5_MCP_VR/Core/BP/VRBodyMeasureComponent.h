#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRBodyMeasureComponent.generated.h"

class AVRPawn;
class UBodyMeasureSaveGame;
class UMotionControllerComponent;

/** 안내 글이 바뀔 때마다 — Guide 는 안내 텍스트(비면 숨김), ButtonLabel 은 측정 시작 버튼의 짧은 글자. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBodyMeasureText, const FText&, Guide, const FText&, ButtonLabel);

/**
 * 신체 측정 상태기계 — 4단계(키 → 팔 벌림 → 무릎 → 엉덩이)를 왼손 X 버튼을 누른 순간의 값으로 잰다.
 * 시작·취소는 설정 화면의 측정 버튼, 단계 진행은 폰이 넘기는 X 입력(Capture). 틱 없음 — 입력이 올 때만 움직인다.
 * 끝나면 값 검증 후 별도 슬롯(UBodyMeasureSaveGame)에 저장하고, 키는 폰의 자세 판정 기준 높이로 바로 반영한다.
 * 팔 벌림·무릎·엉덩이는 저장·표시만 한다(눕힌 몸 비례에는 아직 쓰지 않는다).
 *
 * 높이는 모두 바닥 기준 cm — 폰이 자세 판정에 쓰는 HMD 높이(GetCurrentHMDHeight)와 같은 좌표계다.
 * 메뉴가 열려 있는 동안만 측정이 돌고(시작이 메뉴 버튼), 메뉴가 닫히면 UI 컴포넌트가 취소한다.
 */
UCLASS(ClassGroup = (VR))
class UE5_MCP_VR_API UVRBodyMeasureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVRBodyMeasureComponent();

    /** 안내·버튼 글이 바뀔 때. 메뉴 UI 가 바인딩한다. */
    UPROPERTY(BlueprintAssignable, Category = "BodyMeasure")
    FOnBodyMeasureText OnTextChanged;

    /** true 면 컨트롤러·손이 추적 중일 때만 잰다. 헤드셋 없는 PIE 에서 상태기계를 시험할 때만 끈다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BodyMeasure")
    bool bRequireTracking = true;

    /** 키(HMD 높이)로 받아들이는 범위(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BodyMeasure|Validation")
    FVector2D HeightRange = FVector2D(100.f, 250.f);

    /** 팔 벌림(양손 수평 간격)으로 받아들이는 범위(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BodyMeasure|Validation")
    FVector2D ArmSpanRange = FVector2D(60.f, 250.f);

    /** 무릎 높이 하한(cm). 이보다 낮으면 컨트롤러를 무릎에 대지 않은 것으로 본다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BodyMeasure|Validation")
    float MinKneeHeight = 10.f;

    /** 측정 시작(1단계). 이미 측정 중이면 무시. */
    void Start();

    /** 측정 중이면 취소한다(버튼 재클릭·메뉴 닫힘). 측정 중이 아니면 무시. */
    void Cancel();

    /** X 입력 — 지금 단계의 값을 잰다. 측정 중이 아니면 무시. */
    void Capture();

    /** 측정 중이 아닐 때의 평소 글(저장값 요약 또는 빈 안내)을 알린다. 메뉴를 열 때와 초기화에 쓴다. */
    void ShowIdle();

    bool IsMeasuring() const { return Step != EStep::Idle; }

    /** 저장된 키(HMD 높이)가 있으면 true 와 함께 돌려준다. 처음 부를 때 슬롯을 읽는다. */
    bool GetSavedHeight(float& OutHeight);

private:
    enum class EStep : uint8 { Idle, Height, ArmSpan, Knee, Hip };

    AVRPawn* GetPawn() const;

    /** 슬롯을 한 번만 읽어 Saved 를 채운다(없으면 Saved 는 null). */
    void EnsureLoaded();

    /** 지금 단계의 값을 잰다. 못 재면 false 와 사유(한 줄). */
    bool SampleStep(float& OutValue, FText& OutWhy) const;

    /** 오른손 컨트롤러 월드 위치. 컨트롤러가 안 잡히면 추적 중인 손으로 대신하고, 둘 다 없으면 false. */
    bool GetRightHandPoint(FVector& OutLocation) const;

    /** 한쪽 손(컨트롤러 또는 핸드트래킹)이 지금 추적 중인가. */
    bool IsHandLive(bool bLeft) const;

    /** 지금 단계의 안내 문구(제목 — 지시). */
    static FText StepPrompt(EStep S);

    /** 4값(키·팔·무릎·엉덩이) 검증. 통과 못 하면 사유 한 줄. 막 잰 값과 불러온 저장본이 같은 범위를 쓴다. */
    bool Validate(const float (&V)[4], FText& OutWhy) const;

    /** 검증 통과한 값을 저장하고 폰의 기준 높이에 반영한다. */
    void Finish();

    void Fail(const FText& Why);

    /** 안내·버튼 글을 갱신하고 알린다. */
    void SetTexts(const FString& Guide, const FString& Button);

    /** 평소 버튼 글 — 저장값이 있으면 "다시 측정". */
    FString IdleButton() const;

    EStep Step = EStep::Idle;

    /** 단계별 잰 값 — 인덱스는 EStep(Height~Hip) 에서 1 뺀 값. */
    float Values[4] = {};

    /** 다음 안내에 한 줄 앞붙일 직전 단계 결과("측정됨: 172cm"). */
    FString LastResultLine;

    bool bLoaded = false;

    UPROPERTY(Transient)
    UBodyMeasureSaveGame* Saved = nullptr;
};
