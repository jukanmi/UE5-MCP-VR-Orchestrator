# W14 (2026-03-31 ~ 04-06) — SLM Action Pipeline·전술 위치 결정

## 핵심
SLM(작은 언어 모델) 액션 파이프라인 도입. EQS 결과를 LLM이 받아 SAFE/OPTIMAL/AGGRESSIVE 중 선택하는 전술 위치 결정 시스템 신설.

## 주요 작업
- **SLM Action Pipeline + 전술 위치 결정 시스템** (`9e96eff`)
- **구버전 코드 제거** (`942c38a`)

## 메모
- **전술 위치 결정 패턴**: UE5가 후보 좌표를 SAFE/OPTIMAL/AGGRESSIVE 3개 카테고리로 보냄 → LLM이 캐릭터 성격·상황 기반으로 1개 선택 → 좌표 echo. 이후 W19~W20에서 stale 응답 차단(`request_gen` 카운터)으로 보강됨.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 03-29 | 942c38a | remove old version code |
| 04-05 | 9e96eff | SLM action pipeline + tactical location decision |
