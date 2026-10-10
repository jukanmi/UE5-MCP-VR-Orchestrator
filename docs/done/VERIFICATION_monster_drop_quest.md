# 몬스터 전리품 드랍 및 수집 서브퀘스트 완료 근거 및 검증 기준서 (Verification Plan)

> **문서 목적**: 몬스터 사망 시 전리품 드랍 및 수집 서브퀘스트(`s_hunt_forest_raiders`) 구현에 대한 합격 판정 기준(Acceptance Criteria)과 검증 절차 정의.  
> **책임 에이전트**: Gemini Master (총괄 & 검증관) / 구현: Claude Code CLI / 검증 도구: SoL-Pi(`sol_pi.py build`) + 라이브 PIE(MCP `ue5`)

---

## 1. 완료 판정 근거 (Acceptance Criteria)

| ID | 검증 항목 | 합격 판정 기준 (Acceptance Criteria) | 검증 대상 파일 |
| :--- | :--- | :--- | :--- |
| **AC-1** | **전리품 레지스트리** | `DT_ItemRegistry`에 기존 3D 메시를 활용한 퀘스트 전리품(`BanditInsignia`)이 등록되어 있고, 메시 경로(`/Game/Core/Mesh/Items/PassDoc`)가 유효한가? | `ItemRegistry.csv` |
| **AC-2** | **몬스터 사망 드랍** | `AEnemyCharacter`에 `DropItemID`, `DropChance`, `DropAmount`가 구현되어, `HandleDeath()` 호출 시 발밑에 `ADroppedItemBase` 실물 액터가 스폰되고 `ItemManager`에 정상 등록되는가? | `EnemyCharacter.{h,cpp}` |
| **AC-3** | **에너미 BP 매핑** | `tools/make_enemy_bps.py`로 `BP_Enemy_Bandit`에 `DropItemID = "BanditInsignia"`가 주입되는가? | `tools/make_enemy_bps.py` |
| **AC-4** | **서브퀘스트 정의** | `OmniAgent_VR_System/CognitiveEngine/app/story/content/side/s_hunt_forest_raiders.yaml`이 생성되고, `complete_when: {type: flag, name: "item_acquired:BanditInsignia"}` 조건이 올바른가? `main.yaml`에 해금 등록되어 있는가? | `side/s_hunt_forest_raiders.yaml`, `main.yaml` |
| **AC-5** | **라이브 PIE 실측** | 라이브 에디터에서 도적 처치 시 바닥에 실물이 드랍되고, VRPawn 픽업 시 `item_acquired` 이벤트로 서브퀘스트가 `done`으로 완료 처리되는가? | 에디터 라이브 PIE 스크린샷 및 서버 로그 |

---

## 2. 자동화 검증 절차

1. **SoL-Pi C++ 빌드 검증**: `python tools/sol_pi.py build` (에러 0건)
2. **에너미 BP 주입**: `tools/make_enemy_bps.py` 실행
3. **라이브 PIE 실측 검증 (MCP `ue5`)**:
   - 도적 스폰 $\rightarrow$ `apply_damage` 처치 $\rightarrow$ 전리품 스폰 좌표 확인 $\rightarrow$ VRPawn 픽업 $\rightarrow$ `item_acquired` 송신 확인 $\rightarrow$ 뷰포트 촬영

