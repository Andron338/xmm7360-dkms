# Makefile — xmm7360 C port
#
# Dependencies:
#   libnm      (NetworkManager GLib API)   apt: libnm-dev
#   libuuid    (uuid_generate_random)       apt: uuid-dev
#   openssl    (SHA256)                     apt: libssl-dev
#   linux-headers (rtnetlink)               usually pre-installed
#
# Build:
#   make
#
# Build without NetworkManager support (omit --dbus option at runtime):
#   make NO_NM=1

CC      ?= gcc
CFLAGS  := -std=c11 -Wall -Wextra -g -O2
LDFLAGS :=

# ── pkg-config probes ──────────────────────────────────────────────────────
NM_CFLAGS  := $(shell pkg-config --cflags libnm 2>/dev/null)
NM_LIBS    := $(shell pkg-config --libs   libnm 2>/dev/null)
SSL_LIBS   := $(shell pkg-config --libs   openssl 2>/dev/null || echo "-lssl -lcrypto")

CFLAGS  += $(NM_CFLAGS)
LDFLAGS += $(NM_LIBS) $(SSL_LIBS) -luuid

# ── Sources ────────────────────────────────────────────────────────────────
SRCS := \
    open_xdatachannel.c \
    xmm_proto.c         \
    xmm_rpc.c           \
    xmm_netlink.c       \
    xmm_nm.c

OBJS := $(SRCS:.c=.o)
BIN  := open_xdatachannel

# ── Rules ──────────────────────────────────────────────────────────────────
.PHONY: all clean install

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies
open_xdatachannel.o: open_xdatachannel.c xmm_rpc.h xmm_rpc_ids.h xmm_netlink.h xmm_nm.h xmm_proto.h
xmm_rpc.o:           xmm_rpc.c           xmm_rpc.h xmm_rpc_ids.h xmm_unsol.h   xmm_proto.h
xmm_proto.o:         xmm_proto.c         xmm_proto.h
xmm_netlink.o:       xmm_netlink.c       xmm_netlink.h
xmm_nm.o:            xmm_nm.c            xmm_nm.h

install: $(BIN)
	install -m 755 $(BIN) /usr/local/sbin/$(BIN)

clean:
	rm -f $(OBJS) $(BIN)
