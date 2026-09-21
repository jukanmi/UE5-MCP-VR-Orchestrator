# W20 (2026-05-12 ~ 05-18) — Quest 3S Phase 2·TTS M1/M2·NPC AI 안정화

## 핵심
Meta Quest 3S Phase 2(Android 패키지·OpenXR·VRPawn 통합) 완료. TTS M1 사인파 스텁으로 end-to-end 파이프라인 검증 후 M2에서 실제 음성 모델 도입 시도 (VibeVoice → Piper → Coqui XTTS-v2 순서로 교체). NPC AI 대규모 안정화 — gemma SLM thinking 우회·EQS request_gen 카운터·자동 Track 제거·WebSocket race fix·.venv 정비.

## 주요 작업

### Quest 3S 포팅 Phase 2
- **Phase 2 — Android 패키지·OpenXR·VRPawn 통합** (`2eb0964`)
  - DefaultEngine.ini AndroidRuntimeSettings·OpenXR·Mobile RendererSettings
  - Package=`com.jukanmi.ue5mcpvr`, Min SDK 32 / Target 34, ASTC, OBB 통합, Vulkan + arm64
- **.gitignore 정리** — Oculus 도구·user settings·pycache (`23da386`)

### TTS M1 (사인파 스텁)
- **VR NPC·VibeVoice TTS 명세 문서 추가** (`c4de303`)
- **TTS M1 스텁** — TTSService·NPCAudioStreamComponent·tts_client (`6955415`)
- **TTS 운영 환경** — TTSServer.bat·BP_SmartNPC 컴포넌트 첨부 (`0feb220`)

### NPC AI 안정화·SLM 최적화·TTS 라우팅
- **NPC AI 안정화·SLM 최적화·TTS 라우팅 통합** (`2af9663`)
- **NPC EQS 안정화** — cooldown·SLM pre-warm·logger (`bbb9e89`)
- **WebSocket ensure 제거** — OnMessage 콜백 안 mutation 방지 (`40461bb`)

### TTS M2 (모델 교체 시도 — 3차)
- **.venv 도입** — Python 3.12, torch cu128 (sm_120 호환) (`02f777d`)
- **TTS M2 — VibeVoice-Realtime-0.5B 통합 시도** (`bab36b4`)
  - 실패: VibeVoice는 표준 transformers API 비호환 (전용 `vibevoice` 패키지 + voice .pt 임베딩 필요)
  - 폐기 후 Piper TTS로 교체 → 한국어 미지원으로 0샘플 → Coqui XTTS-v2(다국어, 한국어 포함)로 재교체

## 메모 (Memo.md Handoff Notes, 2026-05-13~16)
- **Quest Phase 2 .ini 결정 사항**: Quest 3S 단독 타깃 가정. Min 29/Target 32(원안)은 Quest 2/Pro 폴백용이라 미적용. NDK r25c·JDK 17·Android SDK 32/34는 머신별이라 .ini에 없음 — Editor에서 직접 지정. `bPackageForMetaQuest=True`가 Meta 전용 매니페스트 추가 → Quest 외 안드로이드에는 설치 안 될 수 있음.
- **TTS M1 스텁 구조**: 사인파 더미 서버 + dialogue_text 고정 문자열로 end-to-end 파이프라인만 깐 상태. UE5 NPC BP에 `NPCAudioStreamComponent` 수동 첨부 필요.
- **NPCAudioStreamComponent 지연 측정 로그**: `[T0]` PlayFromUrl → `[T1]` WS Connected +ms → `[T2]` 첫 청크 → Play() +ms. M2 도입 후 첫 음 지연 목표 < 600ms.
- **TTS dispatch 정책**: Python의 NPC_Debug/Skadi 하드코딩 폴백 제거. Python은 UE5 NPCMap 상태를 모름 → 임의 폴백 위험. 우선순위 `final_action.AgentID` → `target_npc_from_payload`. 둘 다 없으면 skip + 로그.
- **자동 Track 주입 제거**: `STTask_PrepareNextAction.cpp` 비전투 자동 Track 블록 삭제. BB.TargetActor 채워지면 매 tick "할 일 없네? Track 주입" → NPC가 플레이어 무한 추적. 비전투에서 NPC가 시야 들어와도 가만 있는 게 정상 — 추적이 필요하면 LLM이 Follow/Track 액션을 명시.
- **gemma SLM thinking 토큰 주의**: `gemma4:e4b`는 답 토큰 전에 사고 토큰을 흘리는 thinking 모델. `num_predict=20` 으로 호출하면 추론 중간 잘려 `response=""` + `done_reason="length"`. 단답형이라도 num_predict 200+ 확보. `/no_think` 또는 chat template 강제 검토.
- **gemma thinking 우회 패턴**: httpx로 Ollama `/api/generate` 직접 호출 + `raw=true` (chat template 우회) + few-shot prompt (Answer: 까지 채워주면 다음 한 단어만 생성). 기존 2200ms/289토큰 → 400ms/4토큰. `num_predict=10` + `stop=["\n"]` + `keep_alive="5m"`.
- **EQS request_gen 프로토콜**: location_decision payload에 `request_gen` 필드 추가. UE5 → Python echo → UE5 비교로 stale 응답 차단. 후보 ID가 `SAFE`/`OPTIMAL`/`AGGRESSIVE` 3개 카테고리 고정이라 신/구 응답 ID만으로 구분 불가했음. `request_gen=0`은 stale 검사 우회(레거시 호환). 다른 비동기 응답(예: TTS request_id)에서도 재사용 권장.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 05-16 | 23da386 | .gitignore 정리 |
| 05-16 | c4de303 | VR NPC·VibeVoice TTS 명세 문서 |
| 05-16 | 2eb0964 | Meta Quest 3S Phase 2 |
| 05-16 | 6955415 | TTS M1 스텁 |
| 05-16 | 2af9663 | NPC AI 안정화·SLM 최적화·TTS 라우팅 |
| 05-16 | bbb9e89 | NPC EQS 안정화 (cooldown·pre-warm·logger) |
| 05-16 | 0feb220 | TTS 운영 환경 (TTSServer.bat·BP 컴포넌트) |
| 05-16 | 40461bb | WebSocket ensure 제거 |
| 05-16 | 02f777d | .venv 도입 (Python 3.12, torch cu128) |
| 05-16 | bab36b4 | TTS M2 — VibeVoice (이후 Piper → XTTS로 교체) |
