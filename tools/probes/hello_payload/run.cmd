@echo off
set R=D:\i7run
for %%d in (D E F G H I J K) do if exist %%d:\i7run\run.cmd set R=%%d:\i7run
echo HELLO_BEGIN %DATE% %TIME% > COM1
echo whoami: >> COM1
whoami >> COM1
ver >> COM1
echo --- stdout redirect >> COM1
%R%\tm_hello.exe > COM1
echo --- direct CreateFile COM1 >> COM1
%R%\tm_hello.exe --com1
echo --- binary via certutil base64 (200000 B) >> COM1
certutil -encode %R%\rand.bin %TEMP%\rand.b64 > nul
echo B64_BEGIN %TIME% >> COM1
type %TEMP%\rand.b64 > COM1
echo B64_END %TIME% >> COM1
echo HELLO_END >> COM1
