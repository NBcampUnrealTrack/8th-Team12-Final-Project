// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UScreenshotService.generated.h"

/**
 * Result of a screenshot operation
 */
USTRUCT(BlueprintType)
struct FScreenshotResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Screenshot")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Screenshot")
	FString FilePath;

	UPROPERTY(BlueprintReadWrite, Category = "Screenshot")
	FString Message;

	UPROPERTY(BlueprintReadWrite, Category = "Screenshot")
	int32 Width = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Screenshot")
	int32 Height = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Screenshot")
	FString CapturedWindowTitle;
};

/**
 * Information about an open editor tab/window
 */
USTRUCT(BlueprintType)
struct FEditorTabInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Editor")
	FString TabLabel;

	UPROPERTY(BlueprintReadWrite, Category = "Editor")
	FString TabType;

	UPROPERTY(BlueprintReadWrite, Category = "Editor")
	FString AssetPath;

	UPROPERTY(BlueprintReadWrite, Category = "Editor")
	bool bIsForeground = false;
};

/**
 * Service for capturing screenshots of editor windows and viewports.
 * Provides Python-accessible methods for AI vision capabilities.
 * 
 * This service supports:
 * - Level viewport screenshots (using existing UE functionality)
 * - Full editor window screenshots (using Windows GDI with DWM)
 * - Active tab/window information retrieval
 * 
 * Usage from Python:
 *   import unreal
 *   result = unreal.ScreenshotService.capture_active_window("E:/Screenshots/capture.png")
 *   if result.success:
 *       print(f"Captured: {result.file_path}")
 */
UCLASS(BlueprintType)
class REVVY_API UScreenshotService : public UObject
{
	GENERATED_BODY()

public:
	UScreenshotService();

	/**
	 * Capture the level viewport to a PNG, optionally from an arbitrary camera pose,
	 * WITHOUT disturbing the user's camera (pose + show flags saved and restored).
	 *
	 * Synchronous and cross-platform: sets the pose on the active level viewport client,
	 * forces Viewport->Draw(), reads the framebuffer via GetViewportScreenShot(), restores.
	 * This is the same recipe Epic's UE 5.8 MCP EditorToolset uses — it works while Python
	 * holds the game thread (no engine tick needed).
	 *
	 * Units: Location in UE centimeters, Rotation in degrees, FOV in degrees.
	 * Output resolution = current viewport size.
	 *
	 * @param Location  Camera world location (cm). Ignored when bUseCurrentCamera.
	 * @param Rotation  Camera rotation (degrees). Ignored when bUseCurrentCamera.
	 * @param FilePath  Output path; normalized to ProjectSaved/Revvy/Screenshots/<name>.png when bare.
	 * @param FOV       Horizontal FOV in degrees; 0 = keep current viewport FOV.
	 * @param bShowUI   Keep selection outlines / gizmos visible (default false: clean image).
	 * @param bUseCurrentCamera  Capture the viewport exactly as-is, ignoring Location/Rotation/FOV.
	 * @return JSON {success, file_path, width, height,
	 *               camera:{location:[x,y,z], rotation:[pitch,yaw,roll], fov}, message?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static FString CaptureFromPose(FVector Location, FRotator Rotation, const FString& FilePath,
		float FOV = 0.0f, bool bShowUI = false, bool bUseCurrentCamera = false);

	/**
	 * Compact JSON description of the actors a camera can see — the "eyes" half of the
	 * see->act->check loop. Pair with CaptureFromPose using the same pose: both return the
	 * same camera block so actor names correlate to image content without burned-in labels.
	 *
	 * Filters match Epic's MCP toolset: skips pending-kill, editor-hidden, transient, and
	 * "management" actors (AInfo subclasses / actors with no primitive components).
	 *
	 * @param Location  Camera pose for the frustum test (cm). Ignored when bUseCurrentCamera.
	 * @param Rotation  Camera rotation (degrees). Ignored when bUseCurrentCamera.
	 * @param FOV       FOV degrees; 0 = current viewport FOV. Ignored when bUseCurrentCamera.
	 * @param ClassFilter  Case-insensitive class-name substring ("" = all classes).
	 * @param MaxActors    Cap on returned actors, nearest-first (default 50, clamp 1..500).
	 * @param bOnlyInFrustum  false = whole-level listing (frustum test skipped).
	 * @param bUseCurrentCamera  Use the live viewport camera for the frustum.
	 * @return JSON {success, camera:{location, rotation, fov}, viewport:[w,h], actor_count,
	 *               actors:[{name, label, class, pos_cm:[x,y,z], extent_cm:[x,y,z],
	 *                        dist_cm, screen_px:[x,y]|null}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static FString DescribeScene(FVector Location, FRotator Rotation, float FOV = 0.0f,
		const FString& ClassFilter = TEXT(""), int32 MaxActors = 50,
		bool bOnlyInFrustum = true, bool bUseCurrentCamera = true);

	/**
	 * Capture the entire Unreal Editor window to a file.
	 * Uses Windows DWM (Desktop Window Manager) for accurate capture of DirectX content.
	 * Works for all editor content including Blueprint graphs, material editors, etc.
	 * @param FilePath - Output file path (PNG format recommended)
	 * @return Screenshot result with success status and file info
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static FScreenshotResult CaptureEditorWindow(const FString& FilePath);

	/**
	 * Capture the currently active/foreground window to a file.
	 * This captures whatever window is currently in focus.
	 * @param FilePath - Output file path (PNG format recommended)
	 * @return Screenshot result with success status and file info
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static FScreenshotResult CaptureActiveWindow(const FString& FilePath);

	/**
	 * Open an asset editor for the given asset path, force its tab to the foreground, and
	 * capture the editor window. One-call replacement for the
	 * "close other editors -> open target -> capture_editor_window" dance that was previously
	 * required to reliably screenshot a specific asset's editor (material graph, blueprint
	 * graph, etc.).
	 *
	 * @param AssetPath - Content Browser path to the asset (e.g. "/Game/Materials/M_Foo")
	 * @param FilePath  - Output file path. Auto-normalized to ProjectSaved/Revvy/Screenshots/<name>.png
	 *                    when no extension is given.
	 * @return Screenshot result; success means the asset's editor was focused and the file written.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static FScreenshotResult CaptureAssetEditor(const FString& AssetPath, const FString& FilePath);

	/**
	 * Get information about open editor tabs.
	 * Useful for understanding what the user is currently viewing.
	 * @return Array of editor tab information
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static TArray<FEditorTabInfo> GetOpenEditorTabs();

	/**
	 * Get the title of the currently focused window.
	 * @return Window title string
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static FString GetActiveWindowTitle();

	/**
	 * Check if the Unreal Editor main window is in focus.
	 * @return True if the editor is the foreground window
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Screenshot")
	static bool IsEditorWindowActive();

private:
	/**
	 * Normalize a user-supplied save path:
	 * - If no directory prefix, defaults to ProjectSaved/Screenshots/
	 * - If no extension, appends .png
	 */
	static FString NormalizeSavePath(const FString& FilePath);

	/**
	 * Find the Unreal Editor main window handle
	 * @return Window handle or nullptr if not found
	 */
	static void* FindEditorWindowHandle();

	/**
	 * Capture a window by its handle to a file
	 * @param WindowHandle - Native window handle
	 * @param FilePath - Output file path
	 * @param OutResult - Result structure to populate
	 */
	static void CaptureWindowToFile(void* WindowHandle, const FString& FilePath, FScreenshotResult& OutResult);

	/**
	 * Save raw bitmap data to PNG file
	 * @param BitmapData - Raw BGRA pixel data
	 * @param Width - Image width
	 * @param Height - Image height
	 * @param FilePath - Output file path
	 * @return True if save succeeded
	 */
	static bool SaveBitmapAsPNG(const TArray<uint8>& BitmapData, int32 Width, int32 Height, const FString& FilePath);
};
