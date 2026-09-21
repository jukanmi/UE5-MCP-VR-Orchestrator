#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ItemRegistryOptions.generated.h"

namespace ItemRegistryPaths
{
    // 아이템 마스터 테이블의 컨벤션 경로. ItemManager 의 폴백 로드와 에디터 드롭다운이 같은 값을 봐야
    // 한쪽만 옮겼을 때 조용히 어긋나지 않는다.
    inline const TCHAR* DefaultItemTable = TEXT("/Game/Data/Items/DT_ItemRegistry.DT_ItemRegistry");
}

/**
 * 에디터에서 아이템 ID 를 직접 타이핑하지 않고 목록에서 고르게 하는 옵션 공급자.
 *
 * ID 는 계속 FString 이다 — Python/LLM 이 주고받는 값이 문자열 ID 라 타입을 바꾸면
 * 변환 계층이 생기고, 이미 블루프린트에 입력된 목록도 전부 날아간다.
 * 대신 `meta = (GetOptions = "...")` 로 에디터 위젯만 드롭다운으로 바꾼다.
 *
 * 사용법(프로퍼티에 메타 지정):
 *   meta = (GetOptions = "/Script/UE5_MCP_VR.ItemRegistryOptions.GetItemIDOptions")
 */
UCLASS()
class UE5_MCP_VR_API UItemRegistryOptions : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** DT_ItemRegistry 의 전체 RowName 목록. 테이블을 못 찾으면 빈 배열(드롭다운만 비고 값은 보존).
     *  GetOptions 메타에는 UFUNCTION 이면 충분하지만, 경로 오타 시 드롭다운이 조용히 비기만 해서
     *  원인을 못 찾는다. BlueprintCallable 로 열어 두면 스크립트로 직접 불러 검증할 수 있다. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    static TArray<FString> GetItemIDOptions();
};
