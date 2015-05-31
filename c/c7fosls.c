
static char help[] = 
"Solves a structured-grid Poisson problem by re-formulating it as\n"
"an over-determined first-order system and then solving the normal equations\n"
"for that system.  Uses DMDA and KSP.\n\n";

#include <math.h>
#include <petscdmda.h>
#include <petscksp.h>

//COMPUTERHS
PetscErrorCode ComputeRHS(KSP ksp, Vec b, void *ctx) {
  PetscErrorCode ierr;
  DMDALocalInfo  info;
  DM             da;

  PetscFunctionBeginUser;
  ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);

  PetscInt       i, j;
  PetscReal      hx = 1./(double)(info.mx-1),
                 hy = 1./(double)(info.my-1),  // domain is [0,1] x [0,1]
                 x, y, x2, y2, f, **ab;
  ierr = DMDAVecGetArray(da, b, &ab);CHKERRQ(ierr);
  for (j=info.ys; j<info.ys+info.ym; j++) {
    y = j * hy;  y2 = y*y;
    for (i=info.xs; i<info.xs+info.xm; i++) {
      x = i * hx;  x2 = x*x;
      // this is example page 64 of Briggs et al 2000
      if ( (i>0) && (i<info.mx-1) && (j>0) && (j<info.my-1) ) { // if not bdry
        f = 2.0 * ( (1.0 - 6.0*x2) * y2 * (1.0 - y2) + (1.0 - 6.0*y2) * x2 * (1.0 - x2) );
        ab[j][i] = hx * hy * f;
      } else {
        ab[j][i] = 0.0;                          // on bdry we have "1 * u = 0"
      }
    }
  }
  ierr = DMDAVecRestoreArray(da, b, &ab);CHKERRQ(ierr);

  ierr = VecAssemblyBegin(b); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(b); CHKERRQ(ierr);
  
  FIXME: call BuildL()
  FIXME: call MatMultTranspose() to get L^* b
  return 0;
}
//ENDCOMPUTERHS


//COMPUTEJAC
PetscErrorCode ComputeA(KSP ksp,Mat J, Mat A,void *ctx) {
  PetscErrorCode ierr;
  PetscInt       i, j;
  PetscScalar    hx, hy;
  DM             da;
  DMDALocalInfo  info;

  PetscFunctionBeginUser;
  ierr  = KSPGetDM(ksp,&da);CHKERRQ(ierr);

  ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
  hx   = 1.0/(PetscReal)(info.mx);
  hy   = 1.0/(PetscReal)(info.my);

  for (j=info.ys; j<info.ys+info.ym; j++) {
    for (i=info.xs; i<info.xs+info.xm; i++) {
      MatStencil  row, col[5];
      PetscReal   v[5];
      PetscInt    ncols = 0;
      row.j = j;               // row of A corresponding to the unknown at (x_i,y_j)
      row.i = i;
      col[ncols].j = j;        // in that diagonal entry ...
      col[ncols].i = i;
      if ( (i==0) || (i==info.mx-1) || (j==0) || (j==info.my-1) ) { // ... on bdry
        v[ncols++] = 1.0;
      } else {
        v[ncols++] = 2*(hy/hx + hx/hy); // ... everywhere else we build a row
        // if neighbor is NOT known to be zero we put an entry:
        if (i-1>0) {
          col[ncols].j = j;    col[ncols].i = i-1;  v[ncols++] = -hy/hx;  }
        if (i+1<info.mx-1) {
          col[ncols].j = j;    col[ncols].i = i+1;  v[ncols++] = -hy/hx;  }
        if (j-1>0) {
          col[ncols].j = j-1;  col[ncols].i = i;    v[ncols++] = -hx/hy;  }
        if (j+1<info.my-1) {
          col[ncols].j = j+1;  col[ncols].i = i;    v[ncols++] = -hx/hy;  }
      }
      ierr = MatSetValuesStencil(A,1,&row,ncols,col,v,INSERT_VALUES); CHKERRQ(ierr);
    }
  }

  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  
  FIXME: now call MatTransposeMatMult(L,L,MAT_REUSE_MATRIX,PETSC_DEFAULT,&A)
  PetscFunctionReturn(0);
}
//ENDCOMPUTEJAC

//MAIN
int main(int argc,char **argv)
{
  KSP            ksp;
  DM             da;
  PetscErrorCode ierr;

  PetscInitialize(&argc,&argv,(char*)0,help);

  ierr = DMDACreate2d(PETSC_COMM_WORLD,
                DM_BOUNDARY_PERIODIC, DM_BOUNDARY_PERIODIC,
                DMDA_STENCIL_STAR,                          // yes, star
                -4,-4,PETSC_DECIDE,PETSC_DECIDE,
                3,1,NULL,NULL,                              // 3 values u,v,w
                &da); CHKERRQ(ierr);
  ierr = DMDASetUniformCoordinates(da,0.0,1.0,0.0,1.0,-1.0,-1.0); CHKERRQ(ierr);

  ierr = KSPCreate(PETSC_COMM_WORLD,&ksp);CHKERRQ(ierr);
  ierr = KSPSetDM(ksp,(DM)da);
  ierr = KSPSetComputeRHS(ksp,ComputeRHS,NULL);CHKERRQ(ierr);
  ierr = KSPSetComputeOperators(ksp,ComputeA,NULL);CHKERRQ(ierr);
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);

  PetscLogStage  stage;
  ierr = PetscLogStageRegister("Solve", &stage); CHKERRQ(ierr);
  ierr = PetscLogStagePush(stage); CHKERRQ(ierr);
  ierr = KSPSolve(ksp,NULL,NULL);CHKERRQ(ierr);
  ierr = PetscLogStagePop();CHKERRQ(ierr);

  // report on grid, ksp results
  PetscInt       its;
  PetscScalar    resnorm;
  DMDALocalInfo  info;
  ierr = DMDAGetLocalInfo(da,&info);CHKERRQ(ierr);
  ierr = KSPGetIterationNumber(ksp,&its); CHKERRQ(ierr);
  ierr = KSPGetResidualNorm(ksp,&resnorm); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
             "on %4d x %4d grid:  iterations %D, residual norm = %g\n",
             info.mx,info.my,its,resnorm); CHKERRQ(ierr);

  ierr = DMDestroy(&da);CHKERRQ(ierr);
  ierr = KSPDestroy(&ksp);CHKERRQ(ierr);
  ierr = PetscFinalize();CHKERRQ(ierr);
  return 0;
}
//ENDMAIN
