// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UBenchmarkService.h"

// Editor / Engine
#include "Editor.h"

// Timing
#include "HAL/PlatformTime.h"

// UObject reflection
#include "UObject/Class.h"
#include "UObject/UObjectHash.h"

// JSON
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyBenchmark, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a single JSON object to a compact FString. */
	FString BenchmarkJsonObjectToString(const TSharedRef<FJsonObject>& JsonObj)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj, Writer);
		return OutStr;
	}

	/** Resolve a Revvy service class by bare name (leading 'U' optional). */
	UClass* BenchmarkResolveRevvyServiceClass(const FString& ServiceClassName)
	{
		if (ServiceClassName.IsEmpty()) { return nullptr; }

		FString BareName = ServiceClassName;
		if (BareName.StartsWith(TEXT("U")))
		{
			BareName.RemoveAt(0, 1);
		}
		const FString FullPath = FString::Printf(TEXT("/Script/Revvy.%s"), *BareName);
		return FindObject<UClass>(nullptr, *FullPath);
	}
}

// =================================================================
// BenchmarkTimestampCall
// =================================================================

FString UBenchmarkService::BenchmarkTimestampCall(int32 Iterations)
{
	const int32 SafeIters = FMath::Max(Iterations, 1);

	// Volatile accumulator so the optimizer cannot elide the loop body.
	volatile double Sink = 0.0;

	const double StartSec = FPlatformTime::Seconds();
	for (int32 i = 0; i < SafeIters; ++i)
	{
		Sink += FPlatformTime::Seconds();
	}
	const double EndSec = FPlatformTime::Seconds();
	(void)Sink;

	const double TotalSec = EndSec - StartSec;
	const double TotalMs  = TotalSec * 1000.0;
	const double AvgUs    = (TotalSec / static_cast<double>(SafeIters)) * 1'000'000.0;
	const double OpsPerSec = TotalSec > 0.0
		? static_cast<double>(SafeIters) / TotalSec
		: 0.0;

	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetNumberField(TEXT("iterations"),  SafeIters);
	Entry->SetNumberField(TEXT("total_ms"),    TotalMs);
	Entry->SetNumberField(TEXT("avg_us"),      AvgUs);
	Entry->SetNumberField(TEXT("ops_per_sec"), OpsPerSec);

	UE_LOG(LogRevvyBenchmark, Verbose,
		TEXT("BenchmarkTimestampCall: iters=%d total_ms=%.4f avg_us=%.4f"),
		SafeIters, TotalMs, AvgUs);

	return BenchmarkJsonObjectToString(Entry);
}

// =================================================================
// MeasureServiceLoadTime
// =================================================================

FString UBenchmarkService::MeasureServiceLoadTime(const FString& ServiceClassName)
{
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();

	if (ServiceClassName.IsEmpty())
	{
		UE_LOG(LogRevvyBenchmark, Warning,
			TEXT("MeasureServiceLoadTime: empty ServiceClassName."));
		Entry->SetStringField(TEXT("service"),       TEXT(""));
		Entry->SetNumberField(TEXT("cdo_access_ns"), 0);
		Entry->SetNumberField(TEXT("ufn_iter_ns"),   0);
		Entry->SetNumberField(TEXT("class_flags"),   0);
		Entry->SetStringField(TEXT("error"),         TEXT("empty"));
		Entry->SetStringField(TEXT("note"),          TEXT("empty service class name"));
		return BenchmarkJsonObjectToString(Entry);
	}

	UClass* Cls = BenchmarkResolveRevvyServiceClass(ServiceClassName);
	if (!Cls)
	{
		UE_LOG(LogRevvyBenchmark, Warning,
			TEXT("MeasureServiceLoadTime: class not found for '%s'."), *ServiceClassName);
		Entry->SetStringField(TEXT("service"),       ServiceClassName);
		Entry->SetNumberField(TEXT("cdo_access_ns"), 0);
		Entry->SetNumberField(TEXT("ufn_iter_ns"),   0);
		Entry->SetNumberField(TEXT("class_flags"),   0);
		Entry->SetStringField(TEXT("error"),         TEXT("not_found"));
		Entry->SetStringField(TEXT("note"),          TEXT("class not loaded"));
		return BenchmarkJsonObjectToString(Entry);
	}

	constexpr int32 SampleIters = 100;

	// ---- CDO access latency ----
	volatile UObject* CdoSink = nullptr;
	const double CdoStart = FPlatformTime::Seconds();
	for (int32 i = 0; i < SampleIters; ++i)
	{
		CdoSink = Cls->GetDefaultObject();
	}
	const double CdoEnd = FPlatformTime::Seconds();
	(void)CdoSink;

	// ---- FieldMap access latency (UFunction field iteration) ----
	volatile int32 FnSink = 0;
	const double FnStart = FPlatformTime::Seconds();
	for (int32 i = 0; i < SampleIters; ++i)
	{
		int32 LocalCount = 0;
		for (TFieldIterator<UFunction> It(Cls); It; ++It)
		{
			if (*It) { ++LocalCount; }
		}
		FnSink += LocalCount;
	}
	const double FnEnd = FPlatformTime::Seconds();
	(void)FnSink;

	const double CdoAccessNs = ((CdoEnd - CdoStart) / static_cast<double>(SampleIters)) * 1'000'000'000.0;
	const double UfnIterNs   = ((FnEnd  - FnStart)  / static_cast<double>(SampleIters)) * 1'000'000'000.0;

	Entry->SetStringField(TEXT("service"),       Cls->GetName());
	Entry->SetNumberField(TEXT("cdo_access_ns"), CdoAccessNs);
	Entry->SetNumberField(TEXT("ufn_iter_ns"),   UfnIterNs);
	Entry->SetNumberField(TEXT("class_flags"),   static_cast<double>(static_cast<uint64>(Cls->GetClassFlags())));

	UE_LOG(LogRevvyBenchmark, Verbose,
		TEXT("MeasureServiceLoadTime: %s cdo_ns=%.2f ufn_ns=%.2f"),
		*Cls->GetName(), CdoAccessNs, UfnIterNs);

	return BenchmarkJsonObjectToString(Entry);
}
