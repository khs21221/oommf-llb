/** FILE: yy_llbeulerevolve.cc                 -*-Mode: c++-*-
 *
 * Euler evolver class for Landau-Lifshitz-Bloch equation including thermal 
 * fluctuations. It is based on thetaevolve.cc and .h written by Oliver 
 * Lemcke released under GPLv2 license.
 *
 * Copyright (C) 2015 Yu Yahagi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <math.h>

#include "nb.h"
#include "director.h"
#include "timedriver.h"
#include "simstate.h"
#include "key.h"
#include "energy.h"    // Needed to make MSVC++ 5 happy
#include "meshvalue.h"
#include "rectangularmesh.h"
#include "scalarfield.h"

#include "yy_llbeulerevolve.h"

// Oxs_Ext registration support
OXS_EXT_REGISTER(YY_LLBEulerEvolve);

/* End includes */

void YY_LLBEulerEvolve::SetTemperature(const Oxs_Mesh* mesh_, OC_REAL8m newtemp)
{
  temperature = fabs(newtemp);   // no temperatures allowed below 0K
  kB_T = KBoltzmann * temperature;
  FillHFluctConst(mesh_);
}

OC_REAL8m YY_LLBEulerEvolve::GetStageTemp
(OC_UINT4m stage) const
{
  if(!has_tempscript) return temperature;

  int index;
  if((index = tempscript_opts[0].position)>=0) { // stage
    tempscript_cmd.SetCommandArg(index,stage);
  }

  tempscript_cmd.SaveInterpResult();
  tempscript_cmd.Eval();
  if(tempscript_cmd.GetResultListSize()!=1) {
    String msg
      = String("Return script value is not a single scalar: ")
      + tempscript_cmd.GetWholeResult();
    tempscript_cmd.RestoreInterpResult();
    throw Oxs_ExtError(this,msg.c_str());
  }
  OC_REAL8m result;
  tempscript_cmd.GetResultListItem(0,result);
  tempscript_cmd.RestoreInterpResult();

  return result;
}

// Constructor
YY_LLBEulerEvolve::YY_LLBEulerEvolve(
    const char* name,     // Child instance id
    Oxs_Director* newdtr, // App director
    const char* argstr)   // MIF input block parameters
    : Oxs_TimeEvolver(name,newdtr,argstr),
    mesh_id(0), min_timestep(0.), max_timestep(1e-10),
    energy_accum_count_limit(25),
    energy_state_id(0),next_timestep(0.),
    KBoltzmann(1.38062e-23),
    iteration_Tcalculated(0),
    has_tempscript(0),
    isMs0Set(0)
{
  // Process arguments
  // For now, it works with a fixed time step but there still are min_ and
  // max_timestep for future implementation of adaptive stepsize.
  fixed_timestep = GetRealInitValue("fixed_timestep",1e-16);
  min_timestep = max_timestep = fixed_timestep;
  if(max_timestep<=0.0) {
    char buf[4096];
    Oc_Snprintf(buf,sizeof(buf),
    "Invalid parameter value:"
    " Specified max time step is %g (should be >0.)",
    max_timestep);
    throw Oxs_Ext::Error(this,buf);
  }

  allowed_error_rate = GetRealInitValue("error_rate",-1);
  if(allowed_error_rate>0.0) {
    allowed_error_rate *= PI*1e9/180.; // Convert from deg/ns to rad/s
  }
  allowed_absolute_step_error
    = GetRealInitValue("absolute_step_error",0.2);
  if(allowed_absolute_step_error>0.0) {
    allowed_absolute_step_error *= PI/180.; // Convert from deg to rad
  }
  allowed_relative_step_error
    = GetRealInitValue("relative_step_error",0.2);

  step_headroom = GetRealInitValue("step_headroom",0.85);
  if(step_headroom<=0.) {
    throw Oxs_Ext::Error(this,"Invalid initialization detected:"
       " step_headroom value must be bigger than 0.");
  }

  if(HasInitValue("alpha_t")) {
    OXS_GET_INIT_EXT_OBJECT("alpha_t",Oxs_ScalarField,alpha_t_init);
  } else {
    alpha_t_init.SetAsOwner(dynamic_cast<Oxs_ScalarField *>
                          (MakeNew("Oxs_UniformScalarField",director,
                                   "value 0.5")));
  }

  if(HasInitValue("Tc")) {
    OXS_GET_INIT_EXT_OBJECT("Tc",Oxs_ScalarField,Tc_init);
  } else {
    Tc_init.SetAsOwner(dynamic_cast<Oxs_ScalarField *>
                          (MakeNew("Oxs_UniformScalarField",director,
                                   "value 400")));
  }

  // User may specify either gamma_G (Gilbert) or
  // gamma_LL (Landau-Lifshitz).  Code uses "gamma"
  // which is LL form.
  gamma_style = GS_INVALID;
  if(HasInitValue("gamma_G") && HasInitValue("gamma_LL")) {
    throw Oxs_Ext::Error(this,"Invalid Specify block; "
       "both gamma_G and gamma_LL specified.");
  } else if(HasInitValue("gamma_G")) {
    OXS_GET_INIT_EXT_OBJECT("gamma_G",Oxs_ScalarField,gamma_init);
    gamma_style = GS_G;
  } else if(HasInitValue("gamma_LL")) {
    OXS_GET_INIT_EXT_OBJECT("gamma_LL",Oxs_ScalarField,gamma_init);
    gamma_style = GS_LL;
  } else {
    gamma_init.SetAsOwner(dynamic_cast<Oxs_ScalarField *>
                          (MakeNew("Oxs_UniformScalarField",director,
                                   "value 2.211e5")));
  }

  do_precess = GetIntInitValue("do_precess",1);

  start_dm = GetRealInitValue("start_dm",0.01);
  start_dm *= PI/180.; // Convert from deg to rad

  // here the new parameters are set up
  // The new parameters for thermal are set up here
  if(HasInitValue("temperature")) {
    // Get temperature of simulation
    temperature = GetRealInitValue("temperature", 0.);
  }

  // Get time dependent multiplier to scale temperature
  if(HasInitValue("tempscript")) {
    has_tempscript=1;
    String cmdoptreq = GetStringInitValue("tempscript_args",
                                          "stage stage_time total_time");
    tempscript_opts.push_back(Nb_TclCommandLineOption("stage",1));
    tempscript_opts.push_back(Nb_TclCommandLineOption("stage_time",1));
    tempscript_opts.push_back(Nb_TclCommandLineOption("total_time",1));
    tempscript_cmd.SetBaseCommand(InstanceName(),
				  director->GetMifInterp(),
				  GetStringInitValue("tempscript"),
				  Nb_ParseTclCommandLineRequest(InstanceName(),
								 tempscript_opts,
								 cmdoptreq));
  }

  // set temperature to zero to get an estimate for a reasonable stepsize
  // or use it for comparison (acts like eulerevolve with temperature=0K)
  if(temperature == 0.){
    min_timestep = 0.;    
    max_timestep = 1e-10; 
  }

  if(HasInitValue("uniform_seed")) {
    uniform_seed = GetIntInitValue("uniform_seed");
    has_uniform_seed = 1;
  } else {
    has_uniform_seed = 0;
  }

  // in Ito calculus no drift term appears, default=false
  ito_calculus = GetIntInitValue("ito_calculus",0);

  gaus2_isset = 0;    //no gaussian random numbers calculated yet

  // Setup outputs
  max_dm_dt_output.Setup(this,InstanceName(),"Max dm/dt","deg/ns",0,
     &YY_LLBEulerEvolve::UpdateDerivedOutputs);
  dE_dt_output.Setup(this,InstanceName(),"dE/dt","J/s",0,
     &YY_LLBEulerEvolve::UpdateDerivedOutputs);
  delta_E_output.Setup(this,InstanceName(),"Delta E","J",0,
     &YY_LLBEulerEvolve::UpdateDerivedOutputs);
  dm_dt_t_output.Setup(this,InstanceName(),"Trans. dm/dt","rad/s",1,
     &YY_LLBEulerEvolve::UpdateDerivedOutputs);
  dm_dt_l_output.Setup(this,InstanceName(),"Long. dm/dt","rad/s",1,
     &YY_LLBEulerEvolve::UpdateDerivedOutputs);
  mxH_output.Setup(this,InstanceName(),"mxH","A/m",1,
     &YY_LLBEulerEvolve::UpdateDerivedOutputs);

  VerifyAllInitArgsUsed();
}   // end Constructor

OC_BOOL YY_LLBEulerEvolve::Init()
{
  // Register outputs
  max_dm_dt_output.Register(director,-5);
  dE_dt_output.Register(director,-5);
  delta_E_output.Register(director,-5);
  dm_dt_t_output.Register(director,-5);
  dm_dt_l_output.Register(director,-5);
  mxH_output.Register(director,-5);

  // dm_dt and mxH output caches are used for intermediate storage,
  // so enable caching.
  dm_dt_t_output.CacheRequestIncrement(1);
  dm_dt_l_output.CacheRequestIncrement(1);
  mxH_output.CacheRequestIncrement(1);

  alpha_t0.Release(); alpha_t.Release(); alpha_l.Release();
  gamma.Release();
  Tc.Release();
  Ms0.Release();
  energy.Release();
  total_field.Release();
  new_energy.Release();
  new_dm_dt_t.Release();
  new_dm_dt_l.Release();

  hFluct_t.Release(); hFluct_l.Release();
  hFluctVarConst_t.Release(); hFluctVarConst_l.Release();
  inducedDriftConst_t.Release(); inducedDriftConst_l.Release();


  energy_state_id=0;   // Mark as invalid state
  next_timestep=0.;    // Dummy value
  energy_accum_count=energy_accum_count_limit; // Force cold count
  // on first pass

  isMs0Set = 0;

  // (Re)initialize random number generator
  if(has_uniform_seed) {
    Oc_Srand(uniform_seed); //initialize Random number generator
  } else {
    // Default seed value is time dependent
    Oc_Srand();
  }

  return Oxs_TimeEvolver::Init();  // Initialize parent class.
  // Do this after child output registration so that
  // UpdateDerivedOutputs gets called before the parent
  // total_energy_output update function.
}

YY_LLBEulerEvolve::~YY_LLBEulerEvolve()
{}

void YY_LLBEulerEvolve::Calculate_dm_dt(
    const Oxs_SimState& state_,
    const Oxs_MeshValue<ThreeVector>& mxH_,
    const Oxs_MeshValue<ThreeVector>& total_field_,
    OC_REAL8m pE_pt_,
    Oxs_MeshValue<ThreeVector>& dm_dt_t_,
    Oxs_MeshValue<ThreeVector>& dm_dt_l_,
    OC_REAL8m& max_dm_dt_,
    OC_REAL8m& dE_dt_,
    OC_REAL8m& min_timestep_)
{
  // Imports: state_, mxH_, pE_pt
  // Exports: dm_dt_t_, dm_dt_l_, max_dm_dt_, dE_dt_
  const Oxs_Mesh* mesh_ = state_.mesh;
  const OC_INDEX size = mesh_->Size(); // Assume all imports are compatible
  const Oxs_MeshValue<OC_REAL8m>& Ms_ = *(state_.Ms);
  const Oxs_MeshValue<OC_REAL8m>& Ms_inverse_ = *(state_.Ms_inverse);
  const Oxs_MeshValue<ThreeVector>& spin_ = state_.spin;
  OC_UINT4m iteration_now = state_.iteration_count;
  ThreeVector scratch;
  ThreeVector dm_fluct;
  ThreeVector inducedDrift_t;
  ThreeVector inducedDrift_l;
  OC_REAL8m hFluctSigma_t;
  OC_REAL8m hFluctSigma_l;
  dm_dt_t_.AdjustSize(mesh_);
  dm_dt_l_.AdjustSize(mesh_);
  OC_INDEX i;

  iteration_now++;
  // if not done, hFluct for first step may be calculated too often

  // Fill out alpha and gamma meshvalue arrays, as necessary.
  if(mesh_id != mesh_->Id() || !gamma.CheckMesh(mesh_)
     || !alpha_t.CheckMesh(mesh_)) {
    Ms0.AdjustSize(mesh_);
    if(!isMs0Set) {
      for(i=0; i<size; i++) {
        Ms0[i] = Ms_[i]; // This will be kept for the whole simulation.
      }
      isMs0Set = 1;
    }
    UpdateMeshArrays(mesh_);
    total_field.AdjustSize(mesh_);
    hFluctVarConst_t.AdjustSize(mesh_);
    hFluctVarConst_l.AdjustSize(mesh_);
    inducedDriftConst_t.AdjustSize(mesh_);
    inducedDriftConst_l.AdjustSize(mesh_);
    FillHFluctConst(mesh_);
    InitHFluct(mesh_);
  }


  // TODO: Update temperature and the meshvalues accordingly.


  // if mesh has changed or hFluct_t doesn't exist yet, create a compatible 
  // array. In this case hFluct_t[i] MUST!! be computed, so force it
  if (!hFluct_t.CheckMesh(mesh_)) {
    hFluct_t.AdjustSize(mesh_);     
    hFluct_l.AdjustSize(mesh_);     
    iteration_Tcalculated = 0;     
    }

  // TODO: Calculation of coefficients are done here and at
  // FillHFluctConst(). Depending on when the temperature and Ms are
  // updated, try to put everything in one place. See line ~870.
  if (iteration_now > iteration_Tcalculated) {
    // i.e. if thermal field is not calculated for this step
    for(i=0;i<size;i++){
      if(Ms_[i] != 0){                                      
        // only sqrt(delta_t) is multiplied for stochastic functions
        // opposed to dm_dt * delta_t for deterministic functions
        // sqrt(alpha/(1+alpha^2) * 2*kB_t/(Ms*V*delta_t)) ->
        // this is the standard deviation of the gaussian distribution
        // used to represent the thermal perturbations
        hFluctSigma_t = hFluctVarConst_t[i] * Ms_inverse_[i];
        hFluctSigma_t = sqrt(hFluctSigma_t / fixed_timestep);
        hFluctSigma_l = hFluctVarConst_l[i] * Ms_inverse_[i];
        hFluctSigma_l = sqrt(hFluctSigma_l / fixed_timestep);

        // create the stochastic (fluctuating) field  
        // that represents the thermal influence
        hFluct_t[i].x = hFluctSigma_t*Gaussian_Random(0.0, 1.0);
        hFluct_t[i].y = hFluctSigma_t*Gaussian_Random(0.0, 1.0);
        hFluct_t[i].z = hFluctSigma_t*Gaussian_Random(0.0, 1.0);
        hFluct_l[i].x = hFluctSigma_l*Gaussian_Random(0.0, 1.0);
        hFluct_l[i].y = hFluctSigma_l*Gaussian_Random(0.0, 1.0);
        hFluct_l[i].z = hFluctSigma_l*Gaussian_Random(0.0, 1.0);
      }
    }
  }

  for(i=0;i<size;i++) {
    if(Ms_[i]==0) {
      dm_dt_t_[i].Set(0.0,0.0,0.0);
      dm_dt_l_[i].Set(0.0,0.0,0.0);
    } else {
      OC_REAL8m cell_alpha_t = alpha_t[i];
      OC_REAL8m cell_alpha_l = alpha_l[i];
      OC_REAL8m cell_gamma = gamma[i];

      // deterministic part
      scratch = mxH_[i];
      scratch *= -cell_gamma; // -|gamma|*(mxH)

      // TODO: This may ought be moved after dm_dt_t_[i] = scratch.
      // In Garanin PRB 70, 212409 (2004), they suddenly omit the
      // stochastic field in the first term without much explanation.
      // In the subsequent papers from the group, no stochastic field
      // in the first term is assumed either. At least in the stochastic
      // LLG case, this makes a big difference. Watch out.
      dm_fluct = spin_[i] ^ hFluct_t[i];  // cross product mxhFluct_t
      dm_fluct *= -cell_gamma;
      scratch += dm_fluct;  // -|gamma|*mx(H+hFluct_t)

      if(do_precess) {
        dm_dt_t_[i]  = scratch;
        dm_dt_l_[i].Set(0.0,0.0,0.0);
      } else {
        dm_dt_t_[i].Set(0.0,0.0,0.0);
        dm_dt_l_[i].Set(0.0,0.0,0.0);
      }

      // Transverse damping term
      scratch ^= spin_[i];
      // -|gamma|((mx(H+hFluct_t))xm) = |gamma|(mx(mx(H+hFluct_t)))
      scratch *= -cell_alpha_t; // -|alpha*gamma|(mx(mx(H+hFluct_t)))
      dm_dt_t_[i] += scratch;

      if (!ito_calculus){     // no additional drifting in Ito case
        // additional drift terms due to integration of a stochastic 
        // function
        // -gamma^2 * sigma^2 * (1 + alpha^2) * m
        inducedDrift_t = (inducedDriftConst_t[i]*Ms_inverse_[i])*spin_[i];
        dm_dt_t_[i] += inducedDrift_t;
      }

      // Longitudinal terms
      OC_REAL8m temp;
      temp = spin_[i] * (total_field_[i] + hFluct_l[i]);  // dot product m.H
      temp *= cell_gamma*cell_alpha_l;
      scratch = temp*spin_[i];
      dm_dt_l_[i] += scratch;

      if (!ito_calculus){     // no additional drifting in Ito case
        inducedDrift_l = (inducedDriftConst_l[i]*Ms_inverse_[i])*spin_[i];
        dm_dt_l_[i] += inducedDrift_l;
        // TODO: Longitudinal induced drift term?
      }
    }
  }

  // now hFluct_t is definetely calculated for this iteration
  iteration_Tcalculated = iteration_now;

  // Zero dm_dt at fixed spin sites
  UpdateFixedSpinList(mesh_);
  const OC_INDEX fixed_count = GetFixedSpinCount();
  for(OC_INDEX j=0;j<fixed_count;j++) {
    dm_dt_t_[GetFixedSpin(j)].Set(0.,0.,0.);
  }

  // Collect statistics
  OC_REAL8m max_dm_dt_sq=0.0;
  OC_REAL8m dE_dt_sum=0.0;
  OC_INDEX max_index=0;
  for(i=0;i<size;i++) {
    OC_REAL8m dm_dt_sq = dm_dt_t_[i].MagSq() + dm_dt_l_[i].MagSq();
    if(dm_dt_sq>0.0) {
      dE_dt_sum += -1*MU0*fabs(gamma[i]*alpha_t[i])
        *mxH_[i].MagSq() * Ms_[i] * mesh_->Volume(i);
      if(dm_dt_sq>max_dm_dt_sq) {
        max_dm_dt_sq=dm_dt_sq;
        max_index = i;
      }
    }
  }

  max_dm_dt_ = sqrt(max_dm_dt_sq);
  dE_dt_ = dE_dt_sum; // Transverse terms
  dE_dt_ += pE_pt_;
  // TODO: What about the longitudinal terms?
  /// The first term is (partial E/partial M)*dM/dt, the
  /// second term is (partial E/partial t)*dt/dt.  Note that,
  /// provided Ms_[i]>=0, that by constructions dE_dt_sum above
  /// is always non-negative, so dE_dt_ can only be made positive
  /// by positive pE_pt_.

  if(temperature == 0.) {
    // Get bound on smallest stepsize that would actually
    // change spin new_max_dm_dt_index:
    OC_REAL8m min_ratio = DBL_MAX/2.;
    if(fabs(dm_dt_t_[max_index].x)>=1.0 ||
       min_ratio*fabs(dm_dt_t_[max_index].x) > fabs(spin_[max_index].x)) {
     min_ratio = fabs(spin_[max_index].x/dm_dt_t_[max_index].x);
    }
   if(fabs(dm_dt_t_[max_index].y)>=1.0 ||
      min_ratio*fabs(dm_dt_t_[max_index].y) > fabs(spin_[max_index].y)) {
     min_ratio = fabs(spin_[max_index].y/dm_dt_t_[max_index].y);
   }
   if(fabs(dm_dt_t_[max_index].z)>=1.0 ||
      min_ratio*fabs(dm_dt_t_[max_index].z) > fabs(spin_[max_index].z)) {
      min_ratio = fabs(spin_[max_index].z/dm_dt_t_[max_index].z);
    }
  min_timestep_ = min_ratio * OC_REAL8_EPSILON;
  }
  else {min_timestep_ = fixed_timestep;}

  return;
} // end Calculate_dm_dt


OC_BOOL
YY_LLBEulerEvolve::Step(const Oxs_TimeDriver* driver,
          Oxs_ConstKey<Oxs_SimState> current_state,
          const Oxs_DriverStepInfo& /* step_info */,
          Oxs_Key<Oxs_SimState>& next_state)
{
  const OC_REAL8m max_step_increase = 1.25;
  const OC_REAL8m max_step_decrease = 0.5;

  OC_INDEX size,i; // Mesh size and indexing variable

  const Oxs_SimState& cstate = current_state.GetReadReference();
  Oxs_SimState& workstate = next_state.GetWriteReference();
  driver->FillState(cstate,workstate);

  if(cstate.mesh->Id() != workstate.mesh->Id()) {
    throw Oxs_Ext::Error(this,
        "YY_LLBEulerEvolve::Step: Oxs_Mesh not fixed across steps.");
  }

  if(cstate.Id() != workstate.previous_state_id) {
    throw Oxs_Ext::Error(this,
        "YY_LLBEulerEvolve::Step: State continuity break detected.");
  }

  // Pull cached values out from cstate.
  // If cstate.Id() == energy_state_id, then cstate has been run
  // through either this method or UpdateDerivedOutputs.  Either
  // way, all derived state data should be stored in cstate,
  // except currently the "energy" mesh value array, which is
  // stored independently inside *this.  Eventually that should
  // probably be moved in some fashion into cstate too.
  if(energy_state_id != cstate.Id()) {
    // cached data out-of-date
    UpdateDerivedOutputs(cstate);
  }
  OC_BOOL cache_good = 1;
  OC_REAL8m max_dm_dt;
  OC_REAL8m dE_dt, delta_E, pE_pt;
  OC_REAL8m timestep_lower_bound;  // Smallest timestep that can actually
  /// change spin with max_dm_dt (due to OC_REAL8_EPSILON restrictions).
  /// The next timestep is based on the error from the last step.  If
  /// there is no last step (either because this is the first step,
  /// or because the last state handled by this routine is different
  /// from the incoming current_state), then timestep is calculated
  /// so that max_dm_dt * timestep = start_dm.

  cache_good &= cstate.GetDerivedData("Max dm/dt",max_dm_dt);
  cache_good &= cstate.GetDerivedData("dE/dt",dE_dt);
  cache_good &= cstate.GetDerivedData("Delta E",delta_E);
  cache_good &= cstate.GetDerivedData("pE/pt",pE_pt);
  cache_good &= cstate.GetDerivedData("Timestep lower bound",
              timestep_lower_bound);
  cache_good &= (energy_state_id == cstate.Id());
  cache_good &= (dm_dt_t_output.cache.state_id == cstate.Id());
  cache_good &= (dm_dt_l_output.cache.state_id == cstate.Id());

  if(!cache_good) {
    throw Oxs_Ext::Error(this,
       "YY_LLBEulerEvolve::Step: Invalid data cache.");
  }

  const Oxs_MeshValue<ThreeVector>& dm_dt_t = dm_dt_t_output.cache.value;
  const Oxs_MeshValue<ThreeVector>& dm_dt_l = dm_dt_l_output.cache.value;

  // Negotiate with driver over size of next step
  OC_REAL8m stepsize = next_timestep;

  if(stepsize<=0.0) {
    if(start_dm < sqrt(DBL_MAX/4) * max_dm_dt) {
      stepsize = start_dm / max_dm_dt;
    } else {
      stepsize = sqrt(DBL_MAX/4);
    }
  }
   OC_BOOL forcestep=0;
  // Insure step is not outside requested step bounds
  if(stepsize<min_timestep) {
    // the step has to be forced here,to make sure we don't produce
    // an infinite loop
    stepsize = min_timestep;
    forcestep = 1;
    }
  if(stepsize>max_timestep) stepsize = max_timestep;

  workstate.last_timestep=stepsize;
  if(stepsize<timestep_lower_bound) {
    workstate.last_timestep=timestep_lower_bound;
  }

  if(cstate.stage_number != workstate.stage_number) {
    // New stage
    workstate.stage_start_time = cstate.stage_start_time
                                + cstate.stage_elapsed_time;
    workstate.stage_elapsed_time = workstate.last_timestep;
    SetTemperature(cstate.mesh,GetStageTemp(workstate.stage_number));
  } else {
    workstate.stage_start_time = cstate.stage_start_time;
    workstate.stage_elapsed_time = cstate.stage_elapsed_time
                                  + workstate.last_timestep;
  }
  workstate.iteration_count = cstate.iteration_count + 1;
  workstate.stage_iteration_count = cstate.stage_iteration_count + 1;
  driver->FillStateSupplemental(workstate);

  if(workstate.last_timestep>stepsize) {
    // Either driver wants to force this stepsize (in order to end stage 
    // exactly at boundary), or else suggested stepsize is smaller than
    // timestep_lower_bound.
    forcestep=1;
  }
  stepsize = workstate.last_timestep;

  // Put new spin configuration in next_state
  workstate.spin.AdjustSize(workstate.mesh); // Safety
  size = workstate.spin.Size();
  //const Oxs_MeshValue<OC_REAL8m>& cMs = *(cstate.Ms);
  //const Oxs_MeshValue<OC_REAL8m>& cMs_inverse = *(cstate.Ms_inverse);
  //Oxs_MeshValue<OC_REAL8m>& wMs = *(workstate.Ms);
  //Oxs_MeshValue<OC_REAL8m>& wMs_inverse = *(workstate.Ms_inverse);
  ThreeVector tempspin;
  for(i=0;i<size;++i) {
    // Transverse movement
    tempspin = dm_dt_t[i];
    tempspin *= stepsize;

    // For improved accuracy, adjust step vector so that
    // to first order m0 + adjusted_step = v/|v| where
    // v = m0 + step.  (????)
    // maybe adjusted_mo + adjusted_step is meant here??
    OC_REAL8m adj = 0.5 * tempspin.MagSq();
    tempspin -= adj*cstate.spin[i];
    tempspin *= 1.0/(1.0+adj);
    tempspin += cstate.spin[i];
    tempspin.MakeUnit();
    workstate.spin[i] = tempspin;

    // Longitudinal movement
    tempspin = dm_dt_l[i]*stepsize;
    tempspin += cstate.spin[i];

    // TODO: Update Ms in the next state.
    // Both of wMs and wMs_inverse should be updated at the same time.
    //wMs[i] = sqrt(tempspin.MagSq())*cMs[i];
    //wMs_inverse[i] = sqrt(tempspin.MagSq())*cMs_inverse[i];
  }
  const Oxs_SimState& nstate
    = next_state.GetReadReference();  // Release write lock

  //  Calculate delta E
  OC_REAL8m new_pE_pt;
  // TODO: Add additional Heff terms in Eq (3) in PRB 85, 014433 (2012).
  GetEnergyDensity(nstate,new_energy,
       &mxH_output.cache.value,
       &total_field,
       new_pE_pt);
  mxH_output.cache.state_id=nstate.Id();
  const Oxs_MeshValue<ThreeVector>& mxH = mxH_output.cache.value;

  OC_REAL8m dE=0.0;
  OC_REAL8m var_dE=0.0;
  OC_REAL8m total_E=0.0;
  for(i=0;i<size;++i) {
    OC_REAL8m vol = nstate.mesh->Volume(i);
    OC_REAL8m e = energy[i];
    total_E += e * vol;
    OC_REAL8m new_e = new_energy[i];
    dE += (new_e - e) * vol;
    var_dE += (new_e*new_e + e*e)*vol*vol;
  }
  var_dE *= 256*OC_REAL8_EPSILON*OC_REAL8_EPSILON/3.; // Variance, assuming
  /// error in each energy[i] term is independent, uniformly
  /// distributed, 0-mean, with range +/- 16*OC_REAL8_EPSILON*energy[i].
  /// It would probably be better to get an error estimate directly
  /// from each energy term.

  // Get error estimate.  See step size adjustment discussion in
  // MJD Notes II, p72 (18-Jan-2001).

  OC_REAL8m new_max_dm_dt;
  OC_REAL8m new_dE_dt,new_timestep_lower_bound;
  Calculate_dm_dt(
      nstate, 
      mxH, 
      total_field, 
      new_pE_pt, 
      new_dm_dt_t,
      new_dm_dt_l,
      new_max_dm_dt, 
      new_dE_dt, 
      new_timestep_lower_bound);

  OC_REAL8m max_error=0;
  for(i=0;i<size;++i) { 
    ThreeVector temp = dm_dt_t[i] + dm_dt_l[i];
    temp -= new_dm_dt_t[i];
    temp -= new_dm_dt_l[i];
    OC_REAL8m temp_error = temp.MagSq();
    if(temp_error>max_error) max_error = temp_error;
  }
  max_error = sqrt(max_error)/2.0; // Actual (local) error
  /// estimate is max_error * stepsize

  // Energy check control
#ifdef FOO
  OC_REAL8m expected_dE = 0.5 * (dE_dt+new_dE_dt) * stepsize;
  OC_REAL8m dE_error = dE - expected_dE;
  OC_REAL8m max_allowed_dE = expected_dE + 0.25*fabs(expected_dE);
  max_allowed_dE += OC_REAL8_EPSILON*fabs(total_E);
  max_allowed_dE += 2*sqrt(var_dE);
#else
  OC_REAL8m max_allowed_dE = 0.5 * (pE_pt+new_pE_pt) * stepsize
    + OC_MAX(OC_REAL8_EPSILON*fabs(total_E),2*sqrt(var_dE));
  /// The above says essentially that the spin adjustment can
  /// increase the energy by only as much as pE/pt allows; in
  /// the absence of pE/pt, the energy should decrease.  I
  /// think this may be problematic, if at the start of a stage
  /// the spins are near equilibrium, and the applied field is
  /// ramping up slowly.  In this case there won't be much "give"
  /// in the spin configuration with respect to pE/pm.  But I
  /// haven't seen an example of this yet, so we'll wait and see.
  /// -mjd, 27-July-2001.
#endif

  // Check step and adjust next_timestep.  The relative error
  // check is a bit fudged, because rather than limiting the
  // relative error uniformly across the sample, we limit it
  // only at the position that has the maximum absolute error
  // (i.e., max_error is max *absolute* error).  I haven't
  // tested to see if uniformly limiting relative error is
  // workable (it might be too restrictive for most purposes),
  // but the present setup seems to solve the problem of convergence
  // near equilibrium.  -mjd, 2001-02-23.
  // NOTE: Since all three error controls (error_rate,
  //  absolute_step_error, and relative_step_error) assume error
  //  grows linearly with step size, we can check up front to see
  //  which control is most restrictive, store that constraint in
  //  working_allowed_error, and then adjust the step size without
  //  regard to which control is being exercised.
  OC_REAL8m working_allowed_error
    = max_step_increase*max_error/step_headroom;
  if(allowed_error_rate>=0.
     && working_allowed_error>allowed_error_rate) {
    working_allowed_error=allowed_error_rate;
  }
  if(allowed_absolute_step_error>=0.
     && stepsize*working_allowed_error>allowed_absolute_step_error) {
    working_allowed_error=allowed_absolute_step_error/stepsize;
  }
  if(allowed_relative_step_error>=0.
     && working_allowed_error>allowed_relative_step_error*max_dm_dt) {
    working_allowed_error = allowed_relative_step_error * max_dm_dt;
  }
  if(!forcestep) {
    next_timestep=1.0;  // Size relative to current step
    if(max_error>working_allowed_error) {
      next_timestep = step_headroom*working_allowed_error/max_error;
    } else if(dE>max_allowed_dE) {
      // Energy check
      next_timestep=0.5;
    }
    if(next_timestep<1.0) {
      // Reject step
      if(next_timestep<max_step_decrease)
  next_timestep=max_step_decrease;
      next_timestep *= stepsize;
      return 0;
    }
  }

  // Otherwise, accept step.  Calculate next step using
  // estimate of step size that would just meet the error
  // restriction (with "headroom" safety margin).
  next_timestep = max_step_increase;
  if(next_timestep*max_error>step_headroom*working_allowed_error) {
    next_timestep = step_headroom*working_allowed_error/max_error;
  }
  if(next_timestep<max_step_decrease)
    next_timestep=max_step_decrease;
  next_timestep *= stepsize;
  if(!nstate.AddDerivedData("Timestep lower bound",
          new_timestep_lower_bound) ||
     !nstate.AddDerivedData("Max dm/dt",new_max_dm_dt) ||
     !nstate.AddDerivedData("dE/dt",new_dE_dt) ||
     !nstate.AddDerivedData("Delta E",dE) ||
     !nstate.AddDerivedData("pE/pt",new_pE_pt)) {
    throw Oxs_Ext::Error(this,
       "YY_LLBEulerEvolve::Step:"
       " Programming error; data cache already set.");
  }

  dm_dt_t_output.cache.value.Swap(new_dm_dt_t);
  dm_dt_l_output.cache.value.Swap(new_dm_dt_l);
  dm_dt_t_output.cache.state_id = nstate.Id();
  dm_dt_l_output.cache.state_id = nstate.Id();

  energy.Swap(new_energy);
  energy_state_id = nstate.Id();

  return 1;  // Good step
}   // end Step

void YY_LLBEulerEvolve::UpdateMeshArrays(const Oxs_Mesh* mesh)
{
  mesh_id = 0; // Mark update in progress
  const OC_INDEX size = mesh->Size();
  OC_INDEX i;

  alpha_t_init->FillMeshValue(mesh,alpha_t0);
  gamma_init->FillMeshValue(mesh,gamma);
  Tc_init->FillMeshValue(mesh,Tc);
  alpha_t.AdjustSize(mesh);
  alpha_l.AdjustSize(mesh);

  for(i=0;i<size;i++) {
    alpha_t[i] = alpha_t0[i];
    alpha_l[i] = 0.0;
    //alpha_t[i] = alpha_t0[i]*(1-temperature/(3*Tc[i]));
    //alpha_l[i] = alpha_t0[i]*2*temperature/(3*Tc[i]);
  }

  if(gamma_style == GS_G) { // Convert to LL form
    for(i=0;i<size;++i) {
      OC_REAL8m cell_alpha_t = alpha_t[i];
      gamma[i] /= (1+cell_alpha_t*cell_alpha_t);
    }
  }

  if(!allow_signed_gamma) {
    for(i=0;i<size;++i) gamma[i] = fabs(gamma[i]);
  }

  mesh_id = mesh->Id();
}

// TODO: Calculation of coefficients are done here and around line 390.
// Depending on when the temperature and Ms are updated, try to put 
// everything in one place.
void YY_LLBEulerEvolve::FillHFluctConst(const Oxs_Mesh* mesh)
{
  // Update variables that will be constant factors in the simulation
  // h_fluctVarConst will store 2*kB*T*alpha/((1+alpha^2)*gamma*MU0*Vol*dt)
  const OC_INDEX size = mesh->Size();
  OC_REAL8m cell_alpha_t, cell_alpha_l, cell_gamma;
  for(OC_INDEX i=0;i<size;i++) {
    cell_alpha_t = fabs(alpha_t[i]);
    cell_alpha_l = fabs(alpha_l[i]);
    cell_gamma = fabs(gamma[i]);
    hFluctVarConst_t[i] = cell_alpha_t/(1+cell_alpha_t*cell_alpha_t);     // 2*alpha/(1+alpha^2)
    hFluctVarConst_t[i] *= 2.*KBoltzmann*temperature; // 2*kB*T*alpha/((1+alpha^2)*MU0*gamma*Volume*dt)
    hFluctVarConst_t[i] /= (MU0*cell_gamma*(mesh->Volume(i)));      // 2*alpha/((1+alpha^2)*MU0*gamma*dt)
    hFluctVarConst_l[i] = cell_alpha_l/(1+cell_alpha_l*cell_alpha_l);     // 2*alpha/(1+alpha^2)
    hFluctVarConst_l[i] *= 2.*KBoltzmann*temperature; // 2*kB*T*alpha/((1+alpha^2)*MU0*gamma*Volume*dt)
    hFluctVarConst_l[i] /= (MU0*cell_gamma*(mesh->Volume(i)));      // 2*alpha/((1+alpha^2)*MU0*gamma*dt)

    // by means of stochastic calculus (that is different from ordinary calculus) an additional deterministic term arises
    // when integrating stochastic equations in an Euler-Scheme (This term is called the noise induced drift term)
    inducedDriftConst_t[i] = -hFluctVarConst_t[i]
      *cell_gamma*cell_gamma*(1.+cell_alpha_t*cell_alpha_t);
    inducedDriftConst_l[i] = -hFluctVarConst_l[i]
      *cell_gamma*cell_gamma*(1.+cell_alpha_l*cell_alpha_l);
  }
}

void YY_LLBEulerEvolve::InitHFluct(const Oxs_Mesh* mesh)
{
  const OC_INDEX size = mesh->Size();
  hFluct_t.AdjustSize(mesh);
  hFluct_l.AdjustSize(mesh);
  for(OC_INDEX i=0;i<size;i++) {
    hFluct_t[i].Set(0., 0., 0.);
    hFluct_l[i].Set(0., 0., 0.);
  }
}

OC_REAL8m YY_LLBEulerEvolve::Calculate_m_e(OC_REAL8m J,
    OC_REAL8m T,
    OC_REAL8m tol_in = 1e-4) const
{
  // Solve for the equilibrium spin polarization m_e using the Newton's
  // method. Returns 0 when A <= 0 or A >= 1/3.
  const OC_REAL8m A = KBoltzmann*T/J;
  if(A <= 0 || A >= 1./3.) {
    return 0;
  }

  const OC_REAL8m tol = fabs(tol_in);
  OC_REAL8m x = 1.0/A;
  OC_REAL8m y = Langevin(x)-A*x;
  OC_REAL8m dy = LangevinDeriv(x)-A;
  while(fabs(y)>tol) {
    x -= y/dy;
    y = Langevin(x)-A*x;
    dy = LangevinDeriv(x)-A;
  }
  return A*x;
}

OC_REAL8m YY_LLBEulerEvolve::Langevin(OC_REAL8m x) const
{
  OC_REAL8m temp = exp(2*x)+1;
  temp /= exp(2*x)-1; // temp == coth(x);
  return temp-1/x;
}

OC_REAL8m YY_LLBEulerEvolve::LangevinDeriv(OC_REAL8m x) const
{
  OC_REAL8m temp = sinh(x);
  return -1.0/(temp*temp)+1.0/(x*x);
}

void YY_LLBEulerEvolve::UpdateDerivedOutputs(const Oxs_SimState& state)
{ // This routine fills all the YY_LLBEulerEvolve Oxs_ScalarOutput's to
  // the appropriate value based on the import "state", and any of
  // Oxs_VectorOutput's that have CacheRequest enabled are filled.
  // It also makes sure all the expected WOO objects in state are
  // filled.
  max_dm_dt_output.cache.state_id
    = dE_dt_output.cache.state_id
    = delta_E_output.cache.state_id
    = 0;  // Mark change in progress

  OC_REAL8m dummy_value;
  if(!state.GetDerivedData("Max dm/dt",max_dm_dt_output.cache.value) ||
     !state.GetDerivedData("dE/dt",dE_dt_output.cache.value) ||
     !state.GetDerivedData("Delta E",delta_E_output.cache.value) ||
     !state.GetDerivedData("pE/pt",dummy_value) ||
     !state.GetDerivedData("Timestep lower bound",dummy_value) ||
     (dm_dt_t_output.GetCacheRequestCount()>0
      && dm_dt_t_output.cache.state_id != state.Id()) ||
     (dm_dt_l_output.GetCacheRequestCount()>0
      && dm_dt_l_output.cache.state_id != state.Id()) ||
     (mxH_output.GetCacheRequestCount()>0
      && mxH_output.cache.state_id != state.Id())) {

    // Missing at least some data, so calculate from scratch

    // Calculate H and mxH outputs
    Oxs_MeshValue<ThreeVector>& mxH = mxH_output.cache.value;
    OC_REAL8m pE_pt;
    GetEnergyDensity(state,energy,&mxH,&total_field,pE_pt);
    energy_state_id=state.Id();
    mxH_output.cache.state_id=state.Id();
    if(!state.GetDerivedData("pE/pt",dummy_value)) {
      state.AddDerivedData("pE/pt",pE_pt);
    }

    // Calculate dm/dt, Max dm/dt and dE/dt
    Oxs_MeshValue<ThreeVector>& dm_dt_t
      = dm_dt_t_output.cache.value;
    Oxs_MeshValue<ThreeVector>& dm_dt_l
      = dm_dt_l_output.cache.value;
    dm_dt_t_output.cache.state_id=0;
    dm_dt_l_output.cache.state_id=0;
    OC_REAL8m timestep_lower_bound;
    Calculate_dm_dt(
        state,
        mxH,
        total_field,
        pE_pt,
        dm_dt_t,
        dm_dt_l,
        max_dm_dt_output.cache.value,
        dE_dt_output.cache.value,timestep_lower_bound);
    dm_dt_t_output.cache.state_id=state.Id();
    dm_dt_l_output.cache.state_id=state.Id();
    if(!state.GetDerivedData("Max dm/dt",dummy_value)) {
      state.AddDerivedData("Max dm/dt",max_dm_dt_output.cache.value);
    }
    if(!state.GetDerivedData("dE/dt",dummy_value)) {
      state.AddDerivedData("dE/dt",dE_dt_output.cache.value);
    }
    if(!state.GetDerivedData("Timestep lower bound",dummy_value)) {
      state.AddDerivedData("Timestep lower bound",
         timestep_lower_bound);
    }

    if(!state.GetDerivedData("Delta E",dummy_value)) {
      if(state.previous_state_id!=0 && state.stage_iteration_count>0) {
  // Strictly speaking, we should be able to create dE for
  // stage_iteration_count==0 for stages>0, but as a practical
  // matter we can't at present.  Should give this more thought.
  // -mjd, 27-July-2001
  throw Oxs_Ext::Error(this,
     "YY_LLBEulerEvolve::UpdateDerivedOutputs:"
     " Can't derive Delta E from single state.");
      }
      state.AddDerivedData("Delta E",0.0);
      dummy_value = 0.;
    }
    delta_E_output.cache.value=dummy_value;
  }

  max_dm_dt_output.cache.value*=(180e-9/PI);
  /// Convert from radians/second to deg/ns

  max_dm_dt_output.cache.state_id
    = dE_dt_output.cache.state_id
    = delta_E_output.cache.state_id
    = state.Id();
}   // end UpdateDerivedOutputs


OC_REAL8m YY_LLBEulerEvolve::Gaussian_Random(const OC_REAL8m muGaus,
    const OC_REAL8m sigmaGaus)
{
  // Box-Muller algorithm, see W.H. Press' "Numerical recipes" chapter7.2 
  // for details.
  OC_REAL8m R, gaus1, FAC;

  if (!gaus2_isset) {
    R = 1.;
    while (R >= 1.){
      gaus1 = 2. * Oc_UnifRand() - 1.;
      gaus2 = 2. * Oc_UnifRand() - 1.;
      R  = gaus1*gaus1 + gaus2*gaus2;
    }
    gaus2_isset = 1;
    FAC = sqrt(-2. * log(R) / R);
    gaus1 = gaus1 * FAC * sigmaGaus + muGaus;
    gaus2 = gaus2 * FAC * sigmaGaus + muGaus;
    return gaus1;
  }

  gaus2_isset = false;
  return gaus2;
}
