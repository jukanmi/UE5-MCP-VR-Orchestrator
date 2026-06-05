# ASRService (faster-whisper)

플레이어 음성(Push-to-talk)을 받아 텍스트로 변환하는 **음성 인식 서버**.
UE5 `UVoiceInputComponent` 가 마이크 PCM 을 WS 로 스트리밍 → transcript 회신 →
`AVRPawn::HandleVoiceTranscript` → `UNPCManager::SendPlayerDialogue` → CognitiveEngine(prompt).

- 모델: faster-whisper `large-v3` (`cuda` / `float16`) — 기동 시 1회 로드 + 0.5s 무음 프리워밍.
- 동시 요청은 `asyncio.Lock` 으로 GPU 추론 직렬화(WhisperModel 비-스레드세이프).
- 무한 스트리밍 OOM 방지: 오디오 버퍼 상한 ~11.5MB(48kHz·16bit·120s).

## 실행

```powershell
# 프로젝트 루트에서
python -m uvicorn OmniAgent_VR_System.ASRService.server:app --host 127.0.0.1 --port 8002
```

의존성: `faster-whisper`, `fastapi`, `uvicorn`, `numpy`, `soxr`. (CUDA 환경 필요 — CPU 시 `WHISPER_DEVICE`/`WHISPER_COMPUTE` 를 `cpu`/`int8` 로 조정.)

> UE5 측 엔드포인트 기본값: `UVoiceInputComponent::AsrServerURL = ws://127.0.0.1:8002/ws/asr/stream`.
> 포트를 바꾸면 양쪽을 함께 맞출 것.

## 프로토콜 (WS `ws://127.0.0.1:8002/ws/asr/stream`)

1. **start** (JSON 텍스트) — 세션 시작:
   ```json
   { "type": "start", "request_id": "asr_xxx", "target_npc_id": "Skadi",
     "player_id": "Player_1", "sample_rate": 48000, "language": "KR" }
   ```
   → 서버 `{ "type": "ready", "request_id": "asr_xxx" }`
2. **오디오** — `s16le` mono PCM 을 **바이너리 프레임**으로 연속 전송 (리샘플은 서버가 16kHz 로 처리).
3. **end** (JSON 텍스트) `{ "type": "end" }` → 서버가 누적 버퍼를 인식해 회신:
   ```json
   { "type": "final", "request_id": "asr_xxx", "target_npc_id": "Skadi",
     "player_id": "Player_1", "transcript": "안녕하세요", "duration_ms": 1800, "language": "KR" }
   ```

- `language` 힌트: `KR/KO→ko`, `EN/US→en`, `JP/JA→ja`, 미지정/미매핑은 자동감지.
- `target_npc_id`/`player_id` 는 서버가 그대로 echo — UE5 가 발화 대상 라우팅에 사용.

## 기타 엔드포인트

- `GET /health` — `{ "status": "ok"|"loading", ... }` (모델 로드 상태).
- `POST /api/asr/stub_transcribe` — WS 없이 transcript 테스트용 스텁.
