#include "Elements/Conversion/PCGDynMeshToPoints.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshToPointsElement"

using namespace UE::Geometry;

TArray<FPCGPinProperties> UPCGDynMeshToPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace_GetRef(PCGDynMeshToPointsConstants::InDynamicMeshLabel, EPCGDataType::DynamicMesh, true, false).SetRequiredPin();
	return Props;
}

TArray<FPCGPinProperties> UPCGDynMeshToPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, false);
	return Props;
}

FPCGElementPtr UPCGDynMeshToPointsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshToPointsElement>();
}

FPCGContext* FPCGDynMeshToPointsElement::CreateContext()
{
	return new FPCGDynMeshToPointsContext();
}

namespace
{
	template<typename OverlayType, typename ValueType>
	ValueType DynMeshToPointsGetFirstVertexOverlayElement(
		const FDynamicMesh3* Mesh,
		const OverlayType* Overlay,
		const int32 VID,
		const ValueType& DefaultValue,
		FIndex2i* OutTriangleCorner = nullptr)
	{
		if (!Mesh || !Overlay)
		{
			return DefaultValue;
		}

		ValueType Result = DefaultValue;
		bool bFound = false;

		Mesh->EnumerateVertexTriangles(VID, [&](int32 TID)
		{
			if (bFound || !Overlay->IsSetTriangle(TID))
			{
				return;
			}

			const FIndex3i TriVerts = Mesh->GetTriangle(TID);
			const FIndex3i TriElems = Overlay->GetTriangle(TID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TriVerts[Corner] == VID && Overlay->IsElement(TriElems[Corner]))
				{
					Result = Overlay->GetElement(TriElems[Corner]);
					if (OutTriangleCorner)
					{
						*OutTriangleCorner = FIndex2i(TID, Corner);
					}
					bFound = true;
					break;
				}
			}
		});

		return Result;
	}
}

bool FPCGDynMeshToPointsElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshToPointsElement::ExecuteInternal);

	check(InContext);
	const UPCGDynMeshToPointsSettings* Settings =
		InContext->GetInputSettings<UPCGDynMeshToPointsSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> MeshInputs =
		InContext->InputData.GetInputsByPin(PCGDynMeshToPointsConstants::InDynamicMeshLabel);

	if (MeshInputs.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("MissingInput", "DynMeshToPoints: missing Dynamic Mesh input."), InContext);
		return true;
	}

	for (const FPCGTaggedData& Input : MeshInputs)
	{
		const UPCGDynamicMeshData* InMeshData = Cast<const UPCGDynamicMeshData>(Input.Data);
		if (!InMeshData)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("BadInput", "DynMeshToPoints: input is not Dynamic Mesh data, skipping."), InContext);
			continue;
		}

		const FDynamicMesh3* Mesh = InMeshData->GetDynamicMesh()
			? InMeshData->GetDynamicMesh()->GetMeshPtr()
			: nullptr;

		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("NullMesh", "DynMeshToPoints: source mesh is null, skipping."), InContext);
			continue;
		}

		const FTransform MeshToOutput = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			InContext, InMeshData, Settings->bOutputToWorldSpace);

		const FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		const FDynamicMeshNormalOverlay* TangentOverlay = nullptr;
		const FDynamicMeshColorOverlay* ColorOverlay = nullptr;

		if (Mesh->HasAttributes())
		{
			const FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
			NormalOverlay = Attributes ? Attributes->PrimaryNormals() : nullptr;
			TangentOverlay = Attributes ? Attributes->PrimaryTangents() : nullptr;
			ColorOverlay = Attributes ? Attributes->PrimaryColors() : nullptr;
		}

		UPCGPointData* PointData = NewObject<UPCGPointData>();
		TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
		Points.Reserve(Mesh->VertexCount());
		UPCGPointArrayData* PointArrayData = NewObject<UPCGPointArrayData>();
		PointArrayData->SetNumPoints(Mesh->VertexCount());
		
		for (const int32 VID : Mesh->VertexIndicesItr())
		{
			const FVector3d Position = Mesh->GetVertex(VID);
			FIndex2i NormalCorner(INDEX_NONE, INDEX_NONE);
			FVector3f Normal = NormalOverlay
				? DynMeshToPointsGetFirstVertexOverlayElement(Mesh, NormalOverlay, VID, FVector3f::UnitZ(), &NormalCorner)
				: (Mesh->HasVertexNormals() ? Mesh->GetVertexNormal(VID) : FVector3f::UnitZ());
			if (Normal.ContainsNaN() || !Normal.Normalize())
			{
				Normal = FVector3f::UnitZ();
			}

			FVector3f Tangent = FVector3f::ZeroVector;
			if (TangentOverlay)
			{
				// One point represents a vertex, including at seams. Keep its frame on the same face corner.
				if (NormalCorner.A != INDEX_NONE)
				{
					if (TangentOverlay->IsSetTriangle(NormalCorner.A))
					{
						const int32 ElementID = TangentOverlay->GetTriangle(NormalCorner.A)[NormalCorner.B];
						if (TangentOverlay->IsElement(ElementID))
						{
							Tangent = TangentOverlay->GetElement(ElementID);
						}
					}
				}
				else
				{
					Tangent = DynMeshToPointsGetFirstVertexOverlayElement(Mesh, TangentOverlay, VID, FVector3f::ZeroVector);
				}
			}

			const FVector3f VertexColor = Mesh->HasVertexColors() ? Mesh->GetVertexColor(VID) : FVector3f::One();
			const FVector4f Color = ColorOverlay
				? DynMeshToPointsGetFirstVertexOverlayElement(Mesh, ColorOverlay, VID, FVector4f(1.0f, 1.0f, 1.0f, 1.0f))
				: FVector4f(VertexColor.X, VertexColor.Y, VertexColor.Z, 1.0f);

			FPCGPoint& Point = Points.Emplace_GetRef();
			Point.Transform = PCGUtilsDynMeshSpaceHelpers::TransformSurfaceFrameToWorld(
				MeshToOutput, FVector(Position), FVector(Normal), FVector(Tangent));
			Point.Color = FVector4(Color.X, Color.Y, Color.Z, Color.W);
			Point.SetExtents(FVector::ZeroVector);
			Point.Density = 1.0f;
		}
		TArray<int> PointIndices;
		for (int i = 0; i < Points.Num(); ++i)
		{
			PointIndices.Add(i);
		}
		PointArrayData->SetPointsFrom(PointData, PointIndices);

		if (Settings->bOutputVertexIndex)
		{
			if (Settings->VertexIndexAttribute.IsNone())
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("EmptyVertexIndexAttribute", "DynMeshToPoints cannot output an unnamed Vertex Index attribute."), InContext);
			}
			else if (UPCGMetadata* Metadata = PointArrayData->MutableMetadata())
			{
				FPCGMetadataDomain* ElementsDomain = Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
				FPCGMetadataAttribute<int32>* VertexIndexAttribute = ElementsDomain
					? ElementsDomain->FindOrCreateAttribute<int32>(Settings->VertexIndexAttribute, INDEX_NONE, false, true)
					: nullptr;
				if (VertexIndexAttribute)
				{
					auto MetadataEntries = PointArrayData->GetMetadataEntryValueRange();
					int32 PointIndex = 0;
					for (const int32 VID : Mesh->VertexIndicesItr())
					{
						ElementsDomain->InitializeOnSet(MetadataEntries[PointIndex]);
						VertexIndexAttribute->SetValue(MetadataEntries[PointIndex], VID);
						++PointIndex;
					}
				}
			}
		}

		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = PointArrayData;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
