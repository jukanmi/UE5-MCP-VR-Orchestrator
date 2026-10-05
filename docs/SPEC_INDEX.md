# SPEC 진행 현황 (SPEC_INDEX)

SPEC 마다 열어 보지 않고 여기서 상태를 본다. 상세·근거·완료 기준은 각 SPEC 본문.
**갱신 규칙**: SPEC 의 마일스톤이 바뀌면 이 표의 그 줄도 같이 고친다. SPEC 이 끝나면 `docs/done/` 으로 옮기고 아래 "완료" 표로 내린다.
마지막 갱신: 2026-10-06.

범례: ✅ 완료 · 🔶 진행 중 · ⬜ 착수 전 · ⏸ 보류 · ❓ 상태 줄 없음

---

## 진행 중·대기

### VR 손·쥐기 (브랜치 `feat/reality_grab`)

| 이름 | 진행상황 | 미결사항 | 비고 |
|---|---|---|---|
| [friction_grip](SPEC_friction_grip.md) | 🔶 M0 ✅ M1 ✅(헤드셋 확인) M2 ✅ 구현(헤드셋 없는 PIE 확인, 헤드셋 대기 DoList 1-23) | 쥐는 힘 환산 강성·평활화·컨트롤러 그립 힘 튜닝, `UseSimd 0` 전역 비용(래그돌 여럿 프레임 시간 미측정), 회전 강성은 관성 최대축 하나 | **다음: M2 헤드셋 확인(DoList 1-23).** 던지기·건네기·양동이 손잡이 체감, `HoldOffset` 0 은 완료 기준 밖 |
| [npc_lift_throw](SPEC_npc_lift_throw.md) | ⏸ M1 구현 후 코드 제거(2026-10-02) | 충돌 피해 속도 임계·계수 PIE 튜닝 | friction_grip M2(양손) 구현으로 선행 조건 충족, 헤드셋 확인 뒤 재착수. 실측·함정은 memo |
| [npc_grip](SPEC_npc_grip.md) | ⬜ M1~M3 착수 전(인터뷰 2026-09-24) | 붙잡기 `EAction` 을 LLM 선택지에서 뺄지, 반사 룰 발동 조건, 룸스케일로 걸어서 벗어날 때 처리, 뿌리치기 속도·최대 지속 시간 | grip_pose M1(`FItemData` 형상 유형) 선행 |

### VR 자세·메뉴 (2026-10-04 신규)

| 이름 | 진행상황 | 미결사항 | 비고 |
|---|---|---|---|
| [body_measure_prone](SPEC_body_measure_prone.md) | ⬜ M0 스파이크 → M1 눕히기 → M2 측정 | 다리 측정 절차(컨트롤러를 어느 지점에), 측정값 저장 위치·형식, 아바타 스케일 복원 여부, 기존 2초 자동 캘리브레이션과의 관계, 엎드린 캡슐 형태 | 현재 Prone 은 태그·캡슐만이고 몸 메시는 직립(W21 부터). 측정 UI 는 pause_settings 선행 |
| [pause_settings](SPEC_pause_settings.md) | 🔶 M0 ✅ M1 ✅ M2 ✅ 구현(헤드셋 없는 PIE 확인, 헤드셋 대기 DoList 2-8) · M3 정보 화면(지도·파티 목업·퀘스트) ✅ 구현(헤드셋 대기) | Menu 버튼 시스템 충돌, 메뉴↔인벤토리 겹침 우선순위, 실제 소리 변화(서브믹스), 핸드트래킹 대체 입력 | 브랜치 `feature/pause-settings`. 신체 측정 버튼은 자리만(`body_measure_prone` M2 가 연결) |

### NPC·Jevlike (Python/C++)

| 이름 | 진행상황 | 미결사항 | 비고 |
|---|---|---|---|
| [jev_daily](SPEC_jev_daily.md) | 🔶 M1 ✅(2026-09-24) M2 학습 ✅ | 골드셋 50건 사람 채점·일치율, 대화 중 판정 보강 여부, 튜닝값(반경·풀 상한·대기 시간), 멀리 있는 NPC 에게 GiveItem 오지정 여지 | 골드셋이 look_at·stay 편중. 실제 POI 시스템은 별도 SPEC |
| [jev_neuro_symbolic_st](SPEC_jev_neuro_symbolic_st.md) | 🔶 Phase 1~3 코드 ✅(2026-09-22) | `BREAKTHROUGH_GAIN` 헤드셋 체감 튜닝, 전투 라벨 재생성·재학습(선택) | StateTree 에셋 바인딩은 동작 변화 0 이라 보류 |
| [poi](SPEC_poi.md) | ⬜ M1 액터·등록소 → M2 `target_poi` 이동 → M3 대사·RAG → M4 스토리 구역(인터뷰 2026-10-05) | 노출 상한 N·별칭 길이, M3 "근처" 반경, `Type` 어휘(M1 미사용), SLM id 오류 잦으면 구제 재검토 | 해석은 C++ `UPOIManager`(Python 은 어휘 검증만). jev_daily `pois` 풀 계약 유지 |
| [party](SPEC_party.md) | ⬜ M1 서브시스템·액션·Envelope → M2 따라다니기·전투 후 복구 → M3 호감도 게이트(인터뷰 2026-10-06) | 최대 인원, 호감도 임계, 일행끼리 피격 누적 처리, 이탈 시 따라붙기, 스토리 영입 비트 연동 | 멤버십만(대형·전투 연계·UI 는 범위 밖). `Follow` 지속 추적(`2e236262`) 위에 올림 |
| [python_cpp_contract](SPEC_python_cpp_contract.md) | ❓ 코드 대조 리뷰(2026-09-18)만 있음 | 판정표의 "구현 가치 있음" 항목(예: `stats` 송신)이 구현됐는지 | 상태 줄 없음. 본문 판정표 기준으로 정리 필요 |

---

## 완료 (`docs/done/`)

| 이름 | 진행상황 | 미결사항 | 비고 |
|---|---|---|---|
| [vr_ghost_hand](done/SPEC_vr_ghost_hand.md) | ✅ M1~M3 (2026-09-30~10-02, 헤드셋 확인) | 드라이브·임계 튜닝은 실사용으로 해소 | 팔 통과는 SPEC 밖. 이후 손 컴포넌트 분리·드라이브 통합 |
| [vr_grip_pose](done/SPEC_vr_grip_pose.md) | ✅ M0~M4 (2026-09-30, 헤드셋 확인) | 가구 등 인벤토리 밖 물체 쥐기는 범위 밖 | 마찰·양손은 friction_grip 이 이어감 |
| [npc_bone_collision](done/SPEC_npc_bone_collision.md) | ✅ M1~M3 (2026-10-02, 헤드셋 확인) | Imp·Puglin 무기 손·Rogue 발·Warrior 갑옷 뼈 값(수동 조정) | 밀기는 접촉 기준으로 교체됨 |
| [item_collision_gen](done/SPEC_item_collision_gen.md) | ✅ 2026-10-03 | 없음 | 오목 아이템 CoACD 는 memo 백로그 |
| [story_director](done/SPEC_story_director.md) | ✅ 2026-09-21 | 없음 | |
| [story_progression_roadmap](done/SPEC_story_progression_roadmap.md) | ✅ 2026-09-18 확정 | 없음 | 로드맵 문서 |
| [crewai_multi_agent](done/SPEC_crewai_multi_agent.md) | ✅ 2026-09-27 | 없음 | 산출물 `tools/federated_orchestrator.py` |
| [behavior_mode_reduce](done/SPEC_behavior_mode_reduce.md) | ✅ 2026-09-24 | 에셋 삭제(D4) | 사용자 몫으로 남음 |
| [realistic_combat](done/SPEC_realistic_combat.md) | ✅ 2026-09 | 문서 내 구현 현황 표 참조 | |
| [player_survival](done/SPEC_player_survival.md) | ✅ 2026-09-04 | 없음 | 비네트(M2)는 철회 |
| [player_systems](done/SPEC_player_systems.md) | ✅ 2026-08-24 | 없음 | |
| [vr_ui_systems](done/SPEC_vr_ui_systems.md) | ✅ 2026-09-05 | PIE 검증 대기(DoList) | |
| [item_registry](done/SPEC_item_registry.md) | ✅ 2026-08-29 | 없음 | 72종, 이후 외부 에셋으로 메시 교체 |
| [item_pipeline](done/SPEC_item_pipeline.md) | ✅ 2026-08-31 | PIE 검증 대기였음 | |
| [source_layout](done/SPEC_source_layout.md) | ✅ 2026-09-04 | 없음 | |
| [reflex_table](done/SPEC_reflex_table.md) | ✅ 구현 완료 | PIE 검증 5항목(memo) | |
| [llm_perf](done/SPEC_llm_perf.md) | ✅ 구현 완료(`bdb94c48`·`38268c72`, PR #24) | 없음 | 12B 선제 웜업 + 요약 idle 디퍼. 원본 시도 `77a53d2` 는 폐기·태그 `archive/llm-perf` 보존 |
| [refactor_encapsulation](done/SPEC_refactor_encapsulation.md) | ✅ 2026-09 | 없음 | 실행 계획의 단일 소스(§6) |
| [refactor_readability](done/SPEC_refactor_readability.md) | ✅ 대체됨 | 없음 | encapsulation §6 으로 대체, 근거 기록용 |

SPEC 이 아닌 검증·가이드 문서(`done/VERIFICATION_villager_phase2·3.md`, `done/finetune_data_guidelines.md`)는 위 표에서 뺐다.
