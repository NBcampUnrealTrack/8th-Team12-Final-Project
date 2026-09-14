// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UServiceHealthCheckService.h"

// Editor / Engine
#include "Editor.h"

// UObject reflection
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyHealthCheck, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON array to a compact FString. */
	FString ServiceHealthJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Serialize a single JSON object to a compact FString. */
	FString ServiceHealthJsonObjectToString(const TSharedRef<FJsonObject>& JsonObj)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj, Writer);
		return OutStr;
	}

	/** True if the class was cooked from the Revvy plugin module. */
	bool ServiceHealthIsRevvyClass(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString PathName = Cls->GetPathName();
		return PathName.StartsWith(TEXT("/Script/Revvy."));
	}

	/** True if the class name matches the "U*Service" service convention. */
	bool ServiceHealthIsServiceClassName(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString Name = Cls->GetName();
		const FString Prefix = Cls->GetPrefixCPP();
		return Prefix == TEXT("U") && Name.EndsWith(TEXT("Service"));
	}

	/** Count BlueprintCallable UFUNCTIONs declared on Cls (including inherited). */
	int32 ServiceHealthCountBlueprintCallableUFunctions(UClass* Cls)
	{
		int32 Count = 0;
		if (!Cls) { return 0; }
		for (TFieldIterator<UFunction> It(Cls); It; ++It)
		{
			UFunction* Fn = *It;
			if (Fn && Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable))
			{
				++Count;
			}
		}
		return Count;
	}

	/** Return the first BlueprintCallable UFUNCTION on Cls, or nullptr. */
	UFunction* GetFirstBlueprintCallableUFunction(UClass* Cls)
	{
		if (!Cls) { return nullptr; }
		for (TFieldIterator<UFunction> It(Cls); It; ++It)
		{
			UFunction* Fn = *It;
			if (Fn && Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable))
			{
				return Fn;
			}
		}
		return nullptr;
	}

	/**
	 * Signature parseability probe: iterate the UFunction's FProperty chain
	 * and confirm that every parm exposes a non-empty CPP type string. This
	 * exercises the same reflection path Python would use to marshal args.
	 */
	bool IsSignatureParseable(UFunction* Fn)
	{
		if (!Fn) { return false; }
		bool bSawAnyProp = false;
		for (TFieldIterator<FProperty> PropIt(Fn); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) { continue; }
			if (!Prop->HasAnyPropertyFlags(CPF_Parm)) { continue; }
			bSawAnyProp = true;
			if (Prop->GetCPPType().IsEmpty()) { return false; }
		}
		// A zero-arg, void-return UFN is still parseable.
		(void)bSawAnyProp;
		return true;
	}

	/** Gather every Revvy "U*Service" class currently loaded. */
	void ServiceHealthCollectRevvyServiceClasses(TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls) { continue; }
			if (!ServiceHealthIsRevvyClass(Cls)) { continue; }
			if (!ServiceHealthIsServiceClassName(Cls)) { continue; }
			OutClasses.Add(Cls);
		}
	}

	/**
	 * Run all four reflection checks against Cls and populate the given
	 * JSON object with {service, ufn_count, status, note}.
	 */
	void FillHealthEntry(UClass* Cls, const TSharedRef<FJsonObject>& OutEntry)
	{
		if (!Cls)
		{
			OutEntry->SetStringField(TEXT("service"),   TEXT(""));
			OutEntry->SetNumberField(TEXT("ufn_count"), 0);
			OutEntry->SetStringField(TEXT("status"),    TEXT("error"));
			OutEntry->SetStringField(TEXT("note"),      TEXT("class not loaded"));
			return;
		}

		const FString ServiceName = Cls->GetName();
		const int32   UfnCount    = ServiceHealthCountBlueprintCallableUFunctions(Cls);

		OutEntry->SetStringField(TEXT("service"),   ServiceName);
		OutEntry->SetNumberField(TEXT("ufn_count"), UfnCount);

		// CDO check.
		UObject* CDO = Cls->GetDefaultObject();
		if (!CDO)
		{
			OutEntry->SetStringField(TEXT("status"), TEXT("error"));
			OutEntry->SetStringField(TEXT("note"),   TEXT("CDO could not be constructed"));
			return;
		}

		if (UfnCount <= 0)
		{
			OutEntry->SetStringField(TEXT("status"), TEXT("warning"));
			OutEntry->SetStringField(TEXT("note"),   TEXT("no BlueprintCallable UFUNCTIONs"));
			return;
		}

		// Signature parseability of the first UFN.
		UFunction* FirstFn = GetFirstBlueprintCallableUFunction(Cls);
		if (!IsSignatureParseable(FirstFn))
		{
			OutEntry->SetStringField(TEXT("status"), TEXT("warning"));
			OutEntry->SetStringField(TEXT("note"),   TEXT("first UFN signature not parseable"));
			return;
		}

		OutEntry->SetStringField(TEXT("status"), TEXT("ok"));
		OutEntry->SetStringField(TEXT("note"),   TEXT(""));
	}

	/** Resolve a Revvy service class by bare name (leading 'U' optional). */
	UClass* ServiceHealthResolveRevvyServiceClass(const FString& ServiceClassName)
	{
		if (ServiceClassName.IsEmpty()) { return nullptr; }

		FString BareName = ServiceClassName;
		if (BareName.StartsWith(TEXT("U")))
		{
			BareName.RemoveAt(0, 1);
		}
		const FString FullPath = FString::Printf(TEXT("/Script/Revvy.%s"), *BareName);
		return FindObject<UClass>(nullptr, *FullPath);
	}
}

// =================================================================
// HealthCheckAllServices
// =================================================================

FString UServiceHealthCheckService::HealthCheckAllServices()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	TArray<UClass*> ServiceClasses;
	ServiceHealthCollectRevvyServiceClasses(ServiceClasses);

	Entries.Reserve(ServiceClasses.Num());
	for (UClass* Cls : ServiceClasses)
	{
		if (!Cls) { continue; }

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		FillHealthEntry(Cls, Entry);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	UE_LOG(LogRevvyHealthCheck, Verbose,
		TEXT("HealthCheckAllServices: checked %d services."), Entries.Num());
	return ServiceHealthJsonArrayToString(Entries);
}

// =================================================================
// HealthCheckService
// =================================================================

FString UServiceHealthCheckService::HealthCheckService(const FString& ServiceClassName)
{
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();

	if (ServiceClassName.IsEmpty())
	{
		UE_LOG(LogRevvyHealthCheck, Warning,
			TEXT("HealthCheckService: empty ServiceClassName."));
		Entry->SetStringField(TEXT("service"),   TEXT(""));
		Entry->SetNumberField(TEXT("ufn_count"), 0);
		Entry->SetStringField(TEXT("status"),    TEXT("error"));
		Entry->SetStringField(TEXT("note"),      TEXT("empty service class name"));
		return ServiceHealthJsonObjectToString(Entry);
	}

	UClass* Cls = ServiceHealthResolveRevvyServiceClass(ServiceClassName);
	if (!Cls)
	{
		UE_LOG(LogRevvyHealthCheck, Warning,
			TEXT("HealthCheckService: class not found for '%s'."), *ServiceClassName);
		Entry->SetStringField(TEXT("service"),   ServiceClassName);
		Entry->SetNumberField(TEXT("ufn_count"), 0);
		Entry->SetStringField(TEXT("status"),    TEXT("error"));
		Entry->SetStringField(TEXT("note"),      TEXT("class not loaded"));
		return ServiceHealthJsonObjectToString(Entry);
	}

	if (!ServiceHealthIsRevvyClass(Cls) || !ServiceHealthIsServiceClassName(Cls))
	{
		UE_LOG(LogRevvyHealthCheck, Warning,
			TEXT("HealthCheckService: '%s' is not a Revvy U*Service class."), *ServiceClassName);
		Entry->SetStringField(TEXT("service"),   Cls->GetName());
		Entry->SetNumberField(TEXT("ufn_count"), 0);
		Entry->SetStringField(TEXT("status"),    TEXT("error"));
		Entry->SetStringField(TEXT("note"),      TEXT("class is not a Revvy U*Service"));
		return ServiceHealthJsonObjectToString(Entry);
	}

	FillHealthEntry(Cls, Entry);
	return ServiceHealthJsonObjectToString(Entry);
}

// =================================================================
// IsClassLoaded
// =================================================================

bool UServiceHealthCheckService::IsClassLoaded(const FString& ServiceClassName)
{
	if (ServiceClassName.IsEmpty()) { return false; }

	FString BareName = ServiceClassName;
	if (BareName.StartsWith(TEXT("U")))
	{
		BareName.RemoveAt(0, 1);
	}
	const FString FullPath = FString::Printf(TEXT("/Script/Revvy.%s"), *BareName);

	UClass* Cls = FindObject<UClass>(nullptr, *FullPath);
	return Cls != nullptr;
}
