param(
    [string]$Path = 'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl',
    [int]$FrameCount = 120,
    [int]$SkipLastFrames = 0,
    [long]$MaxFrameIntervalUs = 0
)

class Dx12FrameSample {
    [long]$FrameIntervalUs = 0
    [long]$PresentWaitUs = 0
    [long]$ModBeginFrameUs = 0
    [hashtable]$StageTotals = @{}
    [hashtable]$CpuStageTotals = @{}
    [hashtable]$CpuStageCalls = @{}
    [hashtable]$ApiStageTotals = @{}
    [hashtable]$ApiStageCalls = @{}
}

class Dx12LogAnalysisUtils {
    static [long] ReadInteger([string]$Payload, [string]$Name) {
        $match = [regex]::Match($Payload, '"' + [regex]::Escape($Name) + '":(-?\d+)')
        if (!$match.Success) {
            return 0
        }
        return [long]$match.Groups[1].Value
    }

    static [string] ReadString([string]$Payload, [string]$Name) {
        $match = [regex]::Match($Payload, '"' + [regex]::Escape($Name) + '":"([^"]*)"')
        if (!$match.Success) {
            return ''
        }
        return $match.Groups[1].Value
    }

    static [string] MakeBar([double]$Percentage, [int]$Width) {
        $clamped = [math]::Max(0.0, [math]::Min(100.0, $Percentage))
        $filled = [int][math]::Round($clamped * $Width / 100.0)
        return ('#' * $filled) + ('.' * ($Width - $filled))
    }

    static [void] Analyze([string]$LogPath, [int]$RequestedFrames, [int]$SkippedLastFrames, [long]$MaximumFrameIntervalUs) {
        if (!(Test-Path -LiteralPath $LogPath)) {
            throw "DX12 log not found: $LogPath"
        }

        $frames = [System.Collections.Generic.List[Dx12FrameSample]]::new()
        [Dx12FrameSample]$currentFrame = $null
        [long]$pendingPresentWaitUs = 0

        $fileShare = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
        $stream = [System.IO.FileStream]::new(
            $LogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $fileShare)
        $reader = [System.IO.StreamReader]::new($stream)
        try {
        while ($null -ne ($line = $reader.ReadLine())) {
            $parts = $line -split "`t", 3
            if ($parts.Count -ne 3) {
                continue
            }

            $category = $parts[1]
            $payload = $parts[2]
            if ($category -eq 'DX12Perf') {
                $scope = [Dx12LogAnalysisUtils]::ReadString($payload, 'scope')
                if ($scope -eq 'PresentWait') {
                    $pendingPresentWaitUs = [Dx12LogAnalysisUtils]::ReadInteger($payload, 'us')
                    continue
                }
                if ($scope -eq 'ModBeginFrame') {
                    $currentFrame = [Dx12FrameSample]::new()
                    $currentFrame.FrameIntervalUs = [Dx12LogAnalysisUtils]::ReadInteger($payload, 'frameIntervalUs')
                    $currentFrame.ModBeginFrameUs = [Dx12LogAnalysisUtils]::ReadInteger($payload, 'us')
                    $currentFrame.PresentWaitUs = $pendingPresentWaitUs
                    $frames.Add($currentFrame)
                    $pendingPresentWaitUs = 0
                }
                continue
            }

            if ($category -eq 'DX12HotPathTiming' -and $null -ne $currentFrame) {
                $stage = [Dx12LogAnalysisUtils]::ReadString($payload, 'stage')
                if ($stage.Length -eq 0) {
                    continue
                }
                $currentFrame.StageTotals[$stage] = [Dx12LogAnalysisUtils]::ReadInteger($payload, 'totalUs')
                continue
            }

            if ($category -eq 'DX12CpuStageTiming' -and $null -ne $currentFrame) {
                $component = [Dx12LogAnalysisUtils]::ReadString($payload, 'category')
                $stage = [Dx12LogAnalysisUtils]::ReadString($payload, 'stage')
                if ($component.Length -eq 0 -or $stage.Length -eq 0) {
                    continue
                }
                $key = $component + '/' + $stage
                if (!$currentFrame.CpuStageTotals.ContainsKey($key)) {
                    $currentFrame.CpuStageTotals[$key] = [long]0
                }
                $currentFrame.CpuStageTotals[$key] =
                    [long]$currentFrame.CpuStageTotals[$key] +
                    [Dx12LogAnalysisUtils]::ReadInteger($payload, 'totalUs')
                if (!$currentFrame.CpuStageCalls.ContainsKey($key)) {
                    $currentFrame.CpuStageCalls[$key] = [long]0
                }
                $currentFrame.CpuStageCalls[$key] =
                    [long]$currentFrame.CpuStageCalls[$key] +
                    [Dx12LogAnalysisUtils]::ReadInteger($payload, 'calls')
                continue
            }

            if ($category -eq 'DX12CreateTiming' -and $null -ne $currentFrame) {
                $api = [Dx12LogAnalysisUtils]::ReadString($payload, 'api')
                $stage = [Dx12LogAnalysisUtils]::ReadString($payload, 'stage')
                if ($api.Length -eq 0 -or $stage.Length -eq 0) {
                    continue
                }
                $key = $api + '/' + $stage
                if (!$currentFrame.ApiStageTotals.ContainsKey($key)) {
                    $currentFrame.ApiStageTotals[$key] = [long]0
                    $currentFrame.ApiStageCalls[$key] = [long]0
                }
                $currentFrame.ApiStageTotals[$key] =
                    [long]$currentFrame.ApiStageTotals[$key] +
                    [Dx12LogAnalysisUtils]::ReadInteger($payload, 'totalUs')
                $currentFrame.ApiStageCalls[$key] =
                    [long]$currentFrame.ApiStageCalls[$key] +
                    [Dx12LogAnalysisUtils]::ReadInteger($payload, 'calls')
            }
        }
        } finally {
            $reader.Dispose()
            $stream.Dispose()
        }

        $eligibleFrames = @($frames | Where-Object {
            $_.FrameIntervalUs -gt 0 -and
                ($MaximumFrameIntervalUs -le 0 -or $_.FrameIntervalUs -le $MaximumFrameIntervalUs)
        })
        $takeCount = [math]::Min($RequestedFrames, [math]::Max(0, $eligibleFrames.Count - $SkippedLastFrames))
        $startIndex = $eligibleFrames.Count - $SkippedLastFrames - $takeCount
        $validFrames = if ($takeCount -gt 0) {
            @($eligibleFrames[$startIndex..($startIndex + $takeCount - 1)])
        } else {
            @()
        }
        if ($validFrames.Count -eq 0) {
            throw 'No valid frame timing samples were found in the requested interval range.'
        }

        [long]$aggregateFrameUs = 0
        [long]$aggregatePresentWaitUs = 0
        [long]$aggregateModBeginFrameUs = 0
        $stageTotals = @{}
        $cpuStageTotals = @{}
        $cpuStageCalls = @{}
        $apiStageTotals = @{}
        $apiStageCalls = @{}
        foreach ($frame in $validFrames) {
            $aggregateFrameUs += $frame.FrameIntervalUs
            $aggregatePresentWaitUs += $frame.PresentWaitUs
            $aggregateModBeginFrameUs += $frame.ModBeginFrameUs
            foreach ($stage in $frame.StageTotals.Keys) {
                if (!$stageTotals.ContainsKey($stage)) {
                    $stageTotals[$stage] = [long]0
                }
                $stageTotals[$stage] = [long]$stageTotals[$stage] + [long]$frame.StageTotals[$stage]
            }
            foreach ($stage in $frame.CpuStageTotals.Keys) {
                if (!$cpuStageTotals.ContainsKey($stage)) {
                    $cpuStageTotals[$stage] = [long]0
                }
                $cpuStageTotals[$stage] =
                    [long]$cpuStageTotals[$stage] + [long]$frame.CpuStageTotals[$stage]
            }
            foreach ($stage in $frame.CpuStageCalls.Keys) {
                if (!$cpuStageCalls.ContainsKey($stage)) {
                    $cpuStageCalls[$stage] = [long]0
                }
                $cpuStageCalls[$stage] =
                    [long]$cpuStageCalls[$stage] + [long]$frame.CpuStageCalls[$stage]
            }
            foreach ($stage in $frame.ApiStageTotals.Keys) {
                if (!$apiStageTotals.ContainsKey($stage)) {
                    $apiStageTotals[$stage] = [long]0
                    $apiStageCalls[$stage] = [long]0
                }
                $apiStageTotals[$stage] =
                    [long]$apiStageTotals[$stage] + [long]$frame.ApiStageTotals[$stage]
                $apiStageCalls[$stage] =
                    [long]$apiStageCalls[$stage] + [long]$frame.ApiStageCalls[$stage]
            }
        }

        $averageFrameUs = [double]$aggregateFrameUs / $validFrames.Count
        $averageFrameMs = $averageFrameUs / 1000.0
        $averagePresentWaitUs = [double]$aggregatePresentWaitUs / $validFrames.Count
        $averageModBeginFrameUs = [double]$aggregateModBeginFrameUs / $validFrames.Count
        $estimatedFps = 1000000.0 / $averageFrameUs

        [Console]::WriteLine(('DX12 frame report: {0}' -f $LogPath))
        $intervals = @($validFrames | ForEach-Object { $_.FrameIntervalUs } | Sort-Object)
        $percentile = {
            param([double]$p)
            $index = [math]::Min($intervals.Count - 1,
                [math]::Max(0, [int][math]::Ceiling($p * $intervals.Count) - 1))
            return [long]$intervals[$index]
        }

        $intervalDescription = if ($MaximumFrameIntervalUs -gt 0) {
            'interval <= {0} us' -f $MaximumFrameIntervalUs
        } else {
            'no upper interval filter'
        }
        [Console]::WriteLine(('Samples: {0} (skip last {1} valid frames, {2})' -f $validFrames.Count, $SkippedLastFrames, $intervalDescription))
        [Console]::WriteLine(('Frame total: {0:N3} ms ({1:N0} us)' -f $averageFrameMs, $averageFrameUs))
        [Console]::WriteLine(('Frame interval: {0:N3} ms ({1:N0} us)' -f $averageFrameMs, $averageFrameUs))
        [Console]::WriteLine(('Present wait: {0:N3} ms ({1:N0} us), {2:N2}%' -f ($averagePresentWaitUs / 1000.0), $averagePresentWaitUs, (100.0 * $averagePresentWaitUs / $averageFrameUs)))
        [Console]::WriteLine(('Mod begin frame: {0:N3} ms ({1:N0} us), {2:N2}%' -f ($averageModBeginFrameUs / 1000.0), $averageModBeginFrameUs, (100.0 * $averageModBeginFrameUs / $averageFrameUs)))
        [Console]::WriteLine(('Expected FPS: {0:N2}' -f $estimatedFps))
        [Console]::WriteLine(('Distribution: min {0:N0} us | P50 {1:N0} us | P90 {2:N0} us | P95 {3:N0} us | P99 {4:N0} us | max {5:N0} us' -f
            $intervals[0], (& $percentile 0.50), (& $percentile 0.90),
            (& $percentile 0.95), (& $percentile 0.99), $intervals[-1]))
        [Console]::WriteLine('')
        [Console]::WriteLine('Inclusive hot-path totals (nested stages can overlap):')
        [Console]::WriteLine(('{0,-28} {1,12} {2,12} {3,9}  {4}' -f 'Stage', 'Total us', 'Avg us/frame', 'Frame %', 'Chart'))

        $sortedStages = $stageTotals.GetEnumerator() | Sort-Object Value -Descending
        foreach ($entry in $sortedStages) {
            $averageStageUs = [double]$entry.Value / $validFrames.Count
            $percentage = 100.0 * $averageStageUs / $averageFrameUs
            $bar = [Dx12LogAnalysisUtils]::MakeBar($percentage, 24)
            [Console]::WriteLine(('{0,-28} {1,12:N0} {2,12:N1} {3,8:N2}%  {4}' -f $entry.Key, $entry.Value, $averageStageUs, $percentage, $bar))
        }

        [Console]::WriteLine('')
        [Console]::WriteLine('CPU sub-stage totals:')
        [Console]::WriteLine(('{0,-48} {1,10} {2,12} {3,12} {4,9}  {5}' -f 'Component/Stage', 'Calls', 'Avg us/call', 'Avg us/frame', 'Frame %', 'Chart'))
        $sortedCpuStages = $cpuStageTotals.GetEnumerator() | Sort-Object Value -Descending
        foreach ($entry in $sortedCpuStages) {
            $averageStageUs = [double]$entry.Value / $validFrames.Count
            $calls = if ($cpuStageCalls.ContainsKey($entry.Key)) { [long]$cpuStageCalls[$entry.Key] } else { 0 }
            $averageCallUs = if ($calls -gt 0) { [double]$entry.Value / $calls } else { 0.0 }
            $percentage = 100.0 * $averageStageUs / $averageFrameUs
            $bar = [Dx12LogAnalysisUtils]::MakeBar($percentage, 24)
            [Console]::WriteLine(('{0,-48} {1,10:N0} {2,12:N2} {3,12:N1} {4,8:N2}%  {5}' -f $entry.Key, $calls, $averageCallUs, $averageStageUs, $percentage, $bar))
        }

        [Console]::WriteLine('')
        [Console]::WriteLine('Wrapper API stage totals:')
        [Console]::WriteLine(('{0,-48} {1,10} {2,12} {3,12} {4,9}  {5}' -f 'API/Stage', 'Calls', 'Avg us/call', 'Avg us/frame', 'Frame %', 'Chart'))
        $sortedApiStages = $apiStageTotals.GetEnumerator() | Sort-Object Value -Descending
        foreach ($entry in $sortedApiStages) {
            $averageStageUs = [double]$entry.Value / $validFrames.Count
            $calls = [long]$apiStageCalls[$entry.Key]
            $averageCallUs = if ($calls -gt 0) { [double]$entry.Value / $calls } else { 0.0 }
            $percentage = 100.0 * $averageStageUs / $averageFrameUs
            $bar = [Dx12LogAnalysisUtils]::MakeBar($percentage, 24)
            [Console]::WriteLine(('{0,-48} {1,10:N0} {2,12:N2} {3,12:N1} {4,8:N2}%  {5}' -f $entry.Key, $calls, $averageCallUs, $averageStageUs, $percentage, $bar))
        }
    }
}

[Dx12LogAnalysisUtils]::Analyze($Path, $FrameCount, $SkipLastFrames, $MaxFrameIntervalUs)
