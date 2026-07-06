# Log Format Simplification Plan

## Objective
Replace the JSON-like key-value format in runtime log (`d3d12_log.jsonl`) with a flat `key=value,` format to eliminate JSON formatting overhead (currently 18,195 us/frame = 62% of log cost).

## Background
- Current log format: `timestamp\tCategory\t"key1":value1,"key2":value2,...` 
- Log cost per frame (Frame 2866): 29,137 us total
  - `formatUs`: 18,195 us (JSON string formatting)
  - `enqueueUs`: 10,942 us (queue enqueue)
  - `debugJsonCalls`: 2,228 calls per frame
- The JSON-like format requires wrapping in braces, quoting keys, handling commas - all pure overhead.

## Target Format
```
timestamp\tCategory\tkey1=value1,key2=value2
```

## Design Principles
1. **FrameAnalysisLog stays JSONL** - Frame dump/analysis content is machine-read by external tools, must remain valid JSONL
2. **Runtime log switches to flat key=value** - Human-readable and grep-friendly, no JSON serialization overhead
3. **One line at a time** - Change each log call site individually, no big-bang refactor
4. **Keep the tab-separated structure** - `timestamp\tCategory\tmessage` format stays, only the message payload changes

## Implementation Steps

### Phase 1: Audit current log call sites
- Identify all log categories and their message formats
- Find DX12LogManager and understand the logging pipeline (formatJson, formatText, debugJson, enqueue)
- Distinguish runtime log call sites from FrameAnalysisLog call sites

### Phase 2: Add new format primitive
- Add a `key=value` format method to DX12LogManager
- Avoid sprintf/stringstream - use simple char buffer or direct file write

### Phase 3: Convert call sites one by one
- Start with highest-frequency categories (debugJsonCalls = 2,228/frame)
- Each conversion removes JSON quoting and brace wrapping

### Phase 4: Update analysis tools
- `analyze_frame_log.py` and `breakdown_frame.py` need to parse the new format
- Keep backward compatibility with old format during transition

## Expected Impact
- `formatUs`: 18,195 us → estimated ~2,000 us (90% reduction)
- `debugJsonCalls`: 2,228 → 0 (all converted to text)
- Total log cost: 29,137 us → estimated ~11,000 us (62% reduction)
- Frame wall reduction: ~18ms (6.8% of 264ms frame)

## Scope Boundaries
- DO NOT touch: FrameAnalysisLog (frame dump system, needs JSONL)
- DO NOT touch: Log enqueue threading model
- ONLY change: The message format string generation
