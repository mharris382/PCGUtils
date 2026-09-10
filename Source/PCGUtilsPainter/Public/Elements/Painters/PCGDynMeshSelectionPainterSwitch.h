// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "PCGDynMeshSelectionPainterSwitch.generated.h"

class UPCGUtilsDynMeshSelectionFactoryData;

/** Where a Selection Painter Switch branch takes its value from. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterBranchSource : uint8
{
	/** A fixed scalar. */
	Constant,
	/** A Painter connected to this branch's pin. */
	Painter
};

/**
 * Immutable configuration for the binary Painter multiplexer shared by "Selection to Painter" and
 * "Selection Painter Switch": a Value Selection classifies every canonical-mesh vertex, and each vertex returns
 * the Selected branch or the Unselected branch. Only the chosen branch is evaluated per vertex.
 *
 * This "Value Selection" is independent of the outer Paint node's DynMesh "Write Selection": the Value
 * Selection decides *what value* a vertex gets, the Write Selection decides *whether* that vertex is written.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshSelectionPainterSwitchFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	/** The Value Selection predicate. Any DynMesh Selector (including composite Selection Logic) is accepted. */
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> Selector;

	UPROPERTY()
	EPCGUtilsPainterBranchSource SelectedSource = EPCGUtilsPainterBranchSource::Constant;

	UPROPERTY()
	float SelectedConstant = 1.0f;

	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData> SelectedPainter;

	UPROPERTY()
	EPCGUtilsPainterBranchSource UnselectedSource = EPCGUtilsPainterBranchSource::Constant;

	UPROPERTY()
	float UnselectedConstant = 0.0f;

	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData> UnselectedPainter;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Shared base for both presentations: owns the Selector pin, the branch pins, and one CreateFactory path. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshSelectionPainterSwitchProviderSettingsBase
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	/** Presentation-specific branch configuration written into the shared factory data. */
	virtual void ConfigureBranches(UPCGDynMeshSelectionPainterSwitchFactoryData& Factory) const PURE_VIRTUAL(
		UPCGDynMeshSelectionPainterSwitchProviderSettingsBase::ConfigureBranches, );

	/** Whether the Selected/Unselected Painter branch pins are exposed for this presentation. */
	virtual bool ExposesPainterBranchPins() const { return true; }
};

/**
 * Simple presentation: turns a DynMesh selection into a scalar mask Painter. Both branches are constants
 * (Selected Value / Unselected Value, default 1 / 0); the Painter branch pins and source modes are hidden.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters",
	meta=(Keywords="selection selector painter mask convert to scalar boolean switch multiplexer DynMesh"))
class PCGUTILSPAINTER_API UPCGDynMeshSelectionToPainterProviderSettings
	: public UPCGDynMeshSelectionPainterSwitchProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionToPainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Value returned for vertices the Selector selects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float SelectedValue = 1.0f;

	/** Value returned for vertices the Selector does not select. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float UnselectedValue = 0.0f;

protected:
	virtual void ConfigureBranches(UPCGDynMeshSelectionPainterSwitchFactoryData& Factory) const override;
	virtual bool ExposesPainterBranchPins() const override { return false; }
};

/**
 * Full presentation: each branch is Constant or Painter independently, covering every branch combination.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters",
	meta=(Keywords="selection selector painter mask switch multiplexer branch constant blend if DynMesh"))
class PCGUTILSPAINTER_API UPCGDynMeshSelectionPainterSwitchProviderSettings
	: public UPCGDynMeshSelectionPainterSwitchProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionPainterSwitch"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selected Branch", meta=(PCG_Overridable))
	EPCGUtilsPainterBranchSource SelectedSource = EPCGUtilsPainterBranchSource::Constant;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selected Branch",
		meta=(PCG_Overridable, EditCondition="SelectedSource==EPCGUtilsPainterBranchSource::Constant", EditConditionHides))
	float SelectedValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unselected Branch", meta=(PCG_Overridable))
	EPCGUtilsPainterBranchSource UnselectedSource = EPCGUtilsPainterBranchSource::Constant;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unselected Branch",
		meta=(PCG_Overridable, EditCondition="UnselectedSource==EPCGUtilsPainterBranchSource::Constant", EditConditionHides))
	float UnselectedValue = 0.0f;

protected:
	virtual void ConfigureBranches(UPCGDynMeshSelectionPainterSwitchFactoryData& Factory) const override;
};
