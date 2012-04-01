#!/bin/bash

echo ""
echo "### FFdecsa optimization helper/benchmark "
echo "### Version 9d"
echo ""
echo ""

#   gcc-2.95   = i386, i486, i586,pentium, i686,pentiumpro, k6
#   gcc-3.0   += athlon
#   gcc-3.1   += pentium-mmx, pentium2, pentium3, pentium4, k6-2, k6-3, athlon-{tbird, 4,xp,mp}
#   gcc-3.3   += winchip-c6, winchip2, c3
#   gcc-3.4.0 += k8,opteron,athlon64,athlon-fx, c3-2
#   gcc-3.4.1 += pentium-m, pentium3m, pentium4m, prescott, nocona
#   gcc-4.3   += core2, amdfam10, geode
#   gcc-4.5   += atom
#   gcc-4.6   += corei7, corei7-avx, bdver1, btver1


# customize here
FFdecsaDIR="../FFdecsa"
OwnFlags=""

# compiler
CC=g++

# some initial values
OLevel=""
PMode=""
MTimes=10

_FLAGS=""
_CPU=""
_VERB=1
_SHORT=1
_NOTEST=0
_CANNAT=0
_NONAT=0
_DONAT=0

print_usage() {
  echo ""
  echo "Usage: $0 [OPTION [ARG]]..."
  echo ""
  echo "Options:"
  echo "-D [PATH]     path to FFdecsa source directory"
  echo "-O [LEVEL]    set custom optimization level e.g. S, 2 or 3"
  echo "-P [MODE]     test only the given PARALLEL_MODE"
  echo "-T [N]        number of tests per mode"
  echo "-e            extended - test all PARALLEL_MODEs"
  echo "-i            print system info - do not perform FFdecsa test"
  echo "-n            disable \"native\" flags if gcc/g++ >=4.2"
  echo "-q            be quiet - only results, warnings and errors"
  echo "-h            this output"
  echo ""
}


while getopts "D:O:P:T:einqh" options; do
  case $options in
    D ) if [ -d $OPTARG ]; then
          FFdecsaDIR=$OPTARG
        fi
        ;;
    O ) if [ -n $OPTARG ]; then
          OLevel=$OPTARG
        fi
        ;;
    P ) if [ -n $OPTARG ]; then
          PMode=$OPTARG
        fi
        ;;
    T ) if [[ $OPTARG =~ ^[0-9]+$ ]]; then
          MTimes=$OPTARG
        fi
        ;;
    e ) _SHORT=0
        ;;
    i ) _NOTEST=1
        ;;
    n ) _NONAT=1
        ;;
    q ) _VERB=0
        ;;
    h ) print_usage
        exit 0
        ;;
   \? ) print_usage
        exit 1;;
  esac
done

if [ "$OLevel" != "" ]; then
  case $OLevel in
    S|s) OptFlag="-Os";;
    0)   OptFlag="-O0";;
    1)   OptFlag="-O1";;
    2)   OptFlag="-O2";;
    3)   OptFlag="-O3";;
    *)   OptFlag="-O2";;
  esac
fi


source_check() {
  testpath=$1
  if [ -f "${testpath}/FFdecsa.c" ] && [ -f "${testpath}/FFdecsa_test.c" ]; then
    return 0
  else
    return 1
  fi
}


if [ $_NOTEST -lt 1 ]; then

  if [ ! -d $FFdecsaDIR ] ;then
    echo "Warning: No valid path specified"
    echo "Searching..."
    echo ""
    SRCPaths="/usr/src /usr/local/src /home"
    PathFound=0

    for vpth in $SRCPaths; do
      FFtmpDIR="`find $vpth -name 'FFdecsa' -type d | grep -v -m 1 contrib`"
      if [ ! -z $FFtmpDIR ]; then
        FFdecsaDIR=$FFtmpDIR
        PathFound=1
      fi
    done

    if [ $PathFound -eq 0 ] ;then
      echo "Error: FFdecsa not found!"
      echo "Please specify the correct path to FFdecsa source directory using option \"-D\""
      echo "FFdecsa test disabled!"
      echo ""
      _NOTEST=1
    else
      echo "Found possible FFdecsa source at:"
      echo "$FFdecsaDIR"
      if source_check "$FFdecsaDIR"; then
        echo "...and source files looks valid."
        echo "proceed..."
        echo ""
        echo ""
      else
        echo "...but no valid source files found."
        echo "FFdecsa test disabled!"
        echo ""
        echo ""
        _NOTEST=1
      fi
    fi

  elif ! source_check "$FFdecsaDIR"; then
        echo ""
        echo "Error: No valid FFdecsa source files found at:"
        echo "$FFdecsaDIR"
        echo "FFdecsa test disabled!"
        echo ""
        _NOTEST=1
  fi

fi

[ -z "$CC" ] && CC=g++

try_gcc_options() {
    $CC $* -S -o /dev/null -xc /dev/null >/dev/null 2>&1
}

if ! try_gcc_options; then
    echo "Error: Couldn't execute your compiler ($CC)" >&2
    exit 1
fi

if try_gcc_options -march=i386; then
    is_x86_64=0
else
    is_x86_64=1
fi

try_line() {
    skip=0
    for arch in $1; do
        if try_gcc_options -march=$arch; then
            echo $arch
            return
        fi
    done
    return 1
}

read_cpu_data_linux() {
    IFS=":"
    while read name value; do
        unset IFS
        name=`echo $name` #strip spaces
        if [ "$name" != "flags" ]; then
            value=`echo $value | sed 's/\([^ ]*\).*/\1/'` #take first word
        fi
        IFS=":"
        if [ "$name" = "vendor_id" ]; then
            vendor_id="$value"
        elif [ "$name" = "cpu family" ]; then
            cpu_family="$value"
        elif [ "$name" = "model" ]; then
            cpu_model="$value"
        elif [ "$name" = "flags" ]; then
            flags="$value"
            break #flags last so break early
        fi
    done < /proc/cpuinfo
    unset IFS
}

read_cpu_data() {
    # Default values
    vendor_id="NotFound"
    cpu_family="-1"
    cpu_model="-1"
    flags=""
    read_cpu_data_linux
}


### gcc arch detection

find_cpu_flags() {
if [ "$vendor_id" = "AuthenticAMD" ]; then
    if [ $cpu_family -eq 4 ]; then
        _CPU="i486"
    elif [ $cpu_family -eq 5 ]; then
        if [ $cpu_model -lt 4 ]; then
            _CPU="pentium"
        elif [ \( $cpu_model -eq 6 \) -o \( $cpu_model -eq 7 \) ]; then
            _CPU="k6"
        elif [ \( $cpu_model -eq 8 \) -o \( $cpu_model -eq 12 \) ]; then
            line="k6-2 k6"
        elif [ \( $cpu_model -eq 9 \) -o \( $cpu_model -eq 13 \) ]; then
            line="k6-3 k6-2 k6"
        elif [ $cpu_model -eq 10 ]; then #geode LX
            line="geode k6-2 k6"
            #The LX supports 3dnowext in addition to the k6-2 instructions,
            #however gcc doesn't support explicitly selecting that.
        fi
    elif [ $cpu_family -eq 6 ]; then
        if [ $cpu_model -le 3 ]; then
            line="athlon k6-3 k6-2 k6"
        elif [ $cpu_model -eq 4 ]; then
            line="athlon-tbird athlon k6-3 k6-2 k6"
        elif [ $cpu_model -ge 6 ]; then #athlon-{4,xp,mp} (also geode NX)
            line="athlon-4 athlon k6-3 k6-2 k6"
        fi
    elif [ $cpu_family -eq 15 ]; then #k8,opteron,athlon64,athlon-fx
        line="k8 athlon-4 athlon k6-3 k6-2 k6"
    elif [ $cpu_family -eq 16 ] ||    #barcelona,amdfam10
         [ $cpu_family -eq 17 ]; then #griffin
        line="amdfam10 k8 athlon-4 athlon k6-3 k6-2 k6"
    elif [ $cpu_family -eq 20 ]; then #bobcat
        line="btver1 amdfam10 k8 athlon-4 athlon k6-3 k6-2 k6"
    elif [ $cpu_family -eq 21 ]; then #bulldozer
        line="bdver1 btver1 amdfam10 k8 athlon-4 athlon k6-3 k6-2 k6"
    fi
elif [ "$vendor_id" = "CentaurHauls" ]; then
    if [ $cpu_family -eq 5 ]; then
        if [ $cpu_model -eq 4 ]; then
            line="winchip-c6 pentium"
        elif [ $cpu_model -eq 8 ]; then
            line="winchip2 winchip-c6 pentium"
        elif [ $cpu_model -ge 9 ]; then
            line="winchip2 winchip-c6 pentium" #actually winchip3 but gcc doesn't support this currently
        fi
    elif [ $cpu_family -eq 6 ]; then
        if echo "$flags" | grep -q cmov; then
            fallback=pentiumpro
        else
            fallback=pentium #gcc incorrectly assumes i686 always has cmov
        fi
        if [ $cpu_model -eq 6 ]; then
            _CPU="pentium" # ? Cyrix 3 (samuel)
        elif [ $cpu_model -eq 7 ] || [ $cpu_model -eq 8 ]; then
            line="c3 winchip2 winchip-c6 $fallback"
        elif [ $cpu_model -ge 9 ]; then
            line="c3-2 c3 winchip2 winchip-c6 $fallback"
        fi
    fi
elif [ "$vendor_id" = "GenuineIntel" ]; then
    if [ $cpu_family -eq 3 ]; then
        _CPU="i386"
    elif [ $cpu_family -eq 4 ]; then
        _CPU="i486"
    elif [ $cpu_family -eq 5 ]; then
        if [ $cpu_model -ne 4 ]; then
            _CPU="pentium"
        else
            line="pentium-mmx pentium" #No overlap with other vendors
        fi
    elif [ $cpu_family -eq 6 ]; then
        if [ \( $cpu_model -eq 0 \) -o \( $cpu_model -eq 1 \) ]; then
            _CPU="pentiumpro"
        elif [ \( $cpu_model -ge 3 \) -a \( $cpu_model -le 6 \) ]; then #4=TM5600 at least
            line="pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ \( $cpu_model -eq 9 \) -o \( $cpu_model -eq 13 \) ]; then #centrino
            line="pentium-m pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ \( $cpu_model -eq 14 \) ]; then #Core
            line="prescott pentium-m pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ \( $cpu_model -eq 28 \) -o \( $cpu_model -eq 38 \) ]; then #Atom
            line="atom core2 pentium-m pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ $cpu_model -eq 26 ] ||
             [ $cpu_model -eq 30 ] ||
             [ $cpu_model -eq 31 ] ||
             [ $cpu_model -eq 46 ] ||
             # ^ Nehalem ^
             [ $cpu_model -eq 37 ] ||
             [ $cpu_model -eq 44 ] ||
             [ $cpu_model -eq 47 ]; then
             # ^ Westmere ^
            line="corei7 core2 pentium-m pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ $cpu_model -eq 42 ]; then #Sandy Bridge
            line="corei7-avx corei7 core2 pentium-m pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ $cpu_model -eq 15 ] ||
             # ^ Merom ^
             [ $cpu_model -eq 23 ] ||
             [ $cpu_model -eq 29 ]; then
             # ^ Penryn ^
            line="core2 pentium-m pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        elif [ \( $cpu_model -ge 7 \) -a \( $cpu_model -le 11 \) ]; then
            line="pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        fi
    elif [ $cpu_family -eq 15 ]; then
        line="pentium4 pentium3 pentium2 pentiumpro pentium-mmx pentium i486 i386"
        if [ $cpu_model -ge 3 ]; then
            line="prescott $line"
        fi
    fi
elif [ "$vendor_id" = "Geode" ]; then #by NSC
    if [ $cpu_family -eq 5 ]; then
        if [ \( $cpu_model -eq 4 \) -o \( $cpu_model -eq 9 \) ]; then
            # Note both models 4 and 9 have cmov.
            # In addition, model 9 has cxmmx.
            # Note also, the "geode" gcc arch is for newer AMD geode cores
            # and is not appropriate for this older core.
            line="pentium-mmx pentium"
        fi
    fi
fi

if [ \( -z "$_CPU" \) -a \( -z "$line" \) ]; then
    echo "\
Unrecognised CPU.
  Vendor:$vendor_id family:$cpu_family model:$cpu_model
  flags:$flags" >&2
    exit 1
fi

[ -z "$_CPU" ] && _CPU="`try_line "$line"`"

if echo "$_CPU" | grep -q "amdfam10"; then
    if echo "$flags" | grep -q "sse5"; then
        if try_gcc_options "-msse5"; then #gcc >= 4.3
            _FLAGS=" -msse5"
        fi
    fi
elif echo "$_CPU" | grep -E -q "(k8|c3-2)"; then
    if echo "$flags" | grep -q "sse3"; then
        if try_gcc_options "-msse3"; then #gcc >= 3.3.3
            _FLAGS=" -msse3"
        fi
    fi
elif echo "$_CPU" | grep -q "core2"; then
    if echo "$flags" | grep -q "sse4_2"; then
        if try_gcc_options "-msse4"; then #gcc >= 4.3
            _FLAGS=" -msse4"
        fi
    elif echo "$flags" | grep -q "sse4_1"; then
        if try_gcc_options "-msse4.1"; then #gcc >= 4.3
            _FLAGS=" -msse4.1"
        fi
    fi
fi

}


read_cpu_data

# look if "native" flag is supported
if try_gcc_options "-march=native"; then
  _CANNAT=1
fi

if [ "$OwnFlags" == "" ]; then
    find_cpu_flags
else
  _NONAT=1
  if try_gcc_options $OwnFlags; then
    echo "Custom compiler flags accepted by $CC"
    echo ""
  else
    echo "Custom compiler flags rejected by $CC"
    echo "FFdecsa_Test will not be performed"
    echo ""
    exit 1
  fi
fi

if [ $_VERB -ge 1 ]; then
    gcc_ver=`$CC -v 2>&1 | grep -i "gcc version" | head -n 1`
    
    echo "### CPU-INFO ###"
    if [ $is_x86_64 -eq 0 ]; then
      echo "System: x86"
    else
      echo "System: x86_64"
    fi
    if [ "$_CPU" != "" ]; then
      echo "Auto detected arch: $_CPU"
    fi
    echo ""
    echo "Vendor-ID: $vendor_id"
    echo "CPU-Family: $cpu_family"
    echo "CPU-Model: $cpu_model"
    echo "Flags: $flags"
    echo ""
    echo "$gcc_ver"
fi

if [ $_CANNAT -ge 1 ] && [ $_NONAT -eq 0 ]; then
  _DONAT=1
  _CPU="native"
  if [ $_VERB -ge 1 ]; then
    echo "Using compilers \"native\" flags"
    echo ""
    echo ""
  fi
elif [ $_VERB -ge 1 ] && [ "$OwnFlags" == "" ]; then
  echo "Compilers \"native\" flags disabled or unsupported"
  echo ""
  echo ""
fi

if [ $is_x86_64 -eq 1 ]; then
    if [ "$_FLAGS" != "" ]; then
        _FLAGS="$_FLAGS -fPIC"
    else
        _FLAGS=" -fPIC"
    fi
fi

# complete flags for FFdecsa_Test
if [ "$OwnFlags" != "" ]; then
  IFLAGS=$OwnFlags
elif [ "$OLevel" == "" ]; then
  IFLAGS="-march=${_CPU}${_FLAGS} -fexpensive-optimizations -fomit-frame-pointer -funroll-loops"
else
  IFLAGS="$OptFlag -march=${_CPU}${_FLAGS} -fexpensive-optimizations -fomit-frame-pointer -funroll-loops"
fi


###
### FFdecsa Test - get best PARALLEL_MODE
###

ffdecsa_test() {

  if [ "$PMode" == "" ]; then
    if [ $_SHORT -lt 1 ]; then
      FFDECSA_MODES="PARALLEL_32_INT PARALLEL_32_4CHAR PARALLEL_32_4CHARA \
                     PARALLEL_64_8CHAR PARALLEL_64_8CHARA PARALLEL_64_2INT \
                     PARALLEL_64_LONG PARALLEL_64_MMX PARALLEL_128_16CHAR \
                     PARALLEL_128_16CHARA PARALLEL_128_4INT PARALLEL_128_2LONG \
                     PARALLEL_128_2MMX PARALLEL_128_SSE PARALLEL_128_SSE2"
    else
      FFDECSA_MODES="PARALLEL_32_INT PARALLEL_64_2INT PARALLEL_64_LONG \
                     PARALLEL_64_MMX PARALLEL_128_2LONG PARALLEL_128_2MMX \
                     PARALLEL_128_SSE PARALLEL_128_SSE2"
    fi
  else
    FFDECSA_MODES=$PMode
  fi

  if test "x${TMPDIR}" = "x"; then
    TMPDIR="/tmp/FFdecsa"
  fi

  if [ -d "${TMPDIR}" ]; then
    rm -rf "${TMPDIR}"
  fi
  
  mkdir "${TMPDIR}"
  TMPOUT="${TMPDIR}/out"

  cp $FFdecsaDIR/*.c $FFdecsaDIR/*.h $FFdecsaDIR/Makefile "${TMPDIR}"

  if [ "$OwnFlags" != "" ]; then
    FLAGS="$IFLAGS"
  else
    FLAGS="$1 $IFLAGS"
  fi 

  COMPILER=$CC
  export FLAGS
  export COMPILER

  for var in ${FFDECSA_MODES}; do
    if [ $_VERB -ge 1 ]; then
      echo "    ${var}"
    fi
    make -C "${TMPDIR}" -e FFdecsa_test "PARALLEL_MODE=${var}" >/dev/null 2>&1
    if test $? -ne 0 ; then
      if [ $_VERB -ge 1 ]; then
        echo "    ${var}: build failed"
      fi
    else
      MAX_M_val=0
      rm -f ${TMPOUT}
      sync;sleep 2; "${TMPDIR}/FFdecsa_test" > /dev/null 2>"${TMPOUT}"
      if test $? -ne 0; then
        if [ $_VERB -ge 1 ]; then
          echo "    ...failed!"
        fi
      else
        grep FAILED "${TMPOUT}" >/dev/null 2>&1
        if test $? -ne 1; then
          if [ $_VERB -ge 1 ]; then
            echo "    ...failed!"
          fi
        else
          MAX_M_val=`grep "speed=.*Mbit" "${TMPOUT}" | sed -e 's/^.*=\([0-9]*\)\.[0-9]* Mbit.*$/\1/'`
          if [ $_VERB -ge 1 ]; then
            printf "      - $MAX_M_val"
          fi
          
          if [ $MTimes -ge 2 ]; then
            for i in `seq 2 $MTimes`; do
              # sync;
              # sleep 1; 
              "${TMPDIR}/FFdecsa_test" > /dev/null 2>"${TMPOUT}"
              res=`grep "speed=.*Mbit" "${TMPOUT}" | sed -e 's/^.*=\([0-9]*\)\.[0-9]* Mbit.*$/\1/'`
              if test $res -gt $MAX_M_val; then
                MAX_M_val=$res
              fi
              if [ $_VERB -ge 1 ]; then
                printf ", $res"
              fi
            done
          fi

          if [ $_VERB -ge 1 ]; then
            printf "\n"
            echo "      - $MAX_M_val Mbit/s max."
          fi

          if [ "$1" == "-O2" ]; then          
            if test $MAX_M_val -gt $MAX_val_2; then
              MAX_val_2=$MAX_M_val
              MAX_MODE_2=$var
              MAX_val=$MAX_M_val
              MAX_MODE=$var
            fi
          elif [ "$1" == "-O3" ]; then
            if test $MAX_M_val -gt $MAX_val_3; then
              MAX_val_3=$MAX_M_val
              MAX_MODE_3=$var
              MAX_val=$MAX_M_val
              MAX_MODE=$var
            fi
          elif test $MAX_M_val -gt $MAX_val; then
            MAX_val=$MAX_M_val
            MAX_MODE=$var
          fi

        fi
      fi
    fi

    make -C "${TMPDIR}" clean >/dev/null 2>&1

  done

  unset

  if [ $_VERB -ge 1 ]; then
    echo "  Fastest PARALLEL_MODE = ${MAX_MODE} (${MAX_val} Mbit/s)"
    echo ""
  fi

  rm -rf "${TMPDIR}"
}

MAX_val=0
MAX_val_2=0
MAX_val_3=0
MAX_MODE="PARALLEL_64_MMX"
MAX_MODE_2="PARALLEL_64_MMX"
MAX_MODE_3="PARALLEL_64_MMX"

if [ $_NOTEST -eq 0 ]; then

  if [ $_VERB -ge 1 ]; then
      echo "### FFdeCSA TEST ###"
      echo "Using compiler: $CC"
      echo "Flags: $IFLAGS"
      echo ""
  fi

  if [ "$OwnFlags" != "" ]; then
    ffdecsa_test
  elif [ "$OLevel" != "" ]; then
    ffdecsa_test "$OptFlag"
  else
    if [ $_VERB -ge 1 ]; then
      echo "Testing optimization levels 2 and 3"
      echo ""
    else
      echo "This may take a while..."
    fi
    for i in `seq 2 3`; do
      if [ $_VERB -ge 1 ]; then
        echo "  Level -O${i}:"
      fi
      ffdecsa_test "-O${i}"
    done
    if [ $MAX_val_3 -ge $MAX_val_2 ]; then
      MAX_MODE=$MAX_MODE_3
      MAX_val=$MAX_val_3
      OptFlag="-O3"
    else
      MAX_MODE=$MAX_MODE_2
      MAX_val=$MAX_val_2
      OptFlag="-O2"
    fi
    echo ""
    echo "Best result with $OptFlag and $MAX_MODE at $MAX_val Mbit/s"
    echo ""
    echo ""
  fi

fi

if [ $_VERB -ge 1 ]; then
  echo "### VDR-SC Makefile FFdeCSA OPTS ###"
  if [ "$OwnFlags" != "" ]; then
    echo "Custom compiler flags are set."
    echo "Adapt the Makefile yourself!"
  else
    echo "CPUOPT     ?= $_CPU"
    if [ $_NOTEST -eq 0 ]; then
      echo "PARALLEL   ?= $MAX_MODE"
    else
      echo "PARALLEL   ?= $MAX_MODE   # untested!"
    fi
    if [ "$_FLAGS" == "" ]; then
      echo "CSAFLAGS   ?= $OptFlag -fexpensive-optimizations -fomit-frame-pointer -funroll-loops"
    else
      echo "CSAFLAGS   ?= ${OptFlag}${_FLAGS} -fexpensive-optimizations -fomit-frame-pointer -funroll-loops"
    fi
  fi
  echo ""
  echo "### GENERIC FFdeCSA make OPTS ###"
fi

if [ "$OwnFlags" != "" ] || [ "$OLevel" != "" ]; then
  echo "FLAGS=\"${IFLAGS}\" PARALLEL_MODE=${MAX_MODE}"
else
  echo "FLAGS=\"$OptFlag ${IFLAGS}\" PARALLEL_MODE=${MAX_MODE}"
fi
echo ""

exit 0
