# W05 (2026-01-27 ~ 02-02) — VR Player·SmartNPC·BT/BB 도입

## 핵심
VR 입력 + SmartNPC 1차 구현. BehaviorTree/Blackboard 기반 NPC 행동 처리 시작. LLM 기반 Rules Agent 도입.

## 주요 작업
- **VR Player 입력 시스템** — InputAction · 마우스 축 (`691edd9`)
- **SmartNPC 캐릭터 + AIController 초기 버전** — BT 기반 액션 처리 (`d86cd05`)
- **VR Player + AI System + Cognitive Engine 통합** — 신규 스키마 (`c8ea884`)
- **Rules Agent 도입** — LLM 기반 정책 검증 + Player/NPC BasicStats (`ed1d8e6`)

## 설계 메모 (Notion 개발일기)

### 2026-02-03 — "중간정리" (3계층 두뇌 모델 확립)
*"정리 좀 해야겠음. 수정도 빠르고 개발도 빠르니까 정신 아프네"* — 코드 변동이 빨라 머릿속을 글로 정돈.

**3계층 두뇌 모델** (프로젝트의 핵심 메타포)
1. **LLM 계층 — 대뇌(Reasoning)** : 클라우드 LLM, 전략·서사
   - Rules / Dialogue(페르소나 YAML) / Interface(발수신) / Supervisor(서사 조율, `BasePolicy` 생성)
2. **SLM 계층 — 소뇌(Intuition)** : 로컬 RTX 5060, Falcon-H1R(당시 안)
   - 100ms 내 즉각 판단, `PatchPolicy` 임시 조항 발행 → 대뇌 응답 대기 없이 NPC 보정
3. **C++ 계층 — 척수(Reflex)** : `PolicyCacheComponent`
   - 0ms 반응, 결정론적 RNG, 정책 TTL 관리, Urgent 시 BT 즉시 전환, `ClearPhysicalState`

→ 이후 모든 작업이 이 3계층 어디에 속하는지로 판단됨. W05~W08 SmartNPC·BT·BB가 "척수", W14 SLM 도입이 "소뇌" 자리(Falcon-H1R 대신 gemma4).

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 02-01 | 691edd9 | Reflect player InputAction and mouse axis |
| 02-01 | d86cd05 | Initial SmartNPC + AI controller (BT) |
| 02-02 | c8ea884 | Core VR player + AI system + Cognitive Engine schemas |
| 02-05 | ed1d8e6 | Cognitive Engine with LLM Rules Agent + BasicStats |
