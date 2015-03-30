CC=g++ -I ../../Downloads/boost_1_56_0
CFLAGS=-Wall -std=c++11 -arch i386
LIBS=-L ../../Downloads/boost_1_56_0/stage/lib -lCrypto -lboost_system -lboost_thread -arch i386 -dynamiclib
OBJ = interposer.o encrypt.o
DEPS = encrypt.h

%.o: %.cpp $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

libinterpose_x86: $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

.PHONY: clean

clean:
	rm -f *.o
	rm libinterpose_x86
