#pragma once

#include "CoreMinimal.h"
#include "PCGDataAsset.h"
#include "PCGUtilsDataCacheAsset.generated.h"

/**
 * The asset Save PCG Cache Data writes. A plain PCG Data Asset, except that it can be saved when it holds DynMesh
 * data and nothing else.
 *
 * Why: SavePackage first walks every export with a reference-collecting archive to learn which custom versions the
 * package uses, then writes it for real and refuses the save if a version shows up that the first pass did not
 * see. FDynamicMesh3::Serialize returns early for reference collectors, so the versions only its attribute set
 * declares (FortniteRelease, FortniteMain) appear only in the real write: "Unexpected custom version
 * "FortniteRelease" found when saving ... Package will not be saved." Declaring them here, on every pass, keeps both
 * passes in agreement.
 */
UCLASS(BlueprintType, hidecategories=Object, ClassGroup=(Procedural))
class PCGUTILSDATACACHE_API UPCGUtilsDataCacheAsset : public UPCGDataAsset
{
	GENERATED_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;
};
