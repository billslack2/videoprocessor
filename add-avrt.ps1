# Close Visual Studio first!
$vcxproj = "src\VideoProcessor-GUI\VideoProcessor-GUI.vcxproj"

$content = Get-Content $vcxproj -Raw

# Add Avrt.lib to both Debug and Release configurations
$content = $content -replace '<AdditionalDependencies>opengl32\.lib;', '<AdditionalDependencies>Avrt.lib;opengl32.lib;'

Set-Content $vcxproj $content -NoNewline

Write-Host "✅ Added Avrt.lib to VideoProcessor-GUI.vcxproj" -ForegroundColor Green
Write-Host "   Now reopen Visual Studio and rebuild!" -ForegroundColor Yellow