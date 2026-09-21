# SPEC: story_director — 작가 주도 스토리 디렉터 (메인 퀘스트 + 서브퀘스트)

> 상태: **완료** (구현 A·B·C 2026-09-18, PIE 검증 1-12 2026-09-21, `feature/story-director`). b1→end 전체 비트 전이·스토리 디렉터·서브퀘스트·UE5 퀘스트 로그 전부 확인됨.
> 브랜치 제안: `feature/story-director` (베이스 main 최신). refactor/dead-code 와 분리.

## Context

프로젝트 목적 = NPC 두뇌 + **스토리 진행 두뇌** 두 축. 현재 NPC 두뇌(Stage1 e4b 대사·행동, Stage2 plan, 메모리·affinity)만 있고 스토리 레이어는 0. Supervisor 는 처음부터 if/else 라우터(첫 커밋 `dbbf4eb` 의 LLM 조정 프롬프트는 호출된 적 없음), Rules 는 결정론 검증, Stage2 는 "이 NPC 다음 몇 턴" 단기 plan — 셋 다 "세계가 어디로 가는가" 를 모름.

목표: 작가가 미리 쓴 메인 퀘스트(종착 = 보스 처치, 마인크래프트 엔더드래곤형) + 사전 제작 서브퀘스트를 따라 플레이어가 흘러가는 데모. 디렉터 = **결정론 상태기계가 뼈대, LLM(gemma4:cloud 31B) 은 그 위에서 선택·각색·대사 방향만**. 이 분리를 지키면 31B 로 충분(2026-09-17 실측: PlanBatchResponse 9/9 스키마 준수, 예시 복사 0, 지연 1.4s 중앙값), 안 지키면 데모 중 엔딩 스킵/역행 위험.

## 1. 결정 사항 (인터뷰 확정)

| 항목 | 결정 |
|---|---|
| 스토리 방식 | **작가 주도**. 비트 시트 YAML 사용자가 작성. 디렉터는 허용 전이 안에서만 선택·각색 |
| 디렉터 모델 | `MODELS["cloud_gemma4"]`(gemma4:cloud, 31B). 이미 등록됨(llm_factory.py:104, 미커밋) |
| 위치 | **Stage2 와 별도 모듈** `app/story/`. Stage2 에 섞지 않음(주기·입력·출력 전부 다름) |
| 종착 플래그 | `boss_killed` — 기존 `emergency_report(report_type="combat_victory")` 의 `perceptions[0].target_id` 재사용. **신규 Envelope 불필요** |
| 트리거 3종 | 계약 전부 정의. 구현 (a) combat_victory (b) replan 턴 → 즉시. (c) `story_event` Envelope → 핸들러만(UE5 송신은 추후) |
| 플레이어 체감 경로 | **MVP = A+B**: NPC goal 주입 + UE5 퀘스트 로그 UI. **Phase C(추후)**: 세계 변화 명령, 화이트리스트 `spawn_enemy` 1종만 |
| 퀘스트 상태 보존 | **파일** `story_state.json` (atomic tmp→rename). 서버 재시작 시 이어감 |
| 작가 파일 | `app/story/content/main.yaml` + `content/side/*.yaml`. PyYAML 6.0.3 설치 확인됨 |
| 제외 | 크툴루 룰북·분기 엔딩(엔딩 1개)·세션 간 세이브 슬롯·음성·NPC 간 충돌 조정(supervisor LLM 부활 아님) |

## 2. 현재 상태 (검증 2026-09-17)

| 구성 | 파일 | 상태 |
|---|---|---|
| Envelope 타입 | `app/schemas/envelope.py:27` / `Source/.../Network/EnvelopeBuilder.h:23` | 4종. 세계 이벤트 타입 없음. `tests/test_contract_sync.py::test_envelope_wire_strings_match_python` 가 양쪽 동시 수정 강제 |
| 보스 처치 신호 | `main.py:313 _handle_combat_victory` | target_id 를 메모리 기록만. 플래그 소비처 없음 |
| NPC goal 주입 | `dialogue.py:353 _generate_plans` sections / `interface_input.py:126 _format_plan_context` | Stage2 입력 = Stage1 출력 섹션만. 상위 목표 주입 지점 없음 |
| plan → UE5 | `main.py:438 _finalize_prompt_response` `NpcPlans` → `NPCManager.cpp:456` → `NPCStateComponent::SetCurrentPlan` → `OnPlanUpdated` 위젯 | **퀘스트 로그 UI 도 같은 패턴으로 확장 가능** |
| Python→UE5 푸시 | 없음. 요청/응답만 | 디렉터 출력은 항상 UE5 메시지 응답에 실림 |
| 세계 상태 저장 | `STATE.cached_world_states`(메모리) | 퀘스트 상태 둘 곳 없음 |

## 3. 설계

### 3.1 파이프라인
```
UE5 메시지 ─┬─ emergency_report(combat_victory) ──┐
            ├─ prompt(requires_replan=True) ───────┤→ story.on_trigger(kind, data)
            └─ story_event(신규, UE5 송신 추후) ───┘        │
                                        ┌───────────────────┘
                                        ▼
           [결정론] complete_when 평가 → 비트 전이 → 서브 해금 → state 저장(파일)
                                        │ dirty 일 때만
                                        ▼
           [LLM gemma4:cloud] StoryDirectorResponse{npc_goals, quest_log, side_surface}
                                        │
                     ┌──────────────────┼──────────────────────┐
                     ▼                  ▼                      ▼
        Stage2 sections 에          응답 JSON `Story` 블록      (Phase C) `Story.events`
        "Story directive" 주입      → UE5 퀘스트 로그 위젯       → UE5 spawn_enemy 실행
```
- 디렉터 LLM 은 **dirty(비트 전이·해금 발생) 일 때만** 호출. 아니면 캐시된 goals 재사용. 무료 티어 동시 1·크레딧 보호.
- LLM 실패 시: 비트 전이는 이미 확정(결정론). goals 는 작가 YAML `npc_goals` 원문 그대로 사용 → 데모 안 멈춤.

### 3.2 작가 파일 포맷 (`content/main.yaml`)
```yaml
title: 용의 그림자
start: b1_arrival
beats:
  - id: b1_arrival
    title: 마을 도착
    summary: 플레이어가 마을에 도착. 용의 위협을 NPC 들이 암시한다.   # 디렉터 컨텍스트용
    npc_goals:                       # 작가 초안. 디렉터가 상황 맞춰 각색(없으면 원문 사용)
      Elara: 플레이어에게 북쪽 동굴의 용 이야기를 흘린다
      Guard_01: 성문 밖 위험을 경고하고 무기 준비를 권한다
    quest_log: 마을 사람들과 이야기해 용에 대해 알아보자   # 플레이어 UI 초안
    complete_when: {type: talked_to, npc_id: Elara, min_turns: 2}
    next: b2_prepare
    unlocks_side: [s_lost_brother]
    events: []                       # Phase C. [{cmd: spawn_enemy, enemy_id: zombie, loc: EastBridge, count: 3}]
  - id: b7_dragon
    title: 용 처치
    complete_when: {type: boss_killed, boss_id: DragonBoss}
    next: end
```
`complete_when.type` ∈ `talked_to | boss_killed | flag`(flag 는 story_event 로 세팅). 서브퀘스트(`side/*.yaml`) 동일 구조 + `available_after: <main beat id>`, `next` 없음(단일 비트 또는 짧은 체인).

### 3.3 상태 파일 (`app/story/story_state.json`, gitignore)
```json
{"main_beat": "b2_prepare", "completed": ["b1_arrival"], "flags": {},
 "talk_counts": {"Elara": 3}, "side": {"s_lost_brother": "available"},
 "director_cache": {"npc_goals": {...}, "quest_log": "..."}, "updated_at": "..."}
```
side status ∈ `locked | available | active | done`.

### 3.4 디렉터 LLM 계약
```python
class NPCGoalDirective(BaseModel):
    npc_id: str
    goal: str            # 한국어 1구절. Stage2 가 steps 로 분해
    hint: str = ""       # 대사 방향 1문장 (Stage1 컨텍스트엔 안 넣음, Stage2 sections 에만)
class StoryDirectorResponse(BaseModel):
    npc_goals: List[NPCGoalDirective]
    quest_log: str                     # 플레이어 UI 1문장
    side_surface: List[str] = []       # available 중 지금 드러낼 서브 id — 후보 밖 값은 코드가 버림
```
입력: 현재 비트(summary·npc_goals·quest_log 초안) + 직전 비트 요약 + available 서브 목록 + 최근 플레이어 이력(talk_counts·flags) + 트리거 사유. 프롬프트 ~1.5k 토큰. `ollama_structured(..., model_name=STORY_MODEL, temperature=0.4, num_predict=400)`.
디렉터는 **비트를 고르지 않는다** — 코드가 확정한 비트를 각색만.

### 3.5 주입 지점 (기존 경로 재사용)
- **Stage2**: `dialogue._generate_plans` sections 앞에 `=== STORY DIRECTIVE ===\n<npc>: <goal> — <hint>` 블록 1개 추가. PLAN_SYSTEM_PROMPT 에 "STORY DIRECTIVE 가 있으면 goal 은 그 방향을 따르되 NPC 응답 사실만 사용" 1문장.
- **Stage1 경량 루프**: 변경 없음 — Stage2 plan 이 이미 디렉티브를 반영해 UE5 `current_plan` 으로 돌아옴.
- **응답 JSON**: `_finalize_prompt_response`·`_handle_combat_victory` 응답에 최상위 `Story: {beat_id, quest_log, side: [...], events: [...]}` (PascalCase 키, 내부 snake_case — NpcPlans 규약 동일). dirty 아닐 땐 생략.

### 3.6 Envelope 계약 추가 (`story_event`)
```python
class StoryEventPayload(BaseModel):
    event: str                     # "flag" | "zone_enter" | "item_acquired"
    name: str                      # flag 이름 / 구역 id / 아이템 id
    agent_id: Optional[str] = None
```
Python: `EEnvelopeType.STORY_EVENT = "story_event"` + `main._handle_story_event` (플래그 세팅 → on_trigger → 빈 배치 + Story 블록 응답).
C++: `EEnvelopeType::StoryEvent` + `EnvelopeBuilder.cpp` wire string `"story_event"` + switch — **송신 코드는 이 스펙 범위 밖**(계약 테스트 통과용 최소). 이유: `test_cpp_envelope_enum_fully_switched`·`test_envelope_wire_strings_match_python` 가 한쪽만 추가하면 실패.

### 3.7 UE5 측 (Phase B / C)
- B: `NPCManager.cpp:456` 근처 `Story` 오브젝트 파싱 → `FStoryState{BeatId, QuestLog, Side[]}` → 신규 `UStorySubsystem`(GameInstanceSubsystem) `OnStoryUpdated` 브로드캐스트 → 퀘스트 로그 WBP 바인딩(에디터 수작업 → DoList).
- C: `Story.events[]` → `UStorySubsystem::ExecuteEvent` 화이트리스트 `spawn_enemy` 만. `enemy_id → TSubclassOf<AActor>` 매핑 UPROPERTY, `loc` 는 이름→월드 좌표 레지스트리(기존 location id 해석 경로 재사용 여부 구현 시 확인). 미상 cmd/enemy_id 는 로그 후 무시.

## 4. Phase 분할

| Phase | 내용 | 검증 |
|---|---|---|
| **A** | `app/story/` 로더·상태기계·디렉터·트리거 3종·Stage2 주입·`Story` 블록·`story_event` 계약(Py+C++ enum) | pytest 전부 + ws roundtrip |
| **B** | UE5 `Story` 파싱·`UStorySubsystem`·퀘스트 로그 WBP | Build.bat + PIE(사용자) |
| **C** | `events` 파싱 + `spawn_enemy` 실행기 | PIE(사용자) |

A 만으로 Python 단독 데모 검증 가능(더미 main.yaml + combat_victory mock). B·C 는 각각 별도 커밋/이슈.

## 5. 파일 참조

| 파일 | 변경 |
|---|---|
| `app/story/__init__.py`, `loader.py` | YAML 로드·스키마 검증(pydantic). `start`·`next` 참조 무결성, `complete_when.type` 검증. 실패 시 서버 기동 시 명확한 에러 |
| `app/story/state.py` | `StoryState` + `load/save`(atomic). `on_trigger(kind, data) -> bool dirty` |
| `app/story/director.py` | `DIRECTOR_SYSTEM_PROMPT`, `StoryDirectorResponse`, `run_director(state) -> dict` (LLM 실패 시 작가 원문 폴백) |
| `app/story/content/main.yaml`, `content/side/example.yaml` | 더미 3비트 + 서브 1 (테스트용, 사용자가 실제 콘텐츠로 교체) |
| `app/schemas/envelope.py:27` | `STORY_EVENT`, `StoryEventPayload`, `parse_story_event_payload` |
| `app/main.py:203` `_process_llm_message` | `STORY_EVENT` 분기 → `_handle_story_event` |
| `app/main.py:313` `_handle_combat_victory` | `story.on_trigger("combat_victory", defeated)` → Story 블록 동봉 |
| `app/main.py:382` `_build_prompt_state` | `story_directive` 를 AgentState 에 실음(replan 턴만) |
| `app/main.py:438` `_finalize_prompt_response` | `Story` 블록 추가 |
| `app/agents/state.py` | `story_directive: Optional[dict]` |
| `app/agents/subgraphs/dialogue.py:353` | sections 에 STORY DIRECTIVE 블록 |
| `app/agents/subgraphs/prompts.py:58` | PLAN_SYSTEM_PROMPT 1문장 |
| `app/utils/llm_factory.py` | `STORY_MODEL = "cloud_gemma4"` (STAGE2_MODEL 은 그대로) |
| `Source/UE5_MCP_VR/Network/EnvelopeBuilder.h:23`, `.cpp:16` | `StoryEvent` + `"story_event"` |
| `tests/test_story_loader.py`, `test_story_state.py`, `test_story_director.py` | 신규 |
| `tests/test_ws_roundtrip.py`, `test_contract_sync.py` | 확장 |
| `.gitignore` | `app/story/story_state.json` |
| `docs/DoList.md` | B·C 에디터 작업(WBP·enemy 클래스 매핑) |

## 6. 수용 기준 (Phase A)

1. `main.yaml` 의 `next` 가 없는 id 를 가리키면 서버 기동 시 `ValueError` 로 실패, 메시지에 비트 id 포함
2. `combat_victory` target_id == 현재 비트 `boss_id` → 응답 JSON `Story.beat_id` 가 `next` 로 전이. 다른 target_id 면 무전이
3. `talked_to(min_turns=2)` — 해당 NPC 로 prompt 2회 후 전이, 1회 후 무전이
4. `story_event{event:"flag", name:X}` → `flags[X]=true`, `complete_when{type:flag,name:X}` 비트 전이
5. 비트 전이 시 `unlocks_side` 의 서브 status `locked→available`, 전이 없으면 불변
6. 디렉터 응답 `side_surface` 에 available 아닌 id 가 있으면 제거됨(테스트: 모킹 응답에 오염 id 주입)
7. 디렉터 LLM 예외 시 `npc_goals` = YAML 원문, 응답 정상, 로그 1줄
8. dirty=False 인 replan 턴은 디렉터 LLM 호출 0회(호출 카운터 모킹)
9. 서버 재시작 후 `story_state.json` 의 `main_beat` 유지
10. Stage2 sections 에 `=== STORY DIRECTIVE ===` 블록 포함(replan 턴, 디렉티브 존재 시)
11. `test_contract_sync` 전부 통과(story_event 양쪽 등록)
12. 기존 pytest 전부 통과, `python tools/sol_pi.py verify all` 통과
13. 라이브 테스트 1건(`@pytest.mark.llm`, 네트워크 없으면 skip): gemma4:cloud 로 더미 비트 → `StoryDirectorResponse` 파싱 성공

## 7. 테스트

| 층 | 대상 | 수 |
|---|---|---|
| Unit | loader 검증 3, state 전이 5(boss/talked/flag/side/persist), director 필터·폴백·dirty 게이트 3 | +11 |
| Integration | ws roundtrip: combat_victory→Story 블록, prompt replan→sections 디렉티브 | +2 |
| Live | gemma4:cloud 디렉터 1 | +1 |
| PIE(사용자) | B: 퀘스트 로그 갱신 / C: spawn_enemy 3마리 | DoList |

## 8. 롤백

`STORY_ENABLED=0` env 또는 `content/main.yaml` 부재 → 모든 훅 no-op, `Story` 블록 미생성. Stage2 sections 무변경. C++ enum 추가는 무해(송신 없음).

## 9. 효과 추정 (CC)

A: loader+state 1.5h · director+프롬프트 1h · main/dialogue 훅 1h · 테스트 1.5h · C++ enum+빌드 0.5h = **~5.5h**
B: C++ 파싱+Subsystem 1.5h + WBP(사용자) · C: 실행기 1h + 에디터 매핑(사용자)

## 10. 열린 질문 (구현 중 확인)

- `loc` 이름→좌표: 기존 `target_loc`/location id 해석기 재사용 가능한지 (Phase C 시점)
- 무료 티어 402 시 폴백 = 작가 원문(§3.1). `mid`(qwen3:8b) 폴백은 예시 복사 결함 있어 **안 함**
- 결정원장 §A 에 "스토리 디렉터 = 별도 모듈, Stage2 미혼합, 결정론 뼈대 + LLM 각색" 행 추가 (구현 커밋 시)
