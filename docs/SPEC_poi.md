# SPEC: poi

> 인터뷰 결정 2026-10-05. 의도·결정만 적는다(구현 세부는 코드가 설명). 상태: ⬜ 착수 전.

## 목표

NPC 가 **이름 있는 장소**(POI)를 안다 — LLM 이 "호수로 가" 를 `Lake` 로 읽어 이동을 지시하고, NPC 는 그 장소를 알고 말하며, 일상 행동(jev_daily)·스토리 구역도 같은 장소 어휘를 쓴다.
현행은 `POI_<이름>` 태그 액터 3개(Gate·Plaza·Well) 목업과 `FVector target_loc` 뿐이라 장소명으로 이동할 수 없다. 위치명 해석은 2026-07-09 에 소비처 0 으로 삭제된 채(`actions.py`) 미구현이다.

## 범위 (변경 파일·시스템)

| 영역 | 변경 |
|---|---|
| C++ 신규 | `APOIActor`(레벨 액터), `UPOIManager`(UWorldSubsystem 등록소) |
| C++ 수정 | `NPCManager`(`Pois` 캐시·`GetPoiActor` 목업 제거, `CollectNearbyContext` 가 등록소 호출), `NPCActionComponent`(`target_poi` 해석), `NPCActionKeys`(`Key_TargetPoi` 상수), `StoryZoneTrigger`(M4) |
| Python 수정 | `schemas/actions.py`(`target_poi` 스키마·어휘 검증), 프롬프트 조립(`interface_input.py` — POI id·표시명·별칭 노출), `jev_service.py` 는 `pois` 풀 계약이 유지되므로 변경 없음 |
| 레벨 | `POI_*` 태그 액터 3개를 `APOIActor` 로 교체, 스토리 맵 주요 장소 배치 |
| 문서 | `docs/SPEC_jev_daily.md` §299("실제 POI 는 별도 SPEC") 연결, `.agents/rules/pitfalls.md` |

범위 밖: 장소 자동 생성·스캔, 지형 저장 포맷, 플레이어가 장소를 지정하는 UI.

## 결정 사항

**D1. 원본 = 레벨 액터.** `APOIActor`(TargetPoint 파생)를 레벨에 놓는다. 속성: `PoiId`(영문 식별자, 예 `Lake`) · `DisplayName`(한국어) · `Aliases`(키워드 목록 — "호수"·"연못") · `Type` · `Description`(한 줄) · 도착 반경. 위치를 에디터에서 눈으로 잡고 좌표 SSOT 가 레벨 하나로 유지된다. DataTable·서버 YAML 은 좌표 이중화 위험이라 쓰지 않는다. WP 에선 `Is Spatially Loaded=false` 로 둔다(공간 로딩되면 등록소에 안 보임).

**D2. 조회·등록 = `UPOIManager`(WorldSubsystem).** BeginPlay/EndPlay 에서 자동 등록·해제하므로 런타임 스폰 POI 도 보인다(현행 `NPCManager` 캐시는 첫 조회 때 한 번만 훑어 못 봄). 조회: id → 액터, 별칭 → POI, 반경 내 목록, 타입별. NPC 관리자가 장소까지 떠맡지 않는다.

**D3. 장소명 → 좌표 해석은 C++(UE 수신 후).** `interface_output.py` 에서 좌표로 바꾸지 않는다. 근거:
1. Python 은 좌표를 직접 보낸 적이 없다(`NPCActionComponent.cpp:1007` — 위치는 C++ 내부 주입뿐). 좌표를 Python 에 두면 레벨과 이중 원본이 되고 POI 를 옮길 때마다 어긋난다.
2. 변환하려면 UE 가 좌표표를 매 프롬프트에 실어야 해서 SLM 토큰 다이어트 규칙(`python_backend.md` §3)과 부딪힌다.
3. 런타임 스폰·이동 POI 의 현재 위치는 UE 만 안다.

흐름: LLM 이 프롬프트에 노출된 POI 중 하나를 골라 `target_poi` 에 id 를 낸다 → Python 은 **어휘 검증만**(노출한 id 집합에 없으면 기존 `valid_*` 사후검증처럼 제거) → UE 가 `UPOIManager` 로 id → 좌표 → 기존 Move 경로(NavMesh 투영 포함). `target_loc` 은 C++ 내부 주입용으로 남기고 Python 이 직접 내지 않는 원칙을 유지한다. 키 철자는 Python 스키마가 단일 소스(`python_backend.md` §2).

**D4. 키워드 인식은 LLM 이 한다. 구제는 없다.** LLM 이 "호수로 가" → `Lake` 를 고른다. 별칭은 LLM 이 id 를 고르는 단서로 프롬프트에만 쓰고, 표시명·별칭 문자열이 id 자리에 오면 C++·Python 모두 구제하지 않는다. 미등록 id 가 UE 에 도달하면 조용히 증발시키지 않고 경고 로그를 남기고 액션을 건너뛴다(무음 실패 방지 — 실패가 보여야 프롬프트·어휘를 고친다. SLM 실측 뒤 필요하면 구제를 추가).

**D4-1. 프롬프트 노출 = 전체 POI 의 id·표시명 압축 + 상한 N.** `Lake(호수)` 한 줄씩. 상한을 넘으면 NPC 기준 가까운 순. 멀리 있는 장소로도 이동을 지시할 수 있다. 별칭은 노출하되 짧게.

**D4-2. `Type` 필드는 만들되 M1 은 쓰지 않는다.** 현행 풀은 타입 없이 동작 중이라 wander·patrol 이 타입으로 거를 필요가 확인될 때 어휘를 확정한다.

**D4-3. 도착 반경은 POI 별 속성(기본값 있음).** NavMesh 투영이 실패하면 근처로 대충 이동시키지 않고 경고 로그를 남기고 이동 없이 액션을 끝낸다(잘못 배치된 POI 가 숨지 않게). 액션 실패를 서버로 보고하는 채널은 아직 없어 기존 Move 실패와 같이 "경고 후 완료"로 처리한다(2026-10-08 M2).

**D5. jev_daily 는 소스만 교체.** `pois` 풀 계약(`id`·`desc` = `poi|<이름>|<거리>`)은 그대로고 소스만 태그 목업에서 `UPOIManager` 로 바꾼다. 일상 행동이 회귀 없이 같은 풀을 받는 것이 완료 조건이다.

**D6. 스토리 구역은 POI 를 참조만 한다(M4).** 구역(박스 영역)과 POI(점)는 성격이 달라 한 액터로 합치지 않는다. `StoryZoneTrigger` 는 그대로 두고 `PoiId` 필드로 POI 를 가리킨다. `zone_enter:<zone>` 플래그명·`main.yaml` 비트 조건은 바꾸지 않는다(후방 호환).

**D7. M3 장소 지식은 프롬프트마다 근처 POI 의 설명 한 줄만 싣는다.** 새 Envelope 타입 없음(UE·Python 3곳 동시 수정을 피함). 멀리 있는 장소 설명은 모르는 것으로 둔다.

**D8. 단계는 인프라 → 이동 → 인지 → 구역 순.**

## 완료 기준

- **M1** PIE 시작 로그에 등록된 POI 수가 찍힌다. 기존 Gate·Plaza·Well 3개가 `APOIActor` 로 바뀌어도 jev_daily 풀 수집 로그·`patrol dest=POI_Well`·`wander dest=POI_Plaza` 가 종전과 같다. 런타임에 스폰한 POI 도 풀에 잡힌다.
- **M2** 헤드셋 없는 PIE 에서 `say_to_npc("호수로 가")` → LLM 이 `target_poi=Lake` → NPC 가 호수 도착 반경에 도착. 미등록 id 는 경고 로그 + 건너뜀. Python 쪽 어휘 검증 pytest 통과. `sol_pi verify all` 통과.
- **M3** NPC 가 장소 이름·설명을 대사에 반영한다(예: "호수 가봤어?" → 호수에 대한 답).
- **M4** 구역 진입 이벤트·기존 스토리 흐름이 종전과 같고, 구역 이름이 POI 어휘와 일치한다.

## 단계 (Phase/Milestone)

| M | 내용 |
|---|---|
| M1 | `APOIActor`·`UPOIManager`, 태그 목업 교체, `CollectNearbyContext` 소스 전환 |
| M2 | `target_poi` 계약(상수·스키마·프롬프트 노출·어휘 검증)·C++ 해석·이동 |
| M3 | 장소 설명을 지식·프롬프트에 노출 |
| M4 | `StoryZoneTrigger` ↔ `PoiId` 연결 |

## 미결 사항 (작업 중 발견 시 추가)

- 노출 상한 N 값과 별칭 길이(PIE 에서 SLM 토큰·정확도 보며 조정).
- M3 에서 "근처" 의 반경(현행 `FurnitureContextRange` 와 같게 할지).
- `Type` 어휘(필터가 필요해질 때 확정).
- SLM 이 id 를 자주 틀리면 D4 의 구제 없음 결정을 재검토.
