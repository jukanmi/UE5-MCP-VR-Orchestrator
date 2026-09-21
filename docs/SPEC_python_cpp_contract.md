# SPEC: Python ↔ C++ 통신 스키마 누락 및 불일치 동기화 기획 (Contract Sync)

> **코드 대조 리뷰 (2026-09-18)** — 아래 표가 현재 판정. 본문(§1~§4)은 원안 그대로 보존.
>
> | 항목 | 스펙 주장 | 코드 실측 | 판정 |
> |---|---|---|---|
> | `stats` (prompt) | C++ 미송신 | 맞음. Python `_format_stats`(interface_input.py, 미커밋) 가 `hp`/`max_hp` 읽음. C++ 소스 `GetStateComponent()->GetAttributes().Resources.Health/MaxHealth` 존재 | **구현 가치 있음** — `NPCManager::SendPlayerDialogue` target_npc_id 세팅 직후 5줄. hp·max_hp 만(stamina/attack_power 는 소비자 없음) |
> | `player_location` (prompt) | 미송신 | 맞음. 소비자 `_format_location` 존재(GesPrompt 는 Vector3D 로 자동 변환) | 보류 — 절대좌표는 LLM 이 해석 불가, 거리는 perception `distance` 로 이미 전달됨. 'MoveTo 플레이어 좌표' 용도 확정 시에만 |
> | `gestures` / `last_event` | 미송신 | C++ 에 gesture 관련 코드 0건 | 스킵 — 송신할 소스 자체가 없음 |
> | `state_update.hp` (§2.2) | 양쪽 누락 | 맞음. 그러나 Python 에서 `cached_world_states` 읽는 곳은 `_format_perceived_targets` 뿐이고, C++ 은 `perceived_targets` 를 항상 빈 배열로 송신(WebSocketClient.cpp:111). Stage2 전투는 `location_decision.context_summary "HP:45%"` 로 이미 HP 받음 | 스킵 — 소비자 없음·중복. state_update 경로 전체가 사실상 죽은 채널 |
> | `observed_context` (§2.3) | 필요 | Python 소비자 없음, C++ 에 타겟 무기 감지 로직 없음 | 스킵 — 양쪽 다 새로 지어야 하는 기능, 별도 스펙 |
> | Strict Validation (§3.3) | Optional 제거 | `current_plan`·`npc_inventory`·`nearby_furniture`·`valid_targets` 등 C++ 이 조건부로만 넣는 필드 다수 | **거부** — Optional 제거 시 정상 통신 즉시 전멸. Optional 은 땜빵이 아니라 계약 |
>
> **추가 발견**: 미커밋 `_format_inventory` 가 `item["equipped"]` 분기 있으나 C++ `NPCInventoryComponent::GetInventoryJson` 은 `equipped` 키 미송신, 장착 개념 자체 없음. 무해(전부 backpack 으로 감)하나 죽은 분기. §4.1 예시의 `"equipped": true` 도 같은 이유로 현재 미지원.
>
> **결정**: 사용자 판단으로 구현 보류(2026-09-18). 구현 시 최소 범위 = `stats{hp,max_hp}` C++ 5줄 + 미커밋 Python diff 커밋.

## 1. 개요 (Overview)
UE5(C++)와 Python(Cognitive Engine) 간의 JSON WebSocket 통신 브리지(`Envelope` 프로토콜)를 전수 검사한 결과, **파이썬 스키마(schemas/envelope.py)에는 정의되어 있으나 UE5에서 값을 보내지 않는 항목**들과, **AI의 상황 판단에 반드시 필요하지만 양쪽 모두 설계가 누락된 핵심 데이터**들이 다수 발견되었습니다.

가장 치명적인 문제는 **NPC 자신의 체력(HP) 정보가 단 한 번도 파이썬으로 전송되지 않는다**는 점입니다.

## 2. 메시지 타입별 누락 항목 (Missing Fields by Type)

### 2.1. Prompt Envelope (`PromptPayload`)
플레이어가 음성이나 제스처로 명령을 내릴 때 LLM으로 전송되는 대화 컨텍스트 페이로드입니다. (C++: `UNPCManager::SendPlayerDialogue`)

- 🚨 **`stats` (체력/스탯 누락)**
  - **파이썬:** `stats: Optional[Dict[str, float]]`로 정의되어 있음.
  - **UE5:** `Payload->SetObjectField("stats")` 관련 송신 코드가 아예 없음.
  - **문제점:** 플레이어가 "너 피 많이 흘리잖아, 좀 쉬어"라고 말해도, LLM은 자신의 체력이 100인지 10인지 알 수 없어 맥락에 맞지 않는 대답을 할 수 있습니다.
- 🟡 **`player_location` (플레이어 위치 누락)**
  - **파이썬:** `player_location: Optional[Dict[str, float]]`로 정의되어 있음.
  - **UE5:** 송신 코드 없음.
  - **문제점:** 거리에 따른 대사 톤(멀리서 외침 vs 가까이서 속삭임)을 LLM이 프롬프트에서 조절할 수 없습니다.
- 🟡 **`gestures` (제스처 및 최근 이벤트 누락)**
  - **파이썬:** `gestures`, `last_event` 필드 정의됨.
  - **UE5:** 송신 코드 없음.

### 2.2. State Update Envelope (`StateUpdatePayload`)
주기적으로 NPC의 상태를 파이썬에 동기화하여 캐시를 갱신하는 페이로드입니다. (C++: `ULLMNetworkClient::SendStateUpdate`)

- 🚨 **자신의 생존 상태(HP) 완전 누락**
  - **파이썬/UE5 공통:** 양쪽 구조체(`FGameStateData`, `StateUpdatePayload`) 모두에 `HP`나 `Stamina` 필드가 존재하지 않습니다.
  - **문제점:** 주기적 동기화에서 체력을 보내지 않으면, 파이썬 기반의 행동 트리(LangGraph)나 전술 플래너가 "체력이 낮으니 도망(Flee)가자"라는 전략적 결정을 내릴 근거 데이터가 아예 없습니다. 위치와 위협 수준만 보낼 뿐입니다.

### 2.3. Emergency Report Envelope (`EmergencyReportPayload`)
척수반사가 작동한 뒤 파이썬으로 사후 보고하는 긴급 이벤트 페이로드입니다. (C++: `UMCPJsonUtils::BuildPerceptionReport`)

- 🟡 **시각+청각 복합 상태 표현 한계**
  - **현황:** 현재는 "어디서(location), 어떤 자극(sense_type)이 감지되었는지"만 배열(`perceptions`)로 담습니다.
  - **문제점:** 앞서 기획한 **"소리를 듣고 돌아봤더니 플레이어가 무기를 들고 있음"** 이라는 복합 정보를 한 번에 보내려면, 해당 자극을 발생시킨 대상의 구체적 상태(`equipped_weapon` 등)를 `FPerceptionData` 구조체에 추가로 담아 보내야 LLM이 지능적으로 반응할 수 있습니다.

## 3. 해결 방안 (Action Plan)

이 문서의 누락 사항들을 해결하기 위해 아래 3가지 조치가 필요합니다.

1. **C++ `SendPlayerDialogue` 보강:**
   - `NPCStateComponent`를 참조하여 `Resources.Health`, `Resources.MaxHealth`, `Combat.AttackPower`를 읽어 `stats` 필드에 동봉하도록 코드 추가.
   - 플레이어 폰의 `GetActorLocation()` 좌표를 `player_location` 필드에 담아 전송.
2. **C++ `FGameStateData` 확장 및 `SendStateUpdate` 패치:**
   - `FGameStateData` 구조체에 생존 필수 지표인 `CurrentHP` 필드를 추가.
   - `ULLMNetworkClient::SendStateUpdate`에서 `Payload->SetNumberField(TEXT("hp"), StateData.CurrentHP)` 직렬화 로직 추가. 파이썬 `StateUpdatePayload`에도 `hp: float = 100.0` 스키마 추가.
3. **C++ ↔ Python 직렬화 방어 로직 강화:**
   - 현재 파이썬 스키마에서 C++의 미구현 필드들을 `Optional`로 땜빵하여 "조용한 실패(Silent Failure)"가 발생하고 있습니다. 주요 필드는 `Optional`을 떼어내어 통신 누락 시 즉각 에러가 발생하도록 강제(Strict Validation)해야 합니다.

## 4. 제안하는 JSON 스키마 규격 (Proposed JSON Schema)

### 4.1. Prompt Envelope (C++ `UNPCManager::SendPlayerDialogue`)
```json
{
  "type": "prompt",
  "msg_id": "12345-abcde",
  "payload": {
    "player_id": "Player",
    "voice_transcript": "너 피 많이 흘리잖아, 좀 쉬어",
    "target_npc_id": "NPC_Vorg",
    
    // [NEW] NPC 자신의 스탯 동봉 (LLM이 자신의 체력을 인지)
    "stats": {
      "hp": 25.0,
      "max_hp": 150.0,
      "stamina": 40.0,
      "attack_power": 15.0
    },
    
    // [NEW] 플레이어 좌표 동봉 (거리에 따른 대사톤 조절)
    "player_location": { "x": 1250.5, "y": -400.2, "z": 105.0 },
    
    // [NEW] VR 제스처 동봉
    "gestures": [
      { "type": "point_at", "target_id": "Sword_01" }
    ],
    
    // [UPDATE] NPC 인벤토리 및 장착 무기 (LLM이 자신의 무장 상태 인지)
    "npc_inventory": {
      "NPC_Vorg": [
        { "id": "IronSword", "name": "철검", "count": 1, "equipped": true },
        { "id": "HealthPotion", "name": "회복약", "count": 2 }
      ]
    },
    
    "requires_replan": true,
    "valid_targets": ["Player", "NPC_Elara"]
  }
}
```

### 4.2. State Update Envelope (C++ `ULLMNetworkClient::SendStateUpdate`)
```json
{
  "type": "state_update",
  "payload": {
    "owner_agent_id": "NPC_Vorg",
    "current_mode": "Combat",
    "threat_level": "High",
    "in_cover": false,
    "line_of_sight": true,
    
    // [NEW] 전투 AI(Stage 2)가 도망칠지 판단할 근거 스탯
    "hp": 25.0,
    "max_hp": 150.0,
    "stamina": 40.0,
    
    "owner_location": {"x": 100.0, "y": 200.0, "z": 0.0},
    "perceived_targets": []
  }
}
```

### 4.3. Emergency Report (C++ `UMCPJsonUtils::BuildPerceptionReport`)
```json
{
  "type": "emergency_report",
  "payload": {
    "agent_id": "NPC_Vorg",
    "report_type": "perception",
    "reflex_action": "TurnHead", 
    "perceptions": [
      {
        "target_id": "Player",
        "sense_type": "Sight",
        "distance": 300.0,
        "danger_score": 0.9,
        "location": {"x": 150.0, "y": -50.0, "z": 0.0},
        
        // [NEW] 시야+청각 복합 관측 컨텍스트 (무기, 스탠스 등)
        "observed_context": {
          "equipped_weapon": "IronSword",
          "stance": "Aggressive"
        }
      }
    ]
  }
}
```
