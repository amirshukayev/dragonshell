dragonshell: dragonshell.o
	g++ dragonshell.o -o dragonshell

compile: dragonshell.cpp
	g++ -c dragonshell.cpp -Wall

compress: Makefile dragonshell.cpp readme.md
	tar -czvf dragonshell.tar.gz Makefile dragonshell.cpp readme.md

clean:
	rm -rf *.o dragonshell *.tar.gz