// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PythonAPI/NiagaraCompilationDiagnostics.h"
#include "PythonAPI/UNiagaraService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyNiagaraCompilationDiagnosticsTest,
	"Revvy.Niagara.CompilationDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyNiagaraCompilationDiagnosticsTest::RunTest(const FString& Parameters)
{
	FNiagaraExt_SystemCompileState CompileState;
	CompileState.AggregateStatus = ENiagaraExt_ScriptCompileStatus::Error;
	CompileState.bHasErrors = true;
	CompileState.bHasWarnings = true;

	FNiagaraExt_ScriptCompileInfo& Script = CompileState.Scripts.AddDefaulted_GetRef();
	Script.EmitterName = TEXT("Smoke");
	Script.ScriptName = TEXT("ParticleUpdateScript");
	Script.LastCompileStatus = ENiagaraExt_ScriptCompileStatus::Error;

	FNiagaraExt_CompileEvent& CompileError = Script.CompileEvents.AddDefaulted_GetRef();
	CompileError.Severity = ENiagaraExt_CompileEventSeverity::Error;
	CompileError.Message = TEXT("Invalid module input");

	FNiagaraExt_CompileEvent& CompileWarning = Script.CompileEvents.AddDefaulted_GetRef();
	CompileWarning.Severity = ENiagaraExt_CompileEventSeverity::Warning;
	CompileWarning.Message = TEXT("Deprecated module version");

	FNiagaraExt_StackIssues StackIssues;
	FNiagaraExt_StackIssue& StackError = StackIssues.Issues.AddDefaulted_GetRef();
	StackError.Severity = ENiagaraExt_StackIssueSeverity::Error;
	StackError.ShortDescription = TEXT("Missing dependency");
	StackError.StackDisplayPath = TEXT("Smoke/ParticleUpdate/SolveForcesAndVelocity");

	FNiagaraExt_StackIssue& StackWarning = StackIssues.Issues.AddDefaulted_GetRef();
	StackWarning.Severity = ENiagaraExt_StackIssueSeverity::Warning;
	StackWarning.ShortDescription = TEXT("Expensive renderer setting");
	StackWarning.StackDisplayPath = TEXT("Smoke/Render/SpriteRenderer");

	FNiagaraExt_StackIssue& DismissedError = StackIssues.Issues.AddDefaulted_GetRef();
	DismissedError.Severity = ENiagaraExt_StackIssueSeverity::Error;
	DismissedError.ShortDescription = TEXT("Dismissed issue");
	DismissedError.bIsDismissed = true;

	FNiagaraCompilationResult Result;
	Revvy::NiagaraCompilationDiagnostics::PopulateResult(
		CompileState,
		StackIssues,
		{},
		Result);

	TestFalse(TEXT("Compile and stack errors fail the result"), Result.bSuccess);
	TestEqual(TEXT("Compile and stack errors are collected"), Result.ErrorCount, 2);
	TestEqual(TEXT("Compile and stack warnings are collected"), Result.WarningCount, 2);
	TestTrue(
		TEXT("Compile diagnostics retain their script location"),
		Result.Errors.IsValidIndex(0)
			&& Result.Errors[0].Contains(TEXT("Smoke/ParticleUpdateScript")));
	TestTrue(
		TEXT("Stack diagnostics retain their display path"),
		Result.Errors.IsValidIndex(1)
			&& Result.Errors[1].Contains(TEXT("Smoke/ParticleUpdate/SolveForcesAndVelocity")));
	TestFalse(
		TEXT("Dismissed stack issues are excluded"),
		Result.Errors.ContainsByPredicate([](const FString& Error)
		{
			return Error.Contains(TEXT("Dismissed issue"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyNiagaraCompilationStateFallbackTest,
	"Revvy.Niagara.CompilationStateFallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyNiagaraCompilationStateFallbackTest::RunTest(const FString& Parameters)
{
	FNiagaraExt_SystemCompileState ErrorState;
	ErrorState.AggregateStatus = ENiagaraExt_ScriptCompileStatus::Error;
	ErrorState.bHasErrors = true;

	FNiagaraCompilationResult ErrorResult;
	Revvy::NiagaraCompilationDiagnostics::PopulateResult(
		ErrorState,
		{},
		{ FText::FromString(TEXT("Stack diagnostics unavailable")) },
		ErrorResult);

	TestFalse(TEXT("An eventless compiler error still fails"), ErrorResult.bSuccess);
	TestEqual(
		TEXT("Diagnostic API and compiler state failures are both retained"),
		ErrorResult.ErrorCount,
		2);
	TestTrue(
		TEXT("Compiler state fallback is present"),
		ErrorResult.Errors.Contains(TEXT("Niagara compiler reported errors without diagnostic events")));

	FNiagaraExt_SystemCompileState WarningState;
	WarningState.AggregateStatus = ENiagaraExt_ScriptCompileStatus::UpToDateWithWarnings;

	FNiagaraCompilationResult WarningResult;
	Revvy::NiagaraCompilationDiagnostics::PopulateResult(
		WarningState,
		{},
		{},
		WarningResult);

	TestTrue(TEXT("An eventless warning status remains successful"), WarningResult.bSuccess);
	TestEqual(TEXT("Warning status fallback is retained"), WarningResult.WarningCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyNiagaraCompilationWarningSuccessTest,
	"Revvy.Niagara.CompilationWarningsDoNotFail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyNiagaraCompilationWarningSuccessTest::RunTest(const FString& Parameters)
{
	FNiagaraExt_SystemCompileState CompileState;
	CompileState.AggregateStatus = ENiagaraExt_ScriptCompileStatus::UpToDateWithWarnings;
	CompileState.bHasWarnings = true;

	FNiagaraExt_ScriptCompileInfo& Script = CompileState.Scripts.AddDefaulted_GetRef();
	Script.ScriptName = TEXT("SystemUpdateScript");
	Script.LastCompileStatus = ENiagaraExt_ScriptCompileStatus::UpToDateWithWarnings;
	FNiagaraExt_CompileEvent& Warning = Script.CompileEvents.AddDefaulted_GetRef();
	Warning.Severity = ENiagaraExt_CompileEventSeverity::Warning;
	Warning.Message = TEXT("Compilation completed with a warning");

	FNiagaraExt_StackIssues StackIssues;
	FNiagaraExt_StackIssue& DismissedError = StackIssues.Issues.AddDefaulted_GetRef();
	DismissedError.Severity = ENiagaraExt_StackIssueSeverity::Error;
	DismissedError.bIsDismissed = true;

	FNiagaraCompilationResult Result;
	Revvy::NiagaraCompilationDiagnostics::PopulateResult(
		CompileState,
		StackIssues,
		{},
		Result);

	TestTrue(TEXT("Warnings do not fail a completed compile"), Result.bSuccess);
	TestEqual(TEXT("Warnings are preserved"), Result.WarningCount, 1);
	TestEqual(TEXT("No errors are reported"), Result.ErrorCount, 0);
	return true;
}

#endif
