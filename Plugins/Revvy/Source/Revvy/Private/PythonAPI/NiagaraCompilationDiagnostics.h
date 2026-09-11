// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraExternalSystemEditorUtilities.h"

struct FNiagaraCompilationResult;

namespace Revvy
{
namespace NiagaraCompilationDiagnostics
{
	/**
	 * Fold the engine-reported compile state, stack issues, and diagnostic errors
	 * into a FNiagaraCompilationResult (errors/warnings/counts/success flag).
	 * Implemented in UNiagaraService.cpp.
	 */
	void PopulateResult(
		const FNiagaraExt_SystemCompileState& CompileState,
		const FNiagaraExt_StackIssues& StackIssues,
		const TArray<FText>& DiagnosticErrors,
		FNiagaraCompilationResult& OutResult);
}
}
