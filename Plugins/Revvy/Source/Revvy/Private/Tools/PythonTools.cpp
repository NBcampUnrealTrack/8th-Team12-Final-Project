// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Tools/PythonTools.h"
#include "Tools/PythonTypes.h"
#include "Core/ServiceContext.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Core/ErrorCodes.h"
#include "FileHelpers.h"
#include "Misc/ConfigCacheIni.h"

// Include service headers after PythonTypes
#include "Tools/PythonExecutionService.h"
#include "Tools/PythonDiscoveryService.h"
#include "Tools/PythonSchemaService.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogPythonTools, Log, All);

using namespace Revvy;

// Track whether the last Python execution crashed (SEH-caught).
// When true, skip auto-save to avoid serializing potentially corrupt assets.
static bool bLastPythonExecutionCrashed = false;

static bool ContainsWorldMutatingPythonPattern(const FString& Code)
{
	const FString CodeLower = Code.ToLower();
	static const TArray<FString> DangerousPatterns = {
		TEXT("destroy_actor("),
		TEXT("delete_landscape("),
		TEXT("create_landscape("),
		TEXT("import_heightmap("),
		TEXT("spawn_actor("),
		TEXT("editoractorsubsystem"),
		TEXT("get_all_level_actors("),
		TEXT("apply_splines_to_landscape(")
	};

	for (const FString& Pattern : DangerousPatterns)
	{
		if (CodeLower.Contains(Pattern))
		{
			return true;
		}
	}

	return false;
}

static bool CanRunWorldMutatingPython(FString& OutReason)
{
	if (!GEditor)
	{
		OutReason = TEXT("Editor is not available yet.");
		return false;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld)
	{
		OutReason = TEXT("Editor world is not available yet.");
		return false;
	}

	if (UWorldPartition* WorldPartition = EditorWorld->GetWorldPartition())
	{
		if (!WorldPartition->IsInitialized())
		{
			OutReason = TEXT("World Partition is not initialized yet.");
			return false;
		}

		if (!WorldPartition->GetResolvingDataLayerManager())
		{
			OutReason = TEXT("World Partition data layers are still initializing.");
			return false;
		}
	}

	return true;
}

// Static service instances
static TSharedPtr<FPythonExecutionService> ExecutionServiceInstance;
static TSharedPtr<FPythonDiscoveryService> DiscoveryServiceInstance;
static TSharedPtr<FPythonSchemaService> SchemaServiceInstance;
static TSharedPtr<FServiceContext> ServiceContextInstance;

// Every Python tool entry point is marshaled to the game thread by the MCP
// server, so these lazy singletons are game-thread-only by architecture. The
// lock makes first-touch construction safe anyway — a future off-thread caller
// gets one consistent instance instead of two half-built ones — and the ensure
// names the contract violation in the log instead of leaving a silent race.
static FCriticalSection PythonServiceInitLock;

static void EnsurePythonServiceContext()
{
	ensureMsgf(IsInGameThread(),
		TEXT("UPythonTools services must be created on the game thread; the MCP server marshals tool calls there."));
	if (!ServiceContextInstance.IsValid())
	{
		ServiceContextInstance = MakeShared<FServiceContext>();
	}
}

UPythonTools::UPythonTools()
{
}

TSharedPtr<FPythonExecutionService> UPythonTools::GetExecutionService()
{
	FScopeLock Lock(&PythonServiceInitLock);
	if (!ExecutionServiceInstance.IsValid())
	{
		EnsurePythonServiceContext();
		ExecutionServiceInstance = MakeShared<FPythonExecutionService>(ServiceContextInstance);
		ExecutionServiceInstance->Initialize();
	}
	return ExecutionServiceInstance;
}

TSharedPtr<FPythonDiscoveryService> UPythonTools::GetDiscoveryService()
{
	// GetExecutionService takes the same non-reentrant lock, so resolve it
	// before locking here.
	TSharedPtr<FPythonExecutionService> ExecutionService = GetExecutionService();
	FScopeLock Lock(&PythonServiceInitLock);
	if (!DiscoveryServiceInstance.IsValid())
	{
		EnsurePythonServiceContext();
		DiscoveryServiceInstance = MakeShared<FPythonDiscoveryService>(ServiceContextInstance, ExecutionService);
		DiscoveryServiceInstance->Initialize();
	}
	return DiscoveryServiceInstance;
}

TSharedPtr<FPythonSchemaService> UPythonTools::GetSchemaService()
{
	FScopeLock Lock(&PythonServiceInitLock);
	if (!SchemaServiceInstance.IsValid())
	{
		EnsurePythonServiceContext();
		SchemaServiceInstance = MakeShared<FPythonSchemaService>(ServiceContextInstance);
		SchemaServiceInstance->Initialize();
	}
	return SchemaServiceInstance;
}

void UPythonTools::Shutdown()
{
	UE_LOG(LogPythonTools, Log, TEXT("UPythonTools::Shutdown - Releasing Python service instances"));
	
	// Release all Python service instances BEFORE Python shuts down
	// Order matters - discovery depends on execution
	FScopeLock Lock(&PythonServiceInitLock);
	SchemaServiceInstance.Reset();
	DiscoveryServiceInstance.Reset();
	ExecutionServiceInstance.Reset();
	ServiceContextInstance.Reset();
	
	UE_LOG(LogPythonTools, Log, TEXT("UPythonTools::Shutdown - All service instances released"));
}

FString UPythonTools::ExecutePythonCode(const FString& Code)
{
	// Efficient engine readiness check - once ready, never check again
	static bool bEngineReady = false;
	if (!bEngineReady)
	{
		// Check engine, editor, and that we're past initial load phase
		if (!GEngine || !GEditor || GIsInitialLoad)
		{
			return TEXT("Unreal Engine Loading");
		}
		bEngineReady = true;
	}

	if (ContainsWorldMutatingPythonPattern(Code))
	{
		FString BlockReason;
		if (!CanRunWorldMutatingPython(BlockReason))
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(TEXT("success"), false);
			ErrorObj->SetStringField(TEXT("error_code"), TEXT("WORLD_NOT_READY"));
			ErrorObj->SetStringField(TEXT("error_message"), FString::Printf(
				TEXT("Blocked world-mutating Python execution: %s Retry in a few seconds after the level finishes loading."),
				*BlockReason));
			FString JsonString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
			FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
			return JsonString;
		}
	}

	// Auto-save all dirty packages before executing Python code if enabled.
	// The setting survives the retired in-editor chat: it guards MCP-driven
	// execution just the same, and the ini key stays so existing installs keep
	// their choice.
	bool bAutoSaveBeforePython = true; // Default enabled for safety
	GConfig->GetBool(TEXT("Revvy"), TEXT("AutoSaveBeforePythonExecution"), bAutoSaveBeforePython, GEditorPerProjectIni);
	if (bAutoSaveBeforePython)
	{
		if (bLastPythonExecutionCrashed)
		{
			UE_LOG(LogPythonTools, Warning, TEXT("Skipping auto-save: previous Python execution crashed — dirty assets may be corrupt"));
		}
		else if (!GEditor)
		{
			UE_LOG(LogPythonTools, Warning, TEXT("Cannot auto-save: GEditor is not available"));
		}
		else if (GIsPlayInEditorWorld)
		{
			UE_LOG(LogPythonTools, Warning, TEXT("Cannot auto-save: Currently in PIE mode"));
		}
		else
		{
			UE_LOG(LogPythonTools, Verbose, TEXT("Auto-saving dirty packages before Python execution..."));

			const bool bPromptUserToSave = false;
			const bool bSaveMapPackages = true;
			const bool bSaveContentPackages = true;
			const bool bFastSave = false;
			const bool bNotifyNoPackagesSaved = false;
			const bool bCanBeDeclined = false;

			bool bSaveSuccess = FEditorFileUtils::SaveDirtyPackages(
				bPromptUserToSave,
				bSaveMapPackages,
				bSaveContentPackages,
				bFastSave,
				bNotifyNoPackagesSaved,
				bCanBeDeclined
			);

			if (bSaveSuccess)
			{
				UE_LOG(LogPythonTools, Verbose, TEXT("Auto-save completed successfully"));
			}
			else
			{
				UE_LOG(LogPythonTools, Warning, TEXT("Auto-save completed with warnings or errors"));
			}
		}
	}

	auto Service = GetExecutionService();
	if (!Service.IsValid() || !Service.Get())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
		ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python execution service is not available"));
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	// Extra safety: verify service context is valid
	if (!ServiceContextInstance.IsValid())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), TEXT("SERVICE_CONTEXT_INVALID"));
		ErrorObj->SetStringField(TEXT("error_message"), TEXT("Service context is not properly initialized"));
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	auto Result = Service->ExecuteCode(Code);

	if (Result.IsError())
	{
		// Track crash state so next auto-save is skipped (corrupt assets)
		if (Result.GetErrorCode() == FString(ErrorCodes::PYTHON_INTERPRETER_DAMAGED))
		{
			bLastPythonExecutionCrashed = true;
		}

		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
		ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	// Successful execution — safe to auto-save again
	bLastPythonExecutionCrashed = false;

	return ConvertExecutionResultToJson(Result.GetValue());
}

FString UPythonTools::DiscoverPythonModule(const FString& ModuleName)
{
	auto Service = GetDiscoveryService();
	if (!Service.IsValid())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
		ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python discovery service is not available"));
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	// Use max_depth=1 and empty filter as defaults
	auto Result = Service->DiscoverUnrealModule(1, TEXT(""));

	if (Result.IsError())
	{
		// Return error as JSON
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
		ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	return ConvertModuleInfoToJson(Result.GetValue());
}

FString UPythonTools::DiscoverPythonClass(const FString& ClassName)
{
	auto Service = GetDiscoveryService();
	if (!Service.IsValid())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
		ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python discovery service is not available"));
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}
	
	auto Result = Service->DiscoverClass(ClassName);

	if (Result.IsError())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
		ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	return ConvertClassInfoToJson(Result.GetValue());
}

FString UPythonTools::DiscoverPythonFunction(const FString& FunctionName)
{
	auto Service = GetDiscoveryService();
	if (!Service.IsValid())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
		ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python discovery service is not available"));
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}
	
	auto Result = Service->DiscoverFunction(FunctionName);

	if (Result.IsError())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
		ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	return ConvertFunctionInfoToJson(Result.GetValue());
}

FString UPythonTools::ListPythonSubsystems()
{
	auto Service = GetDiscoveryService();
	if (!Service.IsValid())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
		ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python discovery service is not available"));
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}
	
	auto Result = Service->ListEditorSubsystems();

	if (Result.IsError())
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
		ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return JsonString;
	}

	// Convert subsystems array to JSON
	TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	ResponseObj->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> SubsystemsArray;
	for (const FString& Subsystem : Result.GetValue())
	{
		SubsystemsArray.Add(MakeShared<FJsonValueString>(Subsystem));
	}
	ResponseObj->SetArrayField(TEXT("subsystems"), SubsystemsArray);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);
	return JsonString;
}

FString UPythonTools::ConvertExecutionResultToJson(const Revvy::FPythonExecutionResult& Result)
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetBoolField(TEXT("success"), Result.bSuccess);
	JsonObj->SetStringField(TEXT("output"), Result.Output);
	JsonObj->SetStringField(TEXT("result"), Result.Result);
	
	// Only include error field if there's an actual error message
	if (!Result.ErrorMessage.IsEmpty())
	{
		JsonObj->SetStringField(TEXT("error"), Result.ErrorMessage);
	}

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	for (const FString& Warning : Result.WarningMessages)
	{
		WarningsArray.Add(MakeShared<FJsonValueString>(Warning));
	}
	JsonObj->SetArrayField(TEXT("warnings"), WarningsArray);
	
	JsonObj->SetNumberField(TEXT("execution_time_ms"), Result.ExecutionTimeMs);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	return JsonString;
}

FString UPythonTools::ConvertModuleInfoToJson(const Revvy::FPythonModuleInfo& Info)
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetBoolField(TEXT("success"), true);
	JsonObj->SetStringField(TEXT("module_name"), Info.ModuleName);

	// Convert classes array
	TArray<TSharedPtr<FJsonValue>> ClassesArray;
	for (const FString& ClassName : Info.Classes)
	{
		ClassesArray.Add(MakeShared<FJsonValueString>(ClassName));
	}
	JsonObj->SetArrayField(TEXT("classes"), ClassesArray);

	// Convert functions array
	TArray<TSharedPtr<FJsonValue>> FunctionsArray;
	for (const FString& FunctionName : Info.Functions)
	{
		FunctionsArray.Add(MakeShared<FJsonValueString>(FunctionName));
	}
	JsonObj->SetArrayField(TEXT("functions"), FunctionsArray);

	// Convert constants array
	TArray<TSharedPtr<FJsonValue>> ConstantsArray;
	for (const FString& ConstantName : Info.Constants)
	{
		ConstantsArray.Add(MakeShared<FJsonValueString>(ConstantName));
	}
	JsonObj->SetArrayField(TEXT("constants"), ConstantsArray);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	return JsonString;
}

namespace
{
	/**
	 * How much of a class discovery is worth sending back.
	 *
	 * The tool used to serialize every method with its complete docstring and no
	 * ceiling. unreal.BlueprintService alone came back at 61,030 characters,
	 * which the caller's harness rejected outright — the user saw a wall of
	 * "result exceeds maximum allowed tokens" boilerplate instead of an answer,
	 * and the run stopped. A reply that cannot be read is not a successful call.
	 *
	 * 24k leaves room for the envelope inside a typical 25k-ish tool budget.
	 */
	constexpr int32 ClassDiscoveryBudgetChars = 24000;

	/** Enough of a docstring to choose a method by; the rest is one call away. */
	constexpr int32 TrimmedDocstringChars = 200;

	/**
	 * First sentence-ish of a docstring, for when the full text will not fit.
	 *
	 * Degrading here rather than by dropping methods is deliberate: the names and
	 * signatures ARE the index a caller needs to pick its next move, while the
	 * prose behind any one of them is recoverable with discover_python_function.
	 * Losing half the method list to keep full prose on the other half would be
	 * the worse trade.
	 */
	FString SummarizeDocstring(const FString& Docstring)
	{
		FString Flat = Docstring;
		Flat.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Flat.ReplaceInline(TEXT("\r"), TEXT(" "));
		Flat.ReplaceInline(TEXT("\n"), TEXT(" "));
		Flat.TrimStartAndEndInline();
		if (Flat.Len() <= TrimmedDocstringChars)
		{
			return Flat;
		}
		return Flat.Left(TrimmedDocstringChars) + TEXT("…");
	}

	FString SerializeClassInfoJson(
		const Revvy::FPythonClassInfo& Info,
		bool bTrimDocstrings,
		int32 MethodLimit)
	{
		TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
		JsonObj->SetBoolField(TEXT("success"), true);
		JsonObj->SetStringField(TEXT("class_name"), Info.Name);
		JsonObj->SetStringField(TEXT("full_path"), Info.FullPath);
		JsonObj->SetStringField(
			TEXT("doc_string"),
			bTrimDocstrings ? SummarizeDocstring(Info.Docstring) : Info.Docstring);

		TArray<TSharedPtr<FJsonValue>> BasesArray;
		for (const FString& BaseName : Info.BaseClasses)
		{
			BasesArray.Add(MakeShared<FJsonValueString>(BaseName));
		}
		JsonObj->SetArrayField(TEXT("base_classes"), BasesArray);

		const int32 TotalMethods = Info.Methods.Num();
		const int32 Emitted = MethodLimit >= 0 ? FMath::Min(MethodLimit, TotalMethods) : TotalMethods;
		TArray<TSharedPtr<FJsonValue>> MethodsArray;
		for (int32 Index = 0; Index < Emitted; ++Index)
		{
			const Revvy::FPythonFunctionInfo& Method = Info.Methods[Index];
			TSharedPtr<FJsonObject> MethodObj = MakeShared<FJsonObject>();
			MethodObj->SetStringField(TEXT("name"), Method.Name);
			MethodObj->SetStringField(TEXT("signature"), Method.Signature);
			MethodObj->SetStringField(
				TEXT("docstring"),
				bTrimDocstrings ? SummarizeDocstring(Method.Docstring) : Method.Docstring);
			MethodsArray.Add(MakeShared<FJsonValueObject>(MethodObj));
		}
		JsonObj->SetArrayField(TEXT("methods"), MethodsArray);

		TArray<TSharedPtr<FJsonValue>> PropertiesArray;
		for (const FString& PropName : Info.Properties)
		{
			PropertiesArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		JsonObj->SetArrayField(TEXT("properties"), PropertiesArray);

		// Said in-band, and only when it applies, so the caller can act on it
		// instead of guessing why the prose looks short.
		if (bTrimDocstrings)
		{
			JsonObj->SetBoolField(TEXT("docstrings_trimmed"), true);
		}
		if (Emitted < TotalMethods)
		{
			JsonObj->SetBoolField(TEXT("truncated"), true);
			JsonObj->SetNumberField(TEXT("total_methods"), TotalMethods);
			JsonObj->SetNumberField(TEXT("returned_methods"), Emitted);
		}
		if (bTrimDocstrings || Emitted < TotalMethods)
		{
			JsonObj->SetStringField(
				TEXT("next_step"),
				TEXT("Narrow with method_filter (e.g. 'create|delete'), or call "
					 "discover_python_function on one method for its full docs."));
		}

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return JsonString;
	}
}

FString UPythonTools::ConvertClassInfoToJson(const Revvy::FPythonClassInfo& Info)
{
	// Full fidelity first: most classes fit, and those callers see no change.
	FString Full = SerializeClassInfoJson(Info, /*bTrimDocstrings=*/false, /*MethodLimit=*/-1);
	if (Full.Len() <= ClassDiscoveryBudgetChars)
	{
		return Full;
	}

	// Docstrings are what blow the budget - 220 characters of signature against
	// a kilobyte of prose each - so shorten those before losing any method.
	FString Trimmed = SerializeClassInfoJson(Info, /*bTrimDocstrings=*/true, /*MethodLimit=*/-1);
	if (Trimmed.Len() <= ClassDiscoveryBudgetChars)
	{
		return Trimmed;
	}

	// Still over: drop methods from the tail, and say how many were held back.
	// Binary search rather than a guessed constant, because how many fit depends
	// on the signatures of this particular class.
	int32 Low = 0;
	int32 High = Info.Methods.Num();
	FString Best = SerializeClassInfoJson(Info, true, 0);
	while (Low <= High)
	{
		const int32 Mid = (Low + High) / 2;
		FString Candidate = SerializeClassInfoJson(Info, true, Mid);
		if (Candidate.Len() <= ClassDiscoveryBudgetChars)
		{
			Best = MoveTemp(Candidate);
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}
	return Best;
}

FString UPythonTools::ConvertFunctionInfoToJson(const Revvy::FPythonFunctionInfo& Info)
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetBoolField(TEXT("success"), true);
	JsonObj->SetStringField(TEXT("function_name"), Info.Name);
	JsonObj->SetStringField(TEXT("signature"), Info.Signature);
	JsonObj->SetStringField(TEXT("doc_string"), Info.Docstring);

	// Convert parameters
	TArray<TSharedPtr<FJsonValue>> ParamsArray;
	for (const FString& ParamName : Info.Parameters)
	{
		ParamsArray.Add(MakeShared<FJsonValueString>(ParamName));
	}
	JsonObj->SetArrayField(TEXT("parameters"), ParamsArray);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	return JsonString;
}
