# W06 (2026-02-03 ~ 02-09) — NPC Perception·RoBERTa·RAG·Linear Pipeline

## 핵심
NPC 시각 인식(AIPerception) 도입. Cognitive Engine을 Linear Pipeline(Interface→Supervisor→Dialogue→Rules→UE5)로 정리. RoBERTa 기반 의도 분류로 LLM 라우팅 비용 절감.

## 주요 작업
- **NPC BT/BB 트리 초기 구성** (`6066db5`)
- **NPC 위치 이동 가능** — MoveTo 패턴 (`01a7d26`)
- **NPC Interaction 개선** (`feafd91`)
- **safetensors gitignore** (`206a2e8`)
- **KLUE-RoBERTa intent classifier** — speak vs else 149 예제로 학습, Gemma3/Gemini 라우팅, RAG 구현 (`e1f7e5c`)
- **NPC Perception 시스템 + Hybrid Interface + Linear Pipeline** — AIPerception(시야 반경·주변시·진영 감지), RoBERTa(빠름) + Gemini CLI(똑똑), Rules Agent (Gemini CLI 비용 0) (`b881247`)

## 메모
- KLUE-RoBERTa는 추후 W10에 제거됨 — Interface 입출력 분리·LLM 일원화 방향으로 선회.

## 설계 메모 (Notion 개발일기)

### 2026-02-12 — 즉각 피드백 불가 (LLM 지연의 본질적 문제)
*"지금 고민 중인 게 LLM이 구동하는 동안 시간이 걸리니까 그 사이에 보여줄 게 필요함"*.

→ 이 인식이 W04 `c8de3dc policy-driven hybrid architecture` 의 직접 동기. 이후 W14 SLM 도입(`9e96eff`), W19 EQS 안정화(`8cc6f9a`), W20 SLM pre-warm(`bbb9e89`)까지 일관된 동기로 이어짐.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 02-06 | 6066db5 | Behavior Tree + BB Tree |
| 02-06 | 01a7d26 | NPC Can move particular location |
| 02-07 | feafd91 | improve NPC Interaction |
| 02-08 | 206a2e8 | Ignore *.safetensors |
| 02-08 | e1f7e5c | KLUE-RoBERTa intent classifier + RAG |
| 02-09 | b881247 | NPC Perception + Hybrid Interface + Linear Pipeline |
