
# Para compilar en linux x86/x64
gcc -c -g -Wall -O2 -o obj/main.o main.c
# Para compilar ARM
#gcc -c -g -Wall -mfpu=vfp -O2 -o obj/main.o main.c

gcc -c -g -Wall -O2 -o obj/chrono.o chrono.c

cd obj

gcc -o ../main.elf main.o chrono.o  -lm -lSDL 
