# W23 (2026-06-02 ~ 06-08) — TTS 감정 운영강화 · ASR 배선 확정 · RAG 지식 파이프라인

## 핵심
TTS 감정 음색 연결(M3)을 `feature/TTS-Emotion` 으로 머지하고 운영 강화(0.2s 재시도, msg_id→request_id trace)를 얹었다. ASR 음성입력 파이프라인은 코드상 이미 완결돼 있어 **배선을 검증**하고 빠진 운영 조각(실행 README, 프리워밍 None 가드, whisper env 설정)을 마무리했다. RAG는 "No documents found for Skadi" 원인이 코드가 아니라 **지식 콘텐츠 부재**임을 확인하고, chunk_category 메타 태깅·재빌드 CLI·작성 템플릿까지 파이프라인을 완성(실제 lore 작성은 사용자 몫). Gemini 자동리뷰를 PR마다 1~2라운드 돌려 방어코드를 대폭 보강했다.

## 주요 작업

### TTS (감정 + 운영강화) — PR #5
- **감정 음색 연결 (M3)** (`c264b67` 외, `feature/TTS-Emotion` 머지 `0b1acc3`)
  - FacialState→emotion→ref/speed, MeloTTS→ToneColorConverter zero-shot, target SE 디스크/메모리 캐시+사전적재, `[Facial:]` 태그 우선, 글자없는 대사 스킵
- **재시도 + trace 체인** (`e28a674`)
  - `tts_client` 0.2s 백오프 1회 재시도(실패 시 자막 폴백), `msg_id→request_id` 상속 + `[trace=]` 로그태그
- 구 `feature/TTSExtend`(미머지·옛 base)는 cherry-pick 충돌·고유커밋 4개 손실 감수하고 폐기. ROADMAP §1.1 의 "TTSExtend 포팅" 서술 폐기.

### ASR (배선 확정) — PR #7
- **배선 검증** (`feature/ASR-Wiring` 머지 `5803c1e`): IA_VoiceInput→StartTalking→캡처→ASR WS(8002)→final→`OnTranscriptReady`→`HandleVoiceTranscript`→`SendPlayerDialogue`→BuildPrompt→Cognitive. 포트·PromptPayload·파싱 전부 일치 = 코드 완결.
- **운영 마무리** (`7689189`, `5969836`): `ASRService/README.md` 신규, 프리워밍 `_model` None 가드, whisper `DEVICE/COMPUTE/MODEL_SIZE` env-readable.

### RAG (지식 파이프라인) — PR #6
- **파이프라인** (`201de58`, `feature/RAG-Knowledge` 머지 `3f78963`): 서브폴더(lore/persona/history)→`chunk_category`+`npc_id` 메타 태깅, 재빌드 CLI `build_knowledge.py`(`--agent`/`--all`/`--list`, `--use-cache`), 작성 가이드/템플릿 `knowledge_template/`
- 폴더 정합: `elera`→`elara` 통일, stale vectorstore(elara/james) 삭제. end-to-end 스모크 검증.

### Gemini 리뷰 반영 (방어코드)
- LLM 파싱: snake_case 키 폴백, 폴백 대사 `[Action:]` 누수 차단, 따옴표 없는 대사 Dialogue 폴백 (`53fa87b`, `d30e0f6`)
- 서버: ASR `WhisperModel` `asyncio.Lock`(동시추론 직렬화)·버퍼상한(OOM)·홀수바이트 가드, TTS 비-JSON 재시도·SE 캐시 손상 복구·커넥션 풀(TTFA) (`3327fd0`, `7488f16`)
- UE: VoiceInput WS 콜백 `TWeakObjectPtr`(use-after-free), `StreamSampleRate` atomic, STTask `Self` null 가드, TryGetStringField 어설션 방지 (`7251d77`, `09a9986`, `1879dd2`, `1655d00`)

## 메모 (Memo.md Handoff Notes, 2026-06-05)
- **TTSExtend 폐기**: 미머지 고유커밋 4개(ChatWidget 제거, ASR 스텁, TTS base_voices, NPC ambient 모드) 있었으나 옛 base 기반 cherry-pick 충돌 + 콘텐츠 superseded 판단으로 강제삭제. ambient 모드만 추후 수동 포팅 여지.
- **RAG 콘텐츠는 창작 영역**: 페르소나 yaml 은 전부 기본 스텁(james 제외). 실제 lore 는 게임 세계관이라 AI 가 임의 작성 안 함 — 파이프라인+템플릿까지만 제공.
- **memory_manager.py ≠ RAG 인제스트**: 대화 히스토리(JSON) 관리용. PDF 설계서가 둘을 혼동.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 06-05 | `0b1acc3` | Merge feature/TTS-Emotion — TTS 감정+운영강화 |
| 06-05 | `e28a674` | TTS 재시도+trace 체인 |
| 06-05 | `3f78963` | Merge feature/RAG-Knowledge — RAG 지식 파이프라인 |
| 06-05 | `201de58` | RAG 지식 파이프라인 (chunk_category·CLI·템플릿) |
| 06-05 | `5803c1e` | Merge feature/ASR-Wiring — ASR 배선 확정·README·env |
| 06-05 | `7689189` | ASR 프리워밍 가드 + 실행 README |
| 06-05 | `3327fd0` | 서버 동시성/풀링 (ASR Lock, TTS 커넥션 풀) |
| 06-05 | `7251d77` | VoiceInput 스레드 안전성 (atomic, 소켓 가드) |
