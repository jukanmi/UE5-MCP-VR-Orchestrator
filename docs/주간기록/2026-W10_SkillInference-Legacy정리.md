# W10 (2026-03-03 ~ 03-09) — Skill 추론·NPCStatement·레거시 정리

## 핵심
이전에 본 적 있는 대상을 다시 인지할 수 있는 스킬 추가. SmartNPC에서 NPCAttributes를 NPCStatement로 분리. WebSocketClient.h 정리.

## 주요 작업
- **SendToPython 기능 머지** (#4) (`954b879`)
- **Capsule LLM/SLM Client → WebSocketClient.h 이관** (`8459928`)
- **레거시 코드 제거** — 시야·청각 데이터만 송신, NPCAttributes를 NPCStatement로 이동, NPCManager로 정보 forwarding 정의 (`d4f2591`)

## 메모
- **W10 ~ W13 (3월 중반~말)**: 활동 미미 (`70364ad temporary`, `9716cb8 chore: README` 정도). 4월 초 SLM 작업 재개.

## 설계 메모 (Notion 개발일기)

### 2026-03-03 — 시각·청각 변화 감지만 전송
- **3/2 메모**: *"크악 커밋 잘못했다. 살려다오"* (작업 실수 회고).
- **3/3 메모**: 시각·청각 정보는 무조건 보내지 말고 **변화가 있을 때만** 보내고 싶음 → *"아 이미 그렇게 하고 있네"* 자기 점검. 멀리 보이면 대상 인식 못 하게, 소리도 말소리만 인식하게 작업 중.

→ 본 주의 `d4f2591 시야·청각 데이터만 송신` + W09 `78f1256 EQS friendly 기반 쿼리` 의 직접 동기.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 03-03 | 954b879 | Merge SendToPython (PR #4) |
| 03-06 | 8459928 | Capsule LLM/SLM → WebSocketClient.h |
| 03-09 | d4f2591 | Remove legacy — NPCAttributes→NPCStatement |
