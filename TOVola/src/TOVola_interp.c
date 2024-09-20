#include "GRHayLib.h" //Access to GRHayL library in the ET
#include "TOVola_solve.c" //Mainly for access to the TOVola_ID_persist struct
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_odeiv2.h>

#include <cctk.h>
#include <cctk_Arguments.h>
#include <cctk_Parameters.h>
    
/* Bisection index finder using binary search */
static int TOVola_bisection_idx_finder(const CCTK_REAL rr_iso, const int numpoints_arr, const CCTK_REAL *restrict r_iso_arr) {
  int x1 = 0;
  int x2 = numpoints_arr - 1;
  CCTK_REAL y1 = rr_iso - r_iso_arr[x1];
  CCTK_REAL y2 = rr_iso - r_iso_arr[x2];
  if (y1 * y2 > 0) {
    CCTK_VINFO("INTERPOLATION BRACKETING ERROR: r_iso_min = %e ?<= r_iso = %.15e ?<= %e = r_iso_max\n", r_iso_arr[0], rr_iso,
            r_iso_arr[numpoints_arr - 1]);
    CCTK_ERROR("Shutting down due to error...");
  }
  for (int i = 0; i < numpoints_arr; i++) {
    int x_midpoint = (x1 + x2) / 2;
    CCTK_REAL y_midpoint = rr_iso - r_iso_arr[x_midpoint];
    if (y_midpoint * y1 <= 0) {
      x2 = x_midpoint;
      y2 = y_midpoint;
    } else {
      x1 = x_midpoint;
      y1 = y_midpoint;
    }
    if (abs(x2 - x1) == 1) {
      // If r_iso_arr[x1] is closer to rr_iso than r_iso_arr[x2] then return x1:
      if (fabs(rr_iso - r_iso_arr[x1]) < fabs(rr_iso - r_iso_arr[x2])) {
        return x1;
      }
      // Otherwise return x2:
      return x2;
    }
  }
  CCTK_VINFO("INTERPOLATION BRACKETING ERROR: r_iso_min = %e ?<= r_iso = %.15e ?<= %e = r_iso_max\n", r_iso_arr[0], rr_iso,
            r_iso_arr[numpoints_arr - 1]);
  CCTK_ERROR("Shutting down due to error...");
}

/* Interpolation Function using Lagrange Polynomial */
static void TOVola_TOV_interpolate_1D(CCTK_REAL rr_iso,
                                      const int interpolation_stencil_size, const int numpoints_arr, const CCTK_REAL *restrict r_Schw_arr,
                                      const CCTK_REAL *restrict rho_energy_arr, const CCTK_REAL *restrict rho_baryon_arr, const CCTK_REAL *restrict P_arr,
                                      const CCTK_REAL *restrict M_arr, const CCTK_REAL *restrict expnu_arr, const CCTK_REAL *restrict exp4phi_arr,
                                      const CCTK_REAL *restrict r_iso_arr, CCTK_REAL *restrict rho_energy, CCTK_REAL *restrict rho_baryon, CCTK_REAL *restrict P,
                                      CCTK_REAL *restrict M, CCTK_REAL *restrict expnu, CCTK_REAL *restrict exp4phi) {
  
  DECLARE_CCTK_PARAMETERS	
	
  const int R_idx = numpoints_arr - 1;
  const CCTK_REAL M_star = M_arr[R_idx];
  const CCTK_REAL r_iso_max_inside_star = r_iso_arr[R_idx];
  CCTK_REAL r_Schw = 0.0;
  if (rr_iso < r_iso_max_inside_star) { // If we are INSIDE the star, we need to interpollate the data to the grid.
    // For this case, we know that for all functions, f(r) = f(-r)
    if (rr_iso < 0)
      rr_iso = -rr_iso;

    // First find the central interpolation stencil index:
    int idx_mid = TOVola_bisection_idx_finder(rr_iso, numpoints_arr, r_iso_arr);

    /* Use standard library functions instead of redefining macros */
    int idxmin = MAX(0, idx_mid - TOVola_Interpolation_Stencil / 2 - 1);

    // -= Do not allow the interpolation stencil to cross the star's surface =-
    // max index is when idxmin + (TOVola_Interpolation_stencil-1) = R_idx
    //  -> idxmin at most can be R_idx - TOVola_Interpolation_stencil + 1
    idxmin = MIN(idxmin, R_idx - TOVola_Interpolation_Stencil + 1);

    // Ensure that TOVola_Interpolation_Stencil does not exceed the maximum
    if (TOVola_Interpolation_Stencil > TOVola_Max_Interpolation_Stencil) {
      CCTK_ERROR("Interpolation stencil size exceeds maximum allowed.\n");
    }

    // Now perform the Lagrange polynomial interpolation:

    // First compute the interpolation coefficients:
    CCTK_REAL r_iso_sample[TOVola_Max_Interpolation_Stencil];
    for (int i = idxmin; i < idxmin + TOVola_Interpolation_Stencil; i++) {
      //if(i < 0 || i >= R_idx-1) { fprintf(stderr, "ERROR!\n"); exit(1); }
      r_iso_sample[i - idxmin] = r_iso_arr[i];
    }
    CCTK_REAL l_i_of_r[TOVola_Max_Interpolation_Stencil];
    for (int i = 0; i < TOVola_Interpolation_Stencil; i++) {
      CCTK_REAL numer = 1.0;
      CCTK_REAL denom = 1.0;
      for (int j = 0; j < TOVola_Interpolation_Stencil; j++) {
        if (j != i) {
          numer *= (rr_iso - r_iso_sample[j]);
          denom *= (r_iso_sample[i] - r_iso_sample[j]);
        }
      }
      l_i_of_r[i] = numer / denom;
    }

    // Then perform the interpolation:
    *rho_energy = 0.0;
    *rho_baryon = 0.0;
    *P = 0.0;
    *M = 0.0;
    *expnu = 0.0;
    *exp4phi = 0.0;

    for (int i = idxmin; i < idxmin + TOVola_Interpolation_Stencil; i++) {
      r_Schw += l_i_of_r[i - idxmin] * r_Schw_arr[i];
      *rho_energy += l_i_of_r[i - idxmin] * rho_energy_arr[i];
      *rho_baryon += l_i_of_r[i - idxmin] * rho_baryon_arr[i];
      *P += l_i_of_r[i - idxmin] * P_arr[i];
      *M += l_i_of_r[i - idxmin] * M_arr[i];
      *expnu += l_i_of_r[i - idxmin] * expnu_arr[i];
      *exp4phi += l_i_of_r[i - idxmin] * exp4phi_arr[i];
    }

  } else {
    // If we are OUTSIDE the star, the solution is just Schwarzschild.
    r_Schw = (rr_iso + M_star) + M_star * M_star / (4.0 * rr_iso); // Need to know what r_Schw is at our current grid location.
    *rho_energy = 0;
    *rho_baryon = 0;
    *P = 0;
    *M = M_star;
    *expnu = 1. - 2.0 * (M_star) / r_Schw;
    *exp4phi = (r_Schw * r_Schw) / (rr_iso * rr_iso);
  }
  //printf("%.15e %.15e %.15e %.15e %.15e %.15e %.15e %.15e hhhh\n", rr_iso, r_Schw, *rho_energy, *rho_baryon, *P, *M, *expnu, *exp4phi);
}

void TOVola_interp(CCTK_ARGUMENTS){

  DECLARE_CCTK_ARGUMENTS
  DECLARE_CCTK_PARAMETERS
/*
  const CCTK_REAL x = xCart[0];
  const CCTK_REAL y = xCart[1];
  const CCTK_REAL z = xCart[2];
  const CCTK_REAL r_iso = sqrt(x * x + y * y + z * z);
  // self.xxSph[1] = sp.acos(self.xx[2] / self.xxSph[0])
  const CCTK_REAL theta = acos(z / r_iso);

  // Perform pointwise interpolation to radius r using TOVola_ID_persist data
  CCTK_REAL rho_energy_val, rho_baryon_val, P_val, M_val, expnu_val, exp4phi_val;
  TOVola_TOV_interpolate_1D(r_iso, TOVola_Max_Interpolation_Stencil, TOVola_ID_persist->numpoints_arr,
                            TOVola_ID_persist->r_Schw_arr, TOVola_ID_persist->rho_energy_arr, TOVola_ID_persist->rho_baryon_arr, TOVola_ID_persist->P_arr, TOVola_ID_persist->M_arr,
                            TOVola_ID_persist->expnu_arr, TOVola_ID_persist->exp4phi_arr, TOVola_ID_persist->r_iso_arr, &rho_energy_val, &rho_baryon_val, &P_val, &M_val,
                            &expnu_val, &exp4phi_val);

  // Assign interpolated values to initial_data_struct
  initial_data->alpha = sqrt(expnu_val);

  // Assuming beta and B fields are zero in this context
  initial_data->betaSphorCartU0 = 0.0;
  initial_data->betaSphorCartU1 = 0.0;
  initial_data->betaSphorCartU2 = 0.0;
  initial_data->BSphorCartU0 = 0.0;
  initial_data->BSphorCartU1 = 0.0;
  initial_data->BSphorCartU2 = 0.0;

  // Metric components (assuming diagonal for simplicity)
  initial_data->gammaSphorCartDD00 = exp4phi_val;
  initial_data->gammaSphorCartDD01 = 0.0;
  initial_data->gammaSphorCartDD02 = 0.0;
  initial_data->gammaSphorCartDD11 = exp4phi_val * r_iso * r_iso;
  initial_data->gammaSphorCartDD12 = 0.0;
  initial_data->gammaSphorCartDD22 = exp4phi_val * r_iso * r_iso * sin(theta) * sin(theta);

  // Extrinsic curvature components set to zero
  initial_data->KSphorCartDD00 = 0.0;
  initial_data->KSphorCartDD01 = 0.0;
  initial_data->KSphorCartDD02 = 0.0;
  initial_data->KSphorCartDD11 = 0.0;
  initial_data->KSphorCartDD12 = 0.0;
  initial_data->KSphorCartDD22 = 0.0;

  initial_data->T4SphorCartUU00 = rho_energy_val / expnu_val;
  initial_data->T4SphorCartUU01 = 0.0;
  initial_data->T4SphorCartUU02 = 0.0;
  initial_data->T4SphorCartUU03 = 0.0;
  initial_data->T4SphorCartUU11 = P_val / exp4phi_val;
  initial_data->T4SphorCartUU12 = 0.0;
  initial_data->T4SphorCartUU13 = 0.0;
  initial_data->T4SphorCartUU22 = P_val / (exp4phi_val * r_iso * r_iso);
  initial_data->T4SphorCartUU23 = 0.0;
  initial_data->T4SphorCartUU33 = P_val / (exp4phi_val * r_iso * r_iso * sin(theta) * sin(theta));
  */
}