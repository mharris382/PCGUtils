// Copyright Max Harris

#include "Elements/UV/PCGDynMeshProjectUVsFromBounds.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGSpatialData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshProjectUVsFromBounds"

namespace PCGDynMeshProjectUVsFromBounds
{
	/** The (U, V) world axes for a projection along Axis. */
	void PlaneAxes(int32 Axis, int32& OutU, int32& OutV)
	{
		OutU = Axis == 0 ? 1 : 0;
		OutV = Axis == 2 ? 1 : 2;
	}

	int32 AxisIndex(EPCGUtilsDynMeshProjectionAxis Axis)
	{
		return Axis == EPCGUtilsDynMeshProjectionAxis::X ? 0 : Axis == EPCGUtilsDynMeshProjectionAxis::Y ? 1 : 2;
	}

	FTransform SpaceTransform(FPCGContext* Context, const UPCGDynamicMeshData* MeshData, EPCGUtilsDynMeshProjectionSpace Space)
	{
		return Space == EPCGUtilsDynMeshProjectionSpace::World
			? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, MeshData, /*bConvertToLocalSpace=*/true)
			: FTransform::Identity;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshProjectUVsFromBoundsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Project UVs From Bounds");
}

FText UPCGDynMeshProjectUVsFromBoundsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Projects UVs so a box maps exactly onto UV 0 to 1, Planar along one axis or Box per facing axis. The box comes "
		"from each input, all inputs, the Bounds pin or Custom Bounds, and is written to @Data so UVs can be turned back "
		"into positions (render targets, baked distance fields, texture-to-world lookups).");
}

FString UPCGDynMeshProjectUVsFromBoundsSettings::GetAdditionalTitleInformation() const
{
	const FString Bounds = StaticEnum<EPCGUtilsDynMeshProjectionBounds>()->GetDisplayNameTextByValue(static_cast<int64>(BoundsSource)).ToString();
	if (Projection == EPCGUtilsDynMeshBoundsProjection::Box)
	{
		return FString::Printf(TEXT("Box, %s"), *Bounds);
	}
	return FString::Printf(TEXT("Planar %s, %s"),
		*StaticEnum<EPCGUtilsDynMeshProjectionAxis>()->GetDisplayNameTextByValue(static_cast<int64>(Axis)).ToString(), *Bounds);
}
#endif

FText UPCGDynMeshProjectUVsFromBoundsSettings::GetBuilderInputRejectionReason() const
{
	return LOCTEXT("BuilderRejected", "Bounds Source All Inputs needs every mesh at once, and a Builder has no geometry until it is realized. Realize the Builder first, or use another Bounds Source.");
}

TArray<FPCGPinProperties> UPCGDynMeshProjectUVsFromBoundsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace(PCGDynMeshProjectUVsFromBoundsConstants::BoundsPin, EPCGDataType::Spatial, /*bAllowMultipleConnections=*/true,
		/*bAllowMultipleData=*/true, LOCTEXT("BoundsTooltip",
			"Used with Bounds Source Bounds Pin: the box is the bounds of everything here (points, volumes, any spatial data)."));
	return Pins;
}

FPCGElementPtr UPCGDynMeshProjectUVsFromBoundsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshProjectUVsFromBoundsElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshProjectUVsFromBoundsSettings::CreateProcessOperation(FPCGContext* InContext) const
{
	using namespace PCGDynMeshProjectUVsFromBounds;
	TSharedPtr<FPCGUtilsDynMeshProjectUVsFromBoundsOperation> Operation = MakeShared<FPCGUtilsDynMeshProjectUVsFromBoundsOperation>();
	Operation->UVLayer = UVLayer;
	Operation->Projection = Projection;
	Operation->Axis = Axis;
	Operation->Space = Space;
	Operation->Padding = Padding;
	Operation->bSquare = bSquare;
	Operation->bFlipV = bFlipV;
	Operation->bWriteBoundsAttributes = bWriteBoundsAttributes;
	Operation->ProjectionMinAttribute = ProjectionMinAttribute;
	Operation->ProjectionMaxAttribute = ProjectionMaxAttribute;

	switch (BoundsSource)
	{
	case EPCGUtilsDynMeshProjectionBounds::Custom:
		Operation->SharedBounds = CustomBounds;
		break;
	case EPCGUtilsDynMeshProjectionBounds::BoundsPin:
	{
		FBox Box(ForceInit);
		for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGDynMeshProjectUVsFromBoundsConstants::BoundsPin))
		{
			if (const UPCGSpatialData* Spatial = Cast<const UPCGSpatialData>(Input.Data))
			{
				Box += Spatial->GetBounds();
			}
		}
		Operation->SharedBounds = Box;
		break;
	}
	case EPCGUtilsDynMeshProjectionBounds::AllInputs:
	{
		FBox Box(ForceInit);
		for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(GetMainInputPinLabel()))
		{
			const UPCGDynamicMeshSelectionData* Selection = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
			const UPCGDynamicMeshData* MeshData = Selection ? Selection->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
			const UDynamicMesh* Object = MeshData ? MeshData->GetDynamicMesh() : nullptr;
			if (!Object)
			{
				continue;
			}
			const FTransform ToSpace = SpaceTransform(InContext, MeshData, Space);
			for (const FVector3d& Vertex : Object->GetMeshRef().VerticesItr())
			{
				Box += ToSpace.TransformPosition(Vertex);
			}
		}
		Operation->SharedBounds = Box;
		break;
	}
	default:
		break;
	}
	return Operation;
}

FBox FPCGUtilsDynMeshProjectUVsFromBoundsOperation::FinalBox(FBox Box, double Padding, bool bSquare,
	EPCGUtilsDynMeshBoundsProjection Projection, EPCGUtilsDynMeshProjectionAxis Axis)
{
	Box = Box.ExpandBy(FMath::Max(Padding, 0.0));
	if (bSquare)
	{
		// Square the axes UV spans: the two projected axes for Planar, all three for Box.
		TArray<int32, TInlineAllocator<3>> Axes;
		if (Projection == EPCGUtilsDynMeshBoundsProjection::Planar)
		{
			int32 U = 0, V = 0;
			PCGDynMeshProjectUVsFromBounds::PlaneAxes(PCGDynMeshProjectUVsFromBounds::AxisIndex(Axis), U, V);
			Axes = {U, V};
		}
		else
		{
			Axes = {0, 1, 2};
		}
		double Longest = 0.0;
		for (const int32 A : Axes)
		{
			Longest = FMath::Max(Longest, Box.Max[A] - Box.Min[A]);
		}
		for (const int32 A : Axes)
		{
			const double Center = 0.5 * (Box.Min[A] + Box.Max[A]);
			Box.Min[A] = Center - 0.5 * Longest;
			Box.Max[A] = Center + 0.5 * Longest;
		}
	}
	return Box;
}

bool FPCGUtilsDynMeshProjectUVsFromBoundsOperation::ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const
{
	using namespace PCGDynMeshProjectUVsFromBounds;
	const FTransform ToSpace = SpaceTransform(Invocation.Context, Invocation.MeshData, Space);

	// Positions of the region's vertices in the projection space, and the region's own box.
	TMap<int32, FVector> Positions;
	FBox RegionBox(ForceInit);
	for (const int32 TriangleID : TriangleIDs)
	{
		const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (!Positions.Contains(Triangle[Corner]))
			{
				const FVector P = ToSpace.TransformPosition(Mesh.GetVertex(Triangle[Corner]));
				Positions.Add(Triangle[Corner], P);
				RegionBox += P;
			}
		}
	}

	const FBox Source = SharedBounds.IsSet() ? SharedBounds.GetValue() : RegionBox;
	if (!Source.IsValid)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoBounds",
			"DynMesh | Project UVs From Bounds (input {0}): the box is empty (Bounds Source Bounds Pin with nothing on the pin?); the mesh's UVs were left unchanged."),
			FText::AsNumber(Invocation.InputIndex)), Invocation.Context);
		return true;
	}
	const FBox Box = FinalBox(Source, Padding, bSquare, Projection, Axis);

	auto Map = [&Box, this](const FVector& P, int32 U, int32 V)
	{
		const double SizeU = Box.Max[U] - Box.Min[U];
		const double SizeV = Box.Max[V] - Box.Min[V];
		const double UValue = SizeU > UE_DOUBLE_SMALL_NUMBER ? (P[U] - Box.Min[U]) / SizeU : 0.0;
		double VValue = SizeV > UE_DOUBLE_SMALL_NUMBER ? (P[V] - Box.Min[V]) / SizeV : 0.0;
		if (bFlipV)
		{
			VValue = 1.0 - VValue;
		}
		return FVector2f(static_cast<float>(UValue), static_cast<float>(VValue));
	};

	// Build the region's new UVs directly: a corner shares its UV with every other corner on the same vertex
	// projected along the same axis.
	PCGUtilsDynMeshUV::FRegionUVs Region;
	Region.bWholeMesh = TriangleIDs.Num() == Mesh.TriangleCount();
	TMap<uint64, int32> UVOfKey;
	const int32 PlanarAxis = AxisIndex(Axis);
	for (const int32 TriangleID : TriangleIDs)
	{
		const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		int32 ProjectAxis = PlanarAxis;
		if (Projection == EPCGUtilsDynMeshBoundsProjection::Box)
		{
			const FVector A = Positions[Triangle.A];
			const FVector Normal = FVector::CrossProduct(Positions[Triangle.B] - A, Positions[Triangle.C] - A).GetAbs();
			ProjectAxis = Normal.X >= Normal.Y && Normal.X >= Normal.Z ? 0 : Normal.Y >= Normal.Z ? 1 : 2;
		}
		int32 U = 0, V = 0;
		PlaneAxes(ProjectAxis, U, V);
		UE::Geometry::FIndex3i Local;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Triangle[Corner])) << 2) | static_cast<uint64>(ProjectAxis);
			if (const int32* Existing = UVOfKey.Find(Key))
			{
				Local[Corner] = *Existing;
			}
			else
			{
				Local[Corner] = Region.UVs.Add(Map(Positions[Triangle[Corner]], U, V));
				UVOfKey.Add(Key, Local[Corner]);
			}
		}
		Region.Triangles.Add(TriangleID);
		Region.Corners.Add(Local);
	}
	PCGUtilsDynMeshUV::WriteBack(UVOverlay, Region);

	if (bWriteBoundsAttributes)
	{
		if (ProjectionMinAttribute.IsNone() || ProjectionMaxAttribute.IsNone())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoAttributeName",
				"DynMesh | Project UVs From Bounds (input {0}): {1} is empty while Write Bounds Attributes is on; the box was not written."),
				FText::AsNumber(Invocation.InputIndex),
				ProjectionMinAttribute.IsNone() ? LOCTEXT("MinLabel", "Projection Min Attribute") : LOCTEXT("MaxLabel", "Projection Max Attribute")),
				Invocation.Context);
		}
		else if (ProjectionMinAttribute == ProjectionMaxAttribute)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("SameAttributeName",
				"DynMesh | Project UVs From Bounds (input {0}): Projection Min Attribute and Projection Max Attribute are both '{1}'; the box was not written."),
				FText::AsNumber(Invocation.InputIndex), FText::FromName(ProjectionMinAttribute)), Invocation.Context);
		}
		else if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Invocation.MeshData))
		{
			PCGUtilsDataDomain::WriteValue<FVector>(*Domain, ProjectionMinAttribute, Box.Min);
			PCGUtilsDataDomain::WriteValue<FVector>(*Domain, ProjectionMaxAttribute, Box.Max);
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
