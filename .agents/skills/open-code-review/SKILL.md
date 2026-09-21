---
name: open-code-review
description: >-
  Use this skill to perform AI-powered, high-precision code reviews using Alibaba's OpenCodeReview CLI ('ocr').
  Reads Git diffs or entire files to detect null pointers, concurrency issues, memory leaks, and architectural
  violations with only 1/9 token consumption compared to general-purpose agents. Ideal for QA and pre-commit checks.
---

# OpenCodeReview (Alibaba OCR) Skill Guide

알리바바 그룹의 공식 AI 코드 리뷰 CLI 도구인 **OpenCodeReview (`ocr`)**를 활용하여, 소스 코드 변경 사항(Git Diff)이나 특정 파일을 고정밀·초저비용(일반 에이전트 대비 1/9 토큰)으로 자동 리뷰하는 스킬입니다.

---

## 1. 개요 및 주요 특징

- **1/9 토큰 다이어트**: 결정론적 정적 분석(NPE, 메모리, 스레드 안전성) + LLM 도구 호출 결합으로 일반 코딩 에이전트 대비 88% 토큰 절감.
- **위치 드리프트 방지**: 엉뚱한 라인이 아닌 실제 결함이 발생한 정확한 코드 라인에 정밀 코멘트 부착.
- **다양한 백엔드 지원**: Anthropic, OpenAI뿐 아니라 로컬 **Ollama (`http://localhost:11434/v1`)** 지원으로 외부 API 할당량 0 소모 구동 가능.

---

## 2. 기본 사용법 (Common Workflows)

### 2.1 현재 워크스페이스 변경점(Uncommitted Diff) 리뷰
가장 일반적으로 사용하는 모드로, Staged + Unstaged + Untracked 변경점을 즉시 검토합니다.

```powershell
# 에이전트 모드 (불필요한 프로그레스 바 없이 요약만 출력)
ocr review --audience agent

# JSON 포맷으로 정형화된 결과 추출
ocr review --audience agent --format json
```

### 2.2 기획서/배경 지식을 동봉한 문맥 인식 리뷰
특정 기능 구현 요구사항이나 아키텍처 규칙 문서를 주입하여 "기획 의도에 맞게 코딩되었는지" 검증합니다.

```powershell
ocr review --background-file docs/SPEC_crewai_multi_agent.md --audience agent
```

### 2.3 특정 커밋 또는 브랜치 비교 리뷰
```powershell
# 특정 커밋 리뷰
ocr review --commit <commit-hash> --audience agent

# 브랜치 간 차이 비교 (e.g. main vs feature branch)
ocr review --from main --to refactor/dead-code --audience agent
```

### 2.4 Diff 없는 특정 파일/디렉토리 전체 정적 감사 (Scan)
새로 추가되었거나 기존에 존재하는 파일 전체를 전수 스캔할 때 사용합니다.

```powershell
ocr scan Source/UE5_MCP_VR/NPC/Components/NPCRagdollComponent.cpp
```

---

## 3. LLM 프로바이더 설정 가이드 (`ocr config`)

### 3.1 로컬 Ollama 백엔드 설정 (추천: 외부 할당량 0% 소모)
로컬에 구동 중인 Ollama의 코딩 특화 모델(`qwen2.5-coder`)을 바인딩하여 완전 무료로 무제한 코드 리뷰를 실행합니다.

```powershell
ocr config set provider ollama
ocr config set custom_providers.ollama.url http://localhost:11434/v1
ocr config set custom_providers.ollama.protocol openai
ocr config set model qwen2.5-coder:14b
```

### 3.2 Anthropic / OpenAI 클라우드 설정
```powershell
# Anthropic Claude 설정
ocr config set provider anthropic
ocr config set model claude-3-5-sonnet-20241022
ocr config set providers.anthropic.api_key "sk-ant-..."

# OpenAI 설정
ocr config set provider openai
ocr config set model gpt-4o-mini
ocr config set providers.openai.api_key "sk-..."
```

---

## 4. 게임 엔진(UE5_MCP_VR) 프로젝트 연동 모범 사례

본 프로젝트에서는 **Codex QA / SoL-Pi 하네스와 결합한 2단계 품질 보증 게이트**로 운영합니다:

1. **1단계: 논리 및 정적 결함 검출 (`ocr review`)**:
   - C++ Null 포인터 체크 누락 (`nullptr` 체크 없이 역참조)
   - Unreal C++ Blackboard 직접 쓰기 위반 (`NPCActionComponent`에서 BB 접근)
   - JSON 스키마 필드 누락 (`NPCActionKeys` 미사용)
2. **2단계: 물리적 컴파일 및 UAT 검증 (`python tools/sol_pi.py verify all`)**:
   - C++ UBT 증분 빌드 (0 error)
   - Python 51개 pytest 회귀 검증
   - UE5 헤드리스 UAT 엔진 테스트

