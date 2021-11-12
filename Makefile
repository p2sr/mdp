.PHONY: all clean

SRCDIR=src
OBJDIR=obj

CC=clang
CFLAGS=-Wall -Werror -D__USE_MINGW_ANSI_STDIO
LDFLAGS=-lm

-include config.mk

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
