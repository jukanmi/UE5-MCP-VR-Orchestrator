# W09 (2026-02-24 ~ 03-02) — OmniAgent 프로토콜·Sound·EmergencyReport 배치·EQS Friendly

## 핵심
공식 OmniAgent 통신 프로토콜 정립(WebSocket 메시지 타입 분리·보안 미들웨어). NPC 소리 발생 + 인지. EmergencyReport 배치 발송으로 트래픽 정리. EQS를 진영(우호/적대) 기반 위치 쿼리로 확장.

## 주요 작업
- **Inventory 분리 + ItemManager 도입** (`faaeee3`)
- **Action 실행 + Blackboard 키 표준화** (`c34a490`)
- **OmniAgent 통신 프로토콜 + 하이브리드 아키텍처** (`21ba424`)
- **WebSocket 메시지 타입 분리 + 보안 미들웨어** (`3fd2dc8`)
- **LangGraph 파이프라인 개선 + UE5 액션 처리 강화** (`b1bc82f`)
- **StateComponent 캡슐화 + ApplyDamage LastHitTime 추적** — Getter/Setter 전환, 전술 EQS 분류/방송 반경 TODO 주석, AI 모듈 Public 의존성 이관 (`b8a15a3`)
- **MCPJsonUtils 정리 + SmartNPC facade 강화** — TakeDamage 시 LastHitTime + EmergencyReport, EQS 캐싱 코멘트 (`be3e108`)
- **NPC 소리 발생 + Noise_Volume_Config** (`a4caa73`)
- **EmergencyReport 일괄 변경** — 0.2~0.5초 모은 후 NPC별 1회 Python 송신, Monage 1:1 매칭 (`FActionMediaData`), Envelope 빌더 일원화 (`dd61007`)
- **EQS friendly 기반 위치 쿼리 + NPCRelation/PerceptionData 클래스** — Python `dbmanager.py`로 NPCRelation 조회 (`78f1256`, `f97fdef`, `e068313`)
- **모르는 대상 추론 스킬 추가** (`afae810`)

## 메모
- **EmergencyReport 배치 도입 배경**: 매 충돌마다 individual 메시지 발송 → 트래픽 폭주. 0.2~0.5초 시간창에서 NPC별 1회로 합산 발송하도록 변경.

## 설계 메모 (Notion 개발일기)

### 2026-02-25 — UE→Python 정보 전송 전략 (블랙보드·인플루언스·히스토리)
**문제**: *"그냥 좌표 띡 보내는 거는 에바쎄바 같아서"* — raw 좌표 배열을 LLM에 던지면 비효율.

**Perplexity vs Gemini 비교 — 6가지 상황 인식 기법 중 선택**
| # | 기법 | 채택 |
|---|------|------|
| 1 | 블랙보드 컨텍스트 | ✅ JSON Envelope = "원격 블랙보드" |
| 2 | 유틸리티 스코어 | (부분) EQS 후보 선택에 사용 |
| 3 | 인플루언스/위협 맵 | ✅ UE5 EQS에 위임 — Python은 "SAFE/OPTIMAL/AGGRESSIVE 3개 후보 좌표만" 받음 |
| 4 | 태스크/역할 레이어 | 보류 — Supervisor가 [Breacher][Sniper][Flanker] 할당 |
| 5 | 시공간 메모리 | ✅ LangGraph `AgentState` 누적 — "최근 3분간 행동 패턴" |
| 6 | 디버그 시각화 | EQS Preview / EQSTestingPawn |

**최종 정책**: *"5번을 메인으로 차용. LLM이 여러 개를 던져주면 기억력이 딸려져서 최대한 핵심만 보내야 함"*.
- NPC들이 0.5초마다 캐시에 보고
- npcmanager가 5초마다 취합해 Python 송신
- 좌표 계산은 EQS, Python은 선택만

→ 본 주의 `dd61007 EmergencyReport 일괄 변경` (0.2~0.5초 모은 후 NPC별 1회) 의 직접 설계.
→ W14 `9e96eff SLM action pipeline + tactical location decision` 의 SAFE/OPTIMAL/AGGRESSIVE 3-카테고리 결정.

**부수 결정 (체크리스트)**
- [x] 연타마다 위험 신호 보내면 안 됨. LastHitTime + 2초 가드 → `b8a15a3` 그대로 구현
- [x] BasePlaySound 거리 2분류(가까이=구체적, 멀리=어딘가) → `a4caa73 Noise_Volume_Config` 그대로
- [ ] EQS 최적 장소 알고리즘 (안전/싸우기 좋은/위험) → W17 `e87b58a EQS 7→2 통합` + 스코어링 파라미터화로 완성

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 02-24 | faaeee3 | Relocate Inventory + ItemManager |
| 02-24 | c34a490 | Standardize action execution + Blackboard keys |
| 02-26 | 21ba424 | OmniAgent communication protocol + hybrid architecture |
| 02-26 | 3fd2dc8 | Separate WebSocket message types + security middleware |
| 02-26 | b1bc82f | Improve LangGraph pipeline + UE5 action handling |
| 02-27 | b8a15a3 | encapsulate StateComponent + damage tracking |
| 02-28 | be3e108 | cleanup MCPJsonUtils + SmartNPC facade |
| 03-02 | a4caa73 | NPC make sound + Noise_Volume_Config |
| 03-02 | dd61007 | EmergencyReport batched send |
| 03-02 | 78f1256/e068313 | EQS friendly-based query |
| 03-02 | f97fdef | NPCRelation + PerceptionData class |
