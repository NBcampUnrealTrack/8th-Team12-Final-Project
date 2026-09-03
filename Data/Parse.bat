@echo off
setlocal

REM ---------------------------------------------------------------------------
REM 대사 스크립트 -> DT JSON 변환
REM
REM   build_dialogue.bat MainStory1        확장자 생략 가능
REM   build_dialogue.bat MainStory1.txt
REM   build_dialogue.bat                   Script 폴더 전체 변환
REM
REM ---------------------------------------------------------------------------

REM 이 배치 파일 기준으로 프로젝트 루트를 잡는다 (Tools의 상위)
set "ROOT=%~dp0.."
pushd "%ROOT%"

REM --- 여기만 프로젝트에 맞게 고치면 된다 -------------------------------------
set "PARSER=Data\Parser.py"
set "SCRIPTDIR=Data"
set "OUTDIR=Data"
set "HEADERS=Source\Febricula\Public\Dialogue\GameActionTypes.h Source\Febricula\Public\Dialogue\DialogueDataTypes.h"
REM ---------------------------------------------------------------------------

if "%~1"=="" (
    REM 인자가 없으면 전체 변환
    set "TARGET=%SCRIPTDIR%\*.txt"
) else (
    REM 확장자를 안 붙였으면 .txt를 붙여준다
    if /i "%~x1"==".txt" (
        set "TARGET=%SCRIPTDIR%\%~nx1"
    ) else (
        set "TARGET=%SCRIPTDIR%\%~n1.txt"
    )
)

echo.
echo [변환] %TARGET%
echo.

python "%PARSER%" %TARGET% --outdir "%OUTDIR%" --enum-header %HEADERS%
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo [성공] JSON이 %OUTDIR% 에 생성되었습니다.
) else (
    echo [실패] 위 에러를 고치고 다시 실행하세요.
)

popd
endlocal
pause