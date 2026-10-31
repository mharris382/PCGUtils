// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "PCGUtilsPainterDecorator.generated.h"

namespace PCGUtilsPainterDecoratorConstants
{
	/** The decorated Painter. Same label as the output pin: a decorator is a Painter in, a Painter out. */
	inline const FName SourcePin = TEXT("Painter");
}

/**
 * A Painter that wraps exactly one other Painter and changes its field: Painter | Blur averages it over a radius.
 * The wrapped Painter is created, initialized and prepared by the decorator, so a derived operation only adds
 * its own preparation and evaluation.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGUtilsDynMeshPainterDecoratorFactoryData : public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData> Source;
};

/** Runtime half of a decorator: owns the wrapped Painter's operation and runs its lifecycle. */
class PCGUTILSPAINTER_API FPCGUtilsPainterDecoratorOperation : public FPCGUtilsDynMeshPainterOperation
{
public:
	explicit FPCGUtilsPainterDecoratorOperation(const UPCGUtilsDynMeshPainterDecoratorFactoryData* InFactory);

	/** Creates and initializes the wrapped Painter. Derived operations call this first. */
	virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override;

	/** Prepares the wrapped Painter. Derived operations call this first. */
	virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override;

	/** A decorator returns the same kind of value as the Painter it wraps. */
	virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override;

protected:
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> SourceOperation;

private:
	const UPCGUtilsDynMeshPainterDecoratorFactoryData* DecoratorFactory = nullptr;
};

/** Provider base for decorators: one required Painter input pin, wired into the factory's Source. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGUtilsDynMeshPainterDecoratorProviderSettings : public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;

	/** Derived providers create their factory, fill their own fields, then call this to wire Source and finish. */
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
