CC = cl
INCLUDES = \
	-I"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.43.34808\include" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" \
	-I"lua\include"
CFLAGS = -nologo
LIBPATHS = \
	-LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.43.34808\lib\x64" \
	-LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" \
	-LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" \
	-LIBPATH:"lua" lua51.lib
	

all: main_lib main_exe

main_lib:
	$(CC) main.c -LD $(CFLAGS) $(INCLUDES) -link $(LIBPATHS) -out:swimd.dll

main_exe:
	$(CC) main.c $(CFLAGS) -ZI $(INCLUDES) -link -out:swimd.exe $(LIBPATHS) -DEBUG

clean:
	del *.exe *.exp *.lib *.obj *.dll *.pdb *.idb *.ilk
