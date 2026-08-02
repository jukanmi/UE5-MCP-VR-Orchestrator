"""UE5 Editor MCP 서버 — RemoteControl HTTP API 프록시.

UE5 의 RemoteControl 플러그인(uproject 에 이미 활성)은 에디터 기동 시
127.0.0.1:30010 에 HTTP 서버를 자동으로 띄운다
(WebRemoteControl.cpp: bAutoStartWebServer 기본 true, 에디터면 무조건 허용).
이 서버는 그 raw API 를 MCP 툴 4개로 1:1 노출해, 클로드가 에디터를 직접 조작하게 한다.

전제:
  - UE5 에디터가 켜져 있어야 한다. 꺼져 있으면 모든 툴이 연결 실패 메시지를 반환한다.
  - localhost 요청은 passphrase 검사 면제(RemoteControlDefaultPreprocessors.h 의 IsLocal 통과).
    원격 접속은 지원하지 않는다 — UE_RC_URL 은 127.0.0.1 로 유지할 것.
"""

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


def main() -> None:
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
