#include "Elements/PCGDynMeshToPoints.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPointArrayData.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

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
	ValueType GetFirstVertexOverlayElement(
		const FDynamicMesh3* Mesh,
		const OverlayType* Overlay,
		const int32 VID,
		const ValueType& DefaultValue)
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

		const FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		const FDynamicMeshColorOverlay* ColorOverlay = nullptr;

		if (Mesh->HasAttributes())
		{
			const FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
			NormalOverlay = Attributes ? Attributes->PrimaryNormals() : nullptr;
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
			FVector3f Normal = NormalOverlay
				? GetFirstVertexOverlayElement(Mesh, NormalOverlay, VID, FVector3f::UnitZ())
				: (Mesh->HasVertexNormals() ? Mesh->GetVertexNormal(VID) : FVector3f::UnitZ());
			if (!Normal.Normalize())
			{
				Normal = FVector3f::UnitZ();
			}

			const FVector3f VertexColor = Mesh->HasVertexColors() ? Mesh->GetVertexColor(VID) : FVector3f::One();
			const FVector4f Color = ColorOverlay
				? GetFirstVertexOverlayElement(Mesh, ColorOverlay, VID, FVector4f(1.0f, 1.0f, 1.0f, 1.0f))
				: FVector4f(VertexColor.X, VertexColor.Y, VertexColor.Z, 1.0f);

			FPCGPoint& Point = Points.Emplace_GetRef();
			const FVector NormalVector((double)Normal.X, (double)Normal.Y, (double)Normal.Z);
			Point.Transform = FTransform(
				FRotationMatrix::MakeFromZ(NormalVector).ToQuat(),
				FVector(Position),
				FVector::OneVector);
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

		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = PointArrayData;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
