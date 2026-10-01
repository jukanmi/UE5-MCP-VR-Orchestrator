#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCBoneCapsuleSet.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

/** 뼈 캡슐 하나의 선분 정의 — 같은 뼈대 계열(뼈 이름이 같은 메시들)이 공유한다. */
USTRUCT(BlueprintType)
struct FNPCBoneCapsule
{
    GENERATED_BODY()

    /** 선분 시작 관절. 이 뼈와 그 아래 지정 안 된 뼈(손가락·목 등)의 정점이 이 캡슐 몫이다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName StartBone;

    /** 자동 채움 때 축 방향을 정하는 다음 관절. None 이면 관절 → 정점 무게중심(머리·손·발 같은 끝 부위).
     *  Quaternius 의 `_end` 뼈는 Blender 꼬리라 방향이 엉망이니 쓰지 않는다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName EndBone;
};

/** 메시 하나에 맞춘 캡슐 치수 — 시작 관절에서 축 방향 거리(cm)로 잡은 선분 구간과 반지름. 메시 공간 cm. */
USTRUCT(BlueprintType)
struct FNPCCapsuleFit
{
    GENERATED_BODY()

    /** 캡슐 축 — 시작 뼈 로컬 공간 단위 벡터. 런타임은 뼈 회전만으로 월드 축을 얻는다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FVector Axis = FVector::ZAxisVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
    float Radius = 5.f;

    /** 선분 시작 — 시작 관절에서 축 방향으로 몇 cm(음수면 관절 뒤쪽). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Start = 0.f;

    /** 선분 끝 — 시작 관절에서 축 방향으로 몇 cm. Start 와 같으면 구. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float End = 0.f;
};

USTRUCT(BlueprintType)
struct FNPCMeshCapsuleFits
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<USkeletalMesh> Mesh;

    /** Capsules 와 같은 순서. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FNPCCapsuleFit> Fits;
};

/** 월드 공간 캡슐 — 선분 A·B + 반지름. */
struct FNPCWorldCapsule
{
    FVector A;
    FVector B;
    float Radius;
    FName Bone;
};

/**
 * NPC 뼈 캡슐 세트 — Physics Asset 대신 뼈 선분 + 반지름으로 몸 모양을 나타낸다(플레이어 손 충돌 판정용).
 * 뼈대 계열마다 하나. 선분 정의(Capsules)는 공유, 치수(Meshes)는 체형이 달라 메시별.
 */
UCLASS(BlueprintType)
class UE5_MCP_VR_API UNPCBoneCapsuleSet : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 첫 번째가 몸통 — 지정 캡슐 조상이 없는 뼈(골반 위 루트 등)의 정점도 받는다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capsules")
    TArray<FNPCBoneCapsule> Capsules;

    /** 이 뼈 아래 정점은 맞춤에서 뺀다(손에 붙은 무기 등 — 안 빼면 손 캡슐이 무기 길이만큼 커진다). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capsules")
    TArray<FName> SkipBones;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capsules")
    TArray<FNPCMeshCapsuleFits> Meshes;

    /** 자동 채움 반지름 = 축까지 정점 거리의 이 백분위(1 이면 최대 — 장식·머리카락에 끌려 부푼다). */
    UPROPERTY(EditAnywhere, Category = "Auto Fill", meta = (ClampMin = "0.1", ClampMax = "1"))
    float RadiusPercentile = 0.75f;

    /** 자동 채움 선분 구간 = 축 방향 정점 분포의 [이 값, 1 - 이 값] 백분위(양 끝 튀는 정점 무시). */
    UPROPERTY(EditAnywhere, Category = "Auto Fill", meta = (ClampMin = "0", ClampMax = "0.2"))
    float ExtentPercentile = 0.02f;

    /** Meshes 의 모든 메시에 대해 Fits 를 정점에서 다시 계산한다(에디터 전용, 기존 값 덮어씀). */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Auto Fill")
    void FillFromMeshes();

    /** 메시 컴포넌트의 현재 포즈로 월드 캡슐을 만든다. 이 세트에 메시가 없으면 false. */
    bool GetWorldCapsules(const USkeletalMeshComponent* MeshComp, TArray<FNPCWorldCapsule>& Out) const;

    /** 월드 캡슐을 디버그 드로우로 그린다. Duration 0 이면 한 프레임. */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void DrawDebug(const USkeletalMeshComponent* MeshComp, float Duration = 0.f) const;

private:
    const FNPCMeshCapsuleFits* FindFits(const USkeletalMesh* Mesh) const;
};
