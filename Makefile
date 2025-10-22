CC = cl
INCLUDES = \
	-I"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.43.34808\include" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" \
	-I"c:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
CFLAGS = -nologo -O2
LIBPATHS = \
	-LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.43.34808\lib\x64" \
	-LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" \
	-LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"

main:
	$(CC) main.c $(CFLAGS) $(INCLUDES) -link $(LIBPATHS)
