
static char help[] =
"Read in a FEM grid (unstructured triangulation) from PETSc binary file in parallel.\n\
Demonstrate Mat preallocation.\n\
For a one-process, coarse grid example do:\n\
     triangle -pqa1.0 bump   # generates bump.1.{node,ele,poly}\n\
     c2triangle -f bump.1    # reads bump.1.{node,ele,poly} and generates bump.1.petsc\n\
     c2prealloc -f bump.1    # reads bump.1.petsc\n\
To see the sparsity pattern graphically:\n\
     c2prealloc -f bump.1 -mat_view draw -draw_pause 5\n\n";

// SUMMARY FROM PETSC MANUAL
/*
For (vertex-based) finite element type calculations, an analogous procedure is as follows:
  - Allocate integer array nnz.
  - Loop over vertices, computing the number of neighbor vertices, which determines the
number of nonzeros for the corresponding matrix row(s).
  - Create the sparse matrix via MatCreateSeqAIJ() or alternative.
  - Loop over elements, generating matrix entries and inserting in matrix via MatSetValues().
*/

#include <petscmat.h>
#include <petscksp.h>
#define DEBUG 1
#define matassembly(X) { ierr = MatAssemblyBegin(X,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr); \
                         ierr = MatAssemblyEnd(X,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr); }

int main(int argc,char **args) {

  // STANDARD PREAMBLE
  PetscInitialize(&argc,&args,(char*)0,help);
  const MPI_Comm  COMM = PETSC_COMM_WORLD;
  PetscMPIInt     rank;
  MPI_Comm_rank(COMM,&rank);
  const PetscInt  MPL = PETSC_MAX_PATH_LEN;
  PetscErrorCode  ierr;

  // MAJOR VARIABLES FOR TRIANGULAR MESH
  PetscInt N,   // number of degrees of freedom (= number of all nodes)
           K,   // number of elements
           M;   // number of boundary segments
  Vec      x, y,     // mesh:  x coord of node, y coord of node
           BT, P, Q; // mesh: bdry type, element indexing, boundary segment indexing

  // GET FILENAME FROM OPTION
  char           fname[MPL];
  PetscBool      fset;
  ierr = PetscOptionsBegin(COMM, "", "options for c2prealloc", ""); CHKERRQ(ierr);
  ierr = PetscOptionsString("-f", "filename root with PETSc binary, for reading", "", "",
                            fname, sizeof(fname), &fset); CHKERRQ(ierr);
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);
  if (!fset) {
    SETERRQ(COMM,1,"option  -f FILENAME  required");
  }
  strcat(fname,".petsc");

  // ALLOCATE AND READ IN PARALLEL: NODE INFO
  PetscViewer viewer;
  ierr = PetscPrintf(COMM,"reading x,y,BT,P,Q from %s in parallel ...\n",fname); CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,fname,FILE_MODE_READ,
             &viewer); CHKERRQ(ierr);
  ierr = VecCreate(COMM,&x); CHKERRQ(ierr);
  ierr = VecCreate(COMM,&y); CHKERRQ(ierr);
  ierr = VecCreate(COMM,&BT); CHKERRQ(ierr);
  ierr = VecCreate(COMM,&P); CHKERRQ(ierr);
  ierr = VecCreate(COMM,&Q); CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)x,"node-x-coordinate"); CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)y,"node-y-coordinate"); CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)BT,"node-boundary-type"); CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)P,"element-node-indices"); CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)Q,"boundary-segment-indices"); CHKERRQ(ierr);
  ierr = VecLoad(x,viewer); CHKERRQ(ierr);
  ierr = VecLoad(y,viewer); CHKERRQ(ierr);
  ierr = VecLoad(BT,viewer); CHKERRQ(ierr);
  ierr = VecLoad(P,viewer); CHKERRQ(ierr);
  ierr = VecLoad(Q,viewer); CHKERRQ(ierr);

  ierr = VecGetSize(x,&N); CHKERRQ(ierr);
  ierr = VecGetSize(P,&K); CHKERRQ(ierr);
  ierr = VecGetSize(Q,&M); CHKERRQ(ierr);
  if (K % 3 != 0) {
    SETERRQ(COMM,3,"element node index array P invalid: must have 3 K entries");
  }
  K /= 3;
  if (M % 2 != 0) {
    SETERRQ(COMM,3,"element node index array Q invalid: must have 2 M entries");
  }
  M /= 2;
  ierr = PetscPrintf(COMM,"  N=%d nodes, K=%d elements, M=%d boundary segments\n",N,K,M); CHKERRQ(ierr);

  // PUT A COPY OF THE FULL BT,P,Q ON EACH PROCESSOR
  VecScatter  ctx;
  Vec         BTSEQ, PSEQ, QSEQ;
  ierr = VecScatterCreateToAll(BT,&ctx,&BTSEQ); CHKERRQ(ierr);
  ierr = VecScatterBegin(ctx,BT,BTSEQ,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  ierr = VecScatterEnd(ctx,BT,BTSEQ,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  VecScatterDestroy(&ctx);
  ierr = VecScatterCreateToAll(P,&ctx,&PSEQ); CHKERRQ(ierr);
  ierr = VecScatterBegin(ctx,P,PSEQ,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  ierr = VecScatterEnd(ctx,P,PSEQ,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  VecScatterDestroy(&ctx);
  ierr = VecScatterCreateToAll(Q,&ctx,&QSEQ); CHKERRQ(ierr);
  ierr = VecScatterBegin(ctx,Q,QSEQ,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  ierr = VecScatterEnd(ctx,Q,QSEQ,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  VecScatterDestroy(&ctx);

  // LEARN WHICH ROWS WE OWN
  PetscInt Istart,Iend;
  ierr = VecGetOwnershipRange(x,&Istart,&Iend); CHKERRQ(ierr);

  // ALLOCATE LOCAL ARRAYS FOR NUMBER OF NONZEROS
  PetscInt mm = Iend - Istart, iloc;
  int *dnnz, // dnnz[i] is number of nonzeros in row which are in same-processor column
      *onnz; // onnz[i] is number of nonzeros in row which are in other-processor column
  PetscMalloc(mm*sizeof(int),&dnnz);
  PetscMalloc(mm*sizeof(int),&onnz);
  for (iloc = 0; iloc < mm; iloc++) {
    dnnz[iloc] = 2;  // diagonal entry
    onnz[iloc] = 0;
  }

  // FILL THE NUMBER-OF-NONZEROS ARRAYS
  PetscInt    i, j, k, m, q, r;
  PetscScalar *abt, *ap, *aq;
  ierr = VecGetArray(BTSEQ,&abt); CHKERRQ(ierr);
  ierr = VecGetArray(PSEQ,&ap); CHKERRQ(ierr);
  for (k = 0; k < K; k++) {          // loop over ALL elements
    for (q = 0; q < 3; q++) {        // loop over vertices of current element
      i = (int)ap[3*k+q];            //   global index of q node
      if ((i < Istart) || (i >= Iend))  continue; // skip node if I don't own it
      iloc = i - Istart;
      for (r = 0; r < 3; r++) {      // loop over other vertices
        if (r == q)  continue;       // diagonal entry already counted
        j = (int)ap[3*k+r];          //   global index of r node
        // (i,j) is an edge; we count this nonzero matrix entry
        if ((j >= Istart) && (j < Iend)) {
          dnnz[iloc]++;
        } else {
          onnz[iloc]++;
        }
      }
    }
  }
  ierr = VecRestoreArray(PSEQ,&ap); CHKERRQ(ierr);
  ierr = VecRestoreArray(BTSEQ,&abt); CHKERRQ(ierr);

  ierr = VecGetArray(QSEQ,&aq); CHKERRQ(ierr);
  for (m = 0; m < M; m++) {          // loop over ALL boundary segments
    for (q = 0; q < 2; q++) {        // loop over vertices of current segment
      i = (int)aq[2*m+q];            //   global index of q node
      if ((i < Istart) || (i >= Iend))  continue; // skip node if I don't own it
      iloc = i - Istart;
      r = 1 - q;                     // other vertex
      j = (int)aq[2*m+r];            //   global index of r node
      // (i,j) is a boundary segment; we count this nonzero matrix entry AGAIN
      if ((j >= Istart) && (j < Iend)) {
        dnnz[iloc]++;
      } else {
        onnz[iloc]++;
      }
    }
  }
  ierr = VecRestoreArray(QSEQ,&aq); CHKERRQ(ierr);
  // resolve double counting
  for (iloc = 0; iloc < mm; iloc++) {
    dnnz[iloc] /= 2;
    onnz[iloc] /= 2;
  }
#if DEBUG
  ierr = PetscSynchronizedPrintf(COMM,"showing entries of dnnz[%d] on rank %d (DEBUG)\n",mm,rank); CHKERRQ(ierr);
  for (iloc = 0; iloc < mm; iloc++) {
      ierr = PetscSynchronizedPrintf(COMM,"dnnz[%d] = %d\n",iloc,dnnz[iloc]); CHKERRQ(ierr);
  }
  ierr = PetscSynchronizedPrintf(COMM,"showing entries of onnz[%d] on rank %d (DEBUG)\n",mm,rank); CHKERRQ(ierr);
  for (iloc = 0; iloc < mm; iloc++) {
      ierr = PetscSynchronizedPrintf(COMM,"onnz[%d] = %d\n",iloc,onnz[iloc]); CHKERRQ(ierr);
  }
  ierr = PetscSynchronizedFlush(COMM,PETSC_STDOUT); CHKERRQ(ierr);
#endif

  // PREALLOCATE STIFFNESS MATRIX
  Mat A;
  MatCreate(COMM,&A);
  MatSetType(A,MATMPIAIJ);
  MatSetSizes(A,mm,mm,N,N);
  //"If the *_nnz parameter is given then the *_nz parameter is ignored"
  MatMPIAIJSetPreallocation(A,0,dnnz,0,onnz);
  MatSetOption(A, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE); // FIXME: WHY IS THIS NEEDED?

  // FILL MAT WITH FAKE ENTRIES
  PetscInt    jj[3];
  PetscScalar vv[3];
  ierr = VecGetArray(PSEQ,&ap); CHKERRQ(ierr);
  for (k = 0; k < K; k++) {          // loop over ALL elements
    for (q = 0; q < 3; q++) {        // loop over vertices of current element
      i = (int)ap[3*k+q];            //   global index of q node
      if ((i < Istart) || (i >= Iend))  continue; // skip node if I don't own it
      for (r = 0; r < 3; r++) {      // loop over other vertices
        jj[r] = (int)ap[3*k+r];      //   global index of r node
        vv[r] = 1.0;
      }
      ierr = MatSetValues(A,1,&i,3,jj,vv,ADD_VALUES); CHKERRQ(ierr);
    }
  }
  ierr = VecRestoreArray(PSEQ,&ap); CHKERRQ(ierr);
  matassembly(A)

  // CLEAN UP
  PetscFree(dnnz);
  PetscFree(onnz);
  MatDestroy(&A);
  VecDestroy(&x);
  VecDestroy(&y);
  VecDestroy(&BT);
  VecDestroy(&P);
  VecDestroy(&Q);
  VecDestroy(&BTSEQ);
  VecDestroy(&PSEQ);
  VecDestroy(&QSEQ);
  PetscViewerDestroy(&viewer);

  PetscFinalize();
  return 0;
}
