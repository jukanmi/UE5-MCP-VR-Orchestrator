"""UE5 Editor MCP 서버 — RemoteControl HTTP API 프록시.

UE5 의 RemoteControl 플러그인(uproject 에 이미 활성)은 에디터 기동 시
127.0.0.1:30010 에 HTTP 서버를 자동으로 띄운다
(WebRemoteControl.cpp: bAutoStartWebServer 기본 true, 에디터면 무조건 허용).
이 서버는 그 raw API 를 MCP 툴로 노출해, 클로드가 에디터를 직접 조작하게 한다.
툴 4개는 raw 라우트 1:1 래퍼이고, `ue_run_python` 만 PythonScriptPlugin 의
ExecutePythonCommandEx 를 태워 에디터 안에서 `unreal` 모듈을 돌린다(에셋 생성 등 raw API 밖의 작업).

전제:
  - UE5 에디터가 켜져 있어야 한다. 꺼져 있으면 모든 툴이 연결 실패 메시지를 반환한다.
  - `ue_run_python` 은 PythonScriptPlugin 활성화가 추가로 필요하다(엔진 기본 비활성).
  - localhost 요청은 passphrase 검사 면제(RemoteControlDefaultPreprocessors.h 의 IsLocal 통과).
    원격 접속은 지원하지 않는다 — UE_RC_URL 은 127.0.0.1 로 유지할 것.
"""

import json
import os
from typing import Any

import httpx
from mcp.server.fastmcp import FastMCP

# RemoteControl HTTP 서버 주소. 포트는 Project Settings > Plugins > Remote Control 에서 변경 가능.
BASE_URL = os.environ.get("UE_RC_URL", "http://127.0.0.1:30010").rstrip("/")
TIMEOUT = float(os.environ.get("UE_RC_TIMEOUT", "30"))

mcp = FastMCP("ue5-remote-control")


class UENotRunningError(RuntimeError):
    """에디터 미기동 — 연결 자체가 안 될 때."""


async def _put(path: str, payload: dict[str, Any]) -> str:
    """RemoteControl raw API 에 PUT. 응답 본문을 문자열로 반환.

    모든 raw 라우트가 PUT + application/json 이다(GET 은 /remote/info 뿐).
    Content-Type 이 정확히 application/json 이 아니면 UE 가 400 을 낸다.
    """
    url = f"{BASE_URL}{path}"
    try:
        async with httpx.AsyncClient(timeout=TIMEOUT) as client:
            resp = await client.put(url, json=payload, headers={"Content-Type": "application/json"})
    except httpx.ConnectError as exc:
        raise UENotRunningError(
            f"UE5 에디터의 RemoteControl 서버({BASE_URL})에 연결할 수 없습니다. "
            "에디터가 켜져 있는지 확인하십시오. 켜져 있는데도 실패하면 에디터 콘솔에서 "
            "`WebControl.StartServer` 를 실행하십시오."
        ) from exc

    body = resp.text.strip()
    if resp.status_code >= 400:
        return f"[HTTP {resp.status_code}] {body or '(빈 응답)'}"
    return body or "(성공, 빈 응답)"


@mcp.tool()
async def ue_call_function(
    object_path: str,
    function_name: str,
    parameters: dict[str, Any] | None = None,
    generate_transaction: bool = True,
) -> str:
    """UE5 에디터의 오브젝트에서 UFunction 을 호출한다 (PUT /remote/object/call).

    Args:
        object_path: 대상 오브젝트 경로.
            - 에디터 서브시스템은 CDO 경로: "/Script/UnrealEd.Default__EditorActorSubsystem"
            - 레벨 액터: "/Game/Level/Sample.Sample:PersistentLevel.BP_SmartNPC_C_1"
            - 에셋: "/Game/Blueprint/NPC/ABP_SmartNPC.ABP_SmartNPC"
        function_name: 호출할 함수 이름 (BlueprintCallable/CallInEditor 여야 한다).
        parameters: 함수 인자를 이름:값 dict 로. 없으면 생략.
        generate_transaction: True 면 에디터 Undo 트랜잭션 생성 (기본 True — 되돌리기 가능).

    Returns:
        함수 반환값 JSON 문자열, 또는 에러 메시지.
    """
    payload: dict[str, Any] = {
        "objectPath": object_path,
        "functionName": function_name,
        "generateTransaction": generate_transaction,
    }
    if parameters:
        payload["parameters"] = parameters
    return await _put("/remote/object/call", payload)


@mcp.tool()
async def ue_get_property(object_path: str, property_name: str) -> str:
    """UE5 오브젝트의 프로퍼티 값을 읽는다 (PUT /remote/object/property, READ_ACCESS).

    Args:
        object_path: 대상 오브젝트 경로 (ue_call_function 의 object_path 와 동일 형식).
        property_name: 읽을 프로퍼티 이름 (C++ 변수명 그대로).

    Returns:
        {"프로퍼티명": 값} 형태의 JSON 문자열, 또는 에러 메시지.
    """
    payload = {
        "objectPath": object_path,
        "access": "READ_ACCESS",
        "propertyName": property_name,
    }
    return await _put("/remote/object/property", payload)


@mcp.tool()
async def ue_set_property(
    object_path: str,
    property_name: str,
    value: Any,
    generate_transaction: bool = True,
) -> str:
    """UE5 오브젝트의 프로퍼티 값을 쓴다 (PUT /remote/object/property, WRITE_*_ACCESS).

    Args:
        object_path: 대상 오브젝트 경로.
        property_name: 쓸 프로퍼티 이름.
        value: 넣을 값. 구조체는 dict 로 (예: {"X": 0, "Y": 0, "Z": 100}), 배열은 list 로.
        generate_transaction: True 면 Undo 트랜잭션 생성 (기본 True).

    Returns:
        성공 시 빈 응답 표시, 또는 에러 메시지.

    Note:
        에셋 프로퍼티 변경은 메모리에만 반영된다. 디스크 저장은 별도로
        ue_call_function 으로 EditorAssetSubsystem 의 SaveLoadedAsset 을 호출해야 한다.
    """
    payload = {
        "objectPath": object_path,
        "access": "WRITE_TRANSACTION_ACCESS" if generate_transaction else "WRITE_ACCESS",
        "propertyName": property_name,
        "propertyValue": {property_name: value},
    }
    return await _put("/remote/object/property", payload)


@mcp.tool()
async def ue_search_assets(
    query: str = "",
    limit: int = 50,
    class_names: list[str] | None = None,
    package_paths: list[str] | None = None,
) -> str:
    """콘텐츠 브라우저 에셋을 검색해 오브젝트 경로를 찾는다 (PUT /remote/search/assets).

    다른 툴에 넘길 object_path 를 알아낼 때 쓴다.

    Args:
        query: 에셋 이름에 대한 부분 일치 검색어. 빈 문자열이면 필터 조건만 적용.
        limit: 최대 결과 수 (기본 50).
        class_names: 클래스 전체 경로로 필터. 예: ["/Script/Engine.Blueprint"].
            지정 시 하위 클래스도 포함(RecursiveClasses=true).
        package_paths: 경로로 필터. 예: ["/Game/Blueprint/NPC"]. 하위 폴더 재귀 포함.

    Returns:
        에셋 목록 JSON 문자열 (Name, Class, Path 포함), 또는 에러 메시지.
    """
    asset_filter: dict[str, Any] = {}
    if class_names:
        asset_filter["ClassNames"] = class_names
        asset_filter["RecursiveClasses"] = True
    if package_paths:
        asset_filter["PackagePaths"] = package_paths
        asset_filter["RecursivePaths"] = True

    payload: dict[str, Any] = {"Query": query, "Limit": limit}
    if asset_filter:
        payload["Filter"] = asset_filter
    return await _put("/remote/search/assets", payload)


# PythonScriptPlugin 의 BlueprintFunctionLibrary CDO. ExecutePythonCommandEx 가 여기 붙어 있다.
PYTHON_LIB_PATH = "/Script/PythonScriptPlugin.Default__PythonScriptLibrary"

# ue_run_python 의 mode → EPythonCommandExecutionMode 이름 (PythonScriptTypes.h:36).
#
# 주의: UE 의 ExecuteStatement 는 Py_single_input 으로 컴파일하므로 **여러 줄을 못 받는다**
# (`import unreal\nprint(...)` → "multiple statements found while compiling a single statement").
# 그래서 script 모드는 코드를 통째로 exec(...) 한 줄로 감싸 넘긴다. 임시 .py 파일을 쓰는
# ExecuteFile 방식보다 낫다 — 경로에 공백이 있으면 UE 가 인자로 잘라먹고, 정리 책임도 생긴다.
PYTHON_EXEC_MODES = {
    "script": "ExecuteStatement",  # 기본. 여러 줄 코드. 결과는 print 로 회수(반환값 없음).
    "eval": "EvaluateStatement",  # 표현식 1개 평가. CommandResult 에 repr 이 담긴다.
    "file": "ExecuteFile",  # 디스크의 .py 실행. script 인자에 "경로 [인자...]" 를 넣는다.
}


@mcp.tool()
async def ue_run_python(script: str, mode: str = "script") -> str:
    """UE5 에디터 안에서 파이썬(`unreal` 모듈)을 실행한다.

    RemoteControl 로 노출되지 않는 에디터 자동화 — 블루프린트 에셋 생성, 컴포넌트 추가,
    컴파일, 레벨 스폰, 스크린샷 — 은 전부 이 툴로 한다. 다른 4개 툴로 안 되는 게 여기선 된다.

    Args:
        script: 실행할 파이썬 코드. mode="file" 일 때만 파일 경로로 해석된다.
            결과는 반환되지 않으므로 알고 싶은 값은 반드시 print 할 것 (LogOutput 으로 회수됨).
        mode: "script"(기본, 여러 줄 코드) · "eval"(표현식 1개, 값 반환) · "file"(.py 실행).

    Returns:
        실행 로그 + 실패 시 파이썬 트레이스백.

    Note:
        - **선행 설정 2가지.** PythonScriptPlugin 활성화 + Project Settings > Plugins >
          Remote Control > Security 의 `Enable Remote Python Execution`(선행: `Restrict Server
          Access`). 후자가 꺼져 있으면 RemoteControl 이 PythonScriptLibrary 를 클래스명으로
          하드 차단한다(RemoteControlModule.cpp:2531).
        - 에디터 Undo 트랜잭션을 걸지 않는다. 되돌리기가 필요하면 스크립트 안에서
          `with unreal.ScopedEditorTransaction("설명"):` 을 직접 쓸 것.
        - 에셋 변경은 메모리에만 반영된다. `unreal.EditorAssetLibrary.save_asset(경로)` 필수.
    """
    exec_mode = PYTHON_EXEC_MODES.get(mode)
    if exec_mode is None:
        return f"[에러] mode 는 {sorted(PYTHON_EXEC_MODES)} 중 하나여야 합니다 (받은 값: {mode!r})."

    raw = await _put(
        "/remote/object/call",
        {
            "objectPath": PYTHON_LIB_PATH,
            "functionName": "ExecutePythonCommandEx",
            "generateTransaction": False,
            "parameters": {
                # script 모드만 exec 한 줄로 감싼다(위 PYTHON_EXEC_MODES 주석 참조).
                "PythonCommand": f"exec({script!r})" if mode == "script" else script,
                "ExecutionMode": exec_mode,
                "FileExecutionScope": "Private",
            },
        },
    )

    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        # _put 이 HTTP 에러 문자열을 돌려준 경우. CDO 를 못 찾으면 십중팔구 플러그인 미활성이다.
        if "PythonScriptLibrary" in raw or "404" in raw:
            return (
                f"{raw}\n\n"
                "PythonScriptPlugin 이 꺼져 있을 수 있습니다. 에디터 Edit > Plugins 에서 "
                "'Python Editor Script Plugin' 을 켜고 에디터를 재시작하십시오."
            )
        return raw

    lines = [entry.get("Output", "").rstrip() for entry in data.get("LogOutput") or []]
    log = "\n".join(line for line in lines if line)
    result = data.get("CommandResult") or ""

    if data.get("ReturnValue"):
        # 성공 시 CommandResult 는 eval 모드에서만 값이 있다(그 외엔 "None").
        parts = [log] if log else []
        if mode == "eval" and result:
            parts.append(f"=> {result}")
        return "\n".join(parts) or "(성공, 출력 없음)"

    # 실패 시 CommandResult 에 파이썬 트레이스백이 담긴다.
    parts = ["[파이썬 실행 실패]"]
    if result:
        parts.append(result)
    if log:
        parts.append(log)
    return "\n".join(parts)


def main() -> None:
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
