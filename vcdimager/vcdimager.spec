### autogenerated---edit .spec.in
### $Id: vcdimager.spec,v 1.2 2000-11-28 02:52:16 mikebern Exp $

%define  RELEASE 1
%define  rel     %{?CUSTOM_RELEASE} %{!?CUSTOM_RELEASE:%RELEASE}

Summary:        VideoCD (pre-)mastering tool
Name:           vcdimager
Version:        0.4
Release:        %rel
Copyright:      GPL
Group:          Applications/Multimedia
Packager:       Herbert Valerio Riedel <hvr@gnu.org>
Source:         http://www.hvrlab.dhs.org/pub/vcdimager/%{name}-%{version}/%{name}-%{version}.tar.gz
URL:            http://www.hvrlab.dhs.org/~hvr/vcdimager/
BuildRoot:      %{_tmppath}/%{name}-%{version}-root
Requires:       glib

%description
VCDImager allows you to create VideoCD BIN/CUE CD Images from mpeg files which can be burned with cdrdao or any other program capable of burning BIN/CUE files.

%prep
%setup -q

%build
rm -rf "$RPM_BUILD_ROOT"
%configure

%post
/sbin/install-info          --info-dir=%{_infodir} %{_infodir}/vcdimager.info*

%preun
/sbin/install-info --delete --info-dir=%{_infodir} %{_infodir}/vcdimager.info*

%install
rm -rf "$RPM_BUILD_ROOT"
make DESTDIR="$RPM_BUILD_ROOT" install

%clean
rm -rf "$RPM_BUILD_ROOT"

%files
%defattr(-, root, root)
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
%{_bindir}/vcdimager
%{_infodir}/vcdimager.info*
%{_mandir}/man1/*

%changelog
* Sat Aug 26 2000 Herbert Valerio Riedel <hvr@gnu.org>
- spec file improvements

* Mon Aug 14 2000 Herbert Valerio Riedel <hvr@gnu.org>
- first spec file
