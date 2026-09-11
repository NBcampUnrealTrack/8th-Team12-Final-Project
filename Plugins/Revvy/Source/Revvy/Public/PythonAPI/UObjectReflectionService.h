// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UObjectReflectionService.generated.h"

/**
 * Information about a single reflected UObject property.
 */
USTRUCT(BlueprintType)
struct FObjectPropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Reflection")
	FString PropertyName;

	/** The C++ type of the property (e.g. "int32", "FString", "UTexture2D*") */
	UPROPERTY(BlueprintReadWrite, Category = "Reflection")
	FString PropertyType;

	UPROPERTY(BlueprintReadWrite, Category = "Reflection")
	bool bEditable = false;

	UPROPERTY(BlueprintReadWrite, Category = "Reflection")
	bool bBlueprintVisible = false;

	/** The current value, exported to text via ExportText_InContainer */
	UPROPERTY(BlueprintReadWrite, Category = "Reflection")
	FString CurrentValue;
};

/**
 * Generic UObject / UClass reflection service exposed directly to Python.
 *
 * Mirrors Epic-native ObjectTools: inspect and mutate arbitrary UObjects (and their
 * classes) located by asset path or object path, using UE reflection (FProperty,
 * TFieldIterator, TObjectIterator).
 *
 * Provides 6 reflection actions:
 * - get_class_name: Resolve an object/class path to its class name
 * - list_properties: Enumerate the properties of an object's class (with values)
 * - get_property: Read a single property value (supports dotted sub-paths)
 * - set_property: Write a single property value (with PreEditChange/PostEditChange)
 * - search_subclasses: Find all UClasses descended from a named base class
 * - get_class_default_object_property: Read a property value from a class CDO
 *
 * Python Usage:
 *   import unreal
 *
 *   # Get the class name of an asset or class
 *   name = unreal.ObjectReflectionService.get_class_name("/Game/Data/DT_Items")
 *   cls  = unreal.ObjectReflectionService.get_class_name("/Script/Engine.StaticMeshActor")
 *
 *   # List the editable properties of an object
 *   props = unreal.ObjectReflectionService.list_properties("/Game/Data/DT_Items", True)
 *   for p in props:
 *       print(p.property_name, p.property_type, p.current_value)
 *
 *   # Read / write a single property
 *   value = unreal.ObjectReflectionService.get_property("/Game/MyAsset", "SomeFloat")
 *   ok    = unreal.ObjectReflectionService.set_property("/Game/MyAsset", "SomeFloat", "42.0")
 *
 *   # Find all subclasses of a base class
 *   subs = unreal.ObjectReflectionService.search_subclasses("Actor", False)
 *
 *   # Read a property from a class default object (CDO)
 *   cdo_val = unreal.ObjectReflectionService.get_class_default_object_property("PointLight", "Intensity")
 *
 * @note This replaces the JSON-based ObjectTools MCP toolset.
 */
UCLASS(BlueprintType)
class REVVY_API UObjectReflectionService : public UObject
{
	GENERATED_BODY()

public:
	// ============================================
	// Class Identification
	// ============================================

	/**
	 * Resolve an object or class path to its class name.
	 * For an asset/object path, returns the loaded object's GetClass()->GetName().
	 * For a class path (e.g. "/Script/Engine.Actor" or "Actor"), returns that class name.
	 *
	 * @param ObjectPath - Asset path, object path, or class name
	 * @return Class name, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Reflection")
	static FString GetClassName(const FString& ObjectPath);

	// ============================================
	// Property Inspection
	// ============================================

	/**
	 * List the properties declared on an object's class (including inherited).
	 * Iterates TFieldIterator<FProperty> over the resolved object's class and reports
	 * type, edit/blueprint visibility flags, and the current exported value.
	 *
	 * @param ObjectPath - Object path (asset path or live object path), never a class name
	 * @param bEditableOnly - When true, only return properties editable in the details panel
	 * @return Array of property info (empty on failure)
	 *
	 * @note ObjectPath is an **object path only** — an asset path or the path of a
	 *       live object. A class name is refused, because a class default is not
	 *       this object's value. For class defaults call
	 *       GetClassDefaultObjectProperty, which says so in its name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Reflection")
	static TArray<FObjectPropertyInfo> ListProperties(const FString& ObjectPath, bool bEditableOnly = true);

	/**
	 * Read a single property value from an object.
	 * Supports dotted sub-paths into nested struct properties (e.g. "RelativeLocation.X").
	 *
	 * @param ObjectPath - Object path (asset path or live object path), never a class name
	 * @param PropertyName - Property name, optionally dotted for nested structs
	 * @return Exported value as a string, or empty string on failure
	 *
	 * @note ObjectPath is an **object path only** — an asset path or the path of a
	 *       live object. A class name is refused, because a class default is not
	 *       this object's value. For class defaults call
	 *       GetClassDefaultObjectProperty, which says so in its name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Reflection")
	static FString GetProperty(const FString& ObjectPath, const FString& PropertyName);

	/**
	 * Set a single property value on an object.
	 * Uses ImportText to parse the value, wrapped in PreEditChange/PostEditChange,
	 * and marks the owning package dirty.
	 *
	 * @param ObjectPath - Object path (asset path or live object path), never a class name
	 * @param PropertyName - Property name (top-level only)
	 * @param Value - Value to import (UE text format)
	 * @return True if the value was set successfully
	 *
	 * @note ObjectPath is an **object path only** — an asset path or the path of a
	 *       live object. A class name is refused, because a class default is not
	 *       this object's value. For class defaults call
	 *       GetClassDefaultObjectProperty, which says so in its name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Reflection")
	static bool SetProperty(const FString& ObjectPath, const FString& PropertyName, const FString& Value);

	// ============================================
	// Class Hierarchy
	// ============================================

	/**
	 * Find all UClasses that derive from a named base class.
	 * Resolves the base class by name (with or without U/A prefix, or by class path),
	 * then iterates TObjectIterator<UClass> collecting children.
	 *
	 * @param BaseClassName - Base class name (e.g. "Actor", "UObject") or class path
	 * @param bIncludeAbstract - When true, include abstract classes in the result
	 * @return Array of subclass names (sorted), empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Reflection")
	static TArray<FString> SearchSubclasses(const FString& BaseClassName, bool bIncludeAbstract = false);

	/**
	 * Read a property value from a class's Class Default Object (CDO).
	 *
	 * @param ClassName - Class name (e.g. "PointLight") or class path
	 * @param PropertyName - Property name, optionally dotted for nested structs
	 * @return Exported value as a string, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Reflection")
	static FString GetClassDefaultObjectProperty(const FString& ClassName, const FString& PropertyName);

private:
	/**
	 * Resolve an asset/object path to a live UObject.
	 *
	 * Deliberately does NOT fall back to a class's CDO. A class name returns
	 * nullptr with OutReason naming GetClassDefaultObjectProperty instead — see
	 * docs/design/unreal-cdo-readback-audit.md section 5.1. Returning the CDO made
	 * a class default indistinguishable from an object's own value in GetProperty's
	 * bare-string return, and let SetProperty write into a CDO by accident.
	 */
	static UObject* ResolveObject(const FString& ObjectPath, FString& OutReason);

	/** Resolve a class name or path to a UClass (handles U/A prefixes, name search, path load). */
	static UClass* ResolveClass(const FString& ClassNameOrPath);

	/** Whether a property is considered editable in the details panel. */
	static bool IsPropertyEditable(FProperty* Property);

	/** Whether a property is visible to Blueprint. */
	static bool IsPropertyBlueprintVisible(FProperty* Property);

	/**
	 * Resolve a (possibly dotted) property path against a container, returning the
	 * innermost property and value pointer. Returns false on failure.
	 */
	static bool ResolvePropertyPath(UStruct* OwnerStruct, void* Container, const FString& PropertyPath,
		FProperty*& OutProperty, void*& OutValuePtr);
};
