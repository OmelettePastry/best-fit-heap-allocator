#include <stdlib.h>
#include <stdio.h>

int main (int argc, char **argv){

  char* x = malloc(24);
  char* y = malloc(19);
  char* z = malloc(77);

  printf("\n");

  // ********** MEMORY ALLOCATIONS (X, Y, Z) **********
  printf("x = %p, size = 24\n", x);
  printf("y = %p, size = 19\n", y);
  printf("z = %p, size = 77\n\n", z);

  // ********** FREE BLOCKS (X, Y, Z) **********
  printf("Freeing x, y, x...\n\n");
  free(x);
  free(y);
  free(z);

  // ********** MEMORY ALLOCATIONS (A, B, C) **********
  printf("Allocating a (size = 65)...\n");
  char* a = malloc(65);
  printf("a = %p, size = 65\n\n", a);  

  printf("Allocating b (size = 8)...\n");
  char* b = malloc(8);
  printf("b = %p, size = 8\n", b);

  // Sample contents to be stored at address b
  char text1[8] = "The grey";

  // Our loop to copy the above string (text1) to our location at y
  int count; // counter variable
  
  for (count = 0; count < 8; count++) {
    *(b + count) = text1[count];
  }

  // Contents of b
  printf("Contents of b (8 bytes): '");
  for (count = 0; count < 8; count++) {
    printf("%c", *(b + count));
  }
  printf("'\n\n");

  printf("Allocating c (size = 92)...\n");
  char* c = malloc(92);
  printf("c = %p, size = 92\n\n", c);

  // ********** MEMORY REALLOCATIONS **********
  printf("Reallocating a to d(size 65 ===> 55)...\n");
  char* d = realloc(a, 55);
  printf("d = %p, size = 55\n\n", d);
  
  printf("Reallocating b to e(size 8 ===> 102)...\n");
  char* e = realloc(b, 102);
  printf("e = %p, size = 102\n", e);

  // Contents of e
  printf("Contents of e (102 bytes): '");
  for (count = 0; count < 102; count++) {
    printf("%c", *(e + count));
  }
  printf("'\n\n");
}
