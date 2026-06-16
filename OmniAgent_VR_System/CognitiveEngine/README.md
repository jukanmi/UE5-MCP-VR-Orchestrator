# OmniAgent Cognitive Engine

UE5 VR 게임용 NPC 인지 서버. FastAPI + LangGraph 멀티 에이전트로 플레이어 입력을 처리하고, 전략 대화 생성·게임 규칙 검증·긴급 반사(SLM)·전술 위치 결정(EQS)·감정 TTS 트리거를 수행한다.

> 전체 시스템 개요는 [루트 README](../../README.md), 상세 아키텍처는 `docs/index.html` 참조.

## 실행

```powershell
# 프로젝트 루트에서
python -m uvicorn OmniAgent_VR_System.CognitiveEngine.app.main:app --port 8000
```

- WebSocket: `ws://127.0.0.1:8000/ws/llm` (단일 채널 — emergency_report 는 내부에서 SLM Reflex 로 자동 라우팅)
- 디버그 대시보드: `http://127.0.0.1:8000/debug`
- 의존성: `pip install -r requirements.txt`
- **LLM**: 로컬 Ollama (`gemma4:e4b`/`qwen3:8b`/`gemma4-12b` importance 라우팅). 클라우드 API 미사용.

## LangGraph 파이프라인

```
prompt 수신
  → interface_input   (UE5 GesPrompt → 자연어 컨텍스트, 지시대명사 해소)
  → supervisor        (상황별 에이전트 라우팅)
  → dialogue          (persona·RAG·memory·affinity 반영 in-character 응답, [Action:]/[Facial:] 태그)
  → interface_output  (자연어 → ActionBatch 구조 파싱)
  → rules             (수치 클램핑·규칙 검증)
  → ModeActionRequest 반환
```

- **SLM Reflex**: `emergency_report`(danger ≥ 0.5)는 LangGraph 우회, 단일 경량 SLM 호출로 0.5초 내 전투/회피 ActionBatch 생성 (`main.py::_handle_slm_reflex`).
- **location_decision**: EQS 후보(SAFE/OPTIMAL/AGGRESSIVE) 중 택일을 few-shot raw 프롬프트로 빠르게 결정 (`main.py::_handle_location_decision`). LLM 실패·stale 패킷도 드랍하지 않고 `_location_decision_fast_path` 폴백 결과(`location_decision_result` + request_gen echo)를 반환 — generic drop 응답은 UE5 가 라우팅하지 못해 WaitingLLM 이 타임아웃까지 고착되기 때문.
- **TTS 트리거**: Dialogue 액션의 `text` + `FacialState` → `_trigger_dialogue_audio` → TTSService 합성 요청, `NpcAudioResponse(ws_url)` 를 UE5 로 푸시.

## 핵심 모듈

| 경로 | 역할 |
| :--- | :--- |
| `app/main.py` | WebSocket 진입점, 타입별 핸들러, SLM/location_decision, TTS dispatch |
| `app/graph.py` | LangGraph 워크플로 정의 |
| `app/agents/interface_input.py` · `interface_output.py` | UE5 ↔ 자연어 변환 |
| `app/agents/supervisor.py` | 라우팅 |
| `app/agents/subgraphs/dialogue.py` · `rules.py` | 대화 생성 / 규칙 검증 |
| `app/schemas/envelope.py` | MessageEnvelope·payload (EEnvelopeType) |
| `app/schemas/actions.py` | ActionBatch·GameAction·클램핑 |
| `app/clients/tts_client.py` | TTSService 호출(재시도·trace·커넥션 풀) |
| `app/utils/rag_utils.py` · `build_knowledge.py` | NPC별 FAISS RAG + 재빌드 CLI |
| `app/utils/memory_manager.py` | NPC 대화 히스토리(JSON, 토큰예산 요약) |
| `app/utils/db_manager.py` | affinity(호감도) DB |
| `app/agents/knowledge_template/` | RAG 지식 작성 가이드/템플릿 |

## 데이터 계약 (요약)

- 수신 `prompt.payload`: `player_id`, `voice_transcript`, `target_npc_id`, `gestures[]`, `stats`, ...
- 송신: `ModeActionRequest { Mode, ActionBatches{ AgentID: ActionBatch } }`.
- `GameAction`: `ActionType`, `FacialState`(9종), `Parameters`(snake_case). Pydantic V2 검증 + 데미지 클램핑(MAX_DAMAGE=100).
- JSON 키 규칙: payload 내부 snake_case, 최상위 PascalCase (CLAUDE.md §1).

## 모델 라우팅

`importance` → 모델: `normal` gemma4:e4b / `high` qwen3:8b / `core` gemma4-12b (= hf.co/mradermacher/Gemma-4-12B-OBLITERATED-GGUF:Q4_K_M 별칭, `ollama cp` 로 생성).
변경 시 `dialogue.py`·`main.py`·`debug.html` 세 곳을 함께 맞출 것 (Memo Handoff).
gemma e/p 시리즈는 thinking 모델 — 단답이라도 `num_predict` 충분히 확보 또는 raw 모드 사용.

## 디버깅

- `GET /` health, `GET /debug` 대시보드(NPC 말 걸기 → prompt→ActionBatch 확인).
- `[trace=<msg_id>]` 로그가 prompt→TTS→UE 를 한 줄로 연결.
- RAG `No documents found` → `knowledge/<npc>/{lore,persona,history}/*.md` 작성 후 `python -m app.utils.build_knowledge --all`.
