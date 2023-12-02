#
# ***** BEGIN LICENSE BLOCK *****
# 
# Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren
# Copyright (C) 2017-2019 Olof Hagsand
# Copyright (C) 2020-2023 Olof Hagsand and Rubicon Communications, LLC
#
# This file is part of CLIXON
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 3 or later (the "GPL"),
# in which case the provisions of the GPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of the GPL, and not to allow others to
# use your version of this file under the terms of Apache License version 2, 
# indicate your decision by deleting the provisions above and replace them with
# the notice and other provisions required by the GPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the Apache License version 2 or the GPL.
#
# ***** END LICENSE BLOCK *****
#

# To rebuild the `configure' script from this, execute the command
# 	autoconf
# in the directory containing this script.
#

AC_INIT
AC_CONFIG_SRCDIR([clixon_util_xml.c])

# Default CFLAGS and INSTALLFLAGS unless set by environment
: ${INSTALLFLAGS="-s"}
: ${LINKAGE=dynamic}

# where autotool scripts are: install-sh, config.sub, config.guess
AC_CONFIG_AUX_DIR(config-aux)

# Debug flag
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug],[Build with debug symbols, default: no]),[
	  if test "$enableval" = no; then
	      ac_enable_debug=no
	  else	      
	      ac_enable_debug=yes
          fi
        ],
	[ ac_enable_debug=no])

AC_MSG_RESULT(debug is $ac_enable_debug)	
if test "$ac_enable_debug" = "yes"; then
   : ${CFLAGS="-g -Wall"}
   INSTALLFLAGS=""	
else
   : ${CFLAGS="-O2 -Wall"}
fi

AC_CONFIG_HEADERS([include/clixon_config.h])


# defines: target_cpu, target_vendor, and target_os. 
AC_CANONICAL_TARGET

# Used in expansion in, e.g., example/main/example.xml.in
AC_SUBST(BINDIR)
AC_SUBST(DATADIR)
AC_SUBST(INCLUDEDIR)
AC_SUBST(LIBDIR)
AC_SUBST(LIBEXECDIR)
AC_SUBST(LOCALSTATEDIR)
AC_SUBST(SBINDIR)
AC_SUBST(SYSCONFDIR)

AC_SUBST(INSTALLFLAGS)
AC_SUBST(CLIXON_DEFAULT_CONFIG)
AC_SUBST(LIBXML2_CFLAGS)
#
AC_PROG_CC()
AC_PROG_CPP
AC_PROG_CXX

CPPFLAGS="-DHAVE_CONFIG_H ${CPPFLAGS}"

AC_MSG_RESULT(compiler is $CC)	

AC_MSG_RESULT(CPPFLAGS is $CPPFLAGS)	
AC_MSG_RESULT(CFLAGS is $CFLAGS)
AC_MSG_RESULT(LDFLAGS is $LDFLAGS)
AC_MSG_RESULT(INSTALLFLAGS is $INSTALLFLAGS)	

# SSH binary path for client and test
AC_PATH_PROG(SSH_BIN, ssh)
AC_DEFINE_UNQUOTED(SSH_BIN, "${SSH_BIN}", [SSH binary])

# SSHD binary path for test
AC_PATH_PROG(SSHD_BIN, sshd)
AC_DEFINE_UNQUOTED(SSHD_BIN, "${SSHD_BIN}", [SSHD binary])

if test "$prefix" = "NONE"; then
     prefix=${ac_default_prefix}
fi

if test "$exec_prefix" = "NONE"; then
     exec_prefix=${prefix}
fi

# Expand for easy replacement in example/main/example.xml.in
# Special case is $libdir, which is composed from $exec_prefix
BINDIR=`eval echo $bindir`
SBINDIR=`eval echo $sbindir`
SYSCONFDIR=`eval echo $sysconfdir`
DATADIR=`eval echo \`eval echo $datadir\``
INCLUDEDIR=`eval echo $includedir`
LIBDIR=`eval echo \`eval echo $libdir\``
LIBEXECDIR=`eval echo $libexecdir`
LOCALSTATEDIR=`eval echo $localstatedir`

# This is for cligen
AC_ARG_WITH([cligen], [AS_HELP_STRING([--with-cligen=dir], [Use CLIGEN installation in this dir])], [
  CLIGEN_DIR="$withval"
])
AC_SUBST(CLIGEN_DIR)
if test -n "${CLIGEN_DIR}" -a -d "${CLIGEN_DIR}"; then
    echo "Using CLIGEN here: ${CLIGEN_DIR}"
    CPPFLAGS="-I${CLIGEN_DIR}/include ${CPPFLAGS}"
    LDFLAGS="-L${CLIGEN_DIR}/lib ${LDFLAGS}"
fi

# This is for clixon
AC_ARG_WITH(clixon,  [  --with-clixon=dir       Use Clixon here ] )
if test "${with_clixon}"; then
   echo "Using Clixon here: ${with_clixon}"
   CPPFLAGS="-I${with_clixon}/include ${CPPFLAGS}"
   LDFLAGS="-L${with_clixon}/lib ${LDFLAGS}"
   test -d "$with_clixon" && CLIXON_PREFIX="$with_clixon"
fi
   
# Check curl, needed for tests but not for clixon core
AC_CHECK_HEADERS(curl/curl.h,[])
AC_CHECK_LIB(curl, curl_global_init)

AC_CHECK_HEADERS(nghttp2/nghttp2.h,[], AC_MSG_ERROR([nghttp2 missing]))
AC_CHECK_LIB(nghttp2, nghttp2_session_server_new,, AC_MSG_ERROR([nghttp2 missing]))

AC_CHECK_LIB(ssl, OPENSSL_init_ssl ,, AC_MSG_ERROR([libssl missing]))
AC_CHECK_LIB(crypto, CRYPTO_new_ex_data, , AC_MSG_ERROR([libcrypto missing])) 

AC_CHECK_HEADERS(cligen/cligen.h,, AC_MSG_ERROR(cligen missing. Try: git clone https://github.com/clicon/cligen.git))

AC_CHECK_LIB(cligen, cligen_init,, AC_MSG_ERROR([CLIgen missing. Try: git clone https://github.com/clicon/cligen.git]))

AC_CHECK_HEADERS(clixon/clixon.h,, AC_MSG_ERROR(Clixon missing. Try: git clone https://github.com/clicon/clixon.git), [[#include <signal.h>
#include <cligen/cligen.h>]])
AC_CHECK_LIB(clixon, clicon_log_init,, AC_MSG_ERROR([Clixon missing. Try: git clone https://github.com/clicon/clixon.git]),)

# This is for libxml2 XSD regex engine
# Note this only enables the compiling of the code. In order to actually
# use it you need to set Clixon config option CLICON_YANG_REGEXP to libxml2
AC_ARG_WITH([libxml2],
	[AS_HELP_STRING([--with-libxml2[[=/path/to/xml2-config]]],[Use libxml2 regex engine])])
if test "${with_libxml2}"; then
   # Find libxml2 lib
   if test -x "${with_libxml2}" 2>/dev/null; then
      AC_MSG_CHECKING([for libxml2 header files and libs])
      LIBXML2_CFLAGS="`${with_libxml2} --cflags`"
      LIBS="$LIBS `${with_libxml2} --libs`"
      AC_MSG_RESULT([$LIBXML2_CFLAGS])
   else
      LIBXML2_CFLAGS="-I/usr/include/libxml2"
   fi
   AC_CHECK_LIB(xml2, xmlRegexpCompile,[], AC_MSG_ERROR([libxml2 not found]))
fi 

# Set default config file location
CLIXON_DEFAULT_CONFIG=/usr/local/etc/clixon.xml
AC_ARG_WITH([configfile],
	    [AS_HELP_STRING([--with-configfile=FILE],[Set default path to config file])],
	    [CLIXON_DEFAULT_CONFIG="$withval"],)

# Default location for config file
AC_DEFINE_UNQUOTED(CLIXON_DEFAULT_CONFIG,"${CLIXON_DEFAULT_CONFIG}",[Location for apps to find default config file])

AC_DEFINE_UNQUOTED(CLIXON_CONFIG_BINDIR, "${BINDIR}", [Pass-through $bindir])
AC_DEFINE_UNQUOTED(CLIXON_CONFIG_LOCALSTATEDIR, "${LOCALSTATEDIR}", [Pass-through $localstatedir])
AC_DEFINE_UNQUOTED(CLIXON_CONFIG_SBINDIR, "${SBINDIR}", [Pass-through $sbindir])
AC_DEFINE_UNQUOTED(CLIXON_CONFIG_SYSCONFDIR, "${SYSCONFDIR}", [Pass-through $sysconfdir])

test "x$prefix" = xNONE && prefix=$ac_default_prefix

AC_CONFIG_FILES([Makefile
])
AC_OUTPUT
