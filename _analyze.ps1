$content = Get-Content 'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\FrameAnalysis-2026-07-02-193442\log.jsonl' -Raw

# Key summary stats
Write-Host "=== Summary Stats ==="
$content -split "`n" | Where-Object { $_ -like '*ResourceSummary*' } | ForEach-Object { Write-Host $_ }
$content -split "`n" | Where-Object { $_ -like '*IaBindingsCollected*' } | ForEach-Object { Write-Host $_ }
$content -split "`n" | Where-Object { $_ -like '*ResourceCopyResult*' } | ForEach-Object { Write-Host $_ }
$content -split "`n" | Where-Object { $_ -like '*ResourceDump*mode*' } | ForEach-Object { Write-Host $_ }

# Fallback scan hits
Write-Host "`n=== Fallback Scan Hits ==="
$fallback = ([regex]::Matches($content, 'BufferResolveFallbackScanHit')).Count
Write-Host "BufferResolveFallbackScanHit: $fallback"

# Zero GPU VA
Write-Host "`n=== Zero GPU VA ==="
$zeroVa = ([regex]::Matches($content, 'TrackedBufferResourceZeroGpuVa')).Count
Write-Host "TrackedBufferResourceZeroGpuVa: $zeroVa"

# IA skip analysis  
Write-Host "`n=== IA Skip Analysis ==="
$iaSkipped = ([regex]::Matches($content, 'IaBindingPrepareSkipped')).Count
$iaSkippedIB = ([regex]::Matches($content, 'IaBindingPrepareSkipped.*"role":"IB"')).Count
$iaSkippedVB = ([regex]::Matches($content, 'IaBindingPrepareSkipped.*"role":"VB"')).Count
Write-Host "IaBindingPrepareSkipped total: $iaSkipped"
Write-Host "  IB: $iaSkippedIB"
Write-Host "  VB: $iaSkippedVB"

# BindIA counts
$bindIa = ([regex]::Matches($content, '"func":"BindIA"')).Count
$bindIaIB = ([regex]::Matches($content, '"func":"BindIA".*"role":"IB"')).Count
$bindIaVB = ([regex]::Matches($content, '"func":"BindIA".*"role":"VB"')).Count
Write-Host "`n=== BindIA Manifest ==="
Write-Host "Total: $bindIa"
Write-Host "  IB: $bindIaIB"
Write-Host "  VB: $bindIaVB"

# Deduped files count
$files = Get-ChildItem 'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\FrameAnalysis-2026-07-02-193442\deduped\*.buf' -ErrorAction SilentlyContinue
Write-Host "`n=== Deduped Files ==="
Write-Host ".buf files: $($files.Count)"

# Compare with previous dump
Write-Host "`n=== Comparison with Previous Dump ==="
Write-Host "Previous: 699 IB / 1464 VB resolved, 69 IB / 12 VB skipped"
Write-Host "Current:  $bindIaIB IB / $bindIaVB VB resolved, $iaSkippedIB IB / $iaSkippedVB VB skipped"

if ($iaSkippedIB -gt 0) {
    Write-Host "`n=== Sample Skipped IBs ==="
    $pattern = '\{[^}]*IaBindingPrepareSkipped[^}]*"role":"IB"[^}]*\}'
    $ibSkipLines = [regex]::Matches($content, $pattern)
    $n = [Math]::Min(5, $ibSkipLines.Count)
    for ($i = 0; $i -lt $n; $i++) {
        $line = $ibSkipLines[$i].Value
        $viewGpu = ''
        $reason = ''
        $note = ''
        if ($line -match '"view_gpu":"([^"]+)"') { $viewGpu = $matches[1] }
        if ($line -match '"reason":"([^"]+)"') { $reason = $matches[1] }
        if ($line -match '"note":"([^"]+)"') { $note = $matches[1] }
        Write-Host "  GPU=$viewGpu reason=$reason note=$note"
    }
}

# Fallback details if any
if ($fallback -gt 0) {
    Write-Host "`n=== Fallback Scan Details (first 3) ==="
    $fbPattern = '\{[^}]*BufferResolveFallbackScanHit[^}]*\}'
    $fbLines = [regex]::Matches($content, $fbPattern)
    $n = [Math]::Min(3, $fbLines.Count)
    for ($i = 0; $i -lt $n; $i++) {
        Write-Host $fbLines[$i].Value.Substring(0, [Math]::Min(400, $fbLines[$i].Value.Length))
    }
}
