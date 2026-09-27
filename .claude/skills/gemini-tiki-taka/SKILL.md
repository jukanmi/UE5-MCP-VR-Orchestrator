---
name: gemini-tiki-taka
description: PR Gemini 리뷰 티키타카 루프 — /gemini review 트리거·👀 확인·리뷰 수집·수정·재커밋·라운드 운영
---

## Gemini Tiki-Taka

PR 에서 Gemini(`gemini-code-assist[bot]`)와 자동 수렴 루프를 돈다.
한 라운드 = **수정 → 재커밋 → push → `/gemini review` 코멘트 → 👀 확인 → 새 리뷰 대기 → 다음 라운드**.

인자: `<PR번호> [최대라운드=3] [base브랜치=Develop]`. 미지정 시 현재 브랜치의 열린 PR 자동 탐색.

---

### 라운드 절차

각 라운드:

1. **새 리뷰 수집** — 직전 fix 커밋 이후 도착한 Gemini 리뷰/인라인코멘트만 수집(이전 라운드 지적 중복 제외):
   ```powershell
   $rv = gh api repos/<owner>/<repo>/pulls/<PR>/reviews | ConvertFrom-Json
   $latest = $rv | Sort-Object submitted_at | Select-Object -Last 1
   $latest.body
   gh api repos/<owner>/<repo>/pulls/<PR>/reviews/$($latest.id)/comments | ConvertFrom-Json |
     ForEach-Object { "--- $($_.path):$($_.line) ---"; $_.body }
   ```
   리뷰 타임존: `submitted_at`/`created_at` 은 UTC(Z). 로컬 KST=UTC+9 — 필터 시 변환.

2. **지적 분류** — 각 지적을 판정:
   - **적용**: 최근 변경(이번 PR diff) 범위 + high/medium + 유효. `git diff origin/<base>...HEAD` 로 해당 라인이 내 변경인지 확인.
   - **스킵**: 기존 코드(diff 밖)·오탐·사소. 스킵 사유 기록.
   - **반론**: Gemini 제안이 의도적 설계(이전 라운드 결정)와 충돌하면 적용하지 말고 주석으로 의도 명시. 설계 결정 필요(예: 동기/비동기, 순차/병렬)는 동작 변경 대신 사용자에게 보고.

3. **수정** — 적용 지적만 Edit. C++ 포함 시 빌드는 사용자 몫(리빌드 언급 안 함).

4. **검증** — Python 은 `python -m py_compile <files>`. 통과 후 진행.

5. **커밋·push** — 한 라운드 = 한 커밋. 메시지: `fix: Gemini N라운드 — <요약>`. Co-Authored-By 줄 금지. push:
   ```powershell
   git add <files>; git commit -m "fix: Gemini N라운드 — ..."; if ($?) { git push origin <branch> }
   ```

6. **`/gemini review` 트리거** — **반드시 PowerShell**로 전송:
   ```powershell
   gh pr comment <PR> --repo <owner>/<repo> --body '/gemini review'
   ```
   > Bash(git-bash)는 슬래시를 `C:/Program Files/Git/...` 로 경로변환해 본문이 깨짐 → 봇 트리거 실패. 절대 Bash 로 보내지 말 것.

7. **본문·👀 검증** — 코멘트 id 로:
   ```powershell
   $c = gh api repos/<owner>/<repo>/issues/comments/<id> | ConvertFrom-Json; "BODY=[$($c.body)]"
   gh api repos/<owner>/<repo>/issues/comments/<id>/reactions | ConvertFrom-Json |
     ForEach-Object { "$($_.content) by $($_.user.login)" }
   ```
   - 본문이 정확히 `/gemini review` 인지 확인. 깨졌으면 `gh api -X DELETE .../issues/comments/<id>` 후 재전송.
   - 👀(eyes) reaction 은 **지연 50초+** 흔함. 본문 정확하면 봇 latency — 성급히 "잘못된 코멘트" 판정 말 것.

8. **새 리뷰 대기** — `ScheduleWakeup` 270초(캐시 유지)로 폴링. 도착하면 1번으로.

---

### 종료 조건

- 최대 라운드 도달.
- 새 리뷰가 변경 요청 0건(인라인 코멘트 없음·approve·nit only).
- 남은 지적이 전부 사소·오탐·범위 밖.

종료 시: 라운드별 적용/스킵 요약 + 미결정 설계 사항 보고.

---

### 주의

- PowerShell 5.1 은 `--jq` 식 안의 큰따옴표를 먹음 → `gh api | ConvertFrom-Json` 객체 파싱 사용.
- C++ 수정 후 "리빌드하라" 말하지 않음(사용자가 항상 함).
- 2026-06 기준 consumer Gemini Code Assist 가 sunset 예정(2026-07-17 리뷰 중단) — 추후 봇 트리거 동작 변동 가능성.
