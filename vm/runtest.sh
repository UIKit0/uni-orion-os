
#!/bin/sh -x

make
FILE_LIST=`find ./build/tests/ -name $1.o`
rm "`dirname $FILE_LIST `/$1"
make "`dirname $FILE_LIST `/$1.result"