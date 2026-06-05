# TTSService (OpenVoice v2 + MeloTTS)

NPC 대사를 **감정 음색**으로 합성하는 TTS 서버. MeloTTS로 기본 발화(KR/EN)를 만들고 OpenVoice v2 `ToneColorConverter`로 감정별 레퍼런스 음색을 zero-shot으로 입힌다.

> 전체 흐름은 [루트 README](../../README.md), TTS 로드맵은 `docs/ROADMAP.md` §1.

## 실행

```powershell
# 프로젝트 루트에서
python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
```

- 의존성: `openvoice`, `melo`(MeloTTS), `torch`, `fastapi`, `uvicorn`, `soundfile`.
- 기동 시 ToneColorConverter·MeloTTS(KR/EN)·base SE 로드 + `voice_map.yaml`의 모든 NPC 감정 target SE를 **사전 추출**(디스크 캐시 재사용).

## 합성 흐름

```
MeloTTS(text → base wav) → ToneColorConverter(base SE → target SE 변환) → 결과 wav → s16le 청크 스트리밍
```

1. **REST 등록** `POST /v1/tts/synthesize`:
   ```json
   { "text": "...", "voice_id": "Skadi", "emotion": "Angry", "trace_id": "<msg_id>" }
   ```
   응답: `{ "request_id": "tts_<trace_id>", "ws_url": "ws://127.0.0.1:8001/ws/tts/stream/<id>", "sample_rate": 16000, "channels": 1 }`
   - `emotion` → `resolve_voice_meta(voice_id, emotion)` → 감정별 ref WAV·speed (`voice_map.yaml`).
   - `trace_id`(발원 msg_id)를 `request_id`로 상속 → LLM↔TTS↔UE 로그가 `[trace=...]`로 연결.
2. **WS 스트리밍** `ws://.../ws/tts/stream/{request_id}`: `audio_chunk`(base64 pcm_s16le 100ms) 순차 → 마지막 `completed`.

## SynthesizeRequest

| 필드 | 기본 | 비고 |
| :--- | :--- | :--- |
| `text` | — | 합성 텍스트 |
| `voice_id` | `default` | NPC id (voice_map.yaml 키) |
| `emotion` | `neutral` | FacialState 9종 — `normalize_emotion`으로 매핑 |
| `speaking_rate` / `pitch` | 1.0 / 0.0 | |
| `sample_rate` | 16000 | |
| `output_format` | `pcm_s16le` | |
| `language` | 자동 | 미지정 시 텍스트로 KR/EN 감지 |
| `trace_id` | `""` | 발원 msg_id 상속 |

## voice_map.yaml

```yaml
npcs:
  Skadi:
    emotions:
      Angry: { ref: Skadi_angry, speed: 1.0 }
      Happy: { ref: Skadi_happy, speed: 1.05 }
```
- `emotions.<E>` → ref(감정별 레퍼런스 WAV, `base_voices/`)·speed.
- 레퍼런스 없으면 MeloTTS로 6초 샘플 자동 생성.

## 성능·견고성

- **target SE 캐시**: 부팅마다 재추출 방지 — 디스크(`_processed/*.se.pth`)+메모리. 손상 캐시는 삭제 후 재추출.
- **글자 없는 대사**("...")는 CognitiveEngine 측에서 TTS 스킵(자막만).
- 클라이언트(`tts_client.py`) 0.2s 백오프 1회 재시도, 실패 시 자막 폴백.
- 기타: `GET /health`, `POST /api/preview`(미리듣기), `POST /api/reload_voices`.

## 기타 엔드포인트

- `GET /health` — 모델 로드 상태 + 캐시된 voice 목록.
- `POST /api/voice_map` / `POST /api/reload_voices` — voice_map 갱신·재적재.
