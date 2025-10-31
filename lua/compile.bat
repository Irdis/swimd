:: Clean up files from previous builds
@IF EXIST *.o @DEL *.o
@IF EXIST *.obj @DEL *.obj
@IF EXIST *.dll @DEL *.dll
@IF EXIST *.exe @DEL *.exe

:: Compile all .c files into .obj
@CL /MD /O2 /c /DLUA_BUILD_AS_DLL -I"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.43.34808\include" -I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt"  -I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" -I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" *.c

:: Rename two special files
@REN lua.obj lua.o
@REN luac.obj luac.o

:: Link up all the other .objs into a .lib and .dll file
@LINK -LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.43.34808\lib\x64" -LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" -LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" /DLL /IMPLIB:lua51.lib /OUT:lua51.dll *.obj

:: Link lua into an .exe
@LINK /OUT:lua.exe lua.o lua51.lib

:: Create a static .lib
@LIB /OUT:lua-static.lib *.obj

:: Link luac into an .exe
@LINK /OUT:luac.exe luac.o lua-static.lib

:: Move back up out of 'src'
@POPD

:: Copy the library and executable files out from 'src'
@COPY /Y src\lua.exe lua.exe
@COPY /Y src\luac.exe luac.exe
@COPY /Y src\lua.dll lua.dll

:ENDSCRIPT

:: End local variable scope
@ENDLOCAL