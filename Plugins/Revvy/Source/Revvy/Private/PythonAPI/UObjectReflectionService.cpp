// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UObjectReflectionService.h"
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogUObjectReflectionService, Log, All);

// ============================================
// Helper Functions
// ============================================

UObject* UObjectReflectionService::ResolveObject(const FString& ObjectPath, FString& OutReason)
{
	OutReason.Reset();

	if (ObjectPath.IsEmpty())
	{
		OutReason = TEXT("ObjectPath is required.");
		return nullptr;
	}

	// Only content paths reach the asset library. UEditorAssetLibrary::DoesAssetExist
	// logs an Error both for names that map to no mount root ("Actor") and for code
	// paths ("/Script/Engine"), which used to make those calls noisy in the log while
	// still returning a value (audit section 2.4). Measured, not assumed: the
	// leading-slash check alone was not enough.
	const bool bIsRootedPath = ObjectPath.StartsWith(TEXT("/"));
	const bool bIsContentPath = bIsRootedPath && !ObjectPath.StartsWith(TEXT("/Script/"));

	if (bIsContentPath && UEditorAssetLibrary::DoesAssetExist(ObjectPath))
	{
		if (UObject* Asset = UEditorAssetLibrary::LoadAsset(ObjectPath))
		{
			return Asset;
		}
	}

	// Try a direct find by object path (already-loaded objects).
	if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		return Found;
	}

	// Try loading by object path. /Script/ paths are fine here — only the asset
	// library rejects them.
	if (bIsRootedPath)
	{
		if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
		{
			return Loaded;
		}
	}

	// Audit section 5.1: a class is NOT an object. Returning its CDO here made a
	// class default indistinguishable from an object's own value — GetProperty
	// returns a bare string with nowhere to say which it got — and made SetProperty
	// write into the CDO from a call the author believed named one object. The
	// capability stays available under its own name instead.
	if (ResolveClass(ObjectPath) != nullptr)
	{
		OutReason = FString::Printf(
			TEXT("'%s' is a class, not an object. Use get_class_default_object_property ")
			TEXT("for class defaults."),
			*ObjectPath);
		return nullptr;
	}

	OutReason = FString::Printf(TEXT("Could not resolve '%s' to an object."), *ObjectPath);
	return nullptr;
}

UClass* UObjectReflectionService::ResolveClass(const FString& ClassNameOrPath)
{
	if (ClassNameOrPath.IsEmpty())
	{
		return nullptr;
	}

	// Try direct find/load by path (e.g. "/Script/Engine.Actor").
	if (UClass* Found = FindObject<UClass>(nullptr, *ClassNameOrPath))
	{
		return Found;
	}
	if (UClass* Loaded = LoadObject<UClass>(nullptr, *ClassNameOrPath))
	{
		return Loaded;
	}

	// Build a set of candidate names (raw + with U/A/F prefixes stripped or added).
	TArray<FString> Candidates;
	Candidates.Add(ClassNameOrPath);

	// If it has a leading prefix, also try the stripped version.
	if (ClassNameOrPath.Len() > 1)
	{
		const TCHAR First = ClassNameOrPath[0];
		if (First == TEXT('U') || First == TEXT('A') || First == TEXT('F'))
		{
			Candidates.Add(ClassNameOrPath.RightChop(1));
		}
	}

	// Iterate all classes searching by name (case-insensitive).
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		const FString ClassName = Class->GetName();

		for (const FString& Candidate : Candidates)
		{
			if (ClassName.Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return Class;
			}
		}
	}

	UE_LOG(LogUObjectReflectionService, Warning, TEXT("ResolveClass: Could not resolve class '%s'"), *ClassNameOrPath);
	return nullptr;
}

bool UObjectReflectionService::IsPropertyEditable(FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	// Must be exposed to edit, and not edit-const / deprecated.
	if (!Property->HasAnyPropertyFlags(CPF_Edit))
	{
		return false;
	}

	if (Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Deprecated))
	{
		return false;
	}

	return true;
}

bool UObjectReflectionService::IsPropertyBlueprintVisible(FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	return Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
}

bool UObjectReflectionService::ResolvePropertyPath(
	UStruct* OwnerStruct,
	void* Container,
	const FString& PropertyPath,
	FProperty*& OutProperty,
	void*& OutValuePtr)
{
	OutProperty = nullptr;
	OutValuePtr = nullptr;

	if (!OwnerStruct || !Container || PropertyPath.IsEmpty())
	{
		return false;
	}

	// Split on '.' to support dotted sub-paths into nested structs.
	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.Num() == 0)
	{
		return false;
	}

	UStruct* CurrentStruct = OwnerStruct;
	void* CurrentContainer = Container;
	FProperty* CurrentProperty = nullptr;
	void* CurrentValuePtr = nullptr;

	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		const FString& Segment = Segments[i];
		if (!CurrentStruct)
		{
			return false;
		}

		CurrentProperty = CurrentStruct->FindPropertyByName(*Segment);
		if (!CurrentProperty)
		{
			UE_LOG(LogUObjectReflectionService, Warning, TEXT("ResolvePropertyPath: Property '%s' not found"), *Segment);
			return false;
		}

		CurrentValuePtr = CurrentProperty->ContainerPtrToValuePtr<void>(CurrentContainer);

		// If there are more segments, descend into the struct.
		if (i < Segments.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(CurrentProperty);
			if (!StructProp)
			{
				UE_LOG(LogUObjectReflectionService, Warning, TEXT("ResolvePropertyPath: '%s' is not a struct; cannot descend"), *Segment);
				return false;
			}

			CurrentStruct = StructProp->Struct;
			CurrentContainer = CurrentValuePtr;
		}
	}

	OutProperty = CurrentProperty;
	OutValuePtr = CurrentValuePtr;
	return OutProperty != nullptr && OutValuePtr != nullptr;
}

// ============================================
// Class Identification
// ============================================

FString UObjectReflectionService::GetClassName(const FString& ObjectPath)
{
	if (ObjectPath.IsEmpty())
	{
		return FString();
	}

	// If the path resolves directly to a class, return its name.
	if (UClass* AsClass = ResolveClass(ObjectPath))
	{
		// Only short-circuit when the input clearly identifies a class (path or known name).
		// ResolveObject handles asset paths; prefer object resolution for asset-looking inputs.
		if (!UEditorAssetLibrary::DoesAssetExist(ObjectPath))
		{
			return AsClass->GetName();
		}
	}

	FString ResolveError;
	UObject* Object = ResolveObject(ObjectPath, ResolveError);
	if (!Object)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetClassName: Could not resolve '%s'"), *ObjectPath);
		return FString();
	}

	UClass* Class = Object->GetClass();
	if (!Class)
	{
		return FString();
	}

	return Class->GetName();
}

// ============================================
// Property Inspection
// ============================================

TArray<FObjectPropertyInfo> UObjectReflectionService::ListProperties(const FString& ObjectPath, bool bEditableOnly)
{
	TArray<FObjectPropertyInfo> Results;

	FString ResolveError;
	UObject* Object = ResolveObject(ObjectPath, ResolveError);
	if (!Object)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("ListProperties: %s"), *ResolveError);
		return Results;
	}

	UClass* Class = Object->GetClass();
	if (!Class)
	{
		return Results;
	}

	for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property)
		{
			continue;
		}

		const bool bEditable = IsPropertyEditable(Property);
		if (bEditableOnly && !bEditable)
		{
			continue;
		}

		FObjectPropertyInfo Info;
		Info.PropertyName = Property->GetName();
		Info.PropertyType = Property->GetCPPType();
		Info.bEditable = bEditable;
		Info.bBlueprintVisible = IsPropertyBlueprintVisible(Property);

		// Export the current value from the object instance.
		FString ValueStr;
		Property->ExportText_InContainer(0, ValueStr, Object, Object, Object, PPF_None);
		Info.CurrentValue = ValueStr;

		Results.Add(Info);
	}

	// Sort by name for stable output.
	Results.Sort([](const FObjectPropertyInfo& A, const FObjectPropertyInfo& B) {
		return A.PropertyName < B.PropertyName;
	});

	return Results;
}

FString UObjectReflectionService::GetProperty(const FString& ObjectPath, const FString& PropertyName)
{
	if (PropertyName.IsEmpty())
	{
		return FString();
	}

	FString ResolveError;
	UObject* Object = ResolveObject(ObjectPath, ResolveError);
	if (!Object)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetProperty: %s"), *ResolveError);
		return FString();
	}

	UClass* Class = Object->GetClass();
	if (!Class)
	{
		return FString();
	}

	// Fast path: simple (non-dotted) property exports directly from the container.
	if (!PropertyName.Contains(TEXT(".")))
	{
		FProperty* Property = Class->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetProperty: Property '%s' not found"), *PropertyName);
			return FString();
		}

		FString ValueStr;
		Property->ExportText_InContainer(0, ValueStr, Object, Object, Object, PPF_None);
		return ValueStr;
	}

	// Dotted sub-path: resolve the innermost property and value pointer.
	FProperty* InnerProperty = nullptr;
	void* InnerValuePtr = nullptr;
	if (!ResolvePropertyPath(Class, Object, PropertyName, InnerProperty, InnerValuePtr))
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetProperty: Could not resolve path '%s'"), *PropertyName);
		return FString();
	}

	FString ValueStr;
	InnerProperty->ExportTextItem_Direct(ValueStr, InnerValuePtr, nullptr, Object, PPF_None);
	return ValueStr;
}

bool UObjectReflectionService::SetProperty(const FString& ObjectPath, const FString& PropertyName, const FString& Value)
{
	if (PropertyName.IsEmpty())
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("SetProperty: PropertyName is required"));
		return false;
	}

	FString ResolveError;
	UObject* Object = ResolveObject(ObjectPath, ResolveError);
	if (!Object)
	{
		// Audit section 5.1: this refusal is what stops an implicit resolution from
		// writing into a class default object.
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("SetProperty: %s"), *ResolveError);
		return false;
	}

	UClass* Class = Object->GetClass();
	if (!Class)
	{
		return false;
	}

	FProperty* Property = Class->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("SetProperty: Property '%s' not found on %s"), *PropertyName, *Class->GetName());
		return false;
	}

	// Notify the object of the impending change.
	FScopedTransaction Transaction(NSLOCTEXT("ObjectReflectionService", "SetProperty", "Set Object Property"));
	Object->Modify();
	Object->PreEditChange(Property);

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

	// ImportText_Direct returns the position past the consumed text (nullptr on failure).
	const TCHAR* ImportResult = Property->ImportText_Direct(*Value, ValuePtr, Object, PPF_None);
	if (ImportResult == nullptr)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("SetProperty: Failed to import value '%s' into '%s'"), *Value, *PropertyName);
		// Still issue a PostEditChange to keep the object consistent.
		FPropertyChangedEvent FailedEvent(Property, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(FailedEvent);
		return false;
	}

	// Notify the object that the property changed.
	FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(ChangedEvent);

	// Mark the package dirty so the change can be saved.
	Object->MarkPackageDirty();

	UE_LOG(LogUObjectReflectionService, Display, TEXT("SetProperty: Set '%s' = '%s' on %s"), *PropertyName, *Value, *Object->GetName());
	return true;
}

// ============================================
// Class Hierarchy
// ============================================

TArray<FString> UObjectReflectionService::SearchSubclasses(const FString& BaseClassName, bool bIncludeAbstract)
{
	TArray<FString> Results;

	UClass* BaseClass = ResolveClass(BaseClassName);
	if (!BaseClass)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("SearchSubclasses: Could not resolve base class '%s'"), *BaseClassName);
		return Results;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class)
		{
			continue;
		}

		// Must descend from the base (IsChildOf includes the base itself).
		if (!Class->IsChildOf(BaseClass))
		{
			continue;
		}

		// Skip the base class itself.
		if (Class == BaseClass)
		{
			continue;
		}

		// Optionally skip abstract classes.
		if (!bIncludeAbstract && Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		// Skip transient/skeleton/reinstanced helper classes.
		if (Class->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			continue;
		}
		const FString ClassName = Class->GetName();
		if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")) || ClassName.StartsWith(TEXT("TRASHCLASS_")))
		{
			continue;
		}

		Results.Add(ClassName);
	}

	Results.Sort();
	return Results;
}

FString UObjectReflectionService::GetClassDefaultObjectProperty(const FString& ClassName, const FString& PropertyName)
{
	if (PropertyName.IsEmpty())
	{
		return FString();
	}

	UClass* Class = ResolveClass(ClassName);
	if (!Class)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetClassDefaultObjectProperty: Could not resolve class '%s'"), *ClassName);
		return FString();
	}

	UObject* CDO = Class->GetDefaultObject();
	if (!CDO)
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetClassDefaultObjectProperty: No CDO for class '%s'"), *ClassName);
		return FString();
	}

	// Fast path: simple (non-dotted) property exports directly from the CDO.
	if (!PropertyName.Contains(TEXT(".")))
	{
		FProperty* Property = Class->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetClassDefaultObjectProperty: Property '%s' not found on %s"), *PropertyName, *Class->GetName());
			return FString();
		}

		FString ValueStr;
		Property->ExportText_InContainer(0, ValueStr, CDO, CDO, CDO, PPF_None);
		return ValueStr;
	}

	// Dotted sub-path: resolve the innermost property and value pointer.
	FProperty* InnerProperty = nullptr;
	void* InnerValuePtr = nullptr;
	if (!ResolvePropertyPath(Class, CDO, PropertyName, InnerProperty, InnerValuePtr))
	{
		UE_LOG(LogUObjectReflectionService, Warning, TEXT("GetClassDefaultObjectProperty: Could not resolve path '%s'"), *PropertyName);
		return FString();
	}

	FString ValueStr;
	InnerProperty->ExportTextItem_Direct(ValueStr, InnerValuePtr, nullptr, CDO, PPF_None);
	return ValueStr;
}
