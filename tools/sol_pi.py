"""SoL-Pi (Speed-of-Light Pipeline) Harness for UE5_MCP_VR
NVIDIA NVLabs SoL-Pi 기반 에이전트 최적화 하네스 도구.
1. build  : C++ 빌드 로그 슬라이싱 (컴파일 에러 핀포인트 추출)
2. test   : 단위 테스트 자동화 실행 및 결과 슬라이싱 (UAT Engine / Python)
3. verify : 액션 퓨전(Action Fusion) — 빌드 + 테스트를 단일 도구 호출로 묶어 검증
4. log    : 런타임 로그 슬라이싱 (Saved/Logs/UE5_MCP_VR.log)
5. status : 프로젝트 및 엔진 상태 요약
"""

import sys
import subprocess
import re
from pathlib import Path
from typing import Tuple

# Windows 콘솔 UTF-8 출력 강제
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass  # nosec B110 — 콘솔이 reconfigure 미지원이면 기본 인코딩 그대로 진행

PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOG_FILE = PROJECT_ROOT / "Saved" / "Logs" / "UE5_MCP_VR.log"
RAW_LOG_FILE = PROJECT_ROOT / "Saved" / "Logs" / "sol_pi_raw.log"
BUILD_BAT = Path(r"C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat")
UNREAL_CMD = Path(r"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe")
UPROJECT = PROJECT_ROOT / "UE5_MCP_VR.uproject"
COGNITIVE_DIR = PROJECT_ROOT / "OmniAgent_VR_System" / "CognitiveEngine"

# 무시할 무해한 엔진 기본 플러그인 경고 패턴
IGNORE_WARNING_PATTERNS = [
    r"PlasticSourceControl.*Icon",
    r"VisionOS.*Platform_VisionOS",
    r"Interchange.*alertSolid\.png",
]


def is_editor_running() -> bool:
    """UnrealEditor 프로세스가 실행 중인지 확인"""
    try:
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", "Get-Process UnrealEditor -ErrorAction SilentlyContinue"],
            text=True,
            errors="replace",
        )
        return "UnrealEditor" in out
    except Exception:
        return False


def archive_raw_log(raw_text: str):
    """ObservationPack: 전체 원시 로그는 디스크에 아카이빙하여 컨텍스트 낭비 방지"""
    try:
        RAW_LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        RAW_LOG_FILE.write_text(raw_text, encoding="utf-8", errors="replace")
    except Exception:
        pass  # nosec B110 — 아카이빙은 부가 기능, 실패해도 빌드 검증 자체는 계속


def slice_build(timeout_sec: int = 300) -> int:
    """UE5 C++ 컴파일을 실행하고 위상(Phase) 인식 및 샌드위치 기법으로 에러 슬라이싱"""
    if not BUILD_BAT.exists():
        print(f"[SoL-Pi] Build.bat 경로가 올바르지 않습니다: {BUILD_BAT}")
        return 1

    if is_editor_running():
        # Build.bat 은 Live Coding 활성 상태에서 "Unable to build while Live Coding is active" 로 무조건 실패한다.
        # 함수 본문만 바뀐 경우는 Ctrl+Alt+F11, 클래스·UPROPERTY·UFUNCTION 증감(레이아웃 변경)은 에디터를 닫아야 한다.
        print(
            "⛔ [SoL-Pi] UnrealEditor 실행 중 — 빌드 불가. 본문만 수정했으면 에디터에서 Ctrl+Alt+F11(Live Coding), 헤더 레이아웃이 바뀌었으면 에디터를 닫고 다시 실행하세요."
        )
        return 1

    cmd = [str(BUILD_BAT), "UE5_MCP_VREditor", "Win64", "Development", f"-Project={UPROJECT}", "-WaitMutex"]
    print("[SoL-Pi] 🚀 C++ 빌드 검증 시작 (위상 인식 & Watchdog 활성화)...")

    try:
        res = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout_sec
        )
        archive_raw_log(res.stdout + "\n" + res.stderr)
    except subprocess.TimeoutExpired:
        print(f"⏰ [SoL-Pi Watchdog] C++ 빌드 제한 시간({timeout_sec}초) 초과로 강제 중단되었습니다!")
        # 프로세스 정리
        # UBT.exe 는 없다 — dotnet 호스트의 UnrealBuildTool.exe 와 컴파일러 cl.exe 를 끊는다.
        subprocess.run(["taskkill", "/F", "/IM", "UnrealBuildTool.exe", "/T"], capture_output=True)
        subprocess.run(["taskkill", "/F", "/IM", "cl.exe", "/T"], capture_output=True)
        return 1

    if res.returncode == 0:
        print("✅ [SoL-Pi] BUILD SUCCESS — 컴파일 오류 0건! (완벽)")
        return 0

    lines = res.stdout.splitlines() + res.stderr.splitlines()

    # 위상별 에러 정규식
    uht_pattern = re.compile(
        r"(\[UHT\]|UnrealHeaderTool|\.h\(\d+\)\s*:\s*error\s*:|Error:\s+.*(?:UPROPERTY|UFUNCTION|UCLASS|USTRUCT|GENERATED_BODY)|Missing '\w+' in|Found '\w+' but expected)",
        re.IGNORECASE,
    )
    lnk_pattern = re.compile(r"error LNK\d+", re.IGNORECASE)
    msvc_pattern = re.compile(r"(\: (error|fatal error) C\d+|\: error\:)", re.IGNORECASE)
    gen_err_pattern = re.compile(
        r"(\: (error|fatal error) [A-Z0-9]+|\: error\:|\[UHT\] Error|\: Error\:)", re.IGNORECASE
    )

    uht_slices = []
    lnk_slices = []
    msvc_slices = []
    other_slices = []

    for i, line in enumerate(lines):
        if gen_err_pattern.search(line):
            start = max(0, i - 1)
            end = min(len(lines), i + 3)
            chunk = "\n".join(lines[start:end])

            if uht_pattern.search(line):
                uht_slices.append(chunk)
            elif lnk_pattern.search(line):
                lnk_slices.append(chunk)
            elif msvc_pattern.search(line):
                msvc_slices.append(chunk)
            else:
                other_slices.append(chunk)

    total_errors = len(uht_slices) + len(lnk_slices) + len(msvc_slices) + len(other_slices)
    print(f"❌ [SoL-Pi] BUILD FAILED — 감지된 오류 총 {total_errors}건")

    # [위상 1] UHT 리플렉션 오류 우선 격리
    if uht_slices:
        print("\n" + "=" * 55)
        print("🔍 [위상 감지: 1단계 UHT(Unreal Header Tool) 리플렉션 오류]")
        print("⚠️ UHT 오류는 후속 컴파일러 에러를 대량 유발하므로 근원지 UHT 에러만 우선 노출합니다.")
        print("=" * 55 + "\n")
        print("\n\n---\n\n".join(uht_slices[:5]))
        if len(uht_slices) > 5:
            print(f"\n... (추가 UHT 오류 {len(uht_slices) - 5}건 생략: Saved/Logs/sol_pi_raw.log 참조)")
        if msvc_slices:
            print(f"\n💡 [정보] UHT 오류로 인한 종속 MSVC 컴파일 에러 {len(msvc_slices)}건은 마스킹되었습니다.")
        print("\n" + "=" * 55)
        return 1

    # [위상 3] 링커 오류
    if lnk_slices and not msvc_slices:
        print("\n" + "=" * 55)
        print("🔍 [위상 감지: 3단계 Linker(링킹) 오류]")
        print("=" * 55 + "\n")
        print("\n\n---\n\n".join(lnk_slices[:5]))
        print("\n" + "=" * 55)
        return 1

    # [위상 2] 네이티브 MSVC 컴파일 오류 (샌드위치 기법 적용)
    active_slices = msvc_slices if msvc_slices else (other_slices if other_slices else [])
    if active_slices:
        print("\n" + "=" * 55)
        print("🔍 [위상 감지: 2단계 MSVC C++ 네이티브 컴파일 오류 (샌드위치 슬라이싱)]")
        print("=" * 55 + "\n")

        # 샌드위치 기법: 첫 5개 (원인 발생지) + 마지막 1개 (최종 실패 상태)
        if len(active_slices) > 6:
            head_part = "\n\n---\n\n".join(active_slices[:5])
            omitted_count = len(active_slices) - 6
            tail_part = active_slices[-1]
            print(head_part)
            print(f"\n\n... ✂️ [중간 연쇄 에러 {omitted_count}건 절단 — 원본: Saved/Logs/sol_pi_raw.log] ✂️ ...\n")
            print(tail_part)
        else:
            print("\n\n---\n\n".join(active_slices))
        print("\n" + "=" * 55)
    else:
        print("\n" + "=" * 50)
        print("\n".join(lines[-15:]))
        print("=" * 50)
    return 1


def run_python_tests(timeout_sec: int = 60) -> Tuple[bool, str]:
    """Python 인지 엔진 단위 테스트(pytest) 실행 및 결과 슬라이싱 (Watchdog 적용)"""
    if not COGNITIVE_DIR.exists():
        return True, "Python 인지 엔진 디렉토리 없음 (스킵)"

    cmd = ["uv", "run", "pytest", "tests", "-q", "-W", "ignore"]
    try:
        res = subprocess.run(
            cmd,
            cwd=str(COGNITIVE_DIR),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_sec,
        )
        archive_raw_log(res.stdout + "\n" + res.stderr)

        output = res.stdout + res.stderr
        passed_match = re.search(r"(\d+ passed)", output)
        failed_match = re.search(r"(\d+ failed)", output)
        error_match = re.search(r"(\d+ error)", output)

        if res.returncode == 0:
            summary = passed_match.group(1) if passed_match else "모든 테스트 통과"
            return True, f"✅ Python Tests: {summary} (100%)"
        else:
            fail_info = []
            if failed_match:
                fail_info.append(failed_match.group(1))
            if error_match:
                fail_info.append(error_match.group(1))
            summary_str = ", ".join(fail_info) if fail_info else "테스트 실패"

            fail_lines = [l for l in output.splitlines() if l.startswith("FAILED") or "AssertionError" in l][:3]
            details = " | ".join(fail_lines) if fail_lines else "상세 로그 참조"
            return False, f"❌ Python Tests FAILED ({summary_str}) — {details}"
    except subprocess.TimeoutExpired:
        return False, f"⏰ Python Tests 제한 시간({timeout_sec}초) 초과 (Watchdog 중단)"
    except Exception as ex:
        return False, f"❌ Python Tests 실행 오류: {str(ex)}"


def run_engine_tests(test_filter: str = "Project", timeout_sec: int = 120) -> Tuple[bool, str]:
    """언리얼 엔진 UAT 자동화 테스트(UnrealEditor-Cmd) 실행 (Watchdog 120초 적용)"""
    if not UNREAL_CMD.exists():
        return False, f"UnrealEditor-Cmd.exe 경로 없음: {UNREAL_CMD}"

    if is_editor_running():
        return True, "⚠️ UnrealEditor 실행 중으로 엔진 UAT 건너뜀 (Live Coding 권장)"

    cmd = [
        str(UNREAL_CMD),
        str(UPROJECT),
        f"-ExecCmds=Automation RunTests {test_filter}; Quit",
        "-nullrhi",
        "-nosound",
        "-unattended",
        "-testexit=Automation Test Queue Empty",
        "-stdout",
    ]

    try:
        res = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout_sec
        )
        archive_raw_log(res.stdout + "\n" + res.stderr)

        output = res.stdout + res.stderr
        if "**** TEST COMPLETE. EXIT CODE: 0 ****" in output or res.returncode == 0:
            failures = [l for l in output.splitlines() if "Error: Automation" in l or "Failed to find" in l]
            if not failures:
                return True, "✅ Engine UAT: Clean (0 errors / Passed)"
            else:
                return False, f"❌ Engine UAT 실패: {failures[0]}"
        else:
            errors = [l for l in output.splitlines() if "Error:" in l][:2]
            err_str = " | ".join(errors) if errors else f"Exit Code {res.returncode}"
            return False, f"❌ Engine UAT FAILED: {err_str}"
    except subprocess.TimeoutExpired:
        # 무한 루프 또는 행(Hang)에 걸린 UnrealEditor-Cmd 강제 종료
        subprocess.run(["taskkill", "/F", "/IM", "UnrealEditor-Cmd.exe", "/T"], capture_output=True)
        return (
            False,
            f"⏰ [Watchdog] Engine UAT 실행 시간({timeout_sec}초) 초과로 프로세스 강제 종료 (무한 루프/데드락 방지)",
        )
    except Exception as ex:
        return False, f"❌ Engine UAT 실행 오류: {str(ex)}"


def run_tests(target: str = "all") -> int:
    """단위 테스트 단독 실행"""
    print(f"\n🧪 [SoL-Pi] 단위 테스트 시작 (대상: {target})...")
    print("=" * 50)

    all_success = True
    if target in ["all", "python", "py"]:
        success, msg = run_python_tests()
        print(f" {msg}")
        if not success:
            all_success = False

    if target in ["all", "engine", "uat"]:
        success, msg = run_engine_tests()
        print(f" {msg}")
        if not success:
            all_success = False

    print("=" * 50)
    print("📄 원시 로그 보관: Saved/Logs/sol_pi_raw.log (ObservationPack)")
    return 0 if all_success else 1


def run_action_fusion(target: str = "all") -> int:
    """NVIDIA SoL-Pi 액션 퓨전(Action Fusion): 컴파일 + 테스트 단일 관측 융합"""
    print("\n⚡ [SoL-Pi Action Fusion] 코드 검증 및 자동화 테스트 융합 실행...")
    print("=" * 55)

    # 1단계: C++ 빌드 (Fast-Fail). python 단독 대상이면 생략 — C++ 를 안 건드렸는데 빌드하지 않는다(AGENTS.md 실사 가드).
    build_code = 0 if target in ["python", "py"] else slice_build()
    if build_code != 0:
        print("\n⛔ [Action Fusion 중단] 컴파일 오류로 인해 후속 테스트를 중단합니다.")
        return 1

    # 2단계: 단위 테스트 일괄 실행
    print("\n🧪 [2단계] 자동화 회귀 테스트(Regression Tests) 구동...")
    all_passed = True
    reports = []

    # Python 인지 엔진 테스트
    if target in ["all", "python", "py"]:
        py_ok, py_msg = run_python_tests()
        reports.append(py_msg)
        if not py_ok:
            all_passed = False

    # Engine UAT 테스트
    if target in ["all", "engine", "uat"]:
        eng_ok, eng_msg = run_engine_tests()
        reports.append(eng_msg)
        if not eng_ok:
            all_passed = False

    print("=" * 55)
    print("📋 [SoL-Pi Action Fusion 영수증 (Compact Receipt)]")
    print(" 1. C++ 빌드:    " + ("(생략 — python 대상)" if target in ["python", "py"] else "✅ SUCCESS (0 errors)"))
    for r in reports:
        print(f" 2. {r}")
    print(" 3. 원시 로그:   Saved/Logs/sol_pi_raw.log")
    print("=" * 55 + "\n")

    return 0 if all_passed else 1


def slice_runtime_log(max_entries: int = 5):
    """엔진 런타임 로그에서 최근 에러/경고만 슬라이싱"""
    if not LOG_FILE.exists():
        print(f"[SoL-Pi] 로그 파일이 존재하지 않습니다: {LOG_FILE}")
        return

    try:
        content = LOG_FILE.read_text(encoding="utf-8", errors="replace")
    except Exception as e:
        print(f"[SoL-Pi] 로그 파일 읽기 실패: {e}")
        return

    lines = content.splitlines()
    matches = []

    for i, line in enumerate(lines):
        if any(re.search(pat, line) for pat in IGNORE_WARNING_PATTERNS):
            continue

        if any(k in line for k in ["Error:", "Fatal:", "CallFunc_"]):
            start = max(0, i - 1)
            end = min(len(lines), i + 3)
            matches.append("\n".join(lines[start:end]))
        elif "Warning:" in line and not any(
            k in line for k in ["LogStreaming: Warning:", "LogSlate: Could not find file"]
        ):
            start = max(0, i - 1)
            end = min(len(lines), i + 2)
            matches.append("\n".join(lines[start:end]))

    recent_matches = matches[-max_entries:]
    if not recent_matches:
        print("✅ [SoL-Pi] 최근 로그에 에러/치명적 경고 없음 (클린 상태)")
        return

    print(f"🔍 [SoL-Pi] 최근 핵심 런타임 로그 슬라이스 ({len(recent_matches)}건):")
    print("\n" + "-" * 40 + "\n")
    print("\n\n---\n\n".join(recent_matches))
    print("\n" + "-" * 40)


if __name__ == "__main__":
    mode = sys.argv[1].lower() if len(sys.argv) > 1 else "verify"
    sub_arg = sys.argv[2].lower() if len(sys.argv) > 2 else "all"

    if mode in ["build", "b"]:
        sys.exit(slice_build())
    elif mode in ["test", "t"]:
        sys.exit(run_tests(sub_arg))
    elif mode in ["verify", "v"]:
        sys.exit(run_action_fusion(sub_arg))
    elif mode in ["log", "l"]:
        slice_runtime_log()
    elif mode in ["status", "s"]:
        print("[SoL-Pi Status]")
        print(f"- Project: {UPROJECT.name}")
        print(f"- Editor Running: {is_editor_running()}")
        print(f"- Log File Size: {LOG_FILE.stat().st_size // 1024 if LOG_FILE.exists() else 0} KB")
    else:
        print("Usage: python tools/sol_pi.py [verify | build | test | log | status]")
