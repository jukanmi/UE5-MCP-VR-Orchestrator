# W18 (2026-04-28 ~ 05-04) — EQS 파라미터화·LLM 라우팅 다층화·디버그 대시보드

## 핵심
EQS 스코어링을 코드 하드코딩에서 UPROPERTY 파라미터화로 전환(에디터 튜닝 가능). LLM을 importance별로 다층 라우팅(normal/high/core). 로컬 디버그 대시보드(`/debug`) 구축으로 호감도/상태를 브라우저에서 확인 가능.

## 주요 작업
- **code-review-graph MCP 서버 설치** — 의존성 그래프 기반 코드 리뷰 도구 (`96fe62d`)
- **BTTask_NotifyActionCompleted 레거시 클래스 제거** — BT_NPC 노드 정리·`State.Condition.Dead` 태그 수정 (`07356c7`)
- **로컬 디버그 대시보드 + LLM 라우팅 다층화** — `debug.html`·`main.py`·`db_manager`·`llm_factory`·`dialogue` (`75618b4`)
- **NPC 인식·액션 큐·EQS 안정화** — `SmartNPCAIController`·`NPCActionComponent`·EQS 에셋 (`898ec00`)
- **데드코드 3건 제거** — `world_constants`·`memory_manager`·`rag_utils` (`c2de6cc`)
- **EQS 스코어링 파라미터화·타임아웃·폴백 개선** — `NPCActionComponent` + EQS 에셋 (`104a9e7`)
- **LLM 라우팅 high→qwen3:8b + 페르소나 절대경로·시작 검증** — `llm_factory`·`dialogue`·`main` (`b977288`)
- **Claude 퍼미션 설정 업데이트** — `settings.local.json` (`d93fee0`)
- **EQS-Improvement → Develop 머지** (`2fc33ee`)

## 메모
- **LLM 모델 라우팅 룰**: normal→`gemma4:e4b`, high→`gemma4:12b`, core→`gemma4:26b`. 모델 변경 시 `dialogue.py`/`main.py`/`debug.html` 세 곳을 동시에 맞출 것 (Memo §LLM 모델 라우팅).
- **EQS Named Parameter 바인딩 위치**: Generator의 Search Radius는 에디터에서 바인딩 불가, **Test 노드의 Score Factor**에만 가능. C++ `SetFloatParam("DistanceWeightParam"/"CoverWeightParam")`는 Test 가중치에서만 의미 (Memo §EQS Named Parameter 바인딩 위치).
- **EQS 에디터 미완 작업**: DistanceWeightParam/CoverWeightParam → Test Score Factor, SafeDistance → Distance Filter Min, AggressionWeightParam → TacticalPositionsQuery Inverse Distance Test에 Named Parameter 바인딩 필요 (Memo §EQS 에디터 작업 미완).
- **code-review-graph 오탐 패턴**: anonymous namespace 함수(EvalSafeScore 등), FastAPI 라우트, LangGraph 노드, C++ 매크로(UE5_MCP_VR_API)는 dead_code로 잘못 잡힘 (Memo §code-review-graph 오탐 패턴).

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 05-02 | 96fe62d | code-review-graph MCP 서버 설치 |
| 05-02 | 07356c7 | BTTask_NotifyActionCompleted 제거 + 태그 수정 |
| 05-02 | 75618b4 | 로컬 디버그 대시보드 + LLM 다층 라우팅 |
| 05-02 | 898ec00 | NPC 인식·액션 큐·EQS 안정화 |
| 05-02 | c2de6cc | 데드코드 3건 제거 |
| 05-02 | 104a9e7 | EQS 스코어링 파라미터화 |
| 05-02 | b977288 | LLM high→qwen3:8b + 페르소나 절대경로 |
| 05-02 | d93fee0 | Claude 퍼미션 설정 |
| 05-02 | 2fc33ee | EQS-Improvement → Develop 머지 |
