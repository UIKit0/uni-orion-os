
#!/bin/sh -x

usage()
{
    echo "Usage: $0 [OPTIONS] test_name"
    echo "Options: "
    echo "     -v | --verbose (verbose mode)"
    echo "     -d | --gdb (use debugger)"
    echo "     -h | --help (this message)"
}

VERBOSE_OPT=""
PINTOS_OPTS="PINTOSOPTS="

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

make
BINARY=`find ./build/tests/ -name $test_name.o`
if [ ! "$BINARY" ]; then
    echo
    echo "Test \"$test_name\" was not found. Check for spelling mistakes"
    echo
    exit
fi
 
rm "`dirname $BINARY`/$test_name.output"
make "`dirname $BINARY`/$test_name.result" $PINTOS_OPTS $VERBOSE_OPT 
