Name:		srmio
Version:	0.1.1~git1
Release:	1%{?dist}
Summary:	SRM Powercontrol library functions

Group:		System Environment/Libraries
License:	GPLv2
URL:		http://www.zuto.de/project/srmio
Source0:	http://www.zuto.de/project/files/srmio/srmio-%{version}.tar.gz
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:	coreutils libtool
Requires:	libsrmio = %{version}-%{release}

%description
Downloads from PowerControl or reads SRM files

%package	-n libsrmio
Summary:	Library files for %{name}
Group:		Development/Libraries
Requires:	libsrmio = %{version}-%{release}

%description    -n libsrmio
Library to use to talk to PowerControl SRM.

%package	-n libsrmio-devel
Summary:	Development files for %{name}
Group:		Development/Libraries
Requires:	libsrmio = %{version}-%{release}

%description    -n libsrmio-devel
The libsrmio-devel package contains libraries and header files for
developing applications that use libsrmio.

%prep
%setup -q -n srmio-0.1.1

%build
#chmod u+x genautomake.sh
#./genautomake.sh
%configure --disable-static
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install
rm -f ${RPM_BUILD_ROOT}%{_libdir}/libsrmio.la

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc README LICENSE Changes
%{_mandir}/man1/*
%{_bindir}/*

%files -n libsrmio
%defattr(-,root,root,-)
%doc README LICENSE Changes
%{_libdir}/libsrmio.so.*

%files -n libsrmio-devel
%defattr(-,root,root,-)
%{_libdir}/libsrmio.so
%{_includedir}/*.h

%changelog
* Sat Mar 21 2012 Gareth Coco <garethcoco@gmail.com>
- First release (srmio 0.1.0)
