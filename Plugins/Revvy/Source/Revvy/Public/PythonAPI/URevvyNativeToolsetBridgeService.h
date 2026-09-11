// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "URevvyNativeToolsetBridgeService.generated.h"

/**
 * Fail-closed bridge used by Revvy's optional UE 5.8 ToolsetRegistry adapter.
 *
 * The official Unreal MCP server has no authentication layer in UE 5.8, so this
 * service deliberately exposes only the same read-only scope enforced by
 * FMCPServer. Full-access execution remains available exclusively through
 * Revvy's authenticated MCP endpoint.
 */
UCLASS(BlueprintType)
class REVVY_API URevvyNativeToolsetBridgeService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Return the current Editor instance's unauthenticated-native tool catalogue.
	 *
	 * The result is a JSON array. Only a small C++ allow-list is included; this
	 * is intentionally narrower than Revvy's authenticated ReadOnly surface.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Interop")
	static FString GetReadOnlyCatalogJson();

	/**
	 * Execute one allow-listed Revvy tool through FMCPServer's ReadOnly gate.
	 *
	 * @param ToolName       Registered Revvy tool name.
	 * @param ArgumentsJson JSON object containing the tool arguments.
	 * @return The tool result, or a structured JSON error when the tool/action
	 *         is not allowed in a read-only session.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Interop")
	static FString CallReadOnlyAction(
		const FString& ToolName,
		const FString& ArgumentsJson = TEXT("{}"));
};
