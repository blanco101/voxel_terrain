if [ ! -d "obj" ]; then
  mkdir obj
fi

# Para compilar en linux x86/x64
gcc -c -g -Wall -O2 -o obj/main.o main.c
objdump -d obj/main.o > main.s
# Para compilar ARM
#gcc -c -g -Wall -mfpu=vfp -O2 -o obj/main.o main.c

gcc -c -g -Wall -O2 -o obj/chrono.o chrono.c

cd obj

gcc -o ../main.elf main.o chrono.o  -lm -lSDL 
