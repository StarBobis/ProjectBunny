# Stream Output

The four stream-output buffer targets are `so0` through `so3`. Dynamic scalar indices such as `so[$slot]` are supported; SO slot ranges are not.

```ini
so0 = ref ResourceOutput
so0 = null
ResourceSaved = ref so0
so[$slot] = ref ResourceOutput->Region($byte_offset, $byte_count)
```

The native SO API uses the selected byte offset, but accepts no size. The Region byte count is validated by the command parser and does not limit emitted shader records. The allocation must fit the shader's actual output. See [Resource Regions](resource-regions.md).

The slots an assignment does not name are rebound with D3D11's append offset. They continue after their previous output instead of starting over. D3D11 does not expose their bound offsets: an SO source reports byte offset zero, and an SO source Region measures from the beginning of its buffer.

## Bind flags

SO requires `stream_output`. The local configuration graph propagates required bind flags through resource references and pool templates before allocation; this is not decided solely by first runtime use. Explicitly declare the intended combination when a buffer is also read through a view:

```ini
[ResourceOutput]
type = Buffer
bind_flags = stream_output shader_resource
```

Already-created native resources cannot acquire new bind flags. With frame analysis enabled, a reference source missing required flags is reported in the log.

## Shader limitation

`CreateGeometryShaderWithStreamOutput` forwards the native shader creation and does not register the shader in the normal hash/binding cache. Consequently hunting, `ShaderOverride`, and shader replacement do not apply to that shader. Its output buffers are accessible through the SO targets.

These examples specify bindings, not a complete shader/draw configuration. The actual geometry shader must have a compatible stream-output declaration and sufficient buffer capacity.
