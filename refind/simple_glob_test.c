#include <stdio.h>
#include "simple_glob.h"

void charToChar16(CHAR16 *out, int maxLength, char *in)
{
  int i;
  
  for(i = 0; i < maxLength-1 && in[i] != '\0'; i++) 
    out[i] = in[i];

  out[i] = 0;
}
    


int main(int argc, char ** argv)
{
  if(argc < 2) {
    fprintf(stderr,"Usage: %s <glob> <value> [values..]\n",argv[0]);
  }

  CHAR16 pat[256];
  charToChar16(pat,255,argv[1]);
  
  for(int i = 2; i < argc; i++) {
    CHAR16 val[256];
    charToChar16(val,255,argv[i]);
      
    printf("%s %s: %s\n",argv[1], argv[i],
	   CompareGlob(pat, val) ? "true" : "false");
  }

  return 0;
}

