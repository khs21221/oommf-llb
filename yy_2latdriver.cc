/** FILE: yy_2latdriver.cc                 -*-Mode: c++-*-
 *
 * Abstract YY_2LatDriver class for ferrimagnet simulation. It fills states 
 * and initiates steps and registers the command "Oxs_Run" with the Tcl
 * interpreter.
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

#include <map>
#include <string>
#include <vector>

#include "oc.h"
#include "director.h"
#include "key.h"
#include "scalarfield.h"
#include "simstate.h"
#include "util.h"
#include "energy.h"     // Needed to make MSVC++ 5 happy
#include "oxswarn.h"
#include "vectorfield.h"

#include "yy_2latdriver.h"

/* End includes */

#define OSO_INIT(name,descript,units) \
   name##_output.Setup(this,InstanceName(),descript,units,0, \
           &YY_2LatDriver::Fill__##name##_output)
// Constructor
YY_2LatDriver::YY_2LatDriver
( const char* name,        // Child instance id
  Oxs_Director* newdtr,    // App director
  const char* argstr       // Args
  ) : Oxs_Driver(name,newdtr,argstr)
// Parent Oxs_Driver initializes members
{
  Oxs_OwnedPointer<Oxs_ScalarField> Msinit;
  OXS_GET_INIT_EXT_OBJECT("Ms2",Oxs_ScalarField,Msinit);

  OXS_GET_INIT_EXT_OBJECT("m02",Oxs_VectorField,m02);

  // Fill Ms and Ms_inverse array, and verify that Ms is non-negative.
  Msinit->FillMeshValue(mesh_obj.GetPtr(),Ms2);
  Ms_inverse2.AdjustSize(mesh_obj.GetPtr());
  for(OC_INDEX icell=0;icell<mesh_obj->Size();icell++) {
    if(Ms2[icell]<0.0) {
      char buf[1024];
      Oc_Snprintf(buf,sizeof(buf),
                  "Negative Ms2 value (%g) detected at mesh index %u.",
                  static_cast<double>(Ms2[icell]),icell);
      throw Oxs_ExtError(this,String(buf));
    } else if(Ms2[icell]==0.0) {
      Ms_inverse2[icell]=0.0; // Special case handling
    } else {
      Ms_inverse2[icell]=1.0/Ms2[icell];
    }
  }

}

//Destructor
YY_2LatDriver::~YY_2LatDriver()
{
}

// The following routine is called by GetInitialState() in child classes.
void YY_2LatDriver::SetStartValues (Oxs_SimState& istate) const
{
  OC_BOOL fresh_start = 1;
  //  Not supporting restart yet.

  if(fresh_start) {
    istate.previous_state_id = 0;
    istate.iteration_count       = start_iteration;
    istate.stage_number          = start_stage;
    istate.stage_iteration_count = start_stage_iteration;
    istate.stage_start_time      = start_stage_start_time;
    istate.stage_elapsed_time    = start_stage_elapsed_time;
    istate.last_timestep         = start_last_timestep;
    istate.mesh = mesh_key.GetPtr();
    // To circumvent the const regulation of Ms, construct a temporary
    // variable using the value of Ms.
    Oxs_MeshValue<OC_REAL8m> Ms_temp(Ms);
    Oxs_MeshValue<OC_REAL8m> Ms_inverse_temp(Ms_inverse);
    istate.Ms = &Ms_temp;
    istate.Ms_inverse = &Ms_inverse_temp;
    m0->FillMeshValue(istate.mesh,istate.spin);
    // Insure that all spins are unit vectors
    OC_INDEX size = istate.spin.Size();
    for(OC_INDEX i=0;i<size;i++) istate.spin[i].MakeUnit();
  }
}

void YY_2LatDriver::SetStartValues2 (Oxs_SimState& istate) const
{
  OC_BOOL fresh_start = 1;
  if(fresh_start) {
    istate.previous_state_id = 0;
    istate.iteration_count       = start_iteration;
    istate.stage_number          = start_stage;
    istate.stage_iteration_count = start_stage_iteration;
    istate.stage_start_time      = start_stage_start_time;
    istate.stage_elapsed_time    = start_stage_elapsed_time;
    istate.last_timestep         = start_last_timestep;
    istate.mesh = mesh_key.GetPtr();
    // To circumvent the const regulation of Ms, construct a temporary
    // variable using the value of Ms.
    Oxs_MeshValue<OC_REAL8m> Ms_temp(Ms2);
    Oxs_MeshValue<OC_REAL8m> Ms_inverse_temp(Ms_inverse2);
    istate.Ms = &Ms_temp;
    istate.Ms_inverse = &Ms_inverse_temp;
    m02->FillMeshValue(istate.mesh,istate.spin);
    // Insure that all spins are unit vectors
    OC_INDEX size = istate.spin.Size();
    for(OC_INDEX i=0;i<size;i++) istate.spin[i].MakeUnit();
  }
}

// The following routine is called by GetInitialState() in child classes.
void YY_2LatDriver::SetStartValues (Oxs_Key<Oxs_SimState>& initial_state) const
{
  OC_BOOL fresh_start = 1;
  int rflag = director->GetRestartFlag();
  Oxs_SimState& istate = initial_state.GetWriteReference(); // Write lock

  // No checkpoint file support at this point

  if(fresh_start) {
    istate.previous_state_id = 0;
    istate.iteration_count       = start_iteration;
    istate.stage_number          = start_stage;
    istate.stage_iteration_count = start_stage_iteration;
    istate.stage_start_time      = start_stage_start_time;
    istate.stage_elapsed_time    = start_stage_elapsed_time;
    istate.last_timestep         = start_last_timestep;
    istate.mesh = mesh_key.GetPtr();

    istate.Ms = &Ms;
    istate.Ms_inverse = &Ms_inverse;
    Ms = static_cast<const Oxs_MeshValue<OC_REAL8m>&>(Ms);
    Ms_inverse = Ms_inverse;
    m0->FillMeshValue(istate.mesh,istate.spin);
    // Insure that all spins are unit vectors
    OC_INDEX size = istate.spin.Size();
    for(OC_INDEX i=0;i<size;i++) istate.spin[i].MakeUnit();

    initial_state.GetReadReference();
  }
}

void YY_2LatDriver::SetStartValues2 (Oxs_Key<Oxs_SimState>& initial_state) const
{
  OC_BOOL fresh_start = 1;
  int rflag = director->GetRestartFlag();
  Oxs_SimState& istate = initial_state.GetWriteReference(); // Write lock

  if(fresh_start) {
    istate.previous_state_id = 0;
    istate.iteration_count       = start_iteration;
    istate.stage_number          = start_stage;
    istate.stage_iteration_count = start_stage_iteration;
    istate.stage_start_time      = start_stage_start_time;
    istate.stage_elapsed_time    = start_stage_elapsed_time;
    istate.last_timestep         = start_last_timestep;
    istate.mesh = mesh_key.GetPtr();

    istate.Ms = &Ms2;
    istate.Ms_inverse = &Ms_inverse2;
    Ms2 = static_cast<const Oxs_MeshValue<OC_REAL8m>&>(Ms2);
    Ms_inverse2 = Ms_inverse2;
    m02->FillMeshValue(istate.mesh,istate.spin);
    // Insure that all spins are unit vectors
    OC_INDEX size = istate.spin.Size();
    for(OC_INDEX i=0;i<size;i++) istate.spin[i].MakeUnit();

    initial_state.GetReadReference();
  }
}

// YY_2LatDriver version of Init().  All children of YY_2LatDriver *must*
// call this function in their Init() routines.  The main purpose
// of this function is to setup base driver outputs and to initialize
// the current state.
OC_BOOL YY_2LatDriver::Init()
{
  if(!Oxs_Ext::Init()) return 0;

  // Try not to call Oxs_Driver::Init() but do all procedures here.
  //if(!Oxs_Driver::Init()) return 0;

  OC_BOOL success=1;
  problem_status = OXSDRIVER_PS_INVALID; // Safety

#if REPORT_TIME
  Oc_TimeVal cpu,wall;
  driversteptime.GetTimes(cpu,wall);
  if(double(wall)>0.0) {
    fprintf(stderr,"Full Step time (secs)%7.2f cpu /%7.2f wall,"
            " module %.1000s (%u iterations)\n",
            double(cpu),double(wall),InstanceName(),GetIteration());
  }
  driversteptime.Reset();
#endif // REPORT_TIME

  // Finish output initializations.
  if(!mesh_obj->HasUniformCellVolumes()) {
    // Magnetization averaging should be weighted by cell volume.  At
    // present, however, the only available mesh is
    // Oxs_RectangularMesh, which has uniform cell volumes.  The
    // computation in this case can be faster, so for now we code only
    // for that case.  Check and throw an error, though, so we will be
    // reminded to change this if new mesh types become available in
    // the future.
    throw Oxs_ExtError(this,"NEW CODE REQUIRED: Current YY_2LatDriver"
                         " aveM and projection outputs require meshes "
                         " with uniform cell sizes, such as "
                         "Oxs_RectangularMesh.");
  }
  if(normalize_aveM) {
    const OC_INDEX mesh_size = Ms.Size();
    OC_REAL8m sum = 0.0;
    for(OC_INDEX j=0;j<mesh_size;++j) sum += fabs(Ms[j]);
    if(sum>0.0) scaling_aveM = 1.0/sum;
    else        scaling_aveM = 1.0; // Safety
  } else {
    if(Ms.Size()>0) scaling_aveM = 1.0/static_cast<OC_REAL8m>(Ms.Size());
    else            scaling_aveM = 1.0; // Safety
  }

  const OC_INDEX projection_count = projection_output.GetSize();
  for(OC_INDEX i=0;i<projection_count;++i) {
    OxsDriverProjectionOutput& po = projection_output[i];

    // Fill projection trellis with vector fields sized to mesh
    Oxs_MeshValue<ThreeVector>& trellis = po.trellis;
    Oxs_OwnedPointer<Oxs_VectorField> tmpinit; // Initializer
    OXS_GET_EXT_OBJECT(po.trellis_init,Oxs_VectorField,tmpinit);
    tmpinit->FillMeshValue(mesh_obj.GetPtr(),trellis);

    // Adjust scaling
    po.scaling = 1.0; // Safety
    if(po.normalize) {
      const OC_INDEX mesh_size = trellis.Size();
      OC_REAL8m sum = 0.0;
      if(normalize_aveM) {
        for(OC_INDEX j=0;j<mesh_size;++j) {
          sum += fabs(Ms[j])*sqrt(trellis[j].MagSq());
        }
      } else {
        for(OC_INDEX j=0;j<mesh_size;++j) {
          sum += sqrt(trellis[j].MagSq());
        }
      }
      if(sum>0.0) po.scaling = 1.0/sum;
    } else {
      po.scaling = scaling_aveM;
    }
    po.scaling *= po.user_scaling;
  }

  // Adjust spin output to always use full precision
  String default_format = spin_output.GetOutputFormat();
  Nb_SplitList arglist;
  if(arglist.Split(default_format.c_str())!=TCL_OK) {
    char bit[4000];
    Oc_EllipsizeMessage(bit,sizeof(bit),default_format.c_str());
    char temp_buf[4500];
    Oc_Snprintf(temp_buf,sizeof(temp_buf),
                "Format error in spin output format string---"
                "not a proper Tcl list: %.4000s",
                bit);
    throw Oxs_ExtError(this,temp_buf);
  }
  if(arglist.Count()!=2) {
    OXS_THROW(Oxs_ProgramLogicError,
              "Wrong number of arguments in spin output format string, "
              "detected in YY_2LatDriver Init");
  } else {
    vector<String> sarr;
    sarr.push_back(arglist[0]); // Data type
    if(sarr[0].compare("binary") == 0) {
      sarr.push_back("8");      // Precision
    } else {
      sarr.push_back("%.17g");
    }
    String precise_format = Nb_MergeList(&sarr);
    spin_output.SetOutputFormat(precise_format.c_str());
  }

  // Determine total stage count requirements
  unsigned int min,max;
  director->ExtObjStageRequestCounts(min,max);
  if(stage_count_check!=0 && min>max) {
    char buf[1024];
    Oc_Snprintf(buf,sizeof(buf),
                "Stage count request incompatibility detected;"
                " request range is [%u,%u].  Double check stage"
                " lists and applied field specifications.  The"
                " stage count compatibility check may be disabled"
                " in the driver Specify block by setting"
                " stage_count_check to 0.",
                min,max);
    throw Oxs_ExtError(this,String(buf));
  }

  // Parameter stage_count_request overrides all automatic settings
  // if set to a value different from 0.  Otherwise, use maximal
  // "min" value requested by all ext objects, unless that value is
  // zero, in which case we use a 1 stage default.
  if(stage_count_request>0) number_of_stages = stage_count_request;
  else                      number_of_stages = min;
  if(number_of_stages<1) number_of_stages=1; // Default.


  // Initialize current state from initial state provided by
  // concrete child class.
  problem_status = OXSDRIVER_PS_INVALID;
  checkpoint_id = 0;

  GetInitialState(current_state, current_state2);

  if (current_state.GetPtr() == NULL || current_state2.GetPtr() == NULL) {
    success = 0; // Error.  Perhaps an exception throw would be better?
  } else {
    const Oxs_SimState& cstate = current_state.GetReadReference();
    const Oxs_SimState& cstate2 = current_state2.GetReadReference();
    // If initial state was loaded from a checkpoint file, then
    // the problem status should be available from the state
    // derived data.  Otherwise, use the default STAGE_START
    // status.
    OC_REAL8m value;
    if(cstate.GetDerivedData("YY_2LatDriver Problem Status",value)
        && cstate2.GetDerivedData("YY_2LatDriver Problem Status",value)) {
      problem_status = FloatToProblemStatus(value);
    } else {
      problem_status = OXSDRIVER_PS_STAGE_START;
    }
    // There is no need (presumably?) to write the initial
    // state as a checkpoint file, so save the id of it.
    checkpoint_id = cstate.Id();
  }

  Oc_TimeVal dummy_time;
  Oc_Times(dummy_time,checkpoint_time,0); // Initialize checkpoint time

  return success;
}

OC_BOOL YY_2LatDriver::IsStageDone(
    const Oxs_SimState& state,
    const Oxs_SimState& state2) const
{
  if(state.stage_done == Oxs_SimState::DONE
      && state2.stage_done == Oxs_SimState::DONE) return 1;
  if(state.stage_done == Oxs_SimState::NOT_DONE
      && state2.stage_done == Oxs_SimState::NOT_DONE) return 0;
  /// Otherwise, state.stage_done == Oxs_SimState::UNKNOWN

  // Check state against parent YY_2LatDriver class stage limiters.
  if( total_iteration_limit > 0
     && (state.iteration_count >= total_iteration_limit
       || state2.iteration_count >= total_iteration_limit) ) {
    state.stage_done = Oxs_SimState::DONE;
    state2.stage_done = Oxs_SimState::DONE;
    return 1;
  }

  // The following is checked only with one of 2 states.
  // Stage iteration check
  OC_UINT4m stop_iteration=0;
  if(state.stage_number >= stage_iteration_limit.size()) {
    stop_iteration
      = stage_iteration_limit[stage_iteration_limit.size()-1];
  } else {
    stop_iteration = stage_iteration_limit[state.stage_number];
  }
  if(stop_iteration>0
     && state.stage_iteration_count + 1 >= stop_iteration) {
    // Note: stage_iteration_count is 0 based, so the number
    // of iterations is stage_iteration_count + 1.
    state.stage_done = Oxs_SimState::DONE;
    state2.stage_done = Oxs_SimState::DONE;
    return 1;
  }

  // Otherwise, leave it up to the child
  if(ChildIsStageDone(state,state2)) {
    state.stage_done = Oxs_SimState::DONE;
    state2.stage_done = Oxs_SimState::DONE;
    return 1;
  }

  state.stage_done = Oxs_SimState::NOT_DONE;
  state2.stage_done = Oxs_SimState::NOT_DONE;
  return 0;
}

OC_BOOL YY_2LatDriver::IsRunDone(
    const Oxs_SimState& state,
    const Oxs_SimState& state2) const
{
  if(state.run_done == Oxs_SimState::DONE
      && state2.run_done == Oxs_SimState::DONE) return 1;
  if(state.run_done == Oxs_SimState::NOT_DONE
      && state2.run_done == Oxs_SimState::NOT_DONE) return 0;
  /// Otherwise, state.run_done == Oxs_SimState::unknown

  // Check state against parent YY_2LatDriver class run limiters.
  if( total_iteration_limit > 0
     && (state.iteration_count >= total_iteration_limit
       || state2.iteration_count >= total_iteration_limit) ) {
    state.run_done = Oxs_SimState::DONE;
    state2.run_done = Oxs_SimState::DONE;
    return 1;
  }

  // The following is checked only with one of 2 states.
  if(number_of_stages > 0) {
    if( state.stage_number >= number_of_stages ||
        (state.stage_number+1 == number_of_stages
        && IsStageDone(state,state2))) {
      state.run_done = Oxs_SimState::DONE;
      state2.run_done = Oxs_SimState::DONE;
      return 1;
    }
  }      

  // Otherwise, leave it up to the child
  if(ChildIsRunDone(state,state2)) {
    state.run_done = Oxs_SimState::DONE;
    state2.run_done = Oxs_SimState::DONE;
    return 1;
  }

  state.run_done = Oxs_SimState::NOT_DONE;
  state2.run_done = Oxs_SimState::NOT_DONE;
  return 0;
}

void YY_2LatDriver::Run(vector<OxsRunEvent>& results,
                     OC_INT4m stage_increment)
{ // Called by director
  if(current_state.GetPtr() == NULL || current_state2.GetPtr() == NULL) {
    // Current state is not initialized.
    String msg="Current state in YY_2LatDriver is not initialized;"
      " This is probably the fault of the child class "
      + String(ClassName());
    throw Oxs_ExtError(this,msg);
  }

  if(current_state.ObjectId()==0 || current_state2.ObjectId()==0) {
    // Current state is not fixed, i.e., is incomplete or transient.
    // To some extent, this check is not necessary, because key should
    // throw an exception on GetReadReference if the pointed to Oxs_Lock
    // object isn't fixed.
    String msg="PROGRAMMING ERROR:"
      " Current state in YY_2LatDriver is incomplete or transient;"
      " This is probably the fault of the child class "
      + String(ClassName());
    throw Oxs_ExtError(this,msg);
  }

  int step_events=0;
  int stage_events=0;
  int done_event=0;
  int step_calls=0; // Number of times child Step() routine is called.

  // There are two considerations involved in the decision to break out
  // of the following step loops: 1) scheduled events should be passed
  // back to the caller for processing while the associated state
  // information is available, and 2) interactive requests should be
  // responded to in a timely manner.  In the future, control criteria
  // for each of these issues should be passed in from the caller.  For
  // the present, though, just ensure that no scheduled events are
  // overlooked by setting max_steps to 1, and guess that 2 step
  // attempts isn't too long between checking for interactive requests.

  const int max_steps=1; // TODO: should be set by caller.
  const int allowed_step_calls=2; // TODO: should be set by caller.

  while (step_events<max_steps && step_calls<allowed_step_calls
         && problem_status!=OXSDRIVER_PS_DONE) {
    Oxs_Key<Oxs_SimState> next_state;
    Oxs_ConstKey<Oxs_SimState> previous_state; // Used for state transitions
    Oxs_Key<Oxs_SimState> next_state2;
    Oxs_ConstKey<Oxs_SimState> previous_state2; // Used for state transitions
    OC_BOOL step_taken=0;
    OC_BOOL step_result=0;
    switch(problem_status) {
      case OXSDRIVER_PS_INSIDE_STAGE:
        // Most common case.
        current_state.GetReadReference(); // Safety: protection against overwrite
        current_state2.GetReadReference();
        director->GetNewSimulationState(next_state);
        director->GetNewSimulationState(next_state2);
        // NOTE: At this point next_state holds a write lock.
        //   The Step() function can make additional calls
        //   to next_state.GetWriteReference() as needed; write
        //   locks do not accumulate.  However, it is the
        //   responsibility of Step or its callees to release
        //   the write lock, once next_state is fully populated.
#if REPORT_TIME
        driversteptime.Start();
#endif // REPORT_TIME
        step_result = Step(
            current_state,
            current_state2,
            step_info,
            next_state,
            next_state2);
#if REPORT_TIME
        driversteptime.Stop();
#endif // REPORT_TIME
        if( step_result ) {
          // Good step.  Release read lock on old current_state,
          // and copy key from next_state.
          next_state.GetReadReference();  // Safety write lock release
          next_state2.GetReadReference();  // Safety write lock release
          current_state = next_state; // Free old read lock
          current_state2 = next_state2; // Free old read lock
          if(report_max_spin_angle) {
            UpdateSpinAngleData(
                *(current_state.GetPtr()),
                *(current_state2.GetPtr())); // Update
            /// max spin angle data on each accepted step.  Might want
            /// to modify this to instead estimate max angle change,
            /// and only do actually calculation when estimate uncertainty
            /// gets larger than some specified value.
          }
          step_taken=1;
          step_info.current_attempt_count=0;
        } else {
          ++step_info.current_attempt_count;
        }
        ++step_info.total_attempt_count;
        ++step_calls;
        break;

      case OXSDRIVER_PS_STAGE_END: {
        const Oxs_SimState& cstate = current_state.GetReadReference();
        const Oxs_SimState& cstate2 = current_state2.GetReadReference();
        director->GetNewSimulationState(next_state);
        Oxs_SimState& nstate = next_state.GetWriteReference();
        director->GetNewSimulationState(next_state2);
        Oxs_SimState& nstate2 = next_state2.GetWriteReference();
        FillNewStageState(cstate,cstate.stage_number+stage_increment,
                          nstate);
        FillNewStageState(cstate2,cstate2.stage_number+stage_increment,
                          nstate2);
        next_state.GetReadReference(); // Release write lock
        next_state2.GetReadReference(); // Release write lock
        previous_state.Swap(current_state); // For state transistion
        previous_state2.Swap(current_state2); // For state transistion
        current_state = next_state;
        current_state2 = next_state2;
      }
      // NB: STAGE_END flow continues through STAGE_START block
      case OXSDRIVER_PS_STAGE_START:
        // Default: NOP
        InitNewStage(
            current_state,
            current_state2,
            previous_state,
            previous_state2); // Send state to
                              /// evolver for bookkeeping updates.
        previous_state.Release();
        previous_state2.Release();
        step_taken=1;
        ++step_info.total_attempt_count;
        step_info.current_attempt_count=0;
        break;

      case OXSDRIVER_PS_DONE: // DONE status not allowed inside loop
      case OXSDRIVER_PS_INVALID:
        throw Oxs_ExtError(this,"PROGRAMMING ERROR:"
                             " Invalid problem status detected in"
                             " YY_2LatDriver::Run().");
    }

    if(step_taken) {
      const Oxs_SimState& cstate = current_state.GetReadReference();
      const Oxs_SimState& cstate2 = current_state2.GetReadReference();
      ++step_events;
      problem_status = OXSDRIVER_PS_INSIDE_STAGE;
      if (IsStageDone(cstate,cstate2)) {
        ++stage_events;
        problem_status = OXSDRIVER_PS_STAGE_END;
        if (IsRunDone(cstate,cstate2)) {
          ++done_event;
          problem_status = OXSDRIVER_PS_DONE;
        }
      }
#ifndef NDEBUG
      // For debugging purposes, we verify the return from
      // AddDerivedData, which returns True on success.  Failure might
      // happen if the previous status was STAGE_START, since in that
      // case the new current state is the same as the previous current
      // state, and so may already have a problem status recorded.  I
      // think this shouldn't happen, and I don't see any way it could
      // be a significant problem if it did happen, so for non-debug
      // builds just remove the check, thereby ignoring any hiccups.
      if(!cstate.AddDerivedData("YY_2LatDriver Problem Status",
                                static_cast<OC_REAL8m>(problem_status))) {
        OC_REAL8m oldvalue = -1.0;
        OC_REAL8m oldvalue2 = -1.0;
        if(!cstate.GetDerivedData("YY_2LatDriver Problem Status",
              oldvalue)
            || !cstate2.GetDerivedData("YY_2LatDriver Problem Status",
              oldvalue2)) {
          throw Oxs_ExtError(this,"Undiagnosable error trying to"
                               " set YY_2LatDriver Problem Status into"
                               " current state.");
        }
        char buf[1000];
        Oc_Snprintf(buf,sizeof(buf),
                    "Error setting YY_2LatDriver Problem Status"
                    " into current state; value already set."
                    " Old value: %d, %d, New value: %d",
                    static_cast<int>(oldvalue),
                    static_cast<int>(oldvalue2),
                    static_cast<int>(problem_status));
        throw Oxs_ExtError(this,String(buf));
      }
#else
      cstate.AddDerivedData("YY_2LatDriver Problem Status",
                            static_cast<OC_REAL8m>(problem_status));
      cstate2.AddDerivedData("YY_2LatDriver Problem Status",
                            static_cast<OC_REAL8m>(problem_status));
#endif
    }

    // TODO: Implement checkpoint file save

  } // End of 'step_events<max_steps ...' loop

  // Currently above block generates at most a single step.  When it
  // goes multi-step the report mechanism will need to be adjusted.
  results.clear();
  if(step_events) {
    results.push_back(OxsRunEvent(OXS_STEP_EVENT,current_state));
  }
  if(stage_events) {
    results.push_back(OxsRunEvent(OXS_STAGE_DONE_EVENT,current_state));
  }
  if(done_event) {
    results.push_back(OxsRunEvent(OXS_RUN_DONE_EVENT,current_state));
  }
}

// TODO: Report max spin angle for both sublattices.
void YY_2LatDriver::UpdateSpinAngleData(
    const Oxs_SimState& state,
    const Oxs_SimState& state2) const
{
  if(!report_max_spin_angle) {
    throw Oxs_ExtError(this,"PROGRAMMING ERROR:"
        " Input MIF file requested no driver spin angle reports,"
        " but YY_2LatDriver::UpdateSpinAngleData is called.");
    static Oxs_WarningMessage nocall(3);
  }
  OC_REAL8m maxang,stage_maxang,run_maxang;
  stage_maxang = run_maxang = -1.0; // Safety init
  maxang = state.mesh->MaxNeighborAngle(state.spin,*(state.Ms))*(180./PI);
  state.GetDerivedData("PrevState Stage Max Spin Ang",stage_maxang);
  state.GetDerivedData("PrevState Run Max Spin Ang",run_maxang);
  if(maxang>stage_maxang) stage_maxang=maxang;
  if(maxang>run_maxang)   run_maxang=maxang;
  state.AddDerivedData("Max Spin Ang",maxang);
  state.AddDerivedData("Stage Max Spin Ang",stage_maxang);
  state.AddDerivedData("Run Max Spin Ang",run_maxang);
}

#undef YY_DEBUG
