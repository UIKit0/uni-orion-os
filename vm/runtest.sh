
#!/bin/sh -x

usage()
{
    echo "Usage: $0 [OPTIONS] test_name"
    echo "Options: "
    echo "     -v | --verbose (verbose mode)"
    echo "     -d | --gdb (use debugger)"
    echo "     -i | --inf-timeout (use a big timeout)"
    echo "     -h | --help (this message)"
}

VERBOSE_OPT=""
PINTOS_OPTS="PINTOSOPTS="
TIMEOUT_OPTS=""

while :
do
    case $1 in
	-h | --help | -\?)
	    usage
	    exit 0
	    ;;
	-v | --verbose)
	    VERBOSE_OPT="VERBOSE=1"
	    shift
	    ;;
	-d | --gdb)
	    PINTOS_OPTS+="--gdb "
	    shift
	    ;;
	-i | --inf-timeout)
	    TIMEOUT_OPTS="TIMEOUT=30000"
	    shift
	    ;;
	--)
	    #no more options.
	    shift
	    break;
	    ;;
	-*)
	    echo "No such option: $1" >&2
	    usage
	    exit 0
	    ;;
	*)
	    # no more options.
	    break;
    esac
done
 
if [ $# -ne 1 ]; then
    usage
    exit 0;
fi;

test_name=$1
tests_make_file=`grep -r -l --include="*.tests" $test_name""_SRC ../tests`

if [ ! $tests_make_file ]; then
    echo
    echo "Test \"$test_name\" was not found. Check for spelling mistakes"
    echo
    exit
fi

if ! make ; then
    exit 1
fi

tests_dir="build/"`dirname ${tests_make_file#../}`
rm "$tests_dir/$test_name.output"
make "$tests_dir/$test_name.result" $PINTOS_OPTS $VERBOSE_OPT $TIMEOUT_OPTS
