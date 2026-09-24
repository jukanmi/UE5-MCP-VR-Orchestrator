# Session Memo

세션 간 인수인계 단일 소스.
형식: `## Todo` (미완) · `## Done` (날짜 필수, 주간기록 이관 전 임시 적재). 함정·제약은 `.agents/rules/pitfalls.md`, 미구현 설계 논의는 `docs/DESIGN_*.md`.

---

## Todo

### 전투 SPEC 잔여 — `docs/SPEC_realistic_combat.md` (§3.2·§2.3 완료 2026-09-21, §3.5 공격토큰 중앙화는 2026-09-22 폐기 — 필드 몹 `MaxAttackers=2` 로 충분, 나머지 3개 보류)
- [ ] **저HP 자동 후퇴·회복 연쇄(척수)** — 2026-09-24 결정. Combat ∧ HP ≤ 0.3 ∧ 회복 Consumable 보유 → 주사위 없이 [EQS 후퇴 → UseItem] 결정론 주입, 전투당 1회. 회복템 없으면 기존 Flee 램프. 상세 `SPEC_jev_daily.md` 비고 B.1. 전투 브랜치에서 구현.
- [ ] **§3.1·3.3 구현** — 설계 확정 `SPEC_realistic_combat.md` §5(2026-09-22). 남은 순서: Footwork(Strafe/Disengage, `Key_Style` 변형, EQS 안 씀) → Startle 룰 → 투사체 회피 → 청각→시각 융합(`FPerceptionData.Context` + Python `context` 1필드). 새 EAction·컴포넌트 0.

### Jevlike 잔여 — 전투 `docs/SPEC_jev_neuro_symbolic_st.md` §9 (Phase 1~3 코드 완료 2026-09-22) · 일상 `docs/SPEC_jev_daily.md` (2026-09-23 신설)
- [ ] **StateTree 에셋 바인딩(전투 전용)** — 일상은 큐 주입이라 무관. 전투 승수는 `SelectCombatAction` 이 캐시를 직접 읽으므로 ST 노드가 실제로 막을 상태가 있는지부터 판단(SPEC_jev_daily 미결).
- [ ] **Phase 4 PIE 실측(전투)** — 적 조우, HP 저하 시 Flee 배율로 거리 벌리기, 백엔드 단절 시 1.0 중립 폴백. 폴백 검증은 휴리스틱만으로 가능.
- [ ] **jevlike 설치·체크포인트 학습 = 일상 M2** — 2026-09-23 확인: 패키지 미설치, `app/models/jevlike_tactics.pt` 없음 → 휴리스틱만 동작. LLM 로그엔 일상 액션 3건뿐이라 합성 데이터 필요. 전투+일상 합쳐 체크포인트 1개.

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

완료 항목은 날짜와 함께 여기 적고, 주가 바뀌면 `docs/주간기록/2026-W##_주제.md` 로 옮기고 여기서 **삭제**한다. 비어 있는 것이 정상.
주간기록·Memo·DoList 는 2026-09-21 부터 git 추적(`docs/` ignore 해제 — 그날 checkout 사고로 Memo 가 날아간 뒤 결정. git 경로는 소문자 `docs/memo.md`). 세션 간 유일한 서사 기록 — 커밋 해시·수치·함정을 반드시 같이 남길 것. `docs/.obsidian/`·`*.canvas`·`*.txt` 는 여전히 ignore. 주차 목록은 폴더 `ls`, 결정 이력은 `주간기록/_결정원장.md`.
**W39(09-21~27) 항목은 2026-09-24 에 1~2줄로 압축했다. 압축 전 원문(커밋 해시·수치·함정 전체)은 `git show 5abfccca:docs/memo.md` — `/week-end` 이관 때 이걸 소스로 쓸 것.**

- [x] **Jev 일상 활동 매칭 M1 — PIE 검증 완료 (2026-09-24)** — `SPEC_jev_daily.md`. Python `b704d0a6`(테스트 15) · C++ `769bd13e` · `9b98ed7b`(기상 억제) ·
  `ca316c01`(POI 액터 3개). 완료 기준 1~15 전부 헤드셋 없는 PIE 확인.
- [x] **BehaviorMode 6→2 축소 (2026-09-24)** — `13f94484`, `SPEC_behavior_mode_reduce.md`. Stage1 5턴 Mode∈{Common,Combat}. 고아 BP enum 삭제만 DoList 1-17.
- [x] **Stumble 3분기 + 4방향 몽타주 — 헤드셋 검증 완료 (2026-09-23)** — `SPEC_realistic_combat.md` §5.3. `NPCRagdollComponent` 만 수정:
  Knockdown ≥40 / Stumble ≥20 / Flinch, Block 중 강타는 가드 브레이크(Stumble). 방향 부호: `LastHitDirection` 은 가해자→대상이라 정면 피격이 로컬 X 음수.
- [x] **Stumble "제자리 밀림"·스냅백 — 루트 모션으로 해결 (2026-09-23)** — 원인·정답은 pitfalls.md D(Mixamo 루트 모션·`MaxWalkSpeed=0`). 이동 잠금 코드 전부 제거.
- [x] **1-16 임포트 뒤처리 (2026-09-23)** — AnimSequence 0개 상태를 `Downloads` 폴더 FBX 4개로 재임포트, `AS_Hit_*`·`AM_Stumble_*` 4개 생성, 잔재 21개 삭제. 함정은 pitfalls.md D(FBX take 2개).
- [x] **1-14 패링·퀘스트 SFX 검증 (2026-09-23)** — 패링 헤드셋 체감 OK, `CheckReflex` 4000회 24.7%(기대 25%). `S_Hit_Metal_0` 음색 교체 미정. 퀘스트 갱신음 실청만 DoList 1-14.
- [x] **`.claude/skills/` 9개 디렉터리 형식 전환 (2026-09-23)** — 평면 `.md` 라 한 번도 로드된 적 없었음(pitfalls.md D). 잔여였던 gstack `spec` 이름 충돌은 `feature-spec` 리네임으로 해소(2026-09-24).
- [x] **세션·주 마감 스킬 정비 (2026-09-23)** — `session-end` 에 DoList 이관 단계, 신규 `week-end`(Done→주간기록 이관, git log `--all` 이 뼈대). 발견: INDEX 표가 W23 에서 멈춤.
- [x] **Jevlike 전술 편향기 파이프라인 구현 (2026-09-22)** — `SPEC_jev_neuro_symbolic_st.md` §5~7. `JEV_QUERY`/`JEV_DECISION`·`JevlikeService`(휴리스틱 폴백 <1ms)·
  Controller 캐시·0.3s 워치독·`FSTEvaluator_JevTactics`·`FSTCondition_NoulGuard`·승수 Clamp. `verify all` 클린, pytest 83. 설계 근거는 pitfalls.md J.
- [x] **Jevlike SPEC 수립 (2026-09-22)** — 클라우드 Jev 대신 오픈소스 `vinnylarouge/jevlike` 로컬 채택, State 선택기 → 전술 가중치 편향기로 재정의(pitfalls.md J).
- [x] **대화창 UI 분리 — 손 패널 → 카메라 고정 패널 (2026-09-21)** — `05d610b0`. `UChatWidget` 신설·`VRCamera` 부착 `ChatWidgetComp`, 포커스 잃으면 숨김.
  WBP_Chat·BP_VRPawn 배선·PIE 전부 MCP(새 WBP 는 루트 없이 생성 → 기존 WBP 복제+reparent 가 유일한 길). 헤드셋 육안만 DoList 1-15.
- [x] **엔진 빌드 봉쇄 해소 (2026-09-21)** — `.uproject` `VisualStudioTools.Enabled=false` 한 줄. 경위는 pitfalls.md U-2.
- [x] **`git checkout main` 으로 ignore 파일 6개 파괴 → 복구 (2026-09-21)** — 경위·복구 소스는 pitfalls.md U-1.
- [x] **전투 SPEC 잔여 + 로드맵 Phase4-5 (2026-09-21)** — §3.2 NPC RNG 패링(`ESenseType::Parried`)·§2.3 패링 리포트·퀘스트 SFX(햅틱은 폐기)·
  서브퀘스트 5종 E2E(`TryPickupInto` 가 줍기 검증 진입점). 플레이어 Block/Parry 는 `dfb16ab2`(09-18)로 이미 완료돼 있었음.
- [x] **스토리 디렉터 PIE 검증 완료 (2026-09-21, `SPEC_story_director.md`)** — b1→end 7비트 전이를 MCP 로 실제 게임 경로 재현. 전투는 반드시
  `GameplayStatics.apply_damage`(`ApplyDamage` 직접 호출은 HandleDeath 안 탐), 대화는 `/api/debug/say`. DoList 1-12 전항목 완료. Phase A~C 는 W38 기록.
