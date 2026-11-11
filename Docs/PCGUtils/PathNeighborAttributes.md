# Path | Neighbor Attributes

Under **PCGUtils > Paths**. Copies attribute values from neighboring points along each input path onto every point: the next point (Offset `+1`), the previous one (`-1`), or any offset. The output is the input with attributes added; every other attribute, `@Data`, tag and point property is kept.

## Settings

| Setting | Meaning |
| --- | --- |
| **Attributes** | One entry per read. Each has a **Source** (any attribute or point property, any type, one value per point), an **Offset**, and an **Output Attribute Name** (same type as Source; may equal Source to shift in place). Optional **Output Has Neighbor** writes a bool (**Has Neighbor Attribute Name**) that is false only where an open path ran out of points. Entries run in order, so a later one can read an earlier one's output. |
| **Closed Mode** | `From Attribute` reads the Data-domain bool **Closed Attribute Name** (default `IsClosed`, the PCGEx convention; missing means open, another type is an error). `Open` / `Closed` force it. Closed paths wrap around. |
| **End Mode** | For open paths, what an end point reads when its neighbor does not exist. `Own Value` (default): the point's own value. `Clamp`: the nearest existing point (the same as Own Value for `+1`/`-1`). `Attribute Default`: the source attribute's default value, or zero for a property. |

## Example: split a path wherever an attribute changes

Each point carries a soft object path `BarrierGeneration`. To cut the path into pieces that each share one value:

1. **Path | Neighbor Attributes**: Source `BarrierGeneration`, Offset `1`, Output `NextBarrierGeneration`. Keep End Mode `Own Value`, so an open path's last point compares equal to itself.
2. PCGEx **Path : Split** with a string comparison filter `BarrierGeneration != NextBarrierGeneration`.

On a closed path the last point compares against the first, so a change across the seam also splits.

The filter fires on the **last point of each run** (the change lies between it and the next point). If the split mode duplicates the split point into both pieces, the next piece starts with one point of the old value. Use Offset `-1` (a `Previous` attribute) instead to fire on the first point of each new run, whichever end suits the split mode.
