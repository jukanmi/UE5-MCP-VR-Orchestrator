# W28 (2026-07-07 ~ 07-13) — DX 인프라 구축·target 검증 버그·style 키 계약·전투 셀렉터 SPEC

<!-- 07-11 기준 작성(주 진행 중). 테스트·CI·훅 인프라를 먼저 깔고, 그 위에서 레거시 버그를 잡고 계약 갭을 메운 주. 마지막에 전투 무한공격을 진단해 다음 큰 작업(combat_selector) 스펙까지 확정. -->

## 핵심
이번 주 전반부는 **DX 인프라 주간**: pytest 회귀 기준선을 27→**37** 로 올리며 계약 동기화 테스트 9종(C++ 헤더 텍스트 파싱으로 EAction·BehaviorMode·Envelope wire·Parameters 키 철자를 빌드 없이 검증), Live WS 왕복 스모크, GitHub Actions CI(ubuntu·torch CPU 휠), PR 게이트 훅(`pr_gate.py` — Source/ 변경 시 에디터 빌드 산출물 신선도 강제)을 구축했다. WS 스모크가 첫 실행에서 **cp949 print 크래시로 LangGraph 노드가 통째 죽어 빈 배치 폴백되던 잠복 버그**를 실증해 즉시 청산(이모지 19줄 ASCII 화) — 인프라가 첫날부터 밥값을 했다.

후반부는 그 기반 위의 수정: ① **target 검증 버그** — `valid_npc_ids` 정적 목록이 Skadi/Moca 타겟 액션을 조용히 제거하던 것을 UE5 동봉 `valid_targets`(런타임 등록 NPC) 우선 검증으로 전환(정적 목록은 폴백 강등). PIE 에서 정적 목록에 없는 "SkadiY" 로 runtime path 확정 실증. ② **레거시 삭제** — `raw_response`·`intent.py`·경량루프 死직렬화·`valid_location_ids` 등 소비처 0 코드 일괄 제거. ③ **style 키 계약 갭 해소** — Python 이 보내던 `style` 을 C++ 어디도 안 읽던 것(Move 스타일 무시)을 `ParseMoveStyle`→`EMoveType` 소비 구현 + Sing/Emote 미디어 키 오용 동시 수정 + `Crawl` 유령 어휘 정렬 + 드리프트 방지 테스트. ④ **CI 그린** — 첫 push 실패(`ModuleNotFoundError: app`, bare pytest 는 cwd 를 sys.path 에 안 넣음)를 `conftest.py` 앵커로 해결.

마지막 날 **전투 무한공격 버그를 진단**: `STTask_PrepareNextAction` 자동주입이 Combat+BB.TargetActor 시 Attack 100% 무한 주입인데 **전투 종료 경로가 시스템에 없음**(플레이어는 Destroy 안 돼 사망·리스폰 후에도 추격, Combat 중 LLM 재상담 계기 0). 수정 방향을 인터뷰로 확정해 `SPEC_combat_selector.md` 작성 — C++ 척수 셀렉터(기구현 Attack/Dodge/Block/Flee 재활용, 스탯 파생 가중치+주사위) + 타겟 사망 종료 게이트. 같은 날 구현·PIE 검증까지 완료, PR #19 머지 — SPEC §8 미결(시야 소실 잔존)도 후속 커밋으로 해소해 combat_selector 전체 종결.

## 주요 작업

### DX 개선 6종 (07-07, `4b64c12`~`8ed7f04`)
- **ruff F401 unfixable**(pyproject.toml): PostToolUse 포매터의 미사용 import 자동삭제 봉인 — PR #16 R1·R2 critical(import 먼저·usage 나중 편집 시 삭제→런타임 NameError) 재발 방지.
- **계약 동기화 pytest 9종**(`test_contract_sync.py`): C++↔Python enum·Envelope wire 문자열·main.py 수신 분기 커버리지·Parameters 키 철자. C++ 헤더 텍스트 파싱이라 빌드 불필요.
- **Live WS 왕복 스모크**(`test_ws_roundtrip.py`): fake UE5 클라이언트로 실서버 prompt→ModeActionRequest 계약 검증. 풀(12s)·`--light`(6s).
- **cp949 print 크래시 수정**: 파이프/리다이렉트 환경(백그라운드·CI)에서 print 이모지 → UnicodeEncodeError → 노드 사망. 19줄 ASCII 태그화.
- **Dev.bat**: LLM 서버 단독 `--reload` 핫리로드 개발 런처(TTS/ASR 제외).
- **CI**(`.github/workflows/pytest.yml`): tests/·.github gitignore 해제·추적 전환, ubuntu pytest.

### PR 게이트 훅 (07-09, settings.local.json PreToolUse)
- `gh pr create` 시 브랜치에 Source/ 변경 있으면 에디터 빌드 산출물 mtime ≥ 마지막 Source 커밋 시각 요구. 미커밋 Source 변경도 차단. 3경로 실검증. 한계: 클로드 경유만 잡음. 훅 stderr 는 `sys.stderr.reconfigure(utf-8)` 필수(파이프 cp949).

### 레거시 정리 + target 검증 버그 (07-09, `e9f8fb6`·`72c0a25`)
- **fix**: rules `_is_target_id_valid` — UE5 prompt 동봉 `valid_targets` 우선(+Player/Self/Enemy 센티넬 합집합), 정적 목록 폴백 강등 + Skadi/Moca 추가·유령(Merchant/Blacksmith) 제거. 정적 목록만으론 등록 NPC 액션이 **조용히** 사라지던 버그.
- **삭제**: `raw_response`(단수) state 필드·경량루프 死직렬화·`schemas/intent.py`·`valid_location_ids`. raw_responses 는 replan 턴 Stage2 입력 전용으로 축소.

### CI 그린 + 검증 3건 (07-10, `ad5428b`·`ca607be` 외)
- CI 1차 실패 → `conftest.py` sys.path 앵커로 그린. 테스트 파일 2건 후속 수정(`b179dd3`·`8ef9d79`).
- **C++ 빌드 검증**: PR #16 Gemini R3·R4 반영분(사망 조기반환·GetWorld null 가드·자가공격 가드) 컴파일+PIE 버그 없음.
- **valid_targets runtime path PIE 실증**: 정적 목록에 없는 "SkadiY" 가 `[Rules] OK 검증 통과` — 폴백으론 불가능한 값이라 확정 증명. 함정: 발화 NPC 이름 바꿔 테스트하면 `_create_persona` 가 제로 페르소나 자동생성+yaml 저장 부작용(스트레이 파일 삭제 필요).
- **NPC 공격 판정 에디터 섹션 종료**(선택항목 `HitHapticEffect` VR 럼블만 미할당 잔존).
- settings.local.json 일회성 permission 9건 삭제.

### style 키 C++ 소비 (07-10~11, `175a366`) — pytest 36→**37**
- `NPCActionKeys::Key_Style` + `ParseMoveStyle(FString)→EMoveType`(Move/Follow 한정 지연 호출 — Emote style 폴백 경고 방지).
- Sing/Emote `ExecuteLifestyleAction` 이 `TargetID` 를 미디어 키로 오용하던 갭 동시 수정(`style` 우선, `target_id` 폴백).
- 어휘 정렬: Python 스키마 `Crawl` 은 C++ enum 에 없는 유령 — C++ `EMoveType` 단일 소스로 정렬. 미매칭 `Walk` 폴백+Warning.
- 드리프트 방지 테스트 신설(`test_move_style_vocabulary_matches_cpp`). ⚠️ **C++ 빌드·PIE 미검증**.
- PR #17 Gemini R1 테스트 하네스 지적 3종 반영(`7ab829a` — 무한대기·payload 계약·약한 assert).

### 전투 무한공격 진단 + SPEC_combat_selector 구현·검증 완료 (07-11)
- 재현: 낮은 호감도 상태 말 걸기 → Combat 진입 → 무한 공격. 원인: 자동주입(Attack 100%, 쿨다운·LLM 재확인 없음) + 종료 경로 부재 — 탈출 조건(LLM Mode 전환·시야 소실·타겟 Destroy)이 플레이어 상대론 전부 막힘. `PawnDeathUtils` 는 NPC 통보 0줄이라 사망·리스폰 후에도 추격 지속.
- 인터뷰 확정: C++ 척수 셀렉터(레퍼토리 전부 기구현 재활용)·스탯 파생 성격(Strength→공격/Agility→회피)·저HP 가중치 급증+주사위·타겟 사망 종료 게이트(§2 소유권 위해 Controller 핸들러 경유)·승리 LLM 통보는 Phase 2 분리(emergency 경로 danger<0.5 드랍 게이트 확인).
- **구현**: 종료 게이트(`4c9ee37`)·행동 셀렉터(`1a8f66d`)·승리 통보(`dd48ac1`). pytest 37 유지(Python 무변경).
- **PIE 검증 + 후속 수정**(`c46f5ea`·`7cb8b6a`·`a91885c`·`7dcd967`): `HasNearbyAlly` 빌드 에러(`GetActiveNPCs` 패스스루 누락) 수정·Dodge 등속 이동(마찰 0 Launch)으로 회피감 보정·공격 이동을 `BaseMoveToActor` 타겟 추적으로 전환(고정좌표 이동이 움직이는 타겟 놓치던 문제)·Gemini 리뷰(`MoveToActor` 즉시결과 동기처리·승리메모리 예외로그). §6 체크리스트 전항 통과, **PR #19 머지**(`974b2bf`).
- **SPEC §8 미결 해소**(`5afe9b4`, 머지 후): 타겟 장기 시야 소실 시 Combat 잔존 — `CombatTargetLostTimeout`(기본 8초) 타이머로 미재발견 시 Common 복귀, 재발견(짧은 엄폐) 시 취소. combat_selector 전 항목 종결.

### 문서·TODO 정비 (07-11)
- Memo.md Todo 갭 발굴 8개 섹션 채움(PR #17 머지 선행·C++ 스텁 4종·문서 부채·운영 리스크 등 — 코드 TODO 스캔·docs 교차검증·Done 잔여 승격).
- 완료 SPEC 3종 삭제(active_ragdoll·kinetic_damage·stage_passthrough) — 결정·함정은 Memo Handoff·W26 에 보존.

## 메모 (Memo.md Handoff 추가분, 07-07~11)
- **print+이모지 = 파이프 지뢰**: 실콘솔(UTF-8)은 안 터지고 파이프(cp949)에서만 터짐 → 노드 통째 사망→빈 배치 폴백. 런타임 print 이모지 금지, logger 는 삼켜서 안전.
- **`python -m tests.…` 불가**: site-packages 잡패키지 `tests` 가 로컬 가림. 라이브 하네스는 `python tests/파일.py` 직접.
- **'style' 갭**은 이번 주 C++ 소비 구현으로 해소(빌드 검증만 잔여) — Handoff 함정 항목 소진.
- **SkadiY 류 신규 이름 테스트 부작용**: 페르소나 자동생성 yaml 스트레이 파일.

## 검증 대기
- 미커밋 `.uasset` 2건(GMB_Sample·ExternalActor) 정체 확인.

## 검증 완료 (07-11 추가)
- **style 키 C++ 빌드·PIE**(`175a366`): Move style Walk/Run/Sprint/Crouch 반영 + Sing/Emote 미디어 키.
- **PR #17 머지**(`5194e51`): CI 그린·mergeable — R3·R4 리팩토링 백로그의 선행 조건 해소.
- **combat_selector Phase 1+2 구현·PIE 검증·SPEC §8 해소**: PR #19 머지(`974b2bf`) + 후속 `5afe9b4`. 상세는 위 "전투 무한공격 진단" 항목.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 07-07 | `4b64c12` | chore: ruff F401 자동삭제 봉인 — pyproject.toml unfixable |
| 07-07 | `09561d8` | fix: cp949 파이프 크래시 제거 — print 이모지 ASCII 치환 |
| 07-07 | `5aa22f2` | test: tests/ 추적 전환 + 계약 동기화·WS 왕복 스모크 신설 |
| 07-07 | `3a03e07` | chore: Dev.bat — LLM 서버 핫리로드 개발 런처 |
| 07-07 | `97a812a` | ci: GitHub Actions pytest 워크플로 — .github 추적 전환 |
| 07-07 | `8ed7f04` | feat: 에셋추가 및 반영 |
| 07-09 | `e9f8fb6` | fix: target 검증 런타임 valid_targets 우선 전환 — Skadi/Moca 액션 증발 수정 |
| 07-09 | `72c0a25` | refactor: 죽은 코드 제거 — raw_response·intent.py·경량루프 직렬화 |
| 07-10 | `ca607be` | chore: copilot-instructions.md 로컬 전용 — gitignore 재등재 |
| 07-10 | `ad5428b` | fix: CI ModuleNotFoundError 'app' — conftest.py sys.path 앵커 |
| 07-10 | `b179dd3`·`8ef9d79` | 테스트 파일 CI 후속 수정 (test_import·test_pipeline) |
| 07-11 | `175a366` | feat: style Parameters 키 C++ 소비 — Move/Follow 속도·Sing/Emote 미디어 키 |
| 07-11 | `7ab829a` | fix: 테스트 하네스 3종 Gemini R1 반영 — 무한대기·payload 계약·약한 assert |
| 07-11 | `4c9ee37` | feat: 전투 종료 게이트 — 타겟 사망 시 BB 클리어·Common 복귀 |
| 07-11 | `1a8f66d` | feat: 전투 행동 셀렉터 — 가중치·주사위·스탯 파생 |
| 07-11 | `dd48ac1` | feat: 전투 승리 이벤트 통보 — report_type 게이트 우회·메모리 기록 |
| 07-11 | `7cb8b6a` | feat: Dodge 등속 이동 — 마찰 0 Launch·회피 방향 스냅·배율 튜닝 |
| 07-11 | `a91885c` | feat: 공격 이동을 타겟 추적으로 — BaseMoveToActor 신설·사거리 도달 시 공격 |
| 07-11 | `c46f5ea` | fix: HasNearbyAlly 컴파일 에러 — GetActiveNPCs 공개 패스스루 추가 |
| 07-11 | `7dcd967` | fix: 제미니 리뷰 반영 — MoveToActor 즉시 결과 동기 처리·승리 메모리 예외 로그 |
| 07-11 | `974b2bf` | Merge pull request #19 from jukanmi/feature/combat-selector |
| 07-11 | `5afe9b4` | fix: 적탐지 타이머 갱신 — Combat 시야 소실 잔존 해소(SPEC §8) |
