param(
    [ValidateSet('X', 'Y', 'Z', 'W', 'V', 'U', 'T', 'S', 'R', 'Q', 'P', 'O')]
    [string]$Variant = 'X',
    [string]$Artifacts = (Join-Path $PSScriptRoot '../e2studio_CPU0/Debug/issue230/artifacts'),
    [string]$LlvmBin = 'C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin'
)
$ErrorActionPreference = 'Stop'
if ($Variant -eq 'O') { throw 'O withdrawn: RTCOS changes require confirmed START=0 and RTCOE=0. See issue-230.md section 19.' }
# Diagnostic images only. Never connects to a device. See doc/design/issue-230.md, sections 9 and 12-17.
$Artifacts = (Resolve-Path -LiteralPath $Artifacts).Path
$inputElf = Join-Path $Artifacts 'G_CPU0.elf'
$expectedHash = '13B44D4518A8C2F456294E16C6FF733FD366D1831F9B6FA71DCC5E216130B221'
if ((Get-FileHash -LiteralPath $inputElf).Hash -ne $expectedHash) { throw 'Wrong G image; refusing to patch.' }
$outputElf = Join-Path $Artifacts "${Variant}_CPU0.elf"
if (Test-Path -LiteralPath $outputElf) { throw "$Variant already exists; preserve prior artifacts." }
$original = [IO.File]::ReadAllBytes($inputElf)
$patched = $original.Clone()
# Parse the ELF32 section table; locate the unique section containing each virtual address.
$shoff = [BitConverter]::ToUInt32($original, 32)
$shsize = [BitConverter]::ToUInt16($original, 46)
$shcount = [BitConverter]::ToUInt16($original, 48)
if ($original[4] -ne 1 -or $original[5] -ne 1 -or $shsize -ne 40) { throw 'Expected ELF32 little endian.' }
$patches = @(
    @{Address=0x0203C230; Before=[byte[]](0xFF,0xF7,0x7B,0xFB); After=[byte[]](0x1A,0xE0,0x00,0xBF)},
    @{Address=0x0203C26C; Before=[byte[]](0x00,0xBF); After=[byte[]](0x0F,0xE0)},
    @{Address=0x0203EDBA; Before=[byte[]](0xAC,0x65); After=[byte[]](0x00,0xBF)}
)
if ($Variant -eq 'Y') { $patches = @($patches[0], $patches[1]) }
if ($Variant -eq 'Z') { $patches = @($patches[2]) }
if ($Variant -eq 'U') { $patches = @($patches[1]) }
if ($Variant -eq 'T') {
    # Keep RCR2 stop/reset; skip RCR1 clear/wait and TCEN clear/wait.
    $patches = @(
        @{Address=0x0203C24E; Before=[byte[]](0x00,0x20); After=[byte[]](0x0B,0xE0)},
        $patches[1]
    )
}
if ($Variant -eq 'S') {
    # Keep RCR1 clear/wait; skip RCR2 stop/reset and TCEN clear/wait.
    $patches = @(
        @{Address=0x0203C234; Before=[byte[]](0x2C,0x70); After=[byte[]](0x0B,0xE0)},
        $patches[1]
    )
}
if ($Variant -in @('W', 'V')) {
    # Keep the original 200-us delay call; branch only after it returns.
    $patches = @(
        @{Address=0x0203C234; Before=[byte[]](0x2C,0x70); After=[byte[]](0x18,0xE0)},
        $patches[1]
    )
}
if ($Variant -eq 'V') {
    # Verified machine code multiplies delay * units: 200 * 250 = 50000 us.
    # Patch only the immediate byte; the MOVS r1 opcode byte (0x21) stays unchanged.
    $patches += @{Address=0x0203C22C; Before=[byte[]](0x01); After=[byte[]](0xFA)}
}
if ($Variant -in @('R', 'Q')) {
    # Skip writes, visit the existing RCR1 read once, and skip its polling branch.
    $patches = @(
        @{Address=0x0203C234; Before=[byte[]](0x2C,0x70); After=[byte[]](0x10,0xE0)},
        @{Address=0x0203C25C; Before=[byte[]](0x08,0xB1); After=[byte[]](0x04,0xE0)},
        $patches[1]
    )
    if ($Variant -eq 'Q') {
        # Compare DSB SY with the single read at exactly the same instruction address.
        $patches += @{Address=0x0203C258; Before=[byte[]](0x15,0xF8,0x02,0x0C); After=[byte[]](0xBF,0xF3,0x4F,0x8F)}
    }
}
if ($Variant -in @('P', 'O')) {
    # RCR1 read/modify/write followed by readback of the selected bits, in the old 20-byte block.
    # Assembled with clang for Cortex-M85; only the mask immediate differs between P and O.
    $mask = if ($Variant -eq 'P') { 0x07 } else { 0xF8 }
    $patches = @(
        @{Address=0x0203C234; Before=[byte[]](0x2C,0x70); After=[byte[]](0x0B,0xE0)},
        @{Address=0x0203C24E;
          Before=[byte[]](0x00,0x20,0x05,0xF8,0x02,0x0C,0x00,0xBF,0x00,0xBF,0x15,0xF8,0x02,0x0C,0x08,0xB1,0x2F,0xF0,0x05,0xC8);
          After=[byte[]]($mask,0x20,0x15,0xF8,0x02,0x1C,0x81,0x43,0x05,0xF8,0x02,0x1C,0x15,0xF8,0x02,0x1C,0x01,0x42,0xFB,0xD1)},
        $patches[1]
    )
}
$expectedDifferences = 0
foreach ($patch in $patches) {
    if ($patch.Before.Length -ne $patch.After.Length) { throw 'Patch size must be unchanged.' }
    for ($i=0; $i -lt $patch.Before.Length; $i++) {
        if ($patch.Before[$i] -ne $patch.After[$i]) { $expectedDifferences++ }
    }
}
$allowed = [Collections.Generic.HashSet[int]]::new()
foreach ($patch in $patches) {
    $matches = @()
    for ($i=0; $i -lt $shcount; $i++) {
        $h = $shoff + $i * $shsize
        $type = [BitConverter]::ToUInt32($original, $h+4)
        $flags = [BitConverter]::ToUInt32($original, $h+8)
        $addr = [BitConverter]::ToUInt32($original, $h+12)
        $offset = [BitConverter]::ToUInt32($original, $h+16)
        $size = [BitConverter]::ToUInt32($original, $h+20)
        if ($type -eq 1 -and ($flags -band 6) -eq 6 -and $patch.Address -ge $addr -and
            $patch.Address + $patch.Before.Length -le $addr + $size) {
            $matches += [int]($offset + $patch.Address - $addr)
        }
    }
    if ($matches.Count -ne 1) { throw 'Patch must lie in exactly one executable PROGBITS section.' }
    $offset = $matches[0]
    for ($i=0; $i -lt $patch.Before.Length; $i++) {
        if ($original[$offset+$i] -ne $patch.Before[$i]) { throw 'Original instruction mismatch.' }
        if (-not $allowed.Add($offset+$i)) { throw 'Overlapping patches.' }
        $patched[$offset+$i] = $patch.After[$i]
    }
    $patch.Offset = $offset
}
$differences = 0
for ($i=0; $i -lt $original.Length; $i++) {
    if ($original[$i] -ne $patched[$i]) {
        if (-not $allowed.Contains($i)) { throw 'Unexpected ELF change.' }
        $differences++
    }
}
if ($differences -ne $expectedDifferences) { throw "Expected exactly $expectedDifferences differing bytes." }
[IO.File]::WriteAllBytes($outputElf, $patched)
& "$LlvmBin/llvm-objcopy.exe" -O srec $outputElf (Join-Path $Artifacts "${Variant}_CPU0.mot")
if ($LASTEXITCODE) { throw 'MOT conversion failed.' }

# Compare addressed data after conversion as well, including unchanged record structure/termination.
$gRecords = [IO.File]::ReadAllLines((Join-Path $Artifacts 'G_CPU0.mot'))
$vRecords = [IO.File]::ReadAllLines((Join-Path $Artifacts "${Variant}_CPU0.mot"))
if ($gRecords.Length -ne $vRecords.Length) { throw 'MOT record count mismatch.' }
$expectedAddresses = @{}
foreach ($patch in $patches) {
    for ($k=0; $k -lt $patch.Before.Length; $k++) {
        $expectedAddresses[[long]($patch.Address+$k)] = @($patch.Before[$k], $patch.After[$k])
    }
}
$motDifferences = 0
for ($i=0; $i -lt $gRecords.Length; $i++) {
    $gLine=$gRecords[$i]; $vLine=$vRecords[$i]
    if ($gLine.StartsWith('S0')) { continue } # Filename header only.
    if ($gLine -eq $vLine) { continue }
    if (-not $gLine.StartsWith('S3') -or $gLine.Substring(0,12) -ne $vLine.Substring(0,12)) {
        throw 'MOT record type/address/length/termination changed.'
    }
    $address=[Convert]::ToUInt32($gLine.Substring(4,8),16)
    $length=[Convert]::ToInt32($gLine.Substring(2,2),16)-5
    for ($k=0; $k -lt $length; $k++) {
        $before=[Convert]::ToByte($gLine.Substring(12+$k*2,2),16)
        $after=[Convert]::ToByte($vLine.Substring(12+$k*2,2),16)
        if ($before -eq $after) { continue }
        $key=[long]($address+$k)
        if (-not $expectedAddresses.ContainsKey($key) -or
            $expectedAddresses[$key][0] -ne $before -or $expectedAddresses[$key][1] -ne $after) {
            throw 'Unexpected addressed MOT data change.'
        }
        $motDifferences++
    }
}
if ($motDifferences -ne $expectedDifferences) { throw 'MOT change count mismatch.' }
& "$LlvmBin/llvm-objdump.exe" -d --start-address=0x0203C218 --stop-address=0x0203C2D4 $outputElf |
    Set-Content -LiteralPath (Join-Path $Artifacts "${Variant}-rtc-disassembly.txt")
if ($LASTEXITCODE) { throw 'RTC disassembly failed.' }
& "$LlvmBin/llvm-objdump.exe" -d --start-address=0x0203EDA4 --stop-address=0x0203EE0C $outputElf |
    Set-Content -LiteralPath (Join-Path $Artifacts "${Variant}-ielsr-disassembly.txt")
if ($LASTEXITCODE) { throw 'IELSR disassembly failed.' }
[ordered]@{
    variant=$Variant
    sourceSha256=$expectedHash
    outputSha256=(Get-FileHash -LiteralPath $outputElf).Hash
    changedBytes=$differences
    motChangedBytes=$motDifferences
    motSha256=(Get-FileHash -LiteralPath (Join-Path $Artifacts "${Variant}_CPU0.mot")).Hash
    patches=$patches
    note='Not hardware validated. Entire ELF unchanged outside listed patch bytes, including symbols and headers.'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Artifacts "${Variant}-patch-manifest.json")
"Created ${Variant}_CPU0.elf and ${Variant}_CPU0.mot. Exactly $differences ELF/MOT data bytes changed; no relinking."
