// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Target/PCGUtilsPainterTarget.h"

class UPCGDynamicMeshData;
namespace UE::Geometry { class FDynamicMesh3; }

/**
 * Painter target backed by a native `UPCGDynamicMeshData`. The canonical mesh IS the input mesh, so evaluation
 * happens directly on it and Commit is a no-op. This keeps `Paint DynMesh Vertex Color` on the exact same
 * write path (`PCGUtilsDynMeshAttributeHelpers`) it always used, now routed through the shared target layer.
 */
struct PCGUTILSPAINTER_API FPCGUtilsPainterDynMeshTarget final : public FPCGUtilsPainterTarget
{
	FPCGUtilsPainterDynMeshTarget(
		UPCGDynamicMeshData* InMeshData,
		const FTransform& InLocalToWorld,
		int32 InDataSetIndex = 0,
		int32 InDataSetCount = 1);

	virtual bool IsValid() const override { return CanonicalMesh != nullptr; }
	virtual UE::Geometry::FDynamicMesh3* GetCanonicalMesh() override { return CanonicalMesh; }
	virtual const UPCGDynamicMeshData* GetCanonicalMeshData() const override { return MeshData; }
	virtual bool IsNativeDynMeshTarget() const override { return true; }
	virtual FTransform GetLocalToWorld() const override { return LocalToWorld; }
	virtual int32 GetDataSetIndex() const override { return DataSetIndex; }
	virtual int32 GetDataSetCount() const override { return DataSetCount; }
	virtual bool Commit(FPCGContext* Context) override { return true; }

private:
	UPCGDynamicMeshData* MeshData = nullptr;
	UE::Geometry::FDynamicMesh3* CanonicalMesh = nullptr;
	FTransform LocalToWorld = FTransform::Identity;
	int32 DataSetIndex = 0;
	int32 DataSetCount = 1;
};
