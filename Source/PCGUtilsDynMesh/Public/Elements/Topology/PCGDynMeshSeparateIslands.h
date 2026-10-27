// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "Geometry/PCGUtilsDynMeshProperties.h"
#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshSeparateIslands.generated.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace PCGDynMeshSeparateIslandsConstants
{
	inline const FName IslandsPin = TEXT("Islands");
	inline const FName UnselectedPin = TEXT("Unselected");
}

/** What joins two triangles into the same island. */
UENUM(BlueprintType)
enum class EPCGDynMeshIslandConnectivity : uint8
{
	/** Triangles that share a vertex are one island. Matches Blender's Separate by Loose Parts. */
	SharedVertices,

	/** Triangles must share an edge. Two parts touching at a single vertex (a bowtie) become separate islands. */
	SharedEdges,

	/**
	 * Triangles whose vertices lie within Connect Distance of each other are one island, even when the mesh does
	 * not join them. Use for meshes whose parts touch but were never welded.
	 */
	Proximity
};

namespace PCGDynMeshSeparateIslands
{
	/**
	 * Groups the triangles of Mesh into islands. With TriangleFilter non-empty, only those triangles are grouped,
	 * and only through each other: two filtered patches that meet only through unfiltered triangles stay apart.
	 * Islands are ordered by their lowest triangle ID; each island's triangles are ascending.
	 */
	PCGUTILSDYNMESH_API void FindIslands(const UE::Geometry::FDynamicMesh3& Mesh, EPCGDynMeshIslandConnectivity Connectivity,
		double ConnectDistance, const TArray<int32>* TriangleFilter, TArray<TArray<int32>>& OutIslands);

	/**
	 * Copies Triangles of Source into a new mesh: positions, every overlay (UVs, normals, colors), material IDs,
	 * polygroup layers and weight layers.
	 */
	PCGUTILSDYNMESH_API UE::Geometry::FDynamicMesh3 ExtractTriangles(const UE::Geometry::FDynamicMesh3& Source, const TArray<int32>& Triangles);
}

/**
 * DynMesh | Separate Islands. Splits each mesh into its connected islands and emits each island as its own DynMesh
 * on Islands. Every island keeps the input's metadata (@Data included), tags and target actor, and can be numbered
 * with an @Data attribute.
 *
 * Selection-aware: with a selection (or a Selector), only the selected triangles are split, and connectivity runs
 * through selected triangles only. The rest of the mesh is emitted whole on Unselected.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh separate split islands loose parts connected components pieces explode"))
class PCGUTILSDYNMESH_API UPCGDynMeshSeparateIslandsSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	/** What joins two triangles into the same island. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGDynMeshIslandConnectivity Connectivity = EPCGDynMeshIslandConnectivity::SharedVertices;

	/** Proximity only: vertices closer than this, in the mesh's own units, join their triangles into one island. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="0.0", Units="cm",
		EditCondition="Connectivity == EPCGDynMeshIslandConnectivity::Proximity", EditConditionHides))
	double ConnectDistance = 0.01;

	/** Write each island's number (0, 1, 2... in order of the island's lowest triangle ID) to @Data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIslandIndex = true;

	/**
	 * Optional output. @Data attribute (int32) that receives the island's number, counted per input mesh. Written
	 * only when the toggle is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bWriteIslandIndex"))
	FName IslandIndexAttribute = TEXT("IslandIndex");

	/** Write how many islands the input mesh had to @Data on every island. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIslandCount = false;

	/** Optional output. @Data attribute (int32) that receives the input mesh's island count. Written only when the toggle is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bWriteIslandCount"))
	FName IslandCountAttribute = TEXT("IslandCount");

	/** Mesh properties written to @Data on each island and on Unselected, as DynMesh | Properties writes them. All off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Properties", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshFreeProperties OutputProperties;

	virtual const FPCGUtilsDynMeshFreeProperties* GetInlineFreeProperties() const override { return &OutputProperties; }

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SeparateIslands"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshSeparateIslandsElement : public FPCGUtilsDynMeshProcessBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
