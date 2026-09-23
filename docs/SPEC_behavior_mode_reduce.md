# SPEC: behavior-mode-reduce — 행동 대분류(BehaviorMode) 6값 → 2값 축소

> 작성 2026-09-24. `SPEC_jev_daily.md` M1 의 **선행 리팩토링**(Jev M1 과 독립, 한 커밋).
> 상태: **완료(2026-09-24)** — 에셋 삭제(D4)만 사용자 몫으로 남음(아래 구현 기록).

## 목표

`ENPCBehaviorMode` 를 `Combat`/`Common` 두 값으로 줄이고, 대분류에만 쓰이던 Python 카테고리 맵을 없앤다.
C++ 와 Python 을 **같은 커밋에서 동시에** 고친다(Envelope 계약 — `test_contract_sync.py:72-74` 가 양쪽 enum 일치를 강제).

**왜 지금**: 대분류(Social·Task·Investigation·Lifestyle)는 동작에 영향이 없는 죽은 값이다. 그런데 LLM 은 매 턴 이 6개 중 하나를 고르고 있다. Jev 활동 표(`SPEC_jev_daily.md` D4)는 대분류를 쓰지 않으므로, M1 코드가 줄어든 모드를 전제로 짜이도록 먼저 정리한다.

**현재 상태(2026-09-24 확인)**

| 사실 | 근거 |
|---|---|
| C++ 에서 `ENPCBehaviorMode::Social/Task/Investigation/Lifestyle` 참조 **0곳** — 동작을 가르는 건 Combat 여부뿐 | `grep "ENPCBehaviorMode::"` 결과 Combat·Common 만 |
| `ST_NPC` 는 C++ enum 을 **이름으로** 참조(`ENPCBehaviorMode::Common`, 이벨류에이터 출력 기본값). 조건 노드는 `StateTreeCompareBoolCondition` 1종뿐, **enum 비교 조건 없음** | `ST_NPC.uasset` 문자열 조사 |
| C++ 는 Mode 문자열을 `GetValueByNameString` 으로 파싱, 모르는 이름이면 **값을 건드리지 않음**(기본 Common) | `MCPJsonUtils.cpp:36-50` |
| Python 대분류 소비처는 Mode 보정 1곳 | `rules.py:265-279 _correct_mode_mismatch` ← `ACTION_CATEGORY` |
| 같은 이름의 **블루프린트 enum 에셋**(`/Game/Core/AI/ENPCBehaviorMode`, 2026-02-14 생성, 값에 `LifeStyle` 오타)이 남아 있다. `ST_NPC` 는 이것이 아니라 C++ enum 을 참조 | 에셋 문자열 조사 — 다른 에셋 참조 없음(구현 시 AssetRegistry 로 재확인) |

## 범위 (변경 파일·시스템)

| 층 | 파일 | 변경 |
|---|---|---|
| C++ | `NPC/Struct/NPCActionTypes.h:29` | `ENPCBehaviorMode` 에서 Social·Task·Investigation·Lifestyle 제거 → `Combat`, `Common` |
| Python | `app/schemas/actions.py` | `NPCBehaviorMode` Literal 2값. `CATEGORY_ACTION_MAP`·`ACTION_CATEGORY` 삭제 → `COMBAT_ACTIONS: frozenset`(Attack·Block·Dodge·Flee·SignalAllies) 신설. `DialogueResponse.mode` 설명 갱신 |
| Python | `app/agents/subgraphs/rules.py` | `_correct_mode_mismatch` 를 "전투 액션이 있는데 Mode≠Combat → Combat" 한 규칙으로 축소(D3) |
| Python | `app/agents/subgraphs/dialogue.py:308` | 폴백 응답 `mode="Social"` → `"Common"` |
| Python | `app/static/debug.html:121-141` | 액션 optgroup 대분류 라벨 제거(평탄 목록), Mode 드롭다운 2값 |
| 테스트 | `tests/test_contract_sync.py` | enum 일치 검사는 유지. "모든 EAction 이 카테고리에 배정" 검사(`:84-85`) 삭제 → `COMBAT_ACTIONS ⊂ EAction` 검사로 교체 |
| 테스트 | `tests/` (신규 1~2건) | Mode 보정 축소 규칙 단위 테스트 |
| 에셋 | `Content/Core/AI/ENPCBehaviorMode.uasset` | AssetRegistry 참조 0 확인 후 **삭제**(MCP) |
| 문서 | `.agents/rules/ue5_cpp.md:39`, `docs/SPEC_jev_daily.md` 비고 B.3-6 | 2값 기준으로 문구 갱신 |

변경 없음: `ST_NPC`(이름 참조라 그대로), `STEvaluator_NPCState`·`STTask_PrepareNextAction`(Combat 비교만), `FActionBatch`·`FModeActionRequest` 구조(필드 유지).

## 결정 사항

**D1. 값 이름은 그대로 두고 두 개만 남긴다** — `Combat`, `Common`.
- 숫자값이 바뀐다(Common 5 → 1). 그래도 안전한 이유:
  1. UPROPERTY 태그 직렬화는 enum 을 이름으로 저장한다.
  2. `ST_NPC` 도 이름으로 참조한다.
  3. enum 비교 조건이 없다.
  4. JSON 은 문자열로 주고받는다.
- 이름을 바꾸지 않으므로(예: Common→Peace 금지) 로그·학습 데이터·프롬프트와의 호환이 유지된다.

**D2. Stage1 LLM 의 `mode` 출력 필드는 유지하고 선택지만 2개로 줄인다.**
- 필드 자체의 제거는 command 전환(`SPEC_jev_daily.md` 비고 B.5) 때 LLM 스키마 정리와 함께 한다.
- Stage1 e4b 는 6값으로 파인튜닝돼 있다. 구조화 출력 문법이 2값만 허용하므로 형식은 깨지지 않는다. 다만 분포가 바뀌므로 완료 기준 6 으로 회귀를 본다.

**D3. Mode 보정은 전투 규칙 하나만 남긴다.**
- 지금 규칙은 "비-Common 액션의 다수 카테고리로 Mode 교정"이다. 2값이 되면 의미 있는 경우는 **LLM 이 Attack 등 전투 액션을 내면서 `mode=Common` 을 준 경우**뿐이다.
- 이걸 보정하지 않으면 C++ 가 Combat 에 들어가지 않아 전투 셀렉터가 돌지 않는다.
- 규칙: 배치에 `COMBAT_ACTIONS` 가 하나라도 있고 `Mode != "Combat"` → `"Combat"`.
- 반대 방향(Combat 인데 전투 액션 없음)은 보정하지 않는다. 전투 중 대사만 하는 배치는 정상이다.
  빈 배치는 C++ 가 원래 Mode 를 바꾸지 않는다(`ExecuteActionBatch`, `NPCActionComponent.cpp:356-371`).

**D4. 고아 블루프린트 enum 에셋은 삭제한다.**
- 같은 이름이 두 개 있으면 에디터에서 잘못 고를 위험이 있다(BP 에서 C++ enum 대신 BP enum 선택).
- 삭제 전 MCP 로 AssetRegistry referencers 가 0 인지 확인한다. 0 이 아니면 삭제를 보류하고 미결로 남긴다.

**D5. 한 커밋, 되돌리기는 revert 한 번.**
- `refactor:` 접두사 한 커밋에 C++·Python·테스트·에셋 삭제를 모두 담는다.
- 에셋 삭제가 끼므로 revert 시 에셋 복원까지 한 번에 된다.

## 완료 기준

1. `pytest tests` 전부 통과. `test_contract_sync` 의 enum 일치 검사가 2값으로 통과한다.
2. 신규 단위 테스트:
   - `[Attack]` + `mode=Common` → `Combat` 으로 보정된다.
   - `[Dialogue]` + `mode=Combat` → 유지된다.
   - `[GiveItem]` + `mode=Common` → 유지된다.
3. `sol_pi.py build` 에러 0.
4. MCP: `ST_NPC` 로드 시 경고 0, 이벨류에이터 BehaviorMode 기본값이 `Common` 으로 읽힌다.
5. MCP Simulate PIE:
   - 적대 대상 근접 → 반사로 Combat 진입.
   - 대상 사망 → `ExitCombat` 으로 Common 복귀(로그).
6. Stage1 대화 5턴(Python 서버 가동): `mode` 출력 ∈ {Combat, Common}, 파싱 실패 0. 모욕성 발화 1턴에서 Attack 이 나오면 배치 Mode 가 Combat 이다.
7. `ENPCBehaviorMode.uasset`(BP enum) 삭제 후 에디터 재시작 시 로드 에러 0.

## 단계

1. **확인** — MCP 로 BP enum 에셋 referencers 확인(D4). 결과에 따라 삭제 여부 확정.
2. **수정** — C++ enum → Python Literal·카테고리 맵·Mode 보정·폴백·debug.html → 테스트 교체·추가.
3. **검증** — pytest → 빌드(헤더 변경이라 에디터 종료 후 `sol_pi.py build`) → MCP 기준 4·5·7 → Python 서버 가동 후 기준 6.
4. **문서** — `.agents/rules/ue5_cpp.md`, `SPEC_jev_daily.md` 비고 B.3-6 갱신.
5. **커밋** — 한 커밋(D5).

## 미결 사항

- Stage1 `mode` 필드 완전 제거는 command 전환 때(D2).
- e4b 파인튜닝 데이터에는 옛 6값이 들어 있다. 재학습 여부는 기준 6 결과를 보고 판단한다. 문법 제약으로 형식은 보장되므로 당장은 불필요할 것으로 본다.
- `train_logs` 의 과거 기록에 있는 Social 등의 값은 그대로 둔다(기록 데이터, 소비처 없음).

## 구현 기록 (2026-09-24)

| 기준 | 결과 |
|---|---|
| 1·2 pytest | 91 passed(신규 Mode 보정 3건 포함) |
| 3 빌드 | `sol_pi.py build` 에러 0 |
| 4 ST_NPC | 컴파일 성공, enum 경고 0. Simulate 중 7개 NPC 모두 `COMMON(1)` 로 읽힘 |
| 5 Simulate | Vorg 를 James 3m 앞에 두자 `[Reflex] 적대·근접 즉시 공격 → Attack (Combat 진입)`. Vorg 사망 후 Guard `전투 타겟 장기 소실(8.0초) — 전투 해제, Common 복귀`(대상 액터가 사망 처리로 퇴출돼 사망 분기 대신 소실 분기로 빠짐 — 둘 다 `ExitCombat`) |
| 6 Stage1 5턴(Moca) | 5배치 전부 Mode ∈ {Common, Combat}, 파싱 실패 0. 모욕 발화 턴은 `[Dialogue, Dodge]` + `Combat`. Attack 은 안 나왔다 |
| 7 BP enum 삭제 | referencers 0 확인. **삭제는 자동 모드 권한 분류기가 차단** → DoList 로 이관 |
