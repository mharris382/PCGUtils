// Copyright Max Harris

#include "Elements/UV/PCGDynMeshPackUVIslands.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshUVPack.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPackUVIslands"

#if WITH_EDITOR
FText UPCGDynMeshPackUVIslandsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Pack UV Islands");
}

FText UPCGDynMeshPackUVIslandsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Packs a UV layer's islands into the unit square without overlap, after Blender's Pack Islands (bounding box "
		"shapes): average island scale, rotation, and Scaled / Add / Fraction margins. Or Geometry Script's Repack. "
		"A DynMesh Selection or Selector packs only its triangles' islands.");
}

FString UPCGDynMeshPackUVIslandsSettings::GetAdditionalTitleInformation() const
{
	if (Method == EPCGUtilsDynMeshUVPackMethod::GeometryScript)
	{
		return FString::Printf(TEXT("Repack, %dpx"), RepackOptions.TargetImageWidth);
	}
	return FString::Printf(TEXT("%s margin %s"),
		*StaticEnum<EPCGUtilsDynMeshUVPackMargin>()->GetDisplayNameTextByValue(static_cast<int64>(MarginMethod)).ToString(),
		*FString::SanitizeFloat(Margin, 0));
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshPackUVIslandsSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshPackUVIslandsOperation> Operation = MakeShared<FPCGUtilsDynMeshPackUVIslandsOperation>();
	Operation->UVLayer = UVLayer;
	Operation->Method = Method;
	Operation->bAverageIslandScale = bAverageIslandScale;
	Operation->bScale = bScale;
	Operation->Rotation = Rotation;
	Operation->MarginMethod = MarginMethod;
	Operation->Margin = Margin;
	Operation->RepackOptions = RepackOptions;
	return Operation;
}

FPCGElementPtr UPCGDynMeshPackUVIslandsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshPackUVIslandsElement>();
}

bool FPCGUtilsDynMeshPackUVIslandsOperation::ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const
{
	if (Method == EPCGUtilsDynMeshUVPackMethod::GeometryScript)
	{
		const bool bRan = PCGUtilsDynMeshUV::RunOnRegion(Invocation.Context, Mesh, UVLayer, TriangleIDs, [this](UDynamicMesh* Working)
		{
			UGeometryScriptLibrary_MeshUVFunctions::RepackMeshUVs(Working, UVLayer, RepackOptions);
			return true;
		});
		if (!bRan)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("RepackFailed",
				"DynMesh | Pack UV Islands (input {0}): Geometry Script Repack did not return UVs on layer {1}; the mesh's UVs were left unchanged."),
				FText::AsNumber(Invocation.InputIndex), FText::AsNumber(UVLayer)), Invocation.Context);
		}
		return true;
	}

	PCGUtilsDynMeshUVPack::FOptions Options;
	Options.bAverageIslandScale = bAverageIslandScale;
	Options.bScale = bScale;
	Options.Rotation = static_cast<PCGUtilsDynMeshUVPack::ERotation>(Rotation);
	Options.MarginMethod = static_cast<PCGUtilsDynMeshUVPack::EMargin>(MarginMethod);
	Options.Margin = Margin;
	const PCGUtilsDynMeshUVPack::FRegionResult Result = PCGUtilsDynMeshUVPack::PackRegion(Mesh, UVOverlay, TriangleIDs, Options);

	if (Result.Unset > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("Unset",
			"DynMesh | Pack UV Islands (input {0}): {1} triangles have no UVs on layer {2}, so were not packed. Run Auto UV or a projection first."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Result.Unset), FText::AsNumber(UVLayer)), Invocation.Context);
	}
	if (Result.Islands > 0 && !Result.Pack.bPacked)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NoArea",
			"DynMesh | Pack UV Islands (input {0}): the {1} islands on layer {2} have no UV area (every UV in one place?), so nothing was packed."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Result.Islands), FText::AsNumber(UVLayer)), Invocation.Context);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
