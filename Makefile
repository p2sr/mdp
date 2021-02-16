.PHONY: all clean

SRCDIR=src
OBJDIR=obj

CC=clang
CFLAGS=-Wall -Werror
LDFLAGS=

SRCS=$(shell find $(SRCDIR) -name '*.c')
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
DEPS=$(OBJS:%.o=%.d)

all: mdp

-include $(DEPS)

clean:
	rm -rf mdp $(OBJDIR)

mdp: $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@
