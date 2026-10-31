// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGCopySockets.generated.h"

class UStaticMesh;

/** How Copy Sockets picks the sockets of each point's mesh. */
UENUM(BlueprintType)
enum class EPCGUtilsSocketSelectionMode : uint8
{
	SocketNames UMETA(DisplayName="Socket Names", ToolTip="The sockets listed in Socket Names. A listed name the mesh does not have is skipped with a warning."),
	SocketTag UMETA(DisplayName="Socket Tag", ToolTip="Every socket whose Tag equals Socket Tag."),
	AllSockets UMETA(DisplayName="All Sockets", ToolTip="Every socket on the mesh.")
};

namespace PCGUtilsCopySockets
{
	/** One selected socket, in the mesh's local space. */
	struct FSelectedSocket
	{
		FName Name;
		FString Tag;
		FTransform LocalTransform;
	};

	/** The mesh's sockets chosen by Mode, in the mesh's socket order. Names missing from the mesh go to OutMissingNames. */
	PCGUTILS_API TArray<FSelectedSocket> SelectSockets(const UStaticMesh& Mesh, EPCGUtilsSocketSelectionMode Mode,
		TConstArrayView<FName> SocketNames, const FString& SocketTag, TArray<FName>* OutMissingNames = nullptr);
}

/**
 * Treats each input point as an instance of its mesh and replaces it with one point per selected socket of that
 * mesh, placed where the socket sits on the instance. Like Copy Points, with the sockets as the source points.
 * Every output point keeps its original point's attributes and properties; @Data and tags carry over.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils",
	meta=(Keywords="Copy Sockets Socket Static Mesh Points Attach Instance Spawn Transform To Mesh Socket"))
class PCGUTILS_API UPCGCopySocketsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGCopySocketsSettings();

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("CopySockets")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Points); }
#endif
	virtual FString GetAdditionalTitleInformation() const override;

	/**
	 * Required. The static mesh each point is an instance of (Soft Object Path, or a string path). A plain name
	 * reads it per point; "@Data.Mesh" reads one mesh for the whole data set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector MeshAttribute;

	/** How the sockets are chosen on each mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGUtilsSocketSelectionMode SocketSelection = EPCGUtilsSocketSelectionMode::SocketNames;

	/** The sockets to copy, in this order for each point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="SocketSelection == EPCGUtilsSocketSelectionMode::SocketNames", EditConditionHides))
	TArray<FName> SocketNames;

	/** Copy every socket whose Tag equals this (case-sensitive, as typed in the mesh's socket details). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="SocketSelection == EPCGUtilsSocketSelectionMode::SocketTag", EditConditionHides))
	FString SocketTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputSocketName = true;

	/** Optional. Per point Name: the socket each point came from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputSocketName"))
	FName SocketNameAttributeName = TEXT("SocketName");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputSocketTag = false;

	/** Optional. Per point String: the Tag of the socket each point came from (empty when the socket has none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputSocketTag"))
	FName SocketTagAttributeName = TEXT("SocketTag");

	/** Load meshes on the game thread instead of asynchronously. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", AdvancedDisplay)
	bool bSynchronousLoad = false;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

struct FPCGCopySocketsContext : public FPCGContext, public IPCGAsyncLoadingContext {};

class PCGUTILS_API FPCGCopySocketsElement : public IPCGElement
{
public:
	virtual FPCGContext* CreateContext() override { return new FPCGCopySocketsContext(); }
	/** Loading and reading mesh sockets touch UObjects; the per-point work inside is parallel. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool SupportsBasePointDataInputs(FPCGContext* Context) const override { return true; }

	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
