.PHONY: all clean

SRCDIR=src
OBJDIR=obj

#CC=clang
CC=x86_64-w64-mingw32-gcc
#CFLAGS=-Wall -Werror -DSHOW_ANTICHEAT
CFLAGS=-Wall -Werror
LDFLAGS=-lm

SRCS=$(shell find $(SRCDIR) -name '*.c')
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
DEPS=$(OBJS:%.o=%.d)

all: mdp

-include $(DEPS)

clean:
	rm -rf mdp mdp.exe $(OBJDIR)

mdp: $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@
