// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Templates/UniquePtr.h"
#include "MCP/BridgeAuth.h"
#include <atomic>

DECLARE_LOG_CATEGORY_EXTERN(LogMCPServer, Log, All);

// Forward declarations
class FSocket;
class FTcpListener;
class FJsonObject;
class FJsonValue;
struct FMCPTool;

enum class EMCPAccessScope : uint8
{
    None,
    ReadOnly,
    Full
};

/**
 * MCP Host Configuration
 * Settings for exposing internal tools to external clients via Streamable HTTP
 * Note: Named FMCPHostConfig to avoid collision with FMCPServerConfig (client connection config)
 */
struct FMCPHostConfig
{
    /** Whether the MCP server is enabled */
    bool bEnabled = true;
    
    /** Port to listen on */
    int32 Port = 8088;
    
    /** Owner-only key used only to authenticate exact proxy/editor messages. */
    FString BridgeHmacKey;

    /** Ephemeral read-only key published for Safe clients */
    FString ReadOnlyApiKey;
};

/**
 * Represents a pending HTTP request to process on the game thread
 */
struct FMCPPendingRequest
{
    FString Body;
    FString SessionId;
    FString Response;
    EMCPAccessScope AccessScope = EMCPAccessScope::None;
    bool bIsNotification = false;
    TAtomic<bool> bCancelled{false};
    FEvent* CompletionEvent = nullptr;

    FMCPPendingRequest()
        : CompletionEvent(FPlatformProcess::GetSynchEventFromPool(false))
    {
    }

    ~FMCPPendingRequest()
    {
        if (CompletionEvent)
        {
            FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
            CompletionEvent = nullptr;
        }
    }
};

/**
 * Represents an active SSE stream connection
 */
struct FMCPSSEConnection
{
    FSocket* ClientSocket = nullptr;
    FString SessionId;
    FDateTime ConnectedAt;
    int32 LastEventId = 0;
    bool bIsActive = true;
};

/**
 * MCP Server - Exposes internal tools to external chat clients
 * 
 * Implements the Model Context Protocol (MCP) Streamable HTTP transport
 * allowing clients like VS Code, Cursor, and Claude Code to use
 * Revvy's internal tools.
 * 
 * @see https://modelcontextprotocol.io/specification/2025-06-18/basic/transports#streamable-http
 */
class REVVY_API FMCPServer : public FRunnable
{
public:
    FMCPServer();
    virtual ~FMCPServer();
    
    /** Get singleton instance */
    static FMCPServer& Get();
    
    /** Initialize the server (loads config, optionally starts if enabled) */
    void Initialize();
    
    /** Shutdown the server */
    void Shutdown();
    
    /** Start the HTTP server */
    bool Start();
    
    /** Stop the HTTP server */
    void StopServer();
    
    /** Check if server is running */
    bool IsRunning() const { return bIsRunning; }
    
    /** Get current configuration */
    const FMCPHostConfig& GetConfig() const { return Config; }
    
    /** Get the server URL for display */
    FString GetServerUrl() const;
    
    // ============ FRunnable Interface ============
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override { bShouldStop = true; }
    virtual void Exit() override;
    
    // ============ Config Persistence ============
    
    /** Get MCP Server enabled state from config */
    static bool GetEnabledFromConfig();
    
    /** Save MCP Server enabled state to config */
    static void SaveEnabledToConfig(bool bEnabled);
    
    /** Get MCP Server port from config */
    static int32 GetPortFromConfig();
    
    /** Save MCP Server port to config */
    static void SavePortToConfig(int32 Port);
    
    /** Get the protected bridge HMAC key (legacy API name retained for callers). */
    static FString GetApiKeyFromConfig();
    
    /** Save the protected bridge HMAC key (legacy API name retained for callers). */
    static void SaveApiKeyToConfig(const FString& ApiKey);

    // ============ Proxy Config Persistence ============

    static bool     GetProxyEnabledFromConfig();
    static void     SaveProxyEnabledToConfig(bool bEnabled);
    static bool     GetProxyAutoStartFromConfig();
    static void     SaveProxyAutoStartToConfig(bool bAutoStart);
    static int32    GetProxyPortFromConfig();
    static void     SaveProxyPortToConfig(int32 Port);
    static FString  GetProxyPythonPathFromConfig();
    static void     SaveProxyPythonPathToConfig(const FString& Path);

    // ============ Proxy Control ============

    /** Returns true for a recorded standalone proxy or a live, strictly validated
     *  Reddy per-project route. The routed case is lifecycle-only and stays Safe. */
    bool IsProxyRunning() const;

    /** Start the standalone proxy if no Reddy route owns the project. A configured
     *  but offline Reddy route fails closed instead of reallocating project ports. */
    bool StartProxy();

    /** Stop the proxy process (only if we spawned it) */
    void StopProxy();

    /** Write revvy-proxy.json with the ephemeral read-only bearer and routing data */
    void WriteProxyConfigJson() const;

    /** Create the project-level .mcp.json / .claude/settings.local.json if missing
     *  so local MCP clients (e.g. the Claude Code CLI) connect with zero setup. */
    void EnsureProjectMcpConfig() const;

    // ============ Editor-owned Full access ============

    /**
     * Give this editor process a Full access bearer for the local proxy.
     *
     * Adopts an operator-pinned REVVY_FULL_ACCESS_TOKEN when there is one; otherwise
     * mints one and publishes it to this process so the proxy and every CLI session
     * the editor launches inherit it. Must run before the proxy is spawned.
     */
    static void EnsureEditorFullAccessToken();

    /**
     * True when the running proxy is known to accept this editor's bearer.
     *
     * Known means one of three things: the operator pinned the token for the whole
     * user session, this editor spawned the proxy itself, or a proxy that was
     * already running answered a Full access request from this editor. Anything
     * else is treated as unknown, and callers stay on the read-only endpoint
     * rather than risk a session with no tools at all.
     */
    static bool IsEditorFullAccessAvailable();

    /**
     * Ask a proxy this editor did not start whether it accepts this editor's bearer.
     *
     * Provenance is not the question worth asking. A proxy the operator launched
     * from a shell that already carried REVVY_FULL_ACCESS_TOKEN accepts the bearer
     * perfectly well, and treating it as untrusted because we did not spawn it left
     * every in-editor session read-only with nothing on screen to say why — reported
     * 2026-08-13. Whether the bearer works is answerable directly, so ask.
     *
     * Sets bProxyAdoptedEditorFullAccessToken on success. Blocking, with a short
     * timeout: it runs once on the startup path that would otherwise skip the spawn.
     * A configured Reddy per-project route is never probed with this bearer.
     */
    static bool ProbeProxyAcceptsEditorBearer();

    /**
     * MCP server config for the sessions this editor launches, written under Saved/.
     *
     * Kept out of the project's shared .mcp.json on purpose: that file is also read
     * by whatever the user runs in the project directory, and Full access there
     * would hand the editor to every one of those sessions.
     * When Reddy owns a routed project endpoint this config uses its canonical Safe
     * URL and omits the editor-owned bearer regardless of pinned-token state.
     */
    static FString WriteEditorMcpConfig();

    /** Load all config into Config struct */
    void LoadConfig();
    
    /** Save all config from Config struct */
    void SaveConfig();
    
    /** Process pending requests on game thread (called by tick) */
    void ProcessPendingRequests();
    
    /** Validate Revvy API key asynchronously - sets bIsRevvyApiKeyValid */
    void ValidateRevvyApiKeyAsync();

    /**
     * Return the current Editor instance's explicitly allow-listed native
     * Toolset catalogue. This avoids the shared on-disk proxy manifest, which
     * may be stale or belong to another running Editor instance.
     */
    FString GetUnauthenticatedNativeToolCatalogJson() const;

    /**
     * Execute a tool through both the unauthenticated-native allow-list and the
     * exact ReadOnly scope used by authenticated Safe MCP sessions.
     *
     * This method must be called on the game thread. It never grants Full scope
     * and deliberately excludes sensitive read-only tools such as read_logs and
     * open-world tools such as deep_research.
     */
    FString ExecuteUnauthenticatedNativeToolInProcess(
        const FString& ToolName,
        const FString& ArgumentsJson);

private:
    /** Handle incoming connection */
    bool HandleConnection(FSocket* ClientSocket);
    
    /** Parse HTTP request from socket */
    bool ParseHttpRequest(FSocket* Socket, FString& OutMethod, FString& OutPath, 
                          TMap<FString, FString>& OutHeaders, FString& OutBody,
                          TArray<uint8>& OutBodyBytes);
    
    /** Send HTTP response */
    void SendHttpResponse(FSocket* Socket, int32 StatusCode, const FString& StatusText,
                          const FString& ContentType, const FString& Body,
                          const TMap<FString, FString>& ExtraHeaders = TMap<FString, FString>(),
                          const FRevvyBridgeAuthContext* AuthContext = nullptr);
    
    /** Handle MCP JSON-RPC request */
    FString HandleMCPRequest(const FString& JsonBody, FString& InOutSessionId,
                             bool& bOutIsNotification, EMCPAccessScope AccessScope);
    
    /** Handle SSE GET request - opens event stream */
    bool HandleSSERequest(FSocket* ClientSocket, const TMap<FString, FString>& Headers);
    
    /** Send SSE event to a client */
    bool SendSSEEvent(FSocket* ClientSocket, const FString& Data, int32 EventId = -1);
    
    /** Send SSE response with Content-Type: text/event-stream */
    void SendSSEResponse(FSocket* Socket, const FString& SessionId);
    
    /** Validate MCP-Protocol-Version header */
    bool ValidateProtocolVersion(const TMap<FString, FString>& Headers) const;
    
    /** Parse Accept header to check for SSE support */
    bool AcceptsSSE(const TMap<FString, FString>& Headers) const;
    
    /** Show one-time proxy nudge toast if client is connected directly (no proxy header) */
    void ShowProxyNudgeIfNeeded();

    /** Handle individual JSON-RPC methods */
    FString HandleInitialize(TSharedPtr<FJsonObject> Params, const TSharedPtr<FJsonValue>& RequestId);
    FString HandleToolsList(TSharedPtr<FJsonObject> Params, const TSharedPtr<FJsonValue>& RequestId,
                            EMCPAccessScope AccessScope);
    FString HandleToolsCall(TSharedPtr<FJsonObject> Params, const TSharedPtr<FJsonValue>& RequestId,
                            EMCPAccessScope AccessScope);
    /**
     * Read on from a reply the server had to bound (see MCP/OversizedResultStore.h).
     *
     * Served here rather than from the tool registry because the read has to be
     * checked against this request's access scope, and registry handlers are
     * dispatched by reflection with the arguments alone - they never see it.
     */
    FString HandleResultPageCall(TSharedPtr<FJsonObject> Params, const TSharedPtr<FJsonValue>& RequestId,
                                 EMCPAccessScope AccessScope);
    FString HandlePing(const TSharedPtr<FJsonValue>& RequestId);
    
    /** Build JSON-RPC response */
    FString BuildJsonRpcResponse(const TSharedPtr<FJsonValue>& RequestId, TSharedPtr<FJsonObject> Result);
    FString BuildJsonRpcError(const TSharedPtr<FJsonValue>& RequestId, int32 Code, const FString& Message);
    
    /** Authenticate a request and return its backend capability scope */
    EMCPAccessScope ValidateApiKey(const TMap<FString, FString>& Headers) const;
    
    /** Validate Origin header for security */
    bool ValidateOrigin(const TMap<FString, FString>& Headers) const;
    
    /** Generate new session ID */
    FString GenerateSessionId() const;
    
    /** Get internal tools in MCP format */
    TArray<FMCPTool> GetInternalTools() const;

    /**
     * Export the tool manifest to <Project>/.revvy/tools-manifest.json by default.
     * REVVY_STATE_DIR can explicitly redirect the project runtime state.
     * Called on successful server start so the MCP proxy can serve
     * tool definitions even when Unreal Engine is not running.
     */
    void ExportToolManifest() const;
    
    /** Current configuration */
    FMCPHostConfig Config;

    /** Listener-lifetime identity returned by ping; rotated after every successful bind/listen. */
    FString BridgeInstanceId;

    /** One-use nonces accepted for the current listener identity. */
    FRevvyBridgeReplayCache BridgeReplayCache;
    
    /** Whether the Revvy API key has been validated (checked at startup) */
    bool bIsRevvyApiKeyValid = false;
    
    /** Whether the server is currently running (read from game + socket threads) */
    std::atomic<bool> bIsRunning{ false };

    /** Signal to stop the server thread (written on game thread, read on socket thread) */
    std::atomic<bool> bShouldStop{ false };


    /** TCP listener socket */
    FSocket* ListenerSocket = nullptr;

    /** Server thread */
    TUniquePtr<FRunnableThread> ServerThread;
    
    /** Sliding-window request timestamps per client IP (rate limiting) */
    TMap<FString, TArray<double>> ClientRequestTimes;

    /** Guards ClientRequestTimes (HandleConnection runs on the socket thread) */
    FCriticalSection RateLimitLock;

    /**
     * Sliding 60s window rate check for a client key. Returns false when the
     * client exceeded MaxRequestsPerMinute. Prunes old entries as it goes.
     */
    bool CheckRateLimit(const FString& ClientKey);

    /**
     * Build the CORS response headers for a validated request: echoes the exact
     * validated Origin (never "*") with Vary: Origin. No Origin header → no CORS
     * headers (non-browser clients don't need them).
     */
    void AppendCorsHeaders(const TMap<FString, FString>& RequestHeaders, TMap<FString, FString>& ResponseHeaders) const;

    /** Active sessions */
    TMap<FString, FDateTime> ActiveSessions;
    
    /** Critical section for thread safety */
    FCriticalSection SessionLock;
    
    /** Queue of requests to process on game thread */
    TQueue<TSharedPtr<FMCPPendingRequest, ESPMode::ThreadSafe>, EQueueMode::Mpsc> PendingRequests;
    
    /** Active SSE connections */
    TArray<TSharedPtr<FMCPSSEConnection>> SSEConnections;
    FCriticalSection SSELock;
    
    /** Next SSE event ID for resumability */
    TAtomic<int32> NextEventId;
    
    /** Tick delegate handle */
    FTSTicker::FDelegateHandle TickDelegateHandle;
    
    /** Proxy process handle (only valid if we spawned it) */
    FProcHandle ProxyProcHandle;

    /** True if we spawned the proxy process and are responsible for stopping it */
    bool bProxyOwnedByUs = false;

    /** Full access bearer this editor hands to the CLI sessions it launches */
    static FString EditorFullAccessToken;

    /** True when the token came from the environment rather than being minted here */
    static bool bEditorFullAccessTokenPinned;

    /** True once a proxy is known to accept EditorFullAccessToken: spawned by this
     *  editor and so inheriting it, or probed and found to answer to it. */
    static bool bProxyAdoptedEditorFullAccessToken;

    /** Cached proxy running state — updated at most every 3 seconds to avoid blocking the game thread */
    mutable bool   bCachedProxyRunning = false;
    mutable double LastProxyCheckTime  = -999.0;

    /** Date string (YYYY-MM-DD) of the last API key validation — used to re-validate on day rollover */
    FString LastValidationDate;

    /** Wall-clock time of the last day-rollover check — throttled to once per minute */
    double LastDayCheckTime = -999.0;

    /** Singleton instance */
    static TSharedPtr<FMCPServer> Instance;
};
