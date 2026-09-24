---
trigger: model_decision
description: 코드 밖 함정·제약 — Ollama/LLM 운영, UE 에디터·MCP·빌드, git·로컬 환경, 전투·인벤토리·스토리의 비직관적 규칙. 해당 영역 작업·디버깅 전에 읽을 것
---

# 함정·제약 노트 (pitfalls.md)

코드·주석만 봐선 모를 배경과 함정만 적는다. 진행 상황은 `docs/Memo.md`, 서사(문제→조치 경위)는 `docs/주간기록/`, 결정 이력은 `docs/주간기록/_결정원장.md`.
새 함정은 해당 도메인 절에 `- **제목**: 왜·제약·다음 주의점` 한 항목으로 추가하고, 폐기된 제약은 지운다.

### A. LLM · Ollama
- **모델 구성(2026-09-08 현행)**: Stage1 hot loop = `gemma4-e4b-dialogue-v1`(v2·v3 는 "줘"→HandObject 회귀로 롤백) · Stage2 플래너 = `qwen3:8b`(`MODELS["mid"]`, 12B 는 npc_id 환각 `Moca→Maca` 조용한 실패) · core NPC = `gemma4-12b`. 교체는 `llm_factory.py` `STAGE1_MODEL`/`STAGE2_MODEL`/`IMPORTANCE_MODELS` 한 곳.
- **gemma4-12b(OBLITERATED Q4)는 rewrite 계열 불능** — 정제/재작성 지시를 프롬프트·few-shot·구조화로도 무시. 생성(plan/요약)은 정상. 프롬프트 재시도로 시간 낭비 말 것. Stage2 goal 도 지시 강화 0/9, **완성 JSON 예시 한 줄**만 먹혔다.
- **gemma4-12b 별칭**: `ollama cp hf.co/mradermacher/Gemma-4-12B-OBLITERATED-GGUF:Q4_K_M gemma4-12b`. 별칭 부재 = 조용한 500(Fast-Path 폴백으로 NPC 는 안 멈추나 품질 저하, 에러가 12B 를 언급 안 함) → `ollama list | grep gemma4-12b` 부터. `reasoning=False` 전역 필수(thinking 이 num_predict 잠식 → content="").
- **KV 양자화 금지**: `OLLAMA_FLASH_ATTENTION=1` + `OLLAMA_KV_CACHE_TYPE=q8_0/q4_0` → 로드 즉시 `CUDA error: shared object initialization failed`(e4b·12B 공통). env 잔존 시 `HKCU:\Environment` 제거 후 Ollama 완전 재시작.
- **VRAM 16GB OOM 잠재(미해결)**: 12B+e4b+PIE 동시 부하 시 Ollama 500/ReadTimeout. TTS GPU(~4GB) 폐기 후 재현 미확인. 대책은 `num_ctx` 축소(DoList 3).
- **대화 기록이 대사를 지배** — 오답이 한 번 기록되면 이후 턴이 전부 따라간다. 이상 응답 반복 시 `/debug` 에서 해당 NPC 기록 초기화부터. NPC 미이동도 같은 곳 — affinity 가 플레이어를 Friendly 캐싱하면 Combat 전환 불가.
- **계획 캐싱**: 이벤트기반 replan — 트리거는 plan 무·combat 최초 전환·plan 달성·`MaxTurnsPerPlan=4` 초과. combat 지속 중 perception tick 은 replan 안 함(`GetBehaviorMode() != Combat` 일 때만 `FlagDangerReplan`). Stage2 `steps` 는 Stage1 에 주입하지 않는다(주입 시 이번 턴 액션 소실 실측).
- **멀티 NPC 병렬**: 지식 격리 목적. `OLLAMA_NUM_PARALLEL=3` 미설정 시 직렬화 → 효과 없음. 상한 `MAX_TARGET_NPCS=3`. 감정 보정은 `interface_output.py::TRAIT_EMOTION_MAP` 결정론(맵에 없는 trait 미보정).
- **NPC 인벤토리 프롬프트**: UE5 가 매 prompt 실시간 동봉(정적 yaml 아님), `돌멩이(Rock)×1` 형식으로 ItemID 노출 필수 — 표시명만 주면 `give_item_id:"돌멩이"` 조회 0건. Stage1 은 보유 아이템만 give/use. `/api/debug/say` 는 UE5 경유(서버 직접 그래프 호출은 인벤·가구·plan 이 빠져 실제와 다른 입력).
- **시나리오 콘텐츠 원본은 `app/story/content/`** (2026-09-18): `main.yaml`(7비트)·`side/*.yaml`·`npcs/<npc>/{persona.yaml,persona.md,history.md}`·`npcs/_world.md`(공통 lore). personas/·knowledge/ 는 gitignore 라 **`python -m app.story.seed`** 로 심는다(옛 .md 삭제·conversation_memory 보존·FAISS 재빌드, 서버 재시작 필요). 콘텐츠 편집 = 원본 수정 → seed. 해적 세계관 백업 `docs/archive/npc_pirate_2026-09-18/`.
- **트리거 9구역 PIE 검증 완료(2026-09-18, 헤드셋 없이)**: `editor_request_begin_play` → `get_player_pawn` 텔레포트로 9구역 순회, 서버 `zone_enter/<zone>` 9건 수신·플래그 기록, bOnce 재진입 무송신 확인. 함정: **스폰 위치가 트리거 박스 안이면 BeginOverlap 이 안 뜬다** → ruins 트리거를 PlayerStart 남쪽으로 옮김. 헤드셋 없는 PIE 도 BP_VRPawn 정상 스폰 — 로그 검증은 이 방식으로.
- **NavMesh 는 Static 생성** — `NavMeshBoundsVolume` 만 키워도 PIE 는 옛 데이터를 쓴다. 에디터에서 `RebuildNavigation` 콘솔 명령(비동기, 전 맵 0.7s) → `is_navigation_being_built` False 확인 → **레벨 재저장**. `build_story_scene.py` 끝에 요청은 넣었지만 저장은 완료 뒤 한 번 더. 검증은 `NavigationSystemV1.project_point_to_navigation(world, pt, None, None, extent)`.
- **BP_Chair = StarterContent SM_Chair**(2026-09-18): 등받이 -X, 앉으면 +X 를 본다 → SeatPoint yaw 0, z 45. 레벨 의자 6개(광장 4·도서관 1·마왕 왕좌 scale 2.4) 전부 BP_Chair, FurnitureID `Chair_Plaza_45/135/225/315`·`Chair_Library_Reading`·`Throne_DemonLord`. 스크립트 `chair()` 헬퍼가 만든다.
- **스토리 전 맵(2026-09-18 v2)**: `tools/build_story_scene.py` — Sample 레벨 500m 전체. 구조물 1605 액터(StaticMeshActor) + 식생·바위·잔해 8719 인스턴스(`BP_HISMCluster`, `/Game/Blueprint/Scene`) 를 `SCN_` 라벨로 생성·저장. 재실행 = 전부 지우고 다시(멱등, 3~4분) — 소품을 손으로 옮겼으면 스크립트 좌표를 고칠 것. 구역만 다시: `SCN_ONLY=village,citadel` 환경변수 + runpy(8초, place_actors 는 별도). 성벽(2026-09-19) = `thick_walls`(두께 4.2m 위 통로·흉벽·소탑 문 관통, 마을·마왕성) + `stair`(정육면체 블록 계단, 꼭대기 참 2m 없으면 NavMesh 가 통로와 안 이어짐 — 실측). 동선: ruins(불타는 성, PlayerStart) → 성문 → 성벽 마을(집 10·시장·우물·광장 제단·은신처, 문 3) → 서문→도서관(Moca) / 남문→숲(약초 빈터 HerbBasket·폭격 자리 Skadi) → 강·다리 → 목책 전초기지(Vorg, Elara 우리) → 죽은 숲 → 대성채(성문루·아성·왕좌 DemonLord·결계실). 구역 트리거 9(ruins·gate·plaza·hideout·library·forest·bridge·outpost·citadel). NavMesh 51000² 전 맵, 안개 0.02. 소품 = `Content/StarterContent/`(gitignore, Engine/Samples 에서 6폴더 복사). **`unreal.Rotator` 위치 인자는 (roll, pitch, yaw)** — 키워드로만. 시각 검증은 SceneCapture2D → `Saved/Screenshots/scene/*.png`(스크립트 말미 아님, MCP 로 별도 실행). 조명이 저녁이라 어둡다 — DirectionalLight 는 손 안 댐.
- **호감도 캐시 웜업 필수(2026-09-18 실측)**: state_update 응답 relations 는 인메모리 캐시만 보고, 캐시는 `get_affinity` 짝 단위 lazy 적재라 DB 에만 쓴 시드가 C++ 에 영영 안 실렸다(PIE 45초 전원 Neutral). `init_db → warm_cache()` 로 기동 시 전 행 적재. **시드 후 서버 재시작** 이 여전히 필요(캐시는 프로세스 소유).
- **NPC vs NPC 전투 검증됨(2026-09-18 Simulate 3분)**: Hostile 시드 → `[Reflex] 적대·근접 즉시 공격 → Attack (Combat 진입)` 양방향, `Damage Applied` 29회. 데미지 = (AttackPower 15 − Defense 10) × 부위배율 0.75 = **3.8/타** → 100HP 킬에 ~27타, 3분 동안 보스 −26HP. 보스전 끝내려면 밸런스(보스 Defense↓ 또는 아군 AttackPower↑) 필요. HealthRegen 은 어디서도 적용 안 됨(속성만 있음). "전투 타겟 장기 소실(8s)" 로 전투가 자주 끊김 — perception tick 9s 보다 짧아서.
- **보스 = BP_SmartNPC + 호감도 시드**: 적 전용 클래스 없음. 적대 판정은 `affinity ≤ -30`(시야 danger 0.6×1.0 ≥ 0.5 → 교전). `python -m app.story.seed` 가 보스↔플레이어·보스↔아군 -100, 아군→플레이어 20(과거 PIE 피격 Hostile 잔재 초기화) 시딩. 보스 배치는 마을에서 80m/150m — 시야 밖. 두 보스 다 `Is Spatially Loaded=false`.
- **story_event.npc_died → boss_killed**: combat_victory 는 아군 NPC 가 보스와 교전 중일 때만 오므로 플레이어 단독 처치는 보스 사망 이벤트로 잡는다(`_handle_story_event` 라우팅). conftest 가 `STORY_ENABLED=0` 을 기본 세팅 — 유닛 테스트가 실제 content/state 를 건드리고 디렉터를 실호출하던 사고(2026-09-18 실측: state 파일 생성) 차단.
- **스토리 디렉터(2026-09-18)**: `get_story()` None = 비활성(`STORY_ENABLED=0` 또는 main.yaml 부재) → 전 훅 no-op. 디렉터 LLM 은 `needs_direction`(전이·해금·캐시 없음) 일 때만 — replan 마다 도는 게 아니다. `gemma4:cloud` 는 grammar 미강제라 `npc_id` 를 `npc` 로 줄여 보냄(실측) → `AliasChoices` + 프롬프트 JSON 예시로 흡수. 타임아웃 5s 초과·402 는 작가 YAML 원문 폴백(qwen3:8b 폴백 금지 — 예시 복사 결함). `talked_to` 카운트는 `_handle_prompt` 성공 턴만(에러/빈 배치 제외). `Story` 블록은 갱신 직후 첫 응답에만 실림(`block_pending`) — UE5 재시작 시 최신 퀘스트 로그를 못 받는 구멍은 Phase B 에서 판단.
- **대화 배선 4건(2026-09-18 전 루프 주행에서 발견, 전부 수정)**: ① `interface_input` 이름 추출이 UE 명시 타겟을 덮었다 → 명시 타겟 우선, 이름 추출은 타겟 없을 때만(자유 채팅). ② `KNOWLEDGE_BASE_PATH` 가 cwd 상대라 README 대로 repo 루트에서 띄우면 RAG 0건(빈 `<repo>/app/agents/knowledge` 자동 생성) → 절대경로. ③ 디렉터 goal 이 Stage2 에만 실려 비트 첫 턴 대사가 목표를 몰랐다 → Stage1 프롬프트 `Story objective` 1줄. ④ `num_ctx` 2048: RAG 실리자 프롬프트 2011 토큰, 생성 37 토큰에 잘려 `......` → 4096 + `done_reason=length` 경고. **대사 품질 의심 시 모델보다 이 배선부터** — 고치기 전엔 Guard 가 "성벽에 불이 났나?", 고친 뒤 "성 안은 이미 함락됐어… 안으로 들어와".
- **전 루프 주행법(헤드셋 없이, 15분)**: `story_state.json` 삭제 + `knowledge/*/conversation_memory.json` 비움(이전 대사가 지배) → 서버 → PIE → `get_player_pawn` 텔레포트 + `CurrentTargetNPCID` 세팅 + `say_to_npc()` → 서버 로그 `[Story] 비트 전이` 확인. 보스는 `apply_damage(100000)`. 아이템은 `ItemManager.get_item_data_by_id` + `Inventory.add_item`. 디렉터 첫 호출은 5s 타임아웃 폴백이 정상(`ReadTimeout('')`).
- **tests/ 회귀 기준선: pytest 75 passed**(2026-09-18, 라이브 gemma4:cloud 1건 포함 — Ollama 없으면 skip). `python -m tests.…` 불가(site-packages `tests` 패키지가 가림) → `python tests/파일.py`. test_pipeline·test_affinity 는 라이브 하네스라 유닛 수집 제외.

### B. NPC 전투 · AI
- **Physics Asset 필수** — 없으면 `SetSimulatePhysics` 조용히 무효 + `FindClosestBone` None→Torso 폴백(부위 인지 무력화). 래그돌은 `UNPCRagdollComponent`(09-12 추출), 튜닝은 BP 의 `Ragdoll` 컴포넌트 Details.
- **스켈레톤 = Mixamo X_Bot**(Mannequin 아님). `BoneToBodyPart` 양 네이밍 수용. 손 소켓/본 = `RightHand`/`LeftHand`(`hand_rSocket` 류 없음).
- **햅틱 전면 폐기(2026-09-21)** — 컨트롤러→손 트래킹 전환 예정이라 진동 낼 하드웨어가 없어짐. 마지막 잔재
  `UPlayerHUDWidget::QuestUpdateHaptic` 삭제로 코드베이스에 `PlayHapticEffect` 호출 0. 새 피드백은 SFX·시각(패널/마커)으로만.
  (옛 메모: `ClientPlayForceFeedback` 은 게임패드용, VR 럼블은 `PlayHapticEffect`+에셋 — 되살릴 일 있으면 참고.)
- **데미지 = 공격 인지 단일 기준** — 밀치기(약속도)는 `TakeDamage` 안 부름 → LLM 공격 인지 없음(의도). 넉다운 임계는 데미지값 비교(임펄스 아님). NPC 근접 데미지 = `Combat.AttackPower×AttackDamageScale` 고정(몽타주라 스윙 속도 없음). 플레이어는 쥔 아이템 방향 상자 + 마스터 테이블 Weight 질량(맨손은 손 구 + `WeaponMass`).
- **새 Attack 몽타주엔 `NPC Attack Hit Window` 노티파이 필수**(SmartNPC) — 미배치 시 `PerformAttackHit` 안 불려 데미지 0(조용히). 스윙당 1회·지정 타겟만·arc 게이트. 판정 본체는 `ACombatCharacter::PerformAttackHit`(2026-09-18 이동). 적(EnemyCharacter)은 노티파이 대신 시간 기준.
- **PauseAI/ResumeAI = StopLogic/StartLogic**, UnPossess 안 씀(재빙의·BB 손실 회피). Resume 시 StateTree 루트 재가동 = 기상 후 위협 재평가(의도). 기상 몽타주는 컴포넌트 기본값(`AM_LayUp`·`AS_stand_up`), 미할당 시 서기 스냅. `ProjectileClass` 미지정이면 원거리 발사 자체 없음.
- **전투 셀렉터(2026-07-11, PIE 미검증)**: `emergency_report` 루트 `report_type` — 새 특수 보고 분기는 `_handle_emergency_report` 의 **danger 게이트 앞**에(위협 아닌 이벤트는 danger=0 이라 조용히 증발). 승리 메모리는 conversation memory `add_entry("Event")`. `SignalAllies` 는 `target_id` 를 미디어 키로 씀 — ActionData 미매핑이면 무음 즉시완료. 연속성 상태 리셋은 `ResetCombatSelectorState()` 한 곳(개별 리셋 흩뿌리기 금지). 거리조절 Move 는 `SpacingIdealRange` 링 지점, EQS 미경유. 아군 = `GetAffinityMultiplier < 1.0`(미캐싱 NPC 는 중립 0.5 라 아군 후보 — 의도).
- **PR #13 보류 사유**: 투사체 콜리전 채널 재설계(스켈메시 직격, 범위 밖) · 동적 플랫폼 바닥 감지(무빙플랫폼 없음).
- **가구**: BP 는 이벤트 그래프 0노드(부모 `FurnitureActor`, 프로퍼티만). 점유 게이트 `bOccupied` 가 착석 충돌의 근본 수정. NPC 사망·넉다운 시 반납.

### C. 인벤토리 · VR 조작
- **수량 기준 두 갈래**: `HasItem`/`GetItemCount` 는 장착분 **포함**, `RemoveItem`/`GetSlotIndexByItemID` 는 슬롯만(의도 — 장착만 하면 보유 false 가 되어 거래·퀘스트가 어긋남). 슬롯에서 실제로 빼는 동작은 `GetItemCountInSlots` 로 판정. 어기면 복제·증발(2026-09-01 3곳 수정).
- **`AddItem` 은 전부-아니면-전무** — 부분 적재 후 `false` 는 호출측이 원본을 남겨 재시도마다 복제됐다. 스택 상한은 `GetEffectiveMaxStack`(= min(아이템 MaxStack, `MaxStackLimit`=10)) 경유, 마스터 테이블 직접 읽기 금지.
- **월드 아이템 = `ADroppedItemBase` 하나** — `ItemManager` 등록 풀을 NPC 탐지가 본다. 별도 픽업 클래스를 만들면 NPC 가 인지 못 하는 두 번째 계통이 생긴다. Deferred 스폰으로 `FinishSpawning` 전에 `ItemTemplateID` 를 넣을 것(늦으면 `BeginPlay` 가 `DefaultEntity_Unknown` 으로 등록).
- **HandObject 는 소유권 이전 없음** — 제시(손에 들기) 연출 전용, 실제 이전은 GiveItem. 플레이어 인벤에 넣으면 복제.
- **손 쥐기 규칙(최종, 사용자 지정 2026-09-07)**: 손에 쥐는 모든 것은 물리 액터. 그립 홀드(`Started` 쥠 / `Completed`·`Canceled` 놓음). 뗄 때 **인벤토리 열림 = 회수 / 닫힘 = 거래접시→NPC 건네기→던지기**. 시간 가드 없음. 장착 무기는 `AttachedMeshes` 라 `HeldItems` 가 아님 — 그립을 누르면 `OnGrabStart` 가 물리 쥐기로 전환해야 던지기 경로를 탄다. 이 규칙 밖의 조작 기능 임의 추가 금지(당일 두 번 뒤집힌 원인).
- **BP 오버라이드가 C++ 기본값을 이긴다** — 상시 켜야 하는 것은 `BeginPlay` 에서 강제하거나 에셋 재직렬화 확인(`HUDWidgetComp` 가시성 사례). `BP_VRPawn::CameraHeightOffset` = **0 필수**(-30 이면 HMD 높이 역산 오염 → 캡슐 30cm 단축). 머리 본 위치는 `HeadEffectorOffset`/FBIK 로, 카메라 오프셋 금지.
- **VR 아바타 1:1 고정** — 키 비율 스케일 삭제(FBIK 하에서 '서면 머리 낮음' 만 유발). `CalibratedStandingHeight` 는 자세판정용만. 몸통은 HMD Yaw 1:1 추종(`BodyMeshYawOffset=-90`), 착석 시 의자 방향 고정.
- **PostProcess 는 이 렌더 경로(`r.ForwardShading`+`vr.InstancedStereo`+`vr.MobileMultiView`)에서 화면 전체 검정** — 비네트·터널 효과 불가. 시각 피드백은 HUD.
- Sprint 해제는 `IA_Move` `Completed`/`Canceled`(`OnMoveReleased`) — `Triggered` 는 입력 0 에서 안 오고 `OnMove` 가 조기 return.

### D. 통신 · DX
- **상세 문서 `docs/index.html`**: **`docs/index.html`** (브라우저로 열기). 주요 앵커: `#ai-codebase-guide`(아키텍처) · `#tts-plan`(TTS 계획) · `#langgraph`(LangGraph) · `#integration-guide`(통합 시퀀스). 로드맵은 `docs/Memo.md` Todo 섹션·`docs/DoList.md` 가 담당(index.html 에 별도 `#todo` 없음, 2026-07-27 중복 방지로 참조 제거).
- **index.html 동기화 의무**: 파일 삭제·이동·프로토콜 변경(Envelope 타입, 응답 방식 등)·엔진 교체(TTS 모델 등)처럼 `docs/index.html` 이 서술하는 사실이 깨지는 수정을 하면 **같은 세션에서 해당 앵커 섹션도 같이 고칠 것** — 나중으로 미루지 말 것. 방치 사례(2026-07-27 발견): `schemas/intent.py` 는 2026-07-09 삭제됐는데 `#ai-codebase-guide` 파일 목록엔 3주 넘게 남아있었음, TTS 도 CosyVoice2→OpenVoice v2+MeloTTS 전환(2026-06-23)이 반영 안 됨. 코드와 문서가 따로 놀면 다음 세션이 잘못된 사실을 근거로 판단하게 됨.
- **WP 외부 액터는 `modify(True)` 없이 옮기면 저장에서 빠진다(2026-09-18 실측)**: `set_actor_location`·`set_mobility`·`set_box_extent` 는 `Modify()` 를 안 불러 패키지가 dirty 되지 않고 `save_current_level`/`save_dirty_packages` 모두 건너뛴다. 에디터 메모리엔 반영돼 보여 "저장됐다" 로 착각. NPC 7·PlayerStart·Bed 가 두 커밋에 걸쳐 유실됐던 원인. 판정은 `get_dirty_map_packages()` 개수 = 건드린 액터 수, 디스크는 `git status --short | grep -c ExternalActors`.
- **무료 에셋 소스(2026-09-18)**: Quaternius Google Drive 는 익명 접근 몇 번이면 `uc?id=` 경로가 "many accesses" 로 막힘 → `fetch_assets.py` 는 gdown 으로 목록만, 본체는 `drive.usercontent.google.com/download?id=…&confirm=t`(같은 시점에 200). itch.io 배포분(Fantasy Props MegaKit·Universal Animation Library·Bestiary)은 세션 서명 URL 이라 curl/헤드리스 실패 — 수동. Kenney 는 직접 zip. Fab/Quixel 은 계정 로그인이라 자동화 불가. `mesh_doctor` 는 glb/obj 만(FBX 미지원). `RawAssets/` 는 gitignore.
- **WS 동시 처리**: 응답 순서 비보장 — prompt=msg_id, location_decision=request_gen 으로 수신 측 매칭. 소켓 쓰기는 `_ws_send_lock` 직렬화.
- **대화 입력 경로(음성 폐기 후)**: HUD `ChatInput` + `SayToNpc` Exec → `PlayerInteractionUtils::SendDialogueToNpc` 수렴. 자막 `HandleNPCDialogue → ShowSubtitle`, 길이 비례 타이머.
- **print+이모지 = 파이프 지뢰**: stdout 이 파이프/리다이렉트면 cp949 `UnicodeEncodeError` → LangGraph 노드 통째 사망 → 빈 배치 폴백. 런타임 print 에 이모지·em-dash 금지, logger 는 삼켜서 안전. 스크립트는 상단 `sys.stdout.reconfigure(encoding='utf-8')`.
- **`style` 파라미터**: 어휘 단일 소스 = C++ `EMoveType`(Walk/Run/Sprint/Crouch), 미매칭은 Walk 폴백 + Warning. 액션별 의미 다름(Move/Follow=속도, Sing/Emote=미디어 키). `EMoveType` 추가 시 `ParseMoveStyle` 분기 필수(`test_move_style_vocabulary_matches_cpp` 가 잡음).
- **`.claude/skills/` 는 디렉터리 형식만 로드된다**: `<name>/SKILL.md` 여야 하고 frontmatter `name:` 은 디렉터리명과
  같은 kebab-case. 평면 `skills/foo.md` 는 조용히 무시된다(에러 없음 — 스킬 목록에 안 뜨는 걸로만 판정 가능).
  `.claude/` 전체가 gitignore 라 이 폴더의 소실·변경은 **git 으로 추적·복구 불가**. 이름이 유저/플러그인 스킬과
  겹치면 그쪽이 이기므로 프로젝트 스킬 이름은 충분히 구체적으로(`spec` 같은 일반어 금지).
- **Mixamo 액션 애니는 In Place 배포본이 없다 — 루트 모션을 켜는 게 정답**: 이동이 Hips 트랙에 통째로 들어있어
  ① 루트모션 OFF 면 메시만 끌려갔다가 몽타주 끝에 캡슐로 스냅백 ② `bForceRootLock` 으로 묶으면 "밀려나는 그림인데
  제자리". 시퀀스 `bEnableRootMotion=true` + `bForceRootLock=false` + AnimBP `RootMotionFromMontagesOnly`
  (ABP_SmartNPC 는 이미 그렇게 돼 있음) 조합이어야 몽타주가 캡슐을 끌고 간다. 이때 코드로도 밀면 이중 이동.
- **`MaxWalkSpeed=0` 은 이동 잠금으로 쓰면 안 된다**: Walking 복귀 순간 `CalcVelocity` 가 직전 `LaunchCharacter`
  속도까지 0 으로 클램프해 한 발짝도 안 밀린다. `DisableMovement` 도 같은 이유로 못 씀(밀림 속도 소멸).
- **FBX 임포트 함정 2개**: `import_mesh=False`·`FBXIT_ANIMATION` 을 줘도 **SkeletalMesh·PhysicsAsset·Material 이
  같이 생성**되므로 임포트 후 잔재 정리가 필수. Mixamo FBX 는 take 2개(`_mixamo_com` = 진짜, `_Take_001` = 빈 트랙)로
  들어오니 본 회전 변화량으로 골라낼 것(`AnimationLibrary.get_bone_pose_for_time`).
- **cpp 만 고쳤으면 MCP 로 Live Coding**: `execute_console_command(None, "LiveCoding.Compile")` → UE 로그
  `Live coding succeeded`. 에디터·PIE 안 끄고 반영된다. 헤더를 건드렸으면 이 길은 없고 `quit_editor`→빌드→재실행.
- **WS 는 서버보다 먼저 뜨면 Offline Mode 로 고착**: 5회 재접속 실패 후 `Switching to permanent Offline AI Mode`.
  **PIE 재시작으로는 안 풀리고 에디터를 재시작해야 한다.** 상태 확인은 `curl 127.0.0.1:8000/api/ws/status`.
- **에디터 MCP**: `ue_run_python` 으로 에셋·프로퍼티·`WidgetTree`(`find_object(".../WBP:WidgetTree.X")`) 편집 가능. **K2Node 그래프 노드만 불가** → 사용자 수작업. RemoteControl 설정은 `Saved/Config/…/RemoteControl.ini`(미추적) — 새 환경마다 UI 재설정. 에디터 켜진 채 에셋 파일은 잠김(`git rm` "Invalid argument").
- **gitignore(2026-09-24 현행)**: `personas/`·`knowledge/`·`models/`·`.claude/`·`.mcp.json`·`*.txt`·`.obsidian/` 로컬 전용. `docs/`·`tests/` 는 추적(docs 는 2026-09-21 부터). NPC 4인: Skadi(과격 여성 해적선장)·Moca(ASMR 여성 스트리머)·Elara(근엄 남성 기사단장)·James(Skadi 해적단 항법사).

### E. 파인튜닝
→ `OmniAgent_VR_System/CognitiveEngine/finetune/RESULT.md` "운영 주의점" 절. Stage2 12B 는 unsloth 미지원으로 보류(서빙은 qwen3:8b 로 대체) · VRAM 스필오버는 OOM 아닌 감속 · GGUF 변환 베이스 태그 인자 · 시드 데이터 페르소나 규칙.

### J. Jevlike 전술 편향기 — 설계 근거는 `docs/SPEC_jev_neuro_symbolic_st.md`
- **C++ Gotchas 방어**: Jev 승수 연산 시 `[0.25, 4.0]` Clamp 필수(후보 전멸 방지), `MoveToLocation` 호출 시 `bProjectDestinationToNavigation=true` 투영 필수(BaseMove:585 기본값 false 로 인한 벽 끼임 버그 방지).
- **daily(2026-09-24)**: 비전투 반사는 Jev 활동을 끊지 않는다 — 중립 주민 경계 Scan(쿨다운 10s)이 앉기·산책을 시작 20ms 만에 잘랐다. 전투 진입 반사·LLM 배치·전투 셀렉터만 선점. 대화 직후 8s 는 비전투 반사 억제(말한 상대 보기 유지).
- **SetFocalPoint 는 원래 몸을 안 돌렸다**: SmartNPC 는 `bOrientRotationToMovement` 전용이라 TurnTo·Scan 이 시선만 바꿨다. 지금은 `BaseFaceRotate` 가 컨트롤러 목표 회전 추종으로, `PrepareMove`·Track 이 진행 방향 회전으로 전환한다. 새 이동 경로를 만들면 이 전환을 거칠 것.
- **Jev 검증은 에디터 포그라운드 조건**: 백그라운드 3fps 면 응답이 0.3s 워치독을 넘어 전부 폐기된다. MCP 로 `/Script/UnrealEd.Default__EditorPerformanceSettings.bThrottleCPUWhenNotForeground=false` 후 검증(메모리만, 재시작 시 원복). 서버 재시작 시 UE 는 재연결 5회 후 Offline 영구 → PIE 재시작.

### S. 스토리 진행 & NPC 오케스트레이션 (2026-09-18 신설)
- **`Content/StarterContent/` 는 로컬 전용(gitignore) — 통째로 비면 `build_story_scene.py` 가 조용히
  안 죽고 예외로 죽는다**(2026-09-21 실측): 6폴더(Architecture·Materials·Particles·Props·Shapes·Textures)
  가 전부 0파일이면 상인 좌판(`Shape_Cube`) 로드에서 `RuntimeError: asset missing`. 원인 불명(git 이력
  없음). 복구는 `C:\Program Files\Epic Games\UE_5.5\Samples\StarterContent\Content\StarterContent` 에서
  그 6폴더만 복사(495MB, 대부분 Textures) — Audio/Blueprints/HDRI/Maps 는 불필요.
- **Elara 위치는 정적 감금이 아닌 Stage 2 동적 제어**: Elara를 레벨 시작부터 전초기지 우리에 박제하고 `CAPTIVE_PAIRS`로 전투를 억제하는 것은 부자연스럽다. `Story Directive`가 비트 진행(`b4_reforge_and_elara`)에 맞춰 주입되면, Stage 2 플래너가 이동(`MoveTo: OutpostCage`)과 포로 대기(`Wait/Pain`) steps를 산출해 동적으로 제어한다. 구출 후에도 플래너가 복귀 행동을 생성한다.
- **필드 몬스터 = C++ 경량 액터(AEnemySpawner / AEnemyCharacter)**: LLM 토큰을 쓰지 않으며 VR 90Hz 프레임 유지를 위해 동시 2~3마리로 제한. 스포너가 킬 수를 세어 `flag` 송신(`s_hunt_forest_raiders`), 네임드는 사망 시 `npc_died` 송신(`s_hunt_bridge_troll`).
- **적 클래스 설계 결정(2026-09-18 구현)**: `ASmartNPC` 파생이 아니라 `ACombatCharacter`(신설 베이스) 형제 — SmartNPC 는 피격마다 `SendEventReport` 로 서버에 보고하므로 상속하면 미등록 에이전트 이벤트가 서버로 샌다. 플레이어 스윙·투사체·노티파이·래그돌·아군 AI 사망 판정은 전부 베이스 타입만 본다. 보스 Vorg/DemonLord 는 여전히 BP_SmartNPC(호감도 시드).
- **적 애니는 AnimBP 없이 단일노드** — AnimBP 상태머신은 그래프 편집이라 MCP/Python 으로 못 만든다. `AEnemyCharacter` 가 속도로 Idle/Walk/Run 클립을 `PlayAnimation`, 공격은 클립 1회 + `AttackHitDelay` 시점 판정(노티파이 아님). 에디터에서 AnimBP 를 만들어 꽂아도 `AnimationSingleNode` 모드라 무시된다 — 쓰려면 생성자 모드 변경 필요.
- **적 릭 = Quaternius CharacterArmature ≠ X_Bot** — SmartNPC 의 ABP/PA/AM_* 재사용 불가(그래서 (a) 적만 교체 결정). 3캐릭터가 스켈레톤을 각자 가짐(같은 릭이지만 임포트가 분리). 무기 슬롯이 몸 텍스처를 공유해 색이 약간 어긋남(Todo).
- **적 AI 함정 3개(전부 실측)**: ① `MoveToActor` 기본 `bStopOnOverlap=true` 는 양쪽 캡슐 반경을 더해 ~187cm 에서 "도착" → 사거리 170 밖에서 교착 → `false` 로. ② `bUseRVOAvoidance` 켜면 타겟 앞 1.7~2.2m 에서 서로 피하느라 v=0 으로 얼어 32초 무타격 → 끄기. ③ 나무 수관(cone HISM)에 충돌이 있어 1.3~2.3m 높이에서 캡슐이 걸려 AI 가 끼임 → 수관 `collision=False`(`forest_patch`). 공격 슬롯은 거리 창이 아니라 `bEngaged` 플래그(틱 0.15s×415cm/s = 62cm 라 창을 건너뜀). 상태 확인은 `AEnemyAIController::GetDebugState()`(BlueprintCallable).
- **적 밸런스 실측**: 도적(ATK 18, HP 240) 2명이 플레이어 300HP 를 ~55s 에 깎음 — 플레이어 방어 0. 테스트 시 `CurrentStats.resources.health` 를 부풀려 진행.

### U. 로컬 환경 지뢰 — git·엔진·MCP (2026-09-21 실사고 3건에서 추출)
- **U-1. 브랜치 체크아웃이 ignore 파일을 지운다 — "A 브랜치는 추적·B 브랜치는 ignore" 인 경로가 있으면.** git 은 ignore
  파일을 소모품으로 봐서 A 체크아웃 시 **로컬본을 A 의 커밋본으로 덮어쓰고**, B 로 돌아올 때 **삭제**한다. 2026-09-21
  17:33→18:03 실사고: stale 로컬 `main`(99e1b6cd) 이 `docs/Memo.md`·`ROADMAP.md`·`주간기록/INDEX.md`·`W23`·
  `.agents/rules/debug.md`·`.mcp.json` 6개를 추적 중이었고 Develop 은 전부 ignore → Memo 57KB→18KB→소멸. **해결됨
  (같은 날)**: ① 로컬 main 을 `origin/main`(f3efae76, 6개 미추적) 으로 FF — `git fetch origin main:main`, ② `docs/` ignore
  해제·git 추적 시작(21:2x 커밋). 이제 남은 위험은 `.mcp.json`(여전히 ignore, 어느 브랜치도 미추적 — `debug.md` 는 2026-09-24 삭제, `.agents/rules/` 는 추적 전환)
  뿐이라 같은 사고는 못 남. 재발 점검 명령: `git ls-tree -r <브랜치> --name-only -z | git check-ignore -z --stdin` 이 비어야
  함. 복구 소스 우선순위(ignore 파일 한정): 클로드 `~/.claude/file-history/<세션>/<경로해시>@vN`(경로↔해시는 세션
  `.jsonl` 의 `file-history-snapshot.trackedFileBackups`) → 옛 브랜치 `git show <ref>:<경로>` → 없음.
- **U-2. Installed Build 엔진에서 "Expecting to find a type ... in 'UE5Rules'" = 플러그인 끄는 게 답.** 런처 설치판은
  `Engine/Build/InstalledBuild.txt` 로 `Unreal.IsFileInstalled()` 가 Engine/ 하위 전부를 "완성품" 취급 → 룰 캐시
  `UE5Rules.dll` 은 **파일이 없어도·내용이 틀려도 재컴파일 안 함**. `.uproject` 에 새 플러그인이 켜졌는데 캐시가 그 이전
  시점이면 저 에러가 나고 자연 회복 없음. 하지 말 것: 캐시 삭제(재생성 불가, Verify 로만 복구) · 마커 rename(UBT 가
  소스빌드로 오판, 서드파티 헤더 없어 846 파일 C1083). 할 것: 해당 플러그인 `Enabled:false`, 진짜 필요하면 런처 Verify 후
  Epic 이 배포한 캐시 상태로 쓰기. VisualStudioTools 는 VS 의 "C++ 게임 개발" 워크로드 컴포넌트(마켓플레이스 확장 아님)가
  프로젝트를 열 때마다 자동 활성화하던 것 — 사용자가 2026-09-21 컴포넌트 제거, `.uproject` 도 false 고정.
- **U-3. `.mcp.json` 은 로컬 전용(ignore) — MCP 서버가 세션 중 한꺼번에 사라지면 이 파일부터 본다.** ue5(`.venv/Scripts/
  python.exe tools/ue_mcp/server.py`)·comfyui(`tools/comfyui_mcp/server.py`, `COMFYUI_URL=http://127.0.0.1:8188`)·
  code-review-graph(`uvx code-review-graph serve`) 3개 정의. 파일이 살아 있어도 세션 내 재연결은 안 됨(`/mcp` 또는 새 세션).
  ue5 서버가 붙으려면 에디터가 떠 있어야 하고(Remote Control `:30010` 응답이 준비 신호), 에디터가 떠 있어도 이 파일이 없으면
  도구 목록에 안 뜬다. `sol_pi.py` 는 에디터 켜진 상태에서 Build.bat 을 돌리면 Live Coding 충돌로 실패 — `quit_editor` 먼저.
- **U-4. `sol_pi.py build` 워치독 300초** — 룰 캐시 재생성처럼 오래 걸리는 빌드는 중간에 죽고 `cl.exe` 만 taskkill 됨
  (`dotnet.exe` 호스트는 안 죽음). 워치독에 끊긴 뒤 재실행하면 이미 컴파일된 `.obj` 는 재사용되니 그냥 다시 돌리면 된다.
