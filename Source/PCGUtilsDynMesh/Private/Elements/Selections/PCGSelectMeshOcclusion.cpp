// Copyright Max Harris

#include "Elements/Selections/PCGSelectMeshOcclusion.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Geometry/PCGUtilsDynMeshOcclusion.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectMeshOcclusion"

namespace PCGSelectMeshOcclusionImpl
{
	class FSelectMeshOcclusionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectMeshOcclusionOperation(const UPCGSelectMeshOcclusionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}
			if (!FMath::IsFinite(Factory->MaximumDistance) || Factory->MaximumDistance < 0.0)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadDistance",
					"Select | By Mesh Occlusion: Maximum Distance is {0}; it must be 0 (unlimited) or a positive number."),
					FText::AsNumber(Factory->MaximumDistance)), Context);
				return false;
			}

			TestedToShared = Factory->bMeshesAreActorLocal
				? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, InSelectionContext.MeshData, true)
				: FTransform::Identity;

			FVector ResolvedDirection = Factory->TraceDirection;
			if (Factory->DirectionSpace == EPCGDynMeshSelfOcclusionDirectionSpace::MeshLocal)
			{
				ResolvedDirection = TestedToShared.TransformVectorNoScale(ResolvedDirection);
			}
			if (!ResolvedDirection.Normalize())
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("ZeroDirection",
					"Select | By Mesh Occlusion: Trace Direction is zero; it must point somewhere."), Context);
				return false;
			}
			Direction = FVector3d(ResolvedDirection);

			for (int32 Index = 0; Index < Factory->OccluderMeshes.Num(); ++Index)
			{
				const UPCGDynamicMeshData* OccluderData = Factory->OccluderMeshes[Index];
				const UE::Geometry::FDynamicMesh3* OccluderMesh = OccluderData && OccluderData->GetDynamicMesh()
					? OccluderData->GetDynamicMesh()->GetMeshPtr() : nullptr;
				const FTransform OccluderToShared = OccluderData && Factory->bMeshesAreActorLocal
					? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, OccluderData, true)
					: FTransform::Identity;
				if (!OccluderMesh || Occluders.Add(*OccluderMesh, OccluderToShared) == INDEX_NONE)
				{
					PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("EmptyOccluder",
						"Select | By Mesh Occlusion: Occluders input {0} has no triangles and blocks nothing; it is ignored."),
						FText::AsNumber(Index)), Context);
				}
			}
			if (Factory->bIncludeSelf)
			{
				SelfOccluder = Occluders.Add(InSelectionContext.Mesh, TestedToShared);
			}
			if (Occluders.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("NoOccluders",
					"Select | By Mesh Occlusion needs at least one DynMesh with triangles on its Occluders pin."), Context);
				return false;
			}

			bWantOccluded = Factory->Result == EPCGDynMeshSelfOcclusionResult::Occluded;
			if (Factory->ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex)
			{
				const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
				VertexNormals = MakeUnique<UE::Geometry::FMeshNormals>(&Mesh);
				if (Mesh.HasAttributes() && Mesh.Attributes()->PrimaryNormals())
				{
					VertexNormals->GetVertexNormalsFromOverlayNormals(
						UE::Geometry::FMeshNormals::ECombineSplitNormalsMethod::Average);
				}
				else
				{
					VertexNormals->ComputeVertexNormals();
				}
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			PCGUtilsDynMeshOcclusion::FSourceElement Source;
			Source.Occluder = SelfOccluder;
			FVector3d Position;
			FVector3d Normal;
			if (Factory->ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex)
			{
				if (!Mesh.IsVertex(ElementID)) { return false; }
				Position = Mesh.GetVertex(ElementID);
				const TArray<FVector3d>& Normals = VertexNormals->GetNormals();
				Normal = Normals.IsValidIndex(ElementID) ? Normals[ElementID] : FVector3d::UnitZ();
				Source.VertexID = ElementID;
			}
			else
			{
				if (!Mesh.IsTriangle(ElementID)) { return false; }
				Position = Mesh.GetTriCentroid(ElementID);
				Normal = Mesh.GetTriNormal(ElementID);
				Source.TriangleID = ElementID;
			}

			const FVector3d SharedPosition = TestedToShared.TransformPosition(Position);
			const FVector3d SharedNormal = PCGUtilsDynMeshOcclusion::TransformNormal(TestedToShared, Normal);
			const FVector3d Origin = SharedPosition + SharedNormal * Factory->NormalOffset + Direction * Factory->DirectionOffset;
			const bool bIgnore = SelfOccluder != INDEX_NONE && Factory->bIgnoreSourceTriangles;
			const bool bOccluded = Occluders.IsOccluded(Origin, Direction, Factory->MaximumDistance, bIgnore ? &Source : nullptr);
			return bOccluded == bWantOccluded;
		}

	private:
		TObjectPtr<const UPCGSelectMeshOcclusionFactoryData> Factory;
		PCGUtilsDynMeshOcclusion::FOccluderSet Occluders;
		TUniquePtr<UE::Geometry::FMeshNormals> VertexNormals;
		FTransform TestedToShared = FTransform::Identity;
		FVector3d Direction = FVector3d::UnitZ();
		int32 SelfOccluder = INDEX_NONE;
		bool bWantOccluded = true;
	};
}

#if WITH_EDITOR
FText UPCGSelectMeshOcclusionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | By Mesh Occlusion");
}

FText UPCGSelectMeshOcclusionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects DynMesh vertices or triangles whose ray is blocked (or clear) by the DynMeshes on the Occluders pin. "
		"Include Self also lets the tested mesh block its own rays. An incoming selection limits which candidates "
		"are traced.");
}

FString UPCGSelectMeshOcclusionSettings::GetAdditionalTitleInformation() const
{
	return Result == EPCGDynMeshSelfOcclusionResult::Occluded ? TEXT("Occluded") : TEXT("Unoccluded");
}
#endif

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGSelectMeshOcclusionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<PCGSelectMeshOcclusionImpl::FSelectMeshOcclusionOperation>(this);
}

void UPCGSelectMeshOcclusionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ElementTypeValue = static_cast<uint8>(ElementType);
		uint8 ResultValue = static_cast<uint8>(Result);
		uint8 DirectionSpaceValue = static_cast<uint8>(DirectionSpace);
		FVector DirectionValue = TraceDirection;
		double MaximumDistanceValue = MaximumDistance;
		double NormalOffsetValue = NormalOffset;
		double DirectionOffsetValue = DirectionOffset;
		bool bSelf = bIncludeSelf;
		bool bIgnore = bIgnoreSourceTriangles;
		bool bLocal = bMeshesAreActorLocal;
		Ar << ElementTypeValue << ResultValue << DirectionSpaceValue << DirectionValue;
		Ar << MaximumDistanceValue << NormalOffsetValue << DirectionOffsetValue << bSelf << bIgnore << bLocal;

		// The occluders decide the result as much as the settings do.
		int32 Count = OccluderMeshes.Num();
		Ar << Count;
		for (const UPCGDynamicMeshData* Occluder : OccluderMeshes)
		{
			uint32 OccluderCrc = Occluder ? Occluder->GetOrComputeCrc(bFullDataCrc).GetValue() : 0;
			Ar << OccluderCrc;
		}
	}
}

TArray<FPCGPinProperties> UPCGSelectMeshOcclusionSettings::SourceInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& Pin = Pins.Emplace_GetRef(PCGSelectMeshOcclusionConstants::OccludersPin,
		EPCGDataType::DynamicMesh, true, true);
	Pin.SetRequiredPin();
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("OccludersTip", "Required. The DynMeshes that block rays. Several act together.");
#endif
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGSelectMeshOcclusionSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGDynamicMeshData>> OccluderMeshes;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGSelectMeshOcclusionConstants::OccludersPin))
	{
		if (const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Input.Data))
		{
			OccluderMeshes.Add(MeshData);
		}
	}
	if (OccluderMeshes.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingOccluders",
			"Select | By Mesh Occlusion needs DynMesh data on its Occluders pin."), InContext);
		return nullptr;
	}

	UPCGSelectMeshOcclusionFactoryData* Factory = InFactory
		? Cast<UPCGSelectMeshOcclusionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSelectMeshOcclusionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}
	Factory->Priority = Priority;
	Factory->OccluderMeshes = MoveTemp(OccluderMeshes);
	Factory->ElementType = ElementType;
	Factory->Result = Result;
	Factory->TraceDirection = TraceDirection;
	Factory->DirectionSpace = DirectionSpace;
	Factory->MaximumDistance = MaximumDistance;
	Factory->NormalOffset = FMath::Max(0.0, NormalOffset);
	Factory->DirectionOffset = FMath::Max(0.0, DirectionOffset);
	Factory->bIncludeSelf = bIncludeSelf;
	Factory->bIgnoreSourceTriangles = bIgnoreSourceTriangles;
	Factory->bMeshesAreActorLocal = bMeshesAreActorLocal;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
