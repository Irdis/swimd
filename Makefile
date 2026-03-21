CC = cl
	
all: main

nob.exe:
	$(CC) nob.c

main: nob.exe
	nob.exe

clean:
	del *.exe *.exp *.lib *.obj *.dll *.pdb *.idb *.ilk
