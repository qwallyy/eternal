Name:           eternal
Version:        0.1.0
Release:        1%{?dist}
Summary:        A modern Wayland compositor with scrollable tiling and animations
License:        MIT
URL:            https://github.com/eternal-wm/eternal
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc-c++ >= 12
BuildRequires:  ninja-build
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  pkgconfig(wlroots-0.18) >= 0.18.0
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(libinput)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(xcb)

Requires:       wayland >= 1.22
Requires:       wlroots-0.18 >= 0.18.0
Requires:       libxkbcommon >= 1.5
Requires:       libinput >= 1.23
Requires:       pixman >= 0.42
Requires:       mesa-libEGL
Requires:       mesa-libGLES

Recommends:     xorg-x11-server-Xwayland

%description
Eternal is a modern, high-performance Wayland compositor featuring
scrollable tiling (Niri-style), dwindle/master-stack layouts, spring
animations, IPC control, plugin system, and comprehensive window rules.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DETERNAL_ENABLE_XWAYLAND=ON \
    -DETERNAL_ENABLE_IPC=ON \
    -DETERNAL_ENABLE_PLUGINS=ON \
    -DETERNAL_ENABLE_SCREENCOPY=ON \
    -DETERNAL_BUILD_CLI=ON

%cmake_build

%install
%cmake_install

# Install config file
install -Dm644 config/eternal.kdl %{buildroot}%{_sysconfdir}/eternal/eternal.kdl

# Install desktop entry for display managers
install -Dm644 packaging/eternal.desktop %{buildroot}%{_datadir}/wayland-sessions/eternal.desktop

# Install man page
install -Dm644 docs/eternal.1 %{buildroot}%{_mandir}/man1/eternal.1

%check
cd %{__cmake_builddir}
ctest --output-on-failure

%files
%license LICENSE
%doc README.md
%{_bindir}/eternal
%{_bindir}/eternalctl
%{_bindir}/eternal-session
%config(noreplace) %{_sysconfdir}/eternal/eternal.kdl
%{_datadir}/wayland-sessions/eternal.desktop
%{_datadir}/eternal/
%{_mandir}/man1/eternal.1*

%changelog
* Sun Mar 08 2026 Eternal Developers <eternal@example.com> - 0.1.0-1
- Initial RPM package
