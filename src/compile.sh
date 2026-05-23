if [ ! -d "obj" ]; then
  mkdir obj
fi

# valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose ./main.elf 3500 2> valgrind.txt

# Para compilar en linux x86/x64
# gcc -c -g -Wall -O0 -fno-tree-vectorize -o obj/main.o main.c
gcc -c -g -Wall -O2 -o obj/main.o main.c
# gcc -c -g -Wall -O2 -fsanitize=address,undefined -o obj/main.o main.c
# Para compilar ARM
#gcc -c -g -Wall -mfpu=vfp -O2 -o obj/main.o main.c

gcc -c -g -Wall -O2 -o obj/chrono.o chrono.c

cd obj

gcc -o ../main.elf main.o chrono.o  -lm -lSDL 
# gcc -fsanitize=address,undefined -o ../main.elf main.o chrono.o  -lm -lSDL 

# objdump -d -M intel obj/main.o > main.s