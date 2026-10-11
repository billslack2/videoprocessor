[CmdletBinding()]
param([string]$PublicKeyPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'packaging\updates\UpdatePublicKey.xml'))
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath $PublicKeyPath) { throw 'A release key already exists. Key rotation needs a release trusted by the previous key.' }
$certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=VideoProcessor Release Manifest Signing' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 -KeyExportPolicy NonExportable -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(5)
$rsa = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
try { [IO.File]::WriteAllText($PublicKeyPath, $rsa.ToXmlString($false), [Text.Encoding]::ASCII) } finally { $rsa.Dispose() }
Write-Host "Public key: $PublicKeyPath"
Write-Host "Signing certificate: Cert:\CurrentUser\My\$($certificate.Thumbprint)"
Write-Host 'Private key is non-exportable in this Windows account. Do not remove it. Bootstrap installs establish trust; this is not a publicly trusted Windows publisher certificate.'
