
#!/bin/sh -x

if [ "$#" -ne 1 ]; then
    echo "Wrong number of parameters. You should call it: $0 test_name"
    exit 
fi
 
make
BINARY=`find ./build/tests/ -name $1.o`
if [ -z "$BINARY" ]; then
    echo
    echo "Test $1 was not found. Check for spelling mistakes"
    echo
    exit
fi
 
rm "`dirname $BINARY`/$1"
make "`dirname $BINARY`/$1.result"