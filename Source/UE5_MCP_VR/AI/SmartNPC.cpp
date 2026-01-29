#include "SmartNPC.h"
#include "NPCManager.h"
#include "../Utils/MCPMathUtils.h"
#include "Kismet/GameplayStatics.h"

ASmartNPC::ASmartNPC()
{
    PrimaryActorTick.bCanEverTick = true;
    AgentID = TEXT("UnknownAgent");
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

    if (Type == TEXT("Move"))
    {
        // Default Params
        float X = 0.0f; 
        float Y = 0.0f; 
        float Speed = 200.0f; // Default Speed

        if (const FString* Val = P.Find(TEXT("x"))) X = FCString::Atof(**Val);
        if (const FString* Val = P.Find(TEXT("y"))) Y = FCString::Atof(**Val);
        if (const FString* Val = P.Find(TEXT("speed"))) Speed = FCString::Atof(**Val);

        // Convert Math (Input X, Y assumed to be Meters on Ground)
        // Note: MathUtils::ConvertToUnrealLocation expects (x, y, z).
        // Since Move is 2D usually, we assume Z=0 or input supplies z.
        // If Python sends x,y for 2D map:
        FVector TargetLoc = UMCPMathUtils::ConvertToUnrealLocation(X, Y, 0.0f);
        
        ExecuteMove(TargetLoc, Speed);
    }
    else if (Type == TEXT("Speak"))
    {
        FString Text = TEXT("...");
        if (const FString* Val = P.Find(TEXT("text"))) Text = *Val;

        ExecuteSpeak(Text);
    }
    else
    {
        // Interact, Attack, Emote, etc.
        FString TargetParam = Action.TargetID;
        ExecuteGenericAction(Type, TargetParam);
    }
}

void ASmartNPC::ClearPhysicalState()
{
    // Default implementation: Can be overriden by Blueprint or Subclasses.
    StopAnimMontage();
    if (GetController())
    {
        GetController()->StopMovement();
    }
}

