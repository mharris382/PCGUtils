// Copyright Max Harris
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Creation/PCGDynMeshFromStaticMeshPoints.h"
#include "Elements/Creation/PCGSplineToDynamicMesh.h"
#include "FunctionLibraries/PCGUtilsSplineMeshFunctions.h"
#include "Elements/Conversion/PCGDynMeshToPoints.h"
#include "Elements/Conversion/PCGDynMeshSample.h"
#include "Elements/Output/PCGDynMeshComponentOutput.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Elements/PCGDebugElement.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Misc/ScopeExit.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Tests/AutomationCommon.h"
#include "UDynamicMesh.h"

namespace PCGUtilsCoordinateSpaceTests
{
	using namespace UE::Geometry;
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	AActor* Actor(UWorld* World, const FTransform& Transform)
	{
		AActor* Result = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Result);
		Result->SetRootComponent(Root);
		Root->RegisterComponent();
		Result->SetActorTransform(Transform);
		return Result;
	}

	FPCGDataCollection Run(UPCGSettings* Settings, UPCGData* Data, UPCGComponent* Component, FName Pin = PCGPinConstants::DefaultInputLabel)
	{
		FPCGDataCollection Inputs;
		FPCGTaggedData& Input = Inputs.TaggedData.Emplace_GetRef();
		Input.Data = Data;
		Input.Pin = Pin;
		Input.Tags.Add(TEXT("AuthoredSource"));
		Inputs.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Inputs, Component, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Attempt = 0; Attempt < 20; ++Attempt)
		{
			if (Element->Execute(Context.Get())) return Context->OutputData;
		}
		return {};
	}

	UPCGDynamicMeshData* Triangle()
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();
		Mesh.AppendVertex(FVector3d(2, 3, 4));
		Mesh.AppendVertex(FVector3d(6, 5, 5));
		Mesh.AppendVertex(FVector3d(1, 9, 6));
		int32 TID = Mesh.AppendTriangle(0, 1, 2);
		const FVector3f Normal(Mesh.GetTriNormal(TID));
		FIndex3i NormalIDs;
		for (int32 I = 0; I < 3; ++I) NormalIDs[I] = Mesh.Attributes()->PrimaryNormals()->AppendElement(Normal);
		Mesh.Attributes()->PrimaryNormals()->SetTriangle(TID, NormalIDs);
		auto* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	TArray<UPCGDynMeshOutputManagedComponent*> Resources(UPCGComponent* Component)
	{
		TArray<UPCGDynMeshOutputManagedComponent*> Result;
		Component->ForEachManagedResource([&](UPCGManagedResource* Resource)
		{
			if (auto* Output = Cast<UPCGDynMeshOutputManagedComponent>(Resource)) Result.Add(Output);
		});
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshAffineSpaceTest, "PCGUtils.DynMesh.CoordinateSpaces.ExactFrameConversion", PCGUtilsCoordinateSpaceTests::Flags)
bool FPCGUtilsDynMeshAffineSpaceTest::RunTest(const FString&)
{
	using namespace PCGUtilsCoordinateSpaceTests;
	const FTransform Source(FRotator(24, 61, -13), FVector(120, -70, 43), FVector(1.3, 0.7, 2.1));
	for (const FVector Scale : {FVector(2.0, 0.5, 1.7), FVector(-2.0, 0.5, 1.7)})
	{
		const FTransform Destination(FRotator(-17, 39, 28), FVector(-310, 85, 19), Scale);
		FDynamicMesh3 Mesh = Triangle()->GetDynamicMesh()->GetMeshRef();
		const FDynamicMesh3 Original = Mesh;
		PCGUtilsDynMeshSpaceHelpers::TransformMeshBetweenSpaces(Mesh, Source, Destination);
		for (int32 VID : Mesh.VertexIndicesItr())
		{
			const FVector ExpectedWorld = Source.TransformPosition(FVector(Original.GetVertex(VID)));
			const FVector ActualWorld = Destination.TransformPosition(FVector(Mesh.GetVertex(VID)));
			TestTrue(TEXT("Exact affine placement survives rotated nonuniform or reflected target scale"), ActualWorld.Equals(ExpectedWorld, 1e-6));
		}
		const FVector3d GeometricNormal = Mesh.GetTriNormal(0);
		const auto* Normals = Mesh.Attributes()->PrimaryNormals();
		TestTrue(TEXT("Overlay normals agree with transformed winding"), FVector3d(Normals->GetElement(Normals->GetTriangle(0).A)).Dot(GeometricNormal) > 0.99999);
		PCGUtilsDynMeshSpaceHelpers::TransformMeshBetweenSpaces(Mesh, Destination, Source);
		for (int32 VID : Mesh.VertexIndicesItr()) TestTrue(TEXT("Frame round trip preserves vertices"), Mesh.GetVertex(VID).Equals(Original.GetVertex(VID), 1e-6));
		TestTrue(TEXT("Frame round trip preserves winding"), Mesh.GetTriangle(0) == Original.GetTriangle(0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshFromPointsSpaceTest, "PCGUtils.DynMesh.CoordinateSpaces.FromPoints", PCGUtilsCoordinateSpaceTests::Flags)
bool FPCGUtilsDynMeshFromPointsSpaceTest::RunTest(const FString&)
{
	using namespace PCGUtilsCoordinateSpaceTests;
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Editor)) return false;
	ON_SCOPE_EXIT { World.DestroyTestWorld(true); };
	AActor* Owner = Actor(World.GetTestWorld(), FTransform(FRotator(21, 73, -19), FVector(110, 240, -30), FVector(2, 0.6, 1.4)));
	AActor* Foreign = Actor(World.GetTestWorld(), FTransform(FRotator(0, 110, 0), FVector(-800, 40, 23)));
	auto* Component = NewObject<UPCGComponent>(Owner);
	auto* Points = NewObject<UPCGPointData>();
	Points->TargetActor = Foreign;
	Points->GetMutablePoints().Emplace_GetRef().Transform = FTransform(FRotator(-12, 43, 17), FVector(200, -100, 91), FVector(1.2, 0.8, 0.7));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube fixture"), Cube)) return false;
	Points->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(Cube), false, false);
	auto* Settings = NewObject<UPCGDynMeshFromStaticMeshPointsSettings>();
	TestTrue(TEXT("From Points defaults to automatic conversion"), Settings->bConvertToLocalSpace);
	Settings->bConvertToLocalSpace = false;
	const auto WorldOutput = Run(Settings, Points, Component);
	Settings->bConvertToLocalSpace = true;
	const auto LocalOutput = Run(Settings, Points, Component);
	if (!TestEqual(TEXT("World-space reference output"), WorldOutput.TaggedData.Num(), 1)
		|| !TestEqual(TEXT("Actor-local output"), LocalOutput.TaggedData.Num(), 1)) return false;
	const auto* WorldData = CastChecked<UPCGDynamicMeshData>(WorldOutput.TaggedData[0].Data);
	const auto* LocalData = CastChecked<UPCGDynamicMeshData>(LocalOutput.TaggedData[0].Data);
	TestTrue(TEXT("Output frame belongs to execution actor, not upstream source actor"), LocalData->TargetActor.Get() == Owner);
	TestTrue(TEXT("Input tags survive conversion"), LocalOutput.TaggedData[0].Tags.Contains(TEXT("AuthoredSource")));
	const auto& LocalMesh = LocalData->GetDynamicMesh()->GetMeshRef();
	const auto& WorldMesh = WorldData->GetDynamicMesh()->GetMeshRef();
	for (int32 VID : LocalMesh.VertexIndicesItr())
	{
		TestTrue(TEXT("Local output reproduces authored world-space vertex"), Owner->GetActorTransform().TransformPosition(FVector(LocalMesh.GetVertex(VID))).Equals(FVector(WorldMesh.GetVertex(VID)), 1e-5));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshPointNormalScaleTest, "PCGUtils.DynMesh.CoordinateSpaces.ToPointsScaledNormals", PCGUtilsCoordinateSpaceTests::Flags)
bool FPCGUtilsDynMeshPointNormalScaleTest::RunTest(const FString&)
{
	using namespace PCGUtilsCoordinateSpaceTests;
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Editor)) return false;
	ON_SCOPE_EXIT { World.DestroyTestWorld(true); };
	AActor* Owner = Actor(World.GetTestWorld(), FTransform(FRotator(13, 42, 28), FVector(100, -70, 210), FVector(2.0, 0.4, 1.6)));
	auto* Input = Triangle();
	Input->TargetActor = Owner;
	auto* Settings = NewObject<UPCGDynMeshToPointsSettings>();
	TestTrue(TEXT("To Points defaults to world output"), Settings->bOutputToWorldSpace);
	const auto Output = Run(Settings, Input, nullptr, PCGDynMeshToPointsConstants::InDynamicMeshLabel);
	if (!TestEqual(TEXT("One point dataset"), Output.TaggedData.Num(), 1)) return false;
	const auto* Points = CastChecked<UPCGBasePointData>(Output.TaggedData[0].Data);
	const auto Transforms = Points->GetConstTransformValueRange();
	const auto& Mesh = Input->GetDynamicMesh()->GetMeshRef();
	const FTransform Frame = Owner->GetActorTransform();
	const FVector A = Frame.TransformPosition(FVector(Mesh.GetVertex(0)));
	const FVector B = Frame.TransformPosition(FVector(Mesh.GetVertex(1)));
	const FVector C = Frame.TransformPosition(FVector(Mesh.GetVertex(2)));
	// Unreal mesh triangle normals use the left-handed winding convention: edge 2 cross edge 1.
	const FVector ExpectedNormal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
	for (int32 I = 0; I < Points->GetNumPoints(); ++I)
	{
		TestTrue(TEXT("Point follows scaled surface normal"), Transforms[I].GetUnitAxis(EAxis::Z).Equals(ExpectedNormal, 1e-5));
		TestTrue(TEXT("Point position includes full actor transform"), Transforms[I].GetLocation().Equals(Frame.TransformPosition(FVector(Mesh.GetVertex(I))), 1e-6));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshComponentSpaceTest, "PCGUtils.DynMesh.CoordinateSpaces.ComponentOutput", PCGUtilsCoordinateSpaceTests::Flags)
bool FPCGUtilsDynMeshComponentSpaceTest::RunTest(const FString&)
{
	using namespace PCGUtilsCoordinateSpaceTests;
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Editor)) return false;
	ON_SCOPE_EXIT { World.DestroyTestWorld(true); };
	AActor* Owner = Actor(World.GetTestWorld(), FTransform(FRotator(19, 68, -12), FVector(80, -110, 37), FVector(2, 0.5, 1.3)));
	AActor* DisplayOwner = Actor(World.GetTestWorld(), FTransform(FRotator(14, 147, -23), FVector(900, 20, 12), FVector(1.8, 0.6, 2.1)));
	auto* Source = NewObject<UPCGComponent>(Owner);
	auto* Input = Triangle();
	Input->TargetActor = Owner;
	auto* Debug = NewObject<UPCGDynMeshDebugSettings>();
	auto* Spawn = NewObject<UPCGDynMeshSpawnSettings>();
	// Reproduce the native convention separately: rotation and scale stay world-identity on initial output.
	auto* NativeSettings = NewObject<UPCGDebugSettings>();
	FPCGContext NativeContext;
	NativeContext.ExecutionSource = Source;
	auto* NativeResource = PCGDynamicMeshManagedComponent::GetOrCreateDynamicMeshManagedComponent(
		&NativeContext, NativeSettings, Input, Owner, EPCGEditorDirtyMode::Preview);
	if (!TestNotNull(TEXT("Native debug comparison fixture"), NativeResource)) return false;
	TestTrue(TEXT("Native DynMesh display uses identity world rotation"), NativeResource->GetComponent()->GetComponentQuat().Equals(FQuat::Identity, 1e-6));
	TestTrue(TEXT("Native DynMesh display uses unit world scale"), NativeResource->GetComponent()->GetComponentScale().Equals(FVector::OneVector, 1e-6));
	Debug->TargetActor = DisplayOwner;
	Spawn->TargetActor = DisplayOwner;
	const auto DebugOutput = Run(Debug, Input, Source);
	const auto SpawnOutput = Run(Spawn, Input, Source);
	TestEqual(TEXT("Debug has no output data"), DebugOutput.TaggedData.Num(), 0);
	if (TestEqual(TEXT("Spawn passes through input"), SpawnOutput.TaggedData.Num(), 1)) TestTrue(TEXT("Spawn does not mutate input"), SpawnOutput.TaggedData[0].Data == Input);
	const auto First = Resources(Source);
	if (!TestEqual(TEXT("Debug and Spawn own distinct components"), First.Num(), 2)) return false;
	for (auto* Resource : First)
	{
		TestTrue(TEXT("Output frame includes rotation and scale independent of display owner"), Resource->GetComponent()->GetComponentTransform().Equals(Owner->GetActorTransform(), 1e-6));
		TestTrue(TEXT("Requested actor owns output"), Resource->GetComponent()->GetOwner() == DisplayOwner);
		TestTrue(TEXT("Generated component keeps input tags"), Resource->GetComponent()->ComponentHasTag(TEXT("AuthoredSource")));
	}
	Owner->SetActorTransform(FTransform(FRotator(-22, 121, 31), FVector(-90, 160, -17), FVector(-0.8, 1.7, 2.1)));
	for (auto* Resource : First)
	{
		Resource->MarkAsReused();
		TestTrue(TEXT("Resource reuse restores full current frame"), Resource->GetComponent()->GetComponentTransform().Equals(Owner->GetActorTransform(), 1e-6));
	}
	Run(Spawn, Input, Source);
	TestEqual(TEXT("Regeneration reuses component instead of duplicating"), Resources(Source).Num(), 2);
	Spawn->bInputIsActorLocal = false;
	Run(Spawn, Input, Source);
	for (auto* Resource : Resources(Source))
	{
		if (Resource->OutputSettings == Spawn) TestTrue(TEXT("Explicit world-space input uses identity frame"), Resource->GetComponent()->GetComponentTransform().Equals(FTransform::Identity, 1e-6));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshSamplerScaleTest, "PCGUtils.DynMesh.CoordinateSpaces.SamplerScaledNormals", PCGUtilsCoordinateSpaceTests::Flags)
bool FPCGUtilsDynMeshSamplerScaleTest::RunTest(const FString&)
{
	using namespace PCGUtilsCoordinateSpaceTests;
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Editor)) return false;
	ON_SCOPE_EXIT { World.DestroyTestWorld(true); };
	AActor* Owner = Actor(World.GetTestWorld(), FTransform(FRotator(16, 34, -29), FVector(210, -39, 48), FVector(2, 0.4, 1.7)));
	auto* Input = Triangle();
	Input->TargetActor = Owner;
	auto* Settings = NewObject<UPCGDynMeshSampleSettings>();
	Settings->SamplingMethod = EPCGMeshSamplingMethod::OnePointPerTriangle;
	const auto Output = Run(Settings, Input, nullptr, TEXT("Mesh"));
	if (!TestEqual(TEXT("Sampler output"), Output.TaggedData.Num(), 1)) return false;
	const auto* Points = CastChecked<UPCGBasePointData>(Output.TaggedData[0].Data);
	if (!TestEqual(TEXT("One sample per triangle"), Points->GetNumPoints(), 1)) return false;
	const auto& Mesh = Input->GetDynamicMesh()->GetMeshRef();
	const FTransform Frame = Owner->GetActorTransform();
	const FVector A = Frame.TransformPosition(FVector(Mesh.GetVertex(0)));
	const FVector B = Frame.TransformPosition(FVector(Mesh.GetVertex(1)));
	const FVector C = Frame.TransformPosition(FVector(Mesh.GetVertex(2)));
	const FTransform Point = Points->GetConstTransformValueRange()[0];
	TestTrue(TEXT("Sample follows scaled geometric surface normal"), Point.GetUnitAxis(EAxis::Z).Equals(FVector::CrossProduct(C - A, B - A).GetSafeNormal(), 1e-5));
	TestTrue(TEXT("Sample position is transformed triangle center"), Point.GetLocation().Equals((A + B + C) / 3.0, 1e-6));
	TestTrue(TEXT("Sampler retains bounds scale behavior"), Point.GetScale3D().Equals(Frame.GetScale3D(), 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshSplineScaleTest, "PCGUtils.DynMesh.CoordinateSpaces.SplineMeshPlacement", PCGUtilsCoordinateSpaceTests::Flags)
bool FPCGUtilsDynMeshSplineScaleTest::RunTest(const FString&)
{
	using namespace PCGUtilsCoordinateSpaceTests;
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Editor)) return false;
	ON_SCOPE_EXIT { World.DestroyTestWorld(true); };
	AActor* Owner = Actor(World.GetTestWorld(), FTransform(FRotator(23, 57, -16), FVector(71, -83, 125), FVector(2, 0.5, 1.3)));
	auto* Component = NewObject<UPCGComponent>(Owner);
	auto* Curve = NewObject<USplineComponent>(Owner);
	Curve->SetupAttachment(Owner->GetRootComponent());
	Curve->RegisterComponent();
	Curve->SetSplinePoints(TArray<FVector>{FVector(100, 20, 30), FVector(320, 160, 95)}, ESplineCoordinateSpace::World);
	auto* Input = NewObject<UPCGSplineData>();
	Input->Initialize(Curve);
	Input->TargetActor = Owner;
	auto* Settings = NewObject<UPCGSplineToDynamicMeshSettings>();
	Settings->StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Spline fixture cube"), Settings->StaticMesh.Get())) return false;
	Settings->bUnionSegments = false;
	Settings->bConvertWorldToActorLocal = false;
	const auto WorldOutput = Run(Settings, Input, Component);
	Settings->bConvertWorldToActorLocal = true;
	const auto LocalOutput = Run(Settings, Input, Component);
	if (!TestEqual(TEXT("World spline mesh"), WorldOutput.TaggedData.Num(), 1)
		|| !TestEqual(TEXT("Local spline mesh"), LocalOutput.TaggedData.Num(), 1)) return false;
	const auto& WorldMesh = CastChecked<UPCGDynamicMeshData>(WorldOutput.TaggedData[0].Data)->GetDynamicMesh()->GetMeshRef();
	const auto& LocalMesh = CastChecked<UPCGDynamicMeshData>(LocalOutput.TaggedData[0].Data)->GetDynamicMesh()->GetMeshRef();
	for (int32 VID : LocalMesh.VertexIndicesItr()) TestTrue(TEXT("Spline deformation preserves world placement after actor conversion"), Owner->GetActorTransform().TransformPosition(FVector(LocalMesh.GetVertex(VID))).Equals(FVector(WorldMesh.GetVertex(VID)), 1e-5));

	auto* Authored = NewObject<USplineMeshComponent>(Owner);
	Authored->SetupAttachment(Owner->GetRootComponent());
	Authored->SetStaticMesh(Settings->StaticMesh);
	Authored->SetStartAndEnd(FVector::ZeroVector, FVector(200, 60, 10), FVector(240, 110, 80), FVector(160, 20, 50));
	Authored->SetRelativeTransform(FTransform(FRotator(17, -37, 12), FVector(13, -29, 45), FVector(0.8, 1.4, 0.7)));
	Authored->RegisterComponent();
	UDynamicMesh* ComponentWorld = UPCGUtilsSplineMeshFunctions::SplineMeshComponentToDynamicMesh(Owner, Authored, false, 0);
	UDynamicMesh* ComponentLocal = UPCGUtilsSplineMeshFunctions::SplineMeshComponentToDynamicMesh(Owner, Authored, true, 0);
	if (!TestNotNull(TEXT("Authored spline world mesh"), ComponentWorld) || !TestNotNull(TEXT("Authored spline local mesh"), ComponentLocal)) return false;
	for (int32 VID : ComponentLocal->GetMeshRef().VertexIndicesItr()) TestTrue(TEXT("Spline component reader preserves exact world placement"), Owner->GetActorTransform().TransformPosition(FVector(ComponentLocal->GetMeshRef().GetVertex(VID))).Equals(FVector(ComponentWorld->GetMeshRef().GetVertex(VID)), 1e-5));
	return true;
}

#endif
