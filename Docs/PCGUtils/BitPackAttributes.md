# Param | Bit Pack Attributes

Packs several small numeric attributes into one number. The usual reason is a material: Per Instance Custom
Data gives each instance a few floats, and a tint index, a variant id, a wear level and a flag can all fit in
one of them instead of taking four.

## Pins

| Pin | Type | Notes |
| --- | --- | --- |
| In | Point or Attribute Set | Data to pack. Every input must carry every attribute in the list. |
| Out | Point or Attribute Set | One output per input: the input, duplicated, with the packed attribute added. All other attributes (all domains), tags, point properties and the target actor are kept. |

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Output Attribute Name | `BitPacked` | Required. Attribute the packed value is written to. Created if missing, replaced if it exists with another type. `@Data.Name` writes one value for the whole data. Overridable. |
| Output Type | Float | Float (24 bits exact - use this for Per Instance Custom Data), Double (53), Integer 32 (32), Integer 64 (64). With an integer type and every bit used, a set top bit reads as a negative number; the bit pattern is still right. Overridable. |
| Attributes To Pack | empty | The fields, **lowest bits first**. Each has an Attribute Name (required; numeric or Boolean; `$Density` and `@Data.Name` work), a Bit Count (1-64) and a Value Mode. |
| Warn On Clamped Values (advanced) | on | Warn when a value did not fit its field. |

Value modes:

- **Integer** rounds to the nearest whole number and clamps to `0 .. 2^Bits - 1` (4 bits: 0-15).
- **Normalized** clamps to `0..1` and scales to `0 .. 2^Bits - 1` (8 bits: 0.5 becomes 128).

Out-of-range values (and NaN, which packs as 0) are clamped and reported in one warning per field, naming the
input, the field, how many values were clamped and the first one. The packed attribute never interpolates,
so later PCG nodes that blend metadata cannot produce meaningless in-between numbers.

## Layout and unpacking

Entry 0 sits in the lowest bits, each next entry directly above. With entries of 4, 4, 1 bits: entry 0 is bits
0-3, entry 1 bits 4-7, entry 2 bit 8. A field's **Offset** is the sum of the bit counts before it.

In a material, for a Float output read from Per Instance Custom Data:

```
Field = floor(Packed / exp2(Offset)) % exp2(Bits)        // Integer mode: the whole value
Field = (floor(Packed / exp2(Offset)) % exp2(Bits)) / (exp2(Bits) - 1)   // Normalized mode: back to 0..1
```

`%` here is `fmod`. Keep the total at 24 bits or fewer for a float; above that the float can no longer hold
every whole number and low fields get corrupted. The node refuses a total larger than the Output Type holds.

## Errors

Each problem is reported on its own and names exactly what failed: an empty Output Attribute Name, an empty
list, an entry with no name or a bit count outside 1-64, a bit total above the Output Type's limit, an
attribute missing from an input, an attribute of an unsupported type, or a value count that matches neither the
output nor a single value. An input that fails is skipped (no output for it).

## Tests

`PCGUtils.Metadata.BitPack.*` - quantization, packing with a data-domain field and preservation of the
input, a full 32-bit Integer 32 pattern on an attribute set, and each error and the clamp warning.

# Multi Step (Step node)

The Step node (`Smooth Step` / `Step`) has a third operation, **Multi Step**. It takes In, Min and Max like
Smooth Step, plus a **Step Count** setting (default 5, overridable). Instead of easing from 0 to 1 between Min
and Max, it climbs in equal steps:

```
Alpha  = saturate((In - Min) / (Max - Min))
Result = floor(Alpha * StepCount) / StepCount
```

With Step Count 5 the result is 0, 0.2, 0.4, 0.6, 0.8 across the range and 1 at or above Max. Step Count 1 is
a hard step at Max. Vectors are stepped per component; integer inputs are promoted to double as with the other
operations. Test: `PCGUtils.Metadata.Step.MultiStep`.
