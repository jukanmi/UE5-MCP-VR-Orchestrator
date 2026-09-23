# Session Memo

세션 간 인수인계 단일 소스.
형식: `## Todo` (미완) · `## Done` (날짜 필수, 주간기록 이관 전 임시 적재) · `## Handoff Notes` (왜·제약·주의점만, 도메인별)
형식: `## Todo` (미완) · `## Done` (날짜 필수) · `## Handoff Notes` (왜·제약·주의점만)

---

## Todo

### 전투 SPEC 잔여 — `docs/SPEC_realistic_combat.md` (§3.2·§2.3 완료 2026-09-21, §3.5 공격토큰 중앙화는 2026-09-22 폐기 — 필드 몹 `MaxAttackers=2` 로 충분, 나머지 3개 보류)
- [x] ~~§3.4 Stumble~~ — 2026-09-23 구현·헤드셋 검증 완료(아래 Done).
- [ ] **저HP 자동 후퇴·회복 연쇄(척수)** — 2026-09-24 결정. Combat ∧ HP ≤ 0.3 ∧ 회복 Consumable 보유 → 주사위 없이 [EQS 후퇴 → UseItem] 결정론 주입, 전투당 1회. 회복템 없으면 기존 Flee 램프. 상세 `SPEC_jev_daily.md` 비고 B.1. 전투 브랜치에서 구현.
- [ ] **§3.1·3.3 구현** — 설계 확정 `SPEC_realistic_combat.md` §5(2026-09-22). 남은 순서: Footwork(Strafe/Disengage, `Key_Style` 변형, EQS 안 씀) → Startle 룰 → 투사체 회피 → 청각→시각 융합(`FPerceptionData.Context` + Python `context` 1필드). 새 EAction·컴포넌트 0.

### Jevlike 잔여 — 전투 `docs/SPEC_jev_neuro_symbolic_st.md` §9 (Phase 1~3 코드 완료 2026-09-22) · 일상 `docs/SPEC_jev_daily.md` (2026-09-23 신설)
- [ ] **StateTree 에셋 바인딩(전투 전용)** — 일상은 큐 주입이라 무관. 전투 승수는 `SelectCombatAction` 이 캐시를 직접 읽으므로 ST 노드가 실제로 막을 상태가 있는지부터 판단(SPEC_jev_daily 미결).
- [ ] **Phase 4 PIE 실측(전투)** — 적 조우, HP 저하 시 Flee 배율로 거리 벌리기, 백엔드 단절 시 1.0 중립 폴백. 폴백 검증은 휴리스틱만으로 가능.
- [ ] **jevlike 설치·체크포인트 학습 = 일상 M2** — 2026-09-23 확인: 패키지 미설치, `app/models/jevlike_tactics.pt` 없음 → 휴리스틱만 동작. LLM 로그엔 일상 액션 3건뿐이라 합성 데이터 필요. 전투+일상 합쳐 체크포인트 1개.

### 플레이어 시스템 잔여 갭 — `docs/SPEC_player_systems.md`
- [x] **[갭 3] VR 물리 손 쥐기 및 플레이어→NPC 전달** — 2026-09-05 C++ 구현·빌드 완료.
  `IA_Grab`(Digital) 생성 + `IMC_VR` → `OculusTouch_Right_Grip_Click` 매핑까지 MCP 로 완료.
  스냅 부착(물리 Off + `RightHand` 본) · 손 속도 던지기 · 투척 ½mv² 데미지(`KineticDamage` 재사용,
  2초 창) · 놓을 때 `HandOverRange` 내 NPC 인벤토리로 즉시 건네기(LLM 미경유).
  2026-09-07 후속: 그립은 홀드(`Started` 쥠 / `Completed`·`Canceled` 놓음), 놓기 분기는
  인벤토리 열림=회수 · 닫힘=거래접시→NPC 건네기→던지기. 쥔 상태(`HeldItem`)와 손안 자세 보정,
  `AttachItemToHand`/`ReleaseHeldItem`/`TakeItemToHand`/`StoreHeldItem` 은 `InventoryComponent` 로 이관.
  2026-09-07 PIE 검증 6항목 전부 통과(DoList 1-9) — 쥐기·추적·놓기·투척 데미지·NPC 건네기·
  꽉 찬 인벤 폴백까지. 잔여 없음.
- [x] **[갭 4] 구르기/회피** — 2026-09-04 대쉬로 구현 완료(`AVRPawn::OnDash`). 에디터 작업
  (`IA_Dash` 생성·IMC 매핑·멀미 튜닝)만 DoList 1-6 에 남음.

### 척수반사 테이블 — 잔여 1건 (PR #23 Develop 머지 완료 2026-08-22)
- [ ] **PIE 검증 5항목**(SPEC §7) — 학습 완료됨. 에디터 열고 검증 가능.

### LLM 대사·계획 품질 잔여 (2026-09-05 발굴)
- [ ] **대화 기록이 대사를 지배한다** — (2026-09-18 재주행: RAG·goal·num_ctx 배선 고친 뒤엔 James 2턴 반복이 사라졌다. 남은 건 장기 기록 오염 케이스) e4b(v1)가 직전 답변 문형을 그대로 복사한다. 오답이 한 번
- [ ] **대화 기록이 대사를 지배한다** — e4b(v1)가 직전 답변 문형을 그대로 복사한다. 오답이 한 번
  기록되면 이후 모든 턴이 그걸 따라가고, 인벤토리·plan 을 고쳐도 안 풀린다. 실측: plan 을 완전히
  빼도 "한번 확인해 보시겠어요?" 가 유지됐고 기록을 비우자 사라졌다. 근본책은 기록 주입량 축소
  (현행 최근 5턴) 또는 요약 주입. 지금은 오염 시 수동 초기화 외 방법이 없다.
- [ ] **Stage2 steps 가 전방 계획이 아니다** — 입력이 NPC 직전 대사 한 줄뿐이라 방금 한 말을
  요약할 수밖에 없다. goal 은 예시 JSON 으로 잡혔지만 steps 는 여전히 goal 의 되풀이. 지금은
  steps 를 Stage1 에 주입하지 않아 실害는 없다. 쓰려면 플레이어 발화·이전 goal 을 입력에 넣어야
  하는데, 격리 테스트에선 효과가 없었다(실제 흐름에선 미검증).
- [ ] **"줘" 에 Drop 이 나오는 경우** — PIE 에서 1회 실측. `Drop` 은 월드에 떨어뜨릴 뿐 전달이
  아니다. GiveItem/HandObject/Drop 구분을 프롬프트에 명시할지 검토.

### 3D 아이템 텍스처 잔여 품질 이슈 (2026-09-03 원인 규명, 조치 미완)
- [ ] 4건 미조치 — 상세는 `주간기록/2026-W36` 메모. 요약:
### 리팩토링 1단계 — 죽은 코드·음성 파이프라인 삭제 (브랜치 `refactor/dead-code`)
- [x] **`docs/SPEC_refactor_encapsulation.md` §6 C1~C12 완료(2026-09-12)** — 커밋 12개. C3 는 `FSTEvaluator_NPCState` 유지로 축소(ST_NPC 에셋이 사용, 부록 A 정정).
- [x] **2단계(세 벌→한 벌) 완료(2026-09-12)** — 커밋 8개(`9c2069b`…`76f94d4`): SubsystemUtils·MovementUtils·EngineShapes·PickWeightedIndex·OmniAgentConfig 1회 로드·MCPJsonUtils ParseObject/ToString·llm_factory.get_ollama_client·db_manager reputation_tag_for. 2-12(다음 틱 헬퍼)는 남은 곳 2곳뿐이라 생략. + 부록 A Python 죽은 코드 `1dcf14d`(§6.3 에 빠져 있던 항목).
- [x] **3단계(캡슐화) 완료(2026-09-12)** — 커밋 9개(`eba241c`…`675e54e`): `SetPhysicsFrozen`(아이템 물리 4곳→1)·`TryPickupInto`+`GetItemsInRange` 액터 반환+`ItemTable` 1회 로드·`TryOccupyAndSeat`·`FCheckpoint`·`UNPCMap` 흡수·`UNetworkClientBase` 제거·`FPerceptionData` 생성자·`ExitCombat/StopSightTracking`·`PrepareMove/StopTracking`. BP 영향 없음(삭제된 UPROPERTY 는 전부 서브시스템 소유). PIE 확인 항목은 DoList.
- [x] **4단계(Python 구조) 완료(2026-09-12)** — 커밋 6개(`a33a51b`…`88b1169`): vr_context 정규화(interface_input 이 객체를 state 로 반환 → `_vr_get` 계열·rules→dialogue 역방향 import 소멸)·`GesPrompt(PromptPayload)` 상속(수동 복사 10줄→1)·`fallback_batch`+`DEFAULT_NPC`·`llm_factory.model_for_importance`(README "세 곳 고쳐라" 규칙 폐기)·`_record_event_memory_bg`·인라인 import 상단화·print→logger 65곳·`ServerState`(STATE)+`debug_routes.py` 분리(main.py 931→658줄). pytest 51 passed.
- [x] **5단계 완료(2026-09-12)** — 2-14: `FEnvelopeBuilder::Build*` 가 `TSharedRef<FJsonObject>` 를 받아 메시지당 재파싱 1회 제거, `SerializePerceptionReport`→`BuildPerceptionReport`(객체 반환), `SendEventReport` 객체 인자. action_failed·제스처/스탯 결정은 §6.1 에서 이미 처리.
- 6단계(`UNPCRagdollComponent` 추출, 선택 — BP Details 값 이전 동반)는 별도 세션. `SPEC_refactor_readability.md` 는 §6 으로 대체됨.

  **1. 감폴리 미달 — 원인 확정·해결책 검증됨, 전체 적용만 남음.**
  `hy3dgen/shapegen/postprocessors.py` 의 `reduce_face()` 가 pymeshlab 감폴리에 `preservetopology=True` + `qualitythr=1.0`(MeshLab 기본 0.3)을 하드코딩해, 구멍 많은 복셀 메시에서 목표의 7배 근처에 멈춘다. 컴포넌트 수와는 무관(상관계수 0.341, StarPendant 는 컴포넌트 1개인데 24,508).
  `generate_textures.py` 에 `Hy3DFastSimplifyMesh(preserve_border=False)` 2단 감폴리를 추가해 해결 확인: Glasses 22,138→3,232 · StarPendant 24,508→3,159 · ShipWheel 22,636→4,123. **단 72종 전체 재생성은 미실행** — 현재 커밋된 세트는 6종만 이 수정이 적용된 혼재 상태.
  선행 조건: venv 에 `pyfqmr` 필요(설치 완료). `Hy3DSampleMultiView` 는 elevation 을 `{-90,-45,-20,0,20,45,90}` 로만 받는다(그 외 값은 KeyError).
### C++ 리팩토링 백로그 (별도 세션)
- [ ] **C++ (별도 세션)**: `OnTacticalCandidatesDone` 182줄(L·높음 §7 EQS) · `OnTargetPerceptionUpdated` 165줄(L·높음 §2 BB) · `OnLLMMessageReceived` 133줄→타입별 핸들러(M·중간). `ExecuteInteraction` 은 §3 switch 본질 → 유지 권장.

  **2. 검은 텍셀(미착색) — 해결 실패.**
  얇은 형상에서 텍셀의 58~83%가 순수 검정. 측정 신뢰성은 확인됨(면중심 1점·면적 12점·면적가중이 모두 일치). 카메라를 6뷰→10뷰로 늘려도 WineCup 80.9%→66.2%, Glasses 82.6%→64.0%, GuardSpear 는 58.1%→62.8%로 오히려 악화. 뷰 추가로는 못 고친다. 베이크 단계의 커버리지/마스크 로직을 파고들거나 다른 텍스처링 경로가 필요.
### C++ 계약·스텁 결정 잔여
- [ ] **스텁 3종 TODO 존치 결정(2026-07-11)**: Craft 레시피 검증(레시피 데이터 설계 선행)·Drop 스폰(TemplateID→클래스 매핑 필요)·DetectEntities 확장. 필요 대두 시 개별 SPEC.
- [ ] (선택) `ItemManager.cpp:138` — 인벤토리 JSON 직렬화를 `FJsonObjectConverter` 규격화(현행 수제 문자열).

  **3. 페인트 자체가 어두움 — 참조 아이콘 문제.**
  멀티뷰(페인트 직후) 밝기부터 이미 낮은 부류: Glasses 0.162 · BrokenCompass 0.071 · AncientScroll 0.087 · Leather 0.119. 참조 아이콘이 불꽃·날개 등 이펙트가 얹힌 복잡한 일러스트인데 메시는 단순 덩어리라 페인트 모델이 매핑에 실패한다. 이펙트 없는 단순 아이콘 재제작 없이는 생성 파이프라인만으론 해결 불가.

  **4. 저가중치 뷰 색 환각.** Rock 바닥면이 파랗게 칠해짐(원본은 흰 대리석). 바닥·상단 뷰에서만 보이는 면은 페인트 모델이 근거 없이 지어냄.
### 문서 부채 (2026-07-10 발굴)
- [ ] **CLAUDE.md↔docs 앵커 정합** — CLAUDE.md 가 가리키는 `#todo`(로드맵) 앵커의 `src-todo` 블록이 index.html 에 없음(로드맵 문서 부재). 로드맵 블록 신설 or CLAUDE.md 참조 제거.

### SPEC 작성됨·구현 대기 (2026-09-03)
- [x] `SPEC_vr_ui_systems.md` — VR 몰입형 공간 UI 4종 기획. 이름표·거래 테이블은 09-05 구현(W36), 플레이어 HUD/퀘스트 로그 기획 문서화 완료.
- [x] `SPEC_python_cpp_contract.md` — 파이썬 스키마 ↔ C++ 파라미터 통신 누락/불일치 동기화 기획.
- [x] `SPEC_crewai_multi_agent.md` (2026-09-19) — CrewAI 기반 자율형 계층 다중 에이전트(Master-Dev-QA) 오케스트레이션 시스템 기획. SoL-Pi 하네스 연동, UBT 락 직렬화, 자가 치유 2회 가드레일.

### 백로그 (착수 미정, 2026-07 발굴분 — 필요 대두 시 개별 `/spec`)
- [ ] **C++ 대형 함수 분할**: `OnTacticalCandidatesDone` 182줄 · `OnTargetPerceptionUpdated` 165줄 · `OnLLMMessageReceived` 133줄→타입별 핸들러. `ExecuteInteraction` 은 switch 본질 → 유지.
- [ ] **스텁 2종 존치**: Craft 레시피 검증(레시피 데이터 설계 선행)·DetectEntities 확장. (Drop 스폰은 08-31 해소.)
- [ ] (선택) `ItemManager` 인벤토리 JSON 직렬화를 `FJsonObjectConverter` 규격화(현행 수제 문자열).
- [ ] **문서 부채**: CLAUDE.md `#todo` 앵커의 `src-todo` 블록이 index.html 에 없음 · `src-bt-guide` 는 구 BT 가이드(ST 마이그레이션 후 스테일).
- [ ] (선택) POI 명명 위치 시스템 — "EastBridge" 류 장소명 이동. 현행은 FVector target_loc 만.
- [ ] **LLM latency hiding(bark)** — LLM 왕복 동안 UE5 로컬 정형 bark·고민 애니 즉출. 애니 에셋 엮임.
- [ ] (소형) Constrained Generation — BehaviorMode·인벤토리 기반 액션 enum grammar 사전 제약. 현행은 valid_targets enum + rules 사후검증.
- [ ] VR 멀티플레이어 · Quest 3 스탠드얼론 APK(W22 보류 결정 재검토 시점 미정) — 스펙 미작성.

### 사용자 작업 (MCP 로 안 되는 것만)
→ **`docs/DoList.md`** — 에디터 작업은 ue5 MCP 로 클로드가 먼저 시도(2026-09-21 규칙 변경, 종전엔 에디터 작업 전부 사용자
이관). MCP 로 실패한 것·BP 그래프 노드·헤드셋 PIE 육안·GitHub/운영 결정만 거기에. Memo 에 중복 기재 금지.

---

## Done

- [x] **Jev 일상 활동 매칭 M1 — PIE 검증 완료 (2026-09-24)** — `docs/SPEC_jev_daily.md` 구현 기록.
  Python `b704d0a6`(evaluate_daily·domain 분기·테스트 15) · C++ `769bd13e`(CollectNearbyContext·POI 목업·daily 요청/조립/주입/선점·
  (타입+대상) 중복 필터·Pray style·D10 추적/바라보기) · `9b98ed7b`(기상 억제) · `ca316c01`(POI 액터 3개).
  완료 기준 1~15 전부 PIE(헤드셋 없이) 확인 — Sleep·Sit·Pray·Scan·GiveItem·POI 순찰·LLM 선점·서버 단절·전투 중 0건·추적·바라보기.
- [x] **BehaviorMode 6→2 축소 (2026-09-24)** — `13f94484`, `docs/SPEC_behavior_mode_reduce.md`. Simulate 반사 Combat 진입·Common 복귀,
  Stage1 5턴 Mode∈{Common,Combat}. 고아 BP enum 삭제만 DoList 1-17(권한 분류기 차단).

- [x] **§5.3 Stumble 3분기 구현 + 4방향 몽타주 — 헤드셋 검증 완료 (2026-09-23)** — `docs/SPEC_realistic_combat.md` §5.3.
  `NPCRagdollComponent` 만 수정(호출자 3곳 무변경): `EStumbleDir` enum · `StumbleMontages` TMap(생성자에서
  `/Game/Core/Animation/Hit/AM_Stumble_*` 4개 자동 바인딩 — 기상 몽타주와 같은 관례) · `StumbleThreshold 20` ·
  `ReactToHit` 3분기(Knockdown ≥40 / Stumble ≥20 / Flinch) · **가드 브레이크**(Block 중 강타는 넉다운 대신 Stumble).
  - **PIE 실측(헤드셋 없이)**: 4방향 dir 매칭 4/4 정확 · 폴백 `montage=None` 경로 · 미방어 120 raw →
    `IsKnockedDown=True` · Skadi `Block` 중 120 raw → `Stumble dir=Front`(넉다운 안 됨). `sol_pi build` 0 오류,
    pytest 83 passed. **헤드셋 PIE(사용자)**: 몽타주·밀림·복귀 정상.
  - **방향 부호 함정** — `LastHitDirection` 은 ShotDirection(가해자→대상)이라 정면 피격이 소유자 로컬 X **음수**.
    처음 반대로 썼다가 PIE 전에 수정.

- [x] **Stumble 애니 "밀려나는 그림인데 제자리" / "튕겼다 스냅백" — 루트 모션으로 해결 (2026-09-23)** —
  Mixamo hit-reaction 은 **In Place 배포본이 없어** 이동이 애니에 통째로 들어있다(실측 Hips 수평 이동:
  Back 398cm · Left 603cm · Right 455cm · Front 13cm). 세 상태를 거쳤다: ① 루트모션 OFF → 메시만 끌려갔다가
  몽타주 끝에 캡슐로 **스냅백** ② `bForceRootLock=true` → 제자리는 되는데 **밀려나는 그림인데 안 움직임**
  ③ **정답: 시퀀스 `bEnableRootMotion=true` + `bForceRootLock=false`**(`ABP_SmartNPC` 는 이미
  `RootMotionFromMontagesOnly`) → 몽타주가 캡슐을 직접 끌고 간다. 코드의 `LaunchCharacter` 는 몽타주 분기에서
  제거(이중 이동), 몽타주 없는 폴백에만 남김.
  - **밀림이 아예 안 되던 진짜 원인은 내가 넣은 이동 잠금**: `MaxWalkSpeed=0` 이 Walking 복귀 순간
    `CalcVelocity` 에서 방금 준 launch 속도를 같이 0 으로 클램프했다. 잠금 코드·헤더 멤버
    (`StumbleLockTime`·`StumbleLockTimer`·`SavedMaxWalkSpeed`·`EndStumbleLock`) 전부 제거하고 같은 커밋에 포함.

- [x] **1-16 임포트 뒤처리 — AnimSequence 가 0개였다 (2026-09-23)** — 사용자가 임포트한
  `Content/Core/Animation/Hit/` 에 **SkeletalMesh 4 + PhysicsAsset 4 + Material 2 뿐, 애니 시퀀스 0개**
  (skin 포함 FBX 가 메시로만 들어감). 원본이 기록된 `C:\Program Files\Epic Games\Art\Meshes\` 는 폴더째
  사라져 `Downloads\` 의 4개(Head Hit / Hit On Back·Left·Right Of Head)로 애니만 재임포트.
  FBX 당 take 2개가 들어오는데 `_Take_001` 은 **회전 변화 0도**(빈 트랙), `_mixamo_com` 이 진짜(45~71도) —
  빈 것 삭제 후 진짜를 `AS_Hit_*` 로 rename, 잔재 21개 삭제(외부 참조 0 확인). `AM_Stumble_*` 4개 생성,
  슬롯 `DefaultSlot`(기존 몽타주 20개 관례).

- [x] **1-14 패링·퀘스트 SFX 검증 (2026-09-23)** — 패링은 헤드셋 체감 확인(소리·데미지 무효 정상. 다만
  `S_Hit_Metal_0` 이 "챙강" 보다 "띵" 에 가까움 — 에셋 취향 문제, 교체 미정).
  헤드리스 확인분: `CheckReflex(Agility 50, Difficulty 2)` 4000회 → **24.7%**(기대 25%) ·
  `ParrySound`/`QuestUpdateSound`(`VR_confirm`) CDO 배정 · `ESenseType::Parried` 주입 →
  `[NPCManager] Event Report Sent` → 0.34s 뒤 서버 응답(`BatchCount: 0` — emergency_report 는 통보 전용이라 정상) ·
  `OnStoryUpdated` 브로드캐스트 → `QuestLogText` 즉시 갱신 + 0.28s 뒤 `LogAudio: parsed seektable`.
  **남은 건 퀘스트 갱신음 실청**(DoList 1-14).

- [x] **`.claude/skills/` 9개가 한 번도 로드된 적 없었음 — 디렉터리 형식으로 전환 (2026-09-23)** — Claude Code 는
  `.claude/skills/<name>/SKILL.md` 만 읽는데 9개 전부 평면 `.md`(2026-05-02~06-12 작성)로 있어 스킬 목록에 아예
  안 떴다. `<name>/SKILL.md` 로 이동 + frontmatter `name:` 을 디렉터리 슬러그(kebab-case)로 통일 → 재시작 없이
  즉시 로드 확인. `.claude/commands/` 는 존재하지도 않아 CLAUDE.md 가 적어둔 `/session-end`·`/spec` 슬래시 커맨드도
  작동한 적 없음. `.claude/` 는 gitignore 라 git 이력 0 — "사라진" 게 아니라 처음부터 안 잡힌 것(전 브랜치 추적 0건으로 확인).
  잔여: 프로젝트 `spec` 이 gstack `spec` 과 이름 충돌(현재 gstack 것만 노출) — 쓰려면 리네임 필요.

- [x] **세션·주 마감 스킬 정비 (2026-09-23)** — `session-end` 에 **B. DoList 이관** 단계 신설(체크된 항목 수집 →
  절 전체 체크면 절째로·일부면 줄만 → `- [X] ~~제목~~ (날짜 주체) — 검증 내용` 취소선 형식 → DoList Done 은
  기간 만료 삭제 없음). 신규 `week-end` 스킬 — Memo/DoList 의 `Done` 을 `docs/주간기록/2026-W##_*.md` 로 **이관 후
  작업판에서 삭제**(Handoff Notes 는 존치), 소스 4개(Memo Done·Handoff·DoList Done·**git log**) 중 커밋 기록이 뼈대
  (`--all` 필수 — 브랜치 분산, Done 에 없는 커밋이 그 주 서사인 경우 많음), 끝에 INDEX·`_결정원장` 갱신.
  발견: `docs/주간기록/INDEX.md` 표가 **W23 에서 멈춤**(파일은 W37 까지) · **W38·W39 미작성**(마지막 파일 W37 = 09-07~09-13).

- [x] **오픈소스 Jevlike 기반 NPC StateTree 전술 편향기 파이프라인 구현 완료 (2026-09-22)** — `docs/SPEC_jev_neuro_symbolic_st.md` §5·§6·§7.
  - **Python 인지 백엔드**: `EEnvelopeType.JEV_QUERY` / `JEV_DECISION`, `JevQueryPayload` 정의(`app/schemas/envelope.py`), `JevlikeService` 로컬 전술 편향기(`app/services/jev_service.py` — 체크포인트 부재 시 단조성 규칙 기반 휴리스틱 폴백 완비, 100회 평균 < 1ms), `_handle_jev_query` LLM 미경유 즉시 동기 회신(`app/main.py`), 신규 단위 테스트 8건 작성 및 통과(`tests/test_jevlike_service.py`).
  - **UE5 C++ 핵심 클래스**: `FEnvelopeBuilder::BuildJevQuery` 구현, `ASmartNPCAIController` Jev 결정 캐시(`FJevDecision`)·1.0s 쿨다운·in-flight 가드·세대 카운터(`JevGeneration`)·0.3s 워치독 타이머(`JevTimeoutTimer`) 및 감각 갱신(`OnTargetPerceptionUpdated`)·HP 25% 하향 교차 시 단 1회 트리거 연동, `FSTEvaluator_JevTactics` 0ms 캐시 복사 및 2.0s TTL 만료 검사, `FSTCondition_NoulGuard` 유해/탈옥 차단 사전조건, `NPCActionComponent::SelectCombatAction` Jevlike 승수 `[0.25, 4.0]` Clamp 곱셈(`MaxConsecutiveAttacks` 0점 처리 뒤, `Bravery`/`Feared` 앞), `ComputeEQSWeights` 전술 편향, `BaseMove` `ProjectPointToNavigation` NavMesh 투영(벽 끼임 방지).
  - **검증 결과**: `tools/sol_pi.py verify all` C++ 컴파일 에러 0건, Python 테스트 83건(100%) 통과, Engine UAT 클린 통과.

- [x] **오픈소스 Jevlike 기반 NPC StateTree 전술 편향기(Tactical Bias) SPEC 수립 (2026-09-22)** — `docs/SPEC_jev_neuro_symbolic_st.md`.
  TypeSafe 상용 클라우드 Jev 대신 오픈소스 `vinnylarouge/jevlike` 로컬 PyTorch 엔진 채택 확정 및 Claude 교차 코드 리뷰/토론 거쳐 설계 완료.
  - **파라미터 산출 책임 엄격 분담**: "행동 종류"가 아닌 **"파라미터 종류"** 기준 분리. 물리/공간 파라미터(좌표, 방향, 타겟, 몽타주, Spacing) = C++ 0ms 동기 유추, 전술 가중치 승수 = Jevlike 5~20ms, 서사/대사 = LLM 1~3s(행동 선행, 텍스트 후행 비동기 도착).
  - **C++ Gotchas 방어**: Jev 승수 `[0.25, 4.0]` Clamp 필수(후보 전멸 방지), `MoveToLocation` 호출 시 `bProjectDestinationToNavigation=true` 투영 필수(벽 끼임 방지).
  - **로컬 5~20ms 초고속 추론**: 클라우드 API 및 외부 인터넷 의존성 완전 제거, Python 백엔드 인프로세스 PyTorch 추론으로 VR 지연 극소화.
  - **아키텍처 전환**: Jevlike를 "State 직접 전이 선택기"에서 **"전술 가중치 편향기(Tactical Bias)"**로 재정의하여 기존 C++ `SelectCombatAction` 및 `TryReflexReact`의 결정론적 소유권과 비침습 결합.
  - **무중단 폴백(Graceful Fallback)**: 네트워크 타임아웃(0.3s) 및 저신뢰도(<0.5) 시 배율 1.0(중립) 유지하여 Jev 장애 시에도 C++ 100% 정상 작동.
  - **StateTree 5.5 연동**: `FSTEvaluator_JevTactics` 0ms 복사(2초 TTL 만료), `FSTCondition_NoulGuard`(bool TestCondition) 가드레일, Choice는 `Alert`/`Common` 내부 서브 전이에만 사용(최상위 `BehaviorMode` 소유권 침범 0).
  - **단일 채널 및 소유권**: 기존 WebSocket `MessageEnvelope`(`jev_query` / `jev_decision`) 단일 채널 및 `SmartNPCAIController` 단일 진입점(규칙 6-2) 준수. Latent Task 및 지연 은폐 몽타주 삭제.

- [x] **대화창 UI 분리 — 손 패널 → 카메라 고정 패널 (2026-09-21, 미커밋)** — `UPlayerHUDWidget` 안에 섞여 있던
  ChatInput/ChatLog/HandleChatCommitted/AppendChatLine/FocusChatInput/IsChatFocused/HandleNPCResponse 를 신규
  `UI/BP/ChatWidget.h/.cpp`(`UChatWidget`) 로 통째 이관, HUD 위젯은 채팅 잔재 0. `AVRPawn` 에 `ChatWidgetComp`
  (`UWidgetComponent`, **`VRCamera` 부착**, `ChatPanelOffset=(80,0,-15)`·`DrawSize 500×260`·`Scale 0.08`, 가시면 +X
  관례라 Yaw 180, 콜리전 없음) + `ChatWidgetClass`/`ChatWidget` 신설. Enter(`OnChatKey`) = `SetVisibility(true)`
  +`FocusChatInput()`, 매 틱 `UpdateChatPanelVisibility()` 가 포커스 잃으면(전송 후 `SetInputMode(GameOnly)`·빈 Enter)
  숨김. `UpdateHUDPanelGaze` 의 `IsChatFocused()` 특례 제거. **AddToViewport 는 여전히 금지**(VR 스테레오, `VRPawn.h:124`
  주석) — 카메라 부착 world-space 가 "화면 고정" 의 정답. head-lock 멀미 금기는 상시 패널 얘기라 채팅(잠깐 켜짐)은 예외로
  명시(생성자 주석). `sol_pi.py verify all` = 빌드 0 오류·pytest 75·UAT clean. 코드는 `05d610b0`.
  - [x] **WBP_Chat + BP_VRPawn 배선 + PIE 검증 — 전부 MCP (`.mcp.json` 복구 후 재연결)**. 함정 2개: ① `WidgetBlueprintFactory`
    로 새로 만들면 트리가 **루트 없이** 생성되고 `WidgetTree.root_widget` 이 Python 미노출이라 못 꽂음 → **기존 WBP 복제
    (`EAL.duplicate_asset`) → `BEL.reparent_blueprint(wbp, unreal.ChatWidget)` → 루트 `clear_children()` → 재구성** 이 유일한 길.
    ② 복제 원본(WBP_InventorySlot)의 위젯 변수(`ItemName`)가 그래프 노드에 남아 컴파일 에러 → `BEL.remove_graph(find_event_graph)`
    로 이벤트그래프 통째 제거 후 `remove_unused_variables` 로 정리(채팅은 그래프 0노드가 목표라 정답). 트리:
    SizeBox(루트, 오버라이드 전부 clear) → Border `ChatFrame`(검정 α.65) → VerticalBox → ScrollBox **`ChatLog`**(Fill) +
    EditableTextBox **`ChatInput`**. 인스턴스 위젯은 `find_object(cw,"WidgetTree")` 로는 안 잡힘 — `ObjectIterator(ScrollBox)`
    에서 outer 체인으로 소유자 판정. PIE(헤드셋 없이): `WBP_Chat_C` 인스턴스 생성·`VRCamera` 부착·초기 숨김·`focus_chat_input`
    → `is_chat_focused` True·`append_chat_line` 2줄 → ChatLog children 2·스크린샷에 텍스트 정방향(Yaw 180 관례 확인)·
    `WidgetLibrary.set_input_mode_game_only` → 다음 틱 숨김. **HighResShot 은 `SetVisibility(true)` 직후 프레임엔 렌더타깃이
    비어 안 찍힘 — 몇 틱 뒤 촬영.** 첫 PIE 는 128s 걸려 MCP HTTP 가 타임아웃(`socket_send_failure`) — 실행은 되니 로그로 확인.
    `WBP_PlayerHUD` 의 옛 채팅 서브트리(`BottomRow` → `ChatBox`(VerticalBox) → `ChatInput`+`ChatLog`)도 MCP 로
    `remove_child` 해 제거·컴파일 클린(`ScrollBox_0` 은 인벤토리 `SlotGrid` 컨테이너라 무관). 헤드셋 육안만 DoList 1-15.
- [x] **엔진 빌드 봉쇄 해소 + `.uproject` VisualStudioTools 비활성화 (2026-09-21)** — 세션 시작부터 `Expecting to find a
  type ... 'VisualStudioTools' in 'UE5Rules'` 로 빌드 0초 실패(develop 순정도 동일 — 코드 무관). 근본 원인은 Handoff U-2.
  헛다리 2건(둘 다 Verify 로 되돌림): ① `Engine/Intermediate/Build/BuildRules/UE5Rules.dll` 삭제 → Installed Build 는
  `IsFileInstalled()` 가 Engine/ 하위 재컴파일을 무조건 스킵해 재생성 불가, ② `InstalledBuild.txt` 마커 임시 rename →
  UBT 가 소스빌드로 오판해 엔진 846 파일 재컴파일 시도, 서드파티 헤더(Oodle·msdfgen·AHEasing) 없어 전부 C1083.
  **정답은 `.uproject` `VisualStudioTools.Enabled=false` 한 줄**(사용자 제안이 맞았음). VS 워크로드 컴포넌트도 사용자가
  Installer 에서 제거. 부수 삭제: `Plugins/VisualStudioTools/`(gitignore, Binaries 만 있고 Source/.uplugin 없던 VS 확장
  잔재 — 승인 후 삭제, 원인 아니었음).
- [x] **`git checkout main` 으로 ignore 파일 6개 파괴 → 복구 (2026-09-21)** — 17:33 다른 클로드 세션(`465db38d`)의
  `checkout main` 이 원인(Handoff U-1). 복구: `docs/Memo.md` ← 클로드 file-history `ea8d3ca82d16353c@v4`(17:09:47 최종본,
  57,022B, 사용자가 되살린 55,310B 본은 그보다 옛 저장본이라 상위 호환 — `%TEMP%\memo.md.user-restored.bak` 백업),
  `.mcp.json` ← file-history `03b1bf8e343f86e8@v2`(ue5·comfyui·code-review-graph 3서버, **세션 중 MCP 40개 동시 실종의
  진짜 원인이 이 파일 삭제**), `docs/ROADMAP.md`·`docs/주간기록/INDEX.md`·`주간기록/2026-W23_*.md`·`.agents/rules/debug.md`
  ← `git show main:` (클로드 편집 이력 없어 스냅샷 없음, main 커밋본이 유일). DoList.md 는 main 미추적이라 무사.

- [x] **전투 SPEC 잔여 + 로드맵 Phase4-5 완료 (2026-09-21)** — `docs/SPEC_realistic_combat.md` §3.2(NPC RNG
  패링)+§2.3(패링 리포트), `docs/SPEC_story_progression_roadmap.md` Phase4(퀘스트 SFX·햅틱)+Phase5(서브퀘스트
  E2E). 남은 §3.1/3.3/3.4/3.5 는 위 Todo 로 명시 분리(애니메이션 에셋·EQS·중앙매니저 필요 — 별도 세션).
  - [x] **플레이어 Block/Parry 는 이미 완료 상태였음 발견** — `dfb16ab2`(2026-09-18 "플레이어 방어력 적용
    및 무기/방패 Block/Parry 구현", `VRPawn.cpp:1612-1658`)로 Memo `[갭 5]` 미기록인 채 구현 완료돼 있었음.
    문서만 동기화, 코드 변경 없음.
  - [x] **NPC RNG 패링(신규)** — `AVRPawn::TryMeleeHits` 훅: `ASmartNPC` 타겟만(필드 몹 제외) `UDiceSystem::
    CheckReflex(Agility, ParryDifficulty=2)` 굴려 성공 시 데미지 무효+`ParrySound`(3D)+`RequestEventCognition`
    (`ESenseType::Parried` 신규, `PerceptionIdFor`·`FPerceptionData` 기존 피격 패턴 재사용) → emergency_report
    로 LLM 이 패링 인지. `ParrySound` 는 `BP_SmartNPC` CDO 에 기존 `S_Hit_Metal_0`(Memo 에 "미배정"으로 남아있던
    바로 그 에셋) 배정.
  - [x] **퀘스트 SFX** — `UPlayerHUDWidget::HandleStoryUpdated` 를 `RefreshQuestLogText`(텍스트만, `NativeConstruct`
    초기 리플레이 전용)와 분리해 소리가 레벨 로드마다 오작동하는 것 방지. `QuestUpdateSound` 는 `/Engine/
    VREditor/Sounds/VR_confirm` 배정 완료. **햅틱(`QuestUpdateHaptic`)은 같은 날 저녁 폐기** — 컨트롤러→손 트래킹
    전환 예정이라 진동 하드웨어 자체가 없어짐(Handoff B "햅틱 전면 폐기"). 부수 기록: `UCurveFloat.float_curve` 를 python 으로
    건드리면 RemoteControl 서버가 멎어 에디터 강제종료됨(재현 위험) — 커브 에셋 자동생성은 하지 말 것.
  - [x] **서브퀘스트 5종 E2E** — `s_moca_herbs`(HerbBasket `TryPickupInto`)·`s_hunt_forest_raiders`(Bandit
    3마리 `apply_damage`)·`s_hunt_bridge_troll`(Orc_Vagron)·`s_hunt_imp`(Imp 처치+ImpHorn `TryPickupInto`)·
    `s_hunt_dead_wraith`(Wraith 5마리 — 스포너 3마리 한도라 3킬 후 리스폰 대기 120s 필요) 전부 `story_state.json.side`
    `done` 확인. `AActor::TryPickupInto`(BlueprintCallable) 가 줍기 검증의 핵심 진입점 — `InventoryComponent::
    AddItem` 직접 호출보다 훨씬 적은 코드로 실제 게임 경로(마스터데이터 조회 포함) 그대로 탐.
  - [x] pytest 74 passed 유지, `sol_pi.py build` 0 오류.

- [x] **스토리 디렉터 완료 — `docs/SPEC_story_director.md` (Phase A·B·C 구현 2026-09-18, PIE 검증 2026-09-21, `feature/story-director`)** — b1→end 7비트 전체 전이를 MCP 경유로 실제 게임 경로 그대로 재현해 확정: 전투는 `GameplayStatics.apply_damage`(TakeDamage→HandleDeath→`npc_died` story_event, `NPCStateComponent::ApplyDamage` 직접 호출은 HandleDeath 를 안 태워 무효였음 — 반드시 엔진 데미지 경로 경유), 대화는 `/api/debug/say`(UE5 `SendPlayerDialogue` 실제 경로, `/api/debug/prompt` 는 인벤토리·plan 없어 실제와 다른 입력이라 미사용). Commander_Vorg·DemonLord 처치 확인, 시야 즉시 교전(danger=0.60) PIE 체감 확인(DoList 1-12 전항목 [X]). pytest 74 passed(story+ws roundtrip+contract_sync 포함, llm 마크 1개만 deselect).
  - [x] Phase B C++ (2026-09-18) — `Story/StorySubsystem`, `NPCManager::OnLLMMessageReceived` → `ApplyStoryJson` → `OnStoryUpdated(FStoryState)`.
  - [x] Phase C — `Story.events[]` → `UStorySubsystem::ExecuteEvent` 화이트리스트 `spawn_enemy`.
  - [x] side yaml 3종 (2026-09-20) — `s_hunt_forest_raiders`/`s_hunt_bridge_troll`/`s_hunt_dead_wraith` + `main.yaml` b2 `unlocks_side` 5종.
  - [x] 퀘스트 giver 주민 + "!" 마커 (2026-09-20) — `AVillagerCharacter.QuestOffers`+`QuestMarkerText`, `SendStoryEvent("quest_accept", sid)` → available→active 멱등 승격.
  - [x] seed.py 적 호감도 (2026-09-20) — `ENEMIES ↔ ALLIES` 양방향 -100 시딩, 보스와 동일 적대 경로(`AffinityHostileThreshold=-30`).
  - [x] 로드맵 스펙↔구현 이름 정렬·Elara 초기 위치 은신처·디렉터 타임아웃 5s·플레이어 방어력 0 — 결정 완료(DoList 3 이관분 포함).
  - [x] UE5 `story_event` 송신·perception/victory id AgentID 통일·플레이어 id "Player" 상수화 (2026-09-18).
완료 항목은 날짜와 함께 여기 적고, 주가 바뀌거나 쌓이면 `docs/주간기록/2026-W##_주제.md` 로 옮기고 여기서 **삭제**한다. 비어 있는 것이 정상.
주간기록·Memo·DoList 는 2026-09-21 부터 git 추적(`docs/` ignore 해제 — 그날 checkout 사고로 Memo 가 날아간 뒤 결정). 세션 간 유일한 서사 기록 — 커밋 해시·수치·함정을 반드시 같이 남길 것. `docs/.obsidian/`·`*.canvas`·`*.txt` 는 여전히 ignore. 주차 목록은 폴더 `ls`, 결정 이력은 `주간기록/_결정원장.md`.

---

## Handoff Notes
    - **Tier 2 (온디맨드 규칙)**: `.agents/rules/ue5_cpp.md`(C++ 4곳 수정), `python_backend.md`(Envelope 동기화), `sol_pi.md`(빌드 경계 및 루프 가드), `mesh_doctor.md`(3D 경계).
    - **Tier 3 (심층 스펙)**: `docs/SPEC_*.md` 등은 사전 주입을 완전히 배제하고 Planning 단계에서만 선택 열람.

코드·주석만 봐선 모를 배경과 함정만. 서사(문제→조치 경위)는 주간기록에, 결정 이력은 `_결정원장.md` 에.
### 음성 파이프라인 폐기 (2026-09-12, 리팩토링 1단계 C10·C11)
- [x] TTS·ASR 후속 3건(ASR partial 스트리밍·TTS M4 Lip Sync·VAD barge-in) — 파이프라인 자체 삭제로 폐기. 복원은 `bd057b8` 이전 이력.
### J. 오픈소스 Jevlike StateTree 전술 편향기 연동 (2026-09-22 신설)
- **오픈소스 `vinnylarouge/jevlike` 로컬 인프로세스 추론 채택**: TypeSafe 상용 클라우드 Jev를 배제하고, Python 백엔드(`OmniAgent_VR_System/CognitiveEngine`) 내에 `jevlike`를 직접 탑재. 클라우드 RTT(150~300ms) 및 외부 API 키 의존성을 완전히 제거하고 **로컬 5~20ms 초고속 추론**으로 VR 90Hz 프레임 예산을 완벽히 보호.
- **Jevlike의 역할은 State 선택기가 아닌 "전술 가중치 편향기(Tactical Bias)"**: C++ `SelectCombatAction` 및 `TryReflexReact`의 결정론적 소유권을 보존하기 위해, Jevlike는 StateTree 최상위 모드(`ENPCBehaviorMode`)를 바꾸지 않고 C++ 셀렉터의 액션 가중치(공격/회피/도주) 및 EQS 파라미터에 배율(Multiplier)로만 개입한다.
- **무중단(Graceful Degradation) 1.0 중립 폴백**: 타임아웃(0.3s) 만료, 세대 불일치, Confidence < 0.5 발생 시 중립 배율 1.0을 유지하므로, Jevlike 지연/예외 시에도 C++ NPC 로직은 0ms로 100% 정상 작동한다.
- **단일 채널 규약(CLAUDE.md §1) 및 단일 진입점(규칙 6-2)**: UE5 클라이언트 소스 변경 없이 기존 로컬 WebSocket `MessageEnvelope`(`jev_query`/`jev_decision`) 채널을 재사용하며, C++ 진입점은 `SmartNPCAIController`로 일원화(세대 카운터, 0.3s 워치독 타이머, 2.0s TTL 캐시).
- **StateTree 5.5 연동 규칙**: Latent Task 및 지연 은폐 몽타주는 일체 사용하지 않음(5~20ms 추론이라 은폐 자체가 불필요). `FSTEvaluator_JevTactics`는 Controller 결정을 0ms 복사만 수행하며, `FSTCondition_NoulGuard`(bool TestCondition)로 유해 행동 진입을 2차 방어한다.
- **파라미터 산출 분담 ("파라미터 종류" 기준)**: 물리/공간 파라미터(좌표, 방향, 몽타주, Spacing) = C++ 100% 동기 유추(0ms). 전술 가중치 승수 = Jevlike(5~20ms). 대사/서사 = LLM(1~3s, 단 행동 선행 집행 후 텍스트 비동기 후속 도착 필수).
- **C++ Gotchas 방어**: Jev 승수 연산 시 `[0.25, 4.0]` Clamp 필수(후보 전멸 방지), `MoveToLocation` 호출 시 `bProjectDestinationToNavigation=true` 투영 필수(BaseMove:585 기본값 false 로 인한 벽 끼임 버그 방지).
- **상세 명세서**: `docs/SPEC_jev_neuro_symbolic_st.md`.
- **daily(2026-09-24)**: 비전투 반사는 Jev 활동을 끊지 않는다 — 중립 주민 경계 Scan(쿨다운 10s)이 앉기·산책을 시작 20ms 만에 잘랐다. 전투 진입 반사·LLM 배치·전투 셀렉터만 선점. 대화 직후 8s 는 비전투 반사 억제(말한 상대 보기 유지).
- **SetFocalPoint 는 원래 몸을 안 돌렸다**: SmartNPC 는 `bOrientRotationToMovement` 전용이라 TurnTo·Scan 이 시선만 바꿨다. 지금은 `BaseFaceRotate` 가 컨트롤러 목표 회전 추종으로, `PrepareMove`·Track 이 진행 방향 회전으로 전환한다. 새 이동 경로를 만들면 이 전환을 거칠 것.
- **Jev 검증은 에디터 포그라운드 조건**: 백그라운드 3fps 면 응답이 0.3s 워치독을 넘어 전부 폐기된다. MCP 로 `/Script/UnrealEd.Default__EditorPerformanceSettings.bThrottleCPUWhenNotForeground=false` 후 검증(메모리만, 재시작 시 원복). 서버 재시작 시 UE 는 재연결 5회 후 Offline 영구 → PIE 재시작.

---

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

### T. VR 물리 그립·NPC 상호작용 설계 논의 (2026-09-21, 코드 미구현 — 구두 스파이크 전 단계)
- **결론**: 논의한 항목 전부 기술적으로 가능(NPC 던지기·수플렉스급 연출 포함). 단 엔진 내장 원클릭 기능은 하나도 없음 — 전부 기존 Chaos physics/Control Rig 부품 조합으로 직접 구현. 참고 사례: Quinn Kuslich YouTube "Advanced VR Hand Physics for in Unreal Engine 5"(유료 Gumroad 애셋, 물리 손+breakable constraint+per-finger collision 구조가 아래 설계와 동일 — 전제조건으로 "VR Procedural Grip Poses" 튜토리얼 필요).
- **손가락 wrap 그립(플레이어)**: Control Rig 내장 솔버 없음, 직접 구현 필요. 실시간 절차적 wrap 아니고 "그립 지점별 고정 포즈 저작+블렌드" 방식이 실전적(Meta Interaction SDK HandGrabPose 도 결국 이 방식).
- **핸드트래킹 전환**: `OpenXRHandTracking` 플러그인 이미 켜져 있음. 단 **Quest 는 VIVE 가 쓰는 `XR_EXT_hand_interaction`(Pinch/Grasp Value 자동 제공) 미지원** — Meta 는 FB 프리픽스 확장(`XR_FB_hand_tracking_aim` 등)과 Interaction SDK(= OculusXR/Meta XR 플러그인, 현재 프로젝트에서 `Enabled: false`)로만 제공. 순정 OpenXR 유지 시 핀치 제스처 판정(손가락 거리 계산) 직접 구현 필요, 지름길 없음.
- **물체 밀기 저항/못 뚫음**: "ghost hand" 기법 — 실제 트래킹 위치를 쫓아가는 물리 시뮬 손(구체/캡슐)을 `PhysicsConstraintComponent` Linear Drive(Max Force 제한)로 구동. 무거운 물체는 Chaos 충돌이 못 밀어서 손도 표면에 막힘 — raw 트래킹은 계속 가는데 보이는 손만 멈추는 시각 트릭(햅틱 대체).
- **큰 물체/조각상 양손 그립 안정성**: `PhysicsConstraint` breakable + 무게중심 토크는 엔진이 자동 계산(형상 무관) — 모서리로 잡으면 토크 커져서 자동으로 놓침, 양손이 무게중심 양쪽 브라케팅하면 안정. 복잡 형상은 Convex Decomposition 필요(애셋당 Hull 10~20개 예산, 안 그러면 동적 상태일 때 물리비용 튐).
- **NPC 물건 쥐기**: 핸드트래킹 불필요(NPC 는 하드웨어 없음) — 기존 FBIK/Control Rig 로 충분, 단 **아이템 카탈로그가 유한할 때만**(정해진 무기·도구). NPC 가 임의 복잡 형상(조각상 등)을 쥐어야 하면 플레이어와 같은 절차적 문제로 재귀함.
- **NPC 가구에 걸려 넘어짐**: NavMesh 가 가구를 Nav Modifier 로 인식해 자동 회피 베이크하므로 **기본 설계로는 NPC 가 가구에 물리적으로 안 닿음**. 걸려 넘어지게 하려면 (a) 해당 가구만 Nav 영역에서 제외해 경로가 관통하게 하거나 (b) 내러티브 연출 순간에 MoveTo 대신 강제 직선 이동 트리거 — 후자가 훨씬 쌈.
- **NPC가 플레이어 잡기**: 시각 그립은 쌈(플레이어가 고정된 단일 리그라 그립 지점별 고정 포즈 몇 개 저작으로 끝, `GetHandLocation()` API 재사용). 물리적 구속은 `AVRPawn::SyncCapsuleToHMD`(매틱 HMD 위치로 캡슐 강제 동기화) 때문에 애니메이션만으론 안 먹힘 — 가구 착석(`SeatedFurniture`) 패턴처럼 별도 이동잠금 상태 필요.
- **NPC 던지기(수플렉스급 연출)**: 순수 관절 토크 계산으론 안 나옴. 그랩 순간 NPC 전신 래그돌 전환(`NPCRagdollComponent::EnterRagdoll` 재사용) + 그립 지점 강한 PD constraint(puppet 방식, breakable 아니고 뻣뻣하게) + 릴리즈 시 손 속도 임펄스(`InventoryComponent`/`VRPawn::ThrowVelocityScale` 이미 있는 패턴 재사용) — Blade & Sorcery 류가 실제로 쓰는 방식, 실사용자 팔 리치가 궤적 품질을 좌우(엔진 문제 아니고 물리적 제약).
- **NPC 안고 들기**: "겨드랑이 끼워 들기"(다리는 래그돌로 바닥에 끌림, 상체만 지지)가 princess-carry 보다 훨씬 쌈 — 기존 넉다운/기상 흐름(`Knockdown()`)과 자연스럽게 이어짐(의식 없는 NPC 끌고 가는 그림). 훅 판정은 겨드랑이 트리거 존 + 진입 각도(노멀 내적) 체크.
- **겨드랑이 "낀" 반응(공간이 생기며 파고듦)**: `Flinch()` 가 이미 쓰는 부분 물리 블렌드 패턴(`SetAllBodiesBelowSimulatePhysics(팔본, true)`) 재사용 — 평소 NPC 본은 QueryOnly(이미 기본값, 항상 분리된 콜리전)만 있다가, 플레이어 손이 근접할 때만 그 팔 하나만 SimulatePhysics 토글. PD 복귀력이 침입 콜라이더에 막혀 자연스럽게 "그 위에 걸침" 이 나옴 — 스크립트로 안 만들고 물리 결과로 나옴. 상시 켜두면 NPC 수만큼 물리비용 누적이라 반드시 근접 게이팅.
- **attach 없이 순수 접촉으로 들어올림(가장 리얼함, 가장 리스크 큼)**: 양팔 다 훅(무게중심 브라케팅 안 하면 회전하며 미끄러짐) + 그 순간 NPC 전신을 동적 물리 바디로 전환해야 함(팔만 블렌드로는 몸통이 안 들림). 안정성은 계산으로 확답 불가 — **기술 스파이크(먼저 프로토타입으로 되는지 확인) 필요**, "연구"라 부르기엔 새 지식 생산이 아니라 기존 부품(PD 블렌드+접촉 해석+2점 브라케팅) 재조합이라 정확한 표현 아님.
- **NPC 옷(Cloth)**: Chaos Cloth 엔진 내장(플러그인 아님). 밀기는 공짜(물리 손과 자동 충돌). 쥐고 당기기는 파티클 핀 방식 별도 구현 필요(Cloth 자체엔 런타임 핀 기능 없음). **진짜 성능 병목은 여기** — Quest 3S 90Hz 프레임 예산 ~11ms 에서 NPC 동시 Cloth 시뮬 개수 제한(LOD 로 근접만) 필수, 안 그러면 바로 병목.
- **총 작업 규모**: 물리 손(ghost hand), breakable constraint, Convex Decomposition, Cloth 파티클 핀, NavMesh 가구 예외 처리, 부분 물리 블렌드(팔 훅), 래그돌 puppet+release 임펄스 — 최소 5~6개 서브시스템 신설. 스프린트 하나 분량 아님, 우선순위 정해서 `/spec` 쪼개야 함.

### U. 로컬 환경 지뢰 — git·엔진·MCP (2026-09-21 실사고 3건에서 추출)
- **U-1. 브랜치 체크아웃이 ignore 파일을 지운다 — "A 브랜치는 추적·B 브랜치는 ignore" 인 경로가 있으면.** git 은 ignore
  파일을 소모품으로 봐서 A 체크아웃 시 **로컬본을 A 의 커밋본으로 덮어쓰고**, B 로 돌아올 때 **삭제**한다. 2026-09-21
  17:33→18:03 실사고: stale 로컬 `main`(99e1b6cd) 이 `docs/Memo.md`·`ROADMAP.md`·`주간기록/INDEX.md`·`W23`·
  `.agents/rules/debug.md`·`.mcp.json` 6개를 추적 중이었고 Develop 은 전부 ignore → Memo 57KB→18KB→소멸. **해결됨
  (같은 날)**: ① 로컬 main 을 `origin/main`(f3efae76, 6개 미추적) 으로 FF — `git fetch origin main:main`, ② `docs/` ignore
  해제·git 추적 시작(21:2x 커밋). 이제 남은 위험은 `.mcp.json`·`.agents/rules/debug.md`(여전히 ignore, 어느 브랜치도 미추적)
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

## Compounding Engineering

클로드가 틀릴 때마다 "CLAUDE.md를 업데이트해"라고 지시하십시오. 추가 내용: 틀린 패턴 + 왜 틀렸는지 + 올바른 대안. 매 교정이 미래 모든 세션을 개선합니다.

### A. LLM · Ollama
---

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
## Critical Reception

### B. NPC 전투 · AI
요청을 수행하기 전 **비판적으로 검토**한다 — 동의부터 하고 시작하지 않는다. 대상은 아키텍처 규칙(§1–§9) 위반뿐 아니라:
- **기술적으로 불가능/비현실적인 요청** (예: VRAM 16GB에 31B 모델 QLoRA 학습 — 가중치만 15.6GB로 계산상 확정 불가)
- **더 나은 대안이 명백한 요청** (목표는 맞으나 수단이 최선이 아님)
- **근거 없이 반복되는 요청** (같은 방향을 재확인 없이 또 요구 — 이전에 이미 반론했다면 새 근거 없인 재반론)

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
확인 가능한 사실(용량·존재 여부·버전 호환 등)은 **먼저 검증하고** 반론에 숫자·근거를 싣는다 — "안 될 것 같다"가 아니라 "15.6GB > 13.2GB 가용, 불가"처럼 확정적으로.
반론 형식: `[반론] <문제(근거)> → <대안>. 진행할까요?`
예외: 단순 버그·오타, 사용자가 이미 근거를 들어 재확인한 사항.

### C. 인벤토리 · VR 조작
---

- **수량 기준 두 갈래**: `HasItem`/`GetItemCount` 는 장착분 **포함**, `RemoveItem`/`GetSlotIndexByItemID` 는 슬롯만(의도 — 장착만 하면 보유 false 가 되어 거래·퀘스트가 어긋남). 슬롯에서 실제로 빼는 동작은 `GetItemCountInSlots` 로 판정. 어기면 복제·증발(2026-09-01 3곳 수정).
- **`AddItem` 은 전부-아니면-전무** — 부분 적재 후 `false` 는 호출측이 원본을 남겨 재시도마다 복제됐다. 스택 상한은 `GetEffectiveMaxStack`(= min(아이템 MaxStack, `MaxStackLimit`=10)) 경유, 마스터 테이블 직접 읽기 금지.
- **월드 아이템 = `ADroppedItemBase` 하나** — `ItemManager` 등록 풀을 NPC 탐지가 본다. 별도 픽업 클래스를 만들면 NPC 가 인지 못 하는 두 번째 계통이 생긴다. Deferred 스폰으로 `FinishSpawning` 전에 `ItemTemplateID` 를 넣을 것(늦으면 `BeginPlay` 가 `DefaultEntity_Unknown` 으로 등록).
- **HandObject 는 소유권 이전 없음** — 제시(손에 들기) 연출 전용, 실제 이전은 GiveItem. 플레이어 인벤에 넣으면 복제.
- **손 쥐기 규칙(최종, 사용자 지정 2026-09-07)**: 손에 쥐는 모든 것은 물리 액터. 그립 홀드(`Started` 쥠 / `Completed`·`Canceled` 놓음). 뗄 때 **인벤토리 열림 = 회수 / 닫힘 = 거래접시→NPC 건네기→던지기**. 시간 가드 없음. 장착 무기는 `AttachedMeshes` 라 `HeldItems` 가 아님 — 그립을 누르면 `OnGrabStart` 가 물리 쥐기로 전환해야 던지기 경로를 탄다. 이 규칙 밖의 조작 기능 임의 추가 금지(당일 두 번 뒤집힌 원인).
- **BP 오버라이드가 C++ 기본값을 이긴다** — 상시 켜야 하는 것은 `BeginPlay` 에서 강제하거나 에셋 재직렬화 확인(`HUDWidgetComp` 가시성 사례). `BP_VRPawn::CameraHeightOffset` = **0 필수**(-30 이면 HMD 높이 역산 오염 → 캡슐 30cm 단축). 머리 본 위치는 `HeadEffectorOffset`/FBIK 로, 카메라 오프셋 금지.
- **VR 아바타 1:1 고정** — 키 비율 스케일 삭제(FBIK 하에서 '서면 머리 낮음' 만 유발). `CalibratedStandingHeight` 는 자세판정용만. 몸통은 HMD Yaw 1:1 추종(`BodyMeshYawOffset=-90`), 착석 시 의자 방향 고정.
- **PostProcess 는 이 렌더 경로(`r.ForwardShading`+`vr.InstancedStereo`+`vr.MobileMultiView`)에서 화면 전체 검정** — 비네트·터널 효과 불가. 시각 피드백은 HUD.
- Sprint 해제는 `IA_Move` `Completed`/`Canceled`(`OnMoveReleased`) — `Triggered` 는 입력 0 에서 안 오고 `OnMove` 가 조기 return.
## Documentation

### D. 통신 · DX
상세 문서: **`docs/index.html`** (브라우저로 열기). 주요 앵커: `#ai-codebase-guide`(아키텍처) · `#tts-plan`(TTS 계획) · `#langgraph`(LangGraph) · `#integration-guide`(통합 시퀀스). 로드맵은 `docs/Memo.md` Todo 섹션·`docs/DoList.md` 가 담당(index.html 에 별도 `#todo` 없음, 2026-07-27 중복 방지로 참조 제거).
세션 메모: `docs/Memo.md` · 주간기록: `docs/주간기록/`

- **WP 외부 액터는 `modify(True)` 없이 옮기면 저장에서 빠진다(2026-09-18 실측)**: `set_actor_location`·`set_mobility`·`set_box_extent` 는 `Modify()` 를 안 불러 패키지가 dirty 되지 않고 `save_current_level`/`save_dirty_packages` 모두 건너뛴다. 에디터 메모리엔 반영돼 보여 "저장됐다" 로 착각. NPC 7·PlayerStart·Bed 가 두 커밋에 걸쳐 유실됐던 원인. 판정은 `get_dirty_map_packages()` 개수 = 건드린 액터 수, 디스크는 `git status --short | grep -c ExternalActors`.
- **무료 에셋 소스(2026-09-18)**: Quaternius Google Drive 는 익명 접근 몇 번이면 `uc?id=` 경로가 "many accesses" 로 막힘 → `fetch_assets.py` 는 gdown 으로 목록만, 본체는 `drive.usercontent.google.com/download?id=…&confirm=t`(같은 시점에 200). itch.io 배포분(Fantasy Props MegaKit·Universal Animation Library·Bestiary)은 세션 서명 URL 이라 curl/헤드리스 실패 — 수동. Kenney 는 직접 zip. Fab/Quixel 은 계정 로그인이라 자동화 불가. `mesh_doctor` 는 glb/obj 만(FBX 미지원). `RawAssets/` 는 gitignore.
**동기화 의무**: 파일 삭제·이동·프로토콜 변경(Envelope 타입, 응답 방식 등)·엔진 교체(TTS 모델 등)처럼 `docs/index.html` 이 서술하는 사실이 깨지는 수정을 하면 **같은 세션에서 해당 앵커 섹션도 같이 고칠 것** — 나중으로 미루지 말 것. 방치 사례(2026-07-27 발견): `schemas/intent.py` 는 2026-07-09 삭제됐는데 `#ai-codebase-guide` 파일 목록엔 3주 넘게 남아있었음, TTS 도 CosyVoice2→OpenVoice v2+MeloTTS 전환(2026-06-23)이 반영 안 됨. 코드와 문서가 따로 놀면 다음 세션이 잘못된 사실을 근거로 판단하게 됨.

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
- **gitignore**: `docs/`·`tests/`·`knowledge/`·`personas/generic/*.yaml` 로컬 전용(`knowledge_template/` 만 추적). NPC 4인: Skadi(과격 여성 해적선장)·Moca(ASMR 여성 스트리머)·Elara(근엄 남성 기사단장)·James(Skadi 해적단 항법사).
---

### E. 파인튜닝
## Git Commit Guidelines

→ `OmniAgent_VR_System/CognitiveEngine/finetune/RESULT.md` "운영 주의점" 절. Stage2 12B 는 unsloth 미지원으로 보류(서빙은 qwen3:8b 로 대체) · VRAM 스필오버는 OOM 아닌 감속 · GGUF 변환 베이스 태그 인자 · 시드 데이터 페르소나 규칙.
**브랜치**: `main`(릴리스 전용, 직접 커밋·force-push 금지) · `Develop`(통합) · `feature/*` · `bugfix/*` · `refactor/*`.

**메시지**: `<type>: <제목 50자↓> — <변경 시스템·파일>`. type: `feat` `fix` `refactor` `chore` `perf` `test`.

**단위**: 한 커밋 = 한 논리 변경. `.uasset` 은 관련 C++ 와 같은 커밋. WIP 커밋 금지.

**타이밍**: 기능 마일스톤·버그 패치 검증 완료 직후 커밋. `git push` 는 명시적 지시 없이 금지.

**PR**: `feature/*` → `Develop`. 본문: 변경 이유·테스트 방법·체크리스트. `main` 직접 머지 금지.

---

## Build & Run

- **UE5 C++ 빌드**: 클로드가 직접 실행 (UE 5.5 로컬 설치 확인, 2026-07-12). C++ 수정 후 커밋 전 필수 — PowerShell 로:
  ```
  & "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" UE5_MCP_VREditor Win64 Development -Project="C:\github\UE5_MCP_VR\UE5_MCP_VR.uproject" -WaitMutex
  ```
  증분 빌드 ~15초. 에디터가 열려 있어도 핫리로드 대상 DLL 링크는 됨(`-WaitMutex` 가 UBT 뮤텍스 대기). 빌드 에러는 클로드가 직접 수정 후 재빌드.
- **에디터 작업·PIE 검증**: 에디터 조작(에셋·BP 생성, 프로퍼티, 레벨, 헤드셋 없는 PIE)은 MCP 로 클로드가 먼저. 헤드셋 육안 확인(몽타주·이동·VR 체감)만 사용자 몫, 클로드는 체크리스트 제시.
- **Python·기타(RAG 인덱스 빌드, 테스트, 스크립트 등)**: 클로드가 직접 실행·검증까지 완료. 사용자에게 미루지 말 것. 예: `python -m app.utils.build_knowledge --all` 후 retrieve 결과까지 확인.

---

## Subagent Model

`Agent` 도구 호출 시 **반드시 `model: "sonnet"` 명시**. 미지정 시 부모 모델(Fable/Opus) 상속 — 비용 낭비.
예외: 사용자가 특정 모델을 명시적으로 지시한 경우.

---

## Code Review Graph

모든 그래프 작업 전 `get_minimal_context(task="<작업 설명>")` 먼저. `detail_level="minimal"` 기본, 부족 시 `"standard"`. 목표 ≤5 tool calls · ≤800 tokens. pre-commit hook 이 커밋 시 자동 증분 업데이트.

---

## gstack

웹 브라우징: `/browse` 스킬만 사용. `mcp__claude-in-chrome__*` 도구 절대 사용 금지.

