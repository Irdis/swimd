CC = cl
INCLUDES = \
	-I"lualib\include" \
	-I"libgit2\include"
CFLAGS = -nologo -MT -O2
LFLAGS = -DEBUG
LIBPATHS = \
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
