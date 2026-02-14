CC = cl
INCLUDES = \
	-I"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\include" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" \
	-I"lualib\include" \
	-I"libgit2\include"
CFLAGS = -nologo -MT
LFLAGS = -DEBUG
LIBPATHS = \
	-LIBPATH:"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\lib\x64" \
	-LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.50.35717\lib\x64" \
	-LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" \
	-LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" \
	-LIBPATH:"lualib" lua51.lib \
	-LIBPATH:"libgit2" git2.lib
	
all: main_lib main_exe

git2.dll:
	copy libgit2\git2.dll .

main_lib: git2.dll
	$(CC) main.c -LD $(CFLAGS) -ZI $(INCLUDES) -link $(LIBPATHS) -out:swimd.dll $(LFLAGS)

main_exe: git2.dll
	$(CC) main.c -DDEBUG_PRINT $(CFLAGS) -ZI $(INCLUDES) -link -out:swimd.exe $(LIBPATHS) $(LFLAGS)

clean:
	del *.exe *.exp *.lib *.obj *.dll *.pdb *.idb *.ilk
