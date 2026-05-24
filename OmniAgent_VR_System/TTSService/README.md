# TTSService (M1 Stub)

명세서 §5.3 / `vibevoice-dev-spec.md` §6 API 규격을 따르는 **더미 TTS 서버**.
GPU 없이 사인파를 pcm_s16le 청크로 스트리밍한다. M2에서 VibeVoice 본체로 교체.

## 실행

```powershell
# 프로젝트 루트에서
python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
```

의존성: `fastapi`, `uvicorn`, `websockets` (CognitiveEngine과 공유).

## 수동 테스트

1. REST로 합성 요청 등록:
   ```powershell
   curl -X POST http://127.0.0.1:8001/v1/tts/synthesize `
     -H "Content-Type: application/json" `
     -d '{"text":"테스트 발화","voice_id":"ko_guard_01","emotion":"serious"}'
   ```
   응답:
   ```json
   {
     "request_id": "tts_xxxxxx",
     "ws_url": "/ws/tts/stream/tts_xxxxxx",
     "sample_rate": 16000,
     "channels": 1
   }
   ```
   > `ws_url`은 **host 없는 경로**만 반환한다. UE5 측이 `Config/DefaultGame.ini`
   > `[OmniAgent] ServerHost`·`TTSPort`를 앞에 붙여 완전한 URL을 만든다.
   > 수동 테스트 시에는 `ws://127.0.0.1:8001` 을 직접 붙여 연결할 것.

2. `ws_url`에 WebSocket 연결 → `audio_chunk` 메시지가 순차적으로 도착, 마지막에 `completed`.

## 청크 메시지 포맷

```json
{
  "type": "audio_chunk",
  "request_id": "tts_xxxxxx",
  "sequence": 0,
  "sample_rate": 16000,
  "channels": 1,
  "audio_base64": "<base64 pcm_s16le 100ms>",
  "is_last": false
}
```

완료:
```json
{ "type": "completed", "request_id": "tts_xxxxxx", "total_duration_ms": 1200 }
```

## 파라미터

- `sample_rate` (default 16000)
- `text` 길이에 비례해 800ms~4000ms 출력
- 청크 길이 100ms (= 1600 samples @ 16kHz)
- 마지막 200ms는 fade-out 처리해 클릭 방지

## 다음 단계 (M2)

- 본 서버를 **VibeVoice-Realtime-0.5B** 추론 서버로 교체.
- API/메시지 포맷은 동일하게 유지(`vibevoice-dev-spec.md` §6).
- 모델 워밍업 후 첫 청크 지연 ~300ms 목표.
