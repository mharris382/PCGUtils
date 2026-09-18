// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Chaos/CacheCollection.h"
#include "Chaos/ChaosCache.h"
#include "Elements/Conversion/PCGLoadChaosCacheAsset.h"
#include "Elements/Fracture/PCGPlanarFracture.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PCGPin.h"

// The fracture module's shared fixtures (build a solid, run one element outside a graph). Test-only reuse of a
// header-only file; this module already depends on PCGUtilsFracture and everything that header includes.
#include "../../../PCGUtilsFracture/Private/Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGLoadChaosCacheAssetTests
{
	using namespace PCGUtilsFractureTests;

	/**
	 * Bone layout of the fixture: DynMesh To GC adds a cluster root above the geometry bone, and one slicing
	 * plane turns that geometry bone into a cluster of two pieces.
	 */
	constexpr int32 Root = 0;
	constexpr int32 Cluster = 1;
	constexpr int32 PieceA = 2;
	constexpr int32 PieceB = 3;

	constexpr double Tolerance = 1e-2;

	/** Where the Cache Manager stood during the recording. Deliberately not identity. */
	const FTransform CacheManagerToWorld(FQuat(FVector::ZAxisVector, UE_HALF_PI), FVector(100.0, -40.0, 5.0));

	UGeometryCollection* MakeRestCollection()
	{
		UPCGSliceFractureSettings* SliceSettings = NewObject<UPCGSliceFractureSettings>();
		SliceSettings->SlicesX = 1;
		SliceSettings->SlicesY = 0;
		SliceSettings->SlicesZ = 0;
		const UPCGUtilsFractureFactoryData* Operation =
			FirstOutput<UPCGUtilsFractureFactoryData>(Run(SliceSettings, {}));
		const UPCGGeometryCollectionData* Sliced = FirstOutput<UPCGGeometryCollectionData>(
			Run(NewObject<UPCGFractureGeometryCollectionSettings>(), {
				{PCGFractureGeometryCollectionConstants::CollectionInputPin, ToCollection(Box())},
				{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
		if (!Sliced)
		{
			return nullptr;
		}

		UGeometryCollection* Asset = NewObject<UGeometryCollection>();
		Sliced->GetCollection().CopyTo(Asset->GetGeometryCollection().Get());
		return Asset;
	}

	TArray<FTransform> RestGlobals(const UGeometryCollection* Asset)
	{
		TArray<FTransform> Globals;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(*Asset->GetGeometryCollection(), Globals);
		return Globals;
	}

	/** World poses of the simulated bones, chosen so each bone's motion is distinct and includes rotation. */
	FTransform RootPose(float T) { return FTransform(FVector(0.0, 0.0, 10.0 * T)); }
	FTransform ClusterPose(float T) { return FTransform(FQuat(FVector::XAxisVector, T), FVector(0.0, 50.0 * T, 20.0)); }
	FTransform PieceAPose(float T) { return FTransform(FQuat(FVector::YAxisVector, 2.0 * T), FVector(-30.0 * T, 0.0, 0.0)); }
	FTransform PieceBPose(float T) { return FTransform(FVector(30.0 * T, 0.0, -5.0 * T)); }

	float KeyTime(int32 Frame) { return 0.1f * static_cast<float>(Frame); }

	/**
	 * Adds one particle track in exactly the layout UChaosCache::FlushPendingFrames writes: keys in Cache Manager
	 * space, BeginOffset at the first key, scale keys of one, and bDeactivateOnEnd on a released cluster.
	 *
	 * Written into the public fields rather than through AddFrame_Concurrent because FPendingFrameWrite owns a
	 * TMap of FCacheEventTrack, whose destructor ChaosCaching does not export.
	 */
	void AddTrack(UChaosCache* Cache, int32 Bone, int32 FirstFrame, int32 LastFrame, bool bDeactivateOnEnd,
		TFunctionRef<FTransform(float)> WorldPose)
	{
		FPerParticleCacheData& Data = Cache->ParticleTracks.AddDefaulted_GetRef();
		Cache->TrackToParticle.Add(Bone);

		FParticleTransformTrack& Track = Data.TransformData;
		Track.BeginOffset = KeyTime(FirstFrame);
		Track.bDeactivateOnEnd = bDeactivateOnEnd;
		for (int32 Frame = FirstFrame; Frame <= LastFrame; ++Frame)
		{
			const FTransform Recorded = WorldPose(KeyTime(Frame)) * CacheManagerToWorld.Inverse();
			Track.KeyTimestamps.Add(KeyTime(Frame));
			Track.RawTransformTrack.PosKeys.Add(FVector3f(Recorded.GetTranslation()));
			Track.RawTransformTrack.RotKeys.Add(FQuat4f(Recorded.GetRotation()));
			Track.RawTransformTrack.ScaleKeys.Add(FVector3f(1.0f));
		}
	}

	/**
	 * A cache recorded the way the Geometry Collection adapter records a breaking collection: the root simulates
	 * as one body until it releases at 0.5s, the inner cluster then flies alone until it releases at 0.8s, and
	 * the two pieces fly independently to the end at 1.0s. The component sits at the world origin.
	 */
	UChaosCache* RecordBreakingCache(UChaosCacheCollection* Collection, UGeometryCollection* Asset, FName Name)
	{
		UGeometryCollectionComponent* Component = NewObject<UGeometryCollectionComponent>(GetTransientPackage());
		Component->SetRestCollection(Asset, /*bApplyAssetDefaults=*/false);

		UChaosCache* Cache = Collection->FindOrAddCache(Name);
		Cache->BuildSpawnableFromComponent(Component, CacheManagerToWorld);

		AddTrack(Cache, Root, 1, 5, /*bDeactivateOnEnd=*/true, RootPose);
		AddTrack(Cache, Cluster, 5, 8, /*bDeactivateOnEnd=*/true, ClusterPose);
		AddTrack(Cache, PieceA, 8, 10, /*bDeactivateOnEnd=*/false, PieceAPose);
		AddTrack(Cache, PieceB, 8, 10, /*bDeactivateOnEnd=*/false, PieceBPose);
		Cache->RecordedDuration = KeyTime(10) - KeyTime(1);
		Cache->NumRecordedFrames = 10;
		return Cache;
	}

	UPCGLoadChaosCacheAssetSettings* MakeSettings(UChaosCacheCollection* Collection, float Time, bool bNormalized = false)
	{
		UPCGLoadChaosCacheAssetSettings* Settings = NewObject<UPCGLoadChaosCacheAssetSettings>();
		Settings->CacheCollection = Collection;
		Settings->SampleTime = Time;
		Settings->bNormalizedTime = bNormalized;
		Settings->bSynchronousLoad = true;
		return Settings;
	}

	TArray<FTransform> SampleGlobals(UPCGLoadChaosCacheAssetSettings* Settings)
	{
		const UPCGGeometryCollectionData* Data = FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {}));
		return Data ? GlobalTransforms(Data) : TArray<FTransform>();
	}

	/** Where a bone that is not simulated ends up: rigidly attached to the simulated ancestor it rests under. */
	FTransform Follow(const TArray<FTransform>& Rest, int32 Bone, int32 Anchor, const FTransform& AnchorPose)
	{
		return Rest[Bone] * Rest[Anchor].Inverse() * AnchorPose;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGLoadChaosCacheAssetContractTest,
	"PCGUtils.ChaosCache.LoadChaosCache.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGLoadChaosCacheAssetContractTest::RunTest(const FString&)
{
	const UPCGLoadChaosCacheAssetSettings* Defaults = GetDefault<UPCGLoadChaosCacheAssetSettings>();
	TestEqual(TEXT("Palette title"), Defaults->GetDefaultNodeTitle().ToString(), FString(TEXT("GC | Load Chaos Cache")));

	TArray<FPCGPinProperties> Inputs = Defaults->AllInputPinProperties();
	Inputs.RemoveAll([](const FPCGPinProperties& Pin) { return Pin.IsAdvancedPin(); });
	TestEqual(TEXT("A source node: no input pins"), Inputs.Num(), 0);

	const TArray<FPCGPinProperties> Outputs = Defaults->AllOutputPinProperties();
	if (TestEqual(TEXT("One output pin"), Outputs.Num(), 1))
	{
		TestEqual(TEXT("It is the GC pin"), Outputs[0].Label, PCGLoadChaosCacheAssetConstants::CollectionOutputPin);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGLoadChaosCacheAssetSampleTest,
	"PCGUtils.ChaosCache.LoadChaosCache.Sample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGLoadChaosCacheAssetSampleTest::RunTest(const FString&)
{
	using namespace PCGLoadChaosCacheAssetTests;

	UGeometryCollection* Asset = MakeRestCollection();
	if (!TestNotNull(TEXT("A sliced rest collection was built"), Asset))
	{
		return false;
	}
	const TArray<FTransform> Rest = RestGlobals(Asset);
	if (!TestEqual(TEXT("Root, cluster and two pieces"), Rest.Num(), 4))
	{
		return false;
	}

	UChaosCacheCollection* Collection = NewObject<UChaosCacheCollection>();
	RecordBreakingCache(Collection, Asset, TEXT("Wall"));

	auto ExpectPose = [this](const TCHAR* What, const TArray<FTransform>& Globals, int32 Bone, const FTransform& Expected)
	{
		if (!Globals.IsValidIndex(Bone))
		{
			AddError(FString::Printf(TEXT("%s: bone %d missing"), What, Bone));
			return;
		}
		TestTrue(FString::Printf(TEXT("%s: position (got %s, expected %s)"), What,
			*Globals[Bone].GetTranslation().ToString(), *Expected.GetTranslation().ToString()),
			Globals[Bone].GetTranslation().Equals(Expected.GetTranslation(), Tolerance));
		TestTrue(FString::Printf(TEXT("%s: rotation"), What),
			Globals[Bone].GetRotation().Equals(Expected.GetRotation(), 1e-3));
	};

	// --- Intact: only the root simulates, and everything beneath it rides along rigidly.
	{
		const TArray<FTransform> Globals = SampleGlobals(MakeSettings(Collection, KeyTime(3)));
		ExpectPose(TEXT("0.3s root"), Globals, Root, RootPose(KeyTime(3)));
		ExpectPose(TEXT("0.3s piece A follows the root"), Globals, PieceA, Follow(Rest, PieceA, Root, RootPose(KeyTime(3))));
		ExpectPose(TEXT("0.3s piece B follows the root"), Globals, PieceB, Follow(Rest, PieceB, Root, RootPose(KeyTime(3))));
	}

	// --- Root released: it holds its release pose, and the pieces now follow the inner cluster instead.
	{
		const TArray<FTransform> Globals = SampleGlobals(MakeSettings(Collection, KeyTime(7)));
		ExpectPose(TEXT("0.7s released root holds its release pose"), Globals, Root, RootPose(KeyTime(5)));
		ExpectPose(TEXT("0.7s cluster"), Globals, Cluster, ClusterPose(KeyTime(7)));
		ExpectPose(TEXT("0.7s piece A follows the cluster"), Globals, PieceA, Follow(Rest, PieceA, Cluster, ClusterPose(KeyTime(7))));
	}

	// --- Fully broken: every piece is where it was recorded, independently of both released ancestors.
	{
		const TArray<FTransform> Globals = SampleGlobals(MakeSettings(Collection, KeyTime(10)));
		ExpectPose(TEXT("1.0s cluster holds its release pose"), Globals, Cluster, ClusterPose(KeyTime(8)));
		ExpectPose(TEXT("1.0s piece A"), Globals, PieceA, PieceAPose(KeyTime(10)));
		ExpectPose(TEXT("1.0s piece B"), Globals, PieceB, PieceBPose(KeyTime(10)));
	}

	// --- Normalized time spans 0 to the last key.
	{
		const TArray<FTransform> Globals = SampleGlobals(MakeSettings(Collection, 0.7f, /*bNormalized=*/true));
		ExpectPose(TEXT("Normalized 0.7 is 0.7s"), Globals, Cluster, ClusterPose(KeyTime(7)));
	}

	// --- Out of range: a warning naming the range, and the time is clamped rather than extrapolated.
	{
		AddExpectedMessagePlain(TEXT("is outside the recorded range of cache 'Wall', 0 to 1.00s"), ELogVerbosity::Warning);
		const TArray<FTransform> Globals = SampleGlobals(MakeSettings(Collection, 5.0f));
		ExpectPose(TEXT("5s clamps to the end"), Globals, PieceB, PieceBPose(KeyTime(10)));

		AddExpectedMessagePlain(TEXT("is outside the normalized range 0 to 1"), ELogVerbosity::Warning);
		const TArray<FTransform> Normalized = SampleGlobals(MakeSettings(Collection, -0.5f, /*bNormalized=*/true));
		TestEqual(TEXT("Negative normalized time clamps to the start and still emits"), Normalized.Num(), 4);
	}

	// --- Space: Cache Manager places the collection where the recording had it relative to the manager.
	{
		UPCGLoadChaosCacheAssetSettings* Settings = MakeSettings(Collection, KeyTime(10));
		Settings->Space = EPCGChaosCacheSampleSpace::CacheManager;
		const TArray<FTransform> Globals = SampleGlobals(Settings);
		ExpectPose(TEXT("Cache Manager space"), Globals, PieceA, PieceAPose(KeyTime(10)) * CacheManagerToWorld.Inverse());
	}

	// --- Each output is tagged with its cache's name.
	{
		const TArray<FPCGTaggedData> Outputs = PCGUtilsFractureTests::Run(MakeSettings(Collection, 0.0f), {});
		if (TestEqual(TEXT("One output per cache"), Outputs.Num(), 1))
		{
			TestTrue(TEXT("Tagged with the cache name"), Outputs[0].Tags.Contains(TEXT("Wall")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGLoadChaosCacheAssetDiagnosticsTest,
	"PCGUtils.ChaosCache.LoadChaosCache.Diagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGLoadChaosCacheAssetDiagnosticsTest::RunTest(const FString&)
{
	using namespace PCGLoadChaosCacheAssetTests;

	auto CountOutputs = [](UPCGLoadChaosCacheAssetSettings* Settings)
	{
		return PCGUtilsFractureTests::Run(Settings, {}).FilterByPredicate(
			[](const FPCGTaggedData& Tagged) { return Cast<UPCGGeometryCollectionData>(Tagged.Data) != nullptr; }).Num();
	};

	// --- No asset is an error.
	AddExpectedMessagePlain(TEXT("has no Chaos Cache Collection asset selected"), ELogVerbosity::Error);
	TestEqual(TEXT("No asset, no output"), CountOutputs(MakeSettings(nullptr, 0.0f)), 0);

	UGeometryCollection* Asset = MakeRestCollection();
	if (!TestNotNull(TEXT("A sliced rest collection was built"), Asset))
	{
		return false;
	}

	// --- A collection that was never recorded holds no caches.
	{
		UChaosCacheCollection* Empty = NewObject<UChaosCacheCollection>();
		AddExpectedMessagePlain(TEXT("holds no caches: it has not been recorded"), ELogVerbosity::Warning);
		TestEqual(TEXT("Empty collection, no output"), CountOutputs(MakeSettings(Empty, 0.0f)), 0);
	}

	// --- An unrecorded cache is a warning. Without geometry nothing is emitted; with an override, the rest state is.
	{
		UChaosCacheCollection* Unrecorded = NewObject<UChaosCacheCollection>();
		Unrecorded->FindOrAddCache(TEXT("Pending"));

		AddExpectedMessagePlain(TEXT("no recorded component, so there is no geometry to emit"), ELogVerbosity::Warning);
		TestEqual(TEXT("Unrecorded, no geometry, no output"), CountOutputs(MakeSettings(Unrecorded, 0.0f)), 0);

		UPCGLoadChaosCacheAssetSettings* WithOverride = MakeSettings(Unrecorded, 0.0f);
		WithOverride->RestCollectionOverride = Asset;
		AddExpectedMessagePlain(TEXT("Emitting the unsimulated rest state"), ELogVerbosity::Warning);
		TestEqual(TEXT("Unrecorded with an override emits the rest state"), CountOutputs(WithOverride), 1);
	}

	// --- A cache name that does not exist is an error naming what does.
	{
		UChaosCacheCollection* Collection = NewObject<UChaosCacheCollection>();
		RecordBreakingCache(Collection, Asset, TEXT("Wall"));

		UPCGLoadChaosCacheAssetSettings* Settings = MakeSettings(Collection, 0.5f);
		Settings->CacheName = TEXT("Tower");
		AddExpectedMessagePlain(TEXT("has no cache named 'Tower'. It holds: Wall"), ELogVerbosity::Error);
		TestEqual(TEXT("Unknown cache, no output"), CountOutputs(Settings), 0);

		Settings->CacheName = TEXT("Wall");
		TestEqual(TEXT("Named cache samples"), CountOutputs(Settings), 1);
	}

	// --- A track recorded against a different collection is an error naming the track and the bone.
	{
		UChaosCacheCollection* Collection = NewObject<UChaosCacheCollection>();
		UChaosCache* Cache = RecordBreakingCache(Collection, Asset, TEXT("Mismatched"));
		AddTrack(Cache, 99, 1, 2, /*bDeactivateOnEnd=*/false, RootPose);

		AddExpectedMessagePlain(TEXT("track 4 records bone 99"), ELogVerbosity::Error);
		TestEqual(TEXT("Mismatched cache, no output"), CountOutputs(MakeSettings(Collection, 0.5f)), 0);
	}

	return true;
}

#endif
