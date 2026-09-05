// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"

struct FPCGContext;

/** Lightweight per-execution operation created from immutable factory data. */
class PCGUTILSCORE_API FPCGUtilsDynMeshOperation : public TSharedFromThis<FPCGUtilsDynMeshOperation>
{
public:
	FPCGUtilsDynMeshOperation();
	FPCGUtilsDynMeshOperation(const FPCGUtilsDynMeshOperation&);
	virtual ~FPCGUtilsDynMeshOperation();

	void BindContext(FPCGContext* InContext) { Context = InContext; }

protected:
	FPCGContext* Context = nullptr;
};
