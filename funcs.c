/*- FUNCS.C ----------------------------------------------------------------*/
/*- terminate the program reporting an error -------------------------------*/

#include "structs.h"
#include "bsp.h"

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_CONIO_H
#include <conio.h>
#endif


void ProgError(const char *errstr, ...)
{
   va_list args;

   va_start( args, errstr);
   fprintf(stderr, "\nProgram Error: *** ");
   vfprintf( stderr, errstr, args);
   fprintf(stderr, " ***\n");
   va_end( args);
#ifdef HAVE_UNLINK
   if (unlinkwad) unlink(unlinkwad);
#endif
   exit( 5);
}

/* Print stuff if verbose output */

int verbosity;

void Verbose(const char *errstr, ...)
{
   va_list args;

   if (!verbosity) return;

#ifdef HAVE_CONIO_H
   {
      char * buff = NULL;
      char dummy[5] = "\0";
      int l;

      va_start( args, errstr);
      l = vsnprintf(dummy, sizeof(dummy)/sizeof(dummy[0]) - 1, errstr, args);
      va_end( args);
      if(l < sizeof(dummy)/sizeof(dummy[0])) buff = &dummy[0];
      else buff = GetMemory((l+2) * sizeof(*buff));
      *buff=0;
      va_start( args, errstr);
      l = vsnprintf(buff, l+1, errstr, args);
      va_end( args);
      cputs(buff);
      if(buff != &dummy[0]) free(buff);
   }   
#else   
   va_start( args, errstr);
   vprintf(errstr, args);
   va_end( args);
#endif

}

#ifndef WITH_DMALLOC
/*- allocate memory with error checking ------------------------------------*/
void* GetMemory(size_t size)
{
   void *ret = malloc( size);
   if (!ret)
      ProgError( "out of memory (cannot allocate %zu bytes)", size);
   return ret;
}

/*- reallocate memory with error checking ----------------------------------*/

void* ResizeMemory(void *old, size_t size)
{
   void *ret = realloc( old, size);
   if (!ret)
      ProgError( "out of memory (cannot reallocate %zu bytes)", size);
   return ret;
}

#endif
/*--------------------------------------------------------------------------*/

