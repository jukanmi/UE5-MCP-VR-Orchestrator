# W04 (2026-01-20 ~ 01-26) — WebSocket·Action Schema·하이브리드 정책

## 핵심
UE5 ↔ Cognitive Engine 간 WebSocket 연결 + Action Schema 정의. NPC 행동 정책 하이브리드(빠른 분류 + 느린 추론) 아키텍처 윤곽.

## 주요 작업
- **Junction 파일로 엔진 인식** (`64f035b`)
- **UE-LLM Factory 연결** — Capsule 단계 (`96b2f33`)
- **WebSocket 통신 + Action Schema 확립** — Cognitive Engine ↔ Unreal SmartNPC (`2293735`)
- **정책 기반 하이브리드 아키텍처** — 낮은 지연을 위한 NPC 행동 정책 (`c8de3dc`)

## 설계 메모 (Notion 개발일기)

### 2026-01-20 — Rules Agent에 LLM 불필요 (설계 수정)
**판단 전환**: *"Rules 역할에 LLM이 필요 없다"*. Pydantic 만으로 예외 처리 가능. *"사실상 노가다긴 함. 그래도 해야지 뭐"*.

**Rules가 잡아야 할 4종** (Gemini 브레인스토밍)
1. **수치 안정성** — 클램핑(damage ≤ 100, heal ≤ HP×50%), 속도(Walk=300, Run=600), 좌표(±5000)
2. **상태·조건** — Stun/Silence/Root CC, 쿨타임 dict, HP≤0 → Respawn 외 거부
3. **자원 관리** — MP/Ammo 비용, 인벤토리 슬롯, Key 권한
4. **논리 정합성** — 자해 거부, 죽은 적 오버킬 거부, 상호작용 거리(300 unit)

**의미**: 원래 4-Agent 중 1개를 코드 검증으로 강등. *"AI가 자유분방해서 예외 처리할 게 많아짐"* — LLM의 자유로움이 곧 검증 부담이라는 인식이 이 시점부터 박힘. → 이후 W17 Critical/High 픽스 묶음으로 직접 연결.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 01-21 | 64f035b | Junction File for engine |
| 01-21 | 2293735 | WebSocket + action schema for SmartNPCs |
| 01-25 | 96b2f33 | Connect UE to LLM factory |
| 01-29 | c8de3dc | policy-driven hybrid architecture for low-latency NPC |
