%{!?disable_offload:%define disable_offload 0}
%{!?disable_toecore:%define disable_toecore 0}
%{!?disable_bonding:%define disable_bonding 0}
%{!?release:%define release 0}
%{!?kversion:%define kversion %(uname -r)}
%define kerver %(echo -n %{kversion} | sed -e 's/-/_/g')

## Summary offload string define.
%if %{disable_offload}
%define offload ""
%else
%define offload "Offload "
%endif
Summary: Chelsio Terminator 4 %{offload}driver for Linux
Name:    %{name}
Version: %{version}
Release: %{kerver}
License: GPL
Group:   System Environment/Kernel
URL:     http://www.chelsio.com
Vendor:  Chelsio Communications, Inc.
Packager:Chelsio Communications, Inc. <http://www.chelsio.com>
Provides: %{name}-%{version}
requires: chelsio-series4-firmware > 1.1.0.10

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-%{kversion}-root
#BuildRequires: kernel-devel
ExclusiveArch: %{ix86} x86_64 ia64 ppc ppc64 ppc64le powerpc aarch64
ExclusiveOS: linux

%if %{disable_offload}
%define conflict_name %{name}toe
%else
%define conflict_name %(echo %{name} | %{__sed} 's/toe//')
%endif

%define drvbase /lib/modules/%{kversion}/updates
%define fwdir /lib/firmware/cxgb4
%define rpmfiles %{_topdir}/BUILD/%{name}-%{version}/rpmfiles.txt
%define ptpbin /usr/local/sbin
%define ptpman /usr/local/man/man8

%description
The Chelsio Terminator 4 Ethernet Adapter driver for Linux kernel [Non-offload] (%{kversion}).

%prep
%{__mkdir} -p %{name}-%{version}/cxgb4/
%{__cp} -a %{srcdir}/cxgb4/cxgb4.ko %{name}-%{version}/cxgb4/
echo "%{drvbase}/drivers/net/cxgb4/cxgb4.ko" > %{rpmfiles}

if [[ %{setptp} -eq 1 ]] ; then
  %{__mkdir} -p %{name}-%{version}/ptp/bin/
  %{__mkdir} -p %{name}-%{version}/ptp/man/
  for p in ptp4l pmc phc2sys hwstamp_ctl phc_ctl timemaster ; do
    if [[ -f %{ptpsrcdir}/${p} ]] ; then
      %{__cp} -a %{ptpsrcdir}/${p} %{name}-%{version}/ptp/bin/
      echo "%{ptpbin}/${p}" >> %{rpmfiles}
    fi 
    if [[ -f %{ptpsrcdir}/${p}.8 ]] ; then
      %{__cp} -a %{ptpsrcdir}/${p}.8 %{name}-%{version}/ptp/man/
      echo "%{ptpman}/${p}.8" >> %{rpmfiles}
    fi
  done
fi

%build
## Nothing to do here.

%pre

%post
## Workaround for auto-loading infiniband drivers.
file=/etc/modprobe.d/libcxgb4.conf
lines=`grep -n "^install cxgb4 " $file 2>/dev/null | sed 's/:.*//g' | sort -gr`
string="# Disabled by Chelsio Makefile on `date`"
for i in $lines; do
  sed -i "$i"'s/^install cxgb4\s/#install cxgb4 /' $file
  let i-=1
  sed -i "$i"'a'"$string" $file
done
## Generate new module dependencies.
depmod
exit 0

%postun
## Workaround for auto-loading infiniband drivers.
file=/etc/modprobe.d/libcxgb4.conf
string="# Disabled by Chelsio Makefile"
lines=`grep -n "^$string" $file 2>/dev/null | sed 's/:.*//g' | sort -gr`
for i in $lines; do
  sed -i "$i"'d' $file
  sed -i "$i"'s/^#//' $file
done
## Update module dependencies.
depmod
exit 0

%install
cd %{_topdir}/BUILD/%{name}-%{version}
%{__install} -D -v cxgb4/cxgb4.ko %{buildroot}/%{drvbase}/drivers/net/cxgb4/cxgb4.ko
%{__rm} -f /etc/modprobe.d/chelsio.conf > /dev/null
if [[ %{setptp} -eq 1 ]] ; then
 %{__mkdir} -p %{buildroot}/%{ptpbin}/
 %{__mkdir} -p %{buildroot}/%{ptpman}/
  for p in ptp4l pmc phc2sys hwstamp_ctl phc_ctl timemaster ; do
    if [[ -f ptp/bin/${p} ]] ; then
      %{__install} -D -v -m 755 ptp/bin/${p} %{buildroot}/%{ptpbin}/
    fi 
    if [[ -f ptp/man/${p}.8 ]] ; then
      %{__install} -D -v -m 644 ptp/man/${p}.8 %{buildroot}/%{ptpman}/
    fi
  done
fi

%if %{debug_enable}
 %debug_package
%endif


%files -f %{_builddir}/%{name}-%{version}/rpmfiles.txt
%defattr(744,root,root)

%clean
%{__rm} -rf %{buildroot}

%changelog
