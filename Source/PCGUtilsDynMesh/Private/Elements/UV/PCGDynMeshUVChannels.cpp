// Copyright Max Harris

#include "Elements/UV/PCGDynMeshUVChannels.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshUVChannels"

#if WITH_EDITOR
FText UPCGDynMeshUVChannelsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | UV Channels");
}

FText UPCGDynMeshUVChannelsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Copies one UV layer into another, clears a layer, or sets how many UV layers the mesh has. Copy and Clear "
		"follow a DynMesh Selection or Selector; Set Layer Count is whole mesh.");
}

FString UPCGDynMeshUVChannelsSettings::GetAdditionalTitleInformation() const
{
	switch (Operation)
	{
	case EPCGUtilsDynMeshUVChannelOperation::Copy: return FString::Printf(TEXT("Copy UV%d to UV%d"), SourceLayer, UVLayer);
	case EPCGUtilsDynMeshUVChannelOperation::Clear: return FString::Printf(TEXT("Clear UV%d"), UVLayer);
	default: return FString::Printf(TEXT("%d layers"), LayerCount);
	}
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshUVChannelsSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshUVChannelsOperation> Result = MakeShared<FPCGUtilsDynMeshUVChannelsOperation>();
	Result->UVLayer = UVLayer;
	Result->Operation = Operation;
	Result->SourceLayer = SourceLayer;
	Result->LayerCount = LayerCount;
	return Result;
}

FPCGElementPtr UPCGDynMeshUVChannelsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshUVChannelsElement>();
}

bool FPCGUtilsDynMeshUVChannelsOperation::ShouldProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, TArrayView<const int32>) const
{
	// Checked before the base creates UV Layer, so a bad setting leaves the mesh untouched.
	if (Operation == EPCGUtilsDynMeshUVChannelOperation::Copy && SourceLayer == UVLayer)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("SameLayer",
			"DynMesh | UV Channels (input {0}): Source Layer and UV Layer are both {1}; copying a layer onto itself does nothing. The mesh was left unchanged."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(UVLayer)), Invocation.Context);
		return false;
	}
	if (Operation == EPCGUtilsDynMeshUVChannelOperation::Copy && SourceLayer < 0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NegativeSource",
			"DynMesh | UV Channels (input {0}): Source Layer is {1}; it must be 0 or more. The mesh was left unchanged."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(SourceLayer)), Invocation.Context);
		return false;
	}
	if (Operation == EPCGUtilsDynMeshUVChannelOperation::Copy)
	{
		const UDynamicMesh* Object = Invocation.MeshData ? Invocation.MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = Object ? Object->GetMeshPtr() : nullptr;
		const int32 NumLayers = Mesh && Mesh->HasAttributes() ? Mesh->Attributes()->NumUVLayers() : 0;
		if (SourceLayer >= NumLayers)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoSource",
				"DynMesh | UV Channels (input {0}): Source Layer is {1}, but the mesh has {2} UV layers. The mesh was left unchanged."),
				FText::AsNumber(Invocation.InputIndex), FText::AsNumber(SourceLayer), FText::AsNumber(NumLayers)), Invocation.Context);
			return false;
		}
	}
	if (Operation == EPCGUtilsDynMeshUVChannelOperation::SetCount && (LayerCount < 1 || LayerCount > 8))
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadCount",
			"DynMesh | UV Channels (input {0}): Layer Count is {1}; it must be 1 to 8. The mesh was left unchanged."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(LayerCount)), Invocation.Context);
		return false;
	}
	return true;
}

bool FPCGUtilsDynMeshUVChannelsOperation::ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const
{
	switch (Operation)
	{
	case EPCGUtilsDynMeshUVChannelOperation::Copy:
	{
		PCGUtilsDynMeshUV::FRegionUVs Region;
		PCGUtilsDynMeshUV::Gather(Mesh, *Mesh.Attributes()->GetUVLayer(SourceLayer), TriangleIDs, Region);
		if (!Region.bWholeMesh)
		{
			// Triangles of the selection the source leaves without UVs end without them too.
			for (const int32 TriangleID : TriangleIDs)
			{
				if (Mesh.IsTriangle(TriangleID) && UVOverlay.IsSetTriangle(TriangleID))
				{
					UVOverlay.UnsetTriangle(TriangleID);
				}
			}
		}
		PCGUtilsDynMeshUV::WriteBack(UVOverlay, Region);
		return true;
	}
	case EPCGUtilsDynMeshUVChannelOperation::Clear:
		if (TriangleIDs.Num() == Mesh.TriangleCount())
		{
			UVOverlay.ClearElements();
		}
		else
		{
			for (const int32 TriangleID : TriangleIDs)
			{
				if (UVOverlay.IsSetTriangle(TriangleID))
				{
					UVOverlay.UnsetTriangle(TriangleID);
				}
			}
		}
		return true;
	default:
		Mesh.Attributes()->SetNumUVLayers(LayerCount);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
