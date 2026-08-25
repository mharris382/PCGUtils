# PCG Graph Batch

A **PCG Graph Batch** is an editor-only Data Asset that stores and runs an ordered list of standalone PCG asset graphs. It is useful for keeping repeatable asset-processing jobs together instead of locating and running each graph individually in the Content Browser.

## Creating and running a batch

1. Create a **PCG Graph Batch** asset from the PCG asset category.
2. Add entries to the **Graphs** array in the order they should run.
3. Assign a PCG Graph or PCG Graph Instance to each entry and optionally change its seed.
4. Press **Run Batch** in the asset's **Execution** category.

Entries run sequentially. Disabled entries are skipped, and **Stop on Failure** controls whether an aborted graph prevents later entries from running. The execution panel reports the active graph and provides a **Cancel** button.

## Graph requirements

Every assigned graph must have its **Graph Usage Context** set to **Asset**. Standard component graphs and Level graphs cannot run in this worldless asset-processing batch.

The batch is integrated with Unreal's Data Validation system and reports:

- Missing graph references.
- Graph Instances with no underlying graph.
- Graphs that do not use the Asset context.
- Duplicate graph entries as warnings.
- Batches with no enabled entries.

## Parameters

An entry can reference either a plain PCG Graph or a **PCG Graph Instance**. Use a Graph Instance when the batch needs saved parameter overrides:

1. Right-click the source PCG Graph and select **Create PCG Graph Instance**.
2. Configure the graph parameters on the new instance asset.
3. Assign that instance to the batch entry.

The batch executes the instance with its saved overrides. This also allows the same underlying graph to appear multiple times with different parameter sets or seeds.

> A batch controls execution order but does not pass PCG output data from one graph to the next. Each entry is an independent standalone graph execution; coordination between entries should happen through the assets or other external state they produce.
