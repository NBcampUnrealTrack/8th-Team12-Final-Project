// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UServiceRegistryService.h"

// Editor / Engine
#include "Editor.h"

// UObject reflection
#include "UObject/Class.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyServiceRegistry, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON array to a compact FString. */
	FString ServiceRegistryJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** True if the class was cooked from the Revvy plugin module. */
	bool ServiceRegistryIsRevvyClass(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString PathName = Cls->GetPathName();
		return PathName.StartsWith(TEXT("/Script/Revvy."));
	}

	/** True if the class name matches the "U*Service" service convention. */
	bool ServiceRegistryIsServiceClassName(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString Name = Cls->GetName();
		// UHT strips the leading 'U' from GetName(); check original CPP name via prefix.
		// Cls->GetPrefixCPP() returns "U" for UObject-derived. GetName() itself
		// therefore does NOT carry the 'U'. We accept classes whose original
		// C++ symbol is "U*Service" — i.e. any non-actor class ending in "Service".
		const FString Prefix = Cls->GetPrefixCPP();
		return Prefix == TEXT("U") && Name.EndsWith(TEXT("Service"));
	}

	/** Count BlueprintCallable UFUNCTIONs declared on Cls (including inherited). */
	int32 ServiceRegistryCountBlueprintCallableUFunctions(UClass* Cls)
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

	/** Build a compact "ReturnType FunctionName(ParamType ParamName, ...)" signature. */
	FString BuildFunctionSignature(UFunction* Fn)
	{
		if (!Fn) { return FString(); }

		FString ReturnTypeStr = TEXT("void");
		TArray<FString> ParamStrs;

		for (TFieldIterator<FProperty> PropIt(Fn); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) { continue; }

			const FString CppType = Prop->GetCPPType();
			if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnTypeStr = CppType;
			}
			else if (Prop->HasAnyPropertyFlags(CPF_Parm))
			{
				ParamStrs.Add(FString::Printf(TEXT("%s %s"), *CppType, *Prop->GetName()));
			}
		}

		return FString::Printf(TEXT("%s %s(%s)"),
			*ReturnTypeStr,
			*Fn->GetName(),
			*FString::Join(ParamStrs, TEXT(", ")));
	}

	/** Count non-return params on a UFunction. */
	int32 CountParams(UFunction* Fn)
	{
		if (!Fn) { return 0; }
		int32 N = 0;
		for (TFieldIterator<FProperty> PropIt(Fn); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (Prop && Prop->HasAnyPropertyFlags(CPF_Parm) && !Prop->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				++N;
			}
		}
		return N;
	}

	/** Read the "Category" meta for a UFUNCTION (empty if unset). */
	FString GetFunctionCategory(UFunction* Fn)
	{
#if WITH_EDITOR
		if (Fn && Fn->HasMetaData(TEXT("Category")))
		{
			return Fn->GetMetaData(TEXT("Category"));
		}
#endif
		return FString();
	}

	/** Gather every Revvy "U*Service" class currently loaded. */
	void ServiceRegistryCollectRevvyServiceClasses(TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls) { continue; }
			if (!ServiceRegistryIsRevvyClass(Cls)) { continue; }
			if (!ServiceRegistryIsServiceClassName(Cls)) { continue; }
			OutClasses.Add(Cls);
		}
	}
}

// =================================================================
// ListRevvyServices
// =================================================================

FString UServiceRegistryService::ListRevvyServices()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	TArray<UClass*> ServiceClasses;
	ServiceRegistryCollectRevvyServiceClasses(ServiceClasses);

	Entries.Reserve(ServiceClasses.Num());
	for (UClass* Cls : ServiceClasses)
	{
		if (!Cls) { continue; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class_name"), Cls->GetName());
		Entry->SetStringField(TEXT("class_path"), Cls->GetPathName());
		Entry->SetNumberField(TEXT("ufunction_count"), ServiceRegistryCountBlueprintCallableUFunctions(Cls));

		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	UE_LOG(LogRevvyServiceRegistry, Verbose, TEXT("ListRevvyServices: found %d services."), Entries.Num());
	return ServiceRegistryJsonArrayToString(Entries);
}

// =================================================================
// GetServiceUFunctions
// =================================================================

FString UServiceRegistryService::GetServiceUFunctions(const FString& ServiceClassName)
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	if (ServiceClassName.IsEmpty())
	{
		UE_LOG(LogRevvyServiceRegistry, Warning, TEXT("GetServiceUFunctions: empty ServiceClassName."));
		return ServiceRegistryJsonArrayToString(Entries);
	}

	// Path form is "/Script/Revvy.<Name>" (no leading 'U' — GetName() strips prefix).
	FString BareName = ServiceClassName;
	if (BareName.StartsWith(TEXT("U")))
	{
		BareName.RemoveAt(0, 1);
	}
	const FString FullPath = FString::Printf(TEXT("/Script/Revvy.%s"), *BareName);

	UClass* Cls = FindObject<UClass>(nullptr, *FullPath);
	if (!Cls)
	{
		UE_LOG(LogRevvyServiceRegistry, Warning,
			TEXT("GetServiceUFunctions: class not found at '%s'."), *FullPath);
		return ServiceRegistryJsonArrayToString(Entries);
	}

	for (TFieldIterator<UFunction> It(Cls); It; ++It)
	{
		UFunction* Fn = *It;
		if (!Fn) { continue; }
		if (!Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable)) { continue; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),       Fn->GetName());
		Entry->SetStringField(TEXT("signature"),  BuildFunctionSignature(Fn));
		Entry->SetStringField(TEXT("category"),   GetFunctionCategory(Fn));
		Entry->SetNumberField(TEXT("num_params"), CountParams(Fn));

		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return ServiceRegistryJsonArrayToString(Entries);
}

// =================================================================
// SearchServiceByCategory
// =================================================================

FString UServiceRegistryService::SearchServiceByCategory(const FString& CategorySubstring)
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	if (CategorySubstring.IsEmpty())
	{
		UE_LOG(LogRevvyServiceRegistry, Warning, TEXT("SearchServiceByCategory: empty CategorySubstring."));
		return ServiceRegistryJsonArrayToString(Entries);
	}

	TArray<UClass*> ServiceClasses;
	ServiceRegistryCollectRevvyServiceClasses(ServiceClasses);

	for (UClass* Cls : ServiceClasses)
	{
		if (!Cls) { continue; }

		for (TFieldIterator<UFunction> It(Cls); It; ++It)
		{
			UFunction* Fn = *It;
			if (!Fn) { continue; }
			if (!Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable)) { continue; }

			const FString Cat = GetFunctionCategory(Fn);
			if (Cat.IsEmpty()) { continue; }

			if (!Cat.Contains(CategorySubstring, ESearchCase::IgnoreCase)) { continue; }

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("service_name"), Cls->GetName());
			Entry->SetStringField(TEXT("ufn_name"),     Fn->GetName());
			Entry->SetStringField(TEXT("category"),     Cat);

			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	UE_LOG(LogRevvyServiceRegistry, Verbose,
		TEXT("SearchServiceByCategory('%s'): %d matches."), *CategorySubstring, Entries.Num());
	return ServiceRegistryJsonArrayToString(Entries);
}
