Name:		srmio
Version:	0.1.0
Release:	1%{?dist}
Summary:	SRM Powercontrol V library functions

Group:		System Environment/Libraries
License:	GPLv2
URL:		http://www.zuto.de/project/srmio
#Source0:	http://www.zuto.de/project/files/srmio/srmio-${version}.tar.gz
Source0:	http://www.zuto.de/project/files/srmio/srmio-0.0.8.tar.gz
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:	libtool
#Requires:	

%description
srmio is a library to access the most important functions of a Schoberer
Radmesstechnik (SRM) PowerControl V, VI and 7. You can download the data,
mark it deleted, sync the time and set the recording interval. So
hopefully you'll get around booting windows after each exercise. Though it
is not intended as a replacement.

To be as compatible as possible, it's reading (SRM6/SRM7) and writing
(SRM7) files in a the format srmwin uses.

%package	devel
Summary:	Development files for %{name}
Group:		Development/Libraries
Requires:	%{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q

%build
#chmod u+x genautomake.sh
#./genautomake.sh
%configure --disable-static
#make %{?_smp_mflags}
make

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc README LICENSE Changes
%{_mandir}/man1/*
%{_bindir}/*
%{_libdir}/libsrmio.so.*

%files
%defattr(-,root,root,-)
%doc README LICENSE Changes
%{_mandir}/man1/*
%{_bindir}/*
%{_libdir}/*.a
%{_libdir}/*.la
%{_libdir}/libsrmio.so
%{_includedir}/*.h

%changelog

