// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UScreenshotService.h"
#include "Utils/RevvyPaths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/IToolkit.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealClient.h"
#include "LevelEditorViewport.h"
#include "EngineUtils.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "ConvexVolume.h"
#include "GameFramework/Info.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UScreenshotService::UScreenshotService()
{
}

FString UScreenshotService::NormalizeSavePath(const FString& FilePath)
{
	FString Result = FilePath;

	// If no directory prefix, default to the Revvy screenshots directory
	if (!Result.Contains(TEXT("/")) && !Result.Contains(TEXT("\\")))
	{
		Result = FRevvyPaths::GetScreenshotsDir() / Result;
	}

	// If no extension, add .png
	if (FPaths::GetExtension(Result).IsEmpty())
	{
		Result += TEXT(".png");
	}

	FPaths::NormalizeFilename(Result);
	return Result;
}

namespace
{
	FString SerializeCompactJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString OutJson;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
		FJsonSerializer::Serialize(Obj, Writer);
		return OutJson;
	}

	TArray<TSharedPtr<FJsonValue>> Vec3Json(double X, double Y, double Z)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(X));
		Arr.Add(MakeShared<FJsonValueNumber>(Y));
		Arr.Add(MakeShared<FJsonValueNumber>(Z));
		return Arr;
	}

	TSharedRef<FJsonObject> CameraJson(const FVector& Location, const FRotator& Rotation, float FOV)
	{
		TSharedRef<FJsonObject> Cam = MakeShared<FJsonObject>();
		Cam->SetArrayField(TEXT("location"), Vec3Json(Location.X, Location.Y, Location.Z));
		Cam->SetArrayField(TEXT("rotation"), Vec3Json(Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
		Cam->SetNumberField(TEXT("fov"), FOV);
		return Cam;
	}

	FString ErrorJson(const FString& Message)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("message"), Message);
		return SerializeCompactJson(Obj);
	}

	/** Epic MCP-style noise filter: actors an agent never wants in a scene description. */
	bool IsManagementActor(const AActor* Actor)
	{
		if (Actor->IsA<AInfo>())
		{
			return true;
		}
		TArray<UPrimitiveComponent*> Prims;
		Actor->GetComponents<UPrimitiveComponent>(Prims);
		return Prims.Num() == 0;
	}

	/** RAII: save the level viewport pose + agent-hostile show flags, restore on scope exit. */
	struct FScopedViewportPose
	{
		FLevelEditorViewportClient* Client = nullptr;
		FVector SavedLocation;
		FRotator SavedRotation;
		float SavedFOV = 90.0f;
		bool bSavedModeWidgets = false;
		bool bSavedSelectionOutline = false;
		bool bSavedSelection = false;

		explicit FScopedViewportPose(FLevelEditorViewportClient* InClient)
			: Client(InClient)
		{
			if (Client)
			{
				SavedLocation = Client->GetViewLocation();
				SavedRotation = Client->GetViewRotation();
				SavedFOV = Client->ViewFOV;
				bSavedModeWidgets = Client->EngineShowFlags.ModeWidgets;
				bSavedSelectionOutline = Client->EngineShowFlags.SelectionOutline;
				bSavedSelection = Client->EngineShowFlags.Selection;
			}
		}

		void Apply(const FVector& Location, const FRotator& Rotation, float FOV, bool bHideGizmos)
		{
			if (!Client)
			{
				return;
			}
			// Not SetViewLocationForOrbiting — preserves the user's orbit pivot
			Client->SetViewLocation(Location);
			Client->SetViewRotation(Rotation);
			if (FOV > 0.0f)
			{
				Client->ViewFOV = FOV;
			}
			if (bHideGizmos)
			{
				Client->EngineShowFlags.SetModeWidgets(false);
				Client->EngineShowFlags.SetSelectionOutline(false);
				Client->EngineShowFlags.SetSelection(false);
			}
		}

		~FScopedViewportPose()
		{
			if (Client)
			{
				Client->SetViewLocation(SavedLocation);
				Client->SetViewRotation(SavedRotation);
				Client->ViewFOV = SavedFOV;
				Client->EngineShowFlags.SetModeWidgets(bSavedModeWidgets);
				Client->EngineShowFlags.SetSelectionOutline(bSavedSelectionOutline);
				Client->EngineShowFlags.SetSelection(bSavedSelection);
			}
		}
	};
}

FString UScreenshotService::CaptureFromPose(FVector Location, FRotator Rotation, const FString& FilePath,
	float FOV, bool bShowUI, bool bUseCurrentCamera)
{
	FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient;
	if (!Client || !Client->Viewport)
	{
		return ErrorJson(TEXT("No active level editor viewport"));
	}

	const FString NormalizedPath = NormalizeSavePath(FilePath);
	{
		const FString Directory = FPaths::GetPath(NormalizedPath);
		if (!Directory.IsEmpty())
		{
			FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
		}
	}

	TArray<FColor> Bitmap;
	FIntPoint Size(0, 0);
	FVector CapturedLocation;
	FRotator CapturedRotation;
	float CapturedFOV = 0.0f;

	{
		FScopedViewportPose PoseGuard(Client);
		if (!bUseCurrentCamera)
		{
			PoseGuard.Apply(Location, Rotation, FOV, /*bHideGizmos=*/!bShowUI);
		}
		else if (!bShowUI)
		{
			Client->EngineShowFlags.SetModeWidgets(false);
			Client->EngineShowFlags.SetSelectionOutline(false);
			Client->EngineShowFlags.SetSelection(false);
		}

		CapturedLocation = Client->GetViewLocation();
		CapturedRotation = Client->GetViewRotation();
		CapturedFOV = Client->ViewFOV;

		// Synchronous redraw on the game thread, then blocking framebuffer readback.
		// Works while Python holds the game thread — no engine tick required.
		Client->Viewport->Draw();
		Size = Client->Viewport->GetSizeXY();
		if (!GetViewportScreenShot(Client->Viewport, Bitmap))
		{
			return ErrorJson(TEXT("GetViewportScreenShot failed (framebuffer readback)"));
		}
	} // pose + show flags restored here

	if (Bitmap.Num() != Size.X * Size.Y || Bitmap.Num() == 0)
	{
		return ErrorJson(FString::Printf(TEXT("Unexpected bitmap size: %d pixels for %dx%d"), Bitmap.Num(), Size.X, Size.Y));
	}

	// FColor is BGRA in memory; force opaque alpha then reuse the existing PNG writer
	TArray<uint8> RawBGRA;
	RawBGRA.SetNumUninitialized(Bitmap.Num() * 4);
	for (int32 i = 0; i < Bitmap.Num(); ++i)
	{
		RawBGRA[i * 4 + 0] = Bitmap[i].B;
		RawBGRA[i * 4 + 1] = Bitmap[i].G;
		RawBGRA[i * 4 + 2] = Bitmap[i].R;
		RawBGRA[i * 4 + 3] = 255;
	}

	if (!SaveBitmapAsPNG(RawBGRA, Size.X, Size.Y, NormalizedPath))
	{
		return ErrorJson(FString::Printf(TEXT("Failed to write PNG: %s"), *NormalizedPath));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("file_path"), NormalizedPath);
	Obj->SetNumberField(TEXT("width"), Size.X);
	Obj->SetNumberField(TEXT("height"), Size.Y);
	Obj->SetObjectField(TEXT("camera"), CameraJson(CapturedLocation, CapturedRotation, CapturedFOV));
	return SerializeCompactJson(Obj);
}

FString UScreenshotService::DescribeScene(FVector Location, FRotator Rotation, float FOV,
	const FString& ClassFilter, int32 MaxActors, bool bOnlyInFrustum, bool bUseCurrentCamera)
{
	FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient;
	if (!Client || !Client->Viewport)
	{
		return ErrorJson(TEXT("No active level editor viewport"));
	}

	UWorld* World = Client->GetWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world"));
	}

	MaxActors = FMath::Clamp(MaxActors, 1, 500);
	const FIntPoint ViewportSize = Client->Viewport->GetSizeXY();

	FConvexVolume Frustum;
	FMatrix WorldToClip = FMatrix::Identity;
	FVector CameraLocation;
	FRotator CameraRotation;
	float CameraFOV = 0.0f;
	bool bHaveView = false;

	{
		FScopedViewportPose PoseGuard(Client);
		if (!bUseCurrentCamera)
		{
			PoseGuard.Apply(Location, Rotation, FOV, /*bHideGizmos=*/false);
		}

		CameraLocation = Client->GetViewLocation();
		CameraRotation = Client->GetViewRotation();
		CameraFOV = Client->ViewFOV;

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Client->Viewport, Client->GetScene(), Client->EngineShowFlags)
			.SetRealtimeUpdate(Client->IsRealtime()));
		if (const FSceneView* SceneView = Client->CalcSceneView(&ViewFamily))
		{
			Frustum = SceneView->ViewFrustum;
			WorldToClip = SceneView->ViewMatrices.GetWorldToClip();
			bHaveView = true;
		}
	}

	if (!bHaveView)
	{
		return ErrorJson(TEXT("Failed to build a scene view for the requested pose"));
	}

	struct FActorEntry
	{
		const AActor* Actor = nullptr;
		FBox Bounds;
		double DistSq = 0.0;
	};
	TArray<FActorEntry> Candidates;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor || Actor->IsPendingKillPending() || Actor->IsHiddenEd() ||
			Actor->HasAnyFlags(RF_Transient) || IsManagementActor(Actor))
		{
			continue;
		}
		if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FBox Bounds = Actor->GetComponentsBoundingBox(/*bNonColliding=*/true);
		if (bOnlyInFrustum)
		{
			const bool bVisible = Bounds.IsValid
				? Frustum.IntersectBox(Bounds.GetCenter(), Bounds.GetExtent())
				: Frustum.IntersectSphere(Actor->GetActorLocation(), 0.0f);
			if (!bVisible)
			{
				continue;
			}
		}

		FActorEntry Entry;
		Entry.Actor = Actor;
		Entry.Bounds = Bounds;
		Entry.DistSq = FVector::DistSquared(CameraLocation, Actor->GetActorLocation());
		Candidates.Add(Entry);
	}

	Candidates.Sort([](const FActorEntry& A, const FActorEntry& B) { return A.DistSq < B.DistSq; });

	const FIntRect ViewRect(0, 0, ViewportSize.X, ViewportSize.Y);
	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (int32 i = 0; i < Candidates.Num() && i < MaxActors; ++i)
	{
		const FActorEntry& Entry = Candidates[i];
		const AActor* Actor = Entry.Actor;

		TSharedRef<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		const FVector Pos = Actor->GetActorLocation();
		ActorObj->SetArrayField(TEXT("pos_cm"), Vec3Json(Pos.X, Pos.Y, Pos.Z));
		const FVector Extent = Entry.Bounds.IsValid ? Entry.Bounds.GetExtent() : FVector::ZeroVector;
		ActorObj->SetArrayField(TEXT("extent_cm"), Vec3Json(Extent.X, Extent.Y, Extent.Z));
		ActorObj->SetNumberField(TEXT("dist_cm"), FMath::Sqrt(Entry.DistSq));

		FVector2D ScreenPos;
		if (FSceneView::ProjectWorldToScreen(Pos, ViewRect, WorldToClip, ScreenPos))
		{
			TArray<TSharedPtr<FJsonValue>> Px;
			Px.Add(MakeShared<FJsonValueNumber>(FMath::RoundToInt(ScreenPos.X)));
			Px.Add(MakeShared<FJsonValueNumber>(FMath::RoundToInt(ScreenPos.Y)));
			ActorObj->SetArrayField(TEXT("screen_px"), Px);
		}
		else
		{
			ActorObj->SetField(TEXT("screen_px"), MakeShared<FJsonValueNull>());
		}

		ActorArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetObjectField(TEXT("camera"), CameraJson(CameraLocation, CameraRotation, CameraFOV));
	{
		TArray<TSharedPtr<FJsonValue>> VP;
		VP.Add(MakeShared<FJsonValueNumber>(ViewportSize.X));
		VP.Add(MakeShared<FJsonValueNumber>(ViewportSize.Y));
		Obj->SetArrayField(TEXT("viewport"), VP);
	}
	Obj->SetNumberField(TEXT("actor_count"), FMath::Min(Candidates.Num(), MaxActors));
	Obj->SetNumberField(TEXT("total_matching"), Candidates.Num());
	Obj->SetArrayField(TEXT("actors"), ActorArray);
	return SerializeCompactJson(Obj);
}

FScreenshotResult UScreenshotService::CaptureEditorWindow(const FString& FilePath)
{
	FString NormalizedPath = NormalizeSavePath(FilePath);
	FScreenshotResult Result;
	Result.FilePath = NormalizedPath;

#if PLATFORM_WINDOWS
	void* WindowHandle = FindEditorWindowHandle();
	if (WindowHandle)
	{
		CaptureWindowToFile(WindowHandle, NormalizedPath, Result);
	}
	else
	{
		Result.bSuccess = false;
		Result.Message = TEXT("Failed to find Unreal Editor window handle");
	}
#else
	Result.bSuccess = false;
	Result.Message = TEXT("Screenshot capture only supported on Windows platform");
#endif

	return Result;
}

FScreenshotResult UScreenshotService::CaptureAssetEditor(const FString& AssetPath, const FString& FilePath)
{
	FString NormalizedPath = NormalizeSavePath(FilePath);
	FScreenshotResult Result;
	Result.FilePath = NormalizedPath;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return Result;
	}

	UAssetEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!EditorSubsystem)
	{
		Result.bSuccess = false;
		Result.Message = TEXT("AssetEditorSubsystem not available");
		return Result;
	}

	// OpenEditorForAsset returns true if it opens or focuses an existing editor.
	if (!EditorSubsystem->OpenEditorForAsset(Asset))
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Failed to open editor for asset: %s"), *AssetPath);
		return Result;
	}

	// FindEditorForAsset(asset, bFocusIfOpen=true) is the standard way to bring an editor
	// to the foreground. Combined with FocusWindow it covers both the case where the editor
	// was just opened and the case where it was already open under another tab.
	IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/true);
	if (EditorInstance)
	{
		EditorInstance->FocusWindow(Asset);
	}

	// Pump Slate enough frames that the tab activation actually paints. A single Tick is
	// usually too few — the docking system processes activation across multiple frames,
	// and DWM capture sees stale content if we capture mid-transition.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		for (int32 i = 0; i < 8; ++i)
		{
			Slate.Tick();
		}
		if (FSlateRenderer* Renderer = Slate.GetRenderer())
		{
			Renderer->Sync();
		}
	}

#if PLATFORM_WINDOWS
	void* WindowHandle = FindEditorWindowHandle();
	if (WindowHandle)
	{
		CaptureWindowToFile(WindowHandle, NormalizedPath, Result);
	}
	else
	{
		Result.bSuccess = false;
		Result.Message = TEXT("Failed to find Unreal Editor window handle");
	}
#else
	Result.bSuccess = false;
	Result.Message = TEXT("Screenshot capture only supported on Windows platform");
#endif

	return Result;
}

FScreenshotResult UScreenshotService::CaptureActiveWindow(const FString& FilePath)
{
	FString NormalizedPath = NormalizeSavePath(FilePath);
	FScreenshotResult Result;
	Result.FilePath = NormalizedPath;

#if PLATFORM_WINDOWS
	HWND ForegroundWindow = GetForegroundWindow();
	if (ForegroundWindow)
	{
		CaptureWindowToFile(ForegroundWindow, NormalizedPath, Result);
	}
	else
	{
		Result.bSuccess = false;
		Result.Message = TEXT("No active window found");
	}
#else
	Result.bSuccess = false;
	Result.Message = TEXT("Screenshot capture only supported on Windows platform");
#endif

	return Result;
}

TArray<FEditorTabInfo> UScreenshotService::GetOpenEditorTabs()
{
	TArray<FEditorTabInfo> Tabs;

	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				if (Asset)
				{
					FEditorTabInfo TabInfo;
					TabInfo.TabLabel = Asset->GetName();
					TabInfo.AssetPath = Asset->GetPathName();
					TabInfo.TabType = Asset->GetClass()->GetName();
					TabInfo.bIsForeground = false; // Would need more complex logic to determine
					Tabs.Add(TabInfo);
				}
			}
		}
	}

	return Tabs;
}

FString UScreenshotService::GetActiveWindowTitle()
{
#if PLATFORM_WINDOWS
	HWND ForegroundWindow = GetForegroundWindow();
	if (ForegroundWindow)
	{
		int Length = GetWindowTextLengthW(ForegroundWindow) + 1;
		if (Length > 1)
		{
			TArray<WCHAR> Buffer;
			Buffer.SetNum(Length);
			GetWindowTextW(ForegroundWindow, Buffer.GetData(), Length);
			return FString(Buffer.GetData());
		}
	}
#endif
	return FString();
}

bool UScreenshotService::IsEditorWindowActive()
{
#if PLATFORM_WINDOWS
	HWND ForegroundWindow = GetForegroundWindow();
	void* EditorHandle = FindEditorWindowHandle();
	return ForegroundWindow == EditorHandle;
#else
	return false;
#endif
}

void* UScreenshotService::FindEditorWindowHandle()
{
#if PLATFORM_WINDOWS
	// Use Slate's native window handle
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> MainWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (MainWindow.IsValid())
		{
			TSharedPtr<FGenericWindow> NativeWindow = MainWindow->GetNativeWindow();
			if (NativeWindow.IsValid())
			{
				return NativeWindow->GetOSWindowHandle();
			}
		}

		// Fallback: try to find via window enumeration
		struct WindowFinder
		{
			static BOOL CALLBACK EnumCallback(HWND hwnd, LPARAM lParam)
			{
				HWND* pResult = reinterpret_cast<HWND*>(lParam);
				
				int Length = GetWindowTextLengthW(hwnd) + 1;
				if (Length > 1)
				{
					TArray<WCHAR> Buffer;
					Buffer.SetNum(Length);
					GetWindowTextW(hwnd, Buffer.GetData(), Length);
					FString Title(Buffer.GetData());
					
					// Look for main Unreal Editor window
					if (Title.Contains(TEXT("Unreal Editor")))
					{
						*pResult = hwnd;
						return 0; // Stop enumeration
					}
				}
				return 1; // Continue enumeration
			}
		};

		HWND FoundWindow = nullptr;
		EnumWindows(WindowFinder::EnumCallback, reinterpret_cast<LPARAM>(&FoundWindow));
		return FoundWindow;
	}
#endif
	return nullptr;
}

void UScreenshotService::CaptureWindowToFile(void* WindowHandle, const FString& FilePath, FScreenshotResult& OutResult)
{
#if PLATFORM_WINDOWS
	HWND hwnd = static_cast<HWND>(WindowHandle);
	
	// Get window title for result
	int TitleLength = GetWindowTextLengthW(hwnd) + 1;
	if (TitleLength > 1)
	{
		TArray<WCHAR> TitleBuffer;
		TitleBuffer.SetNum(TitleLength);
		GetWindowTextW(hwnd, TitleBuffer.GetData(), TitleLength);
		OutResult.CapturedWindowTitle = FString(TitleBuffer.GetData());
	}

	// Get window dimensions
	RECT WindowRect;
	if (!GetWindowRect(hwnd, &WindowRect))
	{
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Failed to get window dimensions");
		return;
	}

	int Width = WindowRect.right - WindowRect.left;
	int Height = WindowRect.bottom - WindowRect.top;

	if (Width <= 0 || Height <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Invalid window dimensions");
		return;
	}

	OutResult.Width = Width;
	OutResult.Height = Height;

	// Create compatible DC and bitmap
	HDC WindowDC = GetWindowDC(hwnd);
	if (!WindowDC)
	{
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Failed to get window DC");
		return;
	}

	HDC MemDC = CreateCompatibleDC(WindowDC);
	if (!MemDC)
	{
		ReleaseDC(hwnd, WindowDC);
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Failed to create compatible DC");
		return;
	}

	HBITMAP Bitmap = CreateCompatibleBitmap(WindowDC, Width, Height);
	if (!Bitmap)
	{
		DeleteDC(MemDC);
		ReleaseDC(hwnd, WindowDC);
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Failed to create bitmap");
		return;
	}

	HGDIOBJ OldBitmap = SelectObject(MemDC, Bitmap);

	// Try PrintWindow with full content flag first (better for layered/DWM windows)
	// PW_RENDERFULLCONTENT = 2 - captures DWM composed content
	BOOL PrintResult = PrintWindow(hwnd, MemDC, 2);
	
	if (!PrintResult)
	{
		// Fallback to BitBlt (works for some windows but may produce black for DirectX)
		BitBlt(MemDC, 0, 0, Width, Height, WindowDC, 0, 0, SRCCOPY);
	}

	// Prepare bitmap info for pixel extraction
	BITMAPINFOHEADER BitmapInfoHeader = {};
	BitmapInfoHeader.biSize = sizeof(BITMAPINFOHEADER);
	BitmapInfoHeader.biWidth = Width;
	BitmapInfoHeader.biHeight = -Height; // Negative for top-down
	BitmapInfoHeader.biPlanes = 1;
	BitmapInfoHeader.biBitCount = 32;
	BitmapInfoHeader.biCompression = BI_RGB;

	// Calculate buffer size (4 bytes per pixel for BGRA)
	int32 RowSize = ((Width * 32 + 31) / 32) * 4;
	int32 ImageSize = RowSize * Height;
	
	TArray<uint8> PixelData;
	PixelData.SetNum(ImageSize);

	// Get the bitmap bits
	int ScanLines = GetDIBits(MemDC, Bitmap, 0, Height, PixelData.GetData(), 
		reinterpret_cast<BITMAPINFO*>(&BitmapInfoHeader), DIB_RGB_COLORS);

	// Cleanup GDI objects
	SelectObject(MemDC, OldBitmap);
	DeleteObject(Bitmap);
	DeleteDC(MemDC);
	ReleaseDC(hwnd, WindowDC);

	if (ScanLines == 0)
	{
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Failed to get bitmap bits");
		return;
	}

	// Save as PNG
	if (SaveBitmapAsPNG(PixelData, Width, Height, FilePath))
	{
		OutResult.bSuccess = true;
		OutResult.Message = FString::Printf(TEXT("Screenshot saved successfully (%dx%d)"), Width, Height);
	}
	else
	{
		OutResult.bSuccess = false;
		OutResult.Message = TEXT("Failed to save PNG file");
	}
#else
	OutResult.bSuccess = false;
	OutResult.Message = TEXT("Screenshot capture only supported on Windows");
#endif
}

bool UScreenshotService::SaveBitmapAsPNG(const TArray<uint8>& BitmapData, int32 Width, int32 Height, const FString& FilePath)
{
	// Ensure directory exists
	FString Directory = FPaths::GetPath(FilePath);
	if (!Directory.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*Directory))
		{
			PlatformFile.CreateDirectoryTree(*Directory);
		}
	}

	// Convert BGRA to RGBA for image wrapper
	TArray<FColor> Colors;
	Colors.SetNum(Width * Height);

	int32 RowSize = ((Width * 32 + 31) / 32) * 4;
	
	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			int32 SrcIndex = Y * RowSize + X * 4;
			int32 DstIndex = Y * Width + X;
			
			if (SrcIndex + 3 < BitmapData.Num())
			{
				// BGRA to RGBA
				Colors[DstIndex].B = BitmapData[SrcIndex + 0];
				Colors[DstIndex].G = BitmapData[SrcIndex + 1];
				Colors[DstIndex].R = BitmapData[SrcIndex + 2];
				Colors[DstIndex].A = 255; // Force full opacity
			}
		}
	}

	// Use Unreal's image wrapper to save as PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		return false;
	}

	// Set raw data
	TArray<uint8> RawData;
	RawData.SetNum(Width * Height * 4);
	FMemory::Memcpy(RawData.GetData(), Colors.GetData(), RawData.Num());

	if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::RGBA, 8))
	{
		return false;
	}

	// Get compressed PNG data
	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(0);
	if (CompressedData.Num() == 0)
	{
		return false;
	}

	// Save to file
	return FFileHelper::SaveArrayToFile(CompressedData, *FilePath);
}
