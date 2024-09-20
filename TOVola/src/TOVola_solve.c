#include "GRHayLib.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_odeiv2.h>

#include <cctk.h>
#include <cctk_Arguments.h>
#include <cctk_Parameters.h>

#define GSL_SUCCESS 1
#define ODE_SOLVER_DIM 4
#define TOVOLA_PRESSURE 0
#define TOVOLA_NU 1
#define TOVOLA_MASS 2
#define TOVOLA_R_ISO 3

/* Structure to hold raw TOV data */
typedef struct {
  // Current state variables
  CCTK_REAL rho_baryon;
  CCTK_REAL rho_energy;
  CCTK_REAL r_lengthscale;

  CCTK_REAL *restrict rSchw_arr;
  CCTK_REAL *restrict rho_energy_arr;
  CCTK_REAL *restrict rho_baryon_arr;
  CCTK_REAL *restrict P_arr;
  CCTK_REAL *restrict M_arr;
  CCTK_REAL *restrict nu_arr;
  CCTK_REAL *restrict Iso_r_arr;
  int numels_alloced_TOV_arr;

  int numpoints_actually_saved;
} TOVola_data_struct;

/* Structure to hold TOV data that will become the official ID after normalization */
typedef struct {
	CCTK_REAL *restrict r_Schw_arr;
	CCTK_REAL *restrict rho_energy_arr;
	CCTK_REAL *restrict rho_baryon_arr;
	CCTK_REAL *restrict P_arr;
	CCTK_REAL *restrict M_arr;
	CCTK_REAL *restrict expnu_arr;
	CCTK_REAL *restrict r_iso_arr;
	CCTK_REAL *restrict exp4phi_arr;
	int numpoints_arr;
} TOVola_ID_persist_struct;

/*create a global ID_persist to use in the whole code (Needed for interp function)*/
TOVola_ID_persist_struct *TOVola_ID_persist;

/* Exception handler to prevent negative pressures */
void TOVola_exception_handler(CCTK_REAL r, CCTK_REAL y[]) {
  // Ensure pressure does not become negative due to numerical errors
  if (y[TOVOLA_PRESSURE] < 0) {
    y[TOVOLA_PRESSURE] = 0;
  }
}

/* Termination condition for the integration */
int TOVola_do_we_terminate(CCTK_REAL r, CCTK_REAL y[], TOVola_data_struct *TOVdata) {
  
  DECLARE_CCTK_PARAMETERS
  /* if (TOVdata->eos_type == TABULATED_EOS) { */
  /*     // Not implemented in this standalone version */
  /*     // Return 0 to continue integration */
  /*     return 0; */
  /* } */
  /* else { */
  if (y[TOVOLA_PRESSURE] <= 0.0) { // For Simple and Piecewise Polytrope
    return 1;
  }
  /* } */

  return 0; // Continue integration
}

/* Evaluate rho_baryon and rho_energy based on the EOS type */
void TOVola_evaluate_rho_and_eps(CCTK_REAL r, const CCTK_REAL y[], TOVola_data_struct *TOVdata) {
  
  DECLARE_CCTK_PARAMETERS	
  // Simple Polytrope
  /* if (TOVdata->eos_type == SIMPLE_POLYTROPE) { */
  CCTK_REAL aK, aGamma;
  CCTK_REAL aRho_baryon = TOVdata->rho_baryon;
  CCTK_REAL eps, aPress;

  // Retrieve K and Gamma from GRHayL
  ghl_hybrid_get_K_and_Gamma(ghl_eos, aRho_baryon, &aK, &aGamma);
  TOVdata->rho_baryon = pow(y[TOVOLA_PRESSURE] / aK, 1.0 / aGamma);
  aRho_baryon = TOVdata->rho_baryon;
  ghl_hybrid_compute_P_cold_and_eps_cold(ghl_eos, aRho_baryon, &aPress, &eps);
  TOVdata->rho_energy = TOVdata->rho_baryon * (1.0 + eps);
  /* } */
  /*
  // Piecewise Polytrope (Not implemented in this standalone version)
  else if (TOVdata->eos_type == PIECEWISE_POLYTROPE) {
  fprintf(stderr, "PIECEWISE_POLYTROPE EOS not implemented.\n");
  exit(EXIT_FAILURE);
  }
  */
  /*
  // Tabulated EOS (Not implemented in this standalone version)
  else if (TOVdata->eos_type == TABULATED_EOS) {
  // Not implemented
  fprintf(stderr, "TABULATED_EOS not implemented.\n");
  exit(EXIT_FAILURE);
  }
  */
}

/* The main ODE function for GSL */
int TOVola_ODE(CCTK_REAL r_Schw, const CCTK_REAL y[], CCTK_REAL dydr_Schw[], void *params) {
  // Cast params to TOVdata_struct
  TOVola_data_struct *TOVdata = (TOVola_data_struct *)params;

  // Evaluate rho_baryon and rho_energy based on current state
  TOVola_evaluate_rho_and_eps(r_Schw, y, TOVdata);

  // Dereference the struct to use rho_energy
  CCTK_REAL rho_energy = TOVdata->rho_energy;

  //if (isnan(rho_energy)) {
    // Outside the star gives NaNs from the pow function, but we know they
    // should be zeros.
  //  rho_energy = 0.0;
  //}

  // At the center of the star (r_Schw == 0), the TOV equations diverge, so we set reasonable values here.
  if (r_Schw == 0) {
    dydr_Schw[TOVOLA_PRESSURE] = 0.0; // dP/dr
    dydr_Schw[TOVOLA_NU] = 0.0;       // dnu/dr
    dydr_Schw[TOVOLA_MASS] = 0.0;     // dM/dr
    dydr_Schw[TOVOLA_R_ISO] = 1.0;    // dr_iso/dr
    return GSL_SUCCESS;
  }

// TOV Equations

	  
   dydr_Schw[TOVOLA_PRESSURE] = -((rho_energy + y[TOVOLA_PRESSURE]) * ((2.0 * y[TOVOLA_MASS]) / (r_Schw) + 8.0 * M_PI * r_Schw * r_Schw * y[TOVOLA_PRESSURE])) / (r_Schw * 2.0 * (1.0 - (2.0 * y[TOVOLA_MASS]) / (r_Schw)));
   dydr_Schw[TOVOLA_NU] = ((2.0 * y[TOVOLA_MASS]) / (r_Schw) + 8.0 * M_PI * r_Schw * r_Schw * y[TOVOLA_PRESSURE]) / (r_Schw * (1.0 - (2.0 * y[TOVOLA_MASS]) / (r_Schw)));
   dydr_Schw[TOVOLA_MASS] = 4.0 * M_PI * r_Schw * r_Schw * rho_energy;
   //r_iso == isotropic radius, sometimes called rbar.
   dydr_Schw[TOVOLA_R_ISO] = (y[TOVOLA_R_ISO]) / (r_Schw * sqrt(1.0 - (2.0 * y[TOVOLA_MASS]) / r_Schw));
	 
  //Adjust Length Scale. 
  if (y[TOVOLA_R_ISO] > 0 && fabs(dydr_Schw[TOVOLA_R_ISO]) > 0) {
    TOVdata->r_lengthscale = fabs(y[TOVOLA_R_ISO] / dydr_Schw[TOVOLA_R_ISO]);
  }

  return GSL_SUCCESS;
}

/* Placeholder Jacobian function required by GSL */
int TOVola_jacobian_placeholder(CCTK_REAL t, const CCTK_REAL y[], CCTK_REAL *restrict dfdy, CCTK_REAL dfdt[], void *params) {
  // Jacobian is not necessary for the TOV solution, but GSL requires some
  // function Leave it empty as it does not affect the final results
  return GSL_SUCCESS;
}

/* Initialize the ODE variables */
void TOVola_get_initial_condition(CCTK_REAL y[], TOVola_data_struct *TOVdata) {
  
  DECLARE_CCTK_PARAMETERS	
  // Simple Polytrope
  /* if (TOVdata->eos_type == SIMPLE_POLYTROPE) { */
  CCTK_REAL aK, aGamma;
  CCTK_REAL rhoC_baryon = TOVola_central_baryon_density;

  // Retrieve K and Gamma from GRHayL
  ghl_hybrid_get_K_and_Gamma(ghl_eos, rhoC_baryon, &aK, &aGamma);
  y[TOVOLA_PRESSURE] = aK * pow(rhoC_baryon, aGamma); // Pressure
  y[TOVOLA_NU] = 0.0;                                 // nu
  y[TOVOLA_MASS] = 0.0;                               // Mass
  y[TOVOLA_R_ISO] = 0.0;                              // r_iso

  // Assign initial conditions
  TOVdata->rho_baryon = rhoC_baryon;
  TOVdata->rho_energy = pow(y[TOVOLA_PRESSURE] / aK, 1.0 / aGamma) + y[TOVOLA_PRESSURE] / (aGamma - 1.0);
  // Pinitial is no longer needed as it's part of y[TOVOLA_PRESSURE]
  /* } */
  /*
  // Piecewise Polytrope (Not implemented in this standalone version)
  else if (TOVdata->eos_type == PIECEWISE_POLYTROPE) {
  // Implement similarly to Simple Polytrope if needed
  fprintf(stderr, "PIECEWISE_POLYTROPE EOS not implemented.\n");
  exit(EXIT_FAILURE);
  }
  */
  /*
  // Tabulated EOS (Not implemented in this standalone version)
  else if (TOVdata->eos_type == TABULATED_EOS) {
  // Not implemented
  fprintf(stderr, "TABULATED_EOS not implemented.\n");
  exit(EXIT_FAILURE);
  }
  */

  CCTK_VINFO("Initial Conditions Set: P = %.6e, nu = %.6e, M = %.6e, r_iso = %.6e\n", y[TOVOLA_PRESSURE], y[TOVOLA_NU], y[TOVOLA_MASS], y[TOVOLA_R_ISO]);
}

/* Assign constants after each integration step */
void TOVola_assign_constants(CCTK_REAL c[], TOVola_data_struct *TOVdata) {
  // Assign the densities
  c[0] = TOVdata->rho_energy; // Total energy density
  c[1] = TOVdata->rho_baryon; // Baryon density

  // Handle NaN cases
  //if (isnan(TOVdata->rho_energy)) {
  //  c[0] = 0.0;
  //}
}

/* Function to set up the GSL ODE system and driver */
static int setup_ode_system(const char *ode_method, gsl_odeiv2_system *system, gsl_odeiv2_driver **driver, TOVola_data_struct *TOVdata) {
  
  DECLARE_CCTK_PARAMETERS

  system->function = TOVola_ODE;
  system->jacobian = TOVola_jacobian_placeholder;
  system->dimension = 4; // Hardcoded as per requirements
  system->params = TOVdata;

  if (CCTK_EQUALS(TOVola_ODE_method, "ARKF")) {
    *driver = gsl_odeiv2_driver_alloc_y_new(system, gsl_odeiv2_step_rkf45, TOVola_initial_ode_step_size, TOVola_error_limit,
                                            TOVola_error_limit);
  } else if (CCTK_EQUALS(TOVola_ODE_method, "ADP8")) {
    *driver = gsl_odeiv2_driver_alloc_y_new(system, gsl_odeiv2_step_rk8pd, TOVola_initial_ode_step_size, TOVola_error_limit,
                                            TOVola_error_limit);
  } else {
    CCTK_ERROR("Invalid ODE method. Use 'ARKF' or 'ADP8'.\n");
    return -1;
  }

  if (*driver == NULL) {
    CCTK_ERROR("Failed to allocate GSL ODE driver.\n");
    return -1;
  }

  /* Set minimum and maximum step sizes */
  gsl_odeiv2_driver_set_hmin(*driver, TOVola_absolute_min_step);
  gsl_odeiv2_driver_set_hmax(*driver, TOVola_absolute_max_step);

  return 0;
}

/* Initialize TOVola_data_struct structure with initial allocation */
static int initialize_tovola_data(TOVola_data_struct *TOVdata) {
  TOVdata->rSchw_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);
  TOVdata->rho_energy_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);
  TOVdata->rho_baryon_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);
  TOVdata->P_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);
  TOVdata->M_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);
  TOVdata->nu_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);
  TOVdata->Iso_r_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numels_alloced_TOV_arr);

  if (!TOVdata->rSchw_arr || !TOVdata->rho_energy_arr || !TOVdata->rho_baryon_arr || !TOVdata->P_arr || !TOVdata->M_arr || !TOVdata->nu_arr ||
      !TOVdata->Iso_r_arr) {
    CCTK_ERROR("Memory allocation failed for TOVola_data_struct.\n");
    return -1;
  }
  return 0;
}

/* Free TOVola_data_struct structure */
static void free_tovola_data(TOVola_data_struct *TOVdata) {
  free(TOVdata->rSchw_arr);
  free(TOVdata->rho_energy_arr);
  free(TOVdata->rho_baryon_arr);
  free(TOVdata->P_arr);
  free(TOVdata->M_arr);
  free(TOVdata->nu_arr);
  free(TOVdata->Iso_r_arr);
  TOVdata->numels_alloced_TOV_arr = 0;
}

/* Normalize and set data */
void TOVola_Normalize_and_set_data_integrated(TOVola_data_struct *TOVdata, CCTK_REAL *restrict r_Schw, CCTK_REAL *restrict rho_energy,
                                              CCTK_REAL *restrict rho_baryon, CCTK_REAL *restrict P, CCTK_REAL *restrict M, CCTK_REAL *restrict expnu,
                                              CCTK_REAL *restrict exp4phi, CCTK_REAL *restrict r_iso) {
  
  DECLARE_CCTK_PARAMETERS	
	
  CCTK_INFO("TOVola Normalizing raw TOV data...\n");

  /* Check if there are enough points to normalize */
  if (TOVdata->numpoints_actually_saved < 2) {
    CCTK_ERROR("Not enough data points to normalize.\n");
  }

  /* Copy raw data to normalized arrays */
  for (int i = 0; i < TOVdata->numpoints_actually_saved; i++) {
    r_Schw[i] = TOVdata->rSchw_arr[i];
    rho_energy[i] = TOVdata->rho_energy_arr[i];
    rho_baryon[i] = TOVdata->rho_baryon_arr[i];
    P[i] = TOVdata->P_arr[i];
    M[i] = TOVdata->M_arr[i];
    expnu[i] = TOVdata->nu_arr[i];
    r_iso[i] = TOVdata->Iso_r_arr[i];
  }

  /* Surface values for normalization */
  const CCTK_REAL R_Schw_surface = r_Schw[TOVdata->numpoints_actually_saved - 1];
  const CCTK_REAL M_surface = M[TOVdata->numpoints_actually_saved - 1];
  const CCTK_REAL r_iso_surface = r_iso[TOVdata->numpoints_actually_saved - 1];
  const CCTK_REAL nu_surface = expnu[TOVdata->numpoints_actually_saved - 1];

  const CCTK_REAL normalize = 0.5 * (sqrt(R_Schw_surface * (R_Schw_surface - 2.0 * M_surface)) + R_Schw_surface - M_surface) / r_iso_surface;

  /* Normalize r_iso and calculate expnu and exp4phi */
  for (int i = 0; i < TOVdata->numpoints_actually_saved; i++) {
    r_iso[i] *= normalize;
    expnu[i] = exp(expnu[i] - nu_surface + log(1.0 - 2.0 * M_surface / R_Schw_surface));
    exp4phi[i] = (r_Schw[i] / r_iso[i]) * (r_Schw[i] / r_iso[i]);
  }
  CCTK_INFO("Normalization of raw data complete!\n");
}










//Perform the TOV integration using GSL
void TOVola_Solve(CCTK_ARGUMENTS){

  DECLARE_CCTK_PARAMETERS
  
  CCTK_INFO("Starting TOV Integration using GSL for TOVola...\n");

  CCTK_REAL current_position = 0;

  /* Set up ODE system and driver */
  TOVola_data_struct TOVdata_tmp; // allocates memory for the pointer below.
  TOVola_data_struct *restrict TOVdata = &TOVdata_tmp;
  gsl_odeiv2_system system;
  gsl_odeiv2_driver *driver;
  TOVdata->numpoints_actually_saved = 0;
  if (setup_ode_system(TOVola_ODE_method, &system, &driver, TOVdata) != 0) {
    CCTK_ERROR("Failed to set up ODE system.\n");
  }

  /* Initialize ODE variables */
  CCTK_REAL y[ODE_SOLVER_DIM];
  CCTK_REAL c[2];
  TOVola_get_initial_condition(y, TOVdata);
  TOVola_assign_constants(c, TOVdata);

  /* Initial memory allocation */
  TOVdata->numels_alloced_TOV_arr = 1024;
  if (initialize_tovola_data(TOVdata) != 0) {
    gsl_odeiv2_driver_free(driver);
    CCTK_ERROR("Failed to initialize TOVola_data_struct.\n");
  }

  /* Integration loop */
  TOVdata->r_lengthscale = TOVola_initial_ode_step_size; // initialize dr to a crazy small value in double precision.
  for (int i = 0; i < TOVola_size; i++) {
    CCTK_REAL dr = 0.01 * TOVdata->r_lengthscale;
    if (TOVdata->rho_baryon < 0.05 * TOVola_central_baryon_density) {
      // To get a super-accurate mass, reduce the dr sampling near the surface of the star.
      dr = 1e-6 * TOVdata->r_lengthscale;
    }
    /* Exception handling */
    TOVola_exception_handler(current_position, y);

    /* Apply ODE step */
    int status = gsl_odeiv2_driver_apply(driver, &current_position, current_position + dr, y);
    if (status != GSL_SUCCESS) {
      CCTK_VINFO("GSL ODE solver failed with status %d.\n", status);
      gsl_odeiv2_driver_free(driver);
      CCTK_ERROR("Shutting down due to error\n");
    };

    /* Post-step exception handling */
    TOVola_exception_handler(current_position, y);

    /* Evaluate densities */
    TOVola_evaluate_rho_and_eps(current_position, y, TOVdata);
    TOVola_assign_constants(c, TOVdata);

    /* Check if reallocation is needed */
    if (TOVdata->numpoints_actually_saved >= TOVdata->numels_alloced_TOV_arr) {
      // Update arr_size instead of modifying the macro
      const int new_arr_size = 1.5 * TOVdata->numels_alloced_TOV_arr;
      TOVdata->numels_alloced_TOV_arr = new_arr_size;
      TOVdata->rSchw_arr = realloc(TOVdata->rSchw_arr, sizeof(CCTK_REAL) * new_arr_size);
      TOVdata->rho_energy_arr = realloc(TOVdata->rho_energy_arr, sizeof(CCTK_REAL) * new_arr_size);
      TOVdata->rho_baryon_arr = realloc(TOVdata->rho_baryon_arr, sizeof(CCTK_REAL) * new_arr_size);
      TOVdata->P_arr = realloc(TOVdata->P_arr, sizeof(CCTK_REAL) * new_arr_size);
      TOVdata->M_arr = realloc(TOVdata->M_arr, sizeof(CCTK_REAL) * new_arr_size);
      TOVdata->nu_arr = realloc(TOVdata->nu_arr, sizeof(CCTK_REAL) * new_arr_size);
      TOVdata->Iso_r_arr = realloc(TOVdata->Iso_r_arr, sizeof(CCTK_REAL) * new_arr_size);

      if (!TOVdata->rSchw_arr || !TOVdata->rho_energy_arr || !TOVdata->rho_baryon_arr || !TOVdata->P_arr || !TOVdata->M_arr || !TOVdata->nu_arr ||
          !TOVdata->Iso_r_arr) {
        CCTK_ERROR("Memory reallocation failed during integration.\n");
        gsl_odeiv2_driver_free(driver);
      }
    }

    /* Store data */
    TOVdata->rSchw_arr[TOVdata->numpoints_actually_saved] = current_position;
    TOVdata->rho_energy_arr[TOVdata->numpoints_actually_saved] = c[0];
    TOVdata->rho_baryon_arr[TOVdata->numpoints_actually_saved] = c[1];
    TOVdata->P_arr[TOVdata->numpoints_actually_saved] = y[TOVOLA_PRESSURE];
    TOVdata->M_arr[TOVdata->numpoints_actually_saved] = y[TOVOLA_MASS];
    TOVdata->nu_arr[TOVdata->numpoints_actually_saved] = y[TOVOLA_NU];
    TOVdata->Iso_r_arr[TOVdata->numpoints_actually_saved] = y[TOVOLA_R_ISO];
    TOVdata->numpoints_actually_saved++;

    // r_SchwArr_np,rhoArr_np,rho_baryonArr_np,PArr_np,mArr_np,exp2phiArr_np,confFactor_exp4phi_np,r_isoArr_np),
    // printf("%.15e %.15e %.15e %.15e %.15e %.15e %.15e soln\n", current_position, dr, c[0], c[1], y[TOVOLA_PRESSURE], y[TOVOLA_MASS], y[TOVOLA_NU]);

    /* Termination condition */
    if (TOVola_do_we_terminate(current_position, y, TOVdata)) {
      CCTK_VINFO("Finished Integration at position %.6e with Mass %.14e\n", current_position, y[TOVOLA_MASS]);
      break;
    }
  }

  /* Cleanup */
  gsl_odeiv2_driver_free(driver);
  CCTK_INFO("ODE Solver using GSL for TOVola Shutting Down...\n");

  /* Allocate and populate TOVola_ID_persist_struct arrays */
  TOVola_ID_persist->r_Schw_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->rho_energy_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->rho_baryon_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->P_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->M_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->expnu_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->exp4phi_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);
  TOVola_ID_persist->r_iso_arr = (CCTK_REAL *restrict)malloc(sizeof(CCTK_REAL) * TOVdata->numpoints_actually_saved);

  if (!TOVola_ID_persist->r_Schw_arr || !TOVola_ID_persist->rho_energy_arr || !TOVola_ID_persist->rho_baryon_arr || !TOVola_ID_persist->P_arr || !TOVola_ID_persist->M_arr ||
      !TOVola_ID_persist->expnu_arr || !TOVola_ID_persist->exp4phi_arr || !TOVola_ID_persist->r_iso_arr) {
    free_tovola_data(TOVdata);
    CCTK_ERROR("Memory allocation failed for TOVola_ID_persist_struct arrays.\n");
  }

  TOVola_ID_persist->numpoints_arr = TOVdata->numpoints_actually_saved;

  /* Normalize and set data */
  TOVola_Normalize_and_set_data_integrated(TOVdata, TOVola_ID_persist->r_Schw_arr, TOVola_ID_persist->rho_energy_arr, TOVola_ID_persist->rho_baryon_arr, TOVola_ID_persist->P_arr,
                                           TOVola_ID_persist->M_arr, TOVola_ID_persist->expnu_arr, TOVola_ID_persist->exp4phi_arr, TOVola_ID_persist->r_iso_arr);

  /* Free raw data as it's no longer needed */
  free_tovola_data(TOVdata);
}
