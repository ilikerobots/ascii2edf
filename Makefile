default:  ascii2edf 

ascii2edf: xml.o ascii2edf.o 
	g++ xml.o ascii2edf.o -o ascii2edf

xml.o: xml.h xml.cpp
	g++ -c xml.cpp

ascii2edf.o: ascii2edf.c
	g++ -c ascii2edf.c

clean: 
	rm ascii2edf *.o
