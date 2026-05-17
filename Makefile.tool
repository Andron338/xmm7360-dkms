# Makefile — xmm7360 RPC tool
#
# Build:       make
# Install:     sudo make install
# Uninstall:   sudo make uninstall

CC      ?= gcc
CFLAGS  := -std=c11 -Wall -Wextra -g -O2
LDFLAGS :=

PREFIX  ?= /usr
BINDIR  := $(PREFIX)/bin

NM_CFLAGS  := $(shell pkg-config --cflags libnm 2>/dev/null)
NM_LIBS    := $(shell pkg-config --libs   libnm 2>/dev/null)
SSL_LIBS   := $(shell pkg-config --libs   openssl 2>/dev/null || echo "-lssl -lcrypto")

CFLAGS  += $(NM_CFLAGS)
LDFLAGS += $(NM_LIBS) $(SSL_LIBS) -luuid

SRCS := \
    open_xdatachannel.c \
    xmm_proto.c         \
    xmm_rpc.c           \
    xmm_netlink.c       \
    xmm_nm.c

OBJS := $(SRCS:.c=.o)
BIN  := open_xdatachannel

.PHONY: all clean install uninstall

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(OBJS) $(BIN)

# Header dependencies
open_xdatachannel.o: open_xdatachannel.c xmm_rpc.h xmm_rpc_ids.h xmm_netlink.h xmm_nm.h xmm_proto.h
xmm_rpc.o:          xmm_rpc.c xmm_rpc.h xmm_rpc_ids.h xmm_unsol.h xmm_proto.h
xmm_proto.o:        xmm_proto.c xmm_proto.h
xmm_netlink.o:      xmm_netlink.c xmm_netlink.h
xmm_nm.o:           xmm_nm.c xmm_nm.h
