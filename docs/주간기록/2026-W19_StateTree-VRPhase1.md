# W19 (2026-05-05 ~ 05-11) — BT→StateTree 마이그레이션·VR Phase 1

## 핵심
BehaviorTree에서 StateTree로 NPC 행동 트리 마이그레이션 (MCPStateTreeAIComponent + STTask 패턴 도입). EQS 전술 파이프라인 안정화(단일 후보·이동 중 쿼리 금지·타임아웃·노이즈 제거). gemma4 SLM 도입 + VRAM 관리. Meta Quest 3S 포팅 Phase 1 시작(OpenXR 플러그인·VRPawn 스켈레톤·VR 입력 에셋 분리).

## 주요 작업

### BT → StateTree
- **BT→StateTree 마이그레이션** — `MCPStateTreeAIComponent` + `STTask_*` + `SmartNPCAIController` (`1d55be5`)
- **EQS 전술 파이프라인 개선** — 단일 후보·파싱 수정·타임아웃·노이즈 제거 (`8cc6f9a`)
- **데드코드 제거 + 로그 레벨 정리** — `mcp_server`·`Debug_Test_*`·`CharacterAttributes` (`b72fa8e`)

### EQS·LLM 버그픽스 묶음
- **OPTIMAL 전술 후보 점수 상한 수정** — `Score_OptCoverBonus`·`Score_OptLOSBonus` (`7652647`)
- **EQS 이동 중 전술 쿼리 금지** — `TryStartTacticalQueryForCombat` (`3a4d555`)
- **Follow 무한 루프 수정** — Attack/Track 직접 주입·`TrackedTarget` 해제·`IsTrackingTarget` (`3f57d63`)
- **location_decision 대기 중 Event Report 지연 가드** — `FlushEventReport`·`FacialState` BB 제거 (`a6a046f`)
- **state_update 응답 적체·AffinityCache 중복 갱신 방지** — `PendingStateUpdateAgents` (`26000da`)
- **Python LLM 빈 응답·IndexError·모델 변경** — `gemma4_slm`·`location_decision` 프롬프트 (`99bb8a6`)
- **StateTree 마이그레이션 C++ 데드코드 제거** — Key_HasAction/SubAction/BehaviorMode/FacialState 쓰기 삭제 (`d79d587`)
- **StateTree feature → Develop 머지** (`a1d1b25`)

### VR Phase 1
- **Meta Quest 3S VR 포팅 준비** — OpenXR 플러그인 + VRPawn 스켈레톤 (`5044f97`)
- **VR 입력 에셋 + BP_VRPawn 추가** — Phase 1 완료 (`c1a489a`)
- **`.claude/` 전체 gitignore** — 로컬 설정·워크트리 제외 (`02d3e09`)

## 메모 (Memo.md Handoff Notes)
- **Blueprint CDO PerceptionTickInterval 리셋 필요**: C++ 생성자에 9.0f 추가했으나, 이미 저장된 BP CDO가 3.0f 직렬화 중. NPC Blueprint → Details → PerceptionTickInterval 옆 ↺(Reset) 버튼 클릭 + 저장/재컴파일 필요.
- **PC/VR 입력 에셋 폴더 분리 (2026-05-09)**: `Content/Core/Input/` 의 IA_*.uasset 7종을 `Computer/` 하위로 이동, VR용은 `VR/` 폴더에 신규 생성. 기존 `BP_Player`/`BP_VRPawn`에서 IA 참조 경로가 깨졌을 수 있음 — 두 Pawn BP 컴파일 후 IA 레퍼런스 누락 경고 확인 필수.
- **NPC 시작 시 따라오는 버그**: ① `OnTargetPerceptionUpdated` — 중립 대상(FinalDanger<0.5)은 전술 쿼리 미발동. ② `STTask_PrepareNextAction` — Combat 모드일 때만 전술 쿼리 발동. Python DB 호감도 잔류 시 디버그 대시보드(http://127.0.0.1:8000/debug)에서 초기화 필요.
- **Track 몽타주 없음**: `ExecuteTrack`은 이동(MoveToActor 0.5s 반복)만 사용. DA_NPC_Actions "Track" 키 등록 불필요.
- **JSON 키 폴백 제거 배경**: Python `interface_output.py`가 snake_case로 통일됨. Python 출력 스키마가 안 맞으면 `actions.py`를 먼저 확인할 것 — `MCPJsonUtils`에 폴백 추가 금지.

## 커밋 (14건)
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 05-06 | 1d55be5 | BT→StateTree 마이그레이션 |
| 05-06 | 8cc6f9a | EQS 전술 파이프라인 개선 |
| 05-06 | b72fa8e | 데드코드·로그 레벨 정리 |
| 05-08 | 7652647 | OPTIMAL 전술 후보 점수 상한 |
| 05-08 | 3a4d555 | EQS 이동 중 전술 쿼리 금지 |
| 05-08 | 3f57d63 | Follow 무한 루프 수정 |
| 05-08 | a6a046f | location_decision Event Report 가드 |
| 05-08 | 26000da | state_update 적체 + AffinityCache 중복 방지 |
| 05-08 | 99bb8a6 | Python LLM 빈 응답·IndexError·gemma4 |
| 05-08 | d79d587 | StateTree C++ 데드코드 제거 |
| 05-08 | a1d1b25 | StateTree → Develop 머지 |
| 05-08 | 02d3e09 | .claude/ gitignore |
| 05-08 | 5044f97 | Meta Quest 3S VR 포팅 준비 (OpenXR + VRPawn 스켈레톤) |
| 05-09 | c1a489a | VR 입력 에셋·BP_VRPawn — Phase 1 완료 |
