#include "Elements/UV/PCGDynamicMeshUVProject.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#define LOCTEXT_NAMESPACE "PCGDynamicMeshUVProject"

namespace
{
	constexpr double DirectionTolerance = UE_DOUBLE_SMALL_NUMBER;

	bool BuildProjector(const FTransform& PointTransform, FVector Direction,
		FPCGDynamicMeshUVProjector& OutProjector)
	{
		const FVector Origin = PointTransform.GetLocation();
		FQuat Orientation = PointTransform.GetRotation();
		if (Origin.ContainsNaN() || Direction.ContainsNaN() || Orientation.ContainsNaN()
			|| Orientation.SizeSquared() <= DirectionTolerance || !Direction.Normalize())
		{
			return false;
		}

		Orientation.Normalize();

		// Point Right is the preferred U reference. It gives Forward -> (U=Right, V=Up), while Gram-Schmidt
		// projection preserves point-rotation roll for arbitrary direction attributes. If Right is parallel to
		// the selected normal, the other point-rotation axes provide deterministic fallbacks.
		const FVector OrientationCandidates[] =
		{
			Orientation.GetAxisY(),
			Orientation.GetAxisZ(),
			Orientation.GetAxisX()
		};

		FVector UAxis = FVector::ZeroVector;
		for (const FVector& Candidate : OrientationCandidates)
		{
			UAxis = Candidate - Direction * FVector::DotProduct(Candidate, Direction);
			if (UAxis.Normalize())
			{
				break;
			}
		}

		if (UAxis.IsNearlyZero())
		{
			return false;
		}

		FVector VAxis = FVector::CrossProduct(Direction, UAxis);
		if (!VAxis.Normalize())
		{
			return false;
		}

		// Remove accumulated numerical error while keeping the Forward/Right/Up handedness above.
		UAxis = FVector::CrossProduct(VAxis, Direction).GetSafeNormal();
		if (UAxis.IsNearlyZero())
		{
			return false;
		}

		OutProjector.Origin = Origin;
		OutProjector.Direction = Direction;
		OutProjector.UAxis = UAxis;
		OutProjector.VAxis = VAxis;
		return true;
	}

	FVector2f ProjectPosition(const FVector& WorldPosition,
		const FPCGDynamicMeshUVProjector& Projector,
		const FVector2D& UVScale, const FVector2D& UVOffset)
	{
		const FVector Delta = WorldPosition - Projector.Origin;
		return FVector2f(
			static_cast<float>(FVector::DotProduct(Delta, Projector.UAxis) * UVScale.X + UVOffset.X),
			static_cast<float>(FVector::DotProduct(Delta, Projector.VAxis) * UVScale.Y + UVOffset.Y));
	}

	uint64 MakeProjectedElementKey(int32 VertexID, int32 ProjectorIndex)
	{
		return (static_cast<uint64>(static_cast<uint32>(VertexID)) << 32)
			| static_cast<uint32>(ProjectorIndex);
	}

	struct FProjectionApplyResult
	{
		int32 DegenerateTriangleCount = 0;
		int32 OverlayFailureCount = 0;
	};

	FProjectionApplyResult ApplyProjection(
		UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
		TArrayView<const int32> TriangleIDs,
		TArrayView<const FPCGDynamicMeshUVProjector> Projectors,
		const FTransform& MeshToWorld,
		const FVector2D& UVScale,
		const FVector2D& UVOffset)
	{
		using namespace UE::Geometry;

		FProjectionApplyResult Result;
		check(!Projectors.IsEmpty());

		TMap<int32, FVector> WorldPositions;
		WorldPositions.Reserve(TriangleIDs.Num() * 2);
		const auto GetWorldPosition = [&Mesh, &MeshToWorld, &WorldPositions](int32 VertexID)
		{
			if (const FVector* Existing = WorldPositions.Find(VertexID))
			{
				return *Existing;
			}
			const FVector WorldPosition = MeshToWorld.TransformPosition(FVector(Mesh.GetVertex(VertexID)));
			WorldPositions.Add(VertexID, WorldPosition);
			return WorldPosition;
		};

		TMap<int32, int32> TriangleProjectors;
		TriangleProjectors.Reserve(TriangleIDs.Num());
		if (Projectors.Num() == 1)
		{
			for (const int32 TriangleID : TriangleIDs)
			{
				TriangleProjectors.Add(TriangleID, 0);
			}
		}
		else
		{
			for (const int32 TriangleID : TriangleIDs)
			{
				const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
				const FVector A = GetWorldPosition(Triangle.A);
				const FVector B = GetWorldPosition(Triangle.B);
				const FVector C = GetWorldPosition(Triangle.C);
				FVector TriangleNormal = FVector::CrossProduct(B - A, C - A);
				if (TriangleNormal.ContainsNaN() || !TriangleNormal.Normalize())
				{
					++Result.DegenerateTriangleCount;
					continue;
				}

				int32 BestProjectorIndex = 0;
				double BestDot = FVector::DotProduct(TriangleNormal, Projectors[0].Direction);
				for (int32 ProjectorIndex = 1; ProjectorIndex < Projectors.Num(); ++ProjectorIndex)
				{
					const double Dot = FVector::DotProduct(
						TriangleNormal, Projectors[ProjectorIndex].Direction);
					if (Dot > BestDot)
					{
						BestDot = Dot;
						BestProjectorIndex = ProjectorIndex;
					}
				}
				TriangleProjectors.Add(TriangleID, BestProjectorIndex);
			}
		}

		// Only elements created during this operation are reused. This shares (VertexID, ProjectorIndex) inside the
		// processed region, splits different projector assignments, and isolates a partial selection from pre-existing
		// elements that may still be referenced by unselected triangles.
		TMap<uint64, int32> ProjectedElements;
		ProjectedElements.Reserve(TriangleProjectors.Num() * 2);
		for (const int32 TriangleID : TriangleIDs)
		{
			const int32* ProjectorIndexPtr = TriangleProjectors.Find(TriangleID);
			if (!ProjectorIndexPtr)
			{
				continue;
			}

			const int32 ProjectorIndex = *ProjectorIndexPtr;
			const FPCGDynamicMeshUVProjector& Projector = Projectors[ProjectorIndex];
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			FIndex3i UVTriangle;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 VertexID = Triangle[Corner];
				const uint64 Key = MakeProjectedElementKey(VertexID, ProjectorIndex);
				if (const int32* ExistingElementID = ProjectedElements.Find(Key))
				{
					UVTriangle[Corner] = *ExistingElementID;
				}
				else
				{
					const FVector2f UV = ProjectPosition(
						GetWorldPosition(VertexID), Projector, UVScale, UVOffset);
					const int32 ElementID = UVOverlay.AppendElement(UV);
					ProjectedElements.Add(Key, ElementID);
					UVTriangle[Corner] = ElementID;
				}
			}

			if (UVOverlay.SetTriangle(TriangleID, UVTriangle) != EMeshResult::Ok)
			{
				++Result.OverlayFailureCount;
			}
		}

		return Result;
	}
}

UPCGDynamicMeshUVProjectSettings::UPCGDynamicMeshUVProjectSettings()
{
	// Use the supported PCG selector parser so this appears as a real point-property selector in Details.
	ProjectDirection.Update(TEXT("$Rotation.Forward"));
}

#if WITH_EDITOR
FText UPCGDynamicMeshUVProjectSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "UV Project");
}

FText UPCGDynamicMeshUVProjectSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Projects planar UVs onto a Dynamic Mesh or Dynamic Mesh Selection from one Point Data containing one or more projector points.");
}
#endif

TArray<FPCGPinProperties> UPCGDynamicMeshUVProjectSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(
		PCGDynamicMeshUVProjectConstants::ProjectorsInputPin,
		EPCGDataType::Point,
		/*bInAllowMultipleConnections=*/false,
		/*bAllowMultipleData=*/false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGDynamicMeshUVProjectSettings::CreateElement() const
{
	return MakeShared<FPCGDynamicMeshUVProjectElement>();
}

FPCGContext* FPCGDynamicMeshUVProjectElement::CreateContext()
{
	return new FPCGDynamicMeshUVProjectContext();
}

bool FPCGDynamicMeshUVProjectElement::ExecuteInternal(FPCGContext* Context) const
{
	FPCGDynamicMeshUVProjectContext* ProjectContext =
		static_cast<FPCGDynamicMeshUVProjectContext*>(Context);
	check(ProjectContext);

	if (!ProjectContext->bProjectorsResolved)
	{
		ResolveProjectors(ProjectContext);
	}

	return FPCGUtilsDynMeshProcessBaseElement::ExecuteInternal(Context);
}

void FPCGDynamicMeshUVProjectElement::ResolveProjectors(
	FPCGDynamicMeshUVProjectContext* Context) const
{
	check(Context);
	Context->bProjectorsResolved = true;
	Context->Projectors.Reset();

	const UPCGDynamicMeshUVProjectSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProjectSettings>();
	check(Settings);

	const TArray<FPCGTaggedData>& ProjectorInputs = Context->InputData.GetInputsByPin(
		PCGDynamicMeshUVProjectConstants::ProjectorsInputPin);
	if (ProjectorInputs.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneProjectorData", "UV Project requires exactly one Point Data object on the Projectors pin."),
			Context);
		return;
	}

	const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(ProjectorInputs[0].Data);
	if (!PointData)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidProjectorData", "UV Project received invalid Point Data on the Projectors pin."),
			Context);
		return;
	}

	TArray<FVector> Directions;
	if (!PCGAttributeAccessorHelpers::ExtractAllValues<FVector>(
		PointData, Settings->ProjectDirection, Directions, Context))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("InvalidDirectionSelector", "UV Project could not resolve Project Direction selector '{0}' as a Vector."),
			FText::FromString(Settings->ProjectDirection.ToString())), Context);
		return;
	}

	const int32 PointCount = PointData->GetNumPoints();
	if (Directions.Num() != PointCount)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("DirectionCountMismatch", "UV Project resolved a different number of direction values than projector points."),
			Context);
		return;
	}

	const auto Transforms = PointData->GetConstTransformValueRange();
	Context->Projectors.Reserve(PointCount);
	int32 InvalidProjectorCount = 0;
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		FPCGDynamicMeshUVProjector& Projector = Context->Projectors.Emplace_GetRef();
		if (!BuildProjector(Transforms[PointIndex], Directions[PointIndex], Projector))
		{
			Context->Projectors.Pop(EAllowShrinking::No);
			++InvalidProjectorCount;
		}
	}

	if (InvalidProjectorCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidProjectors", "UV Project skipped {0} projector point(s) with an invalid origin, rotation, or direction."),
			FText::AsNumber(InvalidProjectorCount)), Context);
	}

	if (Context->Projectors.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("NoValidProjectors", "UV Project found no valid projector points; target meshes will be passed through unchanged."),
			Context);
	}
}

bool FPCGDynamicMeshUVProjectElement::ShouldProcessUVs(
	UPCGDynamicMeshData* MeshData,
	const UPCGDynamicMeshSelectionData* SelectionData,
	TArrayView<const int32> TriangleIDs,
	FPCGContext* Context) const
{
	const FPCGDynamicMeshUVProjectContext* ProjectContext =
		static_cast<const FPCGDynamicMeshUVProjectContext*>(Context);
	const UPCGDynamicMeshUVProjectSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProjectSettings>();
	check(ProjectContext && Settings);

	if (Settings->UVScale.ContainsNaN() || Settings->UVOffset.ContainsNaN())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidUVTransform", "UV Project requires finite UV Scale and UV Offset values; the mesh was left unchanged."),
			Context);
		return false;
	}

	return !ProjectContext->Projectors.IsEmpty();
}

bool FPCGDynamicMeshUVProjectElement::ProcessUVs(
	UPCGDynamicMeshData* MeshData,
	UE::Geometry::FDynamicMesh3& Mesh,
	UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
	TArrayView<const int32> TriangleIDs,
	FPCGContext* Context) const
{
	using namespace UE::Geometry;

	const FPCGDynamicMeshUVProjectContext* ProjectContext =
		static_cast<const FPCGDynamicMeshUVProjectContext*>(Context);
	const UPCGDynamicMeshUVProjectSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProjectSettings>();
	check(ProjectContext && Settings && !ProjectContext->Projectors.IsEmpty());

	// PCG point positions/directions are world-space. Dynamic Mesh data has no embedded data transform and follows
	// this module's target-actor-local convention, so transform raw vertices once into the common world space.
	const FTransform MeshToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, MeshData, /*bConvertToLocalSpace=*/true);
	const FProjectionApplyResult Result = ApplyProjection(
		Mesh, UVOverlay, TriangleIDs, ProjectContext->Projectors,
		MeshToWorld, Settings->UVScale, Settings->UVOffset);

	if (Result.DegenerateTriangleCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("DegenerateTriangles", "UV Project left {0} degenerate triangle(s) unchanged because a facing normal could not be computed."),
			FText::AsNumber(Result.DegenerateTriangleCount)), Context);
	}
	if (Result.OverlayFailureCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("OverlayFailures", "UV Project could not assign UV overlay elements to {0} triangle(s)."),
			FText::AsNumber(Result.OverlayFailureCount)), Context);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool NearlyEqual(const FVector2f& A, const FVector2f& B, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return FMath::IsNearlyEqual(A.X, B.X, Tolerance)
			&& FMath::IsNearlyEqual(A.Y, B.Y, Tolerance);
	}

	void InitializeTestOverlay(UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& Overlay, const FVector2f& InitialValue)
	{
		using namespace UE::Geometry;
		TArray<int32> VertexElements;
		VertexElements.Init(INDEX_NONE, Mesh.MaxVertexID());
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			VertexElements[VertexID] = Overlay.AppendElement(InitialValue + FVector2f(VertexID, VertexID));
		}
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			Overlay.SetTriangle(TriangleID, FIndex3i(
				VertexElements[Triangle.A], VertexElements[Triangle.B], VertexElements[Triangle.C]));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynamicMeshUVProjectSettingsAndFrameTest,
	"PCGUtils.DynMesh.UVProject.SettingsAndFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynamicMeshUVProjectSettingsAndFrameTest::RunTest(const FString& Parameters)
{
	const UPCGDynamicMeshUVProjectSettings* Settings = NewObject<UPCGDynamicMeshUVProjectSettings>();
	TestEqual(TEXT("Default Project Direction is visible as $Rotation.Forward"),
		Settings->ProjectDirection.ToString(), FString(TEXT("$Rotation.Forward")));
	TestTrue(TEXT("Default UV Scale maps centimeters to meters"),
		Settings->UVScale.Equals(FVector2D(0.01, 0.01)));

	FPCGAttributePropertyInputSelector AlternativeDirection;
	TestTrue(TEXT("Alternative $Rotation.Up selector parses through the PCG selector API"),
		AlternativeDirection.Update(TEXT("$Rotation.Up")));
	TestEqual(TEXT("Alternative selector retains its intended display value"),
		AlternativeDirection.ToString(), FString(TEXT("$Rotation.Up")));

	const TArray<FPCGPinProperties> Pins = Settings->AllInputPinProperties();
	const FPCGPinProperties* ProjectorsPin = Pins.FindByPredicate([](const FPCGPinProperties& Pin)
	{
		return Pin.Label == PCGDynamicMeshUVProjectConstants::ProjectorsInputPin;
	});
	TestNotNull(TEXT("Projectors pin exists"), ProjectorsPin);
	if (ProjectorsPin)
	{
		TestTrue(TEXT("Projectors pin accepts Point Data"), ProjectorsPin->AllowedTypes == EPCGDataType::Point);
		TestFalse(TEXT("Projectors pin rejects multiple data objects"), ProjectorsPin->bAllowMultipleData);
		TestFalse(TEXT("Projectors pin rejects multiple connections"), ProjectorsPin->AllowsMultipleConnections());
		TestTrue(TEXT("Projectors pin is required"), ProjectorsPin->IsRequiredPin());
	}

	FPCGDynamicMeshUVProjector IdentityProjector;
	TestTrue(TEXT("Identity Forward projector frame is valid"),
		BuildProjector(FTransform::Identity, FVector::ForwardVector, IdentityProjector));
	TestTrue(TEXT("Forward projector U is point Right"),
		IdentityProjector.UAxis.Equals(FVector::RightVector));
	TestTrue(TEXT("Forward projector V is point Up"),
		IdentityProjector.VAxis.Equals(FVector::UpVector));

	const FQuat Roll(FVector::ForwardVector, UE_HALF_PI);
	FPCGDynamicMeshUVProjector RolledProjector;
	TestTrue(TEXT("Rolled projector frame is valid"),
		BuildProjector(FTransform(Roll), FVector::ForwardVector, RolledProjector));
	TestTrue(TEXT("Roll rotates the U axis"), RolledProjector.UAxis.Equals(FVector::UpVector));
	TestTrue(TEXT("Roll rotates the V axis"), RolledProjector.VAxis.Equals(-FVector::RightVector));

	FPCGDynamicMeshUVProjector ScaledProjector;
	const FTransform ScaledPoint(FQuat::Identity, FVector(10, 20, 30), FVector(7, 0.25, 12));
	TestTrue(TEXT("Scaled projector point still builds"),
		BuildProjector(ScaledPoint, FVector::ForwardVector, ScaledProjector));
	TestTrue(TEXT("Projector scale does not alter its frame"),
		ScaledProjector.UAxis.Equals(FVector::RightVector)
		&& ScaledProjector.VAxis.Equals(FVector::UpVector));
	const FVector2f ScaledUV = ProjectPosition(
		FVector(10, 120, 230), ScaledProjector, FVector2D(0.01, 0.01), FVector2D::ZeroVector);
	TestTrue(TEXT("Projector scale is ignored by projected UVs"), NearlyEqual(ScaledUV, FVector2f(1, 2)));

	FPCGDynamicMeshUVProjector FallbackProjector;
	TestTrue(TEXT("Direction parallel to preferred Right uses a stable fallback"),
		BuildProjector(FTransform::Identity, FVector::RightVector, FallbackProjector));
	TestTrue(TEXT("Fallback frame remains orthonormal"),
		FMath::IsNearlyZero(FVector::DotProduct(FallbackProjector.Direction, FallbackProjector.UAxis))
		&& FMath::IsNearlyZero(FVector::DotProduct(FallbackProjector.Direction, FallbackProjector.VAxis))
		&& FMath::IsNearlyZero(FVector::DotProduct(FallbackProjector.UAxis, FallbackProjector.VAxis)));
	TestFalse(TEXT("Zero direction is rejected"),
		BuildProjector(FTransform::Identity, FVector::ZeroVector, FallbackProjector));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynamicMeshUVProjectOverlayTest,
	"PCGUtils.DynMesh.UVProject.ProjectionAndOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynamicMeshUVProjectOverlayTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;

	FDynamicMesh3 Mesh;
	const int32 V0 = Mesh.AppendVertex(FVector3d(0, 0, 0));
	const int32 V1 = Mesh.AppendVertex(FVector3d(100, 0, 0));
	const int32 V2 = Mesh.AppendVertex(FVector3d(0, 100, 0));
	const int32 V3 = Mesh.AppendVertex(FVector3d(100, 100, 0));
	const int32 T0 = Mesh.AppendTriangle(V0, V1, V2);
	const int32 T1 = Mesh.AppendTriangle(V1, V3, V2);
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(2);
	FDynamicMeshUVOverlay* Layer0 = Mesh.Attributes()->GetUVLayer(0);
	FDynamicMeshUVOverlay* Layer1 = Mesh.Attributes()->GetUVLayer(1);
	InitializeTestOverlay(Mesh, *Layer0, FVector2f(10, 10));
	InitializeTestOverlay(Mesh, *Layer1, FVector2f(20, 20));

	const FIndex3i OriginalUnselected = Layer0->GetTriangle(T1);
	FVector2f OriginalUnselectedA, OriginalUnselectedB, OriginalUnselectedC;
	Layer0->GetTriElements(T1, OriginalUnselectedA, OriginalUnselectedB, OriginalUnselectedC);
	const FIndex3i OriginalOtherLayer = Layer1->GetTriangle(T0);

	FPCGDynamicMeshUVProjector ProjectorZ;
	ProjectorZ.Direction = FVector::UpVector;
	ProjectorZ.UAxis = FVector::ForwardVector;
	ProjectorZ.VAxis = FVector::RightVector;
	const TArray<FPCGDynamicMeshUVProjector> OneProjector{ProjectorZ};
	const TArray<int32> SelectedTriangles{T0};
	const FProjectionApplyResult SelectionResult = ApplyProjection(
		Mesh, *Layer0, SelectedTriangles, OneProjector, FTransform::Identity,
		FVector2D(0.01, 0.01), FVector2D::ZeroVector);
	TestEqual(TEXT("Selected projection has no overlay failures"), SelectionResult.OverlayFailureCount, 0);
	TestEqual(TEXT("Unselected triangle keeps its overlay topology"), Layer0->GetTriangle(T1), OriginalUnselected);
	FVector2f UnselectedA, UnselectedB, UnselectedC;
	Layer0->GetTriElements(T1, UnselectedA, UnselectedB, UnselectedC);
	TestTrue(TEXT("Unselected triangle keeps its UV values"),
		NearlyEqual(UnselectedA, OriginalUnselectedA)
		&& NearlyEqual(UnselectedB, OriginalUnselectedB)
		&& NearlyEqual(UnselectedC, OriginalUnselectedC));
	TestEqual(TEXT("Non-target UV layer remains untouched"), Layer1->GetTriangle(T0), OriginalOtherLayer);

	const FIndex3i SelectedUVTriangle = Layer0->GetTriangle(T0);
	const FIndex3i UnselectedUVTriangle = Layer0->GetTriangle(T1);
	TestNotEqual(TEXT("Selection boundary splits shared V1 UV element"), SelectedUVTriangle.B, UnselectedUVTriangle.A);
	TestNotEqual(TEXT("Selection boundary splits shared V2 UV element"), SelectedUVTriangle.C, UnselectedUVTriangle.C);
	FVector2f SelectedA, SelectedB, SelectedC;
	Layer0->GetTriElements(T0, SelectedA, SelectedB, SelectedC);
	TestTrue(TEXT("Single projector produces predictable planar UVs"),
		NearlyEqual(SelectedA, FVector2f(0, 0))
		&& NearlyEqual(SelectedB, FVector2f(1, 0))
		&& NearlyEqual(SelectedC, FVector2f(0, 1)));

	const TArray<int32> AllTriangles{T0, T1};
	ApplyProjection(Mesh, *Layer0, AllTriangles, OneProjector, FTransform::Identity,
		FVector2D(0.01, 0.01), FVector2D::ZeroVector);
	const FIndex3i FullT0 = Layer0->GetTriangle(T0);
	const FIndex3i FullT1 = Layer0->GetTriangle(T1);
	TestEqual(TEXT("Same projector shares V1 overlay element"), FullT0.B, FullT1.A);
	TestEqual(TEXT("Same projector shares V2 overlay element"), FullT0.C, FullT1.C);

	FDynamicMesh3 FacingMesh;
	const int32 XA = FacingMesh.AppendVertex(FVector3d(0, 0, 0));
	const int32 XB = FacingMesh.AppendVertex(FVector3d(0, 100, 0));
	const int32 XC = FacingMesh.AppendVertex(FVector3d(0, 0, 100));
	const int32 ZA = FacingMesh.AppendVertex(FVector3d(10, 0, 0));
	const int32 ZB = FacingMesh.AppendVertex(FVector3d(110, 0, 0));
	const int32 ZC = FacingMesh.AppendVertex(FVector3d(10, 100, 0));
	const int32 TX = FacingMesh.AppendTriangle(XA, XB, XC);
	const int32 TZ = FacingMesh.AppendTriangle(ZA, ZB, ZC);
	FacingMesh.EnableAttributes();
	FDynamicMeshUVOverlay* FacingUVs = FacingMesh.Attributes()->PrimaryUV();

	FPCGDynamicMeshUVProjector ProjectorX;
	ProjectorX.Direction = FVector::ForwardVector;
	ProjectorX.UAxis = FVector::RightVector;
	ProjectorX.VAxis = FVector::UpVector;
	ProjectorZ.Origin = FVector(1000, 1000, 0);
	const TArray<FPCGDynamicMeshUVProjector> Projectors{ProjectorX, ProjectorZ};
	const TArray<int32> FacingTriangles{TX, TZ};
	ApplyProjection(FacingMesh, *FacingUVs, FacingTriangles, Projectors,
		FTransform::Identity, FVector2D::One(), FVector2D::ZeroVector);
	FVector2f XAUV, XBUV, XCUV;
	FacingUVs->GetTriElements(TX, XAUV, XBUV, XCUV);
	FVector2f ZAUV, ZBUV, ZCUV;
	FacingUVs->GetTriElements(TZ, ZAUV, ZBUV, ZCUV);
	TestTrue(TEXT("+X triangle chooses +X projector"), NearlyEqual(XBUV, FVector2f(100, 0)));
	TestTrue(TEXT("+Z triangle chooses +Z projector"), NearlyEqual(ZAUV, FVector2f(-990, -1000)));

	FDynamicMesh3 TransformMesh;
	const int32 TA = TransformMesh.AppendVertex(FVector3d(10, 0, 0));
	const int32 TB = TransformMesh.AppendVertex(FVector3d(20, 0, 0));
	const int32 TC = TransformMesh.AppendVertex(FVector3d(10, 10, 0));
	const int32 TT = TransformMesh.AppendTriangle(TA, TB, TC);
	TransformMesh.EnableAttributes();
	FDynamicMeshUVOverlay* TransformUVs = TransformMesh.Attributes()->PrimaryUV();
	const TArray<int32> TransformTriangles{TT};
	const FTransform MeshToWorld(FQuat::Identity, FVector(5, 7, 0), FVector(2, 3, 1));
	ProjectorZ.Origin = FVector::ZeroVector;
	const TArray<FPCGDynamicMeshUVProjector> WorldProjector{ProjectorZ};
	ApplyProjection(TransformMesh, *TransformUVs, TransformTriangles, WorldProjector,
		MeshToWorld, FVector2D::One(), FVector2D::ZeroVector);
	FVector2f TAUV, TBUV, TCUV;
	TransformUVs->GetTriElements(TT, TAUV, TBUV, TCUV);
	TestTrue(TEXT("Mesh-to-world translation and non-uniform scale affect projection exactly once"),
		NearlyEqual(TAUV, FVector2f(25, 7)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
