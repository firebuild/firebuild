
Name:           firebuild
Version:        0.8.4
Release:        1%{?dist}
Summary:        Automatic build accelerator cache

License:        Proprietary
URL:            https://firebuild.com
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.13
BuildRequires:  gcc >= 10
BuildRequires:  gcc-c++ >= 10
BuildRequires:  clang
BuildRequires:  libconfig-devel
BuildRequires:  jemalloc-devel
# TODO(rbalint) package https://github.com/Tessil/hopscotch-map
# BuildRequires:  tsl-hopscotch-map-devel
BuildRequires:  xxhash-devel >= 0.8
BuildRequires:  python3-jinja2
BuildRequires:  libxslt
BuildRequires:  docbook-style-xsl
BuildRequires:  bash-completion
BuildRequires:  bc
BuildRequires:  moreutils
BuildRequires:  bats
BuildRequires:  dash
BuildRequires:  glibc-static
BuildRequires:  po-debconf

Requires:       %{name}-libs%{?_isa} = %{version}-%{release}
Suggests:       nodejs-d3

%description
It works by caching the outputs of executed commands and replaying the results when the same commands are executed with the same parameters within the same environment.
.
The commands can be compilation or other build artifact generation steps, tests, or any command that produces predictable output. The commands to cache and replay from the cache are determined automatically based on firebuild's configuration and each command's and its children's observed behavior.
.
Firebuild supports caching compilation results of C, C++, Fortran, Java, Rust, Scala and other compilers and outputs of scripts written in Bash, Perl, Python and other interpreted languages.

%package libs
Summary:        Automatic build accelerator cache - shared library
License:        Proprietary

%description libs
It works by caching the outputs of executed commands and replaying the results when the same commands are executed with the same parameters within the same environment.
.
The commands can be compilation or other build artifact generation steps, tests, or any command that produces predictable output. The commands to cache and replay from the cache are determined automatically based on firebuild's configuration and each command's and its children's observed behavior.
.
Firebuild supports caching compilation results of C, C++, Fortran, Java, Rust, Scala and other compilers and outputs of scripts written in Bash, Perl, Python and other interpreted languages.
.
This package provides the shared library preloaded by the intercepted processes.

%prep
%setup -q

%build
%cmake -DCMAKE_INSTALL_SYSCONFDIR=/etc .
%cmake_build

%check
make -C %__cmake_builddir check

%install
%cmake_install

%files
%license LICENSE.md
%doc README.md
%{_bindir}/firebuild
%{_datadir}
%{_sysconfdir}/firebuild.conf

%files libs
%license LICENSE.md
%{_libdir}/libfirebuild.so*

%changelog
* Thu Jun 19 2025 Balint Reczey <balint@balintreczey.hu> - 0.8.4-1
- Initial RPM release
