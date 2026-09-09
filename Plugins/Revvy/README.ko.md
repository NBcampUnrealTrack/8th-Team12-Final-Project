<div align="center">

# Revvy — 언리얼 엔진 AI 어시스턴트 (한국어 가이드)

Rev 전용 AI 어시스턴트 — 마스코트: Revvy 🐰🤖

> [VibeUE](https://github.com/kevinpbuckley/VibeUE) (Kevin Buckley, MIT License) 기반 — LICENSE 참조.

[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.8-orange)](https://www.unrealengine.com)
[![MCP](https://img.shields.io/badge/MCP-2025--11--25-blue)](https://modelcontextprotocol.io)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

**Revvy는 언리얼 에디터 안에 AI를 직접 넣어줍니다.** 에디터 내장 AI 채팅과 MCP(Model
Context Protocol) 서버를 통해 블루프린트, 에셋, 머티리얼, 애니메이션, 랜드스케이프,
오디오, UI, AI 비헤이비어, 프로젝트 설정 등을 자연어로 만들고 수정할 수 있습니다.

> 전체 기능·서비스·MCP 도구 레퍼런스는 영문 [README.md](README.md)를 참조하세요.
> 이 문서는 **설치와 연결 문제 해결**에 집중한 한국어 가이드입니다.

---

## 🚀 설치

### 사전 준비

- Unreal Engine **5.8**
- Git (수동 설치 시)

### 1. 플러그인 받기

```bash
cd <내프로젝트>/Plugins
git clone https://github.com/RevStudio/Revvy.git
```

> ⚠️ **중요**: `Binaries/`와 `Intermediate/`는 저장소에 포함되지 않습니다.
> **git으로 받으면 소스만 옵니다.** Revvy는 C++ 에디터 모듈이라 각자 PC에서
> 엔진 버전(5.8)에 맞게 **빌드해야 로드됩니다.** 빌드 없이 에디터를 열면
> "missing modules" 팝업이 뜨고, 플러그인이 비활성 상태로 열립니다.

### 2. 빌드 — 상황별 택 1

| 상황 | 방법 |
|---|---|
| **Visual Studio 없음** (아트 작업자 PC) | `Plugins\Revvy\SetupUnrealDev.bat` 더블클릭 — VS Build Tools 자동 설치(UAC 승인 1회, 10~30분) → 프로젝트 파일 생성 → 에디터 타깃 빌드까지 원클릭 |
| 툴체인 이미 있음 | `Plugins\Revvy\BuildPlugin.bat` 더블클릭 (플러그인만 빌드) |
| 받는 쪽에서 빌드 자체를 안 시키고 싶음 | 빌드된 패키지(`RunUAT BuildPlugin` 산출물, `Binaries/` 포함)를 zip으로 배포 — 단 **UE 5.8 전용**, 엔진 버전이 다르면 무효 |

`SetupUnrealDev.bat` 옵션: `-CheckOnly`(진단만) `-SkipInstall` `-SkipBuild` `-NoPrompt`

### 3. 플러그인 활성화

1. 프로젝트를 언리얼 에디터로 열기
2. **Edit > Plugins** 에서 **"Revvy"** 검색 → 활성화
3. 에디터 재시작

### 4. 채팅 시작

1. PC에 **Claude Code**, **Codex CLI**, **Kimi Code CLI** 중 하나가 설치·로그인돼 있어야 합니다.
2. **Tools > Revvy > AI Chat** (또는 상태 바의 **[레비]** 버튼) 클릭.
3. 바로 입력 시작 — MCP 서버와 CLI 경로는 첫 실행 때 자동 구성됩니다.

API 키를 Revvy에 저장하지 않습니다. 기존 로컬 로그인(Claude/ChatGPT/Kimi)을 그대로 재사용합니다.

---

## 🩺 문제 해결 — Revvy 앱에서 "Unreal 오프라인"

에디터를 실행했는데도 독립 실행 Revvy 앱에 **● Unreal 오프라인**이 계속 뜨는 경우.
플러그인을 처음 받아서 실행하는 사람에게 가장 흔한 문제입니다.

### 상태 배지 동작 원리

```
Revvy 앱 ── 프로젝트 런타임 읽음 ──> <프로젝트>\.revvy\revvy-proxy.json
        ── Safe ping + 서명 요청 ──> http://127.0.0.1:<ue_port>/mcp  (에디터 안 MCP 서버)
```

- MCP 서버는 **언리얼 에디터 안**(Revvy 플러그인)에서 돕니다.
- ping은 프로젝트·엔진·브리지 인스턴스를 확인하고, 실제 요청은 사용자 전용 보안 저장소의 키로 본문과 인스턴스에 묶어 서명합니다.
- 공개 프로젝트 설정에는 영구 Full 키가 저장되지 않습니다.
- 즉 **플러그인이 로드되지 않으면** 이 파일이 없거나 낡은 상태로 남고,
  에디터가 멀쩡히 떠 있어도 앱은 영원히 "오프라인"을 표시합니다.

### 원인 1순위: 플러그인 바이너리 없음 (새로 받은 머신)

```
1. clone/복사로 Plugins\Revvy 배치 (Binaries 없음)
2. 에디터 실행 → "The following modules are missing or built with a
   different engine version: Revvy. Rebuild now?" 팝업
3-a. Yes → VS 툴체인 없으면 빌드 실패 → 플러그인 비활성
3-b. No  → 플러그인 없이 에디터 열림
4. 어느 쪽이든 MCP 서버 미기동 → revvy-proxy.json 생성 안 됨
   → 앱 배지 "Unreal 오프라인" 영구
```

**해결**: 위 [설치 2단계](#2-빌드--상황별-택-1) 표에서 상황에 맞는 빌드 경로 실행.
아트 작업자 PC는 `SetupUnrealDev.bat` 하나로 끝납니다.

### 진단 체크리스트

에디터를 한 번 실행한 뒤 프로젝트 로그를 검색:

```powershell
Select-String -Path "<내프로젝트>\Saved\Logs\*.log" -Pattern "LogMCPServer"
```

| 로그/증상 | 원인 | 해결 |
|---|---|---|
| `LogMCPServer` 라인이 아예 없음 | 플러그인 모듈 미로드 (바이너리 없음 / 엔진 버전 불일치 / 플러그인 비활성) | 빌드 후 **Edit > Plugins > Revvy** 활성화 확인 |
| 에디터 시작 시 "missing modules" 팝업 | 바이너리 없음 / 엔진 버전 불일치 | `SetupUnrealDev.bat` 또는 빌드된 패키지 배포 |
| `Failed to bind to port ...` | 프로젝트에 할당된 포트를 다른 프로세스가 점유 | `revvy.cmd status --json`과 `<project>/.revvy/revvy-proxy.json`에서 실제 `ue_port`를 확인하고 점유 프로세스를 종료한 뒤 다시 시작 |
| `SECURITY: protected MCP Full capability provisioning failed` | 보안 저장소 ACL 실패 (`%LOCALAPPDATA%`가 리다이렉트/링크된 환경) | `%LOCALAPPDATA%\Revvy\Security`가 실제 로컬 NTFS 폴더인지 확인. `REVVY_SECURITY_DIR` 환경 변수로 위치 변경 가능 |
| `MCP Server initialized - Enabled: No` | 프로젝트별 설정에서 서버 꺼짐 | `EditorPerProjectUserSettings.ini` `[Revvy.MCPServer]`에 `Enabled=True` |
| `Wrote standalone proxy config to ...`는 있는데 여전히 오프라인 | 에디터가 파일 쓰기 전에 앱이 폴링했거나, 에디터 여러 개가 설정 파일을 덮어씀 | 앱 새로고침. 에디터 중복 실행 종료 (마지막에 뜬 에디터가 토큰 소유) |
| 에디터 아직 로딩 중 | 플러그인 초기화는 에디터 시작 과정에서 수행 — 대형 프로젝트는 수 분 소요 | 로딩 완료 후 새로고침 |

빠른 확인: 에디터가 완전히 뜬 뒤 `%LOCALAPPDATA%\Revvy\revvy-proxy.json`이
존재하고 `updated_at`이 최신이어야 정상. 파일이 없으면 플러그인이 초기화되지
않은 것 — 위 표로 돌아가세요.

---

## 🔐 외부 클라이언트 권한 — "도구가 읽기 전용밖에 없어요"

프록시는 엔드포인트를 두 개 제공합니다.

| 엔드포인트 | 노출 도구 | 인증 |
|---|---|---|
| `http://127.0.0.1:<proxy_port>/mcp/safe` | 읽기 전용 도구만 | 없음. `revvy.cmd install`과 에디터 브리지가 `.mcp.json`에 쓰는 값 |
| `http://127.0.0.1:<proxy_port>/mcp` | 전체 도구 (변경 포함) | `Authorization: Bearer $REVVY_FULL_ACCESS_TOKEN` 필수. 없으면 HTTP 401 |

Safe 엔드포인트가 "전체가 읽기 전용인 도구"만 주는 것은 아닙니다. 액션이 섞인
도구는 조회 액션만 붙여서 노출됩니다 — `manage_asset`은 `search`/`find`/`list`/`help`,
`terrain_data`는 `preview_elevation`/`list_styles`. 어떤 액션이 남는지는 도구 설명에
적히고, 나머지 액션은 프록시와 에디터 양쪽에서 거부됩니다. 베어러 없이도 프로젝트
조회는 가능하다는 뜻입니다.

설치 직후 기본값은 Safe입니다. 그래서 조회는 되는데 변경 도구를 부르면
*"This Revvy session is Safe/read-only."* 가 돌아옵니다. Full 권한을 얻는
지원 경로는 두 가지입니다.

**1. Revvy 데스크톱 앱에서 실행.** **권한** 선택을 `Full access · 전체 권한`으로
바꾸면 앱이 세션 토큰을 만들어 자식 프로세스에 주입하고 `/mcp`로 연결합니다.
따로 설정할 것이 없습니다.

**2. 본인 클라이언트에 베어러를 직접 물리기.** 프록시는 환경 변수
`REVVY_FULL_ACCESS_TOKEN` 값이 32자 이상일 때만 채택합니다. 더 짧으면 무시하고
난수 토큰을 쓰므로 외부 클라이언트는 Safe에 머뭅니다.

```powershell
# 1. 사용자 계정에 강한 토큰 고정 (최초 1회)
$bytes = New-Object byte[] 32
[System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
[Environment]::SetEnvironmentVariable(
    'REVVY_FULL_ACCESS_TOKEN', [Convert]::ToBase64String($bytes), 'User')

# 2. 변수가 보이도록 새 터미널을 연 뒤, 그 터미널로 프록시 소유권 넘기기
Plugins\Revvy\revvy.cmd restart
```

그다음 클라이언트 설정은 CLI가 써 줍니다.

```powershell
Plugins\Revvy\revvy.cmd grant-full --project .
```

읽기 전용 항목 옆에 두 번째 서버를 추가하므로, 프로젝트가 두 권한 수준을
모두 유지하고 클라이언트는 어느 쪽에 붙을지로 권한을 고릅니다.

```json
{
  "mcpServers": {
    "Revvy":      { "type": "http", "url": "http://127.0.0.1:<proxy_port>/mcp/safe" },
    "Revvy-Full": {
      "type": "http",
      "url": "http://127.0.0.1:<proxy_port>/mcp",
      "headers": { "Authorization": "Bearer ${REVVY_FULL_ACCESS_TOKEN}" }
    }
  }
}
```

파일에는 변수 참조만 들어가고 실제 토큰은 환경변수에 남으므로 커밋해도
안전합니다. 변수가 없거나 짧으면 `grant-full`은 아무것도 쓰지 않고 거부하며,
실행 중인 프록시가 다른 토큰을 쥐고 있으면 경고합니다 — 둘 다 클라이언트
쪽에서는 원인을 알기 어려운 401로 나타나기 때문입니다. 되돌리려면
`revvy.cmd revoke-full --project .`.

손으로 써도 됩니다. `headers` 블록이 있는 항목은 의도적 설정으로 간주해
Unreal Editor도 `revvy.cmd install`도 URL을 덮어쓰지 않습니다.

> ⚠️ Full 권한을 얻으려고 `MCPServer.cpp`의 `/mcp/safe` 기본값을 바꾸지 마세요.
> 공유되는 `Revvy` 항목이 자격증명 없는 엔드포인트를 가리키게 되어, 베어러를
> 싣지 않는 모든 클라이언트가 401을 받고 도구 목록 자체를 잃습니다 — 읽기
> 전용보다 나쁩니다. 두 번째 항목을 추가하는 방식을 쓰세요.

`revvy.cmd`도 같은 규칙을 따릅니다. 실행 중인 프록시를 다른 프로세스가 띄웠으면
한 번 알리고 `/mcp/safe`로 내려가므로, 모든 명령이 401로 죽는 대신 조회는
계속 동작합니다.

### 에디터 내장 채팅 세션

내장 채팅 패널은 도구 실행을 provider CLI에 위임합니다. 따라서 그 CLI가 붙는
엔드포인트가 곧 패널의 변경 권한입니다. (OpenRouter는 예외 — Revvy 자체
인프로세스 툴 루프를 돌리므로 프록시를 거치지 않습니다.)

이건 에디터가 알아서 처리합니다. 시작할 때 고정된 `REVVY_FULL_ACCESS_TOKEN`을
채택하거나, 없으면 하나 만들어 자기 프로세스에 등록합니다. 그러면 에디터가
띄우는 프록시와 실행하는 모든 CLI 세션이 같은 베어러를 공유합니다. Claude Code와
Codex 세션에는 `Saved/Revvy/mcp-editor.json`이 전달됩니다 — 그 세션 전용
설정으로, Full 권한이 가능하면 베어러와 함께 `/mcp`를, 아니면 `/mcp/safe`를
가리킵니다.

Full 권한이 "가능"한 경우는 둘 중 하나입니다.

- 사용자 환경변수로 `REVVY_FULL_ACCESS_TOKEN`을 고정해 세션 내 모든 Revvy
  프로세스가 값을 공유하거나,
- 실행 중인 프록시를 이 에디터가 띄워서 프록시가 그 토큰을 상속했거나.

다른 프로세스가 프록시를 먼저 띄웠고 토큰도 고정하지 않았다면, 에디터는 그
프록시의 베어러를 알 수 없으므로 도구가 0개인 세션을 만드는 대신 읽기 전용
엔드포인트에 머뭅니다. 변수가 설정된 셸에서 `revvy.cmd restart`를 돌리거나,
프록시를 멈춘 상태로 에디터를 재시작하면 해결됩니다.

프로젝트의 공유 `.mcp.json`은 이 세션들에 **의도적으로 쓰지 않습니다.** 그
파일은 프로젝트 디렉터리에서 실행되는 모든 것이 읽으므로, 거기에 Full 권한을
쓰면 그 모든 세션에 에디터를 넘기는 셈입니다. 그걸 원할 때는 위의 `grant-full`을
쓰세요.

Kimi만 예외로 남습니다. 작업 디렉터리의 `.kimi-code/mcp.json`을 읽는데 이
파일은 외부 셸과 공유되므로, 토큰을 세션 전체에 고정한 경우에만 에디터가 Full
엔드포인트를 씁니다.

> ⚠️ Full 권한은 `execute_python_code`와 같은 권한 — 에디터 안에서의 임의 코드
> 실행 — 을 토큰을 읽을 수 있는 모든 프로세스에 주는 것입니다. 대신 프록시의
> 읽기 전용 필터를 뜯어내는 방식은 쓰지 마세요. `/mcp/safe`는 자체 자격증명이
> 없는 엔드포인트라서, 그 필터를 제거하면 Revvy가 **머신의 아무 프로세스나
> 조작 가능한 무인증 로컬 API**가 됩니다.

---

## 🔗 더 보기

- 전체 기능·API·MCP 도구·스킬 시스템 레퍼런스: [README.md](README.md) (영문)
- 설치 상세: [INSTALL.md](INSTALL.md)
- 외부 AI 에이전트(Claude Code, Cursor 등) 연동 템플릿: `Content/samples/README.md`
- Blender 연동: `Content/Blender/README.md`

## 📄 라이선스

MIT — [LICENSE](LICENSE) 참조. Kevin Buckley의 [VibeUE](https://github.com/kevinpbuckley/VibeUE) 기반.
