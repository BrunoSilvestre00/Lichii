# Frame-time benchmarking

`frame_time_metrics.py` computes throughput from interval count divided by total elapsed time. It reports interpolated median, p95 and p99, maximum, the share of intervals at or below 1000/60 ms, and strict counts above 33.3, 50 and 100 ms. Percentiles describe the recorded event stream. They are not display latency measurements.

Every report requires `--domain`. Label guest frame submission, host presentation and physical display scanout separately. Disable frame generation when assessing real rendering throughput. A frame-generation counter or a stationary scene cannot establish sustained gameplay performance.

For complete JSON interval arrays, supply `intervals_ms` and an explicit interval count in `interval_count`, `frames`, or `summary.intervals`. All supplied counts must match. The debug server's historical `frame_times` response includes only the last 120 intervals even when it summarizes a longer window: this tool rejects that mismatch rather than calculating full-window percentiles from the tail. A `truncated` timing window is also rejected.

For a tab-separated recorder file with `frame` and `interval_ms` columns, select both bounds to exclude menus and loading and detect missing endpoints. The ending frame is exclusive. Comment lines beginning with `#` are supported.

```powershell
python tools/nx2/frame_time_metrics.py gameplay.tsv --domain "game present, frame generation off" --first-frame 3000 --end-frame 4200 --output run-1.json
python -m unittest discover -s tools/nx2 -p test_frame_time_metrics.py
```

Use an isolated profile copied from a hash-verified save checkpoint. Record executable/source identities, actual game build, effective runtime settings, resolution, aspect ratio and input script. Separate first visits from warmed runs. Repeat the same warmed route at least three times for each final candidate, resetting to the same checkpoint and camera pose. Include movement, camera rotation, jumping, water and effects where the save permits them. Record which sequences were visibly verified and reject dialogue or loading windows.

Run only one game runtime at a time. Stop builds and shader-preparation jobs before collecting gameplay measurements. Check for surviving child processes, available physical memory and commit headroom. A background preparation job can contaminate timings after its first worker exits by starting another preparation stage.

Collect thread CPU time as a percentage of one logical processor; total process CPU can conceal a saturated thread. Record GPU execution time separately from GPU utilization, and capture memory, clocks and pipeline/upload/wait events where available. Do not infer a causal optimization benefit when routes, cache warmth, measurement domains or timing methods differ. Report unavailable counters explicitly.
