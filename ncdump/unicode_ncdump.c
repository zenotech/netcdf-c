#include <stdio.h>

#ifdef _MSC_VER 
#include <io.h>
#endif


int main(int argc, char *argv[]) {

  printf("Running Unicode Command-Line Test.\n");
  printf("----------------------------------\n");
  printf("* argc:\t%d\n",argc);
  for(int i = 0; i < argc; i++) {
    printf("* argv[%d]: %s\n",i,argv[i]);
  }

  printf("\n");
  printf("----------------------------------\n");
  printf("\n");

  return 0;
}
