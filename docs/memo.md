# Session Memo

세션 간 인수인계 단일 소스.
형식: `## Todo` (미완) · `## Done` (날짜 필수, 주간기록 이관 전 임시 적재). 함정·제약은 `.agents/rules/pitfalls.md`, 미구현 설계 논의는 `docs/DESIGN_*.md`.

---

## Todo

### VR 손 — `SPEC_vr_ghost_hand.md` · `SPEC_vr_grip_pose.md` 진행 상황 (브랜치 `fed/vr_ghost_hand-09252306`, 워크트리 `C:\github\UE5_MCP_VR_wt_vr_ghost_hand-09252306`, 2026-09-27 기준)
- **ghost_hand**: M1·M2 완료. M3 = 손끝 캡슐(콜리전, 메시 정점 맞춤·손바닥 용접)·양손 충돌·핀치·주먹 판정 완료(`9433069e`). 관절→X_Bot AnimBP/Control Rig **비주얼** 매핑은 그래프 작업이라 미완 — DoList 1-19 등록. 핸드트래킹으로 실제 잡히게 하는 건 grip_pose M0 로 처리.
  미충족 완료 기준: "손으로 NPC 를 밀 수 있다"(손 채널이 Pawn 무시) · 팔 통과(SPEC 밖, 팔꿈치 보정 시도 후 되돌림 — X_Bot·사용자 팔 길이 차이).
- **grip_pose** (2026-09-27 'Chaos 내장 우선'으로 방향 수정): M0 완료(잡기 판정·손 속도 → 트래킹 앵커). M1 완료(손 채널 PhysicsBody Block, 헤드셋에서 잘 밀림). M2 = 후보 A(끊어지는 PhysicsConstraint) 구현·헤드셋에서 쥐어짐 확인. M3·M4 완료(2026-09-30, 헤드셋 확인) — 접촉 쥐기(핀치·주먹) + 가운데 마디 캡슐 + 관절별 고정 감싸기 + 팔 IK 로 손 메시↔콜라이더 일치. 남은 것: 컨트롤러 감싸기(듀얼 입력과 함께)·물건별 끊김 임계·양동이 무게(DT_ItemRegistry 0.5 실험값, 미커밋).
- [ ] (보류) **한 손 컨트롤러 + 한 손 실제 손 동시 사용** — 코드는 손마다 이미 독립 판단. Quest 런타임이 컨트롤러가 켜져 있으면 핸드트래킹을 안 넘김(실측: 오른 컨트롤러 내려놓고 5초간 손 추적 0/300). Link 런타임은 `XR_META_simultaneous_hands_and_controllers` 지원(시작 로그), UE 5.5 OpenXR(헤더 1.0.27)은 미사용·미정의. 하려면 `PostConfigInit` 프로젝트 플러그인(IOpenXRExtensionPlugin)으로 확장 요청 + 세션 후 `xrResumeSimultaneousHandsAndControllersTrackingMETA`. 값: `XR_TYPE_SYSTEM_SIMULTANEOUS_HANDS_AND_CONTROLLERS_PROPERTIES_META=1000532001`·`..._TRACKING_RESUME_INFO_META=1000532002`·`..._PAUSE_INFO_META=1000532003`(Khronos openxr.h 대조).
- [ ] **federated_orchestrator 운영 보강 반영** — 메인 트리 `tools/federated_orchestrator.py`(미추적)에 `--review gemini`·diff 파일 전달·빈 응답 가드 반영됨. 남은 것: 워크트리 pytest 용 `UV_PROJECT_ENVIRONMENT`·`UV_NO_SYNC` 를 스크립트가 직접 설정, UAT 가 `NewProjectTest.umap` 을 저장해 `git add -A` 에 섞이는 문제. 스크립트 자체의 git 추적 여부도 결정 필요.

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

### 3D 아이템 텍스처 잔여 품질 이슈 (2026-09-03 원인 규명, 조치 미완)
- [ ] 4건 미조치 — 상세는 `주간기록/2026-W36` 메모. 요약:

  **1. 감폴리 미달 — 원인 확정·해결책 검증됨, 전체 적용만 남음.**
  `hy3dgen/shapegen/postprocessors.py` 의 `reduce_face()` 가 pymeshlab 감폴리에 `preservetopology=True` + `qualitythr=1.0`(MeshLab 기본 0.3)을 하드코딩해, 구멍 많은 복셀 메시에서 목표의 7배 근처에 멈춘다. 컴포넌트 수와는 무관(상관계수 0.341, StarPendant 는 컴포넌트 1개인데 24,508).
  `generate_textures.py` 에 `Hy3DFastSimplifyMesh(preserve_border=False)` 2단 감폴리를 추가해 해결 확인: Glasses 22,138→3,232 · StarPendant 24,508→3,159 · ShipWheel 22,636→4,123. **단 72종 전체 재생성은 미실행** — 현재 커밋된 세트는 6종만 이 수정이 적용된 혼재 상태.
  선행 조건: venv 에 `pyfqmr` 필요(설치 완료). `Hy3DSampleMultiView` 는 elevation 을 `{-90,-45,-20,0,20,45,90}` 로만 받는다(그 외 값은 KeyError).

  **2. 검은 텍셀(미착색) — 해결 실패.**
  얇은 형상에서 텍셀의 58~83%가 순수 검정. 측정 신뢰성은 확인됨(면중심 1점·면적 12점·면적가중이 모두 일치). 카메라를 6뷰→10뷰로 늘려도 WineCup 80.9%→66.2%, Glasses 82.6%→64.0%, GuardSpear 는 58.1%→62.8%로 오히려 악화. 뷰 추가로는 못 고친다. 베이크 단계의 커버리지/마스크 로직을 파고들거나 다른 텍스처링 경로가 필요.

  **3. 페인트 자체가 어두움 — 참조 아이콘 문제.**
  멀티뷰(페인트 직후) 밝기부터 이미 낮은 부류: Glasses 0.162 · BrokenCompass 0.071 · AncientScroll 0.087 · Leather 0.119. 참조 아이콘이 불꽃·날개 등 이펙트가 얹힌 복잡한 일러스트인데 메시는 단순 덩어리라 페인트 모델이 매핑에 실패한다. 이펙트 없는 단순 아이콘 재제작 없이는 생성 파이프라인만으론 해결 불가.

  **4. 저가중치 뷰 색 환각.** Rock 바닥면이 파랗게 칠해짐(원본은 흰 대리석). 바닥·상단 뷰에서만 보이는 면은 페인트 모델이 근거 없이 지어냄.

### 백로그 (착수 미정, 2026-07 발굴분 — 필요 대두 시 개별 `/feature-spec`)
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

- [x] **SPEC_vr_grip_pose M4 — 가운데 마디 캡슐·주먹 쥐기·관절별 고정 (2026-09-30, 헤드셋 확인)** — 손가락 캡슐 손당 5 → 10(가운데·끝마디, 메시 정점 맞춤). 주먹 쥐기 = 손바닥 + 손가락 2개 이상이 닿고 손바닥 중심→마디 선이 물건을 지남(엄지가 손가락 위를 덮어 핀치로 안 걸리던 것). 감싸기 = 손 근처 20cm 아이템에 마디가 닿으면 그 마디를 움직이는 관절을 직전 프레임 각도에 고정(가운데 → 첫·둘째, 끝 → 세 관절), 끝 관절은 계속 굽어 감쌈. 풀기 = 실제 손가락 모양이 안 겹침(툭 치고 뗌)·고정 각도보다 5° 펴짐·근처 물체 없음·트래킹 끊김. 손↔물건 충돌은 유지(사용자 지정). 물리 시간 ≈ 0.5ms/프레임. 떨림 원인 = 쥐기 전 닿은 물체에서 캡슐이 트래킹대로 물체 속에 옮겨져 손 전체가 밀렸다 당겨짐(계산량 아님).
- [x] **SPEC_vr_grip_pose M3 + 접촉 쥐기 + 손 메시 일치 (2026-09-30, 헤드셋 확인)** — ① 감싸기: 쥔 물건이 있으면 손가락 굽힘을 줄여 끝마디 캡슐이 표면에서 멈춤(이분 탐색, `OverlapComponent`). ② 접촉 쥐기(핸드트래킹): 엄지+다른 손끝이 같은 물건에 0.5cm 안으로 닿고 두 캡슐을 잇는 선이 물건을 지나면 쥠, 1.5cm 떨어지면 놓음, 손↔쥔 물건 충돌 유지. 핀치·주먹은 인벤토리 슬롯 발동에만. 이유: 제스처는 손가락이 물건 속에 들어간 뒤 성립 → 막혀 있던 물리 손이 쥐는 순간 튀어 제약이 즉시 끊겼다(로그 10/10). ③ 토크 임계 30,000→60,000(2.5kg 양동이 손잡이 ≈ 41,700). ④ 손 메시↔콜라이더 5~7.6cm 어긋남: FBIK(PBIK) 반복 20 → 60 으로 평균 1cm, 이어 애님 프록시에서 팔 2본 IK(`AnimationCore::SolveTwoBoneIK`, 극점 = FBIK 팔꿈치)로 평균 0.2cm. 몸 메시 틱 = `TG_PostPhysics`(물리 손바닥 이번 프레임 위치로 그림). ⑤ WaterBucket 충돌 모양 8헐(속을 채움) → 자동 분해 20헐(벽 48방향 폐쇄·속 비움 수치 검증). ⑥ `bShowCollisionOnStart`(BP_VRPawn 켬). C 실험(마찰만 들기)은 아이템 사용 키 불가로 폐기 — SPEC 기록.

완료 항목은 날짜와 함께 여기 적고, 주가 바뀌면 `docs/주간기록/2026-W##_주제.md` 로 옮기고 여기서 **삭제**한다. 비어 있는 것이 정상.
주간기록·Memo·DoList 는 2026-09-21 부터 git 추적(`docs/` ignore 해제 — 그날 checkout 사고로 Memo 가 날아간 뒤 결정. git 경로는 소문자 `docs/memo.md`). 세션 간 유일한 서사 기록 — 커밋 해시·수치·함정을 반드시 같이 남길 것. `docs/.obsidian/`·`*.canvas`·`*.txt` 는 여전히 ignore. 주차 목록은 폴더 `ls`, 결정 이력은 `주간기록/_결정원장.md`.
**W39(09-21~27) 항목은 2026-09-24 에 1~2줄로 압축했다. 압축 전 원문(커밋 해시·수치·함정 전체)은 `git show 5abfccca:docs/memo.md` — `/week-end` 이관 때 이걸 소스로 쓸 것.**

