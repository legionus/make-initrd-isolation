VERSION = $(shell sed '/^Version: */!d;s///;q' $(CURDIR)/.gear/isolate.spec)

warning_CFLAGS = \
	-g -Os -D_FORTIFY_SOURCE=2 -fstack-protector \
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
DESTDIR    ?=

ifdef MKLOCAL
bindir   = $(TOPDIR)
sbindir  = $(TOPDIR)
statedir = $(tmpdir)
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

CFLAGS = $(warning_CFLAGS) -I. -DVERSION=\"$(VERSION)\" -D_GNU_SOURCE=1

bin_PROGS =
sbin_PROGS = isolate isolatectl isolate-run
config_ini = config.ini

isolate_SRCS = \
	isolate.c \
	isolate-caps.c \
	isolate-cgroups.c \
	isolate-common.c \
	isolate-env.c \
	isolate-epoll.c \
	isolate-fds.c \
	isolate-hooks.c \
	isolate-mknod.c \
	isolate-mount.c \
	isolate-netns.c \
	isolate-ns.c \
	isolate-userns.c

isolate_LIBS = $(shell pkg-config --libs libcap)

DEPS = $(call get_depends,$(bin_PROGS) $(sbin_PROGS),)
OBJS = $(call get_objects,$(bin_PROGS) $(sbin_PROGS),)

all: $(config_ini) $(bin_PROGS) $(sbin_PROGS)

%: %.in
	$(SED) \
		-e 's,@VERSION@,$(VERSION),g' \
		-e 's,@CONFIG@,$(sysconfdir),g' \
		-e 's,@STATEDIR@,$(statedir),g' \
		-e 's,@BINDIR@,$(bindir),g' \
		-e 's,@SBINDIR@,$(sbindir),g' \
		-e 's,@TMPDIR@,$(tmpdir),g' \
		<$< >$@
	$(TOUCH_R) $< $@
	$(CHMOD) --reference=$< $@

%.o: %.c
	$(COMPILE) $(OUTPUT_OPTION) $<

isolate: $(call get_objects,isolate)
	$(LINK) $(realpath $^) -o $@ $(isolate_LIBS)

format:
	clang-format -style=file -i isolate*.c isolate*.h

install: $(config_ini) $(sbin_PROGS)
	$(MKDIR_P) -- $(DESTDIR)$(bindir) $(DESTDIR)$(sbindir)
	$(INSTALL) -p -m755 $(sbin_PROGS) $(DESTDIR)$(sbindir)/
	$(MKDIR_P) -m700 -- $(DESTDIR)$(sysconfdir)/isolate
	$(INSTALL) -p -m644 $(config_ini) $(DESTDIR)$(sysconfdir)/isolate/
	$(CP) -r example/system $(DESTDIR)$(sysconfdir)/isolate/
	$(MKDIR_P) -- $(DESTDIR)$(statedir)/isolate
	$(TAR) -xf example/system.rootfs.tar.zst -C $(DESTDIR)$(statedir)/isolate
	$(MKDIR_P) -- $(DESTDIR)$(datadir)/make-initrd
	$(CP) -r features $(DESTDIR)$(datadir)/make-initrd/

clean:
	$(RM) -rf -- $(config_ini) $(bin_PROGS) $(sbin_PROGS) $(DEPS) $(OBJS)

# We need dependencies only if goal isn't "format" or "clean".
ifneq ($(MAKECMDGOALS),format)
ifneq ($(MAKECMDGOALS),clean)

%.d: %.c Makefile
	$(DEP) -MM $(CPPFLAGS) $< |sed -e 's,\($*\)\.o[ :]*,\1.o $@: Makefile ,g' >$@

ifneq ($(DEPS),)
-include $(DEPS)
endif

endif # clean
endif # format
