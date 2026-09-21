# SPEC: reflex_table — C++ 전담 척수반사 테이블 + SLM 반사 제거

> 상태: **구현 완료, PIE 검증 대기** (2026-07-11 인터뷰 · 2026-08-18 §8 해소·UE MCP 실측 · 2026-08-19 구현). C++ 빌드 통과, pytest 45/45. 남은 것은 §7 PIE 5항목뿐.
> 브랜치: `feature/reflex-table` — 베이스 **`a9936e4`**(당시 `feature/finetune` 팁). 워크트리 `C:\github\UE5_MCP_VR_reflex`.
> Develop 베이스는 부적격 — UE 에디터 MCP(`f98cd40`·`a9936e4`)와 가구 연동(`e8cdb87`·`299f1bc` 등 5커밋, 반사 대상 파일 직접 수정)이 Develop 에 없어 §5 라인 앵커가 어긋난다.
> 선행 지식: SPEC_combat_selector(Phase 1 셀렉터 — 큐 주입 패턴의 선례).
> `77a53d2`(12B 선제 웜업)는 **2026-08-19 폐기**됐다(`feature/llm-perf` 삭제, 태그 `archive/llm-perf` 만 보존). 후속은 `SPEC_llm_perf.md` — 그 스펙은 replan 훅만 담당하고, **emergency 웜업은 이 스펙 §3.5 로 이관**됐다.
>
> **2026-08-19 구현 완료.** C++: `FReflexRule`·`ENPCRelation`(NPCActionTypes.h) · `TryReflexReact`/룰 6개 기본값(NPCActionComponent) · 컨트롤러 Sight/Hearing 트리거 · 빈 배치 Mode 스킵 · `reflex_action` 직렬화. Python: SLM 반사 4종 삭제, 통보 전용화, `_record_reflex_memory`, `reflex_action` 스키마, 회귀 테스트 5건.
> **12B 선제 웜업(§3.5)은 미적용** — `_prewarm_core_llm` 이 아직 없다(`SPEC_llm_perf` §3.1 선행). 그 스펙 구현 시 한 줄 추가할 것.

## 1. 목표 / Why

현상 (2026-07-11 진단):
- Python SLM 반사(`main.py::_handle_slm_reflex`)의 `_REFLEX_PROMPT` Rules 줄 자체가 이미 결정론 룰 — e4b 가 룰을 흉내내는 구조. 전투 진입 순간 = 12B 웜업·TTS와 GPU 경합 최악 타이밍.
  - **지연 실측**(2026-08-19, `NOTES_reflex_table.md` §2 — HEAD `a9936e4` 의 `_REFLEX_PROMPT` 원문 360토큰을 실제 호출 옵션 그대로 재현): warm p50 **~600ms** · prompt_eval 캐시 미스 tail **2.2s** · **콜드 로드 8.4~12.3s**. 여기에 WS 왕복 + debounce 0.3s → **최악 9~13초**. 구 기재("400ms~1.1s, cold 시 수 초")는 꼬리와 콜드를 과소 표현한 값이었다. `keep_alive=5m` 이라 "5분 조용하다 전투 발생" = 흔한 시나리오에서 매번 콜드다.
  - 같은 측정에서 응답은 3회 모두 `Attack` 로 고정 — 즉 룰 표의 `Sight·Hostile·근접 → Attack 100` 과 결론이 같다. **이관이 관측 가능한 동작을 바꾸지 않는다는 방증**(§7 검증 시 "행동은 같고 지연만 사라졌다"로 읽으면 된다).
- 저위험 소음(Drop 0.4·UseItem 0.2·Dialogue 0.1)은 danger 게이트(≥0.5)에서 전량 무반응 — 비전투 생동 반응 부재.
- 반사 경로에 WS 왕복 + debounce 0.3s + SLM 내재 — 서버 다운 시 반사 자체가 소멸.

목표 3개:
1. **접적 반사 0ms**: Hostile/Friendly/Neutral 접적 반응을 C++ 테이블로 — WS 왕복·debounce·SLM 전부 제거, 서버 오프라인에도 생존.
2. **비전투 생동 반응**: 소음 유형별 반사(Drop→Investigate, UseItem→TurnTo 등) — 기존엔 게이트에서 죽던 자극.
3. **Python 반사층 제거**: emergency_report 를 통보 전용(인지·호감도·기억·replan)으로 재편 — combat_victory(Phase 2) 패턴의 일반화.

## 2. 결정 사항 (인터뷰 확정)

| 항목 | 결정 |
|------|------|
| 반사 위치 | **C++ 전담** — `OnTargetPerceptionUpdated` 에서 테이블 매칭 → `ActionQueue` 직접 주입(셀렉터 동형). **ST 에셋·새 State 불필요** — 기존 `STTask_PrepareNextAction` 루프가 소비 |
| SLM 반사 | **제거** — `_handle_slm_reflex`·`_REFLEX_PROMPT`·`_infer_reflex_action`·`_REFLEX_FACIAL` 삭제. emergency_report 는 통보 전용(빈 배치 응답) |
| 자극 범위 | 전투 접적(Sight) + **Hearing 소음 유형별**. EventType 은 C++ 태그 파싱("Attack:0.9") 결과 로컬 보유 — Python 전달 불필요, FPerceptionData 무변경 |
| 룰 저장 | **UPROPERTY 구조체 배열**(`FReflexRule` TArray) — C++ 기본값 확정(§9, 바이너리에만 두지 말 것). DataAsset·YAML 아님 |
| 반응 | 액션만(기존 EAction: Attack/Block/Dodge/Scan/TurnTo/Investigate/SignalAllies 등) — 감탄사 Dialogue 제외, 새 EAction 없음(§3) |
| 확률 | 룰당 `TMap<EAction, float>` 가중 분포 추첨 + DiceSystem 결(인간화 노이즈 — SLM 변주 대체) |
| Neutral 처리 | C++ 주사위로 처리(Scan60·Signal40 류) — SLM 하이브리드 아님(전담 결정) |

## 3. 설계

### 3.1 파이프라인
`OnTargetPerceptionUpdated`(Sight 확보 / Hearing 태그 파싱 직후) → 반사 테이블 매칭 → 매치 시: 쿨다운 검사 → 가중 분포 추첨 → `ActionQueue` 주입(+bEnterCombat 시 Mode 전환) → 기존 ST 루프가 실행. 미스 시 현행 그대로(무반응).
**emergency_report 발신·debounce 는 무변경** — LLM 인지·replan 경로 보존. 반사(로컬)와 통보(원격)가 병렬.

### 3.2 FReflexRule (UPROPERTY)
- 소유 컴포넌트: **`UNPCActionComponent` 확정**(2026-08-18, §8-① 해소). 룰 배열·매칭·추첨·주입 전부 컴포넌트 안. 컨트롤러는 진입점 하나만 호출:
  ```cpp
  // NPCActionComponent.h — 신규 카테고리 "MCP|Reflex"
  bool TryReflexReact(ESenseType Sense, const FString& EventType, const FString& SourceID,
                      float BaseDanger, float Distance, const FVector& StimulusLoc);
  ```
  반환 bool = 발동 여부 → 컨트롤러가 §3.4 `reflex_action` 동봉 판단에 사용.
  근거: `ActionQueue` 는 `NPCActionComponent.h:118` private `TQueue<FGameAction>` 이고 주입 선례가 전부 컴포넌트 내부다(셀렉터 `.cpp:325` · EQS 결과 `.cpp:1256` · `.cpp:1891`). 컨트롤러→컴포넌트 단일 진입점 형태도 `TryStartTacticalQueryForCombat`(`.h:356` ← 컨트롤러 `:338`/`:421`) 로 이미 있다. 튜닝 UPROPERTY 군도 `MCP|CombatSelector`(`.h:527-602`) 로 컴포넌트에 집결.
- 자극 필터: `ESenseType`(Sight/Hearing) · EventType 부분매치 FString(Hearing 전용: Attack/Drop/UseItem/Dialogue) · affinity 구간(기존 `AffinityFriendlyThreshold`/`AffinityHostileThreshold` 재사용 — 새 임계 금지) · 거리 상한(cm) · danger 하한.
- 반응: `TMap<EAction, float>` 가중 분포 · `bEnterCombat` · 룰별 쿨다운(초).
- 기본 룰 세트(구 SLM few-shot Rules 이관 + 비전투 2종):

| 자극 | 반응 |
|------|------|
| Sight·Hostile·근접(≤500cm) | Attack 100 |
| Sight·Hostile·원거리 | Attack 80 / SignalAllies 20 |
| Sight·Neutral·danger≥0.5 | Scan 60 / SignalAllies 40 |
| Sight·Friendly·≤800cm | TurnTo 100 · 쿨다운 30s · `bEnterCombat=false` (§8-② 해소) |
| Hearing·"Drop" | Investigate 60 / TurnTo 40 (소음 위치로) |
| Hearing·"UseItem" | TurnTo 70 / Investigate 30 |

**danger 필터는 `BaseDanger`(자극 원본 세기) 기준이다.** 최종 위험도(= BaseDanger × 호감도배율)를
쓰면 Friendly 는 배율이 0 이라 항상 danger=0 이 되고, danger 조건이 붙은 Friendly 룰은 영원히
안 걸린다. 관계는 `Relation` 이, 세기는 `BaseDanger` 가 따로 본다.

구 표의 `Sight·Friendly·danger≥0.5 → Scan`("아군이 위협에 노출된 상황") 행은 **삭제**했다(2026-08-19 구현 중 판정).
Sight 의 BaseDanger 는 `SmartNPCAIController.cpp:22` 의 상수 `0.6f` 하나뿐이라 "친구가 위험하다"를
표현할 수가 없다 — 조건을 최종 danger 로 읽으면 절대 안 걸리고, BaseDanger 로 읽으면 모든 친화
목격에 걸려 바로 아래 인사 룰을 통째로 가린다. 아군 위기 감지는 자극 세기가 아니라 **대상의 상태**를
봐야 하므로 반사 테이블의 표현력 밖이다(§8 잔여).

전투 소음(`Attack`/`Damage`/`Hit` 또는 `BaseDanger≥0.5`)은 **반사 테이블이 먹지 않는다**(§8-③ 해소) — 아래 §3.3.7.

### 3.3 소유권·함정 (구현 시 필수 준수)
1. **Combat 진입 소유권 확장(§8)**: 현행 Combat 진입 = Python 응답 `Mode=Combat` 이 유일 경로 — SLM 반사 제거로 소멸. `bEnterCombat` 룰이 `SetBehaviorMode(Combat)` 직접 호출(컨트롤러 경유 — `HandleCombatTargetDead` 선례). **CLAUDE.md §8 표 갱신 포함.**
2. **빈 배치 Mode 역전환 함정**: `ExecuteActionBatch` 가 `SetBehaviorMode(Batch.Mode)` 를 무조건 호출 — Python 통보 응답(빈 배치, Mode=Common)이 반사가 방금 올린 Combat 을 되돌림. **Actions 빈 배치면 Mode 갱신 스킵** 수정 필수(Phase 2 combat_victory 빈 배치도 소급 안전).
3. 비전투 반사는 Mode 무전환(Common 유지) — 소음 조사가 전투 셀렉터를 깨우면 안 됨.
4. 스팸 억제: 룰별 쿨다운 + NPC당 최근 반사 시각(perception tick 9s 반복·hearing 연발 대비).
5. 반사 주입은 `bIsBusy`/큐 상태 존중 — 진행 중 액션 강탈 금지(셀렉터와 동일 규율). `LastQueuedActionType` 중복 가드 고려.
6. 액션 완료 모델(CLAUDE.md §3) 준수 — 주입 액션 전부 기존 실행·완료 경로 재사용.
7. **전투 소음은 EQS 전담, 반사 테이블 비개입**(§8-③ 해소, 2026-08-18). `SmartNPCAIController.cpp:405-420` 의 `bIsCombatNoise`(`Attack`/`Damage`/`Hit` 또는 `BaseDanger≥0.5`) 경로는 `TryStartTacticalQueryForCombat` 가 이미 담당하고, 억제는 `TacticalQueryCooldown=6.0f`(`NPCActionComponent.h:366`)+`LastTacticalQueryTime`(`.h:423`) 가 한다. 같은 자극에 반사 `Scan`(즉시형·큐 즉시 점유)과 EQS `Move`(수백 ms 뒤 `.cpp:1256` 주입)가 겹치면 **Scan 이 큐 앞을 막아 엄폐가 지연**된다 — 전투 소음에서 최악. `LastQueuedActionType` 중복 가드(`.cpp:318`)는 타입이 달라 자동으로 막지도 못한다. 따라서 Hearing 룰은 비전투 소음 전용.
   - 대안(수요 확인 후): `TryStartTacticalQueryForCombat` 가 쿨다운/실패로 **거부한 경우에만** fallback `Scan`. 지금 구현하지 말 것 — §8 잔여 미결.

### 3.4 반사 이력 통보 — LLM stale-context 방지 (2026-07-11 추가 결정)
문제: 반사가 이미 반응했는데 LLM 은 모름 → replan 이 "공격 개시" 류 중복·한 박자 늦은 지시 생성.
- **C++**: 반사 발동 시 emergency_report payload 에 `reflex_action: "<EAction명>"` 옵션 필드 동봉(`SerializePerceptionReport` — `report_type` 과 동일 패턴, 미발동 시 필드 생략·하위호환).
- **Python**: 수신 시 Event 메모리 기록(combat_victory 패턴 재활용 — `add_entry("Event", "...반사적으로 Attack 실행")`). 대화 메모리 컨텍스트가 prompt 에 유입되므로 다음 replan 은 "최소 반응 완료" 전제에서 시작 — plan 이 '전투 돌입'이 아닌 '전투 지속·전술' 수준에서 출발.
- 선례: `failed_action_history`(실패 이력 동봉)와 같은 사상의 실행 이력판.

### 3.5 Python 변경(축소)
- 제거: `_handle_slm_reflex`·`_REFLEX_PROMPT`·`_infer_reflex_action`·`_REFLEX_FACIAL`.
- `_handle_emergency_report`: danger 게이트 통과 시에도 `_apply_hostile_affinity`(호감도 감점) 후 **빈 배치 반환**(통보 전용). danger 게이트·combat_victory 분기는 유지. 신규 `_record_reflex_memory(agent_id, reflex_action)` 가 반사 이력을 Event 로 기록(게이트 **위**에서 호출 — 저위험 반사도 기록돼야 한다).
- **12B 선제 웜업 발사 동반**(2026-08-19 `SPEC_llm_perf` §4 에서 이관). 위 통보 전용화로 이 함수를 어차피 재작성하므로 같은 세션에서 한 줄 넣는다:
  ```python
  spawn_background(_prewarm_core_llm(), label="core-prewarm")   # 빈 배치 반환 직전
  ```
  WHY: 전투 진입은 곧 `FlagDangerReplan` → Stage2(12B) 신호다. 12B 는 `keep_alive=30s` 라 replan 시점엔 거의 항상 축출 상태이고, **재로드 실측 11.0s**(2026-08-19, `SPEC_llm_perf` §1.1). replan 훅 하나로는 Stage1 창 **3s** 밖에 못 가려 8s 가 남는다 — 전투 진입 시점에 미리 걸면 리드타임이 그만큼 길어진다. SLM 반사(e4b)를 제거한 뒤라 이 순간 GPU 가 비어 있어 경합도 없다.
  전제: `_prewarm_core_llm`·30s 스로틀은 `SPEC_llm_perf` §3.1 이 먼저 구현한다. **미구현 상태면 이 한 줄은 넣지 말 것**(NameError). 두 스펙 중 어느 쪽이 먼저 착수돼도 되지만, 이 줄만 단독으로 들어가면 안 된다.
- dist 단위 버그(C++ cm 를 "…m" 표기)는 프롬프트 삭제로 자연 소멸.
- **reflex 관련 기존 pytest 는 0건**(2026-08-18 확인 — `tests/` 9개 파일 grep 무매치). "정리" 할 것이 없고, 통보 전용 회귀 테스트를 **신규 작성**만 하면 된다.

### 3.6 에디터 실측 근거 (2026-08-18, UE MCP `ue_get_property`/`ue_search_assets` — 읽기 전용)

룰 수치가 실제 감지 범위·에셋과 맞는지 에디터에서 직접 확인한 결과. **BP 오버라이드로 인한 C++ 기본값 드리프트 없음** — 아래 값은 전부 C++ 기본값과 일치했다(대상: 레벨 인스턴스 `BP_SmartNPC_C_UAID_A0AD9F201D22F5EA02_1394248719`, AgentID `Moca`).

| 값 | 실측 | C++ 기본값 | 판정 |
|---|---|---|---|
| `SightRadius` | 3000 | `SmartNPC.h:103` 3000 | 일치 — 룰 근접 500cm·인사 800cm 는 감지 반경 안, 조건 성립 |
| `LoseSightRadius` | 3500 | `:106` 3500 | 일치 |
| `SightAngle` | 60 | `:109` 60 | 일치 — 인사 룰은 전방 60° 안에서만 발동됨을 전제할 것 |
| `HearingRange` | 3000 | `:112` 3000 | 일치 — Drop/UseItem 소음 룰의 유효 범위 |
| `TacticalQueryCooldown` | 6 | `NPCActionComponent.h:366` 6.0 | 일치 — §3.3.7 판정의 실측 근거 |
| EQS 에셋 | `EQS_TacticalPositions`·`EQS_Move` **2개뿐** | CLAUDE.md §4 정책 | 개수 정책 준수. 단 **이름이 문서와 다름**(§5 참조) |
| `AM_emote` | 존재, **8.33초** | — | 인사 반사로는 과함 → §4 Out of Scope |
| `AM_Think` | 존재, 4.23초 | — | 반사와 무관하나 latency hiding bark 백로그의 기존 자산 |

감지 설정은 `SmartNPC`(폰)의 `MCP|AI|Vision`/`Hearing` UPROPERTY 를 `SmartNPCAIController.cpp:83-92` 가 `SightConfig`/`HearingConfig` 로 복사하는 구조다 — 즉 튜닝 지점은 폰 쪽이고, 반사 룰의 거리 상한도 같은 값을 기준으로 잡아야 한다.

## 4. Out of Scope

감탄사 Dialogue(bark) · **인사 반사의 `Emote` 동반**(§8-② 판정: 몽타주 `AM_emote` 가 8.33초라 반사용으로 과함 — NPC 를 8초 묶음. 짧은 인사 몽타주 확보 후 재검토, DoList) · `TryReflexAction`(넉다운 물리 반사) 연계 · 새 EAction(§3) · ST 에셋 변경 · 피격 인지 반사(Flinch/Knockdown + 셀렉터 RecentHit 부스트 기커버) · VAD barge-in(별도 /spec) · latency hiding bark(별도 /spec) · Constrained Generation 잔여(별도 백로그).

## 5. 범위 (변경 파일)

- C++: `NPCActionComponent.h/.cpp`(FReflexRule·룰 배열·추첨·주입·ExecuteActionBatch 빈 배치 Mode 스킵), `SmartNPCAIController.cpp`(perception 분기에서 반사 트리거·Combat 진입), `MCPJsonUtils`(reflex_action 옵션 필드 §3.4), 구조체 위치에 따라 `Source/UE5_MCP_VR/NPC/Struct/NPCActionTypes.h`.
- Python: `main.py`(SLM 반사 제거·통보 전용화·반사 이력 메모리 기록), `schemas/envelope.py`(`reflex_action: str = ""` — report_type 과 동일 하위호환 패턴), `tests/`(통보 전용 회귀 테스트 신규).
- 문서: CLAUDE.md §8 표(Combat 진입 소유권) + **§4 EQS 에셋명 정정**(실제 에셋은 `EQS_TacticalPositions`·`EQS_Move`, CLAUDE.md 는 `TacticalPositionsQuery`·`DefaultMoveQuery` 로 오기 — 2026-08-18 MCP 확인), Memo.

### 5.1 코드 라인 앵커 (2026-08-18 `a9936e4` 기준 실측 — 구현 세션 진입점)

- `app/main.py`: `SLM_REFLEX_DANGER_THRESHOLD:52` · `_REFLEX_FACIAL:273` · `_REFLEX_PROMPT:286` · `_apply_hostile_affinity:327` · `_infer_reflex_action:344` · `_handle_slm_reflex:369` · `_handle_emergency_report:463`
- `NPCActionComponent.cpp`: `ExecuteActionBatch:239` · **무조건 `SetBehaviorMode(Batch.Mode)`:264-265** · `ResetCombatSelectorState:268-271`(§3.3.2 가드 자리) · 큐 주입 선례 `:318`(중복 가드)·`:325`·`:1256`·`:1891`
- `SmartNPCAIController.cpp`: `OnTargetPerceptionUpdated:279`(델리게이트 등록 `:68`) · Sight `:288-360` · Hearing `:361-421`(태그 파싱 `:368-379`, `bIsCombatNoise` `:405-420`) · 컨트롤러가 이미 `SetBehaviorMode` 직접 호출 `:207`,`:262`(§3.3.1 선례)
- `schemas/envelope.py`: `PerceptionData:43-51` · `EmergencyReportPayload:135-146`(`report_type` 하위호환 패턴 = `reflex_action` 동형)

## 6. 완료 기준

1. Hostile 접적 → 같은 프레임 반사 액션 주입 + Combat 진입 (`[Reflex]` 로그, SLM 미경유).
2. Drop/UseItem 소음 → Investigate/TurnTo 발동, **BehaviorMode=Common 유지**.
3. Python 서버 오프라인에서도 접적 반사 동작.
4. 빈 배치 통보가 Combat 을 되돌리지 않음(Mode 스킵 회귀).
5. pytest 그린 + 통보 전용 회귀 테스트 신규 1건 이상.
6. 룰 튜닝 = 에디터 UPROPERTY(기본값 C++ 확정, §9).
7. 반사 이력이 메모리에 기록되고 다음 replan prompt 컨텍스트에 나타남(§3.4) — plan 에 '이미 반응함' 반영 육안 확인.

## 7. 검증 (빌드 후 PIE — 사용자)

> **학습 병행 중이면 PIE 금지.** 유휴 UE 에디터가 unsloth 학습에 준 부하는 실측 +2.5%(46.16→47.31 s/step, 2026-08-18)로 무시할 만하지만 PIE 는 렌더·시뮬 부하가 별개다. GPU 여유가 수백 MB 수준이면 학습 종료 후 실행할 것.

1. 낮은 호감도 접적 → 즉시 Attack/Signal (로그로 SLM 미호출 확인).
2. Python 서버 끈 채 접적 → 반사 정상.
3. 아이템 Drop 소음 → 조사/돌아보기, Combat 미진입 확인.
4. 전투 소음 → EQS 엄폐만 발동, **반사 액션 미주입**(§3.3.7).
5. 플레이어 대화(빈 배치 통보 발생) 중 Combat 유지 확인.
6. 친화 NPC 접근 → TurnTo 인사, 30s 내 재발동 없음(쿨다운), Mode=Common 유지.

## 8. 미결

### 해소 (2026-08-18)

- ~~FReflexRule 소유 컴포넌트~~ → **`UNPCActionComponent`** (§3.2 근거).
- ~~Sight 저위험(친화 인사) 반사~~ → **룰 채택, `TurnTo` 100** (§3.2 표). `Emote` 는 몽타주 길이 문제로 제외(§4).
- ~~Hearing "Attack" 반사와 EQS 우선순위~~ → **전투 소음은 EQS 전담, 반사 비개입** (§3.3.7).

### 잔여

- **`AffinityFriendlyThreshold`/`AffinityHostileThreshold` 의 BP 오버라이드 여부를 MCP 로 직접 확인 불가** — `EditDefaultsOnly` 라 RemoteControl 이 `DisableEditOnInstance` 로 읽기를 막는다(인스턴스·CDO 경로 모두 HTTP 400). `BP_SmartNPC.uasset` 문자열 스캔에 두 이름이 없어 오버라이드 없음으로 추정하지만 확정은 아니다. 구현 세션에서 PIE 로그로 실값 1회 덤프해 확정할 것.
- 인사 반사용 **짧은 몽타주(2초 내) 확보** — 확보되면 `TurnTo 70 / Emote 30` 으로 복원 검토. 에셋 작업이라 `docs/DoList.md` 소관.
- 전투 소음 fallback `Scan`(EQS 가 쿨다운/실패로 거부한 경우 한정) — 수요 확인 후 결정(§3.3.7).
- `PerceptionTickInterval`(9.0s, `SmartNPCAIController.h:67`)·`CombatDangerThreshold`(0.5, `SmartNPCAIController.cpp:24` **file-scope `constexpr`** — UPROPERTY 아님)는 컨트롤러 소속이라 에디터 인스턴스가 없다. 반사 쿨다운 하한은 9s tick 을 기준으로 잡을 것(인사 30s 는 이 기준 충족). `CombatDangerThreshold` 를 에디터 튜닝 대상으로 올릴지는 CLAUDE.md §9 관점에서 별건.

## 9. 커밋 단위

1. `feat: C++ 척수반사 테이블 — FReflexRule·큐 주입·Combat 진입 — SmartNPCAIController·NPCActionComponent`
2. `refactor: SLM 반사 제거 — emergency_report 통보 전용화·빈 배치 Mode 스킵 — main.py·NPCActionComponent`
3. `docs: CLAUDE.md §8 + Memo` (docs 는 gitignore — Memo 는 파일 갱신만, CLAUDE.md 는 커밋)
