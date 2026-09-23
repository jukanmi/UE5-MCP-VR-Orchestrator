# SPEC: jev-daily — Jevlike 상황→일상 활동 매칭

> 전투 편향기 SPEC(`docs/SPEC_jev_neuro_symbolic_st.md`)과 **문서는 분리**, 파이프라인은 **공유**.
> 인터뷰 결정 2026-09-23. 개정 2026-09-23: 단일 선택 → **슬롯별 순차 선택**(하위 파라미터까지 Jev 가 고름). 상태: M1 착수 전.

## 목표

비전투 NPC 가 LLM 지시 없이도 상황에 맞는 일상 활동을 스스로 고르게 한다.
**하위 파라미터(누구에게·어디로·어떻게·무엇을·어떤 표정으로)까지** jevlike Choice(5~20ms 로컬 추론)로 고르고, LLM 은 거치지 않는다.

**현재 문제(2026-09-23 확인)**: 비전투에서 큐가 비면 NPC 는 Idle 로 멈춰 있다
(`STTask_PrepareNextAction.cpp:78-87`, 2026-05-16 결정). 일상 활동은 LLM 이 명시했을 때만 나온다.
LLM 학습 로그(`app/data/train_logs`, 336건)에 일상 액션은 3건(Sing 2, Sleep 1)뿐이다.
LLM 판단을 따라 배우는 증류 방식으로는 학습 데이터를 만들 수 없다.

**2026-05-16 결정 번복**: 비전투 자율 주입을 허용한다. 단 Jev 활동은 **최하위 우선순위**다(§D2).

## 범위 (변경 파일·시스템)

| 층 | 파일 | 변경 |
|---|---|---|
| C++ | `NPC/Action/STTask_PrepareNextAction.cpp` | 비전투·큐 빔 분기에서 Idle 경과를 재고, 임계에 도달하면 컨트롤러에 daily 요청 |
| C++ | `NPC/Action/SmartNPCAIController.{h,cpp}` | `RequestJevDecision` 을 도메인 인자화(combat/daily). in-flight·세대·워치독 공유. 응답을 domain 으로 분기 |
| C++ | `NPC/Action/NPCActionComponent.{h,cpp}` | 실행 가능 활동 목록 + 공통 후보 풀 산출, 응답(활동+슬롯) → `FGameAction` 조립·큐 주입, Jev 출처 표시·선점 |
| C++ | `Network/EnvelopeBuilder.{h,cpp}` | `BuildJevQuery` 가 domain 필드 포함 |
| C++ | `NPC/Subsystems/NPCManager.{h,cpp}` | `SendPlayerDialogue` 인라인 주변 수집(`:272-383`)을 `CollectNearbyContext` 로 추출(§D8). LLM·Jev 가 같은 함수 사용 |
| C++ | `NPC/Components/NPCInventoryComponent.cpp` | `GetInventoryJson` 에 `category`(EItemType) 필드 추가(가산 변경) |
| C++ | `Inventory/Subsystems/ItemManager.{h,cpp}` | `FindDroppedItem(InstanceID)` 조회 추가(§D9) |
| C++ | `NPC/Action/STTask_ExecuteSmartAction.cpp` | `ResolveActionTarget` 에 바닥 아이템 instance id 분기 추가(§D9) |
| C++ | `NPC/Action/NPCActionComponent.{h,cpp}` | `ExecutePickUp` 을 대상 아이템 기반(이동+줍기 내장)으로 전환(§D9) |
| Python | `app/schemas/actions.py` | `ACTION_REQUIRED_PARAMS["PickUp"]` 을 `("target_id", "target_loc")` 로(§D9) |
| Python | `app/schemas/envelope.py` | `JevQueryPayload` 에 `domain`·`activities`·`pools` 추가(기본 `combat` — 하위호환) |
| Python | `app/services/jev_service.py` | `evaluate_daily(metrics, activities, pools, persona)` — 활동 1패스 + 슬롯별 순차 패스. 모델은 전투와 같은 체크포인트 |
| Python | `app/main.py::_handle_jev_query` | domain 분기, persona(`load_persona`) role·traits 로 context 보강 |
| Python | `tests/test_jevlike_service.py` | daily 케이스 추가 |
| 에셋 | 없음(규약만) | ST 에셋·새 EAction·새 envelope 타입 **0**. `DA_NPC_Actions` 미디어 키 명명 규약만 추가(§D4) |

## 결정 사항

**D1. 개입 시점: Idle N초 후 1회**
- 조건(모두 충족): BehaviorMode ≠ Combat · 큐 빔 · `!bIsBusy` · Idle 경과 ≥ `JevDailyIdleSeconds`(기본 10s, EditDefaultsOnly)
  · 최근 LLM 배치 수신 후 ≥ `JevDailyAfterLLMSeconds`(기본 15s, 대화 직후 끼어들기 방지) · 서버 연결됨 · Jev in-flight 없음.
- 요청 1회 후 결과 활동이 끝나면 Idle 경과를 0부터 다시 잰다. 자연 페이싱이라 별도 쿨다운은 두지 않는다.
- Sit/Sleep 은 지속 자세라 액션이 끝나도 자세가 유지된다. 이때도 N초 후 재요청하고, 활동 목록에 `stay`·`stand_up` 을 넣는다.

**D2. 우선순위: Jev 활동은 최하위, 다른 출처 액션이 오면 즉시 중단**
- 현재 LLM 배치는 현재 행동을 끊지 않고 큐 뒤에 선다(`NPCActionComponent.cpp:396-430`).
  그대로 두면 Dance 몽타주가 끝날 때까지 LLM 응답이 밀린다.
- 규칙: 현재 액션이 Jev 출처인데 LLM 배치·척수반사·전투 셀렉터가 액션을 넣으면 → `AbortCurrentAction` 후 새 액션 실행.
- 자세(bIsSit/bIsLie)는 선점으로 자동 해제하지 않는다. 기존대로 LLM 이 StandUp 을 고른다.
- 선점은 큐 소유자인 `NPCActionComponent` 의 공통 진입부 **한 곳**에서 처리한다. 출처별로 따로 넣지 않는다.

**D3. 입력: 기존 데이터만. 신규 시스템 0**
C++ 가 정규화해서 보내는 `metrics`(좌표·절대값 금지 — 전투와 같은 규약):

| 키 | 타입 | 출처 |
|---|---|---|
| `posture` | `stand`/`sit`/`lie` | `NPCStateComponent` bIsSit/bIsLie |
| `posture_s` | float(0~600 clamp) | 자세 진입 후 경과 |
| `idle_s` | float | Idle 경과 |
| `player_dist_m` | float, 미인지 시 -1 | 시야 퍼셉션 |
| `player_relation` | `friendly`/`neutral`/`hostile` | `GetRelation` |
| `npc_near` | int | 반경 10m 에서 인지 중인 비적대 NPC 수 |
| `last_activity` | 활동 id 또는 `""` | 직전 Jev 활동(반복 억제) |
| `goal` | str ≤40자 | `FNPCPlan.Goal`(LLM Stage2), 없으면 `""` |

Python 이 `load_persona(npc_id)` 의 `role`·`traits` 를 context 문자열에 덧붙인다. C++ 는 persona 를 모른다.

**D4. 선택 구조: 활동 1패스 + 활동별 슬롯 순차 패스**

펼친 조합은 활동 10 × 대상 12 × 방식 4 × 표정 5 ≈ 2,400개라 한 목록으로 다룰 수 없다. 그래서 슬롯마다 따로 고른다.
- 1패스에서 활동을 고른다. 이어서 아래 표의 슬롯 순서대로 한 패스씩 고른다. 패스 k 의 context 에는 앞 패스들의 선택을 붙인다(`chosen: activity=emote, target=Elara`).
- 모든 패스는 Python 이 **요청 한 번 안에서** 처리한다. 왕복·워치독은 1회다.
- 후보가 1개뿐인 슬롯(기본값만 남은 경우 포함)은 추론하지 않고 그대로 채택한다. 패스 수 = 1 + 후보 ≥2 인 슬롯 수, **최대 4**.
- 모든 슬롯에는 `default` 후보가 항상 있다. 뜻은 "C++ 기존 결정 규칙에 위임"이다(대상=최근접, 방식=Walk 또는 기본 미디어 키, 표정=활동 프리셋). 맞는 후보가 없을 때 억지로 고르지 않게 하는 탈출구다.

슬롯 5종과 후보 출처:

| 슬롯 | `FGameAction` 매핑 | 후보 출처 |
|---|---|---|
| `target` | `Key_TargetID` | 풀 `actors`(인지 중 인물) ∪ `places`(가구) ∪ `pois` ∪ `ground_items` + 특수 후보 `around`(look 전용)·`none`(emote 무대상)·`ground`(give 전용 = 버리기) |
| `dest` | `Key_TargetLoc`(C++ 가 좌표·NavMesh 투영 해석). **`wander`·`patrol` 전용** — 대상 기반 액션은 `target` 을 쓴다(§D9) | 풀 `pois` ∪ `places` ∪ `actors`(그 근처로) ∪ `random`(반경 8m 랜덤 NavMesh 지점) |
| `style` | `Key_Style` | 이동: `Walk`/`Run`/`Crouch`(`Sprint` 는 일상에서 제외) · 표현: `DA_NPC_Actions` 의 표현 계열 키 전부(`Emote*`·`Pray*`·`Dance*`·`Sing*`) |
| `item` | `Key_Item` | 풀 `items`(NPC 인벤, `category` 로 활동별 필터) ∪ `equipped`(장착 중, equip 전용) |
| `facial` | `FacialState` | `Neutral`/`Happy`/`Sad`/`Tired`/`Surprised` (Angry·Fear·Disgusted·Pain 은 전투·부상 전용이라 제외) |

활동 표(2026-09-24 통합 개정 — 대분류 폐지, 비슷한 EAction 병합). 실행 조건은 C++ 가 매 요청마다 판정해 가능한 것만 `activities` 로 보낸다.
**daily 열**: ✅ = daily 트리거에서도 후보, ❌ = command 등 다른 트리거 전용. 활동 목록은 하나이고 트리거별 차이는 이 열로만 둔다.

| 활동 | 조립되는 EAction | daily | 조건 | 슬롯 순서 |
|---|---|---|---|---|
| `stay` | (없음). Wait 는 duration 구현 후 여기로 흡수 | ✅ | 항상 | — |
| `stand_up` | StandUp | ✅ | sit ∨ lie | — |
| `look_at` | TurnTo, 대상이 `around` 면 Scan | ✅ | stand ∨ sit | target(actors ∪ places ∪ pois ∪ `around`) → facial |
| `wander` | Move, 목적지가 자극 지점이면 Investigate | ✅ | stand | dest → style(이동) |
| `patrol` | Scout(현재 위치 → dest) | ✅ 경비 역할 | stand ∧ pois ≥1 | dest(pois) |
| `follow` | Track(지속 추적) — Follow 흡수, 종료 조건 §D10 | ❌ | stand ∧ actors ≥1 | target(actors) → style(이동) |
| `rest` | 가구 타입으로 결정: Seat → Sit, Bed → Sleep (낮에도 잠) | ✅ | stand ∧ 빈 가구 ≥1 | target(places: 빈 가구) — 이동은 C++ 내장 |
| `emote` | 고른 미디어 키 접두사로 결정: `Emote*`→Emote, `Pray*`→Pray, `Dance*`→Dance, `Sing*`→Sing | ✅ | stand (Emote 계열은 sit 도 가능) | style(표현 키) → target(actors ∪ `none`) → facial |
| `use_item` | UseItem | ✅ | Consumable ≥1 | item(Consumable) |
| `equip` | 장착 중이면 Unequip, 아니면 Equip. HandObject 흡수(코드상 `EquipItem` 과 같음) | ❌ | Equipment ∪ equipped ≥1 | item(Equipment ∪ equipped) |
| `give_item` | GiveItem, 대상이 `ground` 면 Drop | ✅ 단 NPC 대상만 (`Player`·`ground` 는 command 전용) | stand ∧ items ≥1 | target(actors ∪ `ground`) → item(Quest·장착 제외) |
| `pick_up` | PickUp | ✅ | stand ∧ ground_items ≥1 | target(ground_items) — 이동은 C++ 내장(§D9) |

- Jev 밖에 남는 것: **Dialogue·Trade**(LLM), **Stop**(즉시 제어 신호 — 선점 경로), **전투 5종**(C++ 척수, 비고 B.1), **Craft·Read**(보류), **Comfort**(제거 — EAction 삭제 4곳 체크리스트 대상).
- **통합은 활동 표에서만 한다.** C++ 조립기가 활동+슬롯을 기존 EAction 으로 풀어 쓰므로 EAction enum·LLM 스키마는 이번에 건드리지 않는다. enum 정리는 LLM 이 액션을 내지 않게 된 뒤(비고 B.5 command 전환) 따로 한다.
- **POI 목업**: POI 시스템은 추후 만든다. 그 전까지 레벨에 `POI_<이름>` 태그를 단 액터(TargetPoint)를 풀 `pois` 의 소스로 쓴다(`desc` = `poi|<이름>|dist_m`). 실제 POI 시스템이 들어오면 소스만 바꾸고 계약(`pois` 풀)은 유지한다. 이 목업으로 `wander`(POI 목적지)·`patrol`·`look_at`(POI 바라보기)·Investigate(자극 지점)가 동작한다.
- `Read` 제외: 2026-09-23 기준 `DA_NPC_Actions` 에 Read 몽타주가 없다. 몽타주가 들어오면 `emote` 의 style 후보(`Read*`)로 흡수한다.
- **미디어 키 명명 규약**: `<계열>` 또는 `<계열>_<변형>`(예: `Dance`, `Dance_Slow`, `Emote_Wave`). C++ 는 표현 계열 접두사가 일치하는 키를 모아 `emote` 의 style 후보로 쓴다.
  몽타주를 추가하면 코드 변경 없이 선택지가 늘어난다. 선택지 확장은 이 규약으로 한다. 현재 계열당 키 1개라도 style 슬롯은 만들어 둔다(4계열이라 이미 후보 4개).
- **style `default` 의 실제 값**: Dance/Sing/Emote 는 style 이 비면 `BasePlayActionMedia("")` 가 실패해 몽타주 없이 즉시 완료된다(`NPCActionComponent.cpp:2306-2308`).
  그래서 `default` 는 빈 값이 아니라 **계열 기본 키**로 조립한다.
- **Pray 는 키 고정**: `BasePlayActionMedia(TEXT("Pray"))`(`:2304`)라 style 을 무시한다. M1 에서 Pray 도 style 경로로 바꾼다(빈 값이면 `Pray`). 그래야 `emote` 가 `Pray_*` 변형을 고를 수 있다.
- `give_item` 의 daily 제한: 플레이어 대상은 경제·퀘스트 흐름이 깨지고, `ground`(버리기)는 자율로 하면 인벤토리가 줄기만 한다. 둘 다 command 전용이다.

**D10. 지속 추적(`follow`) 종료 조건과 대화 시 바라보기 — C++**
- 현재 코드(2026-09-24 확인): `Follow` 는 대상 뒤 300cm 로 **한 번 이동하고 끝난다**(`NPCActionComponent.cpp:1537-1544`). 계속 따라다니는 쪽은 `Track` 이다. Track 은 0.5s 마다 `MoveToActor` 를 재발행하고(`:2193-2241`), 대상 소멸·`AbortCurrentAction` 때만 멈춘다.
  - **결함**: Track 은 이동 콜백을 걸지 않아 액션은 즉시 완료되고 추적 타이머만 뒤에서 돈다. 다음 Move 를 넣어도 0.5s 뒤 추적이 목적지를 덮어쓴다.
- `follow` 는 Track 의 지속 추적을 쓰고, 종료 조건을 C++ 에 넣는다(Jev 재평가 없음):
  1. 추적과 충돌하는 액션이 시작될 때: 다른 대상 공격, 다른 곳으로 이동(Move·PickUp·rest 등 이동을 동반하는 액션), 다른 대상 follow → 그 액션 실행 진입부에서 `StopTracking`.
  2. 피격 시(`ApplyDamage`).
  3. 대상 소멸(기존).
  4. Stop 명령.
  - 대화 수신은 종료 조건이 **아니다**. 따라가면서 말할 수 있다.
- **대화 시 바라보기**: 비전투에서 NPC 가 대사를 할 때 말하는 상대를 자동으로 바라보게 한다(`BaseDialogue` 에 현재 바라보기 없음). Jev 를 거치지 않는 C++ 기본 동작이다. `look_at` 은 선택지로도 계속 남는다.

**D5. 통합·계약: envelope·서비스·체크포인트 공유, 결과는 큐 주입**
- 같은 `jev_query`/`jev_decision` 에 `domain: "combat" | "daily"` 를 넣는다. 누락 시 `combat`(현 C++ 코드·테스트 무변경 호환).
- 후보는 활동마다 따로 보내지 않고 **공통 풀로 한 번만** 보낸다. 풀마다 C++ 가 최대 12개로 줄인다(거리순·인지 중 우선).
  후보 설명은 좌표가 아닌 의미 특징만 담는다. Jev 는 산술을 못 한다.

daily 요청 payload:
```json
{
  "npc_id": "Moca", "generation": 42, "domain": "daily",
  "metrics": { "posture": "stand", "posture_s": 0, "idle_s": 11.2, "player_dist_m": 4.1,
               "player_relation": "friendly", "npc_near": 2, "last_activity": "wander", "goal": "" },
  "activities": ["stay", "look_at", "wander", "patrol", "rest", "emote", "use_item"],
  "pools": {
    "actors": [ { "id": "Player", "desc": "player|friendly|4m|idle" },
                { "id": "Elara",  "desc": "npc|friendly|3m|talking" } ],
    "places": [ { "id": "Bench_03", "desc": "seat|vacant|5m|near_player" },
                { "id": "Bed_01",   "desc": "bed|occupied|14m" } ],
    "pois":   [ { "id": "POI_Gate", "desc": "poi|Gate|12m" } ],
    "items":  [ { "id": "Apple", "desc": "consumable|x3" } ],
    "ground_items": [ { "id": "Item_7F3A", "desc": "Herb|2m" } ],
    "media":  ["Emote", "Pray", "Dance", "Sing"]
  }
}
```

daily 응답 payload:
```json
{ "npc_id": "Moca", "generation": 42, "domain": "daily",
  "activity": "emote", "slots": { "target": "Elara", "style": "Emote", "facial": "Happy" },
  "confidence": 0.41, "passes": 3 }
```

- `slots` 에는 선택된 활동의 슬롯만 담는다. `default` 는 값 `"default"` 로 명시한다.
- 컨트롤러 in-flight·세대·0.3s 워치독·서버 연결 체크는 두 도메인이 **하나를 공유**한다. 전투 요청이 오면 세대가 올라가 daily 늦은 응답은 자동 폐기된다.
- 결과 소비: daily 는 `FJevDecision` 캐시를 쓰지 않는다. 받은 즉시 컴포넌트가 `FGameAction` 을 조립해 주입한다. ST 에셋 변경은 불필요하다.
- 폴백 단위:
  - 타임아웃·세대 불일치·예외·활동 목록 밖 id → 활동 `stay`(= 현행 Idle).
  - 슬롯 값이 후보 밖이거나 조립 시점에 무효(대상 사라짐·의자 점유됨) → **그 슬롯만** `default`.
  - 전체를 버리지 않는다. 장애가 나도 지금보다 나빠지지 않는다.

**D8. 후보 풀은 LLM 송신과 같은 C++ 수집 함수로 만든다**

현재(2026-09-23) 주변 사물 인식은 **`UNPCManager::SendPlayerDialogue` 안에 인라인**으로만 있다(`NPCManager.cpp:272-383`).
Jev 가 따로 수집 코드를 쓰면 LLM 이 보는 세계와 Jev 가 보는 세계가 어긋난다. 예: LLM 에는 보이는 의자가 Jev 후보에는 없음.
그래서 이 블록을 함수 하나로 추출하고, 두 송신 경로가 같은 함수를 쓴다.

기존 인라인 수집 내역(추출 대상):

| 수집물 | 현재 코드 | 범위·필터 | 현재 JSON 키 |
|---|---|---|---|
| NPC 인벤토리 | `UNPCInventoryComponent::GetInventoryJson()`(`:274-285`) | 슬롯 전부. 필드 `id`·`name`·`desc`·`count`·`weight`·`type`(**항상 `"item"` — EItemType 아님**) | `npc_inventory` |
| 주변 가구 | `UFurnitureManager::GetActiveFurniture()` 순회(`:306-349`) | 2D 거리 ≤ `FurnitureContextRange`(1500cm). 필드 `id`·`type`(Seat/Bed)·`occupied`·`dist_m`. 빈 가구만 `valid_targets` 에 합류 | `nearby_furniture` |
| 바닥 아이템 | `UItemManager::GetItemsInRange(NpcLoc, FurnitureContextRange)`(`:352-381`) | 1500cm(주석은 "5m"라 적혀 있으나 실제 15m — 주석 오류). 필드 `id`(instance)·`template_id`·`dist_m` | `nearby_items` |
| 타깃 어휘 | `ActiveNPCs` 전부 + 센티넬 3종 + 빈 가구 + 바닥 아이템(`:293-300`) | **거리·인지 필터 없음** — 등록 NPC 전원 | `valid_targets` |

추출 결정:
- `UNPCManager::CollectNearbyContext(const ASmartNPC* NPC) const → FNPCNearbyContext` 를 신설한다(구조체: `Furniture[]`·`GroundItems[]`·`Inventory[]`·`PerceivedActors[]`).
- `SendPlayerDialogue` 는 이 구조체를 **기존과 같은 JSON 키·값**으로 직렬화한다. LLM 페이로드는 아래 가산 변경 1건 외에 바뀌지 않는다.
- Jev daily 는 같은 구조체에서 풀을 만든다. `places` ← `Furniture`, `items` ← `Inventory`, `actors` ← `PerceivedActors`.
  `desc` 는 `type|vacant/occupied|dist_m|near_player` 형식으로 조립한다.
- `PerceivedActors` 는 신규 수집이다. 소스는 시야 퍼셉션 `GetCurrentlyPerceivedActors(UAISense_Sight)`로, 전투 Jev(`SmartNPCAIController.cpp:553`)와 같은 소스다. 필드는 `id`(`PerceptionIdFor`)·관계·`dist_m`·플레이어 여부.
  LLM 의 `valid_targets` 는 지금처럼 전원 목록을 유지한다(동작 변경은 범위 밖 — 미결 참조).
- 인벤토리에 `category`(EItemType: `General`/`Consumable`/`Equipment`/`Quest`)를 추가한다. 없으면 `use_item` 후보를 소비템으로, `give_item` 후보를 퀘스트템 제외로 거를 수 없다.
  `GetInventoryJson` 에도 같은 필드를 가산한다(Python 은 모르는 키를 무시하므로 LLM 경로 무해).
- `use_item` 풀 = `category==Consumable`. `give_item` 풀 = `category!=Quest` ∧ 슬롯 보유분(장착 제외 — `ExecuteGiveItem` 이 슬롯 기준으로 검사).
- 반경은 `FurnitureContextRange`(1500cm) 하나를 LLM·Jev 가 공유한다. 별도 `JevDailyFurnitureRadius` 는 두지 않는다.
- 바닥 아이템(`GroundItems`) → 풀 `ground_items`(`desc` = `template_id|dist_m`). `pick_up` 활동이 쓴다(§D9).

**D9. 대상 기반 액션은 C++ 가 이동까지 내장한다: Jev 는 "무엇을"만 고른다 (PickUp 전환)**

원칙: 대상을 받으면 C++ 가 거기까지 걷고 수행까지 한다. Sit/Sleep 이 이미 이 방식이다(가구 타겟 → `BaseMove` → 도착 후 자세, `NPCActionComponent.cpp:2273-2292`).
좌표만 받는 PickUp 은 예외이고, 이 예외 때문에 Jev 가 `dest` 슬롯까지 골라야 했다. PickUp 을 같은 방식으로 바꾸면 슬롯이 1개 준다.

현재 PickUp 의 결함(2026-09-23 확인):
1. `ExecutePickUp(FVector Location)`(`:2094`)은 좌표만 받는다. Python 은 좌표를 보내지 않는다(`:811`). LLM 의 `loc` 은 위치 id 문자열이라 파싱이 실패해 (0,0,0)이 된다 → **LLM PickUp 은 월드 원점으로 걸어간다**.
2. `valid_targets` 는 바닥 아이템 instance id 를 이미 싣는다(`NPCManager.cpp:373`). 그런데 `ResolveActionTarget`(`STTask_ExecuteSmartAction.cpp:21-51`)은 NPC·가구만 해석한다. 아이템 id 는 미해석 폴백으로 `BBTarget`(전투 대상)이 된다.
3. 도착 후 `PerformPickupAtDestination`(`:2102`)은 반경 100cm 에서 **아무 아이템이나** 첫 번째 것을 줍는다. 지정한 아이템이라는 보장이 없다.
4. `UItemManager` 는 instance id 키 맵(`ActiveDroppedItems`)을 갖고 있지만 조회 함수가 없다.

변경:
- `UItemManager::FindDroppedItem(const FString& InstanceID) const → ADroppedItemBase*` 를 추가한다(맵 조회).
- `ResolveActionTarget` 에 가구 다음 분기로 `FindDroppedItem(Keyword)` 를 넣는다. 우선순위는 NPC > 가구 > 아이템.
- `ExecutePickUp(AActor* TargetItem, FVector Location)`:
  - `TargetItem` 이 `ADroppedItemBase` 면 → 약참조 `PendingPickupItem` 에 저장 → 그 위치로 `BaseMove(Walk)` → 도착 후 **그 아이템만** `TryPickupInto`.
  - `TargetItem` 이 없고 `Location` 만 있으면 → 기존 동작(반경 100cm 첫 아이템)을 유지한다. 반사·레거시 호환용이다.
- 도착 시 검증: 아이템이 파괴됨(`!IsValid`) · 다른 이가 쥠(오버랩 꺼짐) · 도착 지점에서 150cm 넘게 떨어짐(누가 옮김) → 줍지 않고 완료 처리한다. 경고 로그 1줄만 남기고 추격하지 않는다.
- Python `ACTION_REQUIRED_PARAMS["PickUp"]` = `[("target_id", "target_loc")]`. LLM 은 `target` 에 `valid_targets` 의 아이템 id 를 넣으면 된다.

Jev 효과:
- daily 활동 `pick_up` 을 추가한다. 슬롯은 `target(ground_items)` **1개**뿐이다(dest·style 없음).
- 풀에 `ground_items` 를 추가한다(`CollectNearbyContext.GroundItems`, §D8). 바닥 아이템이 1개면 슬롯 패스를 건너뛰어 총 1패스다.
- 같은 원칙은 이미 적용된 곳이 있다: Sit/Sleep(가구)·Follow/Track(액터)·GiveItem(액터 — 제자리 전달). 새 대상 기반 액션을 추가할 때도 이 원칙을 기본으로 한다.

**D6. 선택 방식: 패스마다 분포 샘플링, argmax 아님**
- argmax 면 같은 상황에서 매번 같은 조합을 고른다. 패스마다 분포 샘플링(Python 측, 온도 `JEV_DAILY_TEMPERATURE` 기본 1.0)으로 다양성을 준다.
- 테스트는 시드를 고정해 결정론으로 검증한다.
- 전투 쪽 confidence<0.5 게이트는 daily 에 적용하지 않는다. 후보가 많아 최댓값이 0.5 를 넘기 어렵고, 잘못 골랐을 때의 비용도 낮다. `confidence` 는 1패스(활동) 확률로, 로그·튜닝용이다.
- 감수할 점: 1패스에서 활동을 잘못 고르면 뒤 패스가 그 오류를 이어받는다. `default` 후보로 뒤 패스의 억지 선택만 막는다.

**D7. 학습: M1 휴리스틱 → M2 합성 데이터·학습**
- M1 휴리스틱(패스별 규칙 → logits → softmax):
  - 활동: 플레이어 5m 이내면 rest(침대)·emote(Dance)↓ look_at·emote(Emote)·stay↑ / sit 상태에서 posture_s>120 이면 stand_up↑ /
    traits `Disciplined`·`Cautious`·`Observant` 면 look_at·wander·patrol↑ emote(Dance)↓ / `Gentle`·`Guilt-ridden` 이면 emote(Pray)↑ /
    `last_activity` 반복 페널티.
  - target·dest: friendly↑ hostile↓(daily 후보에서 hostile 은 사실상 배제) · 가까울수록↑ · `near_player` 가구는 사교형 traits 일 때↑.
  - style(이동): Walk 우세, Crouch 는 `Cautious` 일 때만 소폭↑, Run 은 낮게.
  - facial: traits·posture 기반. 예: `Gentle`→Happy, `Guilt-ridden`→Sad, posture_s 가 길면 Tired.
  - item: `desc` 범주 키워드(food 등) + 수량 많을수록↑.
- M2: LLM 으로 (context, 활동 목록, 풀) → **정답 조합 전체(활동+슬롯)** 합성 JSONL 을 생성한다. 이걸 패스 단위 `ChoiceExample` 로 쪼갠다(조합 1건 → 예시 1~4건, 앞 선택이 context 에 누적).
  형식은 전투 SPEC §4.4 를 재사용하고 context 앞에 `[daily]`/`[combat]` 접두사를 붙인다.
  **전투와 daily 를 합쳐 체크포인트 1개**(`app/models/jevlike_tactics.pt`)로 학습한다. 도메인별·슬롯별 모델은 만들지 않는다.

## 완료 기준

**M1**
1. `pytest tests` 전부 통과. daily 신규 ≥10건:
   - 반환 활동 ∈ `activities`, 반환 슬롯 값 ∈ 해당 슬롯 후보 ∪ {`default`}
   - 활동별 슬롯 순서·구성이 D4 표와 일치(emote 3슬롯, stay 0슬롯)
   - 후보 1개 슬롯은 추론을 건너뜀(`passes` 로 확인)
   - 풀이 비면 해당 슬롯은 `default`
   - `stay` 항상 포함
   - domain 누락 시 combat 경로
   - 시드 고정 시 조합 전체가 재현됨
   - 예외 시 `stay`
   - 반복 페널티 동작
   - `give_item` 대상에 Player 가 나오지 않음
   - `pick_up` 은 슬롯 1개(target ∈ ground_items), ground_items 1개면 총 1패스
   - (Python) `ACTION_REQUIRED_PARAMS["PickUp"]` 이 target_id 만 있는 액션을 통과시킴
2. `evaluate_daily` 최대 4패스 휴리스틱 100회 평균 < 2ms.
3. `sol_pi.py build` 에러 0.
4. PIE(헤드셋 불필요, MCP): 비전투 NPC 가 LLM 입력 없이 10~25s 안에 Idle 이 아닌 활동 1개를 시작한다. 로그 `[Jev] <id> daily → <activity> <slots> passes=<n>` 로 확인.
5. PIE: 조립된 `FGameAction` 의 `Key_TargetID`/`Key_TargetLoc`/`Key_Style`/`Key_Item`/`FacialState` 가 응답 슬롯과 일치한다. 무효 슬롯은 `default` 로 치환된다(로그).
6. PIE: Jev 활동(dance) 중 플레이어가 말을 걸면 LLM 배치 도착 즉시 몽타주가 끊기고 LLM 액션이 실행된다.
7. PIE: Python 서버를 끄면 NPC 는 Idle 을 유지한다(크래시·경고 스팸 없음).
8. 전투 Jev 회귀 없음: 기존 테스트 8건 통과, 전투 중 daily 요청 0건(로그).
9a. PIE: LLM 이 `PickUp target=<아이템 instance id>` 를 내면 NPC 가 그 아이템까지 걸어가 **그 아이템만** 줍는다. 중간에 플레이어가 먼저 집으면 줍지 않고 완료된다(경고 1줄). Jev `pick_up` 도 같은 경로.
9. `CollectNearbyContext` 추출 회귀 없음: 같은 상황에서 `SendPlayerDialogue` 페이로드가 추출 전과 키·값이 동일하다(가산 필드 `category` 제외). 로그 JSON diff 로 확인.
10. 같은 틱에 LLM `nearby_furniture` 에 있는 빈 가구 id 집합과 Jev `places` 의 vacant id 집합이 일치한다(상한 12 이내일 때).

**M2**
11. 합성 조합 daily ≥ 2,000건(패스 예시 ≈ 5,000건) + combat ≥ 2,000건 생성. 체크포인트 로드 로그 `[Jev] jevlike 로드 완료`.
12. 홀드아웃 세트에서 패스별 top-1 이 합성 정답과 일치하는 비율 ≥ 휴리스틱 일치율 + 10%p(활동 패스·슬롯 패스 각각 측정).
13. 모델 경로 최대 4패스 지연 < 100ms(0.3s 워치독 대비 여유 3배).

## 단계

- **M1** — Python(스키마·서비스·핸들러·테스트) → C++(도메인 인자화·활동 목록·후보 풀 산출·조립·주입·선점) → 빌드 → MCP PIE 로 기준 4~10 검증. C++ 는 `CollectNearbyContext` 추출(기준 9)을 가장 먼저 한다 — Jev 풀이 여기에 의존한다.
  Python 을 먼저 하는 이유: 서버 단독 테스트로 계약(payload 형태)을 먼저 고정하면 C++ 가 그 계약에 맞춰 붙는다.
- **M2** — jevlike 설치 → 합성 데이터 생성 스크립트 → 학습 → 체크포인트 교체 → 기준 11~13.
  Memo Todo 의 "jevlike 설치·체크포인트 학습" 항목과 합친다.

## 미결 사항

- 전투 SPEC Todo "ST 에셋 바인딩": daily 가 큐 주입으로 결정돼 ST 노드와 무관해졌다. 전투 쪽 `FSTEvaluator_JevTactics`·`FSTCondition_NoulGuard`
  바인딩은 여전히 남는다. 다만 전투 승수는 이미 `SelectCombatAction` 이 컨트롤러 캐시를 직접 읽는다. ST 노드가 실제로 막는 상태가 있는지부터 따로 판단해야 한다.
- 대화 중 판정: `JevDailyAfterLLMSeconds` 만으로 부족하면(플레이어가 말을 거는 중 LLM 응답 전 공백) "플레이어 발화 수신 시각"을 추가 게이트로 쓴다. PIE 결과 보고 결정.
- `give_item`·`use_item` 의 퀘스트 아이템 보호: 인벤토리에 퀘스트 태그가 있으면 풀에서 제외. 태그 유무를 M1 착수 시 확인.
- 슬롯 추가 후보(지속 시간·대사 주제)는 이번 범위 밖. 추가하려면 D4 표에 슬롯 한 줄 + 휴리스틱 규칙 + 테스트만 늘리면 된다.
- `dest=random` 반경, 풀 최대 12개는 PIE 튜닝 대상. 가구·아이템 반경은 `FurnitureContextRange` 공유(§D8).
- LLM `valid_targets` 가 거리·인지와 무관하게 등록 NPC 전원을 노출한다. 멀리 있는 NPC 에게 GiveItem 하는 식의 오지정 여지가 있다. `PerceivedActors` 로 좁힐지는 LLM 동작 변경이라 별도 결정.
- `NPCManager.cpp:352` 주석 "반경 5m" ↔ 실제 `FurnitureContextRange` 1500cm 불일치. 추출 시 주석 정정.
- Read 몽타주 추가 시 활동 복귀.
- **D9 구현 완료(2026-09-24, `f59fe0c8`)** — Simulate PIE 검증: 지정 아이템만 습득(반경 100cm 안 다른 아이템 무시) ✅, 걷는 중 150cm 밖으로 옮기면 경고 후 생략 ✅. 검증 중 발견한 기존 결함 3건(미수정):
  1. **중복 액션 스킵이 Jev 연속 선택을 삼킨다**: `DispatchActions`(`NPCActionComponent.cpp:417-423`)가 직전 큐잉 타입과 같으면 버린다. PickUp 연속 2회 중 2번째가 무음 드랍됨을 실측. Jev daily 가 같은 활동(다른 대상)을 연달아 고르면 같은 현상 → M1 주입 경로는 이 필터를 우회하거나 (타입+대상) 기준으로 바꿔야 한다.
  2. ~~**`AM_Pickup` 9.57초 vs 액션 워치독 15초**~~ — 해결(2026-09-24): 몽타주 재생 시 워치독을 `길이+2s` 로 연장(줄이지 않음). 10m 보행 후 15.3s 완료 실측.
     원 기록: 5.4초 넘게 걸으면 몽타주가 끝나기 전에 워치독이 강제 완료. D9 로 PickUp 이 최대 15m 를 걷게 돼 더 자주 걸린다. 몽타주를 짧게 하거나, 도착 시 워치독을 몽타주 길이 기준으로 재시작해야 한다.
  3. ~~**습득 실패해도 `AM_Pickup` 재생**~~ — 해결(2026-09-24): `PerformPickupAtDestination` 이 bool 반환, 주웠을 때만 재생. 실측 확인.
     원 기록: 아이템이 없어도 허리를 숙인다(좌표 경로도 동일). `PerformPickupAtDestination` 이 성공 여부를 돌려주고 성공 시에만 재생하면 된다.
  - 참고: 아이템이 월드 밖으로 떨어지면(z 수천 cm 아래) NavMesh 투영이 실패해 이동이 끝나지 않고 워치독 15초까지 정지한다(테스트 중 순간이동으로 재현).

---

## 부록 A. 전체 EAction 파라미터 선택지 카탈로그 (2026-09-23 코드 기준)

근거: `NPCActionComponent::ExecuteInteraction`(`NPCActionComponent.cpp:785-957`)이 읽는 키, 각 `Execute*` 본문, Python `app/schemas/actions.py`(`ACTION_REQUIRED_PARAMS`), `DA_NPC_Actions` 몽타주 보유 여부.

### A.0 공통

| 항목 | 값 |
|---|---|
| `FacialState` | 모든 액션 공통. `Neutral`·`Happy`·`Sad`·`Angry`·`Fear`·`Surprised`·`Disgusted`·`Tired`·`Pain` (9). daily 는 앞 D4 의 5종만 |
| `target_id` 어휘 | 센티넬 `Player`·`Self`·`Enemy`(BB perception 타겟) + 등록 NPC AgentID + 가구 ID. 런타임 목록은 `NPCManager.cpp:288` `valid_targets`(빈 가구만 합류) |
| `target_loc` | C++ 내부 주입 전용 좌표 문자열. Python 이 좌표를 직접 보내는 경로는 없다(`:811`). Jev 는 의미 id 만 고르고 C++ 가 좌표로 바꾼다 |
| `item` 폴백 | `item` 이 비면 `target_id` 를 아이템 ID 로 쓴다(레거시, `:803`) |
| 아이템 분류 | `EItemType`: `General`(재료)·`Consumable`(포션·음식)·`Equipment`(무기·방어구)·`Quest`(버릴 수 없음) |
| 미디어 키 보유(몽타주 ✅/❌) | ✅ Attack·Block·Dance·Death·Dodge·Drop·Emote·LieDown·LieUp·PickUp·Pray·Sing·SitDown·SitUp·Track / ❌ Comfort·Craft·Eat·Give·Read·Repair |

### A.1 액션별 파라미터 · 후보 · 선택 주체 (2026-09-24 사용자 검토 반영)

주체 표기: `현재 → 결정`. **LLM** = Stage1 대사 배치 · **척수** = C++(`SelectCombatAction`·`TryReflexReact`·DiceSystem) · **Jev** = 이 SPEC 의 활동 표(D4).
원칙: **전투는 C++ 척수가 우선**이고 Jev 로 옮기지 않는다. 대사·거래는 LLM 이 맡는다. 그 밖의 선택은 Jev 로 옮긴다. 반사 테이블(척수)은 비전투 자극에도 계속 먼저 반응한다.
"통합 활동" 열 = D4 활동 표에서 이 EAction 을 흡수한 활동.

**Common (11)**

| 액션 | 파라미터 → 후보 | 필수 | 주체 | 통합 활동 | 결정·남은 일 |
|---|---|---|---|---|---|
| Idle | 없음 | — | LLM → Jev | `stay` | 큐에 넣지 않는 "아무것도 안 함" |
| Move | `target_loc` → POI·가구·액터 근처·랜덤 / `style` → `Walk`·`Run`·`Crouch` | 없음 | LLM·척수 → Jev(비전투) · 척수(전투 Spacing·EQS) | `wander` | Jev 는 좌표를 못 내므로 목적지는 의미 후보. **POI 시스템 신설 예정** — 그 전까지 `POI_` 태그 액터 목업(D4) |
| Follow | `target_id` / `style` | target_id | LLM → Jev | `follow` | 실제로는 1회 이동 코드. 지속 추적은 Track 방식으로 통합하고 종료 조건은 D10 |
| Wait | 없음(duration 미구현) | — | LLM → 유지 | `stay`(추후) | **duration 구현 예정.** 구현 전까지는 즉시 완료라 `stay` 와 같다 |
| Dialogue | `text` → 자유문 | text | **LLM 유지** | — | 비전투에서는 말할 때 상대를 자동으로 바라본다(D10) |
| TurnTo | `target_id` ∨ `target_loc` | 둘 중 하나 | LLM·척수 → Jev (+척수 반사 공존) | `look_at` | 대화 시 자동 바라보기는 C++ 기본 동작(D10), 선택지로도 남음 |
| Stop | 없음 | — | LLM → **즉시 제어 신호 유지** | — | 큐 밖 즉시 처리. Jev 활동이 아니다(command 전환 시 선점 경로로) |
| Scan | `target_id` ∨ `target_loc` | 없음 | LLM·척수 → Jev (+척수 반사 공존) | `look_at`(`around`) | 무인자 시 월드 원점을 둘러보는 결함 — 기본 초점을 자기 전방으로 수정 |
| UseItem | `item` → 인벤 `Consumable` | item | LLM → Jev(비전투) · **척수(전투 저HP 연쇄)** | `use_item` | `Eat` 몽타주 ❌. 전투 중 포션은 Jev 가 아니라 척수가 한다(비고 B.1) |
| Equip | `item` → 인벤 `Equipment` | item | LLM → Jev(command) | `equip` | **장착 중 목록 풀(`equipped`) 신설** 필요(`GetInventoryJson` 은 슬롯 보유분만) |
| Unequip | `item` → 장착 중 | item | LLM → Jev(command) | `equip`(토글) | 위와 같음 |

**Combat (5) — Attack·Block·Dodge·Flee·SignalAllies**: **C++ 척수 유지. Jev 대상 아님.** 현 셀렉터·반사 테이블·주사위 그대로(Jev 는 기존 전술 승수만). 저HP 후퇴·포션 연쇄는 척수에 추가(비고 B.1).

**Social (5)**

| 액션 | 파라미터 → 후보 | 필수 | 주체 | 통합 활동 | 결정·남은 일 |
|---|---|---|---|---|---|
| Trade | `target_id` / `give_item_id`·`give_amount` / `get_item_id`·`get_amount` | target_id + give_item | **LLM 유지** | — | 수량·교환비가 수치이고 플레이어 경제 직결이라 LLM 에 남긴다 |
| Emote | `style` → `Emote*` / `target_id` | style | LLM → Jev | `emote` | style 슬롯은 지금 만든다(몽타주 추가 대비) |
| GiveItem | `target_id` / `give_item_id`∨`item` / 수량(1 고정) | target_id + item | LLM → Jev | `give_item` | Player 대상은 command 전용. 수량 슬롯 없음(1) |
| Comfort | `target_id` | target_id | **제거** | — | 몽타주도 없고 쓰임이 적다. EAction 삭제 4곳(C++ enum·Python Literal·프롬프트·카테고리 맵) |
| HandObject | `item` → 인벤 | item | LLM → Jev(command) | `equip` | 코드상 `EquipItem` 호출과 같아 equip 에 흡수 |

**Task (3)**

| 액션 | 파라미터 → 후보 | 필수 | 주체 | 통합 활동 | 결정·남은 일 |
|---|---|---|---|---|---|
| PickUp | `target_id` → 바닥 아이템 ∨ `target_loc` | 둘 중 하나 | LLM → Jev | `pick_up` | D9 완료(2026-09-24 PIE 검증) |
| Drop | `item` / `amount` | item | LLM → Jev(**command 만**) | `give_item`(`ground`) | daily 금지(인벤이 줄기만 함). 고르면 item 슬롯 패스로 가방에서 고른다. 퀘스트·장착품 제외 |
| Craft | `item_ids` | item_ids | **보류** | — | 레시피 미구현·몽타주 ❌ |

**Investigation (3)**

| 액션 | 파라미터 → 후보 | 필수 | 주체 | 통합 활동 | 결정·남은 일 |
|---|---|---|---|---|---|
| Investigate | `target_loc` → 자극 위치 | target_loc | 척수 반사·LLM → Jev (+반사 공존) | `wander`(자극 지점) | POI 목업·자극 지점을 dest 후보로. 반사 규칙이 먼저 |
| Track | `target_id` | target_id | LLM → Jev | `follow` | 지속 추적의 실제 구현. 종료 조건·덮어쓰기 결함은 D10 |
| Scout | `start`+`end` ∨ `target_loc` | 둘 중 하나 | LLM → Jev | `patrol` | POI 목업으로 우선 동작(경비 역할 핵심 일상 행동) |

**Lifestyle (7)**

| 액션 | 파라미터 → 후보 | 필수 | 주체 | 통합 활동 | 결정·남은 일 |
|---|---|---|---|---|---|
| Sit | `target_id` → 빈 가구 | 가구 타겟 | LLM → Jev | `rest` | 자세는 가구 타입이 결정 |
| Sleep | `target_id` → 빈 가구 | 가구 타겟 | LLM → Jev | `rest` | 낮에도 잔다. 피로 같은 NPC 상태는 추후 |
| StandUp | 없음 | — | LLM → Jev | `stand_up` | — |
| Read | 없음(키 고정) | — | **보류** | (`emote` 후보) | `Read` 몽타주 ❌ |
| Pray | 없음(키 고정) | — | LLM → Jev | `emote` | 키 고정 해제(M1)해야 `Pray_*` 변형 선택 가능 |
| Dance | `style` → `Dance*` | style | LLM → Jev | `emote` | — |
| Sing | `style` → `Sing*` | style | LLM → Jev | `emote` | — |

### A.2 요약 (2026-09-24 결정 기준)

- 34개 EAction → **Jev 활동 12개**(D4). 흡수 관계는 위 "통합 활동" 열.
- Jev 밖: **LLM 2**(Dialogue·Trade) · **척수 5**(전투) · **제어 1**(Stop) · **보류 2**(Craft·Read) · **제거 1**(Comfort) · **유지(추후 흡수) 1**(Wait).
- **대분류 폐지 근거**: C++ 에서 `ENPCBehaviorMode::Social/Task/Investigation/Lifestyle` 참조 0곳(동작을 가르는 건 Combat 여부뿐). Python 은 `rules.py:273` Mode 보정 1곳만 대분류를 쓴다 → command 전환으로 LLM 이 액션을 안 내면 함께 제거.
- 남은 일(우선순위 순):
  1. **POI 목업**(`POI_` 태그 → `pois` 풀) — wander·patrol·look_at·Investigate 가 함께 풀림.
  2. **D10**: 지속 추적 종료 조건 + Track 덮어쓰기 결함, 대화 시 자동 바라보기.
  3. **풀 신설**: `equipped`(장착 중), 인벤 `category`(D8).
  4. **코드 결함**: Scan 원점 초점, Pray 키 고정, 같은 타입 연속 스킵 필터.
  5. **몽타주 부재**: Eat·Read·Craft + 표현 변형.
  6. **Comfort 제거**(EAction 4곳).

---

## 비고. 대화·전투 외 행동 Jev 전환 구성안 (2026-09-24 개정, 방향 확정·세부 미정)

목표: **LLM 은 대사·거래·목표(plan·story)만. 전투와 반사는 C++ 척수. 그 밖의 행동 선택은 Jev.**
2026-09-24 결정: 척수(전투 셀렉터·반사 테이블)를 Jev 로 옮기는 안은 **폐기**했다. 전투는 C++ 척수가 우선이다.

### B.1 3층 분업

| 층 | 지연 | 담당 |
|---|---|---|
| **척수(C++)** | 0ms | **전투 전부**(셀렉터·패링·Stumble·Flee·주사위), **반사 테이블**(비전투 자극 포함, 규칙 매칭 시 우선), 물리·공간 수치(좌표·방향·Spacing·NavMesh·몽타주), Jev 폴백 |
| **Jev(소뇌)** | 5~20ms 추론 + localhost 왕복, 워치독 0.3s | 비전투 행동 선택 + 슬롯(D4 활동 12개) |
| **LLM(대뇌)** | 1~3s | 대사·말투, **Trade**, plan goal, story beat |

원칙: **행동이 먼저, 대사가 나중.** Jev 가 고른 행동을 먼저 실행하고, 그 결과를 LLM 프롬프트에 넣는다(`You just did: rest(Bench_03)`). 그래야 대사가 행동과 모순되지 않는다.

**척수 추가 과제 — 저HP 자동 후퇴·회복 연쇄 (전투 기능, 2026-09-24 결정)**
- 조건: Combat 모드 ∧ HP ≤ `LowHpRetreatThreshold`(기본 0.3, EditDefaultsOnly) ∧ 회복 효과가 있는 Consumable 보유 ∧ 이번 전투에서 아직 미실행.
- 동작: 셀렉터 가중치·배짱 주사위를 거치지 않고 **결정론 연쇄**를 큐에 넣는다. [EQS 후퇴(기존 Flee 경로 `TryStartTacticalQueryForCombat`) → 도착(성공·실패 무관) → UseItem(회복템)].
- 회복템이 없으면: 기존 Flee 가중치 램프(`HPLoss²`)·주사위 그대로.
- 현재(2026-09-24): 저HP 는 Flee 가중치만 올리고(`NPCActionComponent.cpp:1824-1828`), 배짱 주사위 성공 시 도주가 억제되며, 도착 후 회복 로직이 없다.
- 전투 기능이므로 `SPEC_realistic_combat.md` 에 등록하고 전투 브랜치에서 구현한다(이 SPEC 범위 밖).

### B.2 트리거 구성

활동 목록은 D4 의 **하나**다. 트리거마다 달라지는 것은 context·daily 열(허용 여부)·폴백뿐이다.

| 트리거 | 언제 | 추가 context | 폴백 |
|---|---|---|---|
| `daily` | 비전투 Idle N초 | D3 | `stay`(현행 Idle) |
| `command` | 플레이어 발화 수신 | `utterance`(원문, ≤80자)·`relation`·affinity 구간·`goal` | 행동 없음(대사만) |

- **command**: jevlike 는 텍스트 context 모델이라 **플레이어 발화 원문을 context 에 그대로 넣는다**. `"사과 좀 줘"` + 인벤 풀 → `give_item(target=Player, item=Apple)`.
  - LLM Stage1 의 `actions` 필드를 없애고 `speech` 만 남긴다. 단 **Trade 는 LLM 에 남는다**.
  - `refuse` 를 정식 선택지로 둔다(persona·관계가 나쁠 때). 이때 LLM 은 거절 대사를 쓴다.
  - daily 에서 막힌 `give_item(Player)`·`give_item(ground)`·`follow`·`equip` 을 허용한다.
  - Noul 가드(`HarmfulProbability`)를 이 트리거에서 쓴다. 탈옥성 명령을 차단한다.
- 전투 중 플레이어 명령: 전투는 척수 우선이라 command 트리거는 Combat 모드에서 행동을 주입하지 않는다(대사만).

### B.3 공통 인프라

1. **`CollectNearbyContext`**(D8): 모든 트리거의 풀 소스. POI 목업 `pois`, 장착 중 `equipped` 도 여기서 모은다.
2. **대상 기반 액션 원칙**(D9): Jev 는 대상만 고르고 이동은 C++ 가 내장한다.
3. **선점 우선순위**: `척수(반사·전투) > command > daily`. 높은 쪽이 낮은 쪽의 진행 중 액션을 끊는다(D2 일반화). Stop 은 큐를 거치지 않는 즉시 신호로 둔다.
4. **중복 액션 스킵 필터 교체**: `DispatchActions` 의 "직전 타입과 같으면 버림"은 다른 대상의 같은 액션도 막는다(2026-09-24 실측). (타입+대상) 기준으로 바꾸거나 Jev 주입 경로는 우회한다.
5. **활동 표 한 곳**: D4 활동 표를 C++ 조립기와 Python 서비스가 함께 쓰도록 JSON 1개로 뽑는다(양쪽 하드코딩 2벌 금지).
6. **BehaviorMode 소유권**: 지금 Combat/Common 전환 일부는 LLM 배치 `Mode` 가 한다(`NPCActionComponent.cpp:356-371`). command 전환으로 LLM 이 액션을 안 보내면 **척수(반사 `bEnterCombat`·`ExitCombat`)가 단독 소유**한다. 대분류 모드(Social·Task 등)는 C++ 참조 0곳이라 함께 정리한다.

### B.4 학습 데이터

- 체크포인트는 전투 Jev(승수)와 공유해 하나로 유지한다. context 앞에 `[daily]`/`[command]`/`[combat]` 접두사를 붙인다.
- **command**: 기존 LLM 로그를 증류 씨앗으로 쓴다. 2026-09-24 기준 액션이 든 Stage1 응답은 **50건**이고 GiveItem 이 31건(62%)이다. 편향이 커서 씨앗으로만 쓰고 LLM 합성으로 채운다(목표 ≥ 3,000 조합, 활동별 최소 100). "보여줘"(equip)와 "줘"(give_item)처럼 헷갈리는 대비 쌍을 반드시 넣는다.

### B.5 도입 순서

```
daily M1(휴리스틱) ─→ daily M2(학습)
      │
      └─ 공통 인프라(B.3) + POI 목업 + D10
             └─→ command  ← LLM actions 제거(Trade 제외), EAction enum 정리는 이 단계 뒤
척수 과제(저HP 후퇴·회복) ─ 전투 브랜치에서 독립 진행
```

### B.6 하지 않을 것

- 전투·반사를 Jev 로 옮기지 않는다(2026-09-24 결정). 전투 Jev 는 기존 승수 역할만 유지한다.
- 대사·거래·story 를 Jev 로 옮기지 않는다.
- 좌표·각도·거리 같은 연속값을 Jev 가 직접 내지 않는다. 의미 후보(`POI_Gate`, `near_player`)만 고르고 수치는 C++ 가 만든다.
