param(
	[string]$Log = 'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt',
	[switch]$Watch
)

# One-shot log analyzer. Mirrors docs/log-analysis-guide.md.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/analyze-dx12-log.ps1
# Output: frame timeline, PreSkin hit rate, API wall clock top list,
#         create original/record split, hot-path per-call costs, and (-Watch)
#         the three metrics targeted by the 18:43 optimization build.

if (!(Test-Path $Log)) {
	Write-Host "Log not found: $Log" -ForegroundColor Red
	exit 1
}

function Median($values) {
	$sorted = @($values | Sort-Object)
	if ($sorted.Count -eq 0) { return 0 }
	return $sorted[[int]($sorted.Count / 2)]
}

Write-Host "===== $Log =====" -ForegroundColor Cyan

# 1) Frame timeline
$tl = rg "func=DX12FrameTimeline" $Log | ForEach-Object {
	[pscustomobject]@{
		Present=[long]([regex]::Match($_,'present=(\d+)').Groups[1].Value)
		Wall=[long]([regex]::Match($_,'frameWallUs=(\d+)').Groups[1].Value)
		Outside=[long]([regex]::Match($_,'outsidePresentUs=(\d+)').Groups[1].Value)
		PresentWall=[long]([regex]::Match($_,'presentWallUs=(\d+)').Groups[1].Value)
		Original=[long]([regex]::Match($_,'presentOriginalUs=(\d+)').Groups[1].Value)
		Queue=[long]([regex]::Match($_,'queueEstimatedTotalUs=(\d+)').Groups[1].Value)
	}
}
if ($tl.Count) {
	$wall = @($tl | ForEach-Object { $_.Wall })
	$med = Median $wall
	$fps = if ($med) { [math]::Round(1000000.0/$med,1) } else { 0 }
	Write-Host ("frames={0} medianFrame={1}us ({2}fps) presentMed={3}us queueMed={4}us outsidePresentMed={5}us" -f
		$tl.Count, $med, $fps,
		(Median @($tl | ForEach-Object { $_.PresentWall })),
		(Median @($tl | ForEach-Object { $_.Queue })),
		(Median @($tl | ForEach-Object { $_.Outside })))
}

# 2) PreSkin cache hit rate
$pk = rg "category=preskin_bind_cache" $Log | ForEach-Object {
	[pscustomobject]@{
		Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
		Hits=[long]([regex]::Match($_,'hits=(\d+)').Groups[1].Value)
	}
}
if ($pk.Count) {
	$c = ($pk | Measure-Object Calls -Sum).Sum
	$h = ($pk | Measure-Object Hits -Sum).Sum
	$rate = if ($c) { [math]::Round($h*100.0/$c,1) } else { 0 }
	Write-Host ("PreSkin cache: {0} frames, hit {1}% ({2}/{3})" -f $pk.Count, $rate, $h, $c)
}

# 3) API wall clock top list
$api = rg "func=DX12ApiWallTiming" $Log | ForEach-Object {
	[pscustomobject]@{
		Bucket=[regex]::Match($_,'bucket=([^,]+)').Groups[1].Value
		Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
		TotalUs=[long]([regex]::Match($_,'totalUs=(\d+)').Groups[1].Value)
		MaxUs=[long]([regex]::Match($_,'maxUs=(\d+)').Groups[1].Value)
	}
}
if ($api.Count) {
	Write-Host "`nAPI wall clock TOP15 (per frame):" -ForegroundColor Yellow
	$api | Group-Object Bucket | ForEach-Object {
		$r=$_.Group; $f=$r.Count
		[pscustomobject]@{
			Bucket=$_.Name
			CallsPerFrame=[math]::Round((($r|Measure-Object Calls -Sum).Sum)/$f,1)
			AvgUsPerFrame=[math]::Round((($r|Measure-Object TotalUs -Sum).Sum)/$f,1)
			MaxUs=($r|Measure-Object MaxUs -Maximum).Maximum
		}
	} | Sort-Object AvgUsPerFrame -Descending | Select-Object -First 15 |
		Format-Table -AutoSize
}

# 4) Create original/record split
$ct = rg "func=DX12CreateTiming" $Log | ForEach-Object {
	[pscustomobject]@{
		Api=[regex]::Match($_,'api=([^,]+)').Groups[1].Value
		Stage=[regex]::Match($_,'stage=([^,]+)').Groups[1].Value
		Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
		TotalUs=[long]([regex]::Match($_,'totalUs=(\d+)').Groups[1].Value)
	}
}
if ($ct.Count) {
	Write-Host "Create original/record split (us/call):" -ForegroundColor Yellow
	$ct | Group-Object Api,Stage | ForEach-Object {
		$r=$_.Group
		$c=($r|Measure-Object Calls -Sum).Sum
		[pscustomobject]@{
			Api=$_.Name.Split(',')[0]; Stage=$_.Name.Split(',')[1]
			UsPerCall=[math]::Round((($r|Measure-Object TotalUs -Sum).Sum)/$c,2)
			CallsPerFrame=[math]::Round($c/$r.Count,1)
		}
	} | Sort-Object Api,Stage | Format-Table -AutoSize
}

# 5) Hot-path per-call costs
$ht = rg "func=DX12HotPathTiming,stage=(PreSkinQuery|BindSnapshot|BindCollect|DrawHook|DispatchHook)" $Log |
	ForEach-Object {
		[pscustomobject]@{
			Stage=[regex]::Match($_,'stage=([^,]+)').Groups[1].Value
			Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
			TotalUs=[long]([regex]::Match($_,'totalUs=(\d+)').Groups[1].Value)
		}
	}
if ($ht.Count) {
	Write-Host "Hot-path per-call:" -ForegroundColor Yellow
	$ht | Group-Object Stage | ForEach-Object {
		$r=$_.Group; $c=($r|Measure-Object Calls -Sum).Sum
		[pscustomobject]@{
			Stage=$_.Name
			UsPerCall=[math]::Round((($r|Measure-Object TotalUs -Sum).Sum)/$c,2)
			CallsPerFrame=[math]::Round($c/$r.Count,1)
		}
	} | Format-Table -AutoSize
}

if ($Watch) {
	Write-Host "Optimization watch list (compare against pre-18:43 baseline):" -ForegroundColor Green
	$agg = @{}
	foreach ($row in $api) {
		if ($row.Bucket -ne 'ResetCommandList' -and
		    $row.Bucket -ne 'DeviceCopyDescriptors' -and
		    $row.Bucket -ne 'SetPipelineState') {
			continue
		}
		if (!$agg.ContainsKey($row.Bucket)) {
			$agg[$row.Bucket] = [pscustomobject]@{ Calls = [long]0; TotalUs = [long]0; Frames = 0 }
		}
		$agg[$row.Bucket].Calls += $row.Calls
		$agg[$row.Bucket].TotalUs += $row.TotalUs
		$agg[$row.Bucket].Frames++
	}
	if ($agg.Count -gt 0) {
		$agg.GetEnumerator() | ForEach-Object {
			$v = $_.Value
			[pscustomobject]@{
				Bucket = $_.Key
				AvgUsPerFrame = [math]::Round($v.TotalUs / $v.Frames, 1)
				CallsPerFrame = [math]::Round($v.Calls / $v.Frames, 1)
			}
		} | Sort-Object Bucket | Format-Table -AutoSize
	} else {
		Write-Host "(buckets not found in this log - different frame segment?)"
	}
}
