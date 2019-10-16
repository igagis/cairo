Push-Location
$scriptdir = Split-Path $MyInvocation.MyCommand.Path
cd $scriptdir


"%VS142COMNTOOLS%\VsMSBuildCmd.bat"

msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v140_Debug /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v140_Release /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v140_Debug /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v140_Release /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}

msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v141_Debug /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v141_Release /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v141_Debug /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v141_Release /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}

msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Debug_MD /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Release_MD /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Debug_MD /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Release_MD /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Debug_MT /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Release_MT /p:Platform=x86 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Debug_MT /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}
msbuild ../msvs_solution/msvs_solution.sln /t:Rebuild /p:Configuration=v142_Release_MT /p:Platform=x64 /v:minimal /nologo
If(!$?){exit 1}


Write-NuGetPackage nuget.autopkg
If(!$?){exit 1}
Pop-Location