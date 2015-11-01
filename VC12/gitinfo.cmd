for /f %%i in ('git ls-remote --get-url') do ( echo #define REPOURL "%%i" > ".\repourl.tmp" )

if not exist ".\repourl.h" copy /y ".\repourl.tmp" ".\repourl.h"
if not exist ".\repourl.h" echo GEEE .\repourl.h is missing!

echo n | comp ".\repourl.tmp" ".\repourl.h" >NUL 2>NUL
if errorlevel 1 (copy /y ".\repourl.tmp" ".\repourl.h") else (echo ... .\repourl.h is up to date.)
del ".\repourl.tmp"

for /f %%i in ('git show-ref -s12 origin/master') do ( echo #define GITIDENT "%%i" > ".\gitident.tmp" )

if not exist ".\gitident.h" copy /y ".\gitident.tmp" ".\gitident.h"
if not exist ".\gitident.h" echo WOOOAH .\gitident.h is missing!

echo n | comp ".\gitident.tmp" ".\gitident.h" >NUL 2>NUL
if errorlevel 1 (copy /y ".\gitident.tmp" ".\gitident.h") else (echo ... .\gitident.h is up to date.)
del ".\gitident.tmp"

