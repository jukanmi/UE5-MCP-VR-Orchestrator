# OmniAgent Cognitive Engine

## 📋 Overview

**Cognitive Engine**은 Unreal Engine 5 VR 게임을 위한 AI 인지 시스템입니다. LangGraph 기반의 멀티 에이전트 아키텍처를 사용하여 플레이어의 음성/제스처 입력을 처리하고, 게임 규칙을 검증하며, 인게임 NPC의 대화를 생성합니다.

### 주요 기능

- 🎮 **VR 입력 처리**: 음성 및 제스처 데이터를 구조화된 Intent로 변환
- 🤖 **멀티 에이전트 시스템**: Interface → Supervisor → Dialogue/Rules 워크플로우
- ⚡ **긴급 반응 시스템**: 전투/피격 시 즉각 대응
- 🔒 **엄격한 검증**: Pydantic V2 기반 데이터 계약 및 게임 규칙 검증
- 🌐 **WebSocket 통신**: FastAPI를 통한 UE5와의 실시간 양방향 통신
- 📊 **행동 정책 관리**: NPC AI를 위한 BehaviorPolicy 시스템

---

## 🏗️ Architecture

### 시스템 구조도

```
┌─────────────────────────────────────────────────────────────┐
│                      Unreal Engine 5                        │
│  (WebSocket Client: GesPrompt → ActionBatch)               │
└────────────────────┬────────────────────────────────────────┘
                     │ WebSocket (/ws/ue5)
┌────────────────────▼────────────────────────────────────────┐
│                   FastAPI Server (main.py)                  │
│  - GesPrompt Validation (Pydantic)                         │
│  - LangGraph Invocation                                    │
│  - ActionBatch Response                                    │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│           LangGraph Workflow (graph.py)                     │
│                                                             │
│  ┌─────────────┐    ┌──────────────┐                      │
│  │  Interface  │───▶│  Supervisor  │                      │
│  │   Agent     │    │    Agent     │                      │
│  └─────────────┘    └──────┬───────┘                      │
│                            ├──────────┬──────────┐         │
│                            ▼          ▼          ▼         │
│                      ┌──────────┐ ┌─────────┐ ┌─────┐     │
│                      │ Dialogue │ │  Rules  │ │ End │     │
│                      │  Agent   │ │  Agent  │ └─────┘     │
│                      └──────────┘ └─────────┘             │
└─────────────────────────────────────────────────────────────┘
```

### 에이전트 역할

#### 1️⃣ **Interface Agent** (`agents/interface.py`)

**역할**: 입력 중재자 (Mediator)

- **입력**: `GesPrompt` (음성 + 제스처 + 통계)
- **출력**: `Intent` (구조화된 의도)
- **기능**:
    - 지시 표현 해결 ("이거", "저것", "저기")
    - 음성과 제스처 융합
    - 긴급 이벤트 우선 처리 (Hit, Ambush)
    - Fast Reflex: 하드코딩된 빠른 응답 (멈춰, 안녕 등)

**System Prompt 핵심**:

- 행동을 직접 생성하지 않음
- 규칙이나 검증을 적용하지 않음
- 모호성 없는 의도 표현만 생성

#### 2️⃣ **Supervisor Agent** (`agents/supervisor.py`)

**역할**: 오케스트레이터

- **입력**: `Intent` 또는 하위 에이전트 결과
- **출력**: 라우팅 결정 (`next: "Dialogue" | "Rules" | "End"`)
- **기능**:
    - Intent 기반 라우팅:
        - `Attack`, `Interact`, `Move` → **Rules**
        - 기타 → **Dialogue**
    - Fallback 처리: Rules 거부 시 Dialogue에 설명 요청
    - 충돌 해결 (미구현)

**System Prompt 핵심**:

- 다른 에이전트의 출력을 조정
- 숫자 값을 변경하지 않음
- 새로운 액션 타입을 추가하지 않음

#### 3️⃣ **Dialogue Agent** (`agents/subgraphs/dialogue.py`)

**역할**: 캐릭터 페르소나 및 대화 생성

- **입력**: `Intent`, Persona YAML, Memory
- **출력**: `SpeakAction` (대화 + 감정)
- **기능**:
    - Persona 기반 인캐릭터 응답 생성
    - 감정 컨텍스트 부여
    - Fallback 모드: 거부된 행동 설명
    - 높은 Temperature (0.7)로 창의적 대화

**Persona 구조** (`personas/core/elara.yaml`):

```yaml
name: Elara
role: Merchant
traits: [Curious, Friendly, Cautious]
memory_summary:
    key_events:
        - 'Met the player in the market square'
    sentiment: 'Friendly'
```

**System Prompt 핵심**:

- SpeakAction만 생성
- 게임 규칙이나 엔진 동작 참조 금지
- 서술이나 설명 출력 금지

#### 4️⃣ **Rules Agent** (`agents/subgraphs/rules.py`)

**역할**: 게임 규칙 심판 (Referee)

- **입력**: `Intent`, World Constants
- **출력**: `GameAction` (검증된 행동) 또는 `RejectResult`
- **기능**:
    - 숫자 매개변수 검증 및 클램핑
    - 게임 규칙 적용 (예: MAX_DAMAGE = 100)
    - Intent를 구체적인 GameAction으로 변환
    - 낮은 Temperature (0.0)로 결정론적 검증

**World Constants** (`config/world_constants.py`):

```python
# Combat
MAX_DAMAGE = 100
MIN_DAMAGE = 1

# Movement
MAX_SPEED = 600.0
WALK_SPEED = 200.0

# Interaction
MAX_INTERACTION_DISTANCE = 300.0

# Magic/Spells
MAX_MANA = 100
MAX_MANA_COST = 50

# NPC Behavior
DETECTION_RANGE = 1000.0
```

**System Prompt 핵심**:

- 서술 텍스트를 생성하지 않음
- 의도나 대화를 결정하지 않음
- 구조화된 데이터만 출력

---

## 📦 Data Schemas

### 입력: `GesPrompt` (`schemas/vr_context.py`)

Unreal Engine에서 전송하는 VR 컨텍스트

```python
class GesPrompt(BaseModel):
    player_id: str
    voice_transcript: str
    gestures: List[GestureData]
    stats: Optional[Dict[str, float]]
    last_event: Optional[str]  # "Hit", "Ambush" 등
```

**GestureData**:

```python
class GestureData(BaseModel):
    gesture_type: str  # "Point", "Grab", "Wave"
    target_entity_id: Optional[str]
    hand: Literal["Left", "Right"]
    location: Optional[str]  # "x,y,z"
    held_object_id: Optional[str]
```

### 중간: `Intent` (`schemas/intent.py`)

Interface Agent가 생성하는 구조화된 의도

```python
class Intent(BaseModel):
    action_type: str  # "Attack", "Open", "Talk"
    target_reference: Optional[str]  # "Door_42", "Goblin_01"
    raw_query: Optional[str]
    confidence: float = 1.0
```

### 출력: `ActionBatch` (`schemas/actions.py`)

Cognitive Engine이 UE5로 반환하는 최종 명령

```python
class ActionBatch(BaseModel):
    agent_id: str
    actions: List[Union[SpeakAction, GameAction]]
    reasoning: Optional[str]
```

**SpeakAction**:

```python
class SpeakAction(BaseModel):
    action_type: Literal["Speak"] = "Speak"
    text: str
    emotion: str = "Neutral"
    target_listener: Optional[str] = None
```

**GameAction**:

```python
class GameAction(BaseModel):
    action_type: Literal["Move", "Attack", "Interact", "Emote", "Speak"]
    target_id: Optional[str]
    parameters: dict  # {"damage": 50, "velocity": 200} 등

    # Pydantic Validator로 자동 클램핑
    @model_validator(mode='after')
    def clamp_damage_values(self):
        if self.action_type == "Attack":
            if "damage" in self.parameters:
                raw = self.parameters["damage"]
                max_dmg = MAX_DAMAGE
                if raw > max_dmg:
                    self.parameters["damage"] = max_dmg
        return self
```

### 보조: `BehaviorPolicy` (`schemas/behavior_policy.py`)

NPC AI를 위한 고수준 행동 정책

```python
class BehaviorPolicy(BaseModel):
    trace_id: str
    policy_version: int
    issued_at: float
    ttl: float  # Time-to-live (seconds)
    target_guid: str  # 타겟 액터 UUID

    # Behavioral traits (0.0 - 1.0)
    aggression: float = 0.5
    fear: float = 0.5
    vigilance: float = 0.5

    # Bitflags (0x1: Urgent, 0x2: AllowAttack, etc.)
    policy_flags: int = 0
```

**PatchPolicy**: 기존 정책의 경량 업데이트

---

## 🚀 Quick Start

### 1. 환경 설정

#### Prerequisites

- Python 3.10+
- OpenAI API Key 또는 Google Gemini API Key

#### Installation

```bash
cd OmniAgent_VR_System/CognitiveEngine

# 가상환경 생성 (권장)
python -m venv venv
venv\Scripts\activate  # Windows
# source venv/bin/activate  # macOS/Linux

# 의존성 설치
pip install -r requirements.txt
```

#### 환경 변수 설정

`.env` 파일 생성:

```env
# LLM Provider (openai or google)
LLM_PROVIDER=google

# API Keys
OPENAI_API_KEY=your_openai_key_here
GOOGLE_API_KEY=your_gemini_key_here

# Model Selection
OPENAI_MODEL=gpt-4
GOOGLE_MODEL=gemini-1.5-flash
```

### 2. 서버 실행

```bash
# FastAPI 서버 시작
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

**출력 예시**:

```
INFO:     Uvicorn running on http://0.0.0.0:8000 (Press CTRL+C to quit)
INFO:     Application startup complete.
```

### 3. 테스트

#### Health Check

```bash
curl http://localhost:8000/
# {"message":"OmniAgent Cognitive Engine is running"}
```

#### WebSocket 테스트 (Python)

```python
import asyncio
import websockets
import json

async def test_websocket():
    uri = "ws://localhost:8000/ws/ue5"
    async with websockets.connect(uri) as websocket:
        # Send GesPrompt
        test_prompt = {
            "player_id": "Player_01",
            "voice_transcript": "Attack the goblin",
            "gestures": [
                {
                    "gesture_type": "Point",
                    "target_entity_id": "Goblin_42",
                    "hand": "Right"
                }
            ],
            "stats": {"health": 80.0, "mana": 50.0}
        }

        await websocket.send(json.dumps(test_prompt))
        response = await websocket.recv()
        print(f"Response: {response}")

asyncio.run(test_websocket())
```

---

## 📂 Project Structure

```
CognitiveEngine/
├── .env                          # 환경 변수 (API Keys)
├── requirements.txt              # Python 의존성
├── README.md                     # 이 파일
│
├── app/
│   ├── main.py                   # FastAPI 엔트리포인트 + WebSocket
│   ├── graph.py                  # LangGraph 워크플로우 정의
│   ├── mcp_server.py             # MCP (Model Context Protocol) 서버
│   │
│   ├── agents/                   # 에이전트 모듈
│   │   ├── state.py              # AgentState 정의 (TypedDict)
│   │   ├── interface.py          # Interface Agent
│   │   ├── supervisor.py         # Supervisor Agent
│   │   │
│   │   ├── subgraphs/            # 하위 에이전트
│   │   │   ├── dialogue.py       # Dialogue Agent
│   │   │   └── rules.py          # Rules Agent
│   │   │
│   │   └── personas/             # 캐릭터 페르소나 (YAML)
│   │       └── core/
│   │           └── elara.yaml    # Elara (상인) 페르소나
│   │
│   ├── schemas/                  # Pydantic 데이터 모델
│   │   ├── vr_context.py         # GesPrompt, GestureData
│   │   ├── intent.py             # Intent
│   │   ├── actions.py            # ActionBatch, GameAction, SpeakAction
│   │   ├── behavior_policy.py    # BehaviorPolicy, PatchPolicy
│   │   └── game_state.py         # GameState (미사용)
│   │
│   ├── config/                   # 설정 파일
│   │   ├── __init__.py
│   │   └── world_constants.py    # 게임 상수 (MAX_DAMAGE 등)
│   │
│   └── utils/                    # 유틸리티
│       └── llm_factory.py        # LLM Provider 팩토리 (OpenAI/Gemini)
│
└── tests/                        # 테스트 (TODO)
```

---

## 🔧 Configuration

### World Constants (`config/world_constants.py`)

게임 규칙과 상수를 중앙 집중식으로 관리합니다.

**카테고리**:

- **Combat**: 전투 관련 (`MAX_DAMAGE`, `MAX_HEALTH`, `CRITICAL_HIT_MULTIPLIER`)
- **Movement**: 이동 속도 (`MAX_SPEED`, `WALK_SPEED`, `RUN_SPEED`)
- **Interaction**: 상호작용 거리 (`MAX_INTERACTION_DISTANCE`, `PICKUP_DISTANCE`)
- **Magic/Spells**: 마법 시스템 (`MAX_MANA`, `MAX_MANA_COST`, `MAX_CAST_TIME`)
- **NPC Behavior**: NPC AI (`MAX_AGGRESSION`, `DETECTION_RANGE`)
- **World Limits**: 월드 경계 (`WORLD_HEIGHT_LIMIT`, `WORLD_BOUNDARY`)

### LLM Factory (`utils/llm_factory.py`)

**지원 Provider**:

- OpenAI (GPT-4, GPT-3.5-turbo)
- Google Gemini (gemini-1.5-flash, gemini-1.5-pro)

**사용법**:

```python
from app.utils.llm_factory import get_llm

# .env 파일의 LLM_PROVIDER에 따라 자동 선택
llm = get_llm(temperature=0.7)

# Structured Output (Pydantic)
structured_llm = llm.with_structured_output(Intent)
```

---

## 🔄 Workflow Example

### 시나리오: "저 고블린 공격해"

1. **Unreal Engine → GesPrompt 전송**

    ```json
    {
        "player_id": "Player_01",
        "voice_transcript": "저 고블린 공격해",
        "gestures": [
            {
                "gesture_type": "Point",
                "target_entity_id": "Goblin_42",
                "hand": "Right"
            }
        ],
        "stats": { "health": 100.0, "mana": 80.0 }
    }
    ```

2. **FastAPI (main.py) → GesPrompt 검증**
    - Pydantic으로 자동 검증
    - AgentState 초기화

3. **LangGraph Workflow 실행**

    **Step 1: Interface Agent**
    - "저 고블린" + Point Gesture → `target_entity_id: "Goblin_42"` 해결
    - Output:
        ```python
        Intent(
            action_type="Attack",
            target_reference="Goblin_42",
            raw_query="저 고블린 공격해",
            confidence=1.0
        )
        ```

    **Step 2: Supervisor Agent**
    - `action_type == "Attack"` → 라우팅: **Rules Agent**

    **Step 3: Rules Agent**
    - World Constants 주입
    - Intent → GameAction 변환
    - Output:
        ```python
        GameAction(
            action_type="Attack",
            target_id="Goblin_42",
            parameters={"damage": 50}
        )
        ```

    **Step 4: Supervisor (Return)**
    - 검증 성공 → `next: "End"`

4. **FastAPI → ActionBatch 반환**

    ```json
    {
        "agent_id": "RulesAgent",
        "actions": [
            {
                "action_type": "Attack",
                "target_id": "Goblin_42",
                "parameters": { "damage": 50 }
            }
        ],
        "reasoning": "Processed intent: Attack"
    }
    ```

5. **Unreal Engine → 파싱 및 실행**
    - C++ WebSocketClient가 JSON 파싱
    - `UActionSystem::ExecuteAttack("Goblin_42", 50)`

---

## 🛡️ Validation & Safety

### Pydantic Validation

모든 데이터 전송은 Pydantic V2로 강제 검증합니다.

**입력 검증 (GesPrompt)**:

```python
try:
    ges_prompt = GesPrompt(**input_data)
except ValidationError as e:
    return {"error": "Invalid GesPrompt", "details": e.errors()}
```

**출력 검증 (ActionBatch)**:

```python
# GameAction의 @model_validator가 자동 실행
action = GameAction(
    action_type="Attack",
    parameters={"damage": 9999}  # ❌ 너무 큼!
)
# → parameters["damage"] = 100 (자동 클램핑)
```

### Fast Reflex (Latency Masking)

Interface Agent에서 하드코딩된 빠른 응답:

```python
def check_fast_reflex(text: str) -> Optional[Intent]:
    text_lower = text.lower()
    if any(w in text_lower for w in ["멈춰", "그만", "stop"]):
        return Intent(action_type="Wait", raw_query=text, confidence=1.0)
    if any(w in text_lower for w in ["안녕", "hello"]):
        return Intent(action_type="Talk", raw_query=text, confidence=1.0)
    return None
```

### Emergency Interrupt

전투 이벤트 우선 처리:

```python
if vr_context.last_event in ["Hit", "Ambush"]:
    return {
        "analysis": {"intent": Intent(
            action_type="Attack",
            target_reference="Attacker",
            raw_query=f"[System Event: {vr_context.last_event}]",
            confidence=1.0
        )},
        "next": "Supervisor"
    }
```

---

## 🧪 Testing

### Manual Testing

#### 1. Health Check

```bash
curl http://localhost:8000/
```

#### 2. WebSocket Echo Test

```python
# test_websocket.py
import asyncio
import websockets
import json

async def test():
    uri = "ws://localhost:8000/ws/ue5"

    test_cases = [
        {
            "player_id": "TestPlayer",
            "voice_transcript": "Attack",
            "gestures": [],
            "stats": {}
        },
        {
            "player_id": "TestPlayer",
            "voice_transcript": "안녕",
            "gestures": [],
            "stats": {}
        }
    ]

    async with websockets.connect(uri) as ws:
        for case in test_cases:
            await ws.send(json.dumps(case))
            response = await ws.recv()
            print(f"Input: {case['voice_transcript']}")
            print(f"Output: {response}\n")

asyncio.run(test())
```

### Integration Testing (TODO)

```bash
pytest tests/
```

---

## 🔌 Integration with Unreal Engine

### UE5 WebSocket Client Setup

**C++ Header** (`WebSocketClient.h`):

```cpp
UCLASS()
class UWebSocketClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable)
    void SendGesPrompt(const FString& VoiceTranscript, const TArray<FGestureData>& Gestures);

    UFUNCTION()
    void OnMessageReceived(const FString& Message);

private:
    TSharedPtr<IWebSocket> WebSocket;
};
```

**JSON Serialization (Unreal)**:

```cpp
void UWebSocketClient::SendGesPrompt(const FString& VoiceTranscript, const TArray<FGestureData>& Gestures)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField("player_id", "Player_01");
    JsonObject->SetStringField("voice_transcript", VoiceTranscript);

    TArray<TSharedPtr<FJsonValue>> GesturesArray;
    for (const FGestureData& Gesture : Gestures)
    {
        TSharedPtr<FJsonObject> GestureObj = MakeShareable(new FJsonObject);
        GestureObj->SetStringField("gesture_type", Gesture.Type);
        GestureObj->SetStringField("target_entity_id", Gesture.TargetID);
        GestureObj->SetStringField("hand", Gesture.Hand);
        GesturesArray.Add(MakeShareable(new FJsonValueObject(GestureObj)));
    }
    JsonObject->SetArrayField("gestures", GesturesArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    WebSocket->Send(OutputString);
}
```

**Response Parsing**:

```cpp
void UWebSocketClient::OnMessageReceived(const FString& Message)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

    if (FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        FString AgentID = JsonObject->GetStringField("agent_id");
        const TArray<TSharedPtr<FJsonValue>>* ActionsArray;

        if (JsonObject->TryGetArrayField("actions", ActionsArray))
        {
            for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
            {
                TSharedPtr<FJsonObject> ActionObj = ActionValue->AsObject();
                FString ActionType = ActionObj->GetStringField("action_type");

                if (ActionType == "Attack")
                {
                    FString TargetID = ActionObj->GetStringField("target_id");
                    int32 Damage = ActionObj->GetObjectField("parameters")->GetIntegerField("damage");

                    // Execute Attack
                    UActionSystem::ExecuteAttack(TargetID, Damage);
                }
                else if (ActionType == "Speak")
                {
                    FString Text = ActionObj->GetStringField("text");
                    FString Emotion = ActionObj->GetStringField("emotion");

                    // Display Dialogue
                    UDialogueWidget::Show(Text, Emotion);
                }
            }
        }
    }
}
```

---

## 🚧 Roadmap

### Phase 1: Core System (✅ Completed)

- [x] Multi-Agent Architecture (Interface, Supervisor, Dialogue, Rules)
- [x] WebSocket Communication with UE5
- [x] Pydantic Validation Pipeline
- [x] World Constants Management

### Phase 2: Advanced Features (🔄 In Progress)

- [ ] MCP (Model Context Protocol) Integration
- [ ] Game State Tracking via MCP
- [ ] BehaviorPolicy to UE5 AI Controller
- [ ] Multi-NPC Coordination

### Phase 3: Optimization (📋 Planned)

- [ ] LLM Response Caching
- [ ] Streaming Output for Long Dialogues
- [ ] Async NPC Policy Updates
- [ ] Profiling & Latency Reduction

### Phase 4: Advanced AI (💡 Ideas)

- [ ] Memory & Relationship System (RAG)
- [ ] Procedural Quest Generation
- [ ] Dynamic World Events
- [ ] Multi-Turn Dialogue Trees

---

## 📚 Technical Details

### AgentState (`agents/state.py`)

LangGraph의 공유 상태 객체:

```python
from typing import TypedDict, List, Optional, Dict, Any

class AgentState(TypedDict):
    messages: List[str]
    vr_context: Optional[GesPrompt]
    game_state: Optional[Dict[str, Any]]
    next: str
    current_speaker: str
    analysis: Dict[str, Any]
    action_batch: Optional[ActionBatch]
```

### Conditional Routing (`graph.py`)

```python
workflow.add_conditional_edges(
    "Supervisor",
    should_continue,  # Callable: (state) -> "Dialogue" | "Rules" | "End"
    {
        "Dialogue": "Dialogue",
        "Rules": "Rules",
        "End": END
    }
)
```

### Fallback Mechanism

**Rules 거부 시 Dialogue 호출**:

```python
# supervisor.py (Rules 반환 처리)
if rejected:
    return {
        "next": "Dialogue",
        "current_speaker": "Supervisor_Fallback",
        "messages": ["System: Action was rejected. Explain to user."]
    }
```

```python
# dialogue.py (Fallback 메시지 감지)
if current_speaker == "Supervisor_Fallback" and messages:
    fallback_reason = f" (Context: {messages[-1]})"
    human_message += f"\n[System Note]: {fallback_reason}. Explain this in character."
```

---

## 🐛 Troubleshooting

### 문제: WebSocket Connection Failed

**해결**:

```bash
# 서버가 실행 중인지 확인
curl http://localhost:8000/

# 방화벽 확인
netstat -an | findstr :8000  # Windows
lsof -i :8000  # macOS/Linux
```

### 문제: LLM Timeout

**해결**:

```python
# utils/llm_factory.py에서 타임아웃 증가
llm = ChatOpenAI(model="gpt-4", timeout=60)  # 기본: 30초
```

### 문제: Pydantic Validation Error

**확인사항**:

1. UE5에서 보내는 JSON이 스키마와 일치하는지 확인
2. 필수 필드 누락 여부 (`player_id`, `voice_transcript`)
3. 타입 불일치 (예: `gestures`가 배열이 아님)

**디버깅**:

```python
# main.py에서 로깅 추가
import logging
logging.basicConfig(level=logging.DEBUG)

try:
    ges_prompt = GesPrompt(**input_data)
except ValidationError as e:
    print(f"Validation Error: {e.json()}")
```

### 문제: Rules Agent가 항상 거부

**확인사항**:

1. World Constants가 로드되었는지 확인
2. Intent의 `action_type`이 유효한지 확인
3. LLM이 GameAction 스키마를 올바르게 생성하는지 확인

**디버깅**:

```python
# rules.py에서 프롬프트 출력
print(f"Rules Prompt: {prompt}")
```

---

## 📄 License

This project is part of the **OmniAgent VR System** for Unreal Engine 5.

---

## 🤝 Contributing

### Code Style

- Python: PEP 8
- Type Hints 필수 (Python 3.10+)
- Docstrings: Google Style

### Pull Request Process

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## 📞 Contact

**Project**: OmniAgent VR System  
**Maintainer**: [Your Name]  
**Repository**: [GitHub Link]

---

## 🙏 Acknowledgments

- **LangChain / LangGraph**: Multi-Agent Orchestration
- **Pydantic V2**: Data Validation
- **FastAPI**: High-Performance Web Framework
- **Unreal Engine 5**: VR Game Engine
