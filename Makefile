PROGRAM = fan_control
CC      = gcc
CFLAGS  = -O2 -Wall
GTKLIBS = `pkg-config gtk+-3.0 --cflags --libs`
SRCPATH = src/
BINPATH = build/
DEBUGPATH = debug/

$(PROGRAM): $(SRCPATH)main.c
	$(CC) $(CFLAGS) -o $(BINPATH)$(PROGRAM) $(SRCPATH)main.c $(GTKLIBS)

.PHONY: beauty clean dist debug

debug:
	$(CC) -g -Wall -o $(DEBUGPATH)$(PROGRAM) $(SRCPATH)main.c $(GTKLIBS)

beauty:
	-indent $(PROGRAM).c
	-rm *~ *BAK

clean:
	-rm *.o $(PROGRAM) *core

dist: beauty clean
	-tar -chvz -C .. -f ../$(PROGRAM).tar.gz $(PROGRAM)
