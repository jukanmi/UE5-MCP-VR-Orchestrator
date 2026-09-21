# W08 (2026-02-17 ~ 02-23) — SmartNPC 분리·Durability·ItemManager

## 핵심
거대해진 SmartNPC를 Action/Default/State 셋으로 파일 분리. CommonAction Fallback 도입(미지정 액션이 카테고리 BTTask로 떨어져도 공통 처리). 인벤토리에 Durability·Repair 통합.

## 주요 작업
- **CommonAction Fallback** — BTTask_CommonAction에 `ExecuteCommonFallback()` 정적 함수, Combat/Social/Task/Investigation/Lifestyle 알 수 없는 액션은 CommonAction에 위임, `ParseBlackboardParams` 헬퍼 추출, 애니메이션 에셋 추가 (`f661ed1`)
- **SmartNPC 디버그 도구 JSON 파싱 안전화** (`84811ec`)
- **BTTask 통합 + Enum 정의 (`InterfaceOutput→NPCManager→NPC`)** (`d686541`)
- **SmartNPC 파일 분리** — Action / Default / State (`776ea65`)
- **ActionComponent 메인 리메이크** — BehaviorTree 스크립트 재구성, `MaxDurability`/`CurrentDurability` 도입 (`bHasDurability` 플래그 조건), InventoryComponent + NPCActionComponent 통합 수리 로직 (`cfb4171`)

## 설계 메모 (Notion 개발일기)

### 2026-02-20 — SmartNPC 리팩토링 설계 (1060줄 → 332줄)
**Facade 패턴 적용**
- `SmartNPC` 1060줄 → 332줄. "얼굴이자 중재자(Facade)"만 담당.
- `NPCStateComponent` — 스탯·표정·데미지·반사 판정
- `NPCActionComponent` — 행동 큐·Execute 13개·Interaction 16종

**흐름**: 외부 호출 → SmartNPC가 직접 처리 안 함 → 컴포넌트로 위임. `ExecuteDialogue() → ActionComponent->ExecuteDialogue()`.

**중요 규칙**: *"액션 완료 신호 (매우 중요: 이걸 빠뜨리면 큐가 멈춤) `NPC->OnActionCompleted()`"*. → W17 `292a1bb BT 완료 경로 단일화` 로 한 번 더 정리됨.

**미실행 후속**: NPCStateComponent를 Companion에 재사용 실험 — Companion 기능 자체가 우선순위에서 밀려 미진행.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 02-19 | f661ed1 | implement fallback to CommonAction in all Behavior Modes |
| 02-20 | 84811ec | Enhance SmartNPC debug tools with safe JSON parsing |
| 02-21 | d686541 | Union BTTask_* file + Enum exactly defined |
| 02-21 | 776ea65 | Split SmartNPC file by Action/Default/State |
| 02-23 | cfb4171 | ActionComponent main remake + Durability/Repair |
