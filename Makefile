PROGRAM = fan_control
CC      = gcc
CFLAGS  = -O2 -Wall
CDFLAGS = -g -Wall
GTKLIBS = `pkg-config gtk+-3.0 --cflags --libs`
APPINDLIBS = `pkg-config ayatana-appindicator3-0.1 --cflags --libs`
SRCPATH = src/
BINPATH = build/
DEBUGPATH = debug/
INSTALL_PATH = ~/.local/bin
GLOBAL_INSTALL = /usr/bin

RESOURCE_XML = $(SRCPATH)fan_control.gresource.xml
RESOURCE_C   = $(SRCPATH)resources.c

# build fan_control by default, duh...
.DEFAULT_GOAL := $(PROGRAM)

# resources.c generates C code from the icon and static images
# so we can just ship this as a standalone binary, not needing the glade file and pngs
$(RESOURCE_C): $(RESOURCE_XML) $(SRCPATH)gui_new.glade data/logo.png data/icon.png
	glib-compile-resources --sourcedir=$(SRCPATH) --target=$(RESOURCE_C) --generate-source $(RESOURCE_XML)

# Just regenerate the resources.c, always runs when compiling fan_control.
resources: $(RESOURCE_C)

.PHONY: beauty clean dist debug resources icon

# WIP: address the deprecated icon stuff
$(SRCPATH)icon.o: $(SRCPATH)icon.c $(SRCPATH)icon.h
	$(CC) $(CFLAGS) -c $(SRCPATH)icon.c -o $(SRCPATH)icon.o $(GTKLIBS) $(APPINDLIBS)

icon: $(SRCPATH)icon.o

# Compile fan_control
$(PROGRAM): $(SRCPATH)main.c resources
	$(CC) $(CFLAGS) -o $(BINPATH)$(PROGRAM) $(SRCPATH)main.c $(RESOURCE_C) $(GTKLIBS)

# Compile without optimisation in case debugging is ever needed
debug: $(SRCPATH)main.c resources
	$(CC) -g -Wall -o $(DEBUGPATH)$(PROGRAM) $(SRCPATH)main.c $(RESOURCE_C) $(GTKLIBS)

beauty:
	-indent $(PROGRAM).c
	-rm *~ *BAK

clean:
	-rm *.o $(SRCPATH)*.o $(BINPATH)/$(PROGRAM) $(BINPATH)/*.o $(RESOURCE_C)

dist: beauty clean
	-tar -chvz -C .. -f ../$(PROGRAM).tar.gz $(PROGRAM)

# install the damned thing
install: $(PROGRAM)
	@cp $(BINPATH)/$(PROGRAM) $(INSTALL_PATH)/$(PROGRAM)

# Install globally (probably needs sudo)
install-glocabl: $(PROGRAM)
	@cp $(BINPATH)/$(PROGRAM) $(GLOBAL_INSTALL)/$(PROGRAM)
