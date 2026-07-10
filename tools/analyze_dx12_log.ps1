param(
    [string]$Path = 'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl',
    [int]$FrameCount = 120,
    [long]$MaxFrameIntervalUs = 100000
)

class Dx12FrameSample {
    [long]$FrameIntervalUs = 0
    [long]$PresentWaitUs = 0
    [long]$ModBeginFrameUs = 0
    [hashtable]$StageTotals = @{}
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

    static [void] Analyze([string]$LogPath, [int]$RequestedFrames, [long]$MaximumFrameIntervalUs) {
        if (!(Test-Path -LiteralPath $LogPath)) {
            throw "DX12 log not found: $LogPath"
        }

        $frames = [System.Collections.Generic.List[Dx12FrameSample]]::new()
        [Dx12FrameSample]$currentFrame = $null
        [long]$pendingPresentWaitUs = 0

        foreach ($line in [System.IO.File]::ReadLines($LogPath)) {
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
            }
        }

        $validFrames = @($frames | Where-Object {
            $_.FrameIntervalUs -gt 0 -and $_.FrameIntervalUs -le $MaximumFrameIntervalUs
        } | Select-Object -Last $RequestedFrames)
        if ($validFrames.Count -eq 0) {
            throw 'No valid frame timing samples were found in the requested interval range.'
        }

        [long]$aggregateFrameUs = 0
        [long]$aggregatePresentWaitUs = 0
        [long]$aggregateModBeginFrameUs = 0
        $stageTotals = @{}
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
        }

        $averageFrameUs = [double]$aggregateFrameUs / $validFrames.Count
        $averageFrameMs = $averageFrameUs / 1000.0
        $averagePresentWaitUs = [double]$aggregatePresentWaitUs / $validFrames.Count
        $averageModBeginFrameUs = [double]$aggregateModBeginFrameUs / $validFrames.Count
        $estimatedFps = 1000000.0 / $averageFrameUs

        [Console]::WriteLine(('DX12 frame report: {0}' -f $LogPath))
        [Console]::WriteLine(('Samples: {0} (last valid frames, interval <= {1} us)' -f $validFrames.Count, $MaximumFrameIntervalUs))
        [Console]::WriteLine(('Frame total: {0:N3} ms ({1:N0} us)' -f $averageFrameMs, $averageFrameUs))
        [Console]::WriteLine(('Frame interval: {0:N3} ms ({1:N0} us)' -f $averageFrameMs, $averageFrameUs))
        [Console]::WriteLine(('Present wait: {0:N3} ms ({1:N0} us), {2:N2}%' -f ($averagePresentWaitUs / 1000.0), $averagePresentWaitUs, (100.0 * $averagePresentWaitUs / $averageFrameUs)))
        [Console]::WriteLine(('Mod begin frame: {0:N3} ms ({1:N0} us), {2:N2}%' -f ($averageModBeginFrameUs / 1000.0), $averageModBeginFrameUs, (100.0 * $averageModBeginFrameUs / $averageFrameUs)))
        [Console]::WriteLine(('Expected FPS: {0:N2}' -f $estimatedFps))
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
    }
}

[Dx12LogAnalysisUtils]::Analyze($Path, $FrameCount, $MaxFrameIntervalUs)
