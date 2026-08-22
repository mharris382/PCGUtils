// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"

struct FPCGContext;

/** Lightweight per-execution operation created from immutable factory data. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshOperation : public TSharedFromThis<FPCGUtilsDynMeshOperation>
{
public:
	virtual ~FPCGUtilsDynMeshOperation() = default;

	void BindContext(FPCGContext* InContext) { Context = InContext; }

protected:
	FPCGContext* Context = nullptr;
};
