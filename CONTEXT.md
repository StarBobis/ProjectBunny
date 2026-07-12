# ProjectBunny DX12 Mod Runtime

This context models the minimum DX12 state required to match and apply Mods without retaining unrelated rendering history.

## Language

**Descriptor State**:
The latest known descriptor value for each written slot in a DX12 descriptor heap.
_Avoid_: Descriptor ledger, descriptor event log, shadow cache

**Descriptor Value**:
An immutable compact description of one CBV, SRV, UAV, RTV, DSV, or sampler value.
_Avoid_: Descriptor record, cached descriptor

**Resource Identity**:
A stable resource identifier plus generation that distinguishes reused COM pointer addresses.
_Avoid_: Resource pointer identity

**Binding State**:
The currently selected PSO, root signatures, descriptor heaps, root tables, root descriptors, constants, and IA bindings for one command list.
_Avoid_: Binding ledger, binding event history

**Observation Plan**:
The exact descriptor registers, resource hashes, and IA bindings that a compiled Mod decision may inspect.
_Avoid_: Full descriptor scan, generic descriptor tracking

**Descriptor History**:
Frame-analysis-only descriptor changes captured after history collection is enabled.
_Avoid_: Descriptor State

## Relationships

- A descriptor heap owns zero or more current **Descriptor States**
- A **Descriptor State** references exactly one **Descriptor Value**
- A resource-backed **Descriptor Value** references one **Resource Identity**
- A command list owns one current **Binding State**
- An **Observation Plan** resolves only the **Descriptor States** reachable from its required **Binding State** registers
- **Descriptor History** is optional and is not required by normal Mod matching

## Example dialogue

> **Dev:** "Do we retain every CreateConstantBufferView call for the Mod?"
> **Domain expert:** "No. Normal Mod matching needs only the current Descriptor State for slots named by the Observation Plan; Descriptor History belongs to frame analysis."

## Flagged ambiguities

- "descriptor tracking" previously meant both current **Descriptor State** and **Descriptor History**; these are now distinct concepts.
- "ledger" previously referred to both authoritative state and a fallback handle map; authoritative current state is **Descriptor State**.
