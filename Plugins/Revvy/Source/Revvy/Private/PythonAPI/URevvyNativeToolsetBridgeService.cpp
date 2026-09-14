// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/URevvyNativeToolsetBridgeService.h"

#include "MCP/MCPServer.h"

FString URevvyNativeToolsetBridgeService::GetReadOnlyCatalogJson()
{
	if (!IsInGameThread())
	{
		return TEXT("[]");
	}
	return FMCPServer::Get().GetUnauthenticatedNativeToolCatalogJson();
}

FString URevvyNativeToolsetBridgeService::CallReadOnlyAction(
	const FString& ToolName,
	const FString& ArgumentsJson)
{
	if (!IsInGameThread())
	{
		return TEXT("{\"success\":false,\"error_code\":\"GAME_THREAD_REQUIRED\",\"error\":\"Native tool execution must run on the Unreal game thread\"}");
	}
	return FMCPServer::Get().ExecuteUnauthenticatedNativeToolInProcess(
		ToolName,
		ArgumentsJson);
}
