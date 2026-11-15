CC ?= cc
PKGS = camel-1.2 libedataserver-1.2
PREFIX ?= $(HOME)/.local

CFLAGS ?= -O2 -g -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

SRC = \
	src/main.c \
	src/evoapp.c \
	src/mail.c \
	src/output.c \
	src/send.c
OBJ = $(SRC:.c=.o)
TARGET = evomail

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/evomail.h src/evoapp.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJ)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
