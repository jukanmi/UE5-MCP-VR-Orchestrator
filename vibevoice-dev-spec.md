# VibeVoice 기반 로컬 TTS 개발 명세 및 가이드라인

## 1. 문서 목적
- 이 문서는 Microsoft VibeVoice / VibeVoice-Realtime-0.5B 모델을 로컬 GPU 환경에서 구동하고, 게임(특히 VR NPC) 및 애플리케이션에 통합하기 위한 **개발 명세와 가이드라인**을 제공합니다.
- 대상 독자:
  - 로컬 LLM/TTS 인프라를 직접 구축하려는 엔지니어
  - 언리얼 엔진, Unity, 웹 클라이언트 등에서 TTS를 연동하려는 클라이언트 개발자

---

## 2. 목표 및 요구 사항

### 2.1 기능적 목표
- 텍스트 입력을 받아 **자연스럽고 감정 표현이 풍부한 음성**으로 변환.
- 실시간 상호작용이 필요한 VR/게임 환경에서 **첫 음성 출력 지연 ~300ms 수준**을 목표로 함 (VibeVoice-Realtime-0.5B 기준).
- 다국어·다화자 TTS 확장 가능성을 고려한 API 설계.

### 2.2 비기능적 목표
- 완전 로컬 환경에서 동작 (인터넷 연결 없이도 서비스 가능).
- GPU 자원을 효율적으로 사용하고, LLM·게임 엔진 등과 **동일 머신에서 공존**할 수 있게 설계.
- 모듈형 아키텍처: VibeVoice 외 다른 TTS 엔진으로도 쉽게 교체 가능.

---

## 3. 하드웨어/성능 요구사항

### 3.1 권장 하드웨어 (VibeVoice-Realtime-0.5B 기준)
- GPU
  - NVIDIA RTX 30/40 시리즈 (예: 3060, 3070, 4070, 4080, 4090 등)
  - 최소 8GB VRAM, **실제 권장 12GB 이상** (LLM + TTS 동시 구동을 고려)
- CPU
  - 4코어/8스레드 이상 (예: Intel 12세대 i5/i7, Ryzen 5/7)
- 메모리
  - 최소 16GB, 권장 32GB (LLM, 게임 엔진, 오케스트레이터 프로세스 동시 실행)

### 3.2 성능 지표(Target)
- TTS 인퍼런스 지연(첫 음성 청크 도달 시간)
  - 목표: 약 300ms 전후 (로컬 RTX 30/40 시리즈 기준)
- 출력 속도
  - 짧은 문장(한두 문단)의 경우, 전체 음성이 1~2초 내로 생성 시작
- 동시 처리
  - 초기 버전: 1–2 스트림 동시 처리
  - 확장 목표: 4–8 스트림 동시 처리 (멀티 NPC 또는 멀티 클라이언트 환경)

---

## 4. 소프트웨어 스택

### 4.1 기본 환경
- OS: Linux (우분투 22.04+ 권장) 또는 Windows 11
- GPU 드라이버 및 CUDA: NVIDIA 공식 드라이버 + CUDA 12.x 이상 (PyTorch 지원 버전)
- Python: 3.10 이상
- 패키지 관리: `pip` 또는 `conda`

### 4.2 필수 라이브러리 (예시)
- PyTorch 및 CUDA 지원 빌드
- VibeVoice 모델 로딩용 라이브러리 (Hugging Face Transformers 또는 공식/비공식 래퍼)
- FastAPI / Flask / Node 등 API 서버 프레임워크
- WebSocket 서버 (예: `websockets`, `uvicorn[standard]`, `fastapi-websocket` 등)

### 4.3 선택 사항
- Docker / Docker Compose를 이용한 컨테이너 배포
- 프롬프트/음성 설정 관리용 데이터베이스 (예: SQLite, PostgreSQL)

---

## 5. 시스템 아키텍처 개요

### 5.1 구성 요소
1. **TTS 서버 (VibeVoice Service)**
   - VibeVoice/VibeVoice-Realtime-0.5B 모델 로딩
   - HTTP/REST 및 WebSocket 기반 TTS API 제공
2. **오케스트레이터 (선택)**
   - LLM, ASR, 게임 엔진과의 중계 및 비즈니스 로직 처리
   - 예: VR NPC 시스템에서 `LLM → TTS → UE5` 파이프라인 조립
3. **클라이언트**
   - 언리얼 엔진, Unity, 웹 앱, 데스크톱 앱 등
   - 텍스트/메타데이터를 API로 전송하고 반환된 오디오 스트림 재생

### 5.2 데이터 흐름 (예시)
1. 클라이언트가 텍스트 + 옵션 (언어, 목소리, 감정, 속도)을 TTS 서버로 전송
2. TTS 서버에서 VibeVoice 모델로 음성 인퍼런스 실행
3. 스트리밍 모드일 경우, 생성된 오디오 청크를 순차적으로 클라이언트로 전송
4. 클라이언트는 수신 즉시 재생을 시작해 체감 지연을 최소화

---

## 6. TTS API 명세 (REST + WebSocket)

### 6.1 REST TTS API (단발 요청)

**Endpoint**
- `POST /v1/tts/synthesize`

**Request Body (JSON)**
```json
{
  "text": "안녕하세요, 오늘은 무엇을 도와드릴까요?",
  "voice_id": "ko_female_01",
  "language": "ko-KR",
  "emotion": "neutral",          
  "speaking_rate": 1.0,
  "pitch": 0.0,
  "seed": 42,
  "output_format": "wav"          
}
```

**Response (성공 시)**
- `Content-Type: audio/wav` (또는 `audio/ogg`, `audio/mpeg` 등 설정값에 따라 변경)
- Body: 바이너리 오디오 데이터

**Response (에러 시)**
```json
{
  "error": {
    "code": "MODEL_ERROR",
    "message": "Failed to generate audio.",
    "details": {
      "trace_id": "..."
    }
  }
}
```

### 6.2 WebSocket 스트리밍 TTS API

**Endpoint**
- `GET /ws/tts/stream`

**초기 메시지 (클라이언트 → 서버)**
```json
{
  "type": "tts_request",
  "request_id": "npc_guard_01_line_001",
  "text": "이 구역은 통행이 금지되어 있다.",
  "voice_id": "ko_male_01",
  "language": "ko-KR",
  "emotion": "serious",
  "speaking_rate": 1.0,
  "output_format": "pcm_s16le"
}
```

**스트리밍 응답 (서버 → 클라이언트)**
- 오디오 청크 예시:
```json
{
  "type": "audio_chunk",
  "request_id": "npc_guard_01_line_001",
  "sequence": 0,
  "audio_base64": "<Base64_Encoded_Audio_Data>",
  "is_last": false
}
```

- 완료 메시지:
```json
{
  "type": "completed",
  "request_id": "npc_guard_01_line_001",
  "total_duration_ms": 1800
}
```

- 에러 메시지:
```json
{
  "type": "error",
  "request_id": "npc_guard_01_line_001",
  "code": "MODEL_ERROR",
  "message": "GPU OOM",
  "retryable": true
}
```

---

## 7. 서버 구현 가이드라인

### 7.1 모델 로딩 및 초기화
- 서버 시작 시 VibeVoice 모델을 메모리에 로딩하고, GPU에 올려두어 **온디맨드 로딩으로 인한 첫 요청 지연**을 방지합니다.
- 환경 변수/설정 파일로 아래 항목을 관리:
  - 모델 경로 또는 Hugging Face 모델 이름
  - 디바이스 설정 (cuda:0 / cpu)
  - 최대 동시 요청 수
  - 로깅/모니터링 옵션

### 7.2 인퍼런스 요청 처리
- 요청마다 다음 단계를 거침:
  1. 입력 텍스트 전처리 (길이 제한, 이스케이프 처리, 금칙어 필터링 등)
  2. TTS 옵션(voice_id, emotion, speaking_rate 등)을 VibeVoice 입력 파라미터로 매핑
  3. 모델 인퍼런스 실행
  4. 오디오 포맷 변환 (PCM → WAV/OGG/MP3 등)
- 스트리밍 모드에서는 모델에서 생성되는 프레임/청크를 바로바로 클라이언트로 전송.

### 7.3 동시성 및 리소스 관리
- 한 번에 너무 많은 요청이 들어오지 않도록 **큐 또는 세마포어**로 동시 처리 개수를 제한.
- GPU OOM 발생 시 재시도 전략 또는 graceful failover 처리.
- 장시간 실행 시 성능 저하가 발생하는 경우, 일정 요청 수마다 워커 프로세스 재시작을 고려.

---

## 8. 클라이언트 통합 가이드라인 (VR/게임 중심)

### 8.1 언리얼 엔진 예시
- 언리얼에서 WebSocket 클라이언트를 이용해 `/ws/tts/stream`에 연결.
- 텍스트를 전송하고, 수신되는 `audio_chunk` 메시지를 받아 **실시간으로 버퍼에 쌓은 뒤 재생**.
- 재생 경로:
  - WebSocket → Base64 디코딩 → `USoundWaveProcedural` 또는 Runtime Audio Importer → 오디오 컴포넌트 재생.

### 8.2 VR NPC 아키텍처와의 연결
- LLM 오케스트레이터에서 NPC 대사를 생성한 뒤:
  1. VibeVoice TTS 서버에 텍스트 + emotion, voice_id를 전달.
  2. 반환된 audio_stream_url 또는 WebSocket 스트림을 UE5로 넘김.
  3. UE5에서 오디오 재생과 동시에 MetaHuman Lip Sync / Audio2Face 등으로 페이셜/바디 애니메이션을 구동.
- 이때 TTS 지연을 고려해 다음 전략을 사용:
  - 대사가 길 경우 **앞부분만 먼저 TTS에 보내고, 나머지는 비동기로 이어서 합성**.
  - LLM이 응답을 스트리밍한다면, LLM 토큰이 나오자마자 TTS로 보내는 파이프라인을 구성.

---

## 9. 데이터 및 파인튜닝 가이드 (선택)

### 9.1 기본 사용
- 공개된 VibeVoice/VibeVoice-Realtime-0.5B 모델을 그대로 사용하는 경우, **추가 훈련 데이터는 필요 없음**.
- 이 경우 음성 품질, 감정 표현, 발음은 모델이 이미 학습한 범위 안에서 제공.

### 9.2 특정 캐릭터/화자 음성 파인튜닝
- LoRA 또는 파인튜닝을 고려할 경우:
  - 권장 데이터 양: **10–20시간 이상의 고품질 음성 + 정렬된 텍스트**.
  - 하나의 화자(또는 여러 화자)에 대해 안정적인 톤/발음을 유지하는 것이 중요.
- 데이터 조건:
  - 잡음이 적고, 동일한 마이크/환경에서 녹음.
  - 각 음성 클립에 정확한 텍스트(자막/스크립트)가 대응되어야 함.

### 9.3 파인튜닝 운영 전략
- 베이스 VibeVoice 모델과 캐릭터별 LoRA를 분리해 관리.
- 런타임에는 특정 NPC/캐릭터에 따라 LoRA를 로드하거나, 미리 로드된 LoRA 중에서 선택.

---

## 10. 품질 및 테스트 체크리스트

1. **지연 측정**
   - 텍스트 요청 → 첫 오디오 청크 수신까지의 시간(ms)을 측정.
   - 평균/최대 값을 기록하고, 300–500ms 내에 안정적으로 들어오는지 확인.
2. **음질 검사**
   - 샘플 대사를 여러 길이로 생성해 노이즈, 발음 오류, 끊김 현상 여부를 평가.
3. **부하 테스트**
   - 동시에 여러 요청을 보내 CPU/GPU 사용률, 응답 시간 변화, OOM 여부 확인.
4. **장시간 안정성**
   - 수 시간 이상 연속 요청 시 성능 저하, 메모리 누수, 모델 크래시 여부 확인.
5. **게임/VR 통합 테스트**
   - 실제 VR 환경에서 플레이어 발화 → LLM → TTS → 오디오/애니메이션까지의 전체 체인 지연과 체감 품질을 점검.

---

## 11. 보안, 라이선스, 운영 고려사항

- 라이선스
  - VibeVoice/VibeVoice-Realtime-0.5B는 오픈소스로 제공되지만, 상용 서비스에 사용할 경우 **공식 라이선스 조항**을 반드시 확인해야 합니다.
- 개인정보/음성 데이터
  - 사용자의 음성/텍스트가 로그에 저장되는 경우, 개인정보 처리 방침 및 동의 절차 필요.
- 롤백 전략
  - 모델/설정 업데이트 시 TTS 품질이 떨어지거나 버그가 발생할 수 있으므로, 이전 버전으로 빠르게 롤백 가능한 배포 전략(버전 태깅, blue-green 배포 등)을 준비.

---

## 12. 향후 확장 방향

- 멀티모달 입력
  - 텍스트뿐 아니라 감정 태그, 배경 상황(Context) 등을 입력받아 더 자연스러운 발화 제어.
- 다화자 대화
  - 하나의 타임라인에서 여러 화자의 음성을 생성해 팟캐스트/오디오 드라마/VR 다자 대화에 활용.
- 온디바이스 경량화
  - 0.5B보다 더 작은 모델 또는 양자화(Q4/Q8 등)를 통해, 노트북/미니 PC에서도 충분히 동작하도록 최적화.

---

이 문서는 VibeVoice 기반 로컬 TTS 시스템을 설계·구현할 때의 기준점이 되는 명세입니다. 실제 프로젝트에서는 여기서 정의한 API/구성 요소를 팀 상황에 맞게 커스터마이징하여 사용하면 됩니다.
