all: main

build/nob:
	gcc -o build/nob nob.c

main: build/nob
	./build/nob

clean:
	rmrf -p build/ 
	mkdir build/
	touch build/.gitkeep
