PROJECT  = serprog

CFLAGS   = -Wall -Wextra -pedantic

all: $(PROJECT)

$(PROJECT): serprog.c serprog.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) $(PROJECT)