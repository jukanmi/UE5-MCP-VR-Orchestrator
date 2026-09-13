#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

/**
 * EngineShapes
 *
 * 엔진 기본 도형 + 이미시브 머티리얼 로딩. 프로젝트 에셋을 만들지 않고 런타임 비주얼(포인터·거래 패널
 * 버튼)을 세우는 공통 경로. 에셋이 없으면 nullptr — 호출부는 조용히 비주얼만 생략한다.
 * EmissiveMeshMaterial 의 벡터 파라미터는 "Color" 하나뿐(2026-09-05 에디터 실측).
 */
namespace EngineShapes
{
    inline UStaticMesh* LoadCube()     { return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")); }
    inline UStaticMesh* LoadSphere()   { return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")); }
    inline UStaticMesh* LoadCylinder() { return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }

    inline UMaterialInstanceDynamic* MakeEmissiveMID(UObject* Outer, const FLinearColor& Color)
    {
        UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
        if (!Emissive) return nullptr;

        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Emissive, Outer);
        MID->SetVectorParameterValue(TEXT("Color"), Color);
        return MID;
    }
}
