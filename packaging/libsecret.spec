%define have_lang 1

Name:           libsecret
Version:        0.14
Release:        0
Summary:        Library for accessing the Secret Service API
License:        LGPL-2.1+
Group:          System/Libraries
Url:            http://www.gnome.org/
Source0:        http://download.gnome.org/sources/libsecret/0.13/%{name}-%{version}.tar.xz
Source99:       baselibs.conf
BuildRequires:  docbook-xsl-stylesheets
BuildRequires:  fdupes
BuildRequires:  gettext-tools
BuildRequires:  gtk-doc
BuildRequires:  intltool
BuildRequires:  libgcrypt-devel >= 1.2.2
BuildRequires:  vala >= 0.17.2.12
BuildRequires:  xsltproc
BuildRequires:  pkgconfig(gio-2.0) >= 2.31.0
BuildRequires:  pkgconfig(gio-unix-2.0)
BuildRequires:  pkgconfig(glib-2.0) >= 2.31.0
BuildRequires:  pkgconfig(gobject-introspection-1.0) >= 1.29
Recommends:     %{name}-lang

%description
libsecret is a library for storing and retrieving passwords and other
secrets. It communicates with the "Secret Service" using DBus.

%package -n typelib-Secret
Summary:        Library for accessing the Secret Service API -- Introspection bindings
Group:          System/Libraries

%description -n typelib-Secret
libsecret is a library for storing and retrieving passwords and other
secrets. It communicates with the "Secret Service" using DBus.

This package provides the GObject Introspection bindings for libsecret.

%package -n typelib-SecretUnstable
Summary:        Library for accessing the Secret Service API -- Introspection bindings
Group:          System/Libraries

%description -n typelib-SecretUnstable
libsecret is a library for storing and retrieving passwords and other
secrets. It communicates with the "Secret Service" using DBus.

This package provides the GObject Introspection bindings for libsecret.

%package tools
Summary:        Library for accessing the Secret Service API -- Tools
Group:          System/Libraries

%description tools
libsecret is a library for storing and retrieving passwords and other
secrets. It communicates with the "Secret Service" using DBus.

%package devel
Summary:        Library for accessing the Secret Service API -- Development Files
Group:          Development/Libraries
Requires:       libsecret = %{version}
Requires:       typelib-Secret = %{version}
Requires:       typelib-SecretUnstable = %{version}

%description devel
libsecret is a library for storing and retrieving passwords and other
secrets. It communicates with the "Secret Service" using DBus.

%lang_package
%prep
%setup -q

%build
%autogen \
        --disable-static
make V=1

%install
%make_install
%find_lang %{name}
rm -rf %{buildroot}%{_datadir}/locales/*
%fdupes %{buildroot}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files 
%defattr (-, root, root)
%license COPYING
%{_libdir}/libsecret-1.so.*

%files -n typelib-Secret
%defattr(-,root,root)
%{_libdir}/girepository-1.0/Secret-1.typelib

%files -n typelib-SecretUnstable
%defattr(-,root,root)
%{_libdir}/girepository-1.0/SecretUnstable-0.typelib

%files tools
%defattr(-,root,root)
%{_bindir}/secret-tool

%files devel
%defattr (-, root, root)
%doc AUTHORS ChangeLog NEWS README
%{_libdir}/libsecret-1.so
%{_libdir}/pkgconfig/libsecret-1.pc
%{_libdir}/pkgconfig/libsecret-unstable.pc
%{_includedir}/libsecret-1/
%{_datadir}/gir-1.0/Secret-1.gir
%{_datadir}/gir-1.0/SecretUnstable-0.gir
%dir %{_datadir}/vala/vapi
%{_datadir}/vala/vapi/libsecret-1.deps
%{_datadir}/vala/vapi/libsecret-1.vapi
%{_datadir}/vala/vapi/libsecret-unstable.deps
%{_datadir}/vala/vapi/libsecret-unstable.vapi
%{_datadir}/vala/vapi/mock-service-0.vapi

