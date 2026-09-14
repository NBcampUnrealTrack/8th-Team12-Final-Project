// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ToolMetadata.generated.h"

class UFunction;
class UClass;
class FProperty;
class FJsonObject;

/**
 * Metadata for a single tool parameter
 */
USTRUCT()
struct REVVY_API FToolParameter
{
	GENERATED_BODY()

	/** Parameter name (as exposed to AI) */
	UPROPERTY()
	FString Name;

	/** Human-readable description */
	UPROPERTY()
	FString Description;

	/** JSON Schema type: string, integer, number, boolean, object, array, or null. */
	UPROPERTY()
	FString Type;

	/** Whether this parameter is required */
	UPROPERTY()
	bool bRequired = false;

	/** Default value (as JSON string) */
	UPROPERTY()
	FString DefaultValue;

	/** Allowed values for enum-like parameters */
	UPROPERTY()
	TArray<FString> AllowedValues;

	/** Item type for array parameters (required for Google/Gemini compatibility) */
	UPROPERTY()
	FString ArrayItemType;

	FToolParameter()
		: bRequired(false)
	{
	}

	/** Normalize legacy Unreal-style aliases to valid JSON Schema primitive types. */
	static FString NormalizeJsonSchemaType(const FString& InType)
	{
		const FString Normalized = InType.TrimStartAndEnd().ToLower();
		if (Normalized == TEXT("int"))
		{
			return TEXT("integer");
		}
		if (Normalized == TEXT("float") || Normalized == TEXT("double"))
		{
			return TEXT("number");
		}
		if (Normalized == TEXT("bool"))
		{
			return TEXT("boolean");
		}
		if (Normalized == TEXT("str"))
		{
			return TEXT("string");
		}
		if (Normalized == TEXT("list"))
		{
			return TEXT("array");
		}
		if (Normalized == TEXT("dict"))
		{
			return TEXT("object");
		}
		if (Normalized == TEXT("string")
			|| Normalized == TEXT("integer")
			|| Normalized == TEXT("number")
			|| Normalized == TEXT("boolean")
			|| Normalized == TEXT("object")
			|| Normalized == TEXT("array")
			|| Normalized == TEXT("null"))
		{
			return Normalized;
		}
		return TEXT("string");
	}

	// Constructor for use with TOOL_PARAM macros
	FToolParameter(const FString& InName, const FString& InDesc, const FString& InType, bool InRequired, const FString& InDefault = TEXT(""))
		: Name(InName)
		, Description(InDesc)
		, Type(NormalizeJsonSchemaType(InType))
		, bRequired(InRequired)
		, DefaultValue(InDefault)
	{
		// Default array item type to string if not specified
		if (Type == TEXT("array"))
		{
			ArrayItemType = TEXT("string");
		}
	}

	// Constructor for array parameters with explicit item type
	FToolParameter(const FString& InName, const FString& InDesc, const FString& InType, bool InRequired, const FString& InDefault, const FString& InArrayItemType)
		: Name(InName)
		, Description(InDesc)
		, Type(NormalizeJsonSchemaType(InType))
		, bRequired(InRequired)
		, DefaultValue(InDefault)
		, ArrayItemType(NormalizeJsonSchemaType(InArrayItemType))
	{
	}
};

/**
 * MCP tool annotations (spec 2025-03-26, carried forward in 2025-11-25 + 2026-07-28-RC).
 * Mirrors the canonical "annotations" object on a tool definition.
 *
 * Defaults match the MCP spec defaults so that unannotated tools keep the
 * conservative client treatment (destructive, openWorld). Mark each tool
 * explicitly with TOOL_ANNOTATIONS_READ_ONLY / _DESTRUCTIVE / _IDEMPOTENT etc.
 */
USTRUCT()
struct REVVY_API FToolAnnotations
{
	GENERATED_BODY()

	/** Human-readable title for the tool (shown in UI). Empty → client falls back to Name. */
	UPROPERTY()
	FString Title;

	/** Tool only reads data; never writes. Default per spec: false. */
	UPROPERTY()
	bool bReadOnlyHint = false;

	/** Tool may perform destructive (irreversible) writes. Default per spec: true. */
	UPROPERTY()
	bool bDestructiveHint = true;

	/** Repeat calls with same args yield same result. Default per spec: false. */
	UPROPERTY()
	bool bIdempotentHint = false;

	/** Tool can talk to systems outside the local editor (web, cloud). Default per spec: true. */
	UPROPERTY()
	bool bOpenWorldHint = true;

	/** True if this annotations block was set explicitly (rather than relying on defaults). */
	UPROPERTY()
	bool bAnnotated = false;

	/**
	 * For multi-action tools, the `action` values that only read.
	 *
	 * A tool whose verbs are mixed cannot be readOnlyHint as a whole, which used to
	 * hide its queries from a read-only session entirely. Listing the safe verbs here
	 * exposes the tool to such a session with those verbs only; every other action is
	 * refused. Values are compared lower-cased and trimmed, matching how the handlers
	 * normalise the argument.
	 */
	UPROPERTY()
	TArray<FString> ReadOnlyActions;
};

/**
 * Metadata for a single AI tool
 */
USTRUCT()
struct REVVY_API FToolMetadata
{
	GENERATED_BODY()

	/** Tool name (as exposed to AI) */
	UPROPERTY()
	FString Name;

	/** Human-readable description */
	UPROPERTY()
	FString Description;

	/** Tool category for organization */
	UPROPERTY()
	FString Category;

	/** Example usage strings */
	UPROPERTY()
	TArray<FString> Examples;

	/** Tool parameters */
	UPROPERTY()
	TArray<FToolParameter> Parameters;

	/**
	 * If true, this tool is only available to Revvy internal chat.
	 * It will NOT be exposed via MCP to external clients (e.g., VS Code Copilot).
	 * Use for tools that need direct access to chat session state (e.g., attach_image).
	 */
	UPROPERTY()
	bool bInternalOnly = false;

	/** MCP tool annotations — emitted as the "annotations" object in tools/list. */
	UPROPERTY()
	FToolAnnotations Annotations;

	/**
	 * Optional JSON Schema for structured tool output (MCP spec 2025-06-18).
	 * Emitted in tools/list as `outputSchema`. When set, the server will additionally
	 * try to parse the tool result as JSON and emit it under `structuredContent` in
	 * tools/call responses (free-form text falls back to `content[text]`).
	 * Not serialized — set in code via FToolRegistry::SetToolOutputSchema.
	 */
	TSharedPtr<FJsonObject> OutputSchema;

	/** Reflection function pointer (not serialized) */
	UFunction* Function = nullptr;

	/** Class containing the tool (not serialized) */
	UClass* ToolClass = nullptr;

	FToolMetadata()
		: bInternalOnly(false)
		, Function(nullptr)
		, ToolClass(nullptr)
	{
	}
};

