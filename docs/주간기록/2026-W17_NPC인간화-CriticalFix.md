# W17 (2026-04-21 ~ 04-27) — NPC 인간화·Critical/High 픽스·NPCAnimInstance·Death

## 핵심
NPC가 단순 명령 수행자가 아니라 "인간"처럼 행동하도록 주사위 판정(panic·전술 가중치) 도입. 대규모 코드 리뷰 결과 Critical 4건·High 6건·Medium 6건 일괄 픽스. NPCAnimInstance + Death 시스템·DA_NPC_Actions 데이터 등록.

## 주요 작업

### NPC 인간화 + 시스템 코어
- **NPC 인간화 시스템** — 주사위 판정 기반 패닉 + 전술 가중치 (`fde549d`)
- **레거시 컴포넌트·데드코드 삭제** (`cbb6841`)
- **NPCManager parse-once 패턴 + JSON 키 단일화** (`6e18308`)
- **NPCStateComponent FlushEventReport 샘플링 단순화** (`48460fa`)
- **NPCActionComponent 다중 개선** — BB 결합도·EQS·ItemManager·JSON 키 (`6404331`)
- **BT 완료 경로 단일화** — `BTTask_ExecuteSmartAction` 동기 처리로 통합 (`292a1bb`)
- **PlayerSystem 머지** — checkpoint respawn, attack montage, faction affinity, EQS LLM decision (`991e96c`)
- **E키 두 번째 누르면 채팅창 닫기 토글** (`42c4202`)

### 애니메이션 + Death
- **Attack 도착 후 몽타주 재생 + Track 미디어 연결** (`ccd84fa`)
- **SmartNPC Death 시스템** — `HandleDeath`/`OnNPCDied`/`DestroyAfterDeath` (`4ff220e`)
- **NPCAnimInstance C++ 클래스 + ABP_SmartNPC 기반 애니메이션 에셋** (`7177b59`)
- **DA_NPC_Actions Attack·Track·Death 데이터** + 레벨 에셋 업데이트 (`a8116eb`)

### 코드 리뷰 픽스 (Critical/High/Medium)
- **Critical 4건** — `check()` UB · WebSocket 누수 · NameError · 빈 액션 실행 (`00a15e0`)
- **High C++ 2건** — `FlushEventReport` 빈 Payload 차단 · `TacticalQuery` BT hang 방지 (`39cfe45`)
- **High Python 4건** — 전역 변수 락 · stale 패킷 · Jailbreak 정규화 · Ollama 타임아웃 (`2e9cd0e`)
- **M-3** `BTTask_PrepareNextAction` `bIsBusy` 무음 실패 → Verbose 로그 (`8c5111d`)
- **M-4·M-5** `interface_output` 파싱 실패 감지 + 서버 기동 시 Ollama 모델 검증 (`4f9dae4`)
- **M-2·M-6** 매직 넘버 UPROPERTY 노출 + Envelope `protocol_version` 추가 (`0d2b70d`)
- **Low 2건** — SmartNPC `BeginPlay` TODO 제거 + `DiceSystem PerceptionBonus` 범위 제한 (`aff50c2`)
- **M-2 빌드 오류 수정** — 스코어링 함수 파라미터 전달 (`7c26bbd`)

### EQS 통합 정리
- **EQS 에셋 7개 → 2개 통합** — `DefaultMoveQuery`(LLM 지시) + `TacticalPositionsQuery`(Perception 전술) 유지, 레거시 5개 삭제 (`e87b58a`)

## 메모
- **EQS 2-에셋 정책 확립**: 이후 새 이동 요구사항은 신규 에셋이 아니라 `UpdateEQSParams()` Named Parameter로 해결 (CLAUDE.md §4 규칙).

## 커밋 (대규모, 16건)
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 04-25 | fde549d | NPC 인간화 (dice panic + 전술 가중치) |
| 04-25 | cbb6841 | 레거시 컴포넌트·데드코드 삭제 |
| 04-25 | 292a1bb | BT 완료 경로 단일화 |
| 04-25 | 6e18308 | NPCManager parse-once + JSON 키 단일화 |
| 04-25 | 48460fa | NPCStateComponent 샘플링 단순화 |
| 04-25 | 6404331 | NPCActionComponent 다중 개선 |
| 04-25 | 991e96c | checkpoint respawn + attack montage + faction affinity + EQS LLM decision |
| 04-25 | 42c4202 | E키 두 번째 누르면 채팅창 닫기 토글 |
| 04-27 | 00a15e0 | Critical 4건 |
| 04-27 | 39cfe45 | High C++ 2건 |
| 04-27 | 2e9cd0e | High Python 4건 |
| 04-27 | 8c5111d | M-3 bIsBusy 무음 실패 |
| 04-27 | 4f9dae4 | M-4·M-5 파싱 실패 + 모델 검증 |
| 04-27 | 0d2b70d | M-2·M-6 UPROPERTY·protocol_version |
| 04-27 | aff50c2 | Low 2건 |
| 04-27 | 7c26bbd | M-2 빌드 오류 수정 |
| 04-27 | a8116eb | DA_NPC_Actions Attack·Track·Death |
| 04-27 | 7177b59 | NPCAnimInstance |
| 04-27 | 4ff220e | SmartNPC Death 시스템 |
| 04-27 | ccd84fa | Attack 몽타주 + Track 미디어 |
| 04-27 | e87b58a | EQS 7→2 통합 |
