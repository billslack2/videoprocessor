function Test-VpTestRegistration([string]$ProductGuid) {
    if ($ProductGuid -notmatch '^[0-9A-Fa-f-]{36}$') { throw 'Invalid test registration GUID.' }
    $hive = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::CurrentUser,
        [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $hive.OpenSubKey(
            ('Software\Microsoft\Windows\CurrentVersion\Uninstall\{' + $ProductGuid + '}_is1'),
            $false)
        if ($key) { $key.Dispose(); return $true }
        return $false
    } finally { $hive.Dispose() }
}

