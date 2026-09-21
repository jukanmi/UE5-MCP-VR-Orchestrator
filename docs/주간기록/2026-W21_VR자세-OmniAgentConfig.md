# W21 (2026-05-19 ~ 05-25) — VR 자세 시스템 Phase 1-3·OmniAgentConfig·TTS OpenVoice

## 핵심
VR 자세 시스템(Standing/Crouching/Prone) C++ Phase 1-3 구현 — HMD 높이 캘리브레이션·슈미트 트리거 히스테리시스·동적 캡슐+Rising Floor 역보정. 네트워크/TTS 레이어를 `OmniAgentConfig`로 정리해 서버 주소 하드코딩을 `DefaultGame.ini [OmniAgent]`로 이전. TTS M2는 OpenVoice v2(MeloTTS base + tone color converter)로 확정해 NPC별 음색 합성 완료. 메시 폴더·VR 입력 에셋 구조 개편.

## 주요 작업

### VR 자세 시스템 (C++ Phase 1-3)
- **VR 자세 시스템 Phase 1-3** (`ee821d1`)
  - PlayerGameplayTags Posture 태그 3종, `EVRPosture`·`FOnVRPostureChanged`
  - 캘리브레이션(2초 샘플링) → 기준 키 산출
  - 슈미트 트리거 히스테리시스(자세 전이 떨림 방지)
  - 동적 캡슐 HalfHeight + Rising Floor 역보정, VInterp 스무딩, 자세별 이동속도

### 네트워크·TTS 레이어
- **OmniAgentConfig 추가 — Network·TTS 레이어 개선** (`95048c8`)
  - 서버 주소 `DefaultGame.ini [OmniAgent]`에서 읽기(하드코딩 제거 기반 마련)
  - EnvelopeBuilder·WebSocketClient·NPCAudioStreamComponent 정리
- **설정·TTS 서버·Memo 업데이트** (`c378d4c`) — DefaultGame.ini·명세.md 삭제
- **TTS M2 OpenVoice v2** (Memo Done 05-19) — MeloTTS base + tone color converter, voice_map.yaml NPC별 음색

### 에셋·정리
- **메시 폴더 구조 개편** (`de4c269`) — X_Bot→`Content/Core/Mesh/NPC/`, 손 메시→`Player/`
- **VR 입력 에셋 정리** (`d424cdb`) — IA_ToggleWristUI·IA_TriggerRight 삭제, IMC_VR·BP_VRPawn 수정
- **애니메이션·BP 에셋 일괄 재저장** (`1530201`)
- **미사용 코드 제거** (`6f66bb4`) — ChatWidget·IsTrackingTarget·AddStatusEffect·CurrentTargetID·get_all_constants
- **.gitignore 정리** (`238d92c`) — .claude/ 중복·TTS 모델 바이너리 패턴

## 메모 (Memo.md Handoff Notes, 2026-05-19~25)
- **VR 자세 Phase 4-5 에디터 작업 잔여**: C++(Phase 1-3)은 끝났고 BP_VRPawn 메시 부착·ABP_VRPawn(BlueprintThreadSafeUpdate, Fast Path)·CR_VRPawn_FBIK(Full Body IK, Head/양손 effector, 척추 제외) 는 에디터 수작업.
- **PC/VR 입력 폴더 분리**: `Content/Core/Input/Computer/` vs `VR/`. 파일명 충돌 시 IMC가 잘못된 IA 참조 위험 → 두 Pawn BP 컴파일 후 IA 레퍼런스 누락 경고 확인 필수.
- **TTS M2 OpenVoice 결정**: VibeVoice(비호환)→Piper(한국어 미지원)→XTTS 거쳐 최종 OpenVoice v2 채택. emotion→voice ref/speed 매핑은 voice_map.yaml `npcs.*.emotions`.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 05-20 | `651149e` | Merge Develop into feature/MetaQuest3S-VR |
| 05-20 | `238d92c` | .gitignore 정리 |
| 05-23 | `6f66bb4` | 미사용 코드 제거 |
| 05-25 | `ee821d1` | VR 자세 시스템 Phase 1-3 |
| 05-25 | `de4c269` | 메시 폴더 구조 개편 |
| 05-25 | `d424cdb` | VR 입력 에셋 정리 |
| 05-25 | `1530201` | 애니메이션·BP 에셋 일괄 재저장 |
| 05-25 | `95048c8` | OmniAgentConfig 추가 |
| 05-25 | `c378d4c` | 설정·TTS 서버·Memo 업데이트 |
| 05-25 | `23daf38` | Memo.md Phase 1-3 Done 기록 |
| 05-25 | `47eb980` | 명세.md 삭제 |
