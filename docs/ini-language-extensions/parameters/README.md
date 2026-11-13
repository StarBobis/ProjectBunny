# INI Runtime Parameters

INI expressions provide access to runtime parameters that are evaluated at the time the expression is executed.

## `FRAME_NUMBER`

Returns the number of the current frame.

The frame boundary is defined by the `Present` call.

## `DRAW_NUMBER`

Returns the number of the current draw call within the frame.

The counter is zero before the first draw and becomes `1` on that context's first draw. Calls skipped by overrides still count. Present resets the counters for the next frame.

## `DISPATCH_NUMBER`

Returns the number of the current compute dispatch within the frame.

The counter is zero before the first dispatch and becomes `1` on that context's first dispatch. It is independent of the draw counter; Present resets both. These are runtime context values and cannot initialize a static section option.

## `TIME`

Returns seconds since launch, sampled from a monotonic clock in microsecond units. The INI result is a float, so its numerical resolution becomes coarser as elapsed time grows; microsecond sampling does not guarantee microsecond float precision throughout a long session.

```ini
$run_time = TIME
```

## `FRAME_TIME`

Returns the last measured frame interval in seconds. Present updates it using the monotonic microsecond clock. The first interval starts at the initialized launch timestamp, rather than at the clock epoch. This remains a float-valued runtime measurement.

```ini
$frame_time = FRAME_TIME
```

## `FPS`

Returns the frame-rate estimate maintained by an exponential moving average. It starts at zero before the counter has a usable interval and remains a runtime value rather than an implicit static initializer. It is not the instantaneous reciprocal of every frame interval.

```ini
$fps = FPS
```
