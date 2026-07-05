param(
	[string]$LogPath = "D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl",
	[int]$Present = -1,
	[int]$Top = 24
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-TsvValue {
	param([string]$Value)
	if ($null -eq $Value) {
		return ""
	}
	$trimmed = $Value.Trim()
	if ($trimmed.Length -ge 2 -and $trimmed[0] -eq '"' -and
			$trimmed[$trimmed.Length - 1] -eq '"') {
		return $trimmed.Substring(1, $trimmed.Length - 2)
	}
	if ($trimmed -eq "true") {
		return $true
	}
	if ($trimmed -eq "false") {
		return $false
	}
	$intValue = 0L
	if ([Int64]::TryParse($trimmed, [ref]$intValue)) {
		return $intValue
	}
	$doubleValue = 0.0
	if ([Double]::TryParse($trimmed, [ref]$doubleValue)) {
		return $doubleValue
	}
	return $trimmed
}

function Read-LogLine {
	param([string]$Line)
	if ([string]::IsNullOrWhiteSpace($Line)) {
		return $null
	}

	$tabs = $Line -split "`t", 3
	if ($tabs.Count -ge 2) {
		$item = [ordered]@{}
		$item.time = $tabs[0]
		$item.func = $tabs[1]
		if ($tabs.Count -ge 3 -and ![string]::IsNullOrWhiteSpace($tabs[2])) {
			$extra = $tabs[2].Trim()
			if ($extra.StartsWith("{")) {
				try {
					$json = $extra | ConvertFrom-Json -ErrorAction Stop
					foreach ($property in $json.PSObject.Properties) {
						$item[$property.Name] = $property.Value
					}
				} catch {
				}
			} else {
				$fragment = $extra
				if ($fragment.StartsWith('"')) {
					try {
						$json = "{${fragment}}" | ConvertFrom-Json -ErrorAction Stop
						foreach ($property in $json.PSObject.Properties) {
							$item[$property.Name] = $property.Value
						}
					} catch {
						foreach ($part in ($extra -split "`t")) {
							$pair = $part -split "=", 2
							if ($pair.Count -eq 2) {
								$item[$pair[0]] = Convert-TsvValue $pair[1]
							}
						}
					}
				} else {
					foreach ($part in ($extra -split "`t")) {
						$pair = $part -split "=", 2
						if ($pair.Count -eq 2) {
							$item[$pair[0]] = Convert-TsvValue $pair[1]
						}
					}
				}
			}
		}
		return [pscustomobject]$item
	}

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
	"{0,-34} {1,7:N2}% {2} {3}" -f $Name, $Percent, $bar, (Format-Time $Us)
}

function Add-FrameItems {
	param([object[]]$Rows, $Item)
	return @($Rows + $Item)
}

if (!(Test-Path -LiteralPath $LogPath)) {
	throw "Log file not found: $LogPath"
}

$selectedTimeline = $null
$selectedHotPath = @()
$selectedMaintenance = @()
$selectedApiWall = @()
$selectedCpuStage = @()
$selectedCoverage = @()
$selectedLogCost = @()
$pendingHotPath = @()
$pendingMaintenance = @()
$pendingApiWall = @()
$pendingCpuStage = @()
$pendingCoverage = @()
$pendingLogCost = @()

foreach ($line in Get-Content -LiteralPath $LogPath) {
	$item = Read-LogLine $line
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
	if ($func -eq "DX12ApiWallTiming") {
		$pendingApiWall += $item
		continue
	}
	if ($func -eq "DX12CpuStageTiming") {
		$pendingCpuStage += $item
		continue
	}
	if ($func -eq "DX12CpuTimelineCoverage") {
		$pendingCoverage += $item
		continue
	}
	if ($func -eq "DX12LogCost") {
		$pendingLogCost += $item
		continue
	}
	if ($func -eq "DX12FrameTimeline") {
		$presentId = [int](Get-Prop $item "present" -1)
		if ($Present -lt 0 -or $presentId -eq $Present) {
			$selectedTimeline = $item
			$selectedHotPath = $pendingHotPath
			$selectedMaintenance = $pendingMaintenance
			$selectedApiWall = $pendingApiWall
			$selectedCpuStage = $pendingCpuStage
			$selectedCoverage = $pendingCoverage
			$selectedLogCost = $pendingLogCost
		}
		$pendingHotPath = @()
		$pendingMaintenance = @()
		$pendingApiWall = @()
		$pendingCpuStage = @()
		$pendingCoverage = @()
		$pendingLogCost = @()
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
$apiWallUs = 0.0
$cpuStageUs = 0.0
$logCostUs = 0.0

foreach ($entry in $selectedHotPath) {
	$knownHotPathUs += [double](Get-Prop $entry "totalUs" 0)
}
foreach ($entry in $selectedApiWall) {
	$apiWallUs += [double](Get-Prop $entry "totalUs" 0)
}
foreach ($entry in $selectedCpuStage) {
	$cpuStageUs += [double](Get-Prop $entry "totalUs" 0)
}
foreach ($entry in $selectedLogCost) {
	$logCostUs += [double](Get-Prop $entry "totalUs" 0)
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
"API wall sum:         $(Format-Time $apiWallUs)"
"CPU stage sum:        $(Format-Time $cpuStageUs)"
"Log cost sum:         $(Format-Time $logCostUs)"
""

if ($selectedCoverage.Count -gt 0) {
	"CPU Timeline Coverage"
	foreach ($entry in $selectedCoverage) {
		Write-Bar "apiScopeTotal" ([double](Get-Prop $entry "apiTotalPctX100" 0) / 100.0) ([double](Get-Prop $entry "apiScopeTotalUs" 0))
		Write-Bar "apiScopeSpan" ([double](Get-Prop $entry "apiSpanPctX100" 0) / 100.0) ([double](Get-Prop $entry "apiScopeSpanUs" 0))
		Write-Bar "apiMaxGap" $(if ($frameWallUs -gt 0) { [double](Get-Prop $entry "apiScopeMaxGapUs" 0) * 100.0 / $frameWallUs } else { 0 }) ([double](Get-Prop $entry "apiScopeMaxGapUs" 0))
		Write-Bar "apiBeforeFirst" $(if ($frameWallUs -gt 0) { [double](Get-Prop $entry "apiBeforeFirstScopeUs" 0) * 100.0 / $frameWallUs } else { 0 }) ([double](Get-Prop $entry "apiBeforeFirstScopeUs" 0))
		Write-Bar "apiAfterLast" $(if ($frameWallUs -gt 0) { [double](Get-Prop $entry "apiAfterLastScopeUs" 0) * 100.0 / $frameWallUs } else { 0 }) ([double](Get-Prop $entry "apiAfterLastScopeUs" 0))
	}
	""
}

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
"CPU Stage Chart"
$sortedCpuStage = $selectedCpuStage | Sort-Object { [double](Get-Prop $_ "totalUs" 0) } -Descending
$count = 0
foreach ($entry in $sortedCpuStage) {
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
	$name = "{0}:{1}" -f ([string](Get-Prop $entry "category" "")), ([string](Get-Prop $entry "stage" ""))
	Write-Bar $name $percent $totalUs
	$count++
}

""
"API Wall Chart"
$sortedApiWall = $selectedApiWall | Sort-Object { [double](Get-Prop $_ "totalUs" 0) } -Descending
$count = 0
foreach ($entry in $sortedApiWall) {
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
	Write-Bar ([string](Get-Prop $entry "bucket" "")) $percent $totalUs
	$count++
}

""
"Timeline Buckets"
$timelineRows = @(
	@("OutsidePresent", $outsidePresentUs),
	@("PresentOriginal", $presentOriginalUs),
	@("ExecuteCommandLists", $queueEstimatedTotalUs),
	@("WorkerTask", $workerTaskUs),
	@("WorkerDrainWait", $workerDrainWaitUs),
	@("ApiWall", $apiWallUs),
	@("CpuStage", $cpuStageUs),
	@("LogCost", $logCostUs)
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
