// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UMcpToolExporterService.generated.h"

/**
 * MCP `tools/list` JSON schema exporter for Revvy services.
 *
 * Emits Model Context Protocol (MCP) tool definitions (2025-06-18 shape)
 * for every BlueprintCallable UFUNCTION exposed by the Revvy service
 * layer, so that external MCP hosts (Claude Code, Cursor, ChatGPT, …)
 * can discover Unreal-driven tool surfaces without hand-authored schema.
 *
 * Mirrors the UPythonBindingExportService reflection-based pattern:
 * all UFUNCTIONs are static + BlueprintCallable so they are reachable
 * from Python as `unreal.McpToolExporterService.<snake_case_name>()`.
 *
 * Per-tool schema shape:
 *   {
 *     "name": "<ServiceName>_<UfnName>",
 *     "description": "<Category> - <Tooltip>",
 *     "inputSchema": {
 *       "type": "object",
 *       "properties": { "<param>": { "type": "<jsonSchemaType>", ... }, ... },
 *       "required": [ "<param>", ... ]
 *     }
 *   }
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.McpToolExporterService
 *   path_single = svc.export_service_as_mcp_tools_json("USubsystemQueryService", "D:/mcp/subsys.json")
 *   path_all    = svc.export_all_services_as_mcp_tools_json("D:/mcp/tools.json")
 */
UCLASS(BlueprintType)
class REVVY_API UMcpToolExporterService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Export the BlueprintCallable UFUNCTIONs of a single Revvy service as
	 * an MCP `tools/list` JSON array (one tool object per UFUNCTION).
	 *
	 * @param ServiceClassName  Bare class name (e.g. "USubsystemQueryService").
	 * @param OutputPath        Absolute path to the .json file to write.
	 * @return                  The absolute path that was written, or "" on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|McpExport|Export")
	static FString ExportServiceAsMcpToolsJson(const FString& ServiceClassName, const FString& OutputPath);

	/**
	 * Export every Revvy "U*Service" class into one combined MCP tool
	 * catalogue file. Every BlueprintCallable UFUNCTION contributes one
	 * tool object; the resulting document is a single JSON array.
	 *
	 * @param OutputPath  Absolute path to the .json file to write.
	 * @return            The absolute path that was written, or "" on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|McpExport|Export")
	static FString ExportAllServicesAsMcpToolsJson(const FString& OutputPath);
};
