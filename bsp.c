/*- BSP.C -------------------------------------------------------------------*

 Node builder for DOOM levels (c) 1998 Colin Reed, version 3.0 (dos extended)

 Performance increased 200% over 1.2x

 Many thanks to Mark Harrison for finding a bug in 1.1 which caused some
 texture align problems when a flipped SEG was split.

 Credit to:-

 Raphael Quinet (A very small amount of code has been borrowed from DEU).

 Matt Fell for the doom specs.

 Lee Killough for performance tuning, support for multilevel wads, special
 effects, and wads with lumps besides levels.

 Also, the original idea for some of the techniques where also taken from the
 comment at the bottom of OBJECTS.C in DEU, and the doc by Matt Fell about
 the nodes.

 Use this code for your own editors, but please credit me.

*---------------------------------------------------------------------------*/

#include "structs.h"
#include "bsp.h"

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_CONIO_H
#include <conio.h>
#endif

#ifndef O_BINARY
# define O_BINARY 0
#endif

/*- Global Vars ------------------------------------------------------------*/

static FILE *outfile;
static char *testwad;
static const char *outwad;
const char* unlinkwad;

static struct directory *direc = NULL;

struct Seg *(*PickNode)(struct Seg *, const bbox_real_t bbox, int keep_precious)=PickNode_traditional;

static int noreject;

static int noblockmap;

static int nomagic;

static int frac;

static unsigned char pcnt;

static struct lumplist *lumplist,*current_level;

static struct wad_header wad;

/*- Prototypes -------------------------------------------------------------*/

static FILE *infile;

/* fcopy - function to completely copy one stream to another */

static void fcopy(FILE* in, FILE* out)
{
	char buf[1024];
	int rb;
	while ((rb = fread(buf, 1, sizeof buf, in)) > 0) {
		fwrite(buf, 1, rb, out);
	}
}

/*--------------------------------------------------------------------------*/

void progress(void)
{
	if((verbosity > 1) && !((++pcnt)&31))
	{
#ifdef HAVE_CONIO_H
		textcolor(GREEN);
#endif
		Verbose("%c\b","/-\\|"[((pcnt)>>5)&3]);
#ifdef HAVE_CONIO_H
		textcolor(LIGHTGRAY);
#endif
	}
}

/*- get the directory from a wad file --------------------------------------*/
/* rewritten by Lee Killough to support multiple levels and extra lumps */

static int OpenWadFile(char *filename)
{
 long i;
 register struct directory *dir;
 struct lumplist *levelp=NULL;
 int levels = 0;

 if (!(infile = fopen(filename,"rb")))
   ProgError("Error: Cannot open WAD file %s", filename);

#if defined(HAVE_TMPFILE)
 if (fseek(infile,0,SEEK_SET) == -1) {
   FILE* intmp;
   if (errno != ESPIPE) {
     perror("fseek"); exit(errno);
   }
   if (!(intmp = tmpfile())) {
     perror("tmpfile"); 
     ProgError("Input was not seekable and failed to create temp file");
   }
   Verbose("Copying piped input to temporary file"CRLF);
   fcopy(infile,intmp);
   fclose(infile);
   rewind(infile = intmp);
 }
#endif

 if (fread(&wad,1,sizeof(wad),infile)!=sizeof(wad) ||
     (wad.type[0]!='I' && wad.type[0]!='P') || wad.type[1]!='W'
     || wad.type[2]!='A' || wad.type[3]!='D')
   ProgError("%s does not appear to be a WAD file -- bad magic", filename);
 /* Swap header into machine endianness */
  swaplong(&wad.num_entries);
  swaplong(&wad.dir_start);

  Verbose("Opened %cWAD file: %s. %" PRIu32 " dir entries at 0x%08" PRIx32 "."CRLF,
	wad.type[0],filename,wad.num_entries,wad.dir_start);

 direc = GetMemory(sizeof(struct directory) * wad.num_entries);

 fseek(infile,wad.dir_start,0);
 fread(direc,sizeof(struct directory),wad.num_entries,infile);

 current_level=NULL;
 dir = direc;
 for (i=wad.num_entries;i--;dir++)
  {
   register struct lumplist *l=GetMemory(sizeof(*l));
   register int islevel = 0;

   /* Swap dir entry to machine endianness */
   swaplong(&(dir->start));
   swaplong(&(dir->length));

   l->dir=dir;
   l->data=NULL;
   l->next=NULL;
   l->level=NULL;
   l->substream=0;

   if ((dir->name[0]=='E' && dir->name[2]=='M' &&
        dir->name[1]>='1' && dir->name[1]<='9' &&
        dir->name[3]>='1' && dir->name[3]<='9' &&
       (!dir->name[4] || (dir->name[4]>='0' &&
                          dir->name[4]<='9' &&
                         !dir->name[5])  )) || (
        dir->name[0]=='M' && dir->name[1]=='A' &&
        dir->name[2]=='P' && !dir->name[5] &&
        dir->name[3]>='0' && dir->name[3]<='9' &&
        dir->name[4]>='0' && dir->name[4]<='9'))
    {
     /* Begin new level */
     islevel=1;
     levels++;
    }
   else if (levelp) /* The previous lump was part of a level, is this one? */
    {
     if (!strncmp(dir->name,"SEGS",8) ||
	 !strncmp(dir->name,"SSECTORS",8) ||
	 !strncmp(dir->name,"NODES",8) ||
	 (!noblockmap && !strncmp(dir->name,"BLOCKMAP",8)) ||
	 !strncmp(dir->name, "BEHAVIOR", 8) ||
	 !strncmp(dir->name, "SCRIPTS", 8) ||
	 (!noreject && !strncmp(dir->name,"REJECT",8)))
       continue;  /* Ignore these since we're rebuilding them anyway */

     if (FindDir(dir->name))
      {
       Verbose("Warning: Duplicate entry \"%-8.8s\" ignored in %-.8s"CRLF,
         dir->name, current_level->dir->name);
       continue;
      }

     if (!strncmp(dir->name,"THINGS",8) ||
         !strncmp(dir->name,"LINEDEFS",8) ||
         !strncmp(dir->name,"SIDEDEFS",8) ||
         !strncmp(dir->name,"VERTEXES",8) ||
         !strncmp(dir->name,"SECTORS",8) ||
         (noreject && !strncmp(dir->name,"REJECT",8)))
      {                       /* Store in current level */
       levelp->next=l;
       levelp=l;
       continue;
      }
     /* Otherwise, it's the end of the level. */
    }

   /* If that's the end of the level, move its list of lumps to the subtree. */
   if (levelp) current_level->level = current_level->next;

   /* Is this lump the start of a new level? */
   levelp = islevel ? l : NULL;

   /* Add this lump or completed level to the lump list */
   if (current_level)
     current_level->next=l;
   else
     lumplist=l;
   current_level=l;
  }

 /*  If very last lump was a level, we have to do th same housekeeping here,
  *  moving its lumps to the subtree and terminating the list. */
 if (levelp)
  {
   current_level->level=current_level->next;
   current_level->next=NULL;
  }

  return levels;
}

/*- find the pointer to a resource -----------------------*/

struct lumplist *FindDir(const char *name)
{
 struct lumplist *l = current_level;
 while (l && strncmp(l->dir->name,name,8))
   l=l->next;
 return l;
}

/* ReadLump - read a lump into memory */

void* ReadLump(struct lumplist *l)
{
  struct directory *dir = l->dir;
  if (!l->data && dir->length) {
    l->data=GetMemory(dir->length);
    if (fseek(infile,dir->start,0) ||
        fread(l->data,1,dir->length,infile) != dir->length)
         ProgError("Unable to read wad directory entry \"%-8.8s\" in %-.8s"CRLF,
                dir->name, current_level->dir->name);
  }
  return l->data;
}

/* Add a lump to current level
   by Lee Killough */

static void add_lump_entry(const char *name, void *data, size_t length, uint8_t substream)
{
 struct lumplist *l;
 for (l=current_level;l;l=l->next)
   if (!strncmp(name,l->dir->name,8))
     break;
 if (!l)
  {
   l=current_level;
   while (l->next)
     l=l->next;
   l->next = GetMemory(sizeof(*l));
   l = l->next;
   l->next=NULL;
   l->dir = GetMemory(sizeof(struct directory));
   strncpy(l->dir->name,name,8);
  }
 l->dir->length=length;
 l->level=NULL;
 l->data=data;
 l->substream=substream;
}

void add_lump(const char *name, void *data, size_t length)
{
  add_lump_entry(name, data, length, 0);
}

void add_substream(const char *name, void *data, size_t length)
{
  if(!frac) return;
  add_lump_entry(name, data, length, 1);
  Verbose("Substream of %d bytes referred to internally as \"%s\" added"CRLF, length, name);
}

static struct directory * write_lump(struct lumplist *lump)
{
 ReadLump(lump); /* cph - fetch into memory if not there already */
 if ((lump->dir->start = ftell(outfile)) == -1 || (lump->dir->length &&
   fwrite(lump->data, 1, lump->dir->length, outfile) != lump->dir->length))
   ProgError("Failure writing %-.8s"CRLF, lump->dir->name);
 if (lump->data) { free(lump->data); lump->data = NULL; }
 if (lump->substream) return NULL;

 /* This dir entry is to be written to file, so swap back to little endian */
 swaplong(&(lump->dir->start));
 swaplong(&(lump->dir->length));

 return lump->dir;
}

static void sortlump(struct lumplist **link)
{
 static const char *const lumps[13]={"THINGS", "LINEDEFS", "SIDEDEFS",
  "VERTEXES", "VERTSUBS", "SEGS", "SEGSSUBS", "SSECTORS", "NODES", 
  "NODESUBS", "SECTORS", "REJECT", "BLOCKMAP"};
 int i=sizeof(lumps)/sizeof(*lumps)-1;
 struct lumplist **l;
 do
   for (l=link;*l;l=&(*l)->next)
     if (!strncmp(lumps[i],(*l)->dir->name,8))
      {
       struct lumplist *t=(*l)->next;
       (*l)->next=*link;
       *link=*l;
       *l=t;
       break;
      }
 while (--i>=0);
}

void usage(const char* path) __attribute__((noreturn));

void usage(const char* path) 
{
 printf(CRLF"BSP v" VERSION );
#ifdef HAVE_CONIO_H
 textcolor(LIGHTMAGENTA);
 cputs(FLAVOUR);
 textcolor(LIGHTGRAY);
#else
 puts(FLAVOUR);
#endif
 printf(CRLF CRLF"See the file AUTHORS for a complete list of credits and contributors"CRLF
        CRLF"Usage: bsp [options] input.wad [[-o] <output.wad>]"CRLF
        "       (If no output.wad is specified, tmp.wad is written)"CRLF CRLF
        "Options:"CRLF CRLF
        "  -factor <nnn>  Changes the cost assigned to SEG splits"CRLF
        "  -picknode {traditional|visplane"
#ifdef PICKMODERN
                                          "|modern"
#endif
                                                  "}"CRLF
        "                 Selects either the traditional nodeline choosing algorithm"CRLF
        "                 (balance the tree and minimise splits) or Lee's algorithm"CRLF
        "                 to minimise visplanes (try to balance distinct sector refs)"CRLF
#ifdef PICKMODERN
        "                 or \"modern\" experimental added specifically to this build"CRLF
#endif        
        "  -blockmap {old|comp}"CRLF
        "                 Selects either the old straightforward blockmap"CRLF
        "                 generation, or the new compressed blockmap code"CRLF
        "  -noreject      Does not clobber reject map"CRLF
        "  -noblockmap    Leaves existing blockmap alone"CRLF
        "  -nomagic       Do not keep \"precious\" sectors and lines intact"CRLF
        "  -frac          Enables saving fraction part of coordinates"CRLF
        "  -q             Quiet mode (only errors are printed)"CRLF
       );
 exit(1);
}

static int quiet;

struct multi_option {
	const char *tag;
	const char *text;
	void       *value;
};

const struct multi_option picknode_options[] = {
{"traditional", "Optimising for SEG splits and balance",PickNode_traditional},
{"visplane", "Optimising for fewest visplanes", PickNode_visplane},
#ifdef PICKMODERN
{"modern", "Experimental \"real\" numbers version", PickNode_modern},
#endif
{NULL,NULL,NULL},
};

void (*CreateBlockmap)(const bbox_t bbox) = CreateBlockmap_old;
const struct multi_option blockmap_options[] = {
{"old", "BSP v3.0 blockmap algorithm",CreateBlockmap_old},
{"comp", "Compressed blockmap", CreateBlockmap_compressed},
{"dummy", "Skip creating blockmap", CreateBlockmap_dummy},
{NULL,NULL,NULL},
};

static void parse_options(int argc, char *argv[])
{
 static char *fnames[2];
 static const struct {
   const char *option;
   void *var;
   enum {NONE, STRING, INT, MULTI} arg;
   const struct multi_option* opts;
 } tab[]= { {"-picknode", &PickNode, MULTI, picknode_options},
            {"-blockmap", &CreateBlockmap, MULTI, blockmap_options},
            {"-noreject", &noreject, NONE},
            {"-noblockmap", &noblockmap, NONE},
            {"-nomagic", &nomagic, NONE},
            {"-q", &quiet, NONE},
            {"-factor", &factor, INT},
            {"-o", fnames+1, STRING},
            {"-frac", &frac, NONE},
          };
 int nf=0;

 while (--argc)
   if (**++argv=='-')
    {
     int i=sizeof(tab)/sizeof(*tab);
     for (;;)
       if (!strcmp(*argv,tab[--i].option))
        {
         if (tab[i].arg != NONE && !--argc) usage(argv[0]);
         switch (tab[i].arg) {
	 case MULTI:
	   {
		const struct multi_option* p = tab[i].opts;
		const char* opt = *++argv;

		while (p->tag) {
			if (!strcmp(p->tag,opt)) {
				*(void**)(tab[i].var) = p->value;
				Verbose("%s: %s"CRLF,tab[i].option, p->text);
				break;
			}
			p++;
		}
	   }
	   break;
         case INT:
            {
             char *end;
             *(int *) tab[i].var=strtol(*++argv,&end,0);
             if (*end || factor<0)
               usage(argv[0]);
            }
           break;
         case STRING:
	   *(char **) tab[i].var = *++argv;
           break;
	 case NONE:
	     ++*(int *) tab[i].var; break;
         }
         break;
        }
       else
         if (!i)
          {
           usage(argv[0]);
           break;
          }
    }
   else
     if (nf<2)
       fnames[nf++]=*argv;
     else
       usage(argv[0]);

 testwad = fnames[0];                          /* Get input name*/

 if (!testwad || factor<0)
   usage(argv[0]);

 outwad = fnames[1] ? fnames[1] : "tmp.wad";   /* Get output name*/

 if (noblockmap) CreateBlockmap = CreateBlockmap_dummy;
}

/*- Main Program -----------------------------------------------------------*/

int main(int argc,char *argv[])
{
 struct lumplist *lump;
 struct directory *newdirec;
 int levels;
 int using_temporary_output = 0;

 parse_options(argc,argv);

 verbosity = quiet ? 0 : 
#ifdef HAVE_ISATTY
	isatty(STDOUT_FILENO) ? 2 : 1
#else
	2
#endif
	;

 /* Don't buffer output to stdout, people want to see the progress 
  * as it happens */
#ifdef HAVE_ISATTY
 if (isatty(STDOUT_FILENO))
#endif
   setbuf(stdout,NULL);

 if (verbosity)
  {
   Verbose("* ");
#ifdef HAVE_CONIO_H
   textcolor(RED);
#endif
   Verbose("Doom");
#ifdef HAVE_CONIO_H
   textcolor(LIGHTGRAY);
#endif
   Verbose(" BSP node builder ver " VERSION);
#ifdef HAVE_CONIO_H
   textcolor(LIGHTMAGENTA);
#endif
   Verbose(FLAVOUR);
#ifdef HAVE_CONIO_H
   textcolor(LIGHTGRAY);
#endif
   Verbose(CRLF
		"Copyright (c) 1998 Colin Reed, Lee Killough"CRLF
		"              2001 Simon Howard"CRLF
		"              2000,2001,2002,2006 Colin Phipps <cph@moria.org.uk>"CRLF CRLF);
  }
 levels = OpenWadFile(testwad);		/* Opens and reads directory*/

 Verbose("Creating nodes using tunable factor of %d"CRLF,factor);

 {
   int fd = open(outwad,O_WRONLY | O_CREAT | O_EXCL | O_BINARY,0644);
   outfile = NULL;
   if (fd == -1 && (errno == EEXIST || errno == -1)) {
#ifdef HAVE_TMPFILE
     outfile = tmpfile();
     using_temporary_output = 1;
#endif
   } else if (fd != -1) {
     outfile = fdopen(fd,"wb");
     unlinkwad = outwad;
   }
   if (!outfile) {
    fputs("Error: Could not open output PWAD file ",stderr);
    perror(outwad);
    exit(1);
   }
 }

 /* Allocate space for existing lumps plus some extra for each level */
 newdirec = GetMemory((wad.num_entries + 10*levels)*sizeof(struct directory));

 wad.num_entries = 0;

 fwrite("xxxxxxxxxxxx",sizeof wad,1,outfile);

 for (lump=lumplist; lump; lump=lump->next) {
  newdirec[wad.num_entries++]=*write_lump(lump);
  if (lump->level)
    {
     char current_level_name[9];
     struct lumplist* l;
     strncpy(current_level_name,lump->dir->name,8);
     current_level_name[8] = 0;
     DoLevel(current_level_name, current_level = lump->level, !nomagic, 
#ifdef PICKMODERN
        PickNode == PickNode_modern ? 0 : 
#endif
        EDGE_TOLERANCE);
     sortlump(&lump->level);
     for (l=lump->level; l; l=l->next)
       {
        struct directory * d = write_lump(l);
        if(d) newdirec[wad.num_entries++] = *d;
       }
   }  /* if (lump->level) */
 }

 Verbose(CRLF"Saving WAD file...");

 if ((wad.dir_start = ftell(outfile)) == -1 ||
    fwrite(newdirec,sizeof(struct directory),wad.num_entries,outfile)!=wad.num_entries)
    ProgError("Failure writing lump directory");
 free(newdirec);

 /* Swap header back to little endian */
 swaplong(&(wad.num_entries));
 swaplong(&(wad.dir_start));
 if (fseek(outfile, 0, SEEK_SET) || fwrite(&wad,1,12,outfile)!=12)
    ProgError("Failure writing wad header");

 if (using_temporary_output) {
   FILE* realout = fopen(outwad,"wb");
   unlinkwad = outwad;
   rewind(outfile);
   fcopy(outfile,realout);
   fclose(realout);
 }
 fclose(outfile);

 Verbose(" saved as %s"CRLF,outwad);

 return 0;
}

/*- end of file ------------------------------------------------------------*/
