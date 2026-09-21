# W34 (2026-08-17 ~ 08-23) — 에디터 파이썬 브리지 · llm-perf 폐기·재작성 · Stage1 v3 · 척수반사 테이블

<!-- Memo.md Done(08-18·08-19·08-21·08-22) + 커밋 14건 기반 소급 작성(2026-08-29). Memo 기재 날짜와 실제 커밋 날짜가 일부 어긋나 있어(반사 테이블 08-22 기재 → 실제 08-19-21) 커밋 표는 git 날짜를 따랐다. -->

## 핵심

밀도가 가장 높았던 주. 네 갈래가 동시에 진행됐다.

① **에디터 파이썬 브리지**(`ue_run_python`) — W31 의 MCP 서버가 프로퍼티 읽기/쓰기만 됐다면, 이번엔 에디터 안에서 `unreal` 모듈을 통째로 돌린다. 에셋 생성·컴포넌트 추가·컴파일·저장까지 왕복 검증했다. 이후 에디터 수작업의 상당 부분이 자동화 가능해진 전환점.

② **`feature/llm-perf` 폐기** — 이게 이 주의 가장 중요한 판단이다. "겹치는 개발 항목이라 쓸데없는 것 아니냐"는 의심에서 출발해 조사했더니, 중복이 아니라 **죽은 코드**인 게 진짜 문제였다. 웜업이 읽는 `llm_factory.DEFAULT_MODEL` 심볼이 Stage1/Stage2 분리 때 사라졌는데, 자체 `except` 가 `AttributeError` 를 삼켜 **로그만 남고 웜업은 영구 무효**가 되는 구조였다. 머지했어도 아무 일도 안 일어났을 코드.

더 중요한 건 **재실측이 원안의 전제를 뒤집었다**는 점이다. 12B 재로드가 11.0s(구 기록 2.8s·5.7s 둘 다 과소), Stage1 e4b 1턴이 2.6–3.1s. 즉 "Stage1 과 병렬 로드하면 cold-start 가 통째로 숨는다"는 원안 주장이 거짓이었고, 실제로 가릴 수 있는 건 3s/11s(**27%**)뿐이었다.

③ **Stage1 v3** — W33 의 3806행으로 재학습. gold 재현 **8/8** 달성.
④ **척수반사 테이블** — SLM 반사를 걷어내고 C++ 결정론 테이블로 대체.

## 주요 작업

### 에디터 파이썬 브리지 `ue_run_python` (08-18, `a9936e4`)

- RemoteControl `/remote/object/call` 로 `UPythonScriptLibrary::ExecutePythonCommandEx` 를 태우는 방식. **새 포트·프로세스 없음.**
- **왕복 검증 완료**: `BP_MCPProbe` 생성(부모 `FurnitureActor`) → `SubobjectDataSubsystem` 으로 StaticMeshComponent 추가 → compile → save → `ue_search_assets` 로 `ParentClass`·`BlueprintComponents=1` 확인 → CDO 에 `FurnitureID`/`FurnitureType` 쓰고 **다른 툴**(`ue_get_property`)로 값 읽기 성공 → 프로브 삭제.
- **막혔던 두 벽**: ① PythonScriptPlugin 이 엔진 기본 비활성 ② RemoteControl 이 `bEnableRemotePythonExecution=false` 일 때 `PythonScriptLibrary` 를 **클래스명으로 하드 차단**(`RemoteControlModule.cpp:2531`). ②의 UI 체크박스는 `Restrict Server Access` 선행 필요(editCondition).
- **여전히 불가**: K2Node 스폰·핀 연결. 파이썬에도 API 노출이 없어 **그래프 편집은 자체 C++ 에디터 플러그인 외엔 길이 없다**.
- 함정: `EditorLevelLibrary`/`EditorAssetLibrary` 는 deprecated. 신규 코드는 `unreal.get_editor_subsystem(...)` 계열을 쓸 것.

#### ⚙ 활성화 절차 (새 환경마다 재설정 필요)

브리지가 동작하려면 **에디터 UI 설정 3건**이 선행돼야 한다. 이 설정은 `Saved/Config/WindowsEditor/RemoteControl.ini` 에 저장되는데 **git 미추적**이라, 새 머신·새 클론에서는 매번 다시 해야 한다.

1. `.uproject` 에 **PythonScriptPlugin** 활성(이건 커밋 `a9936e4` 에 포함 — git 추적됨)
2. Project Settings > Plugins > Remote Control > Security
   - `Restrict Server Access` 켜기 (아래 항목의 editCondition 선행)
   - `Enable Remote Python Execution` 켜기
3. WebSocket 바인딩 `0.0.0.0` → `127.0.0.1`

근거·레시피 상세는 `tools/ue_mcp/README.md`.

### `feature/llm-perf` 폐기 + SPEC 재작성 (08-19)

- 브랜치 삭제, `77a53d2` 는 태그 `archive/llm-perf` 로만 보존. 재작성 스펙 = `docs/SPEC_llm_perf.md`.
- **폐기 사유(치명)**: 위 핵심 참조 — stale 심볼 + 예외 삼킴으로 영구 무효.
- **재실측 결과**(Ollama 직접 호출):
  | 항목 | 구 기록 | 실측 |
  |---|---|---|
  | 12B 재로드 | 2.8s / 5.7s | **11.0s** |
  | 12B 최초 콜드 | — | 36.9s |
  | Stage1 e4b 1턴 | — | 2.6–3.1s |
  - 근본 해법은 `keep_alive` 정책(12B 상주)이고, 그건 VRAM 결정에 묶여 있다.
- **웜업 호출 형태도 교체** — `num_predict=1` 생성은 모델 상주 중에도 3.7s(prompt_eval 3.4s 순낭비). **prompt 키 없는 로드콜은 342ms** — 11배 차이.
- **범위 분리** — replan 훅 + 요약 디퍼 = `SPEC_llm_perf`, emergency 훅 = `SPEC_reflex_table` §3.5 로 이관. 두 스펙이 `_handle_emergency_report` 를 동시에 고치는 충돌을 피하기 위함.

### 척수반사 테이블 (08-19~08-21, `3868f58`·`9d2f10c`·`f570db4`·`560f40b`)

- **C++**: `FReflexRule` · `TryReflexReact` · 큐 주입 · Combat 진입(`SmartNPCAIController`·`NPCActionComponent`).
- **Python**: SLM 반사 제거 → `emergency_report` **통보 전용화**(`main.py`·`envelope`·`MCPJsonUtils`), `reflex_action` 메모리 기록.
- 빈 배치 Mode 가드 포함. 빌드 exit 0. PR #23 Develop 머지.
- 잔여: PIE 5항목.

### Stage1 e4b v3 학습·배포 (08-21, `06e9884`·`00fe3e8`·`868b258`·`cb8629d`)

- 3806행으로 3에폭 재학습.
- **eval(v2 vs v3)**: gold 재현 6/8 → **8/8**, held-out 일반화 0/6 → **2/6**. v2 의 action JSON split 버그(`["S","l","e","e","p"]`)가 v3 에서 대부분 해소.
- **배포**: `deploy/out_v3_gguf/gemma-4-e4b-it.Q4_K_M.gguf` → Ollama `gemma4-e4b-dialogue-v3` 등록 → `llm_factory.py` `gemma4_slm` v2→v3 전환.
- 파이프라인 소스 14파일 추적 등록 + `.gitignore` 를 `**/unsloth_compiled_cache/` 로 전 경로 적용.

### llm-perf 재구현 (08-22, `38268c7`·`af29fa8`·`b09d2c2`·`bdb94c4`)

폐기된 원안을 SPEC 기준으로 다시 구현. **성능 2건 + 동시성 버그 2건**.

- **12B 선제 웜업 + 요약 idle 디퍼**(`bdb94c4`) — replan 확정 직후 빈 프롬프트 로드콜(342ms)로 선제 로드, 스로틀 30s. 메모리 요약은 인라인 → 10s idle 디퍼로 옮겨 Stage1·TTS 와의 GPU 경합 제거. pytest 52 pass.
- **emergency 전투 진입 웜업 훅**(`38268c7`) — danger 게이트 통과 직후 발사. replan 훅보다 리드타임이 길어 11s 콜드 로드가 완전히 숨을 가능성이 있는 유일 지점.
- **호감도/메모리 캐시 동시성 P0**(`b09d2c2`) — 무잠금 dict 순회로 인한 `RuntimeError` · 락 중간 해제로 인한 lost update · check-then-set 비원자화로 `ConversationMemory` 인스턴스 2개 생성 → 대화 유실 · **최대 30초 블로킹 LLM 호출이 락을 쥔 채 실행**되던 것 등 5건.
- **world state 캐시 NPC별 분리**(`af29fa8`) — 전역 캐시 1개라 마지막에 보낸 NPC 의 payload 가 덮어써 **NPC A 가 NPC B 의 perception 으로 추론**하던 지식 격리 붕괴. `owner_agent_id → payload` 맵으로 분리.

## 메모

- **"중복 같다"는 의심에서 죽은 코드가 나왔다** — 코드 리뷰에서 겹쳐 보이는 것을 발견하면 중복 여부만 보지 말고 **그게 실제로 실행되는지**를 확인할 것. 이 건은 `except` 가 예외를 삼켜 로그만 남기고 있었다.
- **성능 수치는 재실측할 것** — 구 기록 2.8s·5.7s 가 둘 다 실제(11.0s)보다 과소였고, 그 위에 세운 "cold-start 를 통째로 숨긴다"는 설계 전제가 통째로 무너졌다.
- **웜업은 생성이 아니라 로드콜로** — `num_predict=1` 이라도 prompt 를 주면 prompt_eval 이 3.4s 순낭비된다. prompt 키 자체를 빼면 342ms.
- **K2Node 편집은 여전히 불가** — 블루프린트 그래프 노드 작업은 MCP·파이썬 어느 쪽으로도 안 된다. 사용자 수작업이 유일한 길(DoList 1-0b).

## 커밋

| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 08-18 | `a9936e4` | feat: 에디터 파이썬 브리지 ue_run_python — ue_mcp·uproject |
| 08-18 | `e2b957b` | feat: Stage1 데이터 파이프라인 — intent 테이블·규칙 생성기·Sonnet speech — synth/ |
| 08-19 | `3868f58` | feat: C++ 척수반사 테이블 — FReflexRule·큐 주입·Combat 진입 |
| 08-19 | `9d2f10c` | refactor: SLM 반사 제거 — emergency_report 통보 전용화 |
| 08-19 | `f570db4` | docs: SLM_REFLEX_DANGER_THRESHOLD 잔재 네이밍 주석 — main.py |
| 08-21 | `00fe3e8` | chore: gitignore unsloth_compiled_cache 전체 경로 적용 |
| 08-21 | `06e9884` | chore: finetune 파이프라인 소스 추가 — synth 유틸·dataset_editor·eval |
| 08-21 | `560f40b` | Merge pull request #23 (feature/reflex-table) |
| 08-21 | `868b258` | feat: Stage1 SLM v2→v3 전환 — llm_factory |
| 08-21 | `cb8629d` | Merge pull request #22 (feature/finetune) |
| 08-22 | `38268c7` | perf: emergency 전투 진입 시 12B 선제 웜업 훅 추가 — main.py |
| 08-22 | `af29fa8` | fix: world state 캐시를 NPC별로 분리 — main.py |
| 08-22 | `b09d2c2` | fix: 호감도/메모리 캐시 동시성 P0 — db_manager·memory_manager·dialogue |
| 08-22 | `bdb94c4` | perf: 12B 선제 웜업 + 메모리 요약 idle 디퍼 — main.py·memory_manager |

## 사용자 작업 보충 (DoList Done 이관, 2026-09-13)

- 08-22 **친화 반사 = TurnTo 100 확정**(1-2) — 짧은 인사 몽타주 미확보로 TurnTo 100 단독 유지.
- 08-22 `origin/revert-18-feature/combat-selector` stale 원격 브랜치 삭제(3).
