# W37 (2026-09-07 ~ 09-13) — VR 손 쥐기 일원화 · 음성 파이프라인 폐기 · 리팩토링 1~6단계 · SoL-Pi

<!-- Memo.md Done(09-07·09-12) + Todo [x] 항목 + DoList Done(09-07~09-12) + 커밋 68건 기반 이관 작성(2026-09-13). -->

## 핵심

① **VR 손 쥐기 일원화**(09-07) — 무기는 `EquipItem`(메시 부착), 일반템은 `TakeItemToHand`(액터 스폰)로 갈라져 있어 그립을 놓을 때 3중 분기(`DropEquippedItem`·`UnequipItem`·`ReleaseHeldItem`)가 필요했다. **손에 쥐는 모든 것은 물리 액터(`ADroppedItemBase`)** 원칙으로 통일하고, 왼손 그립·양손 독립 쥐기·쥔 무기 근접 판정까지. PIE 6항목 전부 통과. 하루 중 조작 규칙이 한 번 뒤집혔다 — "인벤토리 열림 = 회수 / 닫힘 = 던지기" 가 최종.

② **음성 파이프라인 폐기**(09-11~12) — ASR·TTS 가 기능 개발에 불필요하고 불편만 유발한다는 판단. HUD 텍스트 채팅(`ChatInput`)으로 대체. TTS GPU 상주 ~4GB 도 함께 사라졌다. 복원은 `bd057b8` 이전 이력.

③ **리팩토링 1~6단계**(09-12, `SPEC_refactor_encapsulation`) — 하루에 커밋 40+. 죽은 코드 삭제(C1~C12) → 세 벌→한 벌(2단계) → 캡슐화(3단계) → Python 구조(4단계, main.py 931→658줄) → Envelope 직결(5단계) → **래그돌 컴포넌트 추출**(6단계, SmartNPC 795→339줄). pytest 51 passed.

④ **SoL-Pi·Mesh Doctor·AGENTS.md**(09-12) — 빌드 로그 3,000줄 대신 에러만 슬라이싱하는 `tools/sol_pi.py`(`verify all` = UBT 빌드 → pytest 51 → UAT 헤드리스 연쇄, watchdog 타임아웃). 3D 에셋 진단 `tools/mesh_doctor.py`. 규칙 문서를 3-Tier(AGENTS.md 50줄 → `.agents/rules/*` 온디맨드 → SPEC)로 재편.

그 밖에 Stage2 플래너 12B→8B(qwen3:8b) 교체 — 12B 가 `Moca`→`Maca` npc_id 환각(조용한 실패), 8B 는 0.8s 로 정확.

## 주요 작업

### VR 손 쥐기·장착·투척 일원화 (09-07, 커밋 17건)

- **파이프라인 일원화**(`d87bc9f`·`81de783`) — `ActivateItem` 에서 무기/일반 불문 `TakeItemToHand`. `HeldItem` 상태와 `AttachItemToHand`/`ReleaseHeldItem`/`TakeItemToHand`/`StoreHeldItem` 은 `InventoryComponent` 로 이관. `VRPawn::HandleGrabRelease` 는 `GetHeldItem` 하나만 보고 `[열림=회수 / 닫힘=던지기]` 3줄.
- **왼손 그립**(`a765dd1`) — 방패를 왼손에 장착하면 뺄 방법이 없었다(인벤 패널은 `InventorySlots` 만 그리고, 오른손 우회로는 `MainHand` 하드코딩). `IA_GrabLeft` + `HeldItem`→`HeldItems`(손 슬롯 키 맵).
- **쥔 무기 근접 판정**(`ae3e842`) — 손 위치 구 하나만 검사해 칼날 거리에서 무반응. 쥔 아이템의 방향 상자(로컬 바운즈)로 판정, 질량도 마스터 테이블 Weight(장검 4.5kg·단검 1.2kg).
- 버그 수정 — 반대 손·NPC 가 쥔 물건을 다시 집음(`7e9b3bd`), 왼손 뒤집힘(`4aa0728`), 던진 아이템 NPC 통과(`6720a64`), 장착품이 인벤으로 안 돌아옴(`b3c2668`), 인벤 장착 미실행(`da1f362`), 넉다운 기상 시 누운 채 서기(`8293f3a`).
- **조작 규칙 반전 기록** — 오전에 "인벤토리 열린 채 놓으면 수납" 을 임의 기능으로 판단해 완전 삭제했다가, 사용자가 홀드 조작(`Started` 쥠 / `Completed`·`Canceled` 놓음)을 명시하면서 `bInventoryOpen` 분기가 다시 들어왔다. 시간 가드는 없다(열림 여부로 갈리므로 꺼내자마자 떼도 증발 안 함).
- **NPC Repair 액션 제거**(`ff57070`·`3a35973`) — 모루 같은 설비 없이 몽타주만 재생하고 내구도를 올리던 것. 설비 도입 시 재설계. Python `schemas/actions.py` 동조.
- 치트 `Cheat_Unequip <0|1>` 통합, `StoreHeldItemInInventory` Exec. 죽은 에셋 3종(DataAsset 2·ForceFeedback 1) 삭제(`af845bd`). `Refactoring.md` 폐기(`3399343`).

### Stage2 플래너 12B → 8B (09-08, `9a3f029`)

- `STAGE2_MODEL` gemma4-12b(7.4GB) → qwen3:8b(5.2GB, `MODELS["mid"]` 기정의).
- 실측(2 NPC plan): 12B 2.1s·`Moca`→`Maca` 환각 2/2 / 8B 0.8s·정확. npc_id 환각은 UE5 가 NPC 를 못 찾아 plan 이 통째로 버려지는데 로그엔 성공으로 남는 **조용한 실패**.

### 태그 미러 제거 · PR #25 (09-10, `b2fd841`·`c7defbb`·`fd815bf`)

- `State.Action.*` 태그는 쓰기만 하고 읽는 곳 0(에셋 레지스트리 의존성 스캔: `/Game` 488개 중 GameplayTags 의존 BP/AnimBP/ST 0개). `GetGameplayTagForAction`(34-case)·`TransitionStateTag`·`RevertStateTagToIdle` 삭제.
- PR #25 `feature/player-systems` → Develop 머지. VisualStudioTools 플러그인 도입.

### 음성 파이프라인 폐기 → 텍스트 채팅 (09-11~12, `bd057b8`·`ef8c663`·`d752664`·`bff6a5d`)

- Python: `npc_audio.py`·`tts_client.py`·`TTSService`·`ASRService` 제거. C++: `VoiceInputComponent`·`NPCAudioStreamComponent` 제거, BP 재저장.
- 대화 입력 — HUD `ChatInput`(EditableTextBox, 최근접 NPC) + `SayToNpc` Exec 둘 다 `PlayerInteractionUtils::SendDialogueToNpc` 로 수렴. 자막은 `HandleNPCDialogue → ShowSubtitle` 즉시, 길이 비례 타이머 후 숨김.
- TTS·ASR 후속 3건(partial 스트리밍·Lip Sync·VAD barge-in) 폐기.

### 리팩토링 1~6단계 (09-12, `SPEC_refactor_encapsulation` 완료)

- **1단계 죽은 코드 C1~C12** — 커밋 12개(`f485ad0`…`08b42f0`). C3 는 `FSTEvaluator_NPCState` 유지로 축소(ST_NPC 에셋이 사용). `action_failed` 엔벨로프 양쪽 삭제. `location_decision` 폴백 ID `OPTIMAL_0`→`OPTIMAL`.
- **2단계 세 벌→한 벌** — 커밋 8개(`9c2069b`…`76f94d4`): `SubsystemUtils`·`MovementUtils`(대쉬·회피 공용)·`EngineShapes`·`PickWeightedIndex`(반사 룰·전투 셀렉터)·`OmniAgentConfig` 1회 로드·`MCPJsonUtils ParseObject/ToString`(9곳)·`llm_factory.get_ollama_client`·`db_manager.reputation_tag_for`. + 부록 A Python 죽은 코드(`1dcf14d`).
- **3단계 캡슐화** — 커밋 9개(`eba241c`…`675e54e`): `SetPhysicsFrozen`(4곳→1)·`TryPickupInto`+`GetItemsInRange` 액터 반환+`ItemTable` 캐시·`TryOccupyAndSeat`·`FCheckpoint`·`UNPCMap` 흡수·`UNetworkClientBase` 제거·`FPerceptionData` 생성자·`ExitCombat/StopSightTracking`·`PrepareMove/StopTracking`. BP 영향 없음.
- **4단계 Python 구조** — 커밋 6개(`a33a51b`…`88b1169`): vr_context 정규화(rules→dialogue 역방향 import 소멸)·`GesPrompt(PromptPayload)` 상속·`fallback_batch`+`DEFAULT_NPC`·`llm_factory.model_for_importance`(README "세 곳 고쳐라" 폐기)·print→logger 65곳·`ServerState`+`debug_routes.py`(main.py 931→658줄). pytest 51 passed.
- **5단계 Envelope 직결**(`7dd7904`) — `FEnvelopeBuilder::Build*` 가 `TSharedRef<FJsonObject>` 를 받아 메시지당 재파싱 1회 제거. `BuildPerceptionReport` 객체 반환.
- **6단계 래그돌 추출**(`5a5b293`) — `UNPCRagdollComponent`: SmartNPC.cpp 795→339줄, .h 367→225줄. 액터는 `NoteHit`/`ReactToHit`/`EnterDeathRagdoll` 만 호출, 틱 없음. BP_SmartNPC 오버라이드 15개 중 실제 다른 값은 기상 몽타주 2개뿐 → 컴포넌트 생성자 `FObjectFinder` 기본값으로 확정, BP·레벨 재저장(로드 경고 0).
  - **C2011 교훈** — `SmartNPC.h` 와 `NPCRagdollComponent.h` 양쪽에 `enum class EKnockdownPhase` 중복 정의 → 재정의 에러 + 연쇄 28건. `NPCActionTypes.h` UENUM 단일 선언으로 해결. 공용 enum 은 클래스 헤더에 로컬 정의 금지.
- ABP_SmartNPC 이벤트그래프 컴포넌트 조회 제거(`7bcd6c3`) — 프리뷰(`/Engine/Transient` `SkeletalMeshActor`)엔 `NPCStateComponent` 가 없어 None 경고. 자세 플래그는 C++ 상속 변수 `bIsSit`/`bIsLie` 직접 참조. 그래프 노드는 MCP 불가라 사용자 수작업(DoList 1-0b, W34 부터 열려 있던 항목).

### SoL-Pi · Mesh Doctor · AGENTS.md (09-12, 미커밋)

- **`tools/sol_pi.py`** — `build`(에러/경고만 슬라이싱, 토큰 최대 95% 절약)·`log`(`Saved/Logs/UE5_MCP_VR.log` 런타임 에러만)·`status`·`test [all|python|engine]`·**`verify all`**(UBT 증분 빌드 → pytest 51 → UAT 헤드리스 연쇄). 원시 로그는 `Saved/Logs/sol_pi_raw.log` 격리, 7줄 영수증만 반환.
  - UHT 리플렉션 에러 우선 격리(종속 MSVC 에러 마스킹), 샌드위치 슬라이싱(최초 5 + 마지막 1), watchdog(빌드 300s·UAT 120s·pytest 60s, 초과 시 `taskkill`).
- **`tools/mesh_doctor.py`** — GLB/OBJ 비다양체·부유 파편·노멀 뒤집힘·과폴리·VR 스케일 이상 `diagnose`/`heal`, UE 콜리전·Nanite 감사 `ue-audit`.
- **AGENTS.md 3-Tier** — 상시 주입 3,500+ 토큰/턴 낭비 해소. Tier 1 루트 50줄(CLAUDE.md 와 SSOT 동기) → Tier 2 `.agents/rules/{ue5_cpp,python_backend,sol_pi,mesh_doctor}.md` 온디맨드 → Tier 3 SPEC 은 Planning 단계만.

## 메모

- **손 쥐기 조작 규칙(최종)** — 그립 홀드. 뗄 때 인벤토리 열림 = 회수 / 닫힘 = 거래접시→NPC 건네기→던지기. 장착 무기는 `AttachedMeshes` 로 붙고 `HeldItem` 이 아니므로, 던지려면 `OnGrabStart` 에서 물리 쥐기로 전환해야 `LaunchThrown` 경로를 탄다. 임의 기능 추가는 사용자 명시 없이는 금지(이날 두 번 뒤집힌 원인).
- **VRAM 16GB OOM 미해결** — 12B+e4b+PIE 동시 부하 시 Ollama 500/ReadTimeout. TTS GPU 폐기 후 재현 여부 미확인. 재현 시 `num_ctx` 축소(KV 양자화는 크래시 불가). DoList 3.
- **월드 아이템 Python 전송 결정 보류** — `ItemManager::SerializeActiveItemsToJson` 스텁 삭제(`data` 항상 빈 배열·호출자 0·Python `EEnvelopeType` 미대응). 소비처(프롬프트 주입 vs 조회 도구)부터 정해야 스키마가 안 흔들린다. DoList 3.
- **HandObject 제시 몽타주 부재** — `ExecuteHandObject` 는 `EquipItem` 만 불러 들고 서 있는 것으로만 보인다. `DA_NPC_Actions` `HandObject` 키에 팔 뻗는 몽타주 할당하면 C++ 수정 없음(에셋 없어 보류).
- **`WidgetTree` 편집은 파이썬으로 가능** — `find_object(".../WBP:WidgetTree.StatusBox")` + `add_child_to_vertical_box`. 그래프 노드(K2Node)만 불가.
- **에디터 켜진 채 파일 잠김** — 부모 클래스 부재 에셋은 MCP `delete_asset` 실패 → 에디터 닫고 파일 삭제. 켜져 있으면 `git rm` 이 "Invalid argument".

## 사용자 작업 (DoList Done)

- 09-07 물리 손 쥐기 PIE 6항목(1-9) — 쥐기/따라옴/정지 놓기/던지기 타격/120cm 건네기/꽉 참 폴백. 손안 자세는 `DT_ItemRegistry` `HoldOffset`/`HoldRotation`(빈 값은 `DefaultHoldOffset/Rotation`), `TuneGrab` 으로 찍어 CSV. 투척 세기는 `KineticDamageScale`(근접 스윙과 공유).
- 09-07 그립 뗄 때 분기 확인(1-8) · VR 공간 UI 이름표·거래 버튼 튜닝(1-10, `TradeTableHeight` 90cm·`ButtonPressRadius` 8cm·`PlateRadius` 25cm) · 죽은 DataAsset 2종 삭제(1-7) · 대쉬 에셋·튜닝(1-6, `DashDuration` 0.15s·`DashDistance` 300cm, 멀미 시 `DashDuration` 먼저, 하한 0.02).
- 09-07 아이템 사용·드랍·장착 PIE(2-8) — `Bread` HP+15/스태미나+25, `HealthPotion` 클램프, `KnightSword`·`Shield_Knight` 동시 장착, 드랍 벽 너머 안 나감, NPC "빵 먹어" `UseItem`(`Eat` 액션 없음 — 파싱 폐기), NPC "버려" 실제 액터.
- 09-07 아이콘·수량 잔여 검증(2-9) · NPC GiveItem·HandObject PIE(2-7, CDO 공통 5종 + Moca 4종).
- 09-11 텍스트 채팅 입력(1-12) — `WBP_PlayerHUD` 에 `ChatInput`·`ChatLog` MCP 추가(`StatusPanel` 112→292, `HUDPanelDrawSize` 500→660). Enter 포커스→전송·닫힘, 8줄 롤링.
- 09-12 리팩토링 1~3단계 PIE 회귀(1-12) — 채팅 응답 정상, 3단계 회귀 5항목(쥐기/던지기/수납·거래 접시·픽업·착석·전투 해제) 정상.
- 09-12 ABP_SmartNPC 프리뷰 None 경고 제거(1-0b, `7bcd6c3`).
- 09-13 래그돌 컴포넌트 추출 회귀(2-10) — Flinch 복귀·넉다운→안착→기상→AI 재개·사망 래그돌 3초 제거 정상. PR #26 생성.

## 커밋

| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 09-07 | `1820011` | refactor: 검증 끝난 디버그 코드 제거 — VRPawn, VoiceInputComponent, SmartNPC |
| 09-07 | `da1f362` | fix: 인벤토리 장착이 실행되지 않던 문제 — VRPawn |
| 09-07 | `10be1f7` | refactor: ComfyUI MCP 서버 견고화 — server.py |
| 09-07 | `3399343` | docs: Refactoring.md 폐기 — 잔여 항목 처리 완료 |
| 09-07 | `af845bd` | chore: 참조 0인 죽은 에셋 3종 삭제 — DataAsset 2종·ForceFeedback 1종 |
| 09-07 | `81de783` | refactor: 그랩 함수 통합 및 손 보유 상태 InventoryComponent 이관 — VRPawn·InventoryComponent |
| 09-07 | `9a513b3` | feat: 장비 해제 UI 브리지 — PlayerHUDWidget |
| 09-07 | `a765dd1` | feat: 왼손 그립 추가 — 양손 독립 쥐기·장착 |
| 09-07 | `b3c2668` | fix: 장착품이 그립을 떼도 인벤토리로 돌아오지 않던 문제 — VRPawn |
| 09-07 | `ff57070` | refactor: NPC Repair 액션 제거 — 모루 도입 시 다시 설계 |
| 09-07 | `d87bc9f` | refactor: 손에 드는 경로를 물리 액터 하나로 통일 — InventoryComponent·VRPawn |
| 09-07 | `7e9b3bd` | fix: 손에 쥔 물건을 반대 손·NPC 가 다시 집던 문제 — InventoryComponent |
| 09-07 | `4aa0728` | fix: 왼손에서 아이템이 뒤집혀 잡히던 문제 — InventoryComponent |
| 09-07 | `8293f3a` | fix: 넉다운에서 일어날 때 몸이 누운 채로 서던 문제 — SmartNPC |
| 09-07 | `6720a64` | fix: 던진 아이템이 NPC 를 통과해 피해가 없던 문제 — DroppedItemBase |
| 09-07 | `ae3e842` | feat: 쥔 무기까지 근접 판정 확장 — VRPawn |
| 09-07 | `5133517` | chore: 손안 자세 튜닝값 및 편집 중 에셋 반영 — DT_ItemRegistry·X_Bot·레벨 액터 |
| 09-08 | `9a3f029` | perf: Stage2 플래너를 12B → 8B 로 교체 — llm_factory |
| 09-10 | `b2fd841` | refactor: 읽히지 않는 State.Action 태그 미러 제거 — NPCActionComponent |
| 09-10 | `fd815bf` | chore: VisualStudioTools 플러그인 도입 및 VS 산출물 무시 — .uproject·.gitignore |
| 09-10 | `35912e7` | chore: 전투 셀렉터 간격 튜닝 및 편집 중 에셋 반영 — NPCActionComponent·ItemRegistry·레벨 액터 |
| 09-10 | `3a35973` | fix: Repair 제거 사항 Python과 동조화 |
| 09-10 | `c7defbb` | Merge pull request #25 from jukanmi/feature/player-systems |
| 09-11 | `bd057b8` | feat: ASR, TTS기능이 기능개발에 불필요하고 불편함을 유발한다 판단하여 비활성화하고 채팅 옵션을 추가 |
| 09-12 | `f485ad0` | refactor: 미사용 HUD 게터 6개 삭제 — PlayerHUDWidget |
| 09-12 | `1b6978d` | refactor: NPC 죽은 코드 삭제 — NPCStateComponent·SmartNPC·NPCActionComponent·SmartNPCAIController |
| 09-12 | `a945024` | refactor: 미사용 미디어 키 상수 삭제 — NPCActionKeys |
| 09-12 | `1a55c44` | refactor: Network 죽은 코드 삭제 — MCPJsonUtils·MCPMathUtils |
| 09-12 | `9dd346e` | refactor: Inventory·Furniture 죽은 코드 삭제 — ItemManager·ItemDataAsset·FurnitureManager |
| 09-12 | `b1f5c6d` | refactor: VRPawn·Entity 죽은 코드 삭제 — SendNPCDialogue·SetStateTag |
| 09-12 | `57a4caf` | fix: ExecuteDrop 소음 태그 누락 — NPCActionComponent |
| 09-12 | `580e1f7` | fix: location_decision 폴백 ID OPTIMAL_0→OPTIMAL — main.py·NPCActionTypes.h |
| 09-12 | `08b42f0` | refactor: action_failed 엔벨로프 양쪽 삭제 — EnvelopeBuilder·envelope.py·middleware.py·main.py·state.py·tests |
| 09-12 | `ef8c663` | refactor: TTS 파이프라인 제거 — main.py·npc_audio.py·tts_client.py·TTSService·ASRService |
| 09-12 | `d752664` | refactor: 음성 입력·오디오 스트림 컴포넌트 제거 — VoiceInputComponent·NPCAudioStreamComponent·VRPawn·NPCManager·BP_SmartNPC·BP_VRPawn |
| 09-12 | `bff6a5d` | chore: 음성 컴포넌트 삭제 후 BP 재저장 — BP_VRPawn·BP_SmartNPC |
| 09-12 | `9c2069b` | refactor: 서브시스템 조회 단일화 — SubsystemUtils·ItemManager·FurnitureManager·NPCManager·호출부 8곳 |
| 09-12 | `480691d` | refactor: 마찰 0 등속 돌진 공용화 — MovementUtils·VRPawn 대쉬·NPCActionComponent 회피 |
| 09-12 | `b89e924` | refactor: 가중치 추첨 단일화 — NPCActionComponent PickWeightedIndex(반사 룰·전투 셀렉터) |
| 09-12 | `03ef7d6` | refactor: 엔진 도형·이미시브 MID 로딩 공용화 — EngineShapes·VRPawn·TradeSessionActor |
| 09-12 | `223d34d` | refactor: OmniAgentConfig 캐시 3쌍 → 1회 로드 구조체 — OmniAgentConfig |
| 09-12 | `bb87a13` | refactor: JSON 문자열 파싱·직렬화 단일화 — MCPJsonUtils ParseObject/ToString, 호출부 9곳 |
| 09-12 | `543ed1d` | refactor: Ollama httpx 클라이언트 싱글턴 통합 — llm_factory.get_ollama_client·main.py·test_llm_perf |
| 09-12 | `76f94d4` | refactor: db_manager 관계 태그·UPSERT SQL 중복 제거 — reputation_tag_for·_UPSERT_RELATION_SQL |
| 09-12 | `1dcf14d` | refactor: Python 죽은 코드 삭제 — world_constants·RejectResult·EQSQueryResult·Entity·AgentState 필드·Heal 규칙·tests 잔재 |
| 09-12 | `eba241c` | refactor: 아이템 물리 잠금 캡슐화 — DroppedItemBase::SetPhysicsFrozen, InventoryComponent·TradeSessionActor 호출부 |
| 09-12 | `d03856e` | refactor: 아이템 픽업 캡슐화·ItemManager 테이블 캐시 — DroppedItemBase::TryPickupInto·GetItemsInRange 액터 반환·VRPawn::FindNearestItem |
| 09-12 | `139e217` | refactor: 가구 착석 캡슐화 — FurnitureActor::TryOccupyAndSeat, VRPawn·NPCActionComponent 호출부 |
| 09-12 | `7ddbdd6` | refactor: 체크포인트 4멤버 → FCheckpoint 구조체 — PawnDeathUtils·VRPawn |
| 09-12 | `ccd355c` | refactor: UNPCMap 을 NPCManager 에 흡수 — ActiveNPCs 직접 보유, 래퍼·널가드 제거 |
| 09-12 | `a63b3b3` | refactor: UNetworkClientBase 제거 — ULLMNetworkClient 가 UWebSocketClient 직접 상속, NPCManager SendMessage |
| 09-12 | `0ab4e4c` | refactor: 전투 해제 단일화·FPerceptionData 생성자 — SmartNPCAIController ExitCombat/StopSightTracking, 조립 5곳 |
| 09-12 | `675e54e` | refactor: 이동 전처리·추적 해제 단일화 — NPCActionComponent PrepareMove/StopTracking |
| 09-12 | `a33a51b` | refactor: vr_context 정규화 단일화·GesPrompt(PromptPayload) 상속 — interface_input·dialogue·rules·main.py |
| 09-12 | `d9dedbe` | refactor: 폴백 배치·DEFAULT_NPC 단일화 — schemas/actions.fallback_batch, supervisor·interface_output·dialogue |
| 09-12 | `1d83b92` | refactor: importance→모델 매핑·메모리 Event 헬퍼·인라인 import 정리 — llm_factory·main.py·middleware·debug.html |
| 09-12 | `0421a1d` | docs: 모델 라우팅 절 — importance 매핑 단일 소스 반영 — CognitiveEngine README |
| 09-12 | `830a830` | refactor: print → logging.getLogger 일괄 전환 — agents 5·utils 4 파일 65곳 |
| 09-12 | `88b1169` | refactor: ServerState 도입·디버그 REST 분리 — server_state.py·debug_routes.py·main.py 931→658줄 |
| 09-12 | `7bcd6c3` | chore: ABP_SmartNPC 이벤트그래프 컴포넌트 조회 제거 — 프리뷰 None 접근 경고 해소, 자세 플래그는 C++ 상속 변수 참조 |
| 09-12 | `7dd7904` | refactor: Envelope payload FJsonObject 직결 — EnvelopeBuilder·BuildPerceptionReport·SendEventReport, 호출처 3곳 재직렬화 제거 |
| 09-12 | `5a5b293` | refactor: 액티브 래그돌을 UNPCRagdollComponent 로 추출 — SmartNPC 795→339줄, 기상 몽타주 기본값 C++ 확정, BP_SmartNPC·레벨 재저장 |
| 09-13 | `0338038` | chore: 음성 폐기 뒷정리 — Server.bat TTS/ASR 기동 제거·README 실행법·헤더 주석 3곳 |
| 09-13 | `c163c6c` | docs: 파인튜닝 운영 주의점을 Memo.md 에서 finetune/RESULT.md 로 이관 |

> SoL-Pi(`tools/sol_pi.py`)·Mesh Doctor(`tools/mesh_doctor.py`)·`AGENTS.md`·`.agents/rules/*` 는 이 문서 작성 시점(09-13) **미커밋**(untracked).
