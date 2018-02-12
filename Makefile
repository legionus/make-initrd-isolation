PROJECT = make-initrd
VERSION = $(shell sed '/^Version: */!d;s///;q' $(CURDIR)/.gear/make-initrd-container.spec)

warning_CFLAGS = \
	-Wall -Wextra -W -Wshadow -Wcast-align \
	-Wwrite-strings -Wconversion -Waggregate-return -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn \
	-Wmissing-format-attribute -Wredundant-decls -Wdisabled-optimization \
	-Wno-pointer-arith

sysconfdir ?= /etc
bindir     ?= /usr/bin
sbindir    ?= /usr/sbin
datadir    ?= /usr/share
statedir   ?= /var/lib
mandir     ?= $(datadir)/man
man1dir    ?= $(mandir)/man1
tmpdir     ?= /tmp
prefix     ?= $(datadir)/$(PROJECT)
DESTDIR    ?=

ifdef MKLOCAL
prefix        = $(TOPDIR)
bindir        = $(TOPDIR)
sbindir       = $(TOPDIR)
statedir      = $(tmpdir)
endif

quiet_cmd   = $(if $(VERBOSE),$(3),$(Q)printf "  %-07s%s\n" "$(1)" $(2); $(3))
get_objects = $(foreach name,$(notdir $(1)),$($(subst -,_,$(name))_SRCS:.c=.o))
get_depends = $(foreach name,$(notdir $(1)),$($(subst -,_,$(name))_SRCS:.c=.d))

Q = @
VERBOSE ?= $(V)
ifeq ($(VERBOSE),1)
    Q =
endif

CP       = $(Q)cp -a
RM       = $(Q)rm -f
TAR      = $(Q)tar
CHMOD    = $(Q)chmod
INSTALL  = $(Q)install
MKDIR_P  = $(Q)mkdir -p
TOUCH_R  = $(Q)touch -r
STRIP    = $(Q)strip -s
SED      = $(call quiet_cmd,SED,$@,sed)
HELP2MAN = $(call quiet_cmd,MAN,$@,env -i help2man -N)
COMPILE  = $(call quiet_cmd,CC,$<,$(COMPILE.c))
LINK     = $(call quiet_cmd,CCLD,$@,$(LINK.o))
DEP      = $(call quiet_cmd,DEP,$<,$(CC))

CFLAGS = $(warning_CFLAGS) \
	-DPACKAGE=\"$@\" -DVERSION=\"$(VERSION)\" \
	-DPROGRAM_NAME=\"$(notdir $(PROG))\" \
	-D_GNU_SOURCE=1

CFLAGS += -I.
CFLAGS += -Os

bin_PROGS =
sbin_PROGS = container containerctl
config_ini = config.ini

container_SRCS = \
	container.c \
	container-caps.c \
	container-common.c \
	container-env.c \
	container-epoll.c \
	container-fds.c \
	container-hooks.c \
	container-mknod.c \
	container-mount.c \
	container-netns.c \
	container-ns.c \
	container-userns.c

container_LIBS = $(shell pkg-config --libs libcap)

DEPS = $(call get_depends,$(bin_PROGS) $(sbin_PROGS),)
OBJS = $(call get_objects,$(bin_PROGS) $(sbin_PROGS),)

all: $(config_ini) $(bin_PROGS) $(sbin_PROGS)

%: %.in
	$(SED) \
		-e 's,@VERSION@,$(VERSION),g' \
		-e 's,@PROJECT@,$(PROJECT),g' \
		-e 's,@BOOTDIR@,$(bootdir),g' \
		-e 's,@CONFIG@,$(sysconfdir),g' \
		-e 's,@STATEDIR@,$(statedir),g' \
		-e 's,@PREFIX@,$(prefix),g' \
		-e 's,@BINDIR@,$(bindir),g' \
		-e 's,@SBINDIR@,$(sbindir),g' \
		-e 's,@TMPDIR@,$(tmpdir),g' \
		-e 's,@LOCALBUILDDIR@,$(localbuilddir),g' \
		<$< >$@
	$(TOUCH_R) $< $@
	$(CHMOD) --reference=$< $@

%.o: %.c
	$(COMPILE) $(OUTPUT_OPTION) $<

container: $(call get_objects,container)
	$(LINK) $(realpath $^) -o $@ $(container_LIBS)

format:
	clang-format -style=file -i container*.c container*.h

install: $(config_ini) $(sbin_PROGS)
	$(MKDIR_P) -- $(DESTDIR)$(bindir) $(DESTDIR)$(sbindir)
	$(INSTALL) -p -m755 $(sbin_PROGS) $(DESTDIR)$(sbindir)/
	$(MKDIR_P) -m700 -- $(DESTDIR)$(sysconfdir)/container
	$(INSTALL) -p -m644 $(config_ini) $(DESTDIR)$(sysconfdir)/container/
	$(CP) -r example/system $(DESTDIR)$(sysconfdir)/container/
	$(MKDIR_P) -- $(DESTDIR)$(statedir)/container
	$(TAR) -xf example/system.rootfs.tar.zst -C $(DESTDIR)$(statedir)/container

clean:
	$(RM) -rf -- $(config_ini) $(bin_PROGS) $(sbin_PROGS) $(DEPS) $(OBJS)

# We need dependencies only if goal isn't "indent" or "clean".
ifneq ($(MAKECMDGOALS),indent)
ifneq ($(MAKECMDGOALS),clean)

%.d: %.c Makefile
	$(DEP) -MM $(CPPFLAGS) $< |sed -e 's,\($*\)\.o[ :]*,\1.o $@: Makefile ,g' >$@

ifneq ($(DEPS),)
-include $(DEPS)
endif

endif # clean
endif # indent
