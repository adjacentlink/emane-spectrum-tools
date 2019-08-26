AC_COPYRIGHT([
 Copyright (c) 2019 - Adjacent Link LLC, Bridgewater, New Jersey
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 
 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.
 * Neither the name of Adjacent Link LLC nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.

 See toplevel COPYING for more information.
])

AC_INIT([emane-spectrum-tools],0.2.1,[labs at adjacentlink dot com])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([src])

AM_INIT_AUTOMAKE([1.9.6])

AC_PROG_CXX

AC_PROG_CXXCPP

AX_CXX_COMPILE_STDCXX_11([noext],[mandatory])

AC_PROG_INSTALL

AC_PROG_LIBTOOL

AC_LANG([C++])

AC_ARG_WITH(profile,
           [  --with-profile          add profiling support (off)])

AC_ARG_WITH(debug,
           [  --with-debug            add debug support (off)])


AC_ARG_WITH([verbose-logging],
           [  --without-verbose-logging  disable verbose logging support (on)],
           [],
           [with_verbose_logging=yes])

AC_ARG_VAR([emane_SRC_ROOT],[emane source root directory.])

PKG_CHECK_MODULES([libemane], libemane)
PKG_CHECK_MODULES([protobuf],protobuf)
PKG_CHECK_MODULES([libzmq],libzmq)

AM_PATH_PYTHON([2.7])

AS_IF([test -n "${emane_SRC_ROOT}"],[] ,
AC_MSG_ERROR("Missing emane_SRC_ROOT. emane-spectrum-tools requires
internal emulator headers. Please set the environment variable
emane_SRC_ROOT to the top level emane src root directory."))

_cppflags=${CPPFLAGS}
_ldflags=${LDFLAGS}

# Checks emane_SRC_ROOTis usuable
CPPFLAGS="${libemane_CFLAGS} -I${emane_SRC_ROOT}/src/libemane ${CPPFLAGS}"
LDFLAGS="${libemane_LIBS} ${LDFLAGS}"

AC_CHECK_HEADER("${emane_SRC_ROOT}/src/libemane/spectrummonitor.h",
 [],
 [AC_MSG_ERROR("Missing ${emane_SRC_ROOT}/src/libemane/spectrummonitor.h header")])

CPPFLAGS=${_cppflags}
LDFLAGS=${_ldflags}

# compiler options
CXXFLAGS="$CXXFLAGS -W -Wall -O3"

# options for use with gprof
if test "$with_profile" = "yes"
then
CPPFLAGS="$CPPFLAGS -g -pg"
LDFLAGS="$LDFLAGS -g -pg"
fi

# options for use with debug
if test "$with_debug" = "yes"
then
CPPFLAGS="$CPPFLAGS -g -O0"
LDFLAGS="$LDFLAGS -g -O0"
fi

# options for use with verbose logging
if test "$with_verbose_logging" = "yes"
then
CPPFLAGS="$CPPFLAGS -DVERBOSE_LOGGING"
fi

LANG=C
AC_SUBST(LANG)

# check for rpmbuild
AC_CHECK_PROG(HAVE_RPMBUILD, rpmbuild, true, false)
AM_CONDITIONAL(HAVE_RPMBUILD,$HAVE_RPMBUILD)

# check for dh_clean (deb distro)
AC_CHECK_PROG(HAVE_DEB, dh_clean, true, false)
AM_CONDITIONAL(HAVE_DEB,$HAVE_DEB)

AC_CHECK_FILE(/etc/debian_version,[DEBIAN_VERSION=`cat /etc/debian_version | awk -F/ '{print $1}'`],)
AC_SUBST(DEBIAN_VERSION)

AC_SUBST(DATE_RFC2822,`date -R`)


AC_OUTPUT(
 Makefile
 src/Makefile
 src/emane-spectrum-monitor/Makefile
 src/libemane-spectrum-monitor/Makefile
 src/python/Makefile
)
