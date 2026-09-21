# W07 (2026-02-10 ~ 02-16) — Behavior Mode·BTTask 카테고리·Action Queue

## 핵심
LLM Orchestra용 NPC Behavior Mode 정의. BTTask 분산 파일 → 카테고리별 헤더 일원화. SmartNPC에 Action Queue 도입(순차 실행 패턴 확립).

## 주요 작업
- **VR NPC 코어 인지 엔진** — LLM 통합, RAG 지식베이스, Action Schemas (`1e68d3b`)
- **NPC Behavior Modes + Action 정의** (`5f23144`)
- **BTTask 카테고리 헤더 재편성** (`bbf562f`)
- **UE Function 확장** (`1318279`)
- **BTTask 재구조화 + SmartNPC Action Queue** — 레거시 BTTask(Speak, MoveToLocation 등) 삭제, 카테고리 헤더 로직(`BTTask_*.h` Social/Combat/Lifestyle/Task), Action Queue로 순차 실행, `Execute*()` BP 노출, `EMoveType`(Walk/Run/Sprint/Crouch) (`33379a8`)

## 메모
- **Action Queue 패턴 확립 주차**: 이후 모든 NPC 행동은 `ProcessNextAction` 순차 큐를 통해 실행됨. 이후 W17 `292a1bb` 에서 BT 완료 경로를 동기 처리로 단일화하며 한 번 더 정리.

## 설계 메모 (Notion 개발일기)

### 2026-02-14 — 확률 판정 시스템 (DiceSystem) + 인터럽트 고민
**TRPG에서 착안 → 폐기**: *"TRPG는 턴제 느낌이 강해서 실시간이랑은 안 맞는 듯. 그냥 확률만 하기로 했음"*.

**채택 공식**: `Success Chance = StatValue / Difficulty`
- Stat 60, Difficulty 2 → 30%
- Difficulty 1=Normal, 2=Hard, 3=Very Hard, 4=Extreme, 5=Impossible
- Difficulty 는 **SLM이 산출**

```cpp
bool UDiceSystem::CheckReflex(float StatValue, int Difficulty, FDiceResult& OutResult);
```

→ W17 `fde549d NPC 인간화 — 주사위 판정 기반 패닉·전술 가중치` 로 약 2개월 묵힌 설계가 그대로 구현됨.

**미해결 고민 (당시)**: *"행동 중일 때 인터럽트해서 진행 중인 모든 사고회로 멈추고 다시 새로 뽑기. 언제 인터럽트하지? 피해를 입을 때 하면 전투 턴에 그냥 금붕어잖아"*. → W19 `a6a046f location_decision 대기 중 Event Report 가드`, W20 `bbb9e89 cooldown` 으로 부분 해결.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 02-10 | 1e68d3b | core cognitive engine for VR NPCs |
| 02-12 | 5f23144 | NPC behavior modes + action definitions |
| 02-13 | bbf562f | Reorganize BTTasks into category-based headers |
| 02-13 | b714d04 | Remove RoBERTa + reorganize Interface agent |
| 02-14 | 1318279 | Expand UE Function |
| 02-16 | 33379a8 | Restructure BTTasks + SmartNPC Action Queue |
