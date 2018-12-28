#include "simple_glob.h"

/* very simple glob matcher. supports '*' and '?'.
   Will match directory seperator as a regular character. ie (x yz*abc will match xyz/abc), so
   don't pass directory separtors if this matters to you */

#define MAX_STARS 10

//keeps track of stars matched. Used for backtracking
typedef struct Star {
  const CHAR16 * p; //position in pattern where '*' was encountered
  const CHAR16 * sv; //position in value where '*' was encountered
  const CHAR16 * ev; //end position where '*' was encountered
} Star;

BOOLEAN CompareGlob(const CHAR16 *p, const CHAR16 *v)
{
  Star stars[MAX_STARS];
  UINTN starCount = 0;
  const CHAR16 * ev;

  //ev is the end of the value
  ev = v;
  while(*ev) ev++;
  
  for(;;) {
    BOOLEAN match = FALSE;
    
    switch(*p) {
    case '*':
      //we don't go on forever, matching all the stars the user supplies
      if(starCount == MAX_STARS) return FALSE;
      
      stars[starCount].p = p;
      stars[starCount].sv = v;
      stars[starCount].ev = ev; //do a greedy match
      starCount ++;

      v = ev;
      p++;
      match=TRUE;
      
      break;
    case '?':
      if(!(*v)) //if we're at end of string, we don't match
	break;
      
      v++;
      p++;
      match=TRUE;
      break;
    case '\0':
      if(*v)
	//val has another character, so no match
	break;
      
      return TRUE; //we exhausted both the pattern and the value, so we matched everything
    default:
      if(*p != *v)
	break;

      v++;
      p++;
      match=TRUE;
      break;
    }//end switch on pattern character

    if(!match) {
      //no match, so we try and backtrack
      
      Star *cs = NULL;

      while(starCount > 0) {
	cs = &stars[starCount-1];
	if(cs->sv != cs->ev) //we haven't already tried matching all the possibilities for this star
	  break;
	starCount --;
      }

      if(starCount == 0) //exhausted our stars, no more backtracking opportunities
	return FALSE;

      //move back one character in the values matched by the current star
      cs->ev = cs->ev - 1 ;

      //move back the match according to the current star
      v = cs->ev;
      p = cs->p + 1; 
      
      //now we can go on
    }
  } //while still trying to match (loop forever)
}
