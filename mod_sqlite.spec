%define     apxs            /usr/sbin/apxs
%define     mod_name        sqlite
%define     apache_version  2.0.40
Summary: An interface to SQLite over HTTP
Name: mod_sqlite
Version: 0.02
Release: 2
Copyright: GPL
Group: Networking/Daemons
Source: %{name}-%{version}.tar.gz
URL: http://sourceforge.net/modsqlite
BuildRequires: httpd-devel
BuildRequires: sqlite-devel
BuildRoot: /var/tmp/%{name}-buildroot

%define     _pkglibdir  %(%{apxs} -q LIBEXECDIR)

%description
mod_sqlite is an Apache module which provides access to SQLite databases over
HTTP.

%prep
%setup -q

%build
make APXS=%{apxs}

%install
rm -rf $RPM_BUILD_ROOT
install -d $RPM_BUILD_ROOT{%{_pkglibdir},%{_sysconfigdir}/httpd}
install .libs/mod_%{mod_name}.so $RPM_BUILD_ROOT%{_pkglibdir}

%clean
rm -rf $RPM_BUILD_ROOT

%post
%{apxs} -e -a -n %{mod_name} %{_pkglibdir}/mod_%{mod_name}.so 1>&2
if [ -f /var/lock/subsys/httpd ]; then
    /etc/rc.d/init.d/httpd restart 1>&2
fi

%preun
if [ "$1" = "0" ]; then
    %{apxs} -e -A -n %{mod_name} %{_pkglibdir}/mod_%{mod_name}.so 1>&2
    if [ -f /var/lock/subsys/httpd ]; then
        /etc/rc.d/init.d/httpd restart 1>&2
    fi
fi

%files
%defattr(644,root,root,755)
%doc README INSTALL COPYING

/usr/lib/httpd/modules/mod_sqlite.so

%changelog
