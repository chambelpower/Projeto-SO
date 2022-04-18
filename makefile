
FLAGS  = -Wall -g -pthread
CC     = gcc
PROG   = project
OBJS   = project.o

all:	${PROG}

clean:
	rm ${OBJS} *~ ${PROG}
  
${PROG}:	${OBJS}
	${CC} ${FLAGS} ${OBJS} -o $@

.c.o:
	${CC} ${FLAGS} $< -c -o $@

##########################

project.o: project.c

project: project.o

