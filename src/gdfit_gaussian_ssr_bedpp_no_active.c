#include <math.h>
#include <string.h>
#include "Rinternals.h"
#include "R_ext/Rdynload.h"
#include <R.h>
#include <R_ext/Applic.h>
int checkConvergence(double *beta, double *beta_old, double eps, int l, int J);
double crossprod(double *x, double *y, int n, int j);
double norm(double *x, int p);
double S(double z, double l);
double gLoss(double *r, int n);
int sum_rejections(int *x, int n);
// Group descent update
void gd_gaussian_ssr(double *b, double *x, double *r, int g, int *K1, int *K, int n, int l, int p, const char *penalty, double lam1, double lam2, double gamma, SEXP df, double *a);
// sequential strong rule
void ssr_glasso(int *e2, double *xTr, int *K1, int *K, double *lam, double lam_max, int l, int J);
// BEDPP initialization
void bedpp_init(double *yTxxTv1, double *xTv1_sq, double *xTy_sq, double *xTr,double *X, double *y, int *K1, int *K, int *g_star_ptr, int *K_star_ptr, int K1_len, int n, int J);
// basic EDPP screening
void bedpp_glasso(int *e3, double *yTxxTv1, double *xTv1_sq, double *xTy_sq, double y_norm_sq, int *K, double lam, double lam_max, int K_star, int n, int J);
// hybrid sequential safe-strong rule: SSR-BEDPP
void ssr_bedpp_glasso(int *e2, int *e3, double *xTr, int *K1, int *K, double *lam, double lam_max, int l, int J);
// update xTr[g] for groups which are rejected at previous lambda but accepted at current one.
void update_xTr(int *e3, int *e3_old, double *xTr, double *X, double *r, int *K1, int *K, int n, int J);
// Scan for violations in rest set (no active cycling)
int check_rest_set_no_active(int *e2, double *xTr, double *X, double *r, int *K1, int *K, double lam, int n, int J);

SEXP cleanupG_ssr_bedpp_no_active(double *a, double *r, int *e2, int *e3, int *e3_old, int *K, 
                                  double *yTxxTv1, double *xTv1_sq, double *xTy_sq, double *xTr,
                                  SEXP beta, SEXP iter, SEXP df, SEXP loss, SEXP rejections, SEXP safe_rejections) {
  Free(a);
  Free(r);
  Free(e2);
  Free(e3);
  Free(e3_old);
  Free(K);
  Free(yTxxTv1);
  Free(xTv1_sq);
  Free(xTy_sq);
  Free(xTr);
  SEXP res;
  PROTECT(res = allocVector(VECSXP, 6));
  SET_VECTOR_ELT(res, 0, beta);
  SET_VECTOR_ELT(res, 1, iter);
  SET_VECTOR_ELT(res, 2, df);
  SET_VECTOR_ELT(res, 3, loss);
  SET_VECTOR_ELT(res, 4, rejections);
  SET_VECTOR_ELT(res, 5, safe_rejections);
  UNPROTECT(7);
  return(res);
}


// check rest set with SSR-BEDPP screening (no active cycling)
int check_rest_set_ssr_bedpp_no_active(int *e2, int *e3, double *xTr, double *X, double *r, int *K1, int *K, double lam, int n, int J) {
  int violations = 0;
  for (int g = 0; g < J; g++) {
    if (e3[g] == 1 && e2[g] == 0) { // check groups not rejected by BEDPP but by SSR
      double *z = Calloc(K[g], double);
      for (int j = K1[g]; j < K1[g+1]; j++) {
        z[j-K1[g]] = crossprod(X, r, n, j) / n;
      }
      xTr[g] = norm(z, K[g]);
      if (xTr[g] > lam * sqrt(K[g])) {
        e2[g] = 1;
        violations++;
      }
      Free(z);
    }
  }
  return violations;
}


SEXP gdfit_gaussian_ssr_bedpp_no_active(SEXP X_, SEXP y_, SEXP penalty_, SEXP K1_, SEXP K0_, 
                        SEXP lambda, SEXP lam_max_, SEXP alpha_, SEXP eps_, 
                        SEXP max_iter_, SEXP gamma_, SEXP group_multiplier, 
                        SEXP dfmax_, SEXP gmax_, SEXP user_) {

  // Lengths/dimensions
  int n = length(y_);
  int L = length(lambda);
  int J = length(K1_) - 1;
  int p = length(X_)/n;
  
  // Pointers
  double *X = REAL(X_);
  double *y = REAL(y_);
  const char *penalty = CHAR(STRING_ELT(penalty_, 0));
  int *K1 = INTEGER(K1_);
  int K0 = INTEGER(K0_)[0];
  double *lam = REAL(lambda);
  double lam_max = REAL(lam_max_)[0];
  double alpha = REAL(alpha_)[0];
  double eps = REAL(eps_)[0];
  int max_iter = INTEGER(max_iter_)[0];
  double gamma = REAL(gamma_)[0];
  double *m = REAL(group_multiplier);
  int dfmax = INTEGER(dfmax_)[0];
  int gmax = INTEGER(gmax_)[0];
  int user = INTEGER(user_)[0];

  // Outcome
  SEXP res, beta, iter, df, loss, rejections, safe_rejections;
  PROTECT(beta = allocVector(REALSXP, L*p));
  for (int j=0; j<(L*p); j++) REAL(beta)[j] = 0;
  PROTECT(iter = allocVector(INTSXP, L));
  for (int i=0; i<L; i++) INTEGER(iter)[i] = 0;
  PROTECT(df = allocVector(REALSXP, L));
  for (int i=0; i<L; i++) REAL(df)[i] = 0;
  PROTECT(loss = allocVector(REALSXP, L));
  for (int i=0; i<L; i++) REAL(loss)[i] = 0;
  double *b = REAL(beta);
  PROTECT(rejections = allocVector(INTSXP, L));
  for (int i=0; i<L; i++) INTEGER(rejections)[i] = 0;
  PROTECT(safe_rejections = allocVector(INTSXP, L)); // # of rejections by BEDPP
  for (int i=0; i<L; i++) INTEGER(safe_rejections)[i] = 0;

  // Intermediate quantities
  double *r = Calloc(n, double);
  for (int i=0; i<n; i++) r[i] = y[i];
  double *a = Calloc(p, double);
  for (int j=0; j<p; j++) a[j] = 0;
  int converged, lstart = 0, ng, nv, violations;
  double shift, l1, l2;

  // variables for screening
  int *e2 = Calloc(J, int); // strong set
  int *e3 = Calloc(J, int); // BEDPP set
  int *e3_old = Calloc(J, int); // previous BEDPP set
  for (int g=0; g<J; g++) e3_old[g] = 1; // initialize e3_old = 1, not reject.
  int *K = Calloc(J, int); // group size
  int K1_len = 0; // # of variables in K1
  for (int g = 0; g < J; g++) {
    K[g] = K1[g+1] - K1[g];
    K1_len += K[g];
  }
  
  double *xTr = Calloc(J, double);
  double *xTy_sq = Calloc(J, double); // tmp quantity for BEDPP: square norm of X^T*y
  double *yTxxTv1 = Calloc(J, double); // tmp quantity for BEDPP: y^T*X*X^T v1
  double *xTv1_sq = Calloc(J, double); // tmp quantity for BEDPP: square norm of X^T*v1
  int g_star, K_star; // group index and size corresponding to lambda_max
  int *g_star_ptr = &g_star;
  int *K_star_ptr = &K_star;
  double y_norm_sq = pow(norm(y, n), 2);
  
  // pre-compute tmp quantities used for screening
  bedpp_init(yTxxTv1, xTv1_sq, xTy_sq, xTr, X, y, K1, K, g_star_ptr, K_star_ptr, K1_len, n, J);

  // If lam[0]=lam_max, skip lam[0] -- closed form sol'n available
  if (user) {
    lstart = 0;
  } else {
    REAL(loss)[0] = gLoss(r,n);
    INTEGER(rejections)[0] = J;
    INTEGER(safe_rejections)[0] = J;
    lstart = 1;
  }

  // Path
  int bedpp_flag = 1; // whether stop BEDPP or not: 1 means not stop.
  for (int l=lstart; l<L; l++) {
    R_CheckUserInterrupt();
    if (l != 0) {
      for (int j=0; j<p; j++) a[j] = b[(l-1)*p+j];

      // Check dfmax, gmax
      ng = 0;
      nv = 0;
      for (int g=0; g<J; g++) {
      	if (a[K1[g]] != 0) {
      	  ng++;
      	  nv = nv + (K1[g+1]-K1[g]);
      	}
      }
      if (ng > gmax | nv > dfmax) {
        for (int ll=l; ll<L; ll++) INTEGER(iter)[ll] = NA_INTEGER;
        res = cleanupG_ssr_bedpp_no_active(a, r, e2, e3, e3_old, K, yTxxTv1, xTv1_sq, xTy_sq, xTr,
                                 beta, iter, df, loss, rejections, safe_rejections);
        return(res);
      }
    }
    if (bedpp_flag) { //SSR-BEDPP screening
      // BEDPP screening
      bedpp_glasso(e3, yTxxTv1, xTv1_sq, xTy_sq, y_norm_sq, K, lam[l], lam_max, K_star, n, J);
      INTEGER(safe_rejections)[l] = J - sum_rejections(e3, J);
      // update xTr[g] for groups which are rejected at previous lambda but accepted at current one.
      update_xTr(e3, e3_old, xTr, X, r, K1, K, n, J);
      for (int g=0; g<J; g++) e3_old[g] = e3[g]; // reset e3_old to be new e3;
      // SSR screening
      ssr_bedpp_glasso(e2, e3, xTr, K1, K, lam, lam_max, l, J);
    } else { // only SSR screening
      INTEGER(safe_rejections)[l] = 0;
      ssr_glasso(e2, xTr, K1, K, lam, lam_max, l, J);
    }
    INTEGER(rejections)[l] = J - sum_rejections(e2, J);
    if (INTEGER(safe_rejections)[l] <= 0) bedpp_flag = 0; // BEDPP not effective, turn it off for next lambda.
    
    while (INTEGER(iter)[l] < max_iter) {
      while (INTEGER(iter)[l] < max_iter) {
        converged = 0;
        INTEGER(iter)[l]++;
        REAL(df)[l] = 0;
        
        // Update unpenalized covariates
        for (int j=0; j<K0; j++) {
          shift = crossprod(X, r, n, j)/n;
          b[l*p+j] = shift + a[j];
          for (int i=0; i<n; i++) r[i] -= shift * X[n*j+i];
          REAL(df)[l] += 1;
        }
        
        // Update penalized groups
        for (int g=0; g<J; g++) {
          l1 = lam[l] * m[g] * alpha;
          l2 = lam[l] * m[g] * (1-alpha);
          if (e2[g]) {
            gd_gaussian_ssr(b, X, r, g, K1, K, n, l, p, penalty, l1, l2, gamma, df, a);
          }
        }
        
        // Check convergence
        if (checkConvergence(b, a, eps, l, p)) {
          converged  = 1;
          REAL(loss)[l] = gLoss(r,n);
          break;
        }
        for (int j=0; j<p; j++) a[j] = b[l*p+j];
      }
      
      // Scan for violations in rest set
      if (bedpp_flag) {
        violations = check_rest_set_ssr_bedpp_no_active(e2, e3, xTr, X, r, K1, K, lam[l], n, J);
      } else {
        violations = check_rest_set_no_active(e2, xTr, X, r, K1, K, lam[l], n, J);
      }
      if (violations == 0) {
        REAL(loss)[l] = gLoss(r, n);
        break;
      }
    }
  }
  res = cleanupG_ssr_bedpp_no_active(a, r, e2, e3, e3_old, K, yTxxTv1, xTv1_sq, xTy_sq, xTr,
                                     beta, iter, df, loss, rejections, safe_rejections);
  return(res);
}
