param(
	[string]$LogPath = "D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl",
	[int]$Present = -1,
	[int]$Top = 24
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-JsonLine {
	param([string]$Line)
	try {
		return $Line | ConvertFrom-Json -ErrorAction Stop
	} catch {
		return $null
	}
}

function Get-Prop {
	param($Object, [string]$Name, $Default = 0)
	if ($null -eq $Object) {
		return $Default
	}
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property) {
		return $Default
	}
	return $property.Value
}

function Format-Time {
	param([double]$Us)
	return ("{0,10:N3} ms / {1,10:N0} us" -f ($Us / 1000.0), $Us)
}

function Write-Bar {
	param([string]$Name, [double]$Percent, [double]$Us)
	$barWidth = 36
	$filled = [Math]::Min($barWidth, [Math]::Max(0, [int][Math]::Round($Percent * $barWidth / 100.0)))
	$bar = ("#" * $filled).PadRight($barWidth, ".")
	"{0,-28} {1,7:N2}% {2} {3}" -f $Name, $Percent, $bar, (Format-Time $Us)
}

if (!(Test-Path -LiteralPath $LogPath)) {
	throw "Log file not found: $LogPath"
}

$selectedTimeline = $null
$selectedHotPath = @()
$selectedMaintenance = @()
$pendingHotPath = @()
$pendingMaintenance = @()

foreach ($line in Get-Content -LiteralPath $LogPath) {
	$item = Read-JsonLine $line
	if ($null -eq $item) {
		continue
	}

	$func = [string](Get-Prop $item "func" "")
	if ($func -eq "DX12HotPathTiming") {
		$pendingHotPath += $item
		continue
	}

	if ($func -eq "DX12ResidentMaintenance") {
		$pendingMaintenance += $item
		continue
	}

	if ($func -eq "DX12FrameTimeline") {
		$presentId = [int](Get-Prop $item "present" -1)
		if ($Present -lt 0 -or $presentId -eq $Present) {
			$selectedTimeline = $item
			$selectedHotPath = $pendingHotPath
			$selectedMaintenance = $pendingMaintenance
		}
		$pendingHotPath = @()
		$pendingMaintenance = @()
	}
}

if ($null -eq $selectedTimeline) {
	throw "No DX12FrameTimeline entry found for Present=$Present"
}

$present = [int](Get-Prop $selectedTimeline "present" -1)
$frameWallUs = [double](Get-Prop $selectedTimeline "frameWallUs" 0)
$frameIntervalUs = [double](Get-Prop $selectedTimeline "frameIntervalUs" 0)
$presentWallUs = [double](Get-Prop $selectedTimeline "presentWallUs" 0)
$presentOriginalUs = [double](Get-Prop $selectedTimeline "presentOriginalUs" 0)
$outsidePresentUs = [double](Get-Prop $selectedTimeline "outsidePresentUs" ($frameWallUs - $presentWallUs))
$queueEstimatedTotalUs = [double](Get-Prop $selectedTimeline "queueEstimatedTotalUs" 0)
$workerTaskUs = [double](Get-Prop $selectedTimeline "workerTaskUs" 0)
$workerDrainWaitUs = [double](Get-Prop $selectedTimeline "workerDrainWaitUs" 0)
$knownHotPathUs = 0.0

foreach ($entry in $selectedHotPath) {
	$knownHotPathUs += [double](Get-Prop $entry "totalUs" 0)
}

$fps = 0.0
if ($frameIntervalUs -gt 0.0) {
	$fps = 1000000.0 / $frameIntervalUs
}

"DX12 Frame Report"
"Present:              $present"
"Frame wall:           $(Format-Time $frameWallUs)"
"Frame interval:       $(Format-Time $frameIntervalUs)"
"Expected FPS:         {0:N2}" -f $fps
"Present wall:         $(Format-Time $presentWallUs)"
"Present original:     $(Format-Time $presentOriginalUs)"
"Outside Present:      $(Format-Time $outsidePresentUs)"
"ExecuteCommandLists:  $(Format-Time $queueEstimatedTotalUs)"
"Worker task:          $(Format-Time $workerTaskUs)"
"Worker drain wait:    $(Format-Time $workerDrainWaitUs)"
"Known hot path sum:   $(Format-Time $knownHotPathUs)"
""
"Function Cost Chart"

$sortedHotPath = $selectedHotPath | Sort-Object { [double](Get-Prop $_ "totalUs" 0) } -Descending
$count = 0
foreach ($entry in $sortedHotPath) {
	if ($count -ge $Top) {
		break
	}
	$totalUs = [double](Get-Prop $entry "totalUs" 0)
	if ($totalUs -le 0.0) {
		continue
	}
	$percent = 0.0
	if ($frameWallUs -gt 0.0) {
		$percent = $totalUs * 100.0 / $frameWallUs
	}
	Write-Bar ([string](Get-Prop $entry "stage" "")) $percent $totalUs
	$count++
}

""
"Timeline Buckets"
$timelineRows = @(
	@("OutsidePresent", $outsidePresentUs),
	@("PresentOriginal", $presentOriginalUs),
	@("ExecuteCommandLists", $queueEstimatedTotalUs),
	@("WorkerTask", $workerTaskUs),
	@("WorkerDrainWait", $workerDrainWaitUs)
)
foreach ($row in $timelineRows) {
	$name = [string]$row[0]
	$us = [double]$row[1]
	$percent = 0.0
	if ($frameWallUs -gt 0.0) {
		$percent = $us * 100.0 / $frameWallUs
	}
	Write-Bar $name $percent $us
}

""
"Cache And Maintenance"
foreach ($entry in $selectedMaintenance) {
	$category = [string](Get-Prop $entry "category" "")
	if ($category.Length -eq 0) {
		continue
	}
	$parts = @()
	foreach ($property in $entry.PSObject.Properties) {
		if ($property.Name -eq "func" -or $property.Name -eq "category" -or
				$property.Name -eq "time" -or $property.Name -eq "tid") {
			continue
		}
		$parts += ("{0}={1}" -f $property.Name, $property.Value)
	}
	"{0}: {1}" -f $category, ($parts -join ", ")
}
