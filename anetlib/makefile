RM=rm -rf
TARGET_ARCH=$(shell uname)

LINUX=Linux
MACOS=Darwin

PROG=anetlib
SRC=zmalloc.c netlib.c main.c anet.c ae.c adlist.c

ifeq ($(TARGET_ARCH), $(MACOS))
	SRC += ae_kqueue.c
	
else ifeq ($(TARGET_ARCH), $(LINUX))
	SRC += ae_epoll.c
endif


.PHONY: clean

all:$(PROG)

OBJ=$(SRC:.c=.o)


$(PROG) : $(SRC)
	gcc -g -Wall -o $@ $^

clean:
	$(RM) $(PROG)
	$(RM) $(PROG).*
	$(RM) $(OBJ)
