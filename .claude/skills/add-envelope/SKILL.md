---
name: add-envelope
description: 새 Envelope 메시지 타입 추가 시 UE5↔Python 양쪽 동시 수정 강제
---

## Add Envelope Type

새 `EEnvelopeType` 추가 시 UE5와 Python을 **반드시 동시에** 수정해야 합니다.
한쪽만 수정하면 메시지가 무음으로 무시됩니다.

### 사용법

```
/add-envelope <TypeName>
```

예: `/add-envelope TtsRequest`

### 체크리스트 (양쪽 동시 수정)

**UE5 측 (C++)**

1. **`EnvelopeBuilder`** — `Build<TypeName>()` 정적 메서드 추가
   - 반환: `FMessageEnvelope` with `Type = "<type_name>"`
   - Parameters 내부 키: snake_case / 최상위 키: PascalCase (`NPCActionKeys` 상수 사용, 리터럴 문자열 금지)

**Python 측**

2. **`schemas/envelope.py`** — `EEnvelopeType` enum에 `<TYPE_NAME> = "<type_name>"` 추가
3. **`interface_input.py`** — 수신 분기 추가
   ```python
   elif envelope.type == EEnvelopeType.<TYPE_NAME>:
       await handle_<type_name>(envelope, ...)
   ```
4. **핸들러 함수** — `handle_<type_name>()` 구현 (또는 기존 핸들러에 라우팅)

### 검증 포인트

- UE5에서 송신 시 Python `interface_input.py`가 타입을 인식하는지 확인
- Python에서 송신 시 UE5 `EnvelopeBuilder` 역방향 타입이 있는지 확인 (양방향이면)
- `MCPJsonUtils`에 폴백 키 추가 금지 — Python 출력이 상수와 맞지 않으면 Python 쪽 수정

### 완료 후

수정된 파일 목록(UE5/Python 각각)과 Envelope 타입명·JSON 키 목록을 보고합니다.
