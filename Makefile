    TARGET  := janosh 
    SRCS    := janosh.cpp Logger.cpp tri_logger/tri_logger.cpp
    OBJS    := ${SRCS:.cpp=.o} 
    DEPS    := ${SRCS:.cpp=.dep} 

    CXXFLAGS = -std=c++0x -I/usr/local/include/ -Os -Wall -DETLOG
    LDFLAGS = -s 
    LIBS    = -Wl,-Bstatic -lboost_system -lboost_filesystem -ljson_spirit -Wl,-Bdynamic -lkyotocabinet

    .PHONY: all clean distclean 
    all:: ${TARGET} 

    ifneq (${XDEPS},) 
    include ${XDEPS} 
    endif 

    ${TARGET}: ${OBJS} 
	${CXX} ${LDFLAGS} -o $@ $^ ${LIBS} 

    ${OBJS}: %.o: %.cpp %.dep 
	${CXX} ${CXXFLAGS} -o $@ -c $< 

    ${DEPS}: %.dep: %.cpp Makefile 
	${CXX} ${CXXFLAGS} -MM $< > $@ 

    clean:: 
	-rm -f *~ *.o ${TARGET} 

    distclean:: clean

