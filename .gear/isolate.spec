Name: isolate
Version: 2.0.7
Release: alt1

Summary: Simple isolation system
License: GPL3
Group: System/Base

Packager: Alexey Gladkov <legion@altlinux.ru>

BuildRequires: help2man

Source0: %name-%version.tar

%description
%summary

%package -n make-initrd-isolation
Summary: Containers support for make-initrd
Group: System/Base
Requires: %name = %version-%release

AutoReq: noshell, noshebang

%description -n make-initrd-isolation
%summary


%prep
%setup -q


%build
%make_build


%install
%make_install DESTDIR=%buildroot install


%files
%dir %_sysconfdir/isolate
%dir %_sysconfdir/isolate/system
%config(noreplace) %_sysconfdir/isolate/config.ini
%config(noreplace) %_sysconfdir/isolate/system/*
%_bindir/*
%_sbindir/*
%_localstatedir/*


%files -n make-initrd-isolation
%_datadir/make-initrd/features/*

%changelog
