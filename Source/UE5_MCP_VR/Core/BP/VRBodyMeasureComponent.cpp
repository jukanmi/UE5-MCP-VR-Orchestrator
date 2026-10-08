#include "Core/BP/VRBodyMeasureComponent.h"

#include "Core/BP/VRHandComponent.h"
#include "Core/BP/VRPawn.h"
#include "Core/Save/BodyMeasureSaveGame.h"
#include "Engine/Engine.h"
#include "HeadMountedDisplayTypes.h"
#include "IXRTrackingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"

namespace
{
    // HMD 트래킹이 안 잡힌 0 근처 값을 거르는 하한(자동 캘리브레이션과 같은 값).
    constexpr float MinTrackedHMDHeight = 30.f;

    FString Cm(float V) { return FString::Printf(TEXT("%dcm"), FMath::RoundToInt(V)); }

    FString Summary(const UBodyMeasureSaveGame& S)
    {
        return FString::Printf(TEXT("키 %d · 팔 %d · 무릎 %d · 엉덩이 %dcm"),
            FMath::RoundToInt(S.Height), FMath::RoundToInt(S.ArmSpan), FMath::RoundToInt(S.KneeHeight), FMath::RoundToInt(S.HipHeight));
    }
}

UVRBodyMeasureComponent::UVRBodyMeasureComponent()
{
    PrimaryComponentTick.bCanEverTick = false;   // X 입력이 올 때만 움직인다
}

AVRPawn* UVRBodyMeasureComponent::GetPawn() const
{
    return Cast<AVRPawn>(GetOwner());
}

// ============================================================================
// 저장값
// ============================================================================

void UVRBodyMeasureComponent::EnsureLoaded()
{
    if (bLoaded) return;
    bLoaded = true;
    if (!UGameplayStatics::DoesSaveGameExist(UBodyMeasureSaveGame::SlotName, 0)) return;

    Saved = Cast<UBodyMeasureSaveGame>(UGameplayStatics::LoadGameFromSlot(UBodyMeasureSaveGame::SlotName, 0));
    if (!Saved)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BodyMeasure] 슬롯 %s 이 있지만 읽지 못했다 — 자동 캘리브레이션으로 폴백"), *UBodyMeasureSaveGame::SlotName);
        return;
    }

    // 손상·구버전 저장본이 기준 높이를 망치지 않게 방금 잰 값과 같은 범위로 거른다.
    FText Why;
    const float V[4] = { Saved->Height, Saved->ArmSpan, Saved->KneeHeight, Saved->HipHeight };
    if (!Saved->bValid || !Validate(V, Why))
    {
        UE_LOG(LogTemp, Warning, TEXT("[BodyMeasure] 저장본을 무시한다(%s) — 자동 캘리브레이션으로 폴백"),
            Saved->bValid ? *Why.ToString() : TEXT("유효 표시 없음"));
        Saved = nullptr;
    }
}

bool UVRBodyMeasureComponent::GetSavedHeight(float& OutHeight)
{
    EnsureLoaded();
    if (!Saved) return false;
    OutHeight = Saved->Height;
    return true;
}

FString UVRBodyMeasureComponent::IdleButton() const
{
    return Saved ? TEXT("다시 측정") : TEXT("신체 측정");
}

void UVRBodyMeasureComponent::SetTexts(const FString& Guide, const FString& Button)
{
    OnTextChanged.Broadcast(FText::FromString(Guide), FText::FromString(Button));
}

void UVRBodyMeasureComponent::ShowIdle()
{
    if (IsMeasuring()) return;
    EnsureLoaded();
    SetTexts(Saved ? FString::Printf(TEXT("저장된 측정값: %s"), *Summary(*Saved)) : FString(), IdleButton());
}

// ============================================================================
// 상태기계
// ============================================================================

FText UVRBodyMeasureComponent::StepPrompt(EStep S)
{
    switch (S)
    {
    case EStep::Height:  return FText::FromString(TEXT("1/4 키\n똑바로 서서 정면을 보고 X"));
    case EStep::ArmSpan: return FText::FromString(TEXT("2/4 팔 벌림\n양팔을 옆으로 쭉 펴고(T자세) X"));
    case EStep::Knee:    return FText::FromString(TEXT("3/4 무릎\n오른손 컨트롤러를 오른쪽 무릎 옆에 대고 X"));
    case EStep::Hip:     return FText::FromString(TEXT("4/4 엉덩이\n오른손 컨트롤러를 골반 옆에 대고 X"));
    default:             return FText::GetEmpty();
    }
}

void UVRBodyMeasureComponent::Start()
{
    if (IsMeasuring()) return;
    EnsureLoaded();
    Step = EStep::Height;
    LastResultLine = TEXT("신체 측정 — 4단계. 자세를 잡고 왼손 X 버튼을 누르면 잽니다.");
    // 입력 액션이 폰에 지정되지 않으면 X 를 눌러도 영영 안 넘어간다 — 조용히 멈추지 않게 안내에 알린다.
    if (const AVRPawn* Pawn = GetPawn(); Pawn && !Pawn->IA_BodyMeasureCapture)
    {
        LastResultLine += TEXT("\nX 입력이 설정되지 않았어요");
    }
    SetTexts(LastResultLine + TEXT("\n") + StepPrompt(Step).ToString(), TEXT("취소"));
}

void UVRBodyMeasureComponent::Cancel()
{
    if (!IsMeasuring()) return;
    Step = EStep::Idle;
    SetTexts(TEXT("측정을 취소했습니다."), IdleButton());
}

void UVRBodyMeasureComponent::Fail(const FText& Why)
{
    Step = EStep::Idle;
    SetTexts(FString::Printf(TEXT("측정 실패 — %s. 버튼을 눌러 다시 시도하세요."), *Why.ToString()), IdleButton());
}

void UVRBodyMeasureComponent::Capture()
{
    if (!IsMeasuring()) return;

    float Value = 0.f;
    FText Why;
    if (!SampleStep(Value, Why))
    {
        // 같은 단계에 머문다 — 사유와 같은 안내를 다시 보여 X 를 다시 누르게 한다.
        SetTexts(FString::Printf(TEXT("%s — 다시 X\n%s"), *Why.ToString(), *StepPrompt(Step).ToString()), TEXT("취소"));
        return;
    }

    Values[static_cast<int32>(Step) - 1] = Value;
    LastResultLine = FString::Printf(TEXT("측정됨: %s"), *Cm(Value));

    if (Step == EStep::Hip)
    {
        Finish();
        return;
    }
    Step = static_cast<EStep>(static_cast<uint8>(Step) + 1);
    SetTexts(LastResultLine + TEXT("\n") + StepPrompt(Step).ToString(), TEXT("취소"));
}

bool UVRBodyMeasureComponent::IsHandLive(bool bLeft) const
{
    const AVRPawn* Pawn = GetPawn();
    if (!Pawn) return false;
    const UMotionControllerComponent* Controller = bLeft ? Pawn->MotionControllerLeft : Pawn->MotionControllerRight;
    if (Controller && Controller->IsTracked()) return true;

    // 컨트롤러가 없으면 핸드트래킹이 실시간으로 잡히는지 — UVRHandComponent 의 TrackState 는 끊겨도 마지막 자세를 들고 있어 쓸 수 없다.
    if (!GEngine || !GEngine->XRSystem.IsValid()) return false;
    FXRHandTrackingState State;
    GEngine->XRSystem->GetHandTrackingState(GetOwner(), EXRSpaceType::UnrealWorldSpace,
        bLeft ? EControllerHand::Left : EControllerHand::Right, State);
    return State.bValid && State.TrackingStatus == ETrackingStatus::Tracked;
}

bool UVRBodyMeasureComponent::GetRightHandPoint(FVector& OutLocation) const
{
    const AVRPawn* Pawn = GetPawn();
    if (!Pawn) return false;

    // 컨트롤러 포즈를 직접 읽는다 — 핸드트래킹 위주로 바뀐 뒤에도 컨트롤러를 쥐고 있으면 이 컴포넌트는 컨트롤러를 따른다.
    if (Pawn->MotionControllerRight && Pawn->MotionControllerRight->IsTracked())
    {
        OutLocation = Pawn->MotionControllerRight->GetComponentLocation();
        return true;
    }
    // 컨트롤러를 안 쥐고 손만 보이면 실제 손(앵커) 위치로 대신한다.
    if (IsHandLive(false) && Pawn->HandRight)
    {
        OutLocation = Pawn->HandRight->GetComponentLocation();
        return true;
    }
    // 헤드셋 없는 PIE 시험용 — 추적 요구를 끄면 추적 여부와 무관하게 컨트롤러 컴포넌트 위치.
    if (!bRequireTracking && Pawn->MotionControllerRight)
    {
        OutLocation = Pawn->MotionControllerRight->GetComponentLocation();
        return true;
    }
    return false;
}

bool UVRBodyMeasureComponent::SampleStep(float& OutValue, FText& OutWhy) const
{
    const AVRPawn* Pawn = GetPawn();
    if (!Pawn)
    {
        OutWhy = FText::FromString(TEXT("폰이 없어요"));
        return false;
    }

    switch (Step)
    {
    case EStep::Height:
        OutValue = Pawn->GetCurrentHMDHeight();
        if (bRequireTracking && OutValue < MinTrackedHMDHeight)
        {
            OutWhy = FText::FromString(TEXT("헤드셋 위치가 안 잡혀요"));
            return false;
        }
        return true;

    case EStep::ArmSpan:
    {
        if (bRequireTracking && !(IsHandLive(true) && IsHandLive(false)))
        {
            OutWhy = FText::FromString(TEXT("손이 안 보여요"));
            return false;
        }
        if (!Pawn->HandLeft || !Pawn->HandRight)
        {
            OutWhy = FText::FromString(TEXT("손 컴포넌트가 없어요"));
            return false;
        }
        // 실제 손(앵커) 사이 수평 거리 — 핸드트래킹이면 손바닥 관절, 아니면 컨트롤러 그립 포즈.
        OutValue = FVector::Dist2D(Pawn->HandLeft->GetComponentLocation(), Pawn->HandRight->GetComponentLocation());
        return true;
    }

    case EStep::Knee:
    case EStep::Hip:
    {
        FVector P;
        if (!GetRightHandPoint(P))
        {
            OutWhy = FText::FromString(TEXT("오른손이 안 보여요"));
            return false;
        }
        OutValue = Pawn->HeightAboveFloor(P.Z);
        return true;
    }
    default:
        return false;
    }
}

bool UVRBodyMeasureComponent::Validate(const float (&V)[4], FText& OutWhy) const
{
    const float Height = V[0], Arm = V[1], Knee = V[2], Hip = V[3];
    auto Reject = [&OutWhy](const TCHAR* Msg) { OutWhy = FText::FromString(Msg); return false; };

    if (Height < HeightRange.X || Height > HeightRange.Y)  return Reject(TEXT("키 값이 비정상이에요"));
    if (Arm < ArmSpanRange.X || Arm > ArmSpanRange.Y)      return Reject(TEXT("팔 벌림 값이 비정상이에요"));
    if (Knee < MinKneeHeight)                              return Reject(TEXT("무릎 높이가 너무 낮아요"));
    if (Knee >= Hip)                                       return Reject(TEXT("무릎이 엉덩이보다 높아요"));
    if (Hip >= Height)                                     return Reject(TEXT("엉덩이가 키보다 높아요"));
    return true;
}

void UVRBodyMeasureComponent::Finish()
{
    FText Why;
    if (!Validate(Values, Why))
    {
        Fail(Why);
        return;
    }

    UBodyMeasureSaveGame* Data = Cast<UBodyMeasureSaveGame>(UGameplayStatics::CreateSaveGameObject(UBodyMeasureSaveGame::StaticClass()));
    if (!Data)
    {
        Fail(FText::FromString(TEXT("저장 객체를 못 만들었어요")));
        return;
    }
    Data->bValid = true;
    Data->Height = Values[0];
    Data->ArmSpan = Values[1];
    Data->KneeHeight = Values[2];
    Data->HipHeight = Values[3];
    if (!UGameplayStatics::SaveGameToSlot(Data, UBodyMeasureSaveGame::SlotName, 0))
    {
        Fail(FText::FromString(TEXT("저장에 실패했어요")));
        return;
    }
    Saved = Data;
    bLoaded = true;
    Step = EStep::Idle;

    // 측정 직후 자세 판정 기준을 바로 갱신한다(다음 실행을 기다리지 않는다).
    if (AVRPawn* Pawn = GetPawn()) Pawn->SetStandingHeight(Data->Height);

    UE_LOG(LogTemp, Log, TEXT("[BodyMeasure] 저장: %s"), *Summary(*Data));
    SetTexts(FString::Printf(TEXT("측정 완료 — %s. 저장했습니다. 버튼을 누르면 다시 잽니다."), *Summary(*Data)), IdleButton());
}
