#include <stdio.h>

#ifdef _MSC_VER
#include <windows.h>
#include <tchar.h>
#include <io.h>
#endif

#ifdef _MSC_VER
int wmain(int argc, wchar_t *argv[], TCHAR *envp[]) {
#else
int main(int argc, char *argv[]) {

  int i = 0;

  printf("Running Unicode Command-Line Test.\n");
  printf("----------------------------------\n");
  printf("* argc:\t%d\n",argc);
  printf("Arguments:\n");
  for(i = 0; i < argc; i++) {
    printf("* argv[%d]: %s\n",i,argv[i]);
  }

  printf("\n");
  printf("----------------------------------\n");
  printf("\n");

  return 0;
}
