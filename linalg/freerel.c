/* 
 * Program: free relations
 * Author : F. Morain
 * Purpose: creating free relations in a suitable format
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include <gmp.h>
#include "cado.h"
#include "mod_ul.c"
#include "utils/utils.h"
#include "sieve/fb.h"

#include "hashpair.h"

// When p1 == p2, sort r's in increasing order
int
compare_ul2(const void *v1, const void *v2)
{
    unsigned long w1 = *((unsigned long*) v1);
    unsigned long w2 = *((unsigned long*) v2);

    if(w1 > w2)
	return 1;
    if(w1 < w2)
	return -1;
    w1 = *(1 + (unsigned long*) v1);
    w2 = *(1 + (unsigned long*) v2);
    return (w1 >= w2 ? 1 : -1);
}

// find free relations by inspection.
int
findFreeRelations(hashtable_t *H, cado_poly pol, int nprimes)
{
    unsigned long *tmp = (unsigned long *)malloc((2+(nprimes<<1)) * sizeof(unsigned long));
    unsigned int i, j, k, ntmp = 0;
    int pdeg, nfree;

    for(i = 0; i < H->hashmod; i++)
	if(H->hashcount[i] > 0){
	    tmp[ntmp++] = (unsigned long)H->hashtab_p[i];
	    tmp[ntmp++] = H->hashtab_r[i];
	}
    qsort(tmp, (ntmp>>1), 2 * sizeof(unsigned long), compare_ul2);
    // add a sentinel
    tmp[ntmp] = 0;
    tmp[ntmp+1] = 0;
    // now, inspect
    for(i = 0; ; i += 2)
	if(((long)tmp[i+1]) >= 0)
	    break;
    j = i+2;
    pdeg = 1;
    nfree = 0;
    while(1){
	if(tmp[j] == tmp[i]){
	    // same prime
	    if(((long)tmp[j+1]) >= 0)
		// another algebraic factor
		pdeg++;
	}
	else{
	    // new prime
	    if(pdeg == pol[0].degree){
		// a free relation
		printf("%lu,0:%lx:", tmp[i], tmp[i]);
		for(k = i; k < i+(pdeg<<1); k += 2){
		    printf("%lx", tmp[k+1]);
		    if(k < i+(pdeg<<1)-2)
			printf(",");
		}
		printf("\n");
		nfree++;
	    }
	    i = j;
	    if(tmp[j] == 0)
		break;
	    pdeg = 1;
	}
	j += 2;
    }
    free(tmp);
    return nfree;
}

int
scan_relations(FILE *file, int *nprimes_alg, hashtable_t *H)
{
    relation_t rel;
    unsigned int h;
    int ret, irel = -1, j;
    
    while(1){
	ret = fread_relation (file, &rel);
	if(ret != 1)
	    break;
	irel += 1;
	if(!(irel % 100000))
	    fprintf(stderr, "nrel = %d at %2.2lf\n", irel, seconds());
	// ignore already found free relations...
	if(rel.b == 0){
	    fprintf(stderr, "Ignoring already found free relation...\n");
	}
	else{
	    reduce_exponents_mod2(&rel);
	    computeroots(&rel);
	    for(j = 0; j < rel.nb_ap; j++){
		h = hashInsert(H, rel.ap[j].p, rel.ap[j].r);
		if(H->hashcount[h] == 1){
		    // new prime
		    *nprimes_alg += 1;
		}
	    }
	}
	clear_relation(&rel);
    }
    return ret;
}

void
largeFreeRelations(cado_poly pol, char **fic, int nfic)
{
    FILE *file;
    hashtable_t H;
    int Hsizea, nprimes_alg = 0, nfree = 0, i;

    ASSERT(fic != NULL);
    Hsizea = (1<<pol[0].lpba)/((int)(pol[0].lpba * log(2.0)));
    hashInit(&H, Hsizea);
    fprintf(stderr, "Scanning relations\n");
    for(i = 0; i < nfic; i++){
	fprintf(stderr, "Adding file %s\n", fic[i]);
	file = fopen(fic[i], "r");
	scan_relations(file, &nprimes_alg, &H);
	fclose(file);
    }
    fprintf(stderr, "nprimes_alg = %d\n", nprimes_alg);
    nfree = findFreeRelations(&H, pol, nprimes_alg);
    fprintf(stderr, "Found %d usable free relations\n", nfree);
}

int
countFreeRelations(int *deg, char *roots)
{
    FILE *ifile = fopen(roots, "r");
    char str[1024], str0[128], str1[128], str2[128], *t;
    int nfree = 0, nroots;

    *deg = -1;
    // look for the degree
    while(1){
	if(!fgets(str, 1024, ifile))
	    break;
	sscanf(str, "%s %s %s", str0, str1, str2);
	if((str0[0] == '#') && !strcmp(str1, "DEGREE:")){
	    *deg = atoi(str2);
	    break;
	}
    }
    if(*deg == -1){
	fprintf(stderr, "No degree found, sorry\n");
	exit(1);
    }
    fprintf(stderr, "DEGREE = %d\n", *deg);
    while(1){
	if(!fgets(str, 1024, ifile))
            break;
	// format is "11: 2,6,10"
	sscanf(str, "%s %s", str0, str1);
	nroots = 1;
	for(t = str1; *t != '\0'; t++)
	    if(*t == ',')
		nroots++;
	if(nroots == *deg)
	    nfree++;
    }
    fclose(ifile);
    return nfree;
}

void
addFreeRelations(char *roots, int deg)
{
    factorbase_degn_t *fb = fb_read(roots, 1.0, 0), *fbptr;
    fbprime_t p;
    int i;

    for(fbptr = fb; fbptr->p != 0; fbptr = fb_next(fbptr)){
	p = fbptr->p;
	if(fbptr->nr_roots == deg){
	    // free relation, add it!
	    // (p, 0) -> p-0*m
	    printf("%d,0:%lx:", p, (long)p);
	    for(i = 0; i < fbptr->nr_roots; i++){
		printf("%x", fbptr->roots[i]);
		if(i < fbptr->nr_roots-1)
		    printf(",");
	    }
	    printf("\n");
	}
    }
}

static void
smallFreeRelations(char *fbfilename)
{
    int deg;
    int nfree = countFreeRelations (&deg, fbfilename);
    fprintf (stderr, "# Free relations: %d\n", nfree);
    
    fprintf (stderr, "Handling free relations...\n");
    addFreeRelations (fbfilename, deg);
}

static void
usage (char *argv0)
{
  fprintf (stderr, "Usage: %s -poly xxx.poly [-fb xxx.roots]", argv0);
  fprintf (stderr, " [xxx.rels1 xxx.rels2 ... xxx.relsk]\n");
  exit (1);
}

int
main(int argc, char *argv[])
{
    char *fbfilename = NULL, *polyfilename = NULL, **fic;
    char *argv0 = argv[0];
    cado_poly cpoly;
    int nfic = 0;

    while (argc > 1 && argv[1][0] == '-')
      {
        if (argc > 2 && strcmp (argv[1], "-fb") == 0)
          {
            fbfilename = argv[2];
            argc -= 2;
            argv += 2;
          }
        else if (argc > 2 && strcmp (argv[1], "-poly") == 0)
          {
            polyfilename = argv[2];
            argc -= 2;
            argv += 2;
          }
        else
          usage (argv0);
      }

    fic = argv+1;
    nfic = argc-1;

    if (polyfilename == NULL || ((fbfilename == NULL) && (nfic == 0)))
      usage (argv0);

    if (!read_polynomial (cpoly, polyfilename))
      {
        fprintf (stderr, "Error reading polynomial file\n");
        exit (EXIT_FAILURE);
      }

    /* check that n divides Res(f,g) [might be useful to factor n...] */
    check_polynomials (cpoly);

#if 0
    if (mpz_cmp_ui (cpoly->g[1], 1) != 0)
      {
        fprintf (stderr, "Error, non-monic linear polynomial not yet treated");
        fprintf (stderr, " (more theory needed)\n");
        exit (1);
      }
#endif

    if(nfic == 0)
	smallFreeRelations(fbfilename);
    else
	largeFreeRelations(cpoly, fic, nfic);

    clear_polynomial (cpoly);

    return 0;
}
