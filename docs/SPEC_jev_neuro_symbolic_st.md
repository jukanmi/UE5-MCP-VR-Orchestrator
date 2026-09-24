# SPEC: 오픈소스 'Jevlike' 기반 신경 기호형(Neuro-Symbolic) NPC 전술 편향기 및 StateTree 시스템 (Jevlike Neuro-Symbolic Tactical Bias)

> **구현 현황 (2026-09-22 통합 확정)**
>
> | 섹션 / 항목 | 상태 | 비고 |
> |---|---|---|
> | §1 연구 배경 및 오픈소스 `jevlike` 채택 | **확정** | TypeSafe 클라우드 의존 탈피, 로컬 PyTorch 인프로세스 5~20ms 추론 |
> | §2 핵심 아키텍처 결정 및 5대 안티패턴 배제 | **확정** | Claude 리뷰 반영. State 직접 전이 배제, C++ 셀렉터 가중치 배율 개입 |
> | §3 Jevlike 3대 프리미티브(Choice/Score/Noul) 매핑 | 설계완료 | StateTree 서브 전이, 액션/EQS 가중치 배율, Condition 가드레일 |
> | §4 Jev 공학: 정량 수치 사전 정규화 및 토큰 다이어트 | 설계완료 | 산술 연산 결여 대비 C++ 사전 계산, 맥락 부패 방지, JSONL 학습 규격 |
> | §5 엔드투엔드 파이프라인 및 WebSocket Envelope 프로토콜 | 설계완료 | Python CognitiveEngine 로컬 5~20ms 추론, `jev_query`/`jev_decision` 단일 채널 |
> | §6 UE5.5 C++ 핵심 클래스 구현 명세 | **구현완료** (2026-09-22, sol_pi build 0 에러) | `SmartNPCAIController`, `STEvaluator_JevTactics`, `STCondition_NoulGuard`, `SelectCombatAction` |
> | §7 Python 백엔드 로컬 서비스 구현 명세 | **구현완료** (2026-09-22, pytest 76 통과) | `JevlikeService` — jevlike 실제 API(`load_checkpoint`/`ChoiceExample`)로 로드, 체크포인트 부재 시 휴리스틱 폴백 |
> | §8 장애 대응, 지연 및 안전 가드레일 | 설계완료 | 0.3s 워치독 타이머, 세대 카운터, TTL 2.0s 캐시, 1.0 중립 무중단 폴백 |
> | §9 단계별 도입 로드맵 (Phase 1~4) | 진행중 | Phase 2 완료(soft 체크포인트 배포·휴리스틱 라벨 피팅·성격 돌파 2026-09-24), Phase 3 코드 완료. 남은 것: ST 에셋 노드 바인딩(필요성 재검토), Phase 4 PIE 잔여 |
> | §10 프로젝트 규약 준수 매트릭스 | **완료** | 규칙 6-1, 6-2, 6-4, CLAUDE.md §1 준수 검증 완료 |
>
> **기반 연구**: *"신경 기호형 게임 인공지능과 TypeSafe AI 'Jev' 모델을 활용한 차세대 NPC 행동 트리 아키텍처 연구"* (`docs/Jev 기반 NPC 행동트리 조사.pdf`)  
> **기반 엔진**: 오픈소스 [`vinnylarouge/jevlike`](https://github.com/vinnylarouge/jevlike) (TypeSafe Jev 아키텍처 역공학 및 단일 패스 옵션 어텐션 스코어러)  
> **엔진 타겟**: Unreal Engine 5.5 C++ (StateTreeAISchema / UMCPStateTreeAIComponent)  
> **백엔드 타겟**: Python 3.10+ PyTorch (OmniAgent CognitiveEngine 로컬 탑재)

---

## 1. 개요 및 연구 배경 (Overview & Research Background)

### 1.1 레거시 Behavior Tree(BT)의 한계와 StateTree(ST) 도입 배경
- **Behavior Tree(BT)의 한계**: 매 틱마다 루트 노드부터 Selector/Sequence/Decorator를 깊이 우선 탐색(DFS)하는 구조로 인해, 상태(State)가 명시적이지 않고 Blackboard(단일 플랫 키 맵)의 암묵적 데이터 변화에 전적으로 의존합니다. 이는 로직 복잡도가 증가할수록 데드락과 예기치 못한 상태 덮어쓰기를 유발합니다.
- **StateTree(ST)의 진화**: 언리얼 엔진 5.5의 차세대 AI 의사결정 표준입니다. 계층형 상태 기계(Hierarchical State Machine)의 **명시적 상태 전이(Transitions)**와 행동 트리의 **유연한 태스크(Task)/조건(Condition) 모듈성**을 결합하여, 불필요한 틱 부하를 제거하고 이벤트 주도(Event-Driven)의 결정론적 제어를 보장합니다.

### 1.2 생성형 LLM(System 2)의 실시간 게임 제어 결함
- **과도한 지연 시간(Latency)**: 자기회귀(Autoregressive) 토큰 순차 생성으로 인해 1~30초가 소요되어, VR 90Hz 프레임 예산(11ms) 및 NPC 전투 반응 주기(0.1~0.3초)를 파괴합니다.
- **환각(Hallucination) 및 탈옥(Jailbreak)**: 비결정론적 자유 텍스트 출력으로 인해 기정의된 행동 범주를 벗어나거나, 사용자의 악의적 프롬프트에 의해 퀘스트 플래그가 무단 해금되는 등 게임 세계의 룰을 파괴할 위험이 있습니다.

### 1.3 오픈소스 Jevlike(System 1) 엔진 채택 및 로컬 인프로세스 혁신
- 기존 TypeSafe 상용 'Jev' 모델은 비공개 유료 클라우드 API(`https://api.typesafe.ai`)로 제공되어, 클라우드 왕복 네트워크 지연(RTT 150~300ms), 외부 인터넷 의존성, API 키 노출 위험, 오프라인 시 NPC 마비라는 치명적인 문제가 존재했습니다.
- 이를 완전히 극복하기 위해 Jev 아키텍처를 오픈소스로 재현한 **[`vinnylarouge/jevlike`](https://github.com/vinnylarouge/jevlike)**를 채택합니다.
- `jevlike`는 `torch>=2.2`, `numpy>=1.26` 기반의 경량 단일 패스(One-pass) 옵션 어텐션 스코어러 모델로, 우리 프로젝트의 Python 백엔드(`OmniAgent_VR_System/CognitiveEngine`) 프로세스 내부에서 **완전 로컬(In-process GPU/CPU)**로 구동되어 **5~20ms** 내에 추론을 완료합니다.

### 1.4 핵심 설계 철학: C++ 척수(Spine)와 Jevlike 신경망(Nerve)의 결합
- **기호 계층 (Symbolic Layer — C++ / StateTree)**:
  - 0ms 반응 속도의 반사(Flinch, Knockdown, Parry), 물리 상호작용, 룰 집행 및 기본 행동 추첨을 결정론적으로 집행합니다.
  - **세계의 진실(Ground Truth) 통제권을 C++이 독점**합니다.
- **신경망 계층 (Neural Layer — 로컬 Jevlike)**:
  - NPC의 정규화된 물리 지표(체력 비율, 거리, 포위 여부)를 바탕으로 5~20ms 내에 **C++ 셀렉터의 액션 가중치를 유연하게 편향(Tactical Bias)**시킵니다.
- **완전 무중단(Graceful Degradation)**:
  - Jevlike 모델 예외, 타임아웃(0.3s 초과), 저신뢰도(<0.5) 발생 시 **가중치 배율을 1.0(중립)**으로 유지하여, 신경망이 멈추더라도 C++ NPC 로직은 0ms로 100% 정상 작동합니다.

---

## 2. 핵심 아키텍처 결정 사항 및 안티패턴 배제 (Core Decisions & Anti-Patterns)

### 2.1 아키텍처 결정 표 (Decisions Matrix)

| 항목 | 결정 내용 | 근거 및 메리트 |
|---|---|---|
| **추론 엔진** | **로컬 `vinnylarouge/jevlike` (PyTorch)** | 외부 클라우드 RTT 및 API 키 제거, 로컬 인프로세스 5~20ms 초고속 추론 |
| **역할 정의** | **전술 가중치 편향기 (Tactical Bias Engine)** | StateTree 최상위 상태 직접 전이를 배제하고, 기존 C++ `SelectCombatAction` 및 EQS 가중치에 승수(Multiplier)로 개입 |
| **통신 채널** | **WebSocket 단일 채널 (`MessageEnvelope`)** | CLAUDE.md §1 준수. 외부 직접 HTTP 호출 배제, Python 백엔드가 Jevlike 추론 서비스 전담 |
| **소유권 및 진입점** | **`SmartNPCAIController` 단일 소유** | 규칙 6-2 준수. 컨트롤러가 `FJevDecision` 캐시, 세대 카운터, 0.3s 워치독 타이머를 독점 관리 |
| **StateTree 5.5 결합** | **`FSTEvaluator_JevTactics` 0ms 복사** | StateTree 내부 비동기 대기(Latent Task) 삭제. Evaluator가 컨트롤러 캐시를 0ms로 읽어 복사만 수행 |
| **지연 은폐 정책** | **은폐 몽타주 완전 배제** | 5~20ms 추론으로 은폐 자체가 불필요하며, 전투 중 몽타주 재생으로 인한 VR 피격 취약점 원천 차단 |
| **요청 트리거 정책** | **이벤트 주도 (Event-Driven)** | 100ms 매 프레임 무차별 요청 금지. 감각 갱신(`OnTargetPerceptionUpdated`), HP 25% 교차 시에만 1.0s 쿨다운 하에 발송 |

### 2.2 Claude 코드 리뷰를 통해 배제된 5대 안티패턴

| # | 배제된 안티패턴 | 결함 원인 및 런타임 위험 | 확정된 올바른 해결책 |
|---|---|---|---|
| **1** | **UE5 직접 HTTP 호출** | 클라이언트 빌드에 API 키 노출, 오프라인 시 마비, CLAUDE.md §1 단일 채널 위반 | **HTTP 모듈 삭제.** Python 인지 서버가 로컬 `jevlike`를 직접 구동하고 WebSocket `MessageEnvelope`으로 수신 |
| **2** | **Jev의 최상위 State 직접 변경** | C++ `SelectCombatAction` 및 `CurrentBehaviorMode` 소유권 규칙 충돌 ("두 개의 뇌" 문제) | **가중치 편향기로 격하.** 최상위 모드는 보존하고 액션 후보 가중치 및 EQS 파라미터 배율로만 개입 |
| **3** | **Task 내 `[&InstanceData]` 참조 캡처** | State 이탈/AI 일시정지 시 인스턴스 데이터 해제로 200ms 뒤 UAF(Use-After-Free) 크래시 발생 | **Latent Task 완전 삭제.** 수명주기는 컨트롤러가 소유하고 Evaluator는 단순 0ms 값 복사만 수행 |
| **4** | **지연 은폐용 두리번 몽타주 재생** | 몽타주 우회 호출로 `bIsBusy` 상태기계 파괴, 전투 중 6프레임 멈춤으로 VR 피격 치명타 | **은폐 몽타주 배제.** Jevlike는 *다음* 액션 추첨에만 영향을 주므로 현재 C++ 액션이 자연스럽게 이어짐 |
| **5** | **오래된 결정의 상황 오염 (Stale Data)** | 2초 전 "도주" 기조가 늦게 도착하여 현재 역습 상황을 오염시킴 | **세대 카운터 + 0.3s 워치독 + TTL 2.0s 캐시 만료**로 늦은 패킷과 만료된 결정을 0ms 자동 폐기 |

---

## 3. Jevlike 3대 프리미티브와 게임 엔진 매핑 (Primitives Mapping)

`jevlike`는 텍스트 `context`와 가변 `options` 리스트를 받아 Option-Attention Head를 통해 각 옵션의 Softmax 확률을 단일 패스로 산출합니다.

```python
# jevlike 단일 패스 추론 예시
scores = scorer.score(
    context="hp:0.35 dist:4.2 count:2 flanked:true",
    options=["aggressive", "defensive", "flee"]
)
# 결과 확률: [0.15, 0.65, 0.20]
```

| Jevlike 프리미티브 | 수학적/공학적 정의 | StateTree 5.5 매핑 요소 | C++ 구현체 | 런타임 제어 역할 |
|---|---|---|---|---|
| **Choice** | 옵션 집합 중 이산 확률 최대값 | **StateTree Sub-State Transitions** | `FSTEvaluator_JevTactics`<br>`EJevTacticalStance` | 최상위 `BehaviorMode`는 건드리지 않고, `Alert`/`Common` 내부의 탐색·경계 서브 상태 전이 조건(Enum Compare)에 바인딩 |
| **Score** | 연속 확률값 분포 (0.0~1.0) | **Combat Action & EQS Multipliers** | `SelectCombatAction`<br>`ComputeEQSWeights` | 전투 후보 액션(공격, 회피, 도주) 가중치 배율 주입 및 EQS 거리/엄폐 스코어 편향 |
| **Noul** | 유해/위험 판별 이진 확률 | **StateTree Pre-Conditions** | `FSTCondition_NoulGuard`<br>`bool TestCondition()` | 상태 진입 전 사전조건 검증. 유해 확률이 임계값(0.85)을 초과할 경우 해당 State 진입 차단 |

### 3.4 파라미터 산출 책임 매트릭스 (C++ vs Jevlike vs LLM 분업)
아키텍처의 성패는 "어떤 파라미터를 누가 산출하는가"에 달려 있습니다. 분리의 진정한 기준은 "행동의 종류"가 아니라 **"파라미터의 종류"**입니다:

| 파라미터 분류 | 상세 파라미터 항목 | 산출 주체 | 소요 시간 | 설계 근거 및 처리 원칙 |
|---|---|---|---|---|
| **물리 / 공간** | 이동 목표 좌표, 회피 방향(외적 벡터), Spacing 거리, 타겟 액터, 공격 몽타주 스타일, 쿨다운 | **C++ (방안 2)** | **0ms** | C++ 동기 수학 계산(NavMesh, Trace, 삼각함수, EQS). LLM은 3D 좌표/물리를 계산하지 못하며 1~3s 지연 시 플레이어가 2~9m 이동해 100% Stale 데이터가 됨 |
| **전술 기조** | 공격성 배율(`ScoreAggression`), 신중함 배율(`ScoreCaution`), 전술 태도(`EJevTacticalStance`) | **Jevlike** | **5 ~ 20ms** | C++ 셀렉터 후보 가중치 및 EQS 파라미터에 곱해지는 승수(Multiplier). 미도착/만료 시 1.0(중립) |
| **서사 / 대사** | 대사 텍스트, 음성 감정 태그, 대화 제스처 | **LLM (방안 1)** | **1 ~ 3s** | **행동 집행과 분리된 비동기 채널**. 대화도 C++이 즉시 시작(표정/포즈/TurnTo 0ms 집행)하고 텍스트만 1초 뒤 도착. 행동이 텍스트 생성을 기다리는 구조 절대 금지 |

---

## 4. Jev 공학(Jev Engineering): 데이터셋 설계 및 한계 극복 지침

### 4.1 산술 연산 결여 대비 C++ 정량 수치 사전 정규화
- Jev/Jevlike 모델은 공간 좌표 간 거리 계산이나 HP 백분율 계산 등 수학적 산술 연산 능력이 없습니다.
- **원칙**: 모든 공간/물리 지표는 C++ 컨트롤러에서 사전 계산되어 0.0~1.0의 비율 또는 정규화된 실수 형태로 주입되어야 합니다.
  - **허용**: `{"hp_pct": 0.35, "distance_m": 4.2, "enemy_count": 2, "is_flanked": true, "aggression": 0.8, "bravery": 0.7}`
  - 성격(`aggression`·`bravery`)은 C++ `FBehavioralTraits` 0~100 을 /100 한 0~1 값(2026-09-24 추가, §7.2). 누락 시 서버는 0.5(중립)로 본다.
  - **금지**: `{"npc_loc": [100, 200, 50], "target_loc": [500, 200, 50], "hp": 45, "max_hp": 300}`

### 4.2 맥락 부패(Context Rot) 방지를 위한 토큰 다이어트
- 방대한 세계관 텍스트나 장기 대화 기록을 Jevlike 프롬프트에 주입하면 어텐션이 분산되어 단일 패스 판별 정확도가 급격히 하락합니다.
- **원칙**: 직전 3초 이내에 갱신된 물리적 감각 지표 4~5개만으로 압축하여 20토큰 미만의 초경량 컨텍스트 유지.

### 4.3 상충 상태(Conflict) 해결 우선순위 명문화
- Jevlike의 스탠스(Choice)와 점수(Score)가 상충할 경우, C++ 시스템의 결정론적 생존 규칙(0ms 피격 반사 및 방어 우선)이 항상 Jevlike의 출력을 덮어씁니다.

### 4.4 로컬 Jevlike 학습 데이터 포맷 규격 (JSONL)
NPC 전술 모델 파인튜닝 시 `jevlike` 표준 학습 포맷을 사용합니다:

```json
{"context":"hp:0.20 dist:2.5 count:3 flanked:true","options":["aggressive","defensive","flee"],"label":2}
{"context":"hp:0.90 dist:1.8 count:1 flanked:false","options":["aggressive","defensive","flee"],"label":0}
{"context":"hp:0.60 dist:6.0 count:1 flanked:false","options":["aggressive","defensive","flee"],"label":1}
```

---

## 5. 엔드투엔드 시스템 데이터 파이프라인 (Data Pipeline & Sequence)

### 5.1 전체 데이터 흐름도

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Unreal Engine 5.5 Game Thread                         │
│                                                                             │
│  [ 감각 이벤트 발생 ] (시야/청각 갱신, HP 임계값 교차, 전투 진입)          │
│         │                                                                   │
│         ▼ (쿨다운 1.0s & in-flight 1건 가드)                                │
│  SmartNPCAIController::RequestJevDecision()                                 │
│  - 세대 카운터 증가 (JevGeneration++)                                       │
│  - 0.3s 워치독 타이머 가동 (JevTimeoutTimer)                                │
│  - C++ 정규화 감각 데이터 조립 (hp_pct, distance_m 사전 계산)               │
└─────────────────────────────────────┬───────────────────────────────────────┘
                                      │ WebSocket MessageEnvelope (type: "jev_query")
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│              Python CognitiveEngine (FastAPI + jevlike 로컬 탑재)           │
│                                                                             │
│  - WebSocket 라우터에서 Envelope 수신                                       │
│  - 로컬 jevlike PyTorch 모델 인프로세스 추론 (5 ~ 20ms)                     │
│    context = "hp:0.35 dist:4.2 count:2 flanked:true"                        │
│    options = ["aggressive", "defensive", "flee"]                           │
│  - Choice, Score, Noul 추출 후 WebSocket Envelope 패키징                    │
└─────────────────────────────────────┬───────────────────────────────────────┘
                                      │ WebSocket MessageEnvelope (type: "jev_decision")
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│               SmartNPCAIController::HandleJevDecisionResponse()             │
│                                                                             │
│  1. 세대 불일치(Stale) 또는 타임아웃(0.3s 초과) 체크 ──► 즉시 폐기          │
│  2. Confidence < 0.5 체크 ──► 기본값(중립 배율 1.0) 유지                    │
│  3. 정상 응답 ──► Controller 내부 FJevDecision 캐시 갱신 (ReceivedAt 기록)  │
└─────────────────────────────────────┬───────────────────────────────────────┘
                                      │
              ┌───────────────────────┴───────────────────────┐
              ▼                                               ▼
┌───────────────────────────────┐   ┌─────────────────────────────────────────┐
│     UMCPStateTreeAIComponent  │   │          NPCActionComponent             │
│                               │   │                                         │
│ 1. FSTEvaluator_JevTactics    │   │ 1. SelectCombatAction()                 │
│    - 0ms 복사 (Tick)          │   │    - Candidates.Add({ Attack,           │
│    - 2.0s 초과 시 자동 만료   │   │        W * Jev.ScoreAggression });      │
│                               │   │    - Candidates.Add({ Flee,             │
│ 2. FSTCondition_NoulGuard     │   │        Coward * Jev.ScoreCaution });    │
│    - TestCondition() 검증     │   │ 2. ComputeEQSWeights()                  │
│    - 유해 행동 State 진입 차단│   │    - 엄폐/거리 Named Parameter 편향     │
└───────────────────────────────┘   └─────────────────────────────────────────┘
```

### 5.2 통신 프로토콜 규격 (MessageEnvelope JSON Schema)

- **UE5 $\rightarrow$ Python (`jev_query`)**:
```json
{
  "version": "1.0",
  "type": "jev_query",
  "sender": "UE5_Client",
  "timestamp": 1726992000.123,
  "payload": {
    "npc_id": "Guard_01",
    "generation": 42,
    "metrics": {
      "hp_pct": 0.35,
      "distance_m": 4.2,
      "enemy_count": 2,
      "is_flanked": true,
      "aggression": 0.8,
      "bravery": 0.7
    }
  }
}
```

- **Python $\rightarrow$ UE5 (`jev_decision`)**:
```json
{
  "version": "1.0",
  "type": "jev_decision",
  "sender": "CognitiveEngine",
  "timestamp": 1726992000.138,
  "payload": {
    "npc_id": "Guard_01",
    "generation": 42,
    "stance": "Defensive",
    "confidence": 0.65,
    "score_aggression": 0.30,
    "score_caution": 0.85,
    "noul_harmful": 0.02
  }
}
```

---

## 6. UE5.5 C++ 핵심 클래스 구현 명세 (C++ Implementation)

### 6.1 `SmartNPCAIController.h`
컨트롤러가 결정 데이터 캐시, 세대 카운터, 타임아웃 워치독 타이머를 독점 관리합니다 (규칙 6-2 준수).

```cpp
// Source/UE5_MCP_VR/NPC/Action/SmartNPCAIController.h 발췌
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SmartNPCAIController.generated.h"

UENUM(BlueprintType)
enum class EJevTacticalStance : uint8
{
    Default,     // 중립/기본
    Aggressive,  // 저돌적 압박
    Defensive,   // 방어적 거리유지
    Flee         // 후퇴/도주
};

USTRUCT(BlueprintType)
struct UE5_MCP_VR_API FJevDecision
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    EJevTacticalStance Stance = EJevTacticalStance::Default;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float Confidence = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float NoulHarmful = 0.0f;

    /** 전투 액션 가중치 배율 (1.0 = 중립 폴백) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float ScoreAggression = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float ScoreCaution = 1.0f;

    /** 수신 시각 (초 단위 플랫폼 타임, 2.0s 초과 시 자동 만료) */
    double ReceivedAt = -1.0;
};

UCLASS()
class UE5_MCP_VR_API ASmartNPCAIController : public AAIController
{
    GENERATED_BODY()

public:
    const FJevDecision& GetJevDecision() const { return JevDecision; }

    /** 감각 이벤트 발생 시 호출 (쿨다운 1.0s 및 in-flight 가드) */
    void RequestJevDecision();

    /** Python WebSocket 응답 처리 */
    void HandleJevDecisionResponse(const TSharedPtr<FJsonObject>& Payload);

    /** 0.3초 타임아웃 만료 시 호출 */
    void HandleJevTimeout();

private:
    FJevDecision JevDecision;
    uint32 JevGeneration = 0;
    bool bJevRequestInFlight = false;
    double LastJevRequestTime = 0.0;

    FTimerHandle JevTimeoutTimer;
};
```

### 6.2 `STEvaluator_JevTactics.h/.cpp`
StateTree 내부에서 네트워크 요청을 발생시키지 않고 컨트롤러의 캐시를 0ms로 읽어 복사만 수행합니다.

```cpp
// Source/UE5_MCP_VR/NPC/Action/STEvaluator_JevTactics.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionTypes.h"
#include "STEvaluator_JevTactics.generated.h"

class ASmartNPCAIController;

USTRUCT()
struct UE5_MCP_VR_API FSTEvaluator_JevTacticsInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Output")
    EJevTacticalStance TacticalStance = EJevTacticalStance::Default;

    UPROPERTY(EditAnywhere, Category = "Output")
    float Confidence = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Output")
    float HarmfulProbability = 0.0f;
};

USTRUCT(meta = (DisplayName = "Jev Tactics Evaluator", Category = "NPC|Jev"))
struct UE5_MCP_VR_API FSTEvaluator_JevTactics : public FStateTreeEvaluatorCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTEvaluator_JevTacticsInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

    TStateTreeExternalDataHandle<ASmartNPCAIController> AIControllerHandle;
};
```

```cpp
// Source/UE5_MCP_VR/NPC/Action/STEvaluator_JevTactics.cpp
#include "STEvaluator_JevTactics.h"
#include "SmartNPCAIController.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

bool FSTEvaluator_JevTactics::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

void FSTEvaluator_JevTactics::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Out = Context.GetInstanceData(*this);
    const ASmartNPCAIController* AIC = Context.GetExternalData(AIControllerHandle);
    if (!AIC) return;

    const FJevDecision& Decision = AIC->GetJevDecision();
    
    // 2.0초 이내의 신선한 결정만 반영 (오래된 도주 결정 오염 방지)
    const bool bFresh = Decision.ReceivedAt >= 0.0 && (FPlatformTime::Seconds() - Decision.ReceivedAt) < 2.0;

    Out.TacticalStance     = bFresh ? Decision.Stance : EJevTacticalStance::Default;
    Out.Confidence         = bFresh ? Decision.Confidence : 0.0f;
    Out.HarmfulProbability = bFresh ? Decision.NoulHarmful : 0.0f;
}
```

### 6.3 `STCondition_NoulGuard.h`
Noul 프리미티브 확률을 검증하여 위험하거나 탈옥 가능성이 있는 상태 진입을 사전 차단합니다.

```cpp
// Source/UE5_MCP_VR/NPC/Action/STCondition_NoulGuard.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeExecutionTypes.h"
#include "STCondition_NoulGuard.generated.h"

USTRUCT()
struct UE5_MCP_VR_API FSTCondition_NoulGuardInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input")
    float HarmfulProbability = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    float Threshold = 0.85f;
};

USTRUCT(meta = (DisplayName = "Jev Noul Guard", Category = "NPC|Jev"))
struct UE5_MCP_VR_API FSTCondition_NoulGuard : public FStateTreeConditionCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTCondition_NoulGuardInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool TestCondition(FStateTreeExecutionContext& Context) const override
    {
        const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
        // 유해 확률이 임계값 이하일 때만 통과 (True)
        return InstanceData.HarmfulProbability <= InstanceData.Threshold;
    }
};
```

### 6.4 `NPCActionComponent.cpp` (전투 가중치 편향 주입 및 C++ 파라미터 유추)

Jevlike 승수는 기존 결정론적 셀렉터의 후보 가중치에 직접 곱해집니다.

```cpp
// Source/UE5_MCP_VR/NPC/Action/NPCActionComponent.cpp (SelectCombatAction 내부 발췌)

// 1. C++ 동기 물리/공간 파라미터 유추 (0ms)
// - Dodge: 외적으로 타겟 기준 좌/우 측면 스텝 벡터 계산
// - Move: Spacing 링 위 지점 삼각함수 좌표 계산
// - Flee: 타겟 반대 방향 벡터 계산

// 2. Jevlike 전술 승수 획득 (컨트롤러 캐시, 미도착 시 1.0 중립)
const FJevDecision& Jev = AIController ? AIController->GetJevDecision() : FJevDecision();

// [연산 순서 필수 규칙]
// - 순서 1: MaxConsecutiveAttacks 0점 처리 뒤에 곱함 (0 * k = 0, 하드캡 보존)
// - 순서 2: Jev 승수 곱셈 적용 시 [0.25, 4.0] Clamp 필수 (후보 전멸 방지)
// - 순서 3: Bravery 주사위 및 Feared 공포 상태 오버라이드 앞에 곱함 (C++ 생존 룰 최종권한)

for (FCombatCandidate& Candidate : Candidates)
{
    float Multiplier = 1.0f;
    switch (Candidate.Action)
    {
    case EAction::Attack: Multiplier = Jev.ScoreAggression; break;
    case EAction::Dodge:  Multiplier = Jev.ScoreCaution;    break;
    case EAction::Flee:   Multiplier = Jev.ScoreCaution;    break;
    default:              Multiplier = 1.0f;                break;
    }

    // 승수 극단값 Clamp 방어 [0.25, 4.0]
    Candidate.Weight *= FMath::Clamp(Multiplier, 0.25f, 4.0f);
}

// 3. ComputeEQSWeights 편향 주입
void UNPCActionComponent::ComputeEQSWeights(FEQSWeights& OutWeights) const
{
    const FJevDecision& Jev = AIController ? AIController->GetJevDecision() : FJevDecision();
    OutWeights.CoverWeight      *= FMath::Clamp(Jev.ScoreCaution, 0.25f, 4.0f);
    OutWeights.DistanceWeight   *= FMath::Clamp(Jev.ScoreCaution, 0.25f, 4.0f);
    OutWeights.AggressionWeight *= FMath::Clamp(Jev.ScoreAggression, 0.25f, 4.0f);
}

// 4. [Gotcha 방어] MoveToLocation 호출 시 NavMesh 투영 필수 (벽 끼임 방지)
// BaseMove:585 MoveToLocation 기본값이 false 이므로, 셀렉터 산출 좌표를 반드시 투영
FVector ProjectedTargetLoc = TargetLocation;
if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
{
    FNavLocation NavLoc;
    if (NavSys->ProjectPointToNavigation(TargetLocation, NavLoc, FVector(200.f, 200.f, 300.f)))
    {
        ProjectedTargetLoc = NavLoc.Location;
    }
}
```

---

## 7. Python 백엔드 로컬 서비스 구현 명세 (Python Backend Implementation)

### 7.1 `app/services/jev_service.py`
`vinnylarouge/jevlike`의 `OptionScorer`를 백엔드 프로세스에 로드하여 인프로세스로 5~20ms 추론을 수행합니다.

```python
# app/services/jev_service.py
import logging
from typing import Dict, Any
import torch
from jevlike.scorer import OptionScorer  # vinnylarouge/jevlike

logger = logging.getLogger(__name__)

class JevlikeService:
    def __init__(self, checkpoint_path: str = "app/models/jevlike_tactics.pt"):
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self.scorer = OptionScorer.load(checkpoint_path, device=self.device)
        self.options = ["aggressive", "defensive", "flee"]
        logger.info(f"JevlikeService initialized on device: {self.device}")

    def evaluate_tactics(self, metrics: Dict[str, Any]) -> Dict[str, Any]:
        """정규화된 C++ 지표를 받아 로컬에서 5~20ms 내에 전술 점수 반환"""
        context = (
            f"hp:{metrics.get('hp_pct', 1.0):.2f} "
            f"dist:{metrics.get('distance_m', 5.0):.1f} "
            f"count:{metrics.get('enemy_count', 1)} "
            f"flanked:{metrics.get('is_flanked', False)}"
        )
        
        # jevlike 단일 패스 Softmax 확률 추론
        probs = self.scorer.score(context=context, options=self.options)
        
        best_idx = int(torch.tensor(probs).argmax().item())
        best_stance = ["Aggressive", "Defensive", "Flee"][best_idx]
        confidence = float(probs[best_idx])
        
        return {
            "stance": best_stance,
            "confidence": confidence,
            "score_aggression": float(probs[0]) * 2.0,  # 1.0 기준 배율
            "score_caution": (float(probs[1]) + float(probs[2])),
            "noul_harmful": 0.0
        }

# 싱글톤 인스턴스
JEV_SERVICE = JevlikeService()
```

### 7.2 전투 휴리스틱 라벨 피팅 및 성격 돌파 항 (2026-09-24)

- **휴리스틱 계수 = LLM 라벨 피팅값**: `heuristic_probs` 의 클래스별 선형 로짓(상수·hp·hp²·추가 적 수·포위)을
  전투 라벨 2000개(`finetune/jev/data/combos_combat.jsonl`)에 피팅한다(`finetune/jev/fit_combat_heuristic.py`).
  L2 0.003 — 0 이면 계수가 ~15 까지 커져 라벨의 "포위·적 4명·HP<25% 에서 aggressive 0%" 를 그대로 재현하고,
  0.05 면 과평탄해 log-loss 가 나빠진다. 홀드아웃 정확도 70.8%→84.5%, log-loss 0.688→0.429.
- **거리 항은 설계값 고정** `−0.1·max(0, dist−3)`: 라벨러(gemma4)가 거리를 무시해 거리 구간별 라벨 분포가 같았다 — 라벨로는 추정 불가.
  같은 라벨로 학습한 soft 체크포인트는 이 거리 무시를 물려받는다(라벨 재생성 여부는 Memo Todo).
- **성격 돌파 항 `apply_personality`**: 라벨에 성격이 없어 피팅 대상이 아닌 조정 노브.
  `shift = BREAKTHROUGH_GAIN(3.0) × hp × (2·trait − 1) × pressure`, `trait = (aggression + bravery)/2`,
  `pressure = min(1, flanked + 0.5·추가 적 수)` 를 aggressive 로짓에 더한다. 모델·휴리스틱 두 경로 공통(`evaluate_tactics` 말단).
  성격 50 또는 1:1 교전(pressure 0)은 효과 0 — C++ `SelectCombatAction` 의 `AggrMult` 가 이미 1:1 성격을 반영한다.
  근거: 포위 시 Jev 승수가 Clamp 하한 0.25 에 붙으면 `AggrMult`(최대 1.5)를 곱해도 0.375 배라 용감한 NPC 도 돌파 공격이 사실상 불가했다.
- 실측(HP 100%·거리 3m, 성격 0/50/75/100 의 P(aggressive)): 적 2명 포위 4/43/77/94% · 적 4명 포위 0/1/4/14% · HP 30% 는 성격 무관 ~0%.
  PIE(Guard, 적 2명 비포위, HP 100%): 성격 100 conf 1.00 / 성격 0 0.91, 적 1명은 둘 다 0.97.

---

## 8. 장애 대응, 지연 및 안전 가드레일 (Safety Guardrails & Failover)

| 항목 | 위험 요소 | 방어 설계 |
|---|---|---|
| **로컬 지연 (Lag)** | PyTorch 연산 일시 지연 발생 시 | **0.3s 워치독 타이머 + 세대 카운터**: 지연 발생 시 타이머가 세대를 올려 늦게 도착한 패킷을 즉시 폐기. 현재 진행 중인 C++ 액션 지속. |
| **은폐 몽타주 결함** | 몽타주 재생으로 지연 은폐 시도시 6프레임 멈춤 및 피격 사망 | **은폐 몽타주 완전 배제**: 추론 지연이 5~20ms에 불과하여 모션 끊김 자체가 없으며, Jevlike는 다음 액션 추첨에만 개입. |
| **오래된 결정 오염** | 2초 전 "도주" 기조가 현재 공격 상황에 잘못 잔존 | **만료 타이머(TTL 2.0s)**: Evaluator가 `ReceivedAt` 시각을 검사하여 2초 초과 시 자동으로 중립(Default, 배율 1.0)으로 롤백. |
| **모델 로드 실패 / 크래시** | 체크포인트 부재 또는 파이썬 예외 발생 | **완전 무중단(Graceful Fallback)**: `FJevDecision`의 기본 배율이 1.0이므로 C++ 셀렉터와 척수반사 테이블(`TryReflexReact`)이 0ms로 정상 실행 유지. |
| **요청 폭주 (Spam)** | 매 프레임 무차별 요청 시 GC 및 락 부하 | **이벤트 트리거 전용**: 시각/청각 갱신(`OnTargetPerceptionUpdated`), HP 25% 교차 시에만 요청. 최소 쿨다운 1.0s 강제 적용. |

---

## 9. 단계별 도입 로드맵 및 검증 계획 (Roadmap & Verification)

### Phase 1: SPEC 통합 및 아키텍처 확정 (완료)
- [x] 오픈소스 `vinnylarouge/jevlike` 엔진 채택 및 단일 마스터 SPEC 통합 완료 (`docs/SPEC_jev_neuro_symbolic_st.md`).
- [x] Claude 교차 코드 리뷰 및 5대 안티패턴 배제 완료.
- [x] 세션 인수인계 메모 갱신 (`docs/Memo.md`).

### Phase 2: Python 백엔드 `jevlike` 모듈 탑재 및 서비스 구현
- [x] `jevlike` 로컬 패키지 설치(PyPI 미배포 — `pip install git+https://github.com/vinnylarouge/jevlike`) 및 `app/models/jevlike_tactics.pt` 학습 — soft 체크포인트 배포(2026-09-24, `SPEC_jev_daily.md` M2). 미설치 시 휴리스틱 폴백으로 동작. 경로는 절대경로(`0885b200` — repo 루트 기동 시 상대경로 미탐지 버그 수정).
- [x] 전투 휴리스틱 라벨 피팅 + 성격 돌파 항(§7.2, `92027d4e`).
- [x] `app/services/jev_service.py` 생성 및 전술 옵션 스코어러 구현(모델/휴리스틱 이중 경로).
- [x] `app/schemas/envelope.py`에 `EEnvelopeType.JEV_QUERY`, `EEnvelopeType.JEV_DECISION` + `JevQueryPayload` 추가.
- [x] `app/main.py::_process_llm_message` 에 `jev_query` 분기 + `_handle_jev_query` 배선(이 프로젝트의 WS 수신 지점은 `ws_router.py` 가 아니라 `main.py`).
- [x] pytest 단위 테스트 작성 (`tests/test_jevlike_service.py`, 8건).

### Phase 3: UE5.5 C++ StateTree 전술 편향기 구현
- [x] `SmartNPCAIController`에 `FJevDecision`, 세대 카운터, 0.3s 워치독 타이머 추가. 트리거: 시야 적대 감지·전투 소음·HP 25% 하향 교차(`NPCStateComponent::ApplyDamage`).
- [x] `STEvaluator_JevTactics` 및 `STCondition_NoulGuard` 생성.
- [ ] StateTree 에셋에 Evaluator/Condition 노드 배치 및 Output→Condition 바인딩(에디터 작업).
- [x] `NPCActionComponent::SelectCombatAction`·`ComputeEQSWeights`에 Jevlike 가중치 배율 주입, `BaseMove` NavMesh 투영.
- [x] `sol_pi.py build` 컴파일 0 에러 + `pytest tests` 76 통과 (2026-09-22).

### Phase 4: 라이브 VR 에디터 PIE 실측 검증
- [x] 에디터 라이브 PIE 구동 후 적 조우 테스트 — 2026-09-24 헤드셋 없이 Guard vs Vorg·DemonLord, 모델 경로 판정·성격 보정 확인(§7.2).
- [ ] 포위(`flanked=1`) 재현 — 돌진하는 NPC 는 두 적이 동시에 시야 90°+ 로 안 잡힌다. 동시 감지 시 쿨다운으로 `count` 과소 집계되는 C++ 한계도 있음(Memo Todo).
- [ ] 체력 저하 시 Flee 가중치 승수 주입으로 적절한 거리 벌리기 실측.
- [ ] 네트워크 강제 단절 시 C++ 기본 1.0 가중치 폴백 동작 실측.

---

## 10. 프로젝트 규약 준수 매트릭스 (Compliance Matrix)

- [x] **단일 채널 WebSocket 및 MessageEnvelope 규약 준수 (CLAUDE.md §1)**: C++에서 외부로 직접 HTTP 호출하지 않으며, `ULLMNetworkClient` 및 Python 프록시를 경유.
- [x] **Blackboard 및 StateTree 쓰기 단일 진입점 준수 (규칙 6-2)**: `NPCActionComponent`에서 직접 StateTree나 BB를 쓰지 않으며, 모든 결과 수신 및 반영은 `SmartNPCAIController`를 통해서만 처리.
- [x] **BehaviorMode 소유권 보존 (ue5_cpp.md §4)**: Jevlike가 `CurrentBehaviorMode`를 임의로 덮어쓰지 않으며, `Alert`/`Common` 내부의 서브 전이 조건에만 개입.
- [x] **EQS 정책 준수 (규칙 6-4)**: 신규 EQS 에셋을 생성하지 않고, 기존 `TacticalPositionsQuery`의 Named Parameter 가중치를 `ComputeEQSWeights`에서 편향.
- [x] **C++ 0ms 척수반사 테이블 우선순위 보존**: 피격(`Flinch`), 넉다운(`Knockdown`), 패링(`Parry`) 등 실시간 물리 반응은 `SPEC_realistic_combat`과 C++ 반사 테이블이 0ms로 최우선 집행.
