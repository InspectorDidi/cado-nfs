#include "utils/utils.h"

#include "merge_opts.h"

#ifdef USE_MARKOWITZ

#include "sparse.h"
#include "dclist.h"
#include "sparse_mat.h"
#include "report.h"
#include "markowitz.h"

#define MKZ_DEBUG 1

#define MKZ_INF -1

// Again, a priority queue as a heap...!
// Q[0] contains the number of items in Q[], so that useful part of Q
// is Q[1..Q[0]]

// Q[2*i] contains j-jmin = dj
// Q[2*i+1] contains the Markowitz count for j

#define MkZIsQueueEmpty(Q) (Q[0] == 0)

// get j-th component of Q[i] 
#define MkzGet(Q, i, j) (Q[((i)<<1)+(j)])
#define MkzSet(Q, i, j, val) (Q[((i)<<1)+(j)] = (val))

// (Q, A)[k1] <- (Q, A)[k2]
void
MkzAssign(INT *Q, INT *A, INT k1, INT k2)
{
    INT dj = MkzGet(Q, k2, 0);

    MkzSet(Q, k1, 0, dj);
    MkzSet(Q, k1, 1, MkzGet(Q, k2, 1)); // could be simplified...!
    A[dj] = k1;
}

void
MkzPrintQueue(INT *Q)
{
    int level = 0, imax = 1, i;

    fprintf(stderr, "L0:");
    for(i = 1; i <= Q[0]; i++){
	fprintf(stderr, " [%d, %d]", MkzGet(Q, i, 1), MkzGet(Q, i, 0));
	if(i == imax){
	    imax = (imax<<1)+1;
	    fprintf(stderr, "\nL%d:", ++level);
	}
    }
    fprintf(stderr, "\n");
}

void
MkzUpQueue(INT *Q, INT *A, INT k)
{
    INT x = MkzGet(Q, k, 0), v = MkzGet(Q, k, 1);

    while((k > 1) && (MkzGet(Q, k/2, 1) >= v)){
	// we are at level > 0 and the father is >= son
	// the father replaces the son
	MkzAssign(Q, A, k, k/2);
	k /= 2;
    }
    // we found the place of (x, v)
    MkzSet(Q, k, 0, x);
    MkzSet(Q, k, 1, v);
    A[x] = k;
}

void
MkzInsert(INT *Q, INT *A, INT dj, INT c)
{
    Q[0]++;
    MkzSet(Q, Q[0], 0, dj);
    MkzSet(Q, Q[0], 1, c);
    A[dj] = Q[0];
    MkzUpQueue(Q, A, Q[0]);
}

// Move Q[k] down.
void
MkzDownQueue(INT *Q, INT *A, INT k)
{
    INT x = MkzGet(Q, k, 0), v = MkzGet(Q, k, 1), j;

    while(k <= Q[0]/2){
	// k has at least a left son
	j = 2*k;
	if(j < Q[0])
	    // k has a right son
	    if(MkzGet(Q, j, 1) > MkzGet(Q, j+1, 1))
		j++;
	// at this point, Q[j] is the largest son
	if(v <= MkzGet(Q, j, 1))
	    break;
	else{
	    // the father takes the place of the son
	    MkzAssign(Q, A, k, j);
	    k = j;
	}
    }
    // we found the place of (x, v)
    MkzSet(Q, k, 0, x);
    MkzSet(Q, k, 1, v);
    A[x] = k;
}

void
MkzPopQueue(INT *dj, INT *mkz, INT *Q, INT *A)
{
    *dj = MkzGet(Q, 1, 0);
    *mkz = MkzGet(Q, 1, 1);
    MkzAssign(Q, A, 1, Q[0]);
    Q[0]--;
    MkzDownQueue(Q, A, 1);
}

// Remove (A, Q)[k].
void
MkzDelete(INT *Q, INT *A, INT k)
{
    // we put Q[Q[0]] in Q[k]
    MkzAssign(Q, A, k, Q[0]);
    Q[0]--;
    // move new node up or down
    if(k == 1)
	// rare event!
	MkzDownQueue(Q, A, 1);
    else{
	// k has a father
	if(MkzGet(Q, k/2, 1) > MkzGet(Q, k, 1))
	    // we have to move up
	    MkzUpQueue(Q, A, k);
	else
	    MkzDownQueue(Q, A, k);
    }
}

int
MkzIsHeap(INT *Q)
{
    int k;

    for(k = 1; k <= Q[0]/2; k++){
	// k has a left son
	if(MkzGet(Q, k, 1) > MkzGet(Q, 2*k, 1)){
	    fprintf(stderr, "Pb: father=%d > lson=%d\n", 
		    MkzGet(Q, k, 1), MkzGet(Q, 2*k, 1));
	    return 0;
	}
	if(k < Q[0]/2){
	    // k has a right son
	    if(MkzGet(Q, k, 1) > MkzGet(Q, 2*k+1, 1)){
		fprintf(stderr, "Pb: father=%d > rson=%d\n", 
			MkzGet(Q, k, 1), MkzGet(Q, 2*k+1, 1));
		return 0;
	    }
	}
    }
    return 1;
}

void
MkzCheck(sparse_mat_t *mat)
{
    INT dj;

    for(dj = 0; dj < mat->jmax - mat->jmin; dj++)
	if(mat->wt[dj] > 0)
	    if(MkzGet(mat->MKZQ, mat->MKZA[dj], 0) != dj)
		fprintf(stderr, "GASP: %d in MkzCheck\n", dj);
}

int
MkzCount(sparse_mat_t *mat, INT j)
{
    int mkz, k, i;

    mkz = mat->nrows;
    for(k = 1; k <= mat->R[GETJ(mat, j)][0]; k++)
	if((i = mat->R[GETJ(mat, j)][k]) != -1){
	    // this should be the weight of row i
	    if(mat->rows[i][0] < mkz)
		mkz = mat->rows[i][0];
	}
    return ((mkz-1) * mat->wt[GETJ(mat, j)]);
}

// TODO: make this look like fillmat??? or initSWAR???
void
MkzInit(sparse_mat_t *mat)
{
    INT j, mkz;
    int sz = mat->jmax - mat->jmin;

    printf("Entering initMarkowitz\n");
    mat->MKZQ = (INT *)malloc((sz+1) * 2 * sizeof(INT));
    mat->MKZA = (INT *)malloc((sz+1) * sizeof(INT));
    // just to understand
    for(j = mat->jmin; j < mat->jmax; j++)
	if(mat->wt[GETJ(mat, j)] > 0){
	    mkz = MkzCount(mat, j);
#if MKZ_DEBUG >= 2
	    printf("j=%d wt=%d", j, mat->wt[GETJ(mat, j)]);
	    printf(" => mkz=%d\n", mkz);
#endif
	    MkzInsert(mat->MKZQ, mat->MKZA, GETJ(mat, j), mkz);
	}
    // TODO: overflow in mkz???
    MkzCheck(mat);
#if MKZ_DEBUG >= 1
    fprintf(stderr, "Initial queue is\n");
    MkzPrintQueue(mat->MKZQ);
#endif
}

void
MkzClose(sparse_mat_t *mat)
{
    free(mat->MKZQ);
}

int
MkzAddCol(sparse_mat_t *mat, INT j)
{
    int ind = mat->wt[GETJ(mat, j)] = incrS(mat->wt[GETJ(mat, j)]);

    return ind;
}

void
MkzUpdate(sparse_mat_t *mat, INT j)
{
    INT adr = mat->MKZA[GETJ(mat, j)];
    int mkz = MkzCount(mat, j);

    // nothing to do if new count == old count
    if(mkz != MkzGet(mat->MKZQ, adr, 0)){
	// remove old count
	// add new count
    }
}

/* remove the cell (i,j), and updates matrix correspondingly.
   
   Updates:
   - mat->wt[j] (weight of column j)
   - (Q, A)

   We arrive here when mat->wt[j] > 0.

*/
void
MkzRemoveCol(sparse_mat_t *mat, INT j)
{
    INT dj = GETJ(mat, j);

    mat->wt[dj] = decrS(mat->wt[dj]);
    // remove j from the QA structure
    MkzDelete(mat->MKZQ, mat->MKZA, mat->MKZA[dj]);
    mat->MKZA[dj] = MKZ_INF;
#if MKZ_DEBUG >= 1
    MkzIsHeap(mat->MKZQ);
#endif
}

void
MkzRemoveJ(sparse_mat_t *mat, INT j)
{
    INT dj = GETJ(mat, j);

    mat->wt[dj] = 0;
    // remove j from the QA structure
    MkzDelete(mat->MKZQ, mat->MKZA, mat->MKZA[dj]);
    mat->MKZA[dj] = MKZ_INF;
#if MKZ_DEBUG >= 1
    MkzIsHeap(mat->MKZQ);
#endif
}

#endif
