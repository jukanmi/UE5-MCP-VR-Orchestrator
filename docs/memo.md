# Session Memo

세션 간 인수인계 단일 소스.
형식: `## Todo` (미완) · `## Done` (날짜 필수, 주간기록 이관 전 임시 적재). 함정·제약은 `.agents/rules/pitfalls.md`, 미구현 설계 논의는 `docs/DESIGN_*.md`.

---

## Todo

### VR 손 — `SPEC_vr_ghost_hand.md` · `SPEC_vr_grip_pose.md` 진행 상황 (브랜치 `feat/reality_grab`, 메인 트리 — 워크트리는 2026-10-02 통합·삭제)
- **ghost_hand**: M1·M2 완료. M3 = 손끝 캡슐(콜리전, 메시 정점 맞춤·손바닥 용접)·양손 충돌·핀치·주먹 판정 완료(`9433069e`). 관절→X_Bot AnimBP/Control Rig **비주얼** 매핑은 사용자 그래프 작업으로 완료(DoList 1-19, 2026-10-02 확인). 핸드트래킹으로 실제 잡히게 하는 건 grip_pose M0 로 처리.
  "손으로 NPC 를 밀 수 있다": 구현 완료(2026-09-30) — 손 바디가 Pawn Block, 자기 캡슐·몸 메시는 손 채널 Ignore, 막혀 벌어진 오차 비례로 NPC 수평 밀기(`PushNPCWithBlockedHand`). 헤드셋 확인 완료(2026-10-02). 막힘 판정은 npc_bone_collision M2 로 뼈 캡슐 기준이 됐고, 밀기 판정은 M3 에서 교체. 미충족: 팔 통과(SPEC 밖, 팔꿈치 보정 시도 후 되돌림 — X_Bot·사용자 팔 길이 차이).
- **grip_pose** (2026-09-27 'Chaos 내장 우선'으로 방향 수정): M0 완료(잡기 판정·손 속도 → 트래킹 앵커). M1 완료(손 채널 PhysicsBody Block, 헤드셋에서 잘 밀림). M2 = 후보 A(끊어지는 PhysicsConstraint) 구현·헤드셋에서 쥐어짐 확인. M3·M4 완료(2026-09-30, 헤드셋 확인) — 접촉 쥐기(핀치·주먹) + 가운데 마디 캡슐 + 관절별 고정 감싸기 + 팔 IK 로 손 메시↔콜라이더 일치. 남은 것: 컨트롤러 감싸기(듀얼 입력과 함께)·물건별 끊김 임계·양동이 무게(DT_ItemRegistry 0.5 실험값, 미커밋).
- **손바닥 상자 2개로 분할(2026-10-02, `c87a8b3d`, 헤드셋 미확인)** — 손등을 상자에 올리면 뜨던 문제. 원인 = 상자 하나(두께 5.6cm)가 손목 쪽 두께로 맞춰져 손가락 뿌리 쪽(메시 손등 +1.5cm)에서 1.3cm 떴다. 손끝 방향 범위를 반으로 나눠 손목 쪽(반두께 2.97)·손가락 쪽(반두께 1.61, 손등 면 +1.49) 상자를 따로 맞춤(`NumPalmShapes`). 남은 차이: 손등 가운데(손목에서 4~8cm, 정점 48개)는 메시가 손가락 쪽 상자보다 1~1.4cm 높아, 손가락 뿌리를 대면 그 부분이 살짝 묻힐 수 있음. 손바닥 중심(쥐기 판정 기준점)은 두 상자 가운데.
- (선택) **고스트 손 드라이브 하나로 통합** — 양손 맞대기 남은 0.14cm(2프레임 주기)까지 없애려면 속도 덮어쓰기·접촉 전환을 버리고 드라이브 하나(강성 22500·감쇠 300·최대 10000 = 100m/s², 앞먹임 2cm 차단 조건 제거)로. PIE 실측 양손 0.000cm·널빤지 0.02cm·사인 왕복 3.3cm(차단 조건 탓, 빼면 이론 0.14cm). 미는 힘 상한 13 → 88N. 헤드셋에서 0.14cm 가 거슬릴 때만.
- [ ] (보류) **한 손 컨트롤러 + 한 손 실제 손 동시 사용** — 코드는 손마다 이미 독립 판단. Quest 런타임이 컨트롤러가 켜져 있으면 핸드트래킹을 안 넘김(실측: 오른 컨트롤러 내려놓고 5초간 손 추적 0/300). Link 런타임은 `XR_META_simultaneous_hands_and_controllers` 지원(시작 로그), UE 5.5 OpenXR(헤더 1.0.27)은 미사용·미정의. 하려면 `PostConfigInit` 프로젝트 플러그인(IOpenXRExtensionPlugin)으로 확장 요청 + 세션 후 `xrResumeSimultaneousHandsAndControllersTrackingMETA`. 값: `XR_TYPE_SYSTEM_SIMULTANEOUS_HANDS_AND_CONTROLLERS_PROPERTIES_META=1000532001`·`..._TRACKING_RESUME_INFO_META=1000532002`·`..._PAUSE_INFO_META=1000532003`(Khronos openxr.h 대조).
- [ ] **federated_orchestrator 운영 보강 반영** — `tools/federated_orchestrator.py` 는 git 추적됨(`de23307d`), `--review gemini`·diff 파일 전달·빈 응답 가드 반영됨. 남은 것: 워크트리 pytest 용 `UV_PROJECT_ENVIRONMENT`·`UV_NO_SYNC` 를 스크립트가 직접 설정, UAT 가 `NewProjectTest.umap` 을 저장해 `git add -A` 에 섞이는 문제.

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
- [ ] **아이템 충돌 자동 생성 — `SPEC_item_collision_gen.md` (2026-10-03 M3 적용 완료, 남은 것: 헤드셋 확인 DoList 1-20)** — 기본도형 자동 선택 + 컨벡스 분해 폴백, 맞춤 코어는 뼈 캡슐 맞춤과 공유. 후속: NPC active ragdoll(손이 닿은 부위만 밀림)용 Physics Asset 바디 생성이 같은 코어 위에(뼈 캡슐 DataAsset → PA 바디 14개, 주민 루트 스케일 100 문제 선검증).
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

- [x] **SPEC_item_collision_gen M2·M3 — 아이템 72종 충돌 자동 생성·적용 (2026-10-03, 헤드셋 없는 PIE 확인)** — `UItemCollisionGen::GenerateItemCollision`(`Core/Physics/ItemCollisionGen`): V-HACD 2.3 직접 호출(concavity 0.02 — 0.05 면 와인컵 속이 참) → 헐 1개이거나 헐 부피 합 ≥ 전체 헐의 0.5 면 상자·구·캡슐을 메시 로컬 축·관성 주축 두 틀에서 맞춰 부피 오차 ≤ 0.5·로컬 축 돌출/함몰 ≤ 0.15 중 최소, 아니면 헐(상한 16). 결과 상자 12·캡슐 16·헐 44(합 317헐, 옛 Hunyuan 메시 428헐). 교체된 새 메시는 72종 중 68종이 충돌 0개였다(물리 바디 없음). 무게중심 = 바꾸기 전 충돌 무게중심 유지(`DefaultInstance.COMNudge`, `ADroppedItemBase::BeginPlay` 가 적용), 충돌이 없던 메시는 0 — 이번 적용은 전부 0. WaterBucket 수동 20헐은 메시 교체로 사라져 제외 목록 비움, 새 WaterBucket 속 비움 라인트레이스 깊이 0.78~0.87(옛 20헐 0.91). `tools/import_meshes_to_ue.py` 가 충돌 없는 메시에 이 함수를 부름. PIE(90fps, 62종 낙하): 관통 0, 10초 뒤 흔들림 2종(검·마이크가 이웃 아이템에 기대 20°/s). **30fps 에선 16종 관통**(바닥 닿는 프레임 이동 11cm) — 아이템 CCD 가 꺼져 있고, 켜면 30fps 에서도 0. 90fps 라도 던지기(≥10m/s)면 같은 이동량이라 `ADroppedItemBase::BeginPlay` 에서 `SetUseCCD(true)`(사용자 결정) — 8fps 낙하에서도 16종 관통 0. 실패한 길: 관성 주축만으로 맞추면 폭≈높이인 양동이에서 축이 기울어 캡슐이 위로 19% 튀어나옴(로컬 축 후보 추가로 해결), 크기 오차를 부피 오차와 같은 상한 0.5 로 두면 돌출을 못 막음.

- [x] **SPEC_item_collision_gen M1 — 맞춤 코어 추출 (2026-10-02)** — `Core/Physics/CollisionFit`(`Percentile`·`FitCapsuleOnAxis`)로 뼈 캡슐 백분위 맞춤을 옮기고 `NPCBoneCapsuleSet::FillFromMeshes` 가 호출. 검증: 옛 바이너리 Fill 덤프 ↔ 새 바이너리 Fill, DA 4개 메시 10개 float 완전 일치. 미결 결정(SPEC 반영): 수동 보호 = 제외 목록, 오목 판정 = V-HACD 직접 호출 헐 수(엔진 `DecomposeMeshToHulls` 는 `m_concavity=0` 고정이라 못 씀), 무게중심 원위치(COMNudge), 임포트 도구 연결 M3, 도형 조합 없음. 함정: Python `reload_packages` 는 dirty 패키지면 모달 확인창을 띄워 MCP 가 멈추고 입력 주입도 안 먹음(사용자 클릭 필요) — 검증용 변경은 리로드 말고 같은 값이면 저장 후 `git checkout` 으로 바이트 원복.

- [x] **SPEC_npc_bone_collision M3 — 접촉 기준 NPC 밀기, SPEC 전체 완료 (2026-10-02, 헤드셋 확인)** — `ProjectHandOutOfNPCs` 가 실제 손이 가장 깊이 들어간 NPC 를 함께 돌려주고, `PushTouchedNPC` 는 물리 손이 그 표면 목표에서 2cm 안일 때만 들어간 깊이 비례로 수평 스윕 이동. 손바닥 주변 구 오버랩·`HandPushProbeRadius` 삭제. PIE: NPC 23.6cm 밀림·손 멈추면 깊이 3cm 에서 정지, 바닥에 막힌 손이 NPC 앞 40cm 면 0cm, 뒤 NPC 캡슐에 막히면 정지. 시험 함정: 앵커를 한 프레임에 수십 cm 옮기면 `TryMeleeHits` 가 발동해 NPC 가 넘어진다.

- [x] **SPEC_npc_bone_collision M2 — 손이 NPC 뼈 캡슐에서 멈춤 (2026-10-02, 헤드셋 확인)** — `AVRPawn::ProjectHandOutOfNPCs`: 손 모양(손바닥 상자 → 캡슐 2줄 + 손가락 캡슐 10)과 근처 NPC 뼈 캡슐의 선분 최단거리로 드라이브 목표를 표면 밖으로 밀어냄(×4회). 밀어낼 방향 = 물리 손 쪽, 실제 손이 뼈 축을 넘으면 축 기준으로 비춰서 계산(안 그러면 몸을 돌아 뒤로 미끄러짐 — PIE 실측). 치수 있는 NPC 는 몸 캡슐이 손 채널 Ignore(`ACombatCharacter::BeginPlay`). 손 채널 상수는 `Core/Types/CollisionChannels.h`. 밀기는 M3 전까지 기존 구 오버랩.
- [x] **고스트 손 관성 제거 + 양손 버벅임 (2026-10-02, 헤드셋 확인)** — 원인 = Chaos 가속 모드 드라이브의 최대 힘이 곧 가속도 상한(1500 = 15m/s², 엔진 `SetMaxForce` 확인): 40cm 이동에 254ms·17.4cm 지나침. 빈손은 매 틱 속도를 목표 도달 속도로 덮어씀(`bHandVelocityTracking`) → 40ms·지나침 0. 덮어쓰기는 곧 충격(0.9kg×1m/s, 90fps ≈ 80N)이라 물건을 누르면 떨려서, 닿아 있는 동안(무게중심이 보낸 곳에 0.2cm 넘게 못 닿음 → 되민 방향 = 법선, 실제 손이 그 면 밖으로 나오면 해제)과 쥐었을 때는 스프링: 널빤지 누르기 1.2 → 0.01cm. 양손 버벅임 원인 = 두 손바닥 CCD(충돌 순간 되감기 ↔ 파고들기 2프레임 반복) → MACD: 0.56 → 0.14cm, 책상 내리치기 관통 없음. 함정: UE 5.5 `ComponentSweepMulti` 는 용접 모양 오프셋을 무시하고 같은 액터를 통째로 제외 — 손 스윕에 못 씀. 헤드셋 없는 PIE 측정은 `t.MaxFPS 90` + 백그라운드 스로틀 끄기 필수(30fps·8fps 로 떨어지면 수치가 달라짐).

- [x] **SPEC_npc_bone_collision M1 — NPC 뼈 캡슐 데이터·디버그 드로우 (2026-10-01, PIE 육안 확인)** — Physics Asset 대신 뼈 선분 + 반지름. `UNPCBoneCapsuleSet`(`Core/Physics/`, 계열별 DataAsset `/Game/Data/NPC/DA_BoneCapsules_{Villager,Fighter,Mannequin,XBot}`, 메시 10개 × 캡슐 14개), BP 12개 `BoneCapsules` 지정, `npc.DrawBoneCapsules 1`. 자동 채움(에셋 `Fill From Meshes`) = 축까지 정점 거리 75% 백분위·축 방향 2~98% 구간, 끝 부위는 관절→정점 무게중심. PA 재생성 시도는 폐기·원복(자동 PA 는 경계 상자 맞춤이라 부풀고, Quaternius 루트 스케일 100 때문에 바디 4개). 함정: Quaternius `_end` 뼈 방향 엉망, Live Coding 으로 USTRUCT 바꾸면 DataAsset FName 이 None 으로 저장됨, 틱 간격 0.1s NPC 는 1프레임 디버그 선이 깜박임(수명 = 틱 간격). 미결(Imp·Puglin 무기 손, Rogue 발, Warrior 갑옷)은 SPEC. 다음 M2 = 선분 최단거리 + 목표점 투영.

- [x] **SPEC_vr_grip_pose M4 — 가운데 마디 캡슐·주먹 쥐기·관절별 고정 (2026-09-30, 헤드셋 확인)** — 손가락 캡슐 손당 5 → 10(가운데·끝마디, 메시 정점 맞춤). 주먹 쥐기 = 손바닥 + 손가락 2개 이상이 닿고 손바닥 중심→마디 선이 물건을 지남(엄지가 손가락 위를 덮어 핀치로 안 걸리던 것). 감싸기 = 손 근처 20cm 아이템에 마디가 닿으면 그 마디를 움직이는 관절을 직전 프레임 각도에 고정(가운데 → 첫·둘째, 끝 → 세 관절), 끝 관절은 계속 굽어 감쌈. 풀기 = 실제 손가락 모양이 안 겹침(툭 치고 뗌)·고정 각도보다 5° 펴짐·근처 물체 없음·트래킹 끊김. 손↔물건 충돌은 유지(사용자 지정). 물리 시간 ≈ 0.5ms/프레임. 떨림 원인 = 쥐기 전 닿은 물체에서 캡슐이 트래킹대로 물체 속에 옮겨져 손 전체가 밀렸다 당겨짐(계산량 아님).
- [x] **SPEC_vr_grip_pose M3 + 접촉 쥐기 + 손 메시 일치 (2026-09-30, 헤드셋 확인)** — ① 감싸기: 쥔 물건이 있으면 손가락 굽힘을 줄여 끝마디 캡슐이 표면에서 멈춤(이분 탐색, `OverlapComponent`). ② 접촉 쥐기(핸드트래킹): 엄지+다른 손끝이 같은 물건에 0.5cm 안으로 닿고 두 캡슐을 잇는 선이 물건을 지나면 쥠, 1.5cm 떨어지면 놓음, 손↔쥔 물건 충돌 유지. 핀치·주먹은 인벤토리 슬롯 발동에만. 이유: 제스처는 손가락이 물건 속에 들어간 뒤 성립 → 막혀 있던 물리 손이 쥐는 순간 튀어 제약이 즉시 끊겼다(로그 10/10). ③ 토크 임계 30,000→60,000(2.5kg 양동이 손잡이 ≈ 41,700). ④ 손 메시↔콜라이더 5~7.6cm 어긋남: FBIK(PBIK) 반복 20 → 60 으로 평균 1cm, 이어 애님 프록시에서 팔 2본 IK(`AnimationCore::SolveTwoBoneIK`, 극점 = FBIK 팔꿈치)로 평균 0.2cm. 몸 메시 틱 = `TG_PostPhysics`(물리 손바닥 이번 프레임 위치로 그림). ⑤ WaterBucket 충돌 모양 8헐(속을 채움) → 자동 분해 20헐(벽 48방향 폐쇄·속 비움 수치 검증). ⑥ `bShowCollisionOnStart`(BP_VRPawn 켬). C 실험(마찰만 들기)은 아이템 사용 키 불가로 폐기 — SPEC 기록.

완료 항목은 날짜와 함께 여기 적고, 주가 바뀌면 `docs/주간기록/2026-W##_주제.md` 로 옮기고 여기서 **삭제**한다. 비어 있는 것이 정상.
주간기록·Memo·DoList 는 2026-09-21 부터 git 추적(`docs/` ignore 해제 — 그날 checkout 사고로 Memo 가 날아간 뒤 결정. git 경로는 소문자 `docs/memo.md`). 세션 간 유일한 서사 기록 — 커밋 해시·수치·함정을 반드시 같이 남길 것. `docs/.obsidian/`·`*.canvas`·`*.txt` 는 여전히 ignore. 주차 목록은 폴더 `ls`, 결정 이력은 `주간기록/_결정원장.md`.
**W39(09-21~27) 항목은 2026-09-24 에 1~2줄로 압축했다. 압축 전 원문(커밋 해시·수치·함정 전체)은 `git show 5abfccca:docs/memo.md` — `/week-end` 이관 때 이걸 소스로 쓸 것.**

