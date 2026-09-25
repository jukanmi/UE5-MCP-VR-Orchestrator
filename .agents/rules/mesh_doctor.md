---
trigger: model_decision
description: 3D 에셋(GLB/OBJ) 임포트, 지오메트리·콜리전 결함 점검 시
---

# Mesh Doctor (3D Asset Integrity & VR Optimization) Rule

1. **실행 및 호출 경계 (Execution Guard)**:
   - **언제 실행하는가**: `Art/Meshes/`에 3D 에셋(GLB/OBJ)을 새로 추가했거나, 물리 폭발/지터링/렌더링 결함 보고가 있을 때, 또는 사용자가 명시적으로 점검을 요청했을 때만 호출한다.
   - **언제 생략하는가**: 일반 C++ 및 Python 코드 작업 중에는 호출하지 않는다.

2. **온디맨드 진단 및 치료 (On-Demand Diagnosis & Healing)**:
   - 3D 메시 결함(파편, 면 뒤집힘, 과도한 폴리곤, 콜리전 누락) 점검 시 백그라운드 워처 대신 `python tools/mesh_doctor.py`를 직접 호출한다. 단일 파일 또는 폴더 단위로 슬라이싱된 결함 보고서를 확인한다:
     - `python tools/mesh_doctor.py diagnose <경로>`: 지오메트리 결함 핀포인트 진단.
     - `python tools/mesh_doctor.py heal <경로>`: 텍스처/PBR 손실 없는 자동 결함 치유.

3. **VR 퍼포먼스 예산 준수**:
   - 단일 소형 소품(아이템/프랍)은 트라이앵글 10,000개 이하를 권장하며, 25,000개 초과 시 반드시 Nanite 적용 또는 디시메이션(`--max-faces`)을 검토한다.
   - VR 물리 상호작용(그립/투척)에 쓰이는 메시는 단순 콜리전(Simple Collision)이 필수이다 (`python tools/mesh_doctor.py ue-audit`).
