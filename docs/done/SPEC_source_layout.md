# SPEC_source_layout.md — Source 폴더 레이아웃 (BP 진입점 분리)

**상태**: 2026-09-04 **완료** — 1·2단계 모두 적용됨. `Core/BP`·`NPC/BP`·`Inventory/BP`·`Furniture/BP`·`UI/BP` 5개 도메인이 §3.2 목표 레이아웃과 실측 일치.
**브랜치**: `feature/player-systems`
**담당 범위**: `Source/UE5_MCP_VR/` 폴더 전면 계층화(BP, Components, Subsystems, Types, Utils) + 배치 규칙 명문화 + 관련 문서 현행화
**제외**: 없음. (`VRPlayerCharacter`·`BP_Player` 는 2026-08-31 제거 완료 — 이동 대상에서 빠졌다.)

---

## 1. 목표 / Why

### 1.1 문제

`Source/UE5_MCP_VR/` 는 `Core`·`NPC`·`Network`·`Inventory`·`UI`·`Utils`·`Furniture` 로 나뉘어 있는데,
**이 배치를 정한 규칙 문서가 없다.** 2026-08-25 실측:

- `CLAUDE.md` 에 `Source/` 폴더 조항 **0건**. 경로 관련 조항은 §4 EQS **에셋** 정책 하나뿐(`/Game/...`)이고 C++ 배치와 무관.
- `docs/index.html` 의 `§3 핵심 파일 맵`·`§11 디렉토리 구조 요약` 은 **서술형**이다. 해당 구간에서
  "~할 것 / 금지 / 말 것 / 둔다" 류 규범 표현이 **0건**(grep). 현재 상태를 그린 것이지 "새 파일을 어디 둘지"를 정한 게 아니다.

결과로 **블루프린트가 부모로 물고 있는 클래스와 순수 C++ 내부 파일이 한 폴더에 섞여 있다.**
코드만 봐서는 어느 파일을 함부로 이름 바꾸면 에셋이 조용히 끊기는지 알 수 없다.

이미 사고가 한 건 나 있다 — §5.3 의 `NPCInteractionDataAsset` 죽은 참조.

### 1.2 목표

1. 도메인 구조(`NPC`/`Inventory`/…)는 **유지**하고, 각 도메인 **안에서** BP 진입점을 `BP/` 로 갈라낸다.
2. 이번에 **처음으로** 배치 규칙을 `CLAUDE.md` 에 명문화한다. 이게 본체다 — 폴더만 옮기고 규칙을 안 쓰면 6개월 뒤 같은 상태로 돌아간다.
3. 규칙 부재로 이미 생긴 문서 부채·죽은 참조를 같은 작업에서 정리한다.

---

## 2. 분류 기준 — "에셋이 실제 참조하는가"

### 2.1 채택 기준

`Content/**/*.uasset|umap` 바이너리에서 `/Script/UE5_MCP_VR.<Class>` 문자열을 추출한 결과를 단일 근거로 삼는다.
에셋 **190개**를 스캔해 나온 심볼은 **12개뿐**이다.

재생성 명령(작업 시점에 다시 돌릴 것):

```bash
python - <<'PY'
import os,re,collections
pat=re.compile(rb'/Script/UE5_MCP_VR\.([A-Za-z_][A-Za-z0-9_]*)')
use=collections.defaultdict(set)
for r,d,fs in os.walk('Content'):
    for fn in fs:
        if fn.endswith(('.uasset','.umap')):
            for m in set(pat.findall(open(os.path.join(r,fn),'rb').read())):
                use[m.decode()].add(fn.rsplit('.',1)[0])
for k in sorted(use): print(k, sorted(use[k]))
PY
```

### 2.2 기각한 기준 — BP 노출 매크로

`Blueprintable`/`BlueprintCallable`/`BlueprintType`/`BlueprintSpawnableComponent` 중 하나라도 있는 헤더는
**43개 중 31개**다. 이 기준으로는 나머지가 12개뿐이라 분리 자체가 무의미하다.

게다가 **오분류가 난다**: `ACheckpoint` 는 BP 매크로가 0인데 레벨에 배치된 액터라 ExternalActor 가 물고 있다.
반대로 `UNPCActionComponent` 는 `BlueprintCallable` 이 55개인데 **어떤 에셋도 클래스로 참조하지 않는다**(BP 그래프에서 호출만 됨).

"이름을 바꾸면 에셋이 깨지는가" 라는 진짜 위험과 일치하는 건 §2.1 기준뿐이다.

### 2.3 대상 12개

| 클래스 | 현 위치 | `.cpp` | 참조 에셋 |
|---|---|---|---|
| `AFurnitureActor` | `Furniture/FurnitureActor.h` | O | BP_Bed, BP_Chair, ExternalActor 1 |
| `ADroppedItemBase` | `Inventory/DroppedItemBase.h` | O | BP_DropItem, ExternalActor 1 |
| `ASmartNPC` | `NPC/SmartNPC.h` | O | BP_SmartNPC, ExternalActor 1 |
| `UNPCActionDataAsset` | `NPC/NPCActionDataAsset.h` | **없음(헤더 전용)** | DA_NPC_Actions |
| `UNPCAnimInstance` | `NPC/NPCAnimInstance.h` | O | ABP_SmartNPC |
| `FItemData`(구조체) | `Inventory/ItemDataAsset.h` | **없음(헤더 전용)** | DT_ItemRegistry |
| `AVRPawn` | `Core/VRPawn.h` | O | BP_VRPawn |
| `AKineticProjectile` | `Core/KineticProjectile.h` | O | BP_KineticProjectile |
| `ACheckpoint` | `Core/Checkpoint.h` | O | ExternalActor 1 |
| `UPlayerHUDWidget` | `UI/PlayerHUDWidget.h` | O | WBP_PlayerHUD |
| ~~`NPCInteractionDataAsset`~~ | **Source 에 없음** | — | DA_NPC_Interactions, DA_NPC_action → §5.3 |

실 이동 대상 = **10개 클래스 / 헤더 10 + cpp 8 = 18개 파일**. (2026-08-31 `AVRPlayerCharacter` 제거로 1개 감소 — 이하 수치는 작업 시점에 재측정할 것.)

---

## 3. 설계

### 3.1 왜 폴더 이동이 안전한가 (핵심 전제)

클래스 경로는 `/Script/<모듈명>.<클래스명>` 이다. **폴더 경로가 들어가지 않는다.**
따라서 같은 모듈 안에서 파일을 옮기면 에셋 참조는 그대로다. 깨지는 경로는 둘뿐:

- **모듈명 변경 / 모듈 분리** → `/Script/새모듈.X` 로 바뀌어 전부 끊김
- **클래스명 변경** → `/Script/UE5_MCP_VR.새이름` 으로 바뀌어 끊김

이 프로젝트에는 `CoreRedirects` 설정이 **전무하다**(`Config/*.ini` grep 0건). 즉 끊기면 복구 장치가 없다.

→ **별도 모듈 분리는 채택하지 않는다.** 에셋 190개의 참조를 일괄 파손하고, `CoreRedirects` 를 새로 깔아야 하며,
빌드 의존성도 재설계해야 한다. 얻는 것(폴더 경계)에 비해 비용과 위험이 과하다.

`UE5_MCP_VR.Build.cs` 는 명시적 include 경로를 두지 않고 모듈 루트를 재귀 수집한다 → **무수정**.

### 3.2 목표 레이아웃

```
Source/UE5_MCP_VR/
├── Core/
│   ├── BP/
│   │   ├── VRPawn.h/.cpp
│   │   ├── KineticProjectile.h/.cpp
│   │   └── Checkpoint.h/.cpp
│   └── (직속 9개) CharacterAttributes.h · Entity.h · GameStateData.h · GameplayTagUtils.h
│                 KineticDamage.h/.cpp · PawnDeathUtils.h/.cpp · PlayerGameplayTags.h/.cpp
│                 PlayerInteractionUtils.h/.cpp · VoiceInputComponent.h/.cpp
├── NPC/
│   ├── BP/
│   │   ├── SmartNPC.h/.cpp
│   │   ├── NPCActionDataAsset.h
│   │   └── NPCAnimInstance.h/.cpp
│   ├── (직속) NPCManager · NPCStateComponent · NPCInventoryComponent
│   │          NPCAudioStreamComponent · AnimNotifyState_NPCAttackHit
│   ├── Action/  (이동 없음)
│   └── Struct/  (이동 없음)
├── Inventory/
│   ├── BP/
│   │   ├── DroppedItemBase.h/.cpp
│   │   └── ItemDataAsset.h
│   └── (직속) InventoryComponent · ItemManager
├── Furniture/
│   ├── BP/FurnitureActor.h/.cpp
│   └── (직속) FurnitureManager
├── UI/
│   ├── BP/PlayerHUDWidget.h/.cpp
│   └── (직속) NPCDialogueWidget
├── Network/  (이동 없음)
└── Utils/    (이동 없음)
```

**`BP/` 가 파일 1개뿐인 도메인(Furniture·UI)에도 예외 없이 적용한다.**
예외를 두면 규칙을 기억하지 못해 다음 파일이 아무데나 들어간다. 규칙의 값어치는 예측 가능성에 있다.

### 3.3 `#include` 영향 — 39곳

이동 대상 11개를 include 하는 지점이 **총 39곳**이다. 이 프로젝트는 상대경로 표기(`"../Furniture/FurnitureActor.h"`)를
쓰므로 전부 갱신해야 한다. 밀집도:

`SmartNPC.h` 13 · `FurnitureActor.h` 7 · `VRPawn.h` 3 · `PlayerHUDWidget.h` 3 · `NPCActionDataAsset.h` 3 ·
`KineticProjectile.h` 2 · `DroppedItemBase.h` 2 · `ItemDataAsset.h` 2 ·
`Checkpoint.h` 1 · `NPCAnimInstance.h` 1

재생성 명령(작업 시점 기준으로 다시 뽑을 것 — 숫자가 달라져 있을 수 있다):

```bash
grep -rnE '#include "[^"]*(SmartNPC|FurnitureActor|VRPawn|KineticProjectile|Checkpoint|NPCActionDataAsset|NPCAnimInstance|DroppedItemBase|ItemDataAsset|PlayerHUDWidget)\.h"' Source/
```

`.generated.h` 는 UHT 가 **클래스명**으로 생성·해석하므로 폴더 무관 — 수정 불필요.

---

## 4. CLAUDE.md 배치 규칙 (신설 — 이 작업의 본체)

`## Key Conventions` 에 아래를 추가한다. 문안 확정본:

```markdown
- **BP 진입점 폴더**: 에셋(BP·DataAsset·DataTable·AnimBP·Widget)이 부모나 타입으로 물고 있는 C++ 클래스는
  도메인 폴더 아래 `BP/` 에 둔다(`NPC/BP/SmartNPC.h`). 나머지는 도메인 직속.
  `BP/` 안의 클래스는 **이름을 바꾸지 말 것** — 에셋은 클래스를 `/Script/UE5_MCP_VR.<클래스명>` 으로 물고 있어
  이름이 바뀌면 조용히 끊긴다(경고도 안 뜬다). 불가피하면 `Config/DefaultEngine.ini` 에 `[CoreRedirects]` 를 같이 넣는다.
  같은 이유로 **모듈을 쪼개지 말 것** — 경로의 모듈명이 바뀌어 에셋 참조가 일괄 파손된다.
  새 클래스가 에셋에 노출될지 애매하면 `BP/` 밖에서 시작하고, 실제로 에셋이 물는 순간 옮긴다(폴더 이동은 참조를 깨지 않는다).
```

판정 근거가 필요하면 §2.1 스캔을 돌린다 — 매크로 유무로 판단하지 않는다(§2.2 오분류 사유).

---

## 5. 함께 처리할 문서·데이터 부채

### 5.1 `docs/index.html` §11 디렉토리 구조 — 현행화

지금도 실물과 어긋나 있고, 폴더를 옮기면 더 어긋난다. 확인된 불일치:

| 문서 기재 | 실물 |
|---|---|
| `UI/ChatWidget.h/.cpp` | **존재하지 않음**. 실제는 `NPCDialogueWidget`·`PlayerHUDWidget` |
| `Core/` 4개 | 실제 13개 — `VRPawn`·`KineticDamage`·`KineticProjectile`·`Checkpoint`·`PawnDeathUtils`·`PlayerInteractionUtils`·`VoiceInputComponent`·`GameplayTagUtils`·`PlayerGameplayTags` 누락 |
| `Furniture/` | **폴더 통째 누락** |
| `Inventory/` 3개 | `DroppedItemBase` 누락 |

§11 트리를 §3.2 목표 레이아웃으로 교체하고, `§3 핵심 파일 맵` 의 경로 표기(`NPC/SmartNPC.h/.cpp` → `NPC/BP/SmartNPC.h/.cpp`)도 함께 갱신한다.

### 5.2 `docs/Memo.md`

Todo 에 본 SPEC 항목을 추가하고, 완료 시 Done 으로 이동한다.

### 5.3 죽은 클래스 참조 — `NPCInteractionDataAsset`

`DA_NPC_Interactions`·`DA_NPC_action` 두 DataAsset 이 `/Script/UE5_MCP_VR.NPCInteractionDataAsset` 을 물고 있는데
**Source 에 그런 클래스가 없다**(grep 0건). 현행 클래스는 `UNPCActionDataAsset` 이므로 이름 변경 후 에셋을 정리하지 않은 잔부로 보인다.

**이것이 §4 규칙 부재로 생긴 사고의 실물 사례다.** 처리 절차:

1. 두 에셋이 실제로 쓰이는지 확인 — 어느 BP/코드도 참조하지 않으면 삭제
2. 쓰인다면 부모를 `UNPCActionDataAsset` 으로 재지정하고 값 손실 여부 확인
3. 어느 쪽이든 현행 `DA_NPC_Actions` 와 내용이 중복되는지 먼저 비교할 것

MCP(`ue_search_assets`·`ue_run_python`)로 에디터에서 확인 가능. 판단이 갈리면 사용자에게 물을 것 — 에셋 삭제는 되돌리기 어렵다.

---

## 6. 범위 (변경 파일)

| 대상 | 변경 |
|---|---|
| `Source/UE5_MCP_VR/{Core,NPC,Inventory,Furniture,UI}/BP/` | 신규 폴더 5개 |
| 위 20개 파일 | `git mv` 이동 |
| `#include` 39곳 | 경로 갱신 |
| `CLAUDE.md` | §4 규칙 1개 추가 |
| `docs/index.html` | §11 교체 + §3 경로 갱신 |
| `docs/Memo.md` | Todo→Done |
| `DA_NPC_Interactions`·`DA_NPC_action` | 삭제 또는 부모 재지정 |
| `UE5_MCP_VR.Build.cs` | **무수정**(모듈 루트 재귀 수집) |

---

## 7. 실행 절차

1. **에디터 종료 확인** — Live Coding 이 빌드를 막는다.
   `Get-Process UnrealEditor*` 로 확인. 켜져 있으면 `Unable to build while Live Coding is active` 로 실패한다.
2. **이동 전 기준선 채집** — §2.1 스캔 결과를 파일로 저장(`before.txt`). 4단계 비교에 쓴다.
3. `git mv` 로 이동 — 히스토리 보존. `.h`/`.cpp` 쌍을 **항상 같이**(단 `NPCActionDataAsset`·`ItemDataAsset` 은 헤더 전용).
4. `#include` 39곳 갱신(§3.3 명령으로 재추출) → **빌드**:
   ```
   & "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" UE5_MCP_VREditor Win64 Development -Project="C:\github\UE5_MCP_VR\UE5_MCP_VR.uproject" -WaitMutex
   ```
5. §4·§5 문서·규칙 반영.

---

## 8. 검증

1. **빌드 exit 0** — 헤더 이동은 include 누락이 전부 컴파일 에러로 드러나므로 빌드가 1차 관문.
2. **에셋 무결성 증명** — §2.1 스캔을 다시 돌려 `before.txt` 와 **심볼 집합이 동일**함을 보인다.
   이것이 "폴더 이동은 참조를 깨지 않는다"(§3.1)는 전제의 실증이다. 집합이 달라졌다면 클래스명을 건드린 것이다.
3. **에디터 확인(MCP)** — 에디터를 열고 `ue_search_assets` 로 `BP_SmartNPC`·`BP_VRPawn`·`BP_DropItem`·`WBP_PlayerHUD` 의
   `ParentClass` 가 이동 전과 같은지, `DT_ItemRegistry` 의 RowStruct 가 `ItemData` 로 유지되는지 본다.
4. **pytest** — Python 무관 변경이지만 `test_contract_sync.py` 가 C++ 헤더를 **경로로 열어** 텍스트 파싱한다.
   `NPCActionTypes.h`·`NPCActionKeys.h` 는 이동 대상이 아니라 통과해야 정상이나, 통과 확인으로 파서 경로 가정이 안 깨졌음을 보증한다.
5. **PIE(사용자)** — 레벨 로드 후 NPC·플레이어·가구·픽업이 정상 동작하는지. 클래스 참조가 끊기면 액터가 통째로 사라지므로 육안으로 즉시 드러난다.

---

## 9. 커밋 단위

1. `refactor: BP 진입점을 도메인별 BP/ 로 분리 — 20개 파일 이동·include 39곳`
2. `docs: Source 배치 규칙 명문화 + index.html 디렉토리 구조 현행화`
3. `chore: 죽은 NPCInteractionDataAsset 참조 정리 — DA_NPC_Interactions·DA_NPC_action`

1번은 이동+include 를 한 커밋으로 묶는다 — 쪼개면 중간 커밋이 빌드 불가 상태가 된다.
3번은 에셋 변경이라 판단이 갈릴 수 있으니 분리한다.

---

## 10. Option B (전면 계층화) 핵심 리스크 및 해결 전략

2026-09-03 시뮬레이션 결과, 전면 계층화를 수행할 때 3가지 치명적 리스크가 식별되었으며 다음과 같이 해결합니다:

1. **상대 경로 지옥 (`../../`)으로 인한 유지보수 악화**
   - **해결책**: `UE5_MCP_VR.Build.cs`에 `PublicIncludePaths.Add(ModuleDirectory);`를 추가하고, 프로젝트 전체의 `#include`를 상대 경로 대신 모듈 기준 절대 경로(예: `#include "NPC/Components/NPCStateComponent.h"`)로 일괄 강제합니다.
2. **파이썬 테스트 스크립트 파손**
   - **해결책**: 파일 이동 직후 `test_contract_sync.py` 내에 하드코딩된 C++ 경로 5곳을 신규 경로에 맞게 동시 갱신합니다.
3. **UHT 캐시 찌꺼기로 인한 빌드 고착(Class already defined)**
   - **해결책**: 증분 빌드(`Build.bat`) 대신, `Intermediate/`, `Binaries/` 폴더를 삭제하고 `GenerateProjectFiles.bat`을 실행한 뒤 **Clean Build**를 수행합니다.

---

## 11. 미결

- [x] ~~브랜치명 확정~~ — 별도 브랜치 없이 `feature/player-systems` 에서 진행됨.
- [ ] `DA_NPC_Interactions`·`DA_NPC_action` 처리 방향 — 삭제 vs 부모 재지정 (§5.3, 에디터 확인 후 사용자 결정)
- [x] `VRPlayerCharacter` 제거와의 순서 — 2026-08-31 에 먼저 제거됨. 이동 대상에서 제외.
