// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGGetMPCValues.generated.h"

class UMaterialParameterCollection;
class UPCGParamData;

/** Where Get MPC Values reads parameter values from. */
UENUM(BlueprintType)
enum class EPCGUtilsMPCValueSource : uint8
{
	/** The default values saved in the collection asset. */
	Defaults,
	/**
	 * The values the world's instance of the collection holds right now: whatever Blueprints or code have set with
	 * Set Scalar/Vector Parameter Value. In an editor world nothing usually sets them, so they match the defaults.
	 */
	World,
};

/** How a vector parameter is written. Scalar parameters always write a double. */
UENUM(BlueprintType)
enum class EPCGUtilsMPCVectorOutput : uint8
{
	Vector4 UMETA(ToolTip="All four channels as a Vector4 (X=R, Y=G, Z=B, W=A)."),
	Vector UMETA(ToolTip="RGB as a Vector (X=R, Y=G, Z=B)."),
	Vector2 UMETA(ToolTip="RG as a Vector2 (X=R, Y=G)."),
	R UMETA(DisplayName="R (Double)", ToolTip="The R channel as a double."),
	G UMETA(DisplayName="G (Double)", ToolTip="The G channel as a double."),
	B UMETA(DisplayName="B (Double)", ToolTip="The B channel as a double."),
	A UMETA(DisplayName="A (Double)", ToolTip="The A channel as a double."),
};

/** One collection parameter and the attribute it is written to. */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsMPCParameterRead
{
	GENERATED_BODY()

	/** Required. A scalar or vector parameter of the collection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parameter", meta=(GetOptions="GetMPCParameterNameOptions"))
	FName Parameter;

	/**
	 * Required. The attribute the value is written to on the output attribute set. Filled in with the parameter's
	 * name when left empty. Type: double for a scalar; for a vector, set by Vector Output.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parameter")
	FName AttributeName;

	/** How a vector parameter is written. Ignored for scalar parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parameter")
	EPCGUtilsMPCVectorOutput VectorOutput = EPCGUtilsMPCVectorOutput::Vector4;
};

/**
 * Reads parameters of a Material Parameter Collection into an attribute set with one entry, one attribute per
 * listed parameter. Use it to drive a graph from the same global values materials read (wind, wetness, season,
 * a world tint), so the two stay in step.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Params",
	meta=(Keywords="MPC material parameter collection scalar vector global read get attribute set params"))
class PCGUTILS_API UPCGGetMPCValuesSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetMPCValues")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Params); }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual FString GetAdditionalTitleInformation() const override;

	/** Required. The collection to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	TSoftObjectPtr<UMaterialParameterCollection> Collection;

	/**
	 * Read the asset's saved defaults, or the values the world's instance holds right now. Not overridable: the
	 * node's result is cached for Defaults and recomputed every run for World, and that is decided before
	 * overrides apply.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	EPCGUtilsMPCValueSource Source = EPCGUtilsMPCValueSource::Defaults;

	/**
	 * The parameters to read, each with the attribute it writes. Add All Parameters fills this from the collection.
	 * Not overridable: PCG cannot override an array of structs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(TitleProperty="{Parameter} -> {AttributeName}"))
	TArray<FPCGUtilsMPCParameterRead> Parameters;

#if WITH_EDITOR
	/** Adds an entry for every collection parameter not already listed, writing an attribute of the same name. */
	UFUNCTION(CallInEditor, Category="Settings")
	void AddAllParameters();
#endif

	/** Scalar then vector parameter names of Collection, for the Parameter dropdown. */
	UFUNCTION()
	TArray<FString> GetMPCParameterNameOptions() const;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return {}; }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILS_API FPCGGetMPCValuesElement : public IPCGElement
{
public:
	/**
	 * Builds the attribute set. Returns null and logs one graph error per failed check when the settings cannot be
	 * read. World is only needed for EPCGUtilsMPCValueSource::World.
	 */
	static UPCGParamData* ReadValues(FPCGContext* Context, const UPCGGetMPCValuesSettings& Settings,
		const UMaterialParameterCollection* Collection, const UWorld* World);

	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	/** Defaults are cached with the graph; editing the collection asset needs a regenerate to show up. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
