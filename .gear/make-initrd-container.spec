Name: make-initrd-container
Version: 2.0.7
Release: alt1

Summary: Containers support for make-initrd
License: GPL3
Group: System/Base

Packager: Alexey Gladkov <legion@altlinux.ru>

BuildRequires: help2man

Source0: %name-%version.tar

%description
%summary

%prep
%setup -q

%build
%make_build

%install
%make_install DESTDIR=%buildroot install

%files
%dir %_sysconfdir/container
%dir %_sysconfdir/container/system
%config(noreplace) %_sysconfdir/container/config.ini
%config(noreplace) %_sysconfdir/container/system/*
%_sbindir/*
%_localstatedir/*

%changelog
