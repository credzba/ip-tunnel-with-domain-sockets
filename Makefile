#CPP=g++
CPP=clang++
CPPFLAGS=-Wall
CFLAGS=-g 
INCLUDES=-I/usr/include/boost -I/usr/local/include/clang
LIBS=-L/usr/lib -lboost_system -lboost_program_options -lpthread  -L/usr/lib/x86_64-linux-gnu/ -lssl -lcrypto

.PHONY: all
all : connector worker client cscope.out

%.o : %.cpp
	$(CPP) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

worker : workerMain.o Worker.o
	$(CPP) $^ -o $@ $(LIBS)

client : clientMain.o Client.o
	$(CPP) $^ -o $@ $(LIBS)

connector: mainConnector.o MultiConnector.o
	$(CPP) $^ -o $@ $(LIBS)

.PHONY: clean
clean:
	@rm -f *.o
	@rm -f connector worker client
	@rm -f cscope.files cscope.*.out cscope.out

cscope.files : 
	find /usr/include -type f -name "*.h" > cscope.files
	find . -type f -name "*.h" -o -name "*.cpp" -o -name "*.c" >> cscope.files

cscope.out : cscope.files
	@rm -f cscope.*.out
	@rm -f cscope.out
	cscope -q -R -b -i cscope.files
