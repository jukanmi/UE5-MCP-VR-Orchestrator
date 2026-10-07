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

- [ ] **서브퀘스트 목표 마커 — 방향 결정 대기** — 월드 마커·메뉴 지도는 메인 비트의 `quest_target_tag` 하나만 가리킨다(`side/*.yaml` 에 목표 태그 없음, 마커는 월드당 1개). 서브퀘스트를 받아도 안내가 안 생긴다. 선택지: ① 활성 서브 목표로 마커 이동(서브 YAML 에 `quest_target_tag`) ② 마커 여러 개(메인 1+서브별) ③ 길 안내 경로(네비) — 별도 SPEC. 사용자 결정 대기.
- [ ] **Follow 중 전투 뒤 추적 복구** — `Follow` 는 지속 추적(`2e236262`)이지만 적대 반사로 전투에 들어가면 `ProcessNextAction` 이 추적을 끊고 `ExitCombat` 이 복구하지 않는다. `SPEC_party` M2 가 메운다(멤버는 전투 종료 후 다시 플레이어를 따라감).
- [ ] **`StoryDirectorSettings` LoadConfig 오류 2건(에디터 시작 시)** — `EnemyClassMap`(`zombie`→`/Game/Blueprint/Enemy/BP_Enemy_Wraith_C`)·`ItemClassMap`(`holy_sword`→`/Game/Blueprint/Item/BP_DropItem_C`) import failed. 해당 BP 가 없거나 경로가 바뀐 것으로 보임 — 스토리 이벤트의 `spawn_enemy`·`spawn_item` 경로가 안 먹을 수 있다. 원인 확인 필요.
- [ ] **`SignalAllies` 미디어 없음 경고 44건/세션** — `ActionData` 미매핑이면 무음 즉시완료(`pitfalls.md` B). 몽타주를 매핑하거나 액션 후보에서 빼기.
- [ ] **`Content/Maps/NewProjectTest.umap` UAT 재생성** — 지워도(`9c699528`) `sol_pi verify all` 의 UAT 가 매번 다시 만든다(untracked). `.gitignore` 에 넣거나 UAT 가 맵을 안 열게 하는 결정 필요(현재는 untracked 로 방치).
- [ ] **`.ignore` 파일이 계속 다시 생김** — 검색 도구(`rg`)가 읽는 파일이라 `docs`·`tools`·`OmniAgent_VR_System` 등이 검색에서 빠진다. 에디터·IDE 도구가 만드는 것으로 추정(원인 미확인). 생기면 지울 것.
- [ ] **메뉴 지도 재촬영** — 맵 영역·내용이 바뀌면 `T_WorldMap` 을 다시 찍어야 한다(절차: `pitfalls.md` D 의 "메뉴 지도 텍스처"). 서브퀘스트 목표·POI 를 지도에 얹을 때(`SPEC_poi`, 위 마커 결정)도 같이.

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

### 아이디어 메모 (사용자가 던진 것, 기록만 — 검토·착수 전, 2026-10-07~)
- [ ] **Whistle — 가벼운 음성 인식(STT) 모델** (2026-10-07, 검색 확인) — Cactus Compute 가 2026-10-02 공개. 16.9MB 한 파일·CPU 만·의존성 없음·첫 토큰 11ms(M4 Pro), LibriSpeech 등에서 Whisper base 보다 낮은 WER 주장. 가중치 HF `Cactus-Compute/whistle`, 엔진 GitHub `cactus-compute/needle`(`pip install cactus-needle`, C API `needle_load`/`needle_transcribe`). 30초 단위 일괄 처리(스트리밍 언급 없음).
  **한국어 미지원** — 영·독·불·스·이·네덜란드·폴란드 7개만. 라이선스 페이지에 명시 없음(HF 모델 카드 확인 필요). 지금 Whisper 계열을 쓰는 한국어 음성 입력 대체로는 부적합, 한국어 추가 여부만 추적. (동명이인: 칭화대 THU-SPMI 의 Whistle 은 다국어 음소 지도 CTC Conformer 90/218/543MB 연구 모델 — 별개.)
  출처: https://cactuscompute.com/blog/whistle · https://huggingface.co/THU-SPMI/whistle-large
- [ ] **Phonon-2 — 영어 전용 STT** (2026-10-07, 검색 확인) — Fermion Research 가 2026-09-29 공개. NVIDIA Parakeet TDT 0.6B v3 를 2.1비트로 양자화한 164MB(원본 2.5GB), 7개 영어 세트 평균 WER 5.21%(원본 4.96%·Whisper large-v3-turbo 6.58%), M5 맥북에어 174배속·H100 배치 6,680배속. Windows CPU·CUDA(Docker)·MLX 지원, 구두점·대소문자·단어별 타임스탬프. 라이선스 CC-BY-4.0(출처 표기), CLI 는 Apache-2.0. HF `FermionResearch/Phonon-2`.
  **영어 전용 — 한국어 X**, 스트리밍 언급 없음. Whistle 과 같은 이유로 한국어 입력엔 부적합. 한국어 STT 후보는 아래 항목.
  출처: https://huggingface.co/FermionResearch/Phonon-2
- [ ] **한국어 STT 후보 — CPU 로 도는 소형 조건** (2026-10-07, 검색 수집·미검증) — 조건: 16GB VRAM 을 UE5·Ollama 가 쓰므로 CPU 추론 가능한 소형. 기준표는 `models.handy.computer/languages/ko`(FLEURS 한국어 낭독 음성·Q8_0 양자화·Ryzen 4750U 노트북 CPU, 속도는 실시간 배수 — 1 미만이면 실시간 불가). 이전 음성 서버는 faster-whisper `large-v3` GPU(2026-09-12 폐기, 복원은 `ef8c663a^`).
  | 모델 | CER % | 크기 | CPU 속도 | 라이선스 | 비고 |
  | --- | --- | --- | --- | --- | --- |
  | Fun-ASR-MLT-Nano-2512 | 5.20 | 0.83GB | 4.5× | Apache-2.0(HF 카드·저장소 기준, 순위표는 FunASR v1.1 로 적어 불일치) | 표에서 "종합 추천". 소형 중 정확도 최고. 800M·31개 언어(한국어 포함). 아래 상세 |
  | Qwen3-ASR-0.6B | 5.82 | 0.79GB | 4.3× | Apache-2.0 | 실제 0.9B 파라미터. **스트리밍은 vLLM 백엔드에서만**(Linux 중심) |
  | whisper-small | 7.70 | 0.25GB | 3.4× | Apache-2.0 | 가장 무난·생태계 큼, 정확도는 한 단계 아래 |
  | SenseVoiceSmall | 8.27 | 0.24GB | 15.5× | FunASR 모델 라이선스 | 가장 빠름. 정확도 낮음 |
  | moonshine-base-ko / tiny-ko | 8.12 / 9.00 | 0.07 / 0.03GB | 미표기 | MIT | 극소형, 속도 수치 없음 |
  - 주의: ① 벤치는 깨끗한 낭독 음성 — 마이크·VR 잡음·짧은 명령 발화에서의 순위는 미검증 ② 푸시투토크(말 끝나고 변환)면 스트리밍 불필요, 지금 구조가 그랬다 ③ 후보 간 CER 차(5.2~5.8)는 신뢰구간 안쪽일 수 있음 ④ FunASR 라이선스는 상용 허용이라 하나 조건 확인 필요.
  - **Fun-ASR-MLT-Nano-2512 상세**(2026-10-07): 800M, FunASR 계열, 핫워드·ITN(숫자·날짜 정규화) 지원. **CPU 경로 = llama.cpp/GGUF** — GPU·Python 없이 단독 바이너리, Linux·macOS·**Windows 프리빌트** 있음(저장소 릴리스). 그 밖에 vLLM(스트리밍 SDK·WebSocket·VAD, 청크 720ms 예시)·transformers 5.17. 공식 한국어 CER 없음("재현 가능한 평가 공개 시 추가" 문구) — 5.20 은 제3자 수치. 스트리밍은 vLLM 경로뿐이라 CPU 에선 푸시투토크 일괄 변환 전제. 동명 `Fun-ASR-Nano-2512` 는 중·영·일(+중국 방언)용이라 한국어엔 MLT 쪽. 최대 오디오 길이·GGUF 가중치 정확한 저장소명은 미확인.
  출처: https://huggingface.co/FunAudioLLM/Fun-ASR-MLT-Nano-2512 · https://github.com/QwenAudio/Fun-ASR
  - **다른 한국어 벤치(자발 발화 기준, 2026-10-07)** — 위 FLEURS 는 낭독이라 쉽다. 실제 대화 음성(KsponSpeech)·전화 음성(AIHub)은 오류가 3배 가까이 커진다. 오류율은 하드웨어와 무관하므로 GPU 에서 잰 수치도 소형 모델 비교엔 유효.
    | 모델(소형·CPU 가능) | KsponSpeech clean / other CER % | FLEURS CER % | 출처 |
    | --- | --- | --- | --- |
    | Zipformer 스트리밍 ko(155.7M, int8) | **7.18 / 7.14**(청크 64 기준 7.53) | — | HF `kangkyu/icefall-asr-ko-streaming-zipformer-174m` |
    | Qwen3-ASR-0.6B | 18.56 / 16.26 | 5.82 | OpenKoASR |
    | whisper-small | 23.21 / 21.44 | 7.70 | OpenKoASR |
    | whisper-base | 30.40 / 27.80 | 12.98 | OpenKoASR |
    | whisper-tiny | 37.20 / 34.99 | 19.07 | OpenKoASR |
    | moonshine-base-ko | 미측정 | 8.0(F32) | HF 카드 |
    - **눈에 띄는 것: sherpa-onnx 한국어 스트리밍 Zipformer** — 155.7M·Apache-2.0·**CPU 전용 설계**, int8 RTF 0.049(실시간의 20배, CPU 기종 미기재), 청크 16/32/64 = 지연 320ms/640ms/1.28s·CER 8.26/7.82/7.53. 훈련 데이터 ≈6,500h(KsponSpeech 964h + AIHub 약 5,500h)라 **KsponSpeech 수치는 도메인 안쪽(유리한 평가)** — 다른 모델과 같은 잣대로 보면 안 됨. 진짜 스트리밍이라 말하는 도중 자막·조기 반응에 유리. 한계: 어휘 2,460 음절이라 고유명사·게임 용어 약함(핫워드 없음), Windows 는 카드에 언급 없음(sherpa-onnx 자체는 Windows 빌드 제공), 청크 크기 고정(16/32/64).
    - **OpenKoASR 리더보드**(`GT-KIM/open-korean-automatic-speech-recognition`)는 Whisper·Qwen3-ASR 만 있고 Fun-ASR·SenseVoice·Moonshine·Zipformer 는 없음 → 이 후보들끼리 같은 데이터로 잰 비교표는 아직 없다. 결과 제출 이슈 양식은 있음. 직접 재야 함.
    - 한국어 소형 Whisper 파인튜닝(예: ENERZAi EZWhisper-Small 1.58비트 70MB, 자체 보고로 small 18%→6.45%)도 있으나 모델 공개 여부·평가셋 미확인.
    출처: https://gt-kim.github.io/open-korean-automatic-speech-recognition/leaderboard_data.json · https://huggingface.co/kangkyu/icefall-asr-ko-streaming-zipformer-174m · https://huggingface.co/moonshine-ai/moonshine-tiny-ko · https://www.edge-ai-vision.com/2025/11/small-models-big-heat-conquering-korean-asr-with-low-bit-whisper/
  - 다음 한 걸음(착수 시): 실제 마이크 녹음 20~30문장(게임 명령체)으로 Fun-ASR-Nano·Qwen3-ASR-0.6B·whisper-small CER·지연 직접 비교.
  출처: https://models.handy.computer/languages/ko · https://huggingface.co/Qwen/Qwen3-ASR-0.6B · https://github.com/FunAudioLLM/SenseVoice
- [ ] **Jevlike 대체·보강 후보군** (2026-10-07, 검색 수집 — 미검증, 수치는 각 저장소 자체 주장) — 현행 `vinnylarouge/jevlike`(MIT, 바이트 단위 인코더·0.19MB, 메뉴 합성 98%)는 단일 패스 옵션 스코어러. 같은 입출력(문맥 + 옵션 N개 → 옵션별 확률)을 내는 오픈소스가 여럿 나옴:
  | 이름 | 기반·크기 | 구조 | 비고 |
  | --- | --- | --- | --- |
  | `olanotolu/jevbetter` | 해시 n-gram + 2층 트랜스포머 | 경쟁 옵션 어텐션 + 게이트 헤드 + 온도 보정 | **jevlike JSONL 포맷 호환(데이터 재사용 가능)**, MIT. 자체 벤치 top-1 0.916 vs 0.873·보정오차 절반, 대신 처리량 40 vs 4,608 menus/s(100배 느림, CPU 기준). 별 15개 |
  | `wfzyx/von` | ModernBERT-large 395M | 옵션 마커 + 온도 보정 | Apache-2.0, 로컬 23ms(A10G)·96ms(4 vCPU). **영어 전용(한국어 X)**, 가중치 고정·보정만 재학습. 컨텍스트 8192 |
  | `NandhaKishorM/laya` | ModernBERT-large 322~421M | 비자기회귀 + 전용 스코어 헤드 | **100개+ 언어** 주장. 학습 지원 여부 미확인 |
  | `TianyuCodings/NanoJev` | Qwen3-0.6B | 공유 백본 + 결정 헤드 | 가장 작은 LLM 계열, 학습 파이프라인 포함 |
  | `jaredpalmer/kev` | Qwen3.5 0.8~9B + LoRA | 옵션 표현 비교 포인터 헤드 | TypeSafe SDK 호환 API(`/v1/systemone`). 별 약 1.4k |
  | `bespokelabsai/nimble` | Qwen3.5-9B + LoRA | 후보 로짓 추출 | 학습 레시피·평가·데이터 큐레이션 공개 |
  | `theoleecj/semif` · `Rizzo-AI-Academy/rizzo-flow` | Qwen3.5-4B · 1.7~4B | 학습 없이 로짓 추출(llama.cpp) | 가장 쉬움, 대신 16GB VRAM 을 LLM 과 나눠 써야 함 |
  - 관련: Cactus **Needle3**(121M·8~29MB·Apache-2.0)는 도구 호출·구조 추출·임베딩용이라 "옵션 확률" 스코어러와는 결이 다름(신뢰도 점수만). 평가용으로 `JevBench` 가 언급됨(arXiv 2609.30243 `JevOut` 도 참고).
  - 판단 보류 사유: 현행은 0.19MB·5~20ms 로 충분하고 LLM·Ollama 가 이미 VRAM 을 씀. 검토 가치가 있는 건 ① `jevbetter`(데이터 그대로 비교 학습, 골드셋 `eval` 로 jevlike 와 맞대결) ② `laya`(한국어 문맥을 직접 먹여야 할 때). 한국어 컨텍스트를 쓰는지는 `jev_dataset.py` 확인 필요.
  출처: https://github.com/olanotolu/jevbetter · https://huggingface.co/wfzyx/von · https://www.datacamp.com/blog/top-open-source-jev-alternatives · https://huggingface.co/Cactus-Compute/needle3

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

- [x] **메뉴 M0~M3 + UI 컴포넌트 이전 (2026-10-05~06, 브랜치 `feature/pause-settings`, 헤드셋 확인 대부분 완료·M3 대기)** — M1 `47cd39df`: 왼손 Menu 버튼 메뉴(`UMenuPanelUIComponent`·`UMenuWidget`·`WBP_MenuPanel`·`IA_MenuToggle`), 열린 동안 이동·회전·공격·대시·상호작용·새로 쥐기 차단(`UVRPlayerUIComponent::BlocksGameplayInput`). 구조 `2dcec850`: UI 장면 컴포넌트 생성·튜닝값을 폰 생성자 → `UVRPlayerUIComponent` 런타임 생성으로 이전(VRPawn.cpp 1387→1329줄). M2 `bd865393`: 설정 화면(마스터 볼륨 서브믹스 `SetSubmixOutputVolume` + `USettingsSaveGame` 슬롯 저장·시작 적용). M3 `10511f20`·`96d8ad10`: 메인 목록 → 지도(`T_WorldMap` 정적 탑다운 + 앵커 마커)·파티(목업)·퀘스트, 서버 `Story` 블록에 `side_titles`. 헤드셋: 사용자가 Menu 버튼·시스템 메뉴 충돌·입력 차단·포인터·글자 대비·**소리 볼륨 실제 변화**·리팩토링 회귀 확인(DoList 2-8, 저장 유지·M3 만 남음). 함정은 `pitfalls.md` D.
- [x] **채팅 입력창 글자·배경 흰색 수정 (2026-10-05, `bac02c79`)** — `WBP_Chat.ChatInput` 을 어두운 배경 + 흰 글자로 명시(월드 위젯은 기본 Slate 브러시가 흰색). 실제 한글 입력은 확인 대기(Todo).
- [x] **퀘스트 마커 거리 비례 확대 (2026-10-05, `deb9f4b5`)** — `QuestMarkerActor`: 최소 2.5m·거리×0.1·최대 25m, 꼭짓점 높이 유지. PIE 5m → 스케일 2.5, 200m → 20.
- [x] **`Follow` 지속 추적 + 같은 명령 중복 필터 해제 (2026-10-05, `2e236262`)** — 원인: Follow 가 한 점으로 한 번 이동하고 끝남 + `LastQueuedKey` 가 완료 뒤에도 남아 두 번째 "따라와" 를 삼킴. 수정: `ExecuteFollow` → `ExecuteTrack`, 큐가 비면 키 해제, 따라가는 중 Jev 일상 시작 금지. PIE: 가드가 플레이어 곁 2.2m 에서 멈췄다가 12m 이동 후 다시 따라옴.
- [x] **저장소 정리 (2026-10-05)** — PR #30(`feat/reality_grab`→Develop, 48커밋) 머지, 로컬 브랜치 20개 삭제(`backup/*` 2개는 미머지 커밋이 있어 유지), `main` 을 Develop 위치로 올림(`026e3b70`), git flow(AVH) 초기화(production `main`·develop `Develop`·접두어 `feature/` 등), `docs/블로그` → `ai-log/블로그`(ignore 폴더) 이동.
- [x] **프롬프트 감사 정리 (2026-10-05, `005a4def`)** — `/claude-api prompt-audit`: `add-envelope` 스킬의 낡은 경로(`interface_input.py`)·`pitfalls.md` 의 `.claude` ignore 오기·`gemini-tiki-taka` 스킬(삭제)·그래프 스킬 수치 상한 제거.
- [x] **SPEC 3종 작성 (2026-10-05~06)** — `SPEC_poi`(이름 장소 시스템, C++ 해석), `SPEC_party`(멤버십·합류는 대화+호감도·일행은 따라다니되 반응 유지), `SPEC_pause_settings` M3(지도·파티 목업·퀘스트).


완료 항목은 날짜와 함께 여기 적고, 주가 바뀌면 `docs/주간기록/2026-W##_주제.md` 로 옮기고 여기서 **삭제**한다. 비어 있는 것이 정상.
주간기록·Memo·DoList 는 2026-09-21 부터 git 추적(`docs/` ignore 해제 — 그날 checkout 사고로 Memo 가 날아간 뒤 결정. git 경로는 소문자 `docs/memo.md`). 세션 간 유일한 서사 기록 — 커밋 해시·수치·함정을 반드시 같이 남길 것. `docs/.obsidian/`·`*.canvas`·`*.txt` 는 여전히 ignore. 주차 목록은 폴더 `ls`, 결정 이력은 `주간기록/_결정원장.md`.
**W39(09-21~27) 항목은 2026-09-24 에 1~2줄로 압축했다. 압축 전 원문(커밋 해시·수치·함정 전체)은 `git show 5abfccca:docs/memo.md` — `/week-end` 이관 때 이걸 소스로 쓸 것.**

