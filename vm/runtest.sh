pintos -v -k -T 60 --bochs  --filesys-size=2 -p build/tests/vm/$1 -a $1 --swap-size=4 -- -q  -f run $1 < /dev/null  
#2> $1.errors > $1.output

