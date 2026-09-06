// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

/**
 * A Painter operation that returns a fixed scalar for every sample. Used to normalise "constant" branches so a
 * multiplexing Painter (Selection Painter Switch / Selection to Painter) evaluates every branch through the same
 * `FPCGUtilsDynMeshPainterOperation::Evaluate` path instead of carrying a separate constant code path.
 */
class FPCGUtilsPainterConstantOperation final : public FPCGUtilsDynMeshPainterOperation
{
public:
	explicit FPCGUtilsPainterConstantOperation(float InValue)
		: Value(InValue)
	{
	}

	virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
	{
		return EPCGUtilsDynMeshPainterValueType::Scalar;
	}

	virtual FPCGUtilsDynMeshPainterValue Evaluate(
		const FPCGUtilsDynMeshPainterSample& /*Sample*/) const override
	{
		return FPCGUtilsDynMeshPainterValue::MakeScalar(Value);
	}

private:
	float Value = 0.0f;
};
