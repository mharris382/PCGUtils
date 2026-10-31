// Copyright Max Harris

#include "Elements/UV/PCGDynMeshAutoUV.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshUVPack.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshAutoUV"

#if WITH_EDITOR
FText UPCGDynMeshAutoUVSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Auto UV");
}

FText UPCGDynMeshAutoUVSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Generates UVs with one of Geometry Script's automatic methods: Patch Builder, XAtlas, or a per-island "
		"Conformal, Spectral Conformal or Exponential Map flatten. A DynMesh Selection or Selector unwraps only those "
		"triangles, packed into their own unit square.");
}

FString UPCGDynMeshAutoUVSettings::GetAdditionalTitleInformation() const
{
	return StaticEnum<EPCGUtilsDynMeshAutoUVMethod>()->GetDisplayNameTextByValue(static_cast<int64>(Method)).ToString();
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshAutoUVSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshAutoUVOperation> Operation = MakeShared<FPCGUtilsDynMeshAutoUVOperation>();
	Operation->UVLayer = UVLayer;
	Operation->Method = Method;
	Operation->PatchBuilderOptions = PatchBuilderOptions;
	Operation->XAtlasOptions = XAtlasOptions;
	Operation->RecomputeOptions.Method =
		Method == EPCGUtilsDynMeshAutoUVMethod::Conformal ? EGeometryScriptUVFlattenMethod::Conformal
		: Method == EPCGUtilsDynMeshAutoUVMethod::ExpMap ? EGeometryScriptUVFlattenMethod::ExpMap
		: EGeometryScriptUVFlattenMethod::SpectralConformal;
	Operation->RecomputeOptions.IslandSource = IslandSource;
	Operation->RecomputeOptions.GroupLayer = GroupLayer;
	Operation->RecomputeOptions.bAutoAlignIslandsWithAxes = bAutoAlignIslandsWithAxes;
	Operation->RecomputeOptions.ExpMapOptions = ExpMapOptions;
	Operation->RecomputeOptions.SpectralConformalOptions = SpectralConformalOptions;
	Operation->bPackIslands = bPackIslands;
	return Operation;
}

FPCGElementPtr UPCGDynMeshAutoUVSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshAutoUVElement>();
}

bool FPCGUtilsDynMeshAutoUVOperation::ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const
{
	const bool bPerIsland = Method != EPCGUtilsDynMeshAutoUVMethod::PatchBuilder && Method != EPCGUtilsDynMeshAutoUVMethod::XAtlas;

	// Every method runs on just the region's triangles (a copy of them), so a selection unwraps on its own.
	const bool bRan = PCGUtilsDynMeshUV::RunOnRegion(Invocation.Context, Mesh, UVLayer, TriangleIDs, [this](UDynamicMesh* Working)
	{
		switch (Method)
		{
		case EPCGUtilsDynMeshAutoUVMethod::PatchBuilder:
			UGeometryScriptLibrary_MeshUVFunctions::AutoGeneratePatchBuilderMeshUVs(Working, UVLayer, PatchBuilderOptions);
			break;
		case EPCGUtilsDynMeshAutoUVMethod::XAtlas:
			UGeometryScriptLibrary_MeshUVFunctions::AutoGenerateXAtlasMeshUVs(Working, UVLayer, XAtlasOptions);
			break;
		default:
			UGeometryScriptLibrary_MeshUVFunctions::RecomputeMeshUVs(Working, UVLayer, RecomputeOptions, FGeometryScriptMeshSelection());
			break;
		}
		return true;
	});
	if (!bRan)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("Failed",
			"DynMesh | Auto UV (input {0}): {1} did not produce UVs on layer {2}; the mesh's UVs were left unchanged."),
			FText::AsNumber(Invocation.InputIndex),
			StaticEnum<EPCGUtilsDynMeshAutoUVMethod>()->GetDisplayNameTextByValue(static_cast<int64>(Method)),
			FText::AsNumber(UVLayer)), Invocation.Context);
		return true;
	}

	if (bPerIsland && bPackIslands)
	{
		// RunOnRegion rebuilt the overlay; fetch it again rather than trusting the reference we were given.
		PCGUtilsDynMeshUVPack::PackRegion(Mesh, *Mesh.Attributes()->GetUVLayer(UVLayer), TriangleIDs, PCGUtilsDynMeshUVPack::FOptions());
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
