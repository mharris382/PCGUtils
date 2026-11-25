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

		const bool bTriangles = Settings->Mode == EPCGDynMeshToPointsMode::Triangles;
		const int32 NumElements = bTriangles ? Mesh->TriangleCount() : Mesh->VertexCount();

		UPCGPointData* PointData = NewObject<UPCGPointData>();
		TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
		Points.Reserve(NumElements);
		// The mesh element each point was made from, for the ID attribute.
		TArray<int32> ElementIDs;
		ElementIDs.Reserve(NumElements);
		UPCGPointArrayData* PointArrayData = NewObject<UPCGPointArrayData>();
		PointArrayData->SetNumPoints(NumElements);

		auto AddPoint = [&Points, &ElementIDs, &MeshToOutput](int32 ElementID, const FVector3d& Position, FVector3f Normal, const FVector3f& Tangent, const FVector4f& Color)
		{
			if (Normal.ContainsNaN() || !Normal.Normalize())
			{
				Normal = FVector3f::UnitZ();
			}
			FPCGPoint& Point = Points.Emplace_GetRef();
			Point.Transform = PCGUtilsDynMeshSpaceHelpers::TransformSurfaceFrameToWorld(
				MeshToOutput, FVector(Position), FVector(Normal), FVector(Tangent));
			Point.Color = FVector4(Color.X, Color.Y, Color.Z, Color.W);
			Point.SetExtents(FVector::ZeroVector);
			Point.Density = 1.0f;
			ElementIDs.Add(ElementID);
		};

		if (bTriangles)
		{
			for (const int32 TID : Mesh->TriangleIndicesItr())
			{
				// The face's own normal: corner normals belong to the vertices and differ across a smooth face.
				const FVector3f Normal = FVector3f(Mesh->GetTriNormal(TID));

				FVector3f Tangent = FVector3f::ZeroVector;
				if (TangentOverlay && TangentOverlay->IsSetTriangle(TID))
				{
					const FIndex3i Elements = TangentOverlay->GetTriangle(TID);
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						if (TangentOverlay->IsElement(Elements[Corner]))
						{
							Tangent += TangentOverlay->GetElement(Elements[Corner]);
						}
					}
				}

				FVector4f Color(1.0f, 1.0f, 1.0f, 1.0f);
				if (ColorOverlay && ColorOverlay->IsSetTriangle(TID))
				{
					const FIndex3i Elements = ColorOverlay->GetTriangle(TID);
					Color = (ColorOverlay->GetElement(Elements.A) + ColorOverlay->GetElement(Elements.B) + ColorOverlay->GetElement(Elements.C)) / 3.0f;
				}
				else if (!ColorOverlay && Mesh->HasVertexColors())
				{
					const FIndex3i Vertices = Mesh->GetTriangle(TID);
					const FVector3f Average = (Mesh->GetVertexColor(Vertices.A) + Mesh->GetVertexColor(Vertices.B) + Mesh->GetVertexColor(Vertices.C)) / 3.0f;
					Color = FVector4f(Average.X, Average.Y, Average.Z, 1.0f);
				}

				AddPoint(TID, Mesh->GetTriCentroid(TID), Normal, Tangent, Color);
			}
		}
		else
		{
			for (const int32 VID : Mesh->VertexIndicesItr())
			{
				FIndex2i NormalCorner(INDEX_NONE, INDEX_NONE);
				const FVector3f Normal = NormalOverlay
					? DynMeshToPointsGetFirstVertexOverlayElement(Mesh, NormalOverlay, VID, FVector3f::UnitZ(), &NormalCorner)
					: (Mesh->HasVertexNormals() ? Mesh->GetVertexNormal(VID) : FVector3f::UnitZ());

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

				AddPoint(VID, Mesh->GetVertex(VID), Normal, Tangent, Color);
			}
		}

		TArray<int> PointIndices;
		PointIndices.Reserve(Points.Num());
		for (int i = 0; i < Points.Num(); ++i)
		{
			PointIndices.Add(i);
		}
		PointArrayData->SetPointsFrom(PointData, PointIndices);

		const bool bOutputElementID = bTriangles ? Settings->bOutputTriangleId : Settings->bOutputVertexIndex;
		const FName ElementIDAttributeName = bTriangles ? Settings->TriangleIdAttribute : Settings->VertexIndexAttribute;
		if (bOutputElementID)
		{
			if (ElementIDAttributeName.IsNone())
			{
				PCGLog::LogWarningOnGraph(bTriangles
					? LOCTEXT("EmptyTriangleIdAttribute", "DynMesh | To Points: Output Triangle Id is on, but Triangle Id Attribute is None; no triangle ID attribute was written.")
					: LOCTEXT("EmptyVertexIndexAttribute", "DynMeshToPoints cannot output an unnamed Vertex Index attribute."), InContext);
			}
			else if (UPCGMetadata* Metadata = PointArrayData->MutableMetadata())
			{
				FPCGMetadataDomain* ElementsDomain = Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
				FPCGMetadataAttribute<int32>* ElementIDAttribute = ElementsDomain
					? ElementsDomain->FindOrCreateAttribute<int32>(ElementIDAttributeName, INDEX_NONE, false, true)
					: nullptr;
				if (ElementIDAttribute)
				{
					auto MetadataEntries = PointArrayData->GetMetadataEntryValueRange();
					for (int32 PointIndex = 0; PointIndex < ElementIDs.Num(); ++PointIndex)
					{
						ElementsDomain->InitializeOnSet(MetadataEntries[PointIndex]);
						ElementIDAttribute->SetValue(MetadataEntries[PointIndex], ElementIDs[PointIndex]);
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
