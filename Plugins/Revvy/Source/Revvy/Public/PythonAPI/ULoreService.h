// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ULoreService.generated.h"

/**
 * Lore VCS integration for the Revvy Python-facing service layer.
 * Wraps the `lore` CLI (EpicGames/lore — Epic's open-source version control
 * for code + binary assets, released June 2026) so AI workflows can create
 * recovery points before destructive edits and commit finished work.
 *
 * All UFUNCTIONs are static + BlueprintCallable, reachable from Python as
 * `unreal.LoreService.<snake_case_name>()`.
 *
 * Every command returns a compact JSON object:
 *   {"success": bool, "exit_code": int, "stdout": str, "stderr": str}
 * plus per-call extras documented on each method. When the lore binary is
 * missing or the project is not a Lore workspace, success=false with
 * error_code "LORE_NOT_INSTALLED" / "LORE_NO_WORKSPACE" — callers must treat
 * those as normal states, not failures.
 *
 * Notes for callers:
 * - Lore's `status` does NOT scan the filesystem by default; Revvy passes
 *   --scan so editor-saved files are detected.
 * - There is no `pull`; `sync` is the pull equivalent.
 * - Locks in Lore 0.x are advisory only.
 * - Commit requires a configured identity (.lore/config.toml `identity`).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.LoreService
 *   if json.loads(svc.get_workspace_info())["is_workspace"]:
 *       svc.stage_all()
 *       print(svc.commit("checkpoint before landscape rebuild"))
 */
UCLASS(BlueprintType)
class REVVY_API ULoreService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Detect the lore CLI and the project's Lore workspace.
	 *
	 * @return JSON {success, installed: bool, version: str,
	 *               is_workspace: bool, workspace_root: str}.
	 *         `version` empty when not installed; `workspace_root` empty when
	 *         the project directory is not inside a Lore workspace
	 *         (detected by walking up for a `.lore/` or legacy `.urc/` dir).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Query")
	static FString GetWorkspaceInfo();

	/**
	 * `lore status --scan` — working-tree status with a filesystem scan so
	 * files saved by the editor (outside Lore) are detected.
	 *
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Query")
	static FString Status();

	/**
	 * `lore diff [paths]` — unified diff of current revision vs filesystem.
	 * Binary assets show as divergence markers, not text diffs.
	 *
	 * @param Paths Optional space-separated repo-relative paths ("" = all).
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Query")
	static FString Diff(const FString& Paths);

	/**
	 * `lore history --oneline [length]` — recent revisions on current branch.
	 *
	 * @param Length Max entries (default 10; clamped to [1, 200]).
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Query")
	static FString History(int32 Length = 10);

	/**
	 * `lore stage --scan .` from the workspace root — stage every change
	 * found on disk. Use before Commit for a full checkpoint.
	 *
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString StageAll();

	/**
	 * `lore stage <paths>` — stage specific files (always checked on disk).
	 *
	 * @param Paths Space-separated repo-relative paths.
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString Stage(const FString& Paths);

	/**
	 * `lore commit "<message>"` — commit staged changes locally (no network).
	 * Fails with a clear stderr when no identity is configured
	 * ("No commit identity configured..."); surface that to the user verbatim.
	 *
	 * @param Message Commit message (required, non-empty).
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString Commit(const FString& Message);

	/**
	 * Convenience checkpoint: StageAll + Commit in one call. Intended for the
	 * automation contract's "recovery point before bulk/destructive changes".
	 * No-ops with success=true and skipped=true when there is nothing to commit.
	 *
	 * @param Message Checkpoint message; "" → "Revvy checkpoint".
	 * @return JSON {success, skipped?, stage: {...}, commit: {...}}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString Checkpoint(const FString& Message);

	/**
	 * `lore push` — upload local commits to the server-of-record.
	 * On conflict, Lore's guidance is: sync, resolve, commit the merge, push again.
	 *
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString Push();

	/**
	 * `lore sync` — bring the working tree up to the remote latest
	 * (Lore's pull equivalent; there is no `lore pull`).
	 *
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString Sync();

	/**
	 * `lore dirty <paths>` — cheap mark-only dirty flag (no content read).
	 * Designed for editor save hooks: mark files Lore should re-examine.
	 *
	 * @param Paths Space-separated repo-relative paths.
	 * @return JSON {success, exit_code, stdout, stderr}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lore|Edit")
	static FString MarkDirty(const FString& Paths);

private:
	/** Find lore.exe: PATH first, then %USERPROFILE%/bin fallback. Empty if absent. */
	static FString FindLoreBinary();

	/** Walk up from the project dir looking for .lore/ or legacy .urc/. Empty if none. */
	static FString FindWorkspaceRoot();

	/** Run `lore --repository <root> --non-interactive -P <args>`, capture output. */
	static bool RunLore(const FString& Args, FString& OutStdout, FString& OutStderr, int32& OutExitCode);

	/** Build the standard result JSON. */
	static FString MakeResultJson(bool bSuccess, int32 ExitCode, const FString& StdOut, const FString& StdErr, const FString& ErrorCode = FString());
};
