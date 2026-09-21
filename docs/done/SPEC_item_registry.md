# SPEC_item_registry.md — 아이템 데이터테이블 구축 계획

**상태**: 2026-08-29 작성 · **동일자 72행 주입·저장 완료** (Phase 1 + Phase 2 + Phase 3 재료 일부)
**대상 에셋**: `/Game/Data/Items/DT_ItemRegistry` (RowStruct = `FItemData`)
**원천 파일(신규)**: `Content/Data/Items/ItemRegistry.csv` — git 추적, 이 CSV 가 정본
**관련**: `SPEC_player_systems.md` §3(픽업)·§4(GiveItem/HandObject)

---

## §1 배경 — 왜 지금 채워야 하나

### 1.1 실측된 불일치

`DT_ItemRegistry` 의 유일 행은 `Rock`. 그런데 Stage1 SLM(v3) 학습데이터 1016건 아이템 언급 중 `Rock` 은 **0건**. 반대로 모델이 학습한 75개 ID 중 DataTable 에 존재하는 것도 **0개**.

### 1.2 파급 연쇄

1. `InventoryComponent::InitialDefaultItems` → `ItemManager::GetItemDataByID()` 조회 실패 → NPC 인벤토리에 `Rock` 만 적재
2. `dialogue.py:236` 이 NPC 인벤토리를 프롬프트로 주입
3. 프롬프트 제약 `"Only GiveItem/HandObject/UseItem/Equip an item that is in YOUR inventory"` → LLM 이 볼 수 있는 아이템이 `Rock` 뿐
4. 결과: `GiveItem` · `HandObject` · `UseItem` · `Equip` · `Trade` · `Craft` **6개 액션이 전부 돌멩이 하나로 수렴**

`SPEC_player_systems.md` §4(플레이어 인벤토리 연동)를 구현해도 **줄 물건이 없다**. 즉 이 테이블 확충은 §4 의 선행 조건.

### 1.3 완화 요인 — alias 맵이 불필요한 이유

학습데이터엔 표기 변종이 섞여 있다(`Sword`/`KnightSword`, `Bread`/`BreadLoaf`, `Potion`/`HealthPotion`/`HealPotion`, `Coin`/`CoinPouch`, `Water`/`WaterSkin`, `Bow`/`HuntingBow`, `Shield`/`Shield_Knight`/`GuardShield`).

그러나 **런타임은 인벤토리 주입 방식**이라 모델이 컨텍스트에 적힌 ID 를 그대로 베낀다. `InitialDefaultItems` 에 정본 ID 만 넣으면 변종 출현은 억제된다. **C++ 에 alias/폴백 맵을 추가하지 말 것** — 오히려 오타를 정상 동작으로 위장시켜 데이터 결함을 은폐한다.

---

## §2 출처 권위 서열 (향후 행 추가 시 필수 준수)

행을 새로 만들 때 ID·성격은 아래 순서로 결정한다. 상위 출처와 충돌하면 상위가 이긴다.

| 순위 | 출처 | 역할 | 비고 |
|:--:|---|---|---|
| 1 | `docs/CORE_NPC_LOREBOOK.md` **아이템 친화도** | **정본 ID** | 세계관·NPC 역할과 정합. 코어 5인 + 보조 10인 |
| 2 | `finetune/data/synth/generated/scenarios.yaml` 빈도 | **런타임 충실도** | v3 SLM 이 실제로 뱉는 어휘. 빈도 = 등장 확률 |
| 3 | 코드 요구 (`EItemType` / `EEquipmentSlot` / `bHasDurability` 분기) | **경로 커버리지** | 미사용 enum 값은 행 없이 방치 금지 |
| 4 | 임의 창작 | 최후 | 1~3 에 근거 없으면 추가하지 말 것 |

### 2.1 서열 1·2 교차검증 결과 (2026-08-29 실측)

**로어북에 있으나 학습데이터 0건** — 모델이 스스로 못 꺼냄. 행은 만들되 `InitialDefaultItems` 로 명시 지급해야 등장:
`OathScroll` · `WaterBucket` · `HonorFlower` · `DivingHelmet` · `GoldChest` · `Microphone` · `SoftBlanket` · `HerbBasket` · `PassDoc` · `ShipWheel` · `BribeCoin`

**학습데이터 상위인데 로어북 코어에 없음** — synth 생성기가 붙인 범용 베이스라인. 전 NPC 공용으로 채택:
`Bread`(120) · `WaterSkin`(114) · `CoinPouch`(104)

---

## §3 ID·필드 규약

### 3.1 ID 명명

- **PascalCase**, 공백·하이픈 금지. RowName == `ItemID` 필드 (둘을 반드시 일치시킬 것)
- 소유자·계열 한정이 필요할 때만 접미 언더스코어: `Shield_Knight` · `Cutlass_Pirate` · `Staff_Apprentice`
  (로어북 표기를 그대로 따른다. 새로 만들 때 이 패턴을 남발하지 말 것)
- 축약 금지: `HP_Potion` ✗ → `HealthPotion` ✓
- LLM 이 읽는 식별자이므로 **영어 명사구**로 의미가 서는 형태 유지

### 3.2 타입별 필드 기본값 정책

| `ItemType` | `Weight` | `MaxStack` | `EquipSlot` | `BaseValue` | `bHasDurability` |
|---|---|---|---|---|---|
| `General` | 0.1 ~ 2.5 | 16 ~ 64 (고유물은 1) | `None` | 1 ~ 200 | `false` |
| `Consumable` | 0.2 ~ 1.5 | 4 ~ 16 | `None` | 5 ~ 60 | `false` |
| `Equipment` | 1.0 ~ 6.0 | **1 고정** | 필수 지정 | 100 ~ 350 | **`true`** |
| `Quest` | 0.05 ~ 0.5 | **1 고정** | `None` | **0 고정** | `false` |

- `Quest` 의 `BaseValue = 0` 은 "판매·분해 불가"의 데이터 표현. 상점 로직 도입 시 이 값으로 거르면 된다.
- `Equipment` 는 전부 내구도 사용 — `RepairItem()` / `Repair` 액션 경로를 살려두기 위함.
- `CurrentDurability` 는 항상 `MaxDurability` 와 동일하게 초기화(템플릿 = 신품 상태).

### 3.3 `Description` 작성 규칙

`FItemData::Description` 주석에 명시된 대로 **LLM 이 아이템 용도를 이해하는 근거**다. 따라서:

- 한국어 1문장, 40자 내외
- **용도·상황**을 쓸 것. 외형 묘사만 쓰지 말 것
  - ✗ `"낡은 가죽 물통."`
  - ✓ `"갈증을 해소하는 가죽 물통. 지친 이에게 건네기 좋다."`
- 액션 힌트를 심어도 좋음(`건네기`, `마시면`, `장착하면`) — GiveItem/UseItem 선택률에 기여

### 3.4 비주얼 필드

`Icon` · `WorldMeshClass` 는 **Phase 1 에서 전부 공란**. 에셋이 없다.
- `Icon` 공란 → HUD 슬롯이 빈 이미지로 뜸 (WBP 에서 널 폴백 처리 필요, DoList)
- `WorldMeshClass` 공란 → `ExecuteDrop` 스폰 불가 (이미 알려진 TODO 스텁, `Memo.md` 참조)

---

## §4 Phase 1 — 코어 5인 구동 최소셋 (26행)

선정 기준: 로어북 코어 5인 친화 아이템 + 학습 상위 베이스라인 + 타입/슬롯 커버리지.

> **주입 실적 (2026-08-29)**: 실제로는 아래 26행에 §5 Phase 2 전량(로어북 보조 NPC 10인)과
> Phase 3 재료 일부(`IronOre`·`Plank`·`Leather`)를 더한 **72행**을 한 번에 주입했다.
> 최종 분포 — General 31 · Equipment 19 · Consumable 14 · Quest 8 /
> 슬롯 MainHand 11 · OffHand 4 · Head 3 · Accessory 1.
> 이 §4 표는 "코어 5인만으로 최소 구동하는 경계"를 기록으로 남긴 것 — 롤백 시 여기까지 줄이면 된다.

### 4.1 공용 베이스라인 (전 NPC 공통 지급)

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `Bread` | Consumable | 0.4 | 16 | — | 8 | — | 허기를 달래는 딱딱한 호밀빵. 굶주린 이에게 나눠주기 좋다. |
| `Bandage` | Consumable | 0.2 | 16 | — | 12 | — | 상처를 감는 마천 붕대. 다친 사람에게 감아주면 지혈된다. |
| `WaterSkin` | Consumable | 1.2 | 4 | — | 15 | — | 갈증을 해소하는 가죽 물통. 지친 이에게 건네기 좋다. |
| `CoinPouch` | General | 0.5 | 1 | — | 100 | — | 은화가 든 가죽 주머니. 거래나 뇌물에 쓰인다. |
| `Torch` | Equipment | 1.0 | 1 | OffHand | 15 | 120 | 어둠을 밝히는 송진 횃불. 들면 주변 시야가 트인다. |

### 4.2 Elara — 기사단장

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `KnightSword` | Equipment | 4.5 | 1 | MainHand | 250 | 200 | 기사단 제식 장검. 장착하면 정면 교전에 나설 수 있다. |
| `Shield_Knight` | Equipment | 6.0 | 1 | OffHand | 180 | 250 | 기사단 문장이 새겨진 방패. 들면 적의 공격을 막아낸다. |
| `HealthPotion` | Consumable | 0.5 | 8 | — | 60 | — | 붉은 치유 물약. 마시면 상처가 아문다. |
| `OathScroll` | Quest | 0.1 | 1 | — | 0 | — | 기사단 서약이 적힌 두루마리. 명예를 인정한 자에게 수여한다. |

### 4.3 Skadi — 해적선장

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `Cutlass_Pirate` | Equipment | 3.0 | 1 | MainHand | 200 | 160 | 해적의 굽은 커틀러스. 장착하면 근접전을 벌인다. |
| `Pistol` | Equipment | 2.2 | 1 | MainHand | 320 | 100 | 화승식 부싯돌 권총. 한 발로 기선을 제압한다. |
| `RumBottle` | Consumable | 1.5 | 4 | — | 35 | — | 독한 사탕수수 럼주. 마시면 사기가 오른다. |
| `TreasureKey` | Quest | 0.2 | 1 | — | 0 | — | 보물궤를 여는 녹슨 열쇠. 해적선장이 목숨처럼 지닌다. |

### 4.4 Moca — 소울 힐러 (전투 불가)

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `HerbTea` | Consumable | 0.6 | 8 | — | 25 | — | 따뜻한 약초 차. 건네면 마음이 진정된다. |
| `SmokeBomb` | Consumable | 0.4 | 8 | — | 45 | — | 연막을 터뜨리는 구슬. 고립되었을 때 도주에 쓴다. |
| `HerbBasket` | General | 2.0 | 1 | — | 30 | — | 약초를 담은 바구니. 차와 연고의 재료가 된다. |

> Moca 는 `Bandage` · `HealthPotion` 을 베이스라인/Elara 셋과 공유한다(로어북 친화도 일치).

### 4.5 James — 항해사

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `Compass` | General | 0.3 | 1 | — | 120 | — | 자침이 달린 황동 나침반. 항로와 방위를 읽는다. |
| `StarMap` | Quest | 0.2 | 1 | — | 0 | — | 별자리 좌표가 그려진 성도판. 항해 계산에 반드시 필요하다. |
| `NavDagger` | Equipment | 1.2 | 1 | MainHand | 140 | 120 | 항해사의 호신용 단검. 밧줄을 끊고 몸을 지킨다. |
| `Telescope` | General | 1.8 | 1 | — | 200 | — | 놋쇠 망원경. 먼 수평선과 별을 관측한다. |

### 4.6 Guard — 성문 경비관

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `GuardSpear` | Equipment | 5.0 | 1 | MainHand | 160 | 180 | 경비대 제식 장창. 관문을 지키며 접근을 막는다. |
| `GuardShield` | Equipment | 5.5 | 1 | OffHand | 140 | 220 | 성문 경비용 대형 방패. 들면 통로를 봉쇄한다. |
| `Whistle` | General | 0.1 | 1 | — | 20 | — | 비상 호출 휘슬. 불면 동료 경비병이 달려온다. |
| `PassDoc` | Quest | 0.05 | 1 | — | 0 | — | 영주 직인이 찍힌 통행증. 검문 시 제시해야 한다. |

### 4.7 조합 재료

| ItemID | Type | Weight | Stack | Slot | Value | Dur | Description |
|---|---|--:|--:|---|--:|:--:|---|
| `Rock` | General | 1.0 | 64 | — | 1 | — | 흔한 회색 돌덩이. 던지거나 조합 재료로 쓴다. |
| `IronIngot` | General | 2.5 | 32 | — | 40 | — | 제련된 철 주괴. 무기와 도구를 벼리는 재료다. |

> `Rock` 은 기존 유일 행 — **삭제하지 말 것**. 현재 `BP_SmartNPC` 의 `InitialDefaultItems` 가 이 ID 를 참조 중.

### 4.8 커버리지 자가검증

| 검증 항목 | 충족 |
|---|---|
| `EItemType` 4종 | General(7) · Consumable(8) · Equipment(8) · Quest(4) ✅ |
| `EEquipmentSlot` | `MainHand`(5) · `OffHand`(3) ✅ — Head/Torso/Legs/Feet/Gloves/Accessory/Back 은 방어구 미도입, Phase 3 |
| `bHasDurability = true` | 8행 ✅ (`RepairItem` / `Repair` 액션 경로 생존) |
| `MaxStack` 분기 | 1 / 4 / 8 / 16 / 32 / 64 ✅ |
| `ExecuteTrade` 양방향 | Skadi(`RumBottle`) ↔ James(`Compass`) ✅ |
| `ExecuteCraft` 재료 | `Rock` + `IronIngot` + `HerbBasket` ✅ (레시피 검증은 TODO 스텁, 무관) |
| `SPEC_player_systems` §6 PIE #4~#6 | `Bread`(픽업) · `HealthPotion`(GiveItem) · `OathScroll`(HandObject) ✅ |

---

## §5 Phase 2 이후 — 확장 로드맵

Phase 1 주입·검증 완료 후 착수. 각 Phase 는 독립 커밋.

### Phase 2 — 보조 NPC 10인 (약 30행) — ✅ 2026-08-29 주입 완료

`CORE_NPC_LOREBOOK.md` §6 표의 "대표 친화 아이템" 을 그대로 행으로 승격. 아래 표는 주입된 내역.

| NPC | 신규 행 |
|---|---|
| Merchant_Kaelen | `MagnifyingGlass` · `GoldCoins` · `MagicGem` |
| Scholar_Thalia | `AncientScroll` · `QuillPen` · `Glasses` |
| Assassin_Morvath | `PoisonDagger` · `Lockpick` · `SmokeVial` |
| Priest_Eldrin | `HolyWater` · `HolyBook` |
| Blacksmith_Balgor | `ForgeHammer` · `Whetstone` |
| Alchemist_Lysandra | `AlchemistryVial` · `HerbExtract` · `Antidote` |
| Bard_Finnegan | `Lute` · `FeatherCap` · `WineCup` |
| Ranger_Karen | `TrapDisarmKit` · `MapCompass` · `HuntingBow` |
| Shipwright_Garrick | `TarBucket` · `ShipBoard` · `RepairHammer` |
| Astrologer_Cassandra | `CrystalBall` · `ProphecyScroll` · `StarPendant` |

> ✅ `MapCompass`(Ranger) ↔ `Compass`(James) 중복은 `Compass` 로 통합 완료. 로어북 §6 Ranger_Karen 행도 2026-08-29 함께 수정(서열 1 문서가 정본이라 문서 쪽을 고쳐야 다음 세션이 재추가하지 않는다).

### Phase 3 — 시스템 확장 시

| 트리거 | 추가 행 |
|---|---|
| 방어구 도입 | `EEquipmentSlot` Head/Torso/Legs/Feet/Gloves/Accessory/Back 각 1행 이상 |
| 상점·경제 | `GoldCoins` · `SilverCoin` 등 화폐 계층, `BaseValue` 밸런싱 재검토 |
| Craft 레시피 확정 | 중간재 `IronOre` · `Plank` · `Leather` 는 **2026-08-29 선주입 완료**. 추가 중간재는 **레시피 데이터 설계 선행** (`Memo.md` 스텁 3종 참조) |
| 학습데이터 재생성 | 신규 ID 를 `persona_pool.yaml` 인벤토리에 먼저 반영 → synth 재생성 → 그 다음 DataTable |

### 잔여 학습데이터 ID (2026-08-29 주입 후 재집계)

**진짜 미등록 4건** — 소유 NPC 없음(로어북 근거 없음). 필요 대두 시 개별 판단:
`RuneStone`(11) · `OldTome`(8) · `Staff_Apprentice`(7) · `IncenseStick`(7)

**변종이라 등록 불필요 5건** — 이미 정본 ID 로 커버됨. 모델이 뱉어도 인벤토리 주입 방식이라 출현 억제:

| 학습 표기 | 정본 ID |
|---|---|
| `Sword`(8) | `KnightSword` |
| `BreadLoaf`(6) | `Bread` |
| `Water`(5) | `WaterSkin` |
| `Spectacles`(8) | `Glasses` |
| `TrapKit`(11) | `TrapDisarmKit` |

### 노이즈 — 행으로 만들지 말 것

학습데이터의 `item` 필드에 NPC 이름·선체가 오염 유입됨(총 6건): `Elara`(2) · `Skadi`(1) · `Guard`(1) · `Ship_Hull`(1).
→ 차기 synth 재생성 시 `audit_speaker_listener.py` 계열 필터에 아이템 필드 검사 추가할 것.

---

## §6 주입 방법 — CSV 원천 + 에디터 재생성

### 6.1 왜 CSV 를 원천으로 두나

`.uasset` 은 바이너리라 **diff 가 불가능**하다. 아이템 한 줄을 고쳐도 리뷰에서 무엇이 바뀌었는지 볼 수 없고, 병합 충돌 시 복구가 어렵다. `CLAUDE.md` §9(에디터 수작업 최소화 — 바이너리에만 두지 말 것) 와도 정면으로 맞는다.

→ `Content/Data/Items/ItemRegistry.csv` 를 **정본**으로 git 추적하고, `.uasset` 은 그 CSV 에서 재생성되는 산출물로 취급한다.

**향후 행 추가 절차가 이걸로 3단계가 된다:**
1. CSV 에 행 추가 (텍스트 편집 — diff 가 남는다)
2. 재생성 스크립트 1회 실행
3. CSV + `.uasset` 을 같은 커밋에 (`CLAUDE.md` Git §단위 — `.uasset` 은 관련 소스와 한 커밋)

### 6.2 CSV 포맷

헤더 첫 열은 RowName 예약 열(`Name`), 나머지는 `FItemData` 프로퍼티명과 **철자 일치** 필수.

```
Name,ItemID,DisplayName,Description,ItemType,Weight,MaxStack,EquipSlot,BaseValue,bHasDurability,MaxDurability,CurrentDurability,Icon,WorldMeshClass
Bread,Bread,호밀빵,"허기를 달래는 딱딱한 호밀빵. 굶주린 이에게 나눠주기 좋다.",Consumable,0.4,16,None,8,False,100.0,100.0,,
```

- `Name` 과 `ItemID` 는 항상 동일 값 (§3.1)
- 쉼표를 포함한 `Description` 은 큰따옴표로 감쌀 것
- enum 은 접두어 없는 짧은 형태(`Consumable` / `MainHand`)
- `Icon` · `WorldMeshClass` 는 빈 칸

### 6.3 재생성 실행

에디터가 열린 상태에서 `mcp__ue5__ue_run_python` 으로:

```python
import unreal
dt = unreal.EditorAssetLibrary.load_asset('/Game/Data/Items/DT_ItemRegistry')
csv = open(r'C:\github\UE5_MCP_VR\Content\Data\Items\ItemRegistry.csv',
           encoding='utf-8-sig').read()
errs = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, csv)
print(errs)                                    # 빈 리스트 = 전 행 성공
unreal.EditorAssetLibrary.save_loaded_asset(dt)
print(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
```

**주의 3가지:**
- `fill_data_table_from_csv_string` 는 **전체 치환**이다. 기존 행 보존이 아니라 CSV 가 곧 최종 상태 → `Rock` 을 CSV 에 반드시 포함할 것(§4.7)
- 한글 CSV 는 `utf-8-sig` 로 읽어야 BOM 이 첫 헤더명을 오염시키지 않는다
- 반환된 에러 리스트가 비어있지 않으면 **저장하지 말 것**. 열 이름 오타 또는 enum 값 불일치다

### 6.4 실패 시 폴백

`DataTableFunctionLibrary` 에 해당 함수가 노출되지 않는 빌드라면, 에디터에서 DataTable 우클릭 → **Reimport / Import from CSV** 로 동일 CSV 를 지정한다(에디터 수작업, DoList 이관).

---

## §7 주입 후 검증

### 7.1 즉시 (클로드 수행) — ✅ 2026-08-29 완료

1. ✅ `get_data_table_row_names` = **72개**, `Rock` 생존
2. ✅ `get_data_table_column_as_string` 으로 `ItemType`·`EquipSlot`·`MaxStack`·`bHasDurability`·`BaseValue` 전량 덤프 → 엔진 내 분포가 CSV 분석값과 **완전 일치**
3. ✅ 엔진 내 §3.2 정책 재검증 위반 0건

> **함정 2가지 (다음 주입 때 반드시 재확인)**
> - `fill_data_table_from_csv_string` 의 반환값은 **에러 리스트가 아니라 성공 bool** 이다. `True` = 성공. 리스트로 오해해 `if errs:` 로 분기하면 성공을 실패로 읽는다.
> - CSV enum 파싱이 실패하면 예외 없이 **기본값으로 조용히 강등**된다(`ItemType::General` / `EquipSlot::None`). 저장 전에 반드시 엔진 내 분포를 CSV 분포와 대조할 것 — 행 수만 세면 못 잡는다.

### 7.2 NPC 인벤토리 배정 — ✅ 2026-08-29 MCP 처리 완료 (DoList 아님)

행만 만들면 NPC 는 여전히 `Rock` 만 갖는다. `InitialDefaultItems` 를 채워야 §1.2 연쇄가 끊긴다.

**구조 실측**: NPC 파생 BP 는 없다. `BP_SmartNPC` 하나뿐이고 NPC 구분은 **레벨 인스턴스의 `AgentID`** 로 한다.
→ 공통분은 **CDO**, 개체 고유분은 **레벨 인스턴스 오버라이드**로 넣는다. (배열 프로퍼티는 병합이 아니라 **전체 치환**이므로 인스턴스에는 공통분까지 포함한 전체 목록을 적을 것)

| 대상 | `InitialDefaultItems` | 상태 |
|---|---|---|
| **CDO** `BP_SmartNPC` (향후 배치분 상속) | `Bread` · `Bandage` · `WaterSkin` · `CoinPouch` · `Torch` | ✅ 적용·저장 |
| **인스턴스** `BP_SmartNPC4` (`AgentID=Moca`) | 위 5종 + `HerbTea` · `SmokeBomb` · `HerbBasket` · `HealthPotion` | ✅ 적용·레벨 저장 |

**미배치 NPC 예약 목록** — 레벨에 배치될 때 인스턴스에 적을 것(전부 공통 5종 포함한 전체 목록):

| AgentID | 전체 목록 |
|---|---|
| Elara | 공통 5 + `KnightSword` · `Shield_Knight` · `HealthPotion` · `OathScroll` |
| Skadi | 공통 5 + `Cutlass_Pirate` · `Pistol` · `RumBottle` · `TreasureKey` |
| James | 공통 5 + `Compass` · `StarMap` · `NavDagger` · `Telescope` |
| Guard | 공통 5 + `GuardSpear` · `GuardShield` · `Whistle` · `PassDoc` |

### 7.3 PIE (사용자 수행)

1. NPC 대화 로그에 주입된 인벤토리 문자열이 `Rock` 단독이 아님 (`dialogue.py` 프롬프트 확인)
2. "물 좀 줘" → NPC 가 `GiveItem(WaterSkin)` 응답 (v3 SLM 학습 어휘라 재현율 높음)
3. `SPEC_player_systems.md` §6 체크리스트 #4~#6 동반 검증

---

## §8 미결 사항

- [x] **`Icon` 에셋 부재** — 2026-09-01 해소. 아이콘 72종 생성·임포트·경로 매핑 완료(`9b6c47d`).
- [x] **`WorldMeshClass` 부재** — 해소 방식이 바뀌었다. 아이템별 BP 대신 `WorldMesh`(공용 `BP_DropItem` + 메시 교체) 경로로 전 종 커버. `WorldMeshClass` 는 고유 물리가 필요한 아이템용 폴백으로만 남는다.
- [ ] **`MapCompass` ↔ `Compass` 중복** — Phase 2 착수 시 로어북 수정으로 통합
- [ ] **synth 아이템 필드 노이즈 필터** — 차기 데이터 재생성 시 NPC 이름 유입 차단
