include config.mk

__NAME__ = windowchef
__NAME_CLIENT__ = waitron
__THIS_VERSION__ = 0.1
NAME_DEFINES = -D__NAME__=\"$(__NAME__)\" \
			   -D__NAME_CLIENT__=\"$(__NAME_CLIENT__)\" \
			   -D__THIS_VERSION__=\"$(__THIS_VERSION__)\"

SRC = list.c wm.c client.c
OBJ = $(SRC:.c=.o)
BIN = $(__NAME__) $(__NAME_CLIENT__)
CFLAGS += $(NAME_DEFINES)

all: $(BIN)

$(__NAME__): wm.o list.o
	@echo $@
	@$(CC) -o $@ $^ $(LDFLAGS)

$(__NAME_CLIENT__): client.o
	@echo $@
	@$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	@echo $@
	@$(CC) -o $@ -c $(CFLAGS) $<

$(OBJ): common.h list.h ipc.h types.h config.h

install: all
	install $(__NAME__) $(DESTDIR)$(PREFIX)/bin/$(__NAME__)
	install $(__NAME_CLIENT__) $(DESTDIR)$(PREFIX)/bin/$(__NAME_CLIENT__)
	cd ./man; $(MAKE) install

clean:
	rm -f $(OBJ) $(BIN)
