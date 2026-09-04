#include "ItemRegistryOptions.h"

#include "Engine/DataTable.h"
#include "BP/ItemDataAsset.h"

TArray<FString> UItemRegistryOptions::GetItemIDOptions()
{
    TArray<FString> Options;

    UDataTable* Table = Cast<UDataTable>(
        StaticLoadObject(UDataTable::StaticClass(), nullptr, ItemRegistryPaths::DefaultItemTable));

    if (!Table)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ItemRegistry] 아이템 테이블을 찾을 수 없어 드롭다운이 빕니다: %s"),
               ItemRegistryPaths::DefaultItemTable);
        return Options;
    }

    for (const FName& RowName : Table->GetRowNames())
    {
        Options.Add(RowName.ToString());
    }

    // 72종을 스크롤해서 찾아야 하므로 정렬해 둔다.
    Options.Sort();
    return Options;
}
