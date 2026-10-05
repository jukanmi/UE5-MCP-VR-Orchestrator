# Session Memo

세션 간 인수인계 단일 소스.
형식: `## Todo` (미완) · `## Done` (날짜 필수, 주간기록 이관 전 임시 적재). 함정·제약은 `.agents/rules/pitfalls.md`, 미구현 설계 논의는 `docs/DESIGN_*.md`.
**SPEC 진행 현황 한눈에: [`docs/SPEC_INDEX.md`](SPEC_INDEX.md)** — SPEC 마다 열지 말고 여기서 먼저 본다. SPEC 마일스톤이 바뀌면 그 표도 같이 고칠 것.

---

## Todo

### VR 손 — `SPEC_vr_ghost_hand.md` · `SPEC_vr_grip_pose.md` 진행 상황 (브랜치 `feat/reality_grab`, 메인 트리 — 워크트리는 2026-10-02 통합·삭제)
- **ghost_hand**: M1·M2 완료. M3 = 손끝 캡슐(콜리전, 메시 정점 맞춤·손바닥 용접)·양손 충돌·핀치·주먹 판정 완료(`9433069e`). 관절→X_Bot AnimBP/Control Rig **비주얼** 매핑은 사용자 그래프 작업으로 완료(DoList 1-19, 2026-10-02 확인). 핸드트래킹으로 실제 잡히게 하는 건 grip_pose M0 로 처리.
  "손으로 NPC 를 밀 수 있다": 구현 완료(2026-09-30) — 손 바디가 Pawn Block, 자기 캡슐·몸 메시는 손 채널 Ignore, 막혀 벌어진 오차 비례로 NPC 수평 밀기(`PushNPCWithBlockedHand`). 헤드셋 확인 완료(2026-10-02). 막힘 판정은 npc_bone_collision M2 로 뼈 캡슐 기준이 됐고, 밀기 판정은 M3 로 접촉 기준으로 교체 완료(2026-10-02, SPEC_npc_bone_collision 전체 완료). 미충족: 팔 통과(SPEC 밖, 팔꿈치 보정 시도 후 되돌림 — X_Bot·사용자 팔 길이 차이).
- **grip_pose** (2026-09-27 'Chaos 내장 우선'으로 방향 수정): M0 완료(잡기 판정·손 속도 → 트래킹 앵커). M1 완료(손 채널 PhysicsBody Block, 헤드셋에서 잘 밀림). M2 = 후보 A(끊어지는 PhysicsConstraint) 구현·헤드셋에서 쥐어짐 확인. M3·M4 완료(2026-09-30, 헤드셋 확인) — 접촉 쥐기(핀치·주먹) + 가운데 마디 캡슐 + 관절별 고정 감싸기 + 팔 IK 로 손 메시↔콜라이더 일치. 남은 것: 컨트롤러 감싸기(듀얼 입력과 함께)·물건별 끊김 임계·양동이 무게(DT_ItemRegistry 0.5 실험값, 미커밋).
- **손바닥 상자 2개로 분할(2026-10-02, `c87a8b3d`, 헤드셋 확인)** — 손등을 상자에 올리면 뜨던 문제. 원인 = 상자 하나(두께 5.6cm)가 손목 쪽 두께로 맞춰져 손가락 뿌리 쪽(메시 손등 +1.5cm)에서 1.3cm 떴다. 손끝 방향 범위를 반으로 나눠 손목 쪽(반두께 2.97)·손가락 쪽(반두께 1.61, 손등 면 +1.49) 상자를 따로 맞춤(`NumPalmShapes`). 남은 차이: 손등 가운데(손목에서 4~8cm, 정점 48개)는 메시가 손가락 쪽 상자보다 1~1.4cm 높아, 손가락 뿌리를 대면 그 부분이 살짝 묻힐 수 있음. 손바닥 중심(쥐기 판정 기준점)은 두 상자 가운데.
- [ ] **federated_orchestrator 운영 보강 반영** — `tools/federated_orchestrator.py` 는 git 추적됨(`de23307d`), `--review gemini`·diff 파일 전달·빈 응답 가드 반영됨. 남은 것: 워크트리 pytest 용 `UV_PROJECT_ENVIRONMENT`·`UV_NO_SYNC` 를 스크립트가 직접 설정, UAT 가 `NewProjectTest.umap` 을 저장해 `git add -A` 에 섞이는 문제.

### 마찰 한도 쥐기 + 양손 쥐기 — `SPEC_friction_grip.md` (2026-10-03 SPEC 작성)
- M0 완료(2026-10-03, 헤드셋 없는 PIE): 드라이브 상한 + 재고착으로 핀치 늘어짐(0.75τ₀ → 42.0°, 이론 41.4°)·힘 상한 미끄러짐·흔들기 떨림 0.06cm 확인. 회전 토크 상한은 `p.Chaos.Solver.Joint.UseSimd 0` 필요(UE 5.5 SIMD 경로 미구현) — 수치·발견은 SPEC "M0 결과".
- [ ] M2 양손 — 구현(2026-10-04), 헤드셋 없는 PIE 확인(한 손 0.15 로 못 드는 횃불을 두 손 0.15+0.15 로 듦·기록 손이 놓으면 기록이 다른 손으로 넘어가 안 떨어짐·마지막 손이 놓아야 떨어짐). 헤드셋 확인 대기(DoList 1-23). 결과·함정은 SPEC "M2 결과". 두 번째 손은 인벤토리 슬롯이 비어 폰의 "못 든 물건 다시 쥐기" 루프가 매 프레임 재쥐던 버그 수정.

### 포복 몸 눕히기·신체 측정 + 메뉴·설정 UI — SPEC 작성(2026-10-04, 착수 전)
- [ ] `SPEC_body_measure_prone.md` — 헤드셋에서 Prone 이어도 몸이 직립이라 머리가 HMD 위치까지 못 감(자세 시스템은 태그·캡슐만, 몸 눕히는 코드 없음 — 헤드셋 없는 PIE 판정은 정상). M0 스파이크 → M1 눕히기 → M2 신체 측정(키·팔 벌림·다리는 컨트롤러를 다리에 대고).
- [ ] `SPEC_pause_settings.md` — **M0·M1·M2 구현 완료(2026-10-05, 브랜치 `feature/pause-settings`, 헤드셋 없는 PIE 확인)**: 왼손 Menu 버튼 메뉴(월드 안 멈춤, 열린 동안 이동·공격·잡기 차단)·설정(마스터 볼륨 서브믹스 + `USaveGame` 저장)·UI 장면 컴포넌트를 `UVRPlayerUIComponent` 로 이전. **M3 정보 화면(지도·파티 목업·퀘스트)도 구현(2026-10-06).** M1·M2 헤드셋 확인은 사용자가 대부분 완료(DoList 2-8, 저장 유지만 남음). 남은 것 = 저장 유지 + M3 헤드셋 확인(DoList 2-8) 후 머지. 신체 측정 M2 의 선행은 충족.

### 2026-10-05 헤드셋 테스트에서 나온 버그·점검 (미착수 — 결정·수정 대기)
- [ ] **인벤토리에서 소비템을 꺼내면 손에 안 나옴(버그 아님, 설계 결정 필요)** — 핸드트래킹 핀치로 붕대를 꺼내면 `ActivateItem` 이 `Consumable` 을 `UseItem` 으로 바로 소비한다(로그 `Used Bandage — HP +25` ×2). 인벤토리에서 하나 줄고 HP 만 오르며 손엔 아무것도 없다. 재료·잡템은 손에 나온다(PIE `Rock` 확인). 선택지: ① 소비=사용 유지 + 사용 피드백(HUD·소리) ② 소비템도 먼저 손에 꺼내고 사용은 별도 동작(머리·입 근처에서 놓기 등) ③ 손별 분리(한 손 사용·한 손 꺼내기, "조작 기능 임의 추가 금지" 규칙과 충돌 — 별도 결정). 사용자 결정 대기.
- [ ] **`GetMass()` 경고 "피직스 시뮬레이션 옵션이 켜져있어야 질량 구하기 가능" 반복(세션당 40건)** — 원인: 물리가 꺼진 아이템(상인 진열품·부착 중)의 질량을 `ItemMesh->GetMass()` 로 읽음. 호출처 `VRHandComponent.cpp` `CanHold`(:850)·접촉 쥐기(:653)·`GrabWithPhysics`(:877)·`HeldMassScale`(:305/309). `VRMeleeComponent.cpp:97` 은 이미 `GetMassOverride()` 로 회피. 수정안: `ADroppedItemBase::GetMassKg()` 를 두고 `UPrimitiveComponent::CalculateMass()`(오버라이드 있으면 그 값, 없으면 형상 계산, 물리 꺼져도 경고 없음)로 한 곳에 모은 뒤 4곳 교체. 진열품 판정이 0kg 으로 새는지도 같이 확인. 별도 `bugfix/` 로.
- [ ] **가드·제임스가 서로 싸움 — 호감도 누적(2026-10-05 13:16 세션)** — `affinity.db`: `Guard→James -20`·`Guard→Moca -15`·`Moca→Guard -10`·`Guard→Elara -5` 전부 `Hostile Hit`. 서버가 danger≥0.5 지각마다 -5(`main.py::_apply_hostile_affinity`)라 아군끼리 피격(Hit)이 반복되면 -30 이하 적대로 넘어가 교전. `seed.py` 는 보스·적·플레이어만 시딩하고 아군↔아군은 초기화하지 않아 테스트 잔재가 남음(`Guard→Unknown -40` 도 이미 Hostile). ① 즉시: `seed_affinity` 에 아군↔아군 0 시딩 추가 + 서버 재시작 ② 근본: 아군이 왜 서로를 때리는지(후보: `KineticDamage` 동역학 피해·투사체/범위 공격·래그돌 충돌) — 싸움이 붙는 장면의 서버 `[Affinity] … -5` 로그와 UE `TakeDamage` 가해자 로그 필요.
- [ ] **채팅 입력창 글자·배경 모두 흰색** — `WBP_Chat` `ChatInput` 을 어두운 배경 + 흰 글자로 고침(`bac02c79`, 브랜치 `feature/pause-settings`). 한글이 안 써지던 건 입력이 안 보여서 그렇게 느껴졌을 가능성(Roboto 폰트 + 메뉴 한글은 정상). 헤드셋/PIE 에서 실제 키 입력으로 확인 필요 — 그래도 한글이 안 써지면 IME 문제로 별도.
- [ ] **TakeDamage 중복 정리(리팩토링, 사용자 문의 단계)** — 데미지 공식 `max(0, Raw−Defense)×부위배율` 이 `EnemyCharacter`·`VillagerCharacter`·`UNPCStateComponent::ApplyDamage`(SmartNPC)·`AVRPawn::TakeDamage` 4곳에 복제. `AVRPawn` 은 `ACharacter + IPlayerBase` 이고 `ACombatCharacter` 아래가 아니라 상속 통합은 안 맞음. 안: ① `FCharacterAttributesBase::ApplyIncomingDamage(Raw, PartMultiplier)` 로 공식만 모음(위험 거의 없음) ② `ACharacter` 와 두 갈래 사이 얇은 중간 클래스 `ADamageableCharacter` 에 TakeDamage 뼈대 + 훅(`SmartNPC` 는 반환값이 Raw·`ReactToHit(Raw)` 라 동작 보존하려면 전체 오버라이드 필요). 순수 리팩토링(동작 동일) — 브랜치는 `refactor/` 로. 방향 미정.

### NPC 끌기·들기·던지기 — `SPEC_npc_lift_throw.md` (2026-10-02 착수)
- **M1 구현했다가 코드 제거(2026-10-02, 사용자 결정 — 재정리 후 다시 만듦, 커밋 안 함)**. 손바닥 상자 2개 분할은 남김. 다시 만들 때 쓸 실측: ① 넉다운 후 안착 0.6초 만에 기상해 쓰러진 NPC 는 헤드셋에서 못 잡음 → 서 있는 NPC 를 쥐고 들 때 래그돌 전환으로 바꿈(사용자 결정) ② X_Bot 아래팔·주민 팔 전체에 물리 바디 없음 → 제약은 윗팔·어깨에 걸리고 감싼 아래팔이 흔들려, 지금 팔 위치로 놓기를 재면 매단 뒤 0.6~2초 만에 놓침 ③ 잡은 손을 NPC 뼈 캡슐 밀어내기에 그대로 두면 손가락이 팔에서 밀려 바로 놓침 ④ 손 60cm 순간이동이 매단 NPC 를 끌고 튐(골반 3m) ⑤ `MaxCarryMass` 5(88N)면 80kg 꿈쩍 안 함, 40(610N)이면 한 손 끌기·양손 들기 됨. NPC 질량 X_Bot 80.8·Farmer 79.7kg.

### Jevlike 잔여 — 전투 `docs/SPEC_jev_neuro_symbolic_st.md` §9 (Phase 1~3 코드 완료 2026-09-22) · 일상 `docs/SPEC_jev_daily.md` (2026-09-23 신설)
- [ ] (보류) **StateTree 에셋 바인딩(전투 전용)** — 2026-09-24 판단: 붙여도 동작 변화 0. 전투 승수는 `SelectCombatAction`·`ComputeEQSWeights` 가 캐시를 직접 읽고, `FSTEvaluator_JevTactics`·`FSTCondition_NoulGuard` 는 코드 어디서도 안 쓰이며 `noul_harmful` 은 0.0 고정이라 가드가 막을 일이 없다. Noul(유해) 헤드를 학습할 때 재검토.
- [ ] **`BREAKTHROUGH_GAIN`(3.0) 체감 튜닝(헤드셋)** — Phase 4 헤드셋 없는 PIE 는 전부 완료(아래 Done). 돌파 세기가 게임적으로 적당한지만 사용자 체감.
- [ ] (선택) **전투 라벨 재생성 + 전투 모델 재학습** — 전투는 현재 휴리스틱 경로(A안, 아래 Done). 전투에도 모델을 쓰고 싶을 때만: soft 5단계 등급(`GRADE_HINT` 재사용)·프롬프트에 거리 전술 의미·HP×거리 3×3 층화 샘플링으로 재생성 → 재학습 → `evaluate_tactics` 를 `self._model_probs(context) or heuristic_probs(metrics)` 로 복귀.
- [ ] **Jev M2 잔여** — ① 골드 시트 50건 사람 입력(`finetune/jev/data/gold_sheet.html` → `gold_answers.json`) 후 `eval` 로 사람·LLM 일치율 확인, 낮으면 31b 로 daily 재라벨 ② 체크포인트 git 추적 여부(현재 `**/models/*` ignore — 다른 PC 는 휴리스틱. 파일 0.19MB 라 추적 부담 없음, 공개 저장소 공개 여부만 결정) ~~③ 헤드셋 없는 PIE 로 모델 경로 daily 관찰~~(2026-09-24 서버 `jevlike 로드 완료 device=cuda` 후 PIE daily 판정 정상 유입 확인) ④ 골드셋이 look_at·stay 편중(24·15/50) — 활동별 상한 보완 시트.

### 척수반사 테이블 — 잔여 1건 (PR #23 Develop 머지 완료 2026-08-22)
- [ ] **PIE 검증 5항목**(SPEC §7) — 학습 완료됨. 에디터 열고 검증 가능.

### LLM 대사·계획 품질 잔여 (2026-09-05 발굴)
- [ ] **대화 기록이 대사를 지배한다** — e4b(v1)가 직전 답변 문형을 그대로 복사한다(2026-09-18 재주행: RAG·goal·num_ctx
  배선 수정 뒤 James 2턴 반복은 소멸, 남은 건 장기 기록 오염 케이스). 오답이 한 번
  기록되면 이후 모든 턴이 그걸 따라가고, 인벤토리·plan 을 고쳐도 안 풀린다. 실측: plan 을 완전히
  빼도 "한번 확인해 보시겠어요?" 가 유지됐고 기록을 비우자 사라졌다. 근본책은 기록 주입량 축소
  (현행 최근 5턴) 또는 요약 주입. 지금은 오염 시 수동 초기화 외 방법이 없다.
- [ ] **Stage2 steps 가 전방 계획이 아니다** — 입력이 NPC 직전 대사 한 줄뿐이라 방금 한 말을
  요약할 수밖에 없다. goal 은 예시 JSON 으로 잡혔지만 steps 는 여전히 goal 의 되풀이. 지금은
  steps 를 Stage1 에 주입하지 않아 실害는 없다. 쓰려면 플레이어 발화·이전 goal 을 입력에 넣어야
  하는데, 격리 테스트에선 효과가 없었다(실제 흐름에선 미검증).
- [ ] **"줘" 에 Drop 이 나오는 경우** — PIE 에서 1회 실측. `Drop` 은 월드에 떨어뜨릴 뿐 전달이
  아니다. GiveItem/HandObject/Drop 구분을 프롬프트에 명시할지 검토.

### 아이템 메시 — 외부 무료 에셋으로 전량 교체 (2026-10-02~03)
Hunyuan3D 생성 메시(`/Game/Core/Mesh/Items`, 216개)는 품질 문제로 전부 삭제(그 생성 파이프라인 품질 이슈 4건은 이로써 폐기, 원문은 `주간기록/2026-W36`). 원본은 `ItemRegistry.csv` → `DT_ItemRegistry` 재생성(전체 치환, 양동이 무게 2.5 로 복귀). 크기는 각 메시 최장변을 예전 메시 치수(없으면 실물 추정)에 맞춰 임포트·병합 단계에서 굽고, 피벗은 대부분 중심. 충돌체는 2026-10-03 자동 생성 완료(`SPEC_item_collision_gen` M3, 아래 Done). 손안 자세(`HoldOffset`/`HoldRotation` 전부 0)는 아직 — 헤드셋 `TuneGrab`.
- 아이템 72종 전부 메시 있음(2026-10-03). DivingHelmet·ShipWheel 은 사용자 결정으로 CSV·테이블·아이콘에서 삭제 — `docs/CORE_NPC_LOREBOOK.md` 친화도 목록과 Jev 학습 데이터(`finetune/jev/data/`)에는 이름이 남아 있음(런타임 미사용).
- 위치: 메시 전부 `/Game/Core/Mesh/Items/<ItemID>`(아이템당 1개, 이름 = ItemID), 머티리얼·텍스처는 `/Game/Core/Mesh/Items/Materials/<출처>/`(팩마다 같은 이름이 겹쳐 출처별로 나눔).
- 출처: Quaternius 51(Fantasy Props MegaKit·Ultimate RPG·Ultimate Food·Pirate Kit·Survival) · Poly Pizza 17 · Fab 4(Lowpoly Stylized Medieval Weapons 2, Free Prop Bundle 랜턴 1, Medieval Blacksmith tools `tools_10` 1).
- SpellScroll(Poly Pizza Parchment)은 StarMap(RPG Parchment)과 같은 모델이라 겉모습이 같다.
- **라이선스 — CC-BY(출처 표기 필요, 실제 출시 시 크레딧 또는 교체)**: Poly Pizza — Magnifying Glass(Gabriel Valdivia, `c8HQVCBMIMR`) · Bull horn(Poly by Google, `a47Cj9TMSSu`) · Glasses(jeremy, `9i5mmOwt7cu`) · Feather→QuillPen(Christopher F, `5KcQoNwH2IG`) · Animal Hide→Leather(Zsky, `GaurgMNrWC`) · Whistle(Rendercore, `o2k3RhzfuV`) · Fern→HerbBasket(Danni Bittman, `6ttropQuVzQ`) · Crystal Bowl→CrystalBall(CreativeTechLab, `Eaz0fHZOTI`) · Cap→FeatherCap(J-Toastie, `aWxhfEnYwl`) · Ice pick→Lockpick(Poly by Google, `8lQ8h4h_N9Z`). Fab CC BY 4.0 — Free Prop Bundle(Lantern) · Lowpoly Stylized Medieval Weapons Pack(Bklleb: GuardSpear·GuardShield).
  CC0 — Quaternius 전부(Parchment→SpellScroll 포함) · Bedroll(Kenney) · Telescope·Toolbox(CreativeTrio) · Mic(iPoly3D). Fab 스탠다드(무료) — Medieval Blacksmith tools pack(RepairHammer).
- 함정: Quaternius FBX 는 실물 크기가 아니고 모델마다 배율이 다름(단검 156cm, 나침반 91cm). `set_lod_build_settings` 의 BuildScale 은 저장만 되고 즉시 재빌드되지 않아 크기가 안 바뀜 — FBX 는 `import_uniform_scale` 재임포트, GLB 는 액터 스케일 후 `merge_static_mesh_actors` 로 구움. 병합 결과 이름엔 `SM_` 접두가 자동으로 붙는다. 레벨 배치 `BP_DropItem` 은 메시를 테이블에서 다시 읽지 않아(에디터 편집 시만 `SyncMeshFromItemData`) 메시 교체 시 배치 액터를 따로 갱신·저장해야 함(World Partition 셀은 `WorldPartitionBlueprintLibrary.load_actors` 로 로드).

### 백로그 (착수 미정, 2026-07 발굴분 — 필요 대두 시 개별 `/feature-spec`)
- [ ] (참고) **오목 아이템 충돌체 CoACD** — 양동이·컵·바구니 테두리·손잡이 정밀도가 필요할 때 엔진 자동 볼록 분해(양동이 현재 20헐) 대신 CoACD 로 굽기. 손가락은 캡슐 유지(매 프레임 재용접·GJK 비용, 2026-10-02 판단).
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


완료 항목은 날짜와 함께 여기 적고, 주가 바뀌면 `docs/주간기록/2026-W##_주제.md` 로 옮기고 여기서 **삭제**한다. 비어 있는 것이 정상.
주간기록·Memo·DoList 는 2026-09-21 부터 git 추적(`docs/` ignore 해제 — 그날 checkout 사고로 Memo 가 날아간 뒤 결정. git 경로는 소문자 `docs/memo.md`). 세션 간 유일한 서사 기록 — 커밋 해시·수치·함정을 반드시 같이 남길 것. `docs/.obsidian/`·`*.canvas`·`*.txt` 는 여전히 ignore. 주차 목록은 폴더 `ls`, 결정 이력은 `주간기록/_결정원장.md`.
**W39(09-21~27) 항목은 2026-09-24 에 1~2줄로 압축했다. 압축 전 원문(커밋 해시·수치·함정 전체)은 `git show 5abfccca:docs/memo.md` — `/week-end` 이관 때 이걸 소스로 쓸 것.**

