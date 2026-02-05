#include "SmartNPC.h"
#include "NPCManager.h"
#include "SmartNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "../Utils/MCPMathUtils.h"
#include "Kismet/GameplayStatics.h"

ASmartNPC::ASmartNPC()
{
    PrimaryActorTick.bCanEverTick = true;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ASmartNPC::BeginPlay()
{
    Super::BeginPlay();

    // Register self
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->RegisterNPC(AgentID, this);
        }
    }
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Unregister
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->UnregisterNPC(AgentID);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void ASmartNPC::ProcessAction(const FGameAction& Action)
{
    const FString& Type = Action.ActionType;
    const TMap<FString, FString>& P = Action.Parameters;

    ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController());
    if (!AI || !AI->GetBlackboardComponent())
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] No valid AI Controller or Blackboard provided for %s"), *GetName());
        // Fallback or just return
        return;
    }

    UBlackboardComponent* BB = AI->GetBlackboardComponent();

    // Reset previous command keys if necessary, or just overwrite.
    
    // Resolve Action Type String to Enum
    ESmartNPCActionState ActionState = ESmartNPCActionState::Generic;
    if (Type == TEXT("Move")) ActionState = ESmartNPCActionState::Move;
    else if (Type == TEXT("Speak")) ActionState = ESmartNPCActionState::Speak;
    else if (Type == TEXT("Attack")) ActionState = ESmartNPCActionState::Attack;
    else if (Type == TEXT("Interact")) ActionState = ESmartNPCActionState::Interact;

    // Set ActionType as Enum
    BB->SetValueAsEnum(ASmartNPCAIController::Key_ActionType, (uint8)ActionState);

    if (ActionState == ESmartNPCActionState::Move)
    {
        // Default Params
        float X = 0.0f;
        float Y = 0.0f; 
        // float Speed = 200.0f; // Speed might need to be set on CharacterMovement or also in BB

        if (const FString* Val = P.Find(TEXT("x"))) X = FCString::Atof(**Val);
        if (const FString* Val = P.Find(TEXT("y"))) Y = FCString::Atof(**Val);
        // if (const FString* Val = P.Find(TEXT("speed"))) Speed = FCString::Atof(**Val);

        // Convert Math (Input X, Y assumed to be Meters on Ground)
        FVector TargetLoc = UMCPMathUtils::ConvertToUnrealLocation(X, Y, 0.0f);
        
        // Update Blackboard Key
        BB->SetValueAsVector(ASmartNPCAIController::Key_TargetLocation, TargetLoc);
    }
    else if (ActionState == ESmartNPCActionState::Speak)
    {
        // ToDo::말하기 구현 (현재는 BP Event 호출로 처리 중, 필요시 Blackboard 연동)
        
        FString Text = TEXT("...");
        if (const FString* Val = P.Find(TEXT("text"))) Text = *Val;

        ExecuteSpeak(Text);
    }
    else if (ActionState == ESmartNPCActionState::Attack)
    {
        // ToDo::공격 구현 - TargetID를 사용하여 실제 Actor를 찾고 Blackboard Key_TargetActor에 할당하는 로직 필요
        // 예: AActor* Target = FindActorByID(TargetParam);
        // BB->SetValueAsObject(ASmartNPCAIController::Key_TargetActor, Target);
        
        FString TargetParam = Action.TargetID;
        ExecuteAttack(TargetParam);
    }
    else if (ActionState == ESmartNPCActionState::Interact)
    {
        // ToDo::상호작용 구현 - 공격과 마찬가지로 대상 Actor 식별 및 Blackboard 설정 필요
        
        FString TargetParam = Action.TargetID;
        ExecuteInteract(TargetParam);
    }
    else
    {
        // ToDo::기타 일반 액션 처리
        FString TargetParam = Action.TargetID;
        ExecuteGenericAction(Type, TargetParam);
    }
}

void ASmartNPC::ClearPhysicalState()
{
    // Default implementation: Can be overriden by Blueprint or Subclasses.
    // ToDo::상태 초기화 로직 구현 (애니메이션 몽타주 중지, 이동 정지 등)
    StopAnimMontage();
    if (GetController())
    {
        GetController()->StopMovement();
    }
}

