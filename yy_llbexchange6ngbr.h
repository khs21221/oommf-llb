/* FILE: exchange6ngbr.h            -*-Mode: c++-*-
 *
 * 6 neighbor exchange energy on rectangular mesh,
 * derived from Oxs_Energy class.
 *
 */

#ifndef _YY_LLBEXCHANGE6NGBR
#define _YY_LLBEXCHANGE6NGBR

#include "atlas.h"
#include "key.h"
#include "chunkenergy.h"
#include "energy.h"
#include "mesh.h"
#include "meshvalue.h"
#include "simstate.h"
#include "threevector.h"
#include "rectangularmesh.h"

/* End includes */

class YY_LLBExchange6Ngbr
  : public Oxs_ChunkEnergy, public Oxs_EnergyPreconditionerSupport {
private:
  enum ExchangeCoefType {
    A_UNKNOWN, A_TYPE, LEX_TYPE
  }  excoeftype;

  OC_INDEX coef_size;
  OC_REAL8m** coef;
  mutable Oxs_Key<Oxs_Atlas> atlaskey;  
  Oxs_OwnedPointer<Oxs_Atlas> atlas;
  mutable Oxs_ThreadControl thread_control;
  mutable OC_UINT4m mesh_id;
  mutable Oxs_MeshValue<OC_INT4m> region_id;

  // Support for threaded maxang calculations
  mutable vector<OC_REAL8m> maxdot;

  void CalcEnergyA(const Oxs_SimState& state,
                   Oxs_ComputeEnergyDataThreaded& ocedt,
                   Oxs_ComputeEnergyDataThreadedAux& ocedtaux,
                   OC_INDEX node_start,OC_INDEX node_stop,
                   int threadnumber) const;

  // Supplied outputs, in addition to those provided by Oxs_Energy.
  Oxs_ScalarOutput<YY_LLBExchange6Ngbr> maxspinangle_output;
  Oxs_ScalarOutput<YY_LLBExchange6Ngbr> stage_maxspinangle_output;
  Oxs_ScalarOutput<YY_LLBExchange6Ngbr> run_maxspinangle_output;
  void UpdateDerivedOutputs(const Oxs_SimState& state);
  String MaxSpinAngleStateName() const {
    String dummy_name = InstanceName();
    dummy_name += ":Max Spin Angle";
    return dummy_name;
  }
  String StageMaxSpinAngleStateName() const {
    String dummy_name = InstanceName();
    dummy_name += ":Stage Max Spin Angle";
    return dummy_name;
  }
  String RunMaxSpinAngleStateName() const {
    String dummy_name = InstanceName();
    dummy_name += ":Run Max Spin Angle";
    return dummy_name;
  }

protected:
  virtual void GetEnergy(const Oxs_SimState& state,
			 Oxs_EnergyData& oed) const {
    GetEnergyAlt(state,oed);
  }

  virtual void ComputeEnergy(const Oxs_SimState& state,
                             Oxs_ComputeEnergyData& oced) const {
    ComputeEnergyAlt(state,oced);
  }

  virtual void ComputeEnergyChunkInitialize
  (const Oxs_SimState& state,
   Oxs_ComputeEnergyDataThreaded& ocedt,
   Oxs_ComputeEnergyDataThreadedAux& ocedtaux,
   int number_of_threads) const;

  virtual void ComputeEnergyChunkFinalize
  (const Oxs_SimState& state,
   const Oxs_ComputeEnergyDataThreaded& ocedt,
   const Oxs_ComputeEnergyDataThreadedAux& ocedtaux,
   int number_of_threads) const;

  virtual void ComputeEnergyChunk(const Oxs_SimState& state,
                                  Oxs_ComputeEnergyDataThreaded& ocedt,
                                  Oxs_ComputeEnergyDataThreadedAux& ocedtaux,
                                  OC_INDEX node_start,OC_INDEX node_stop,
                                  int threadnumber) const;

public:
  virtual const char* ClassName() const; // ClassName() is
  /// automatically generated by the OXS_EXT_REGISTER macro.
  YY_LLBExchange6Ngbr(const char* name,     // Child instance id
		    Oxs_Director* newdtr, // App director
		    const char* argstr);  // MIF input block parameters
  virtual ~YY_LLBExchange6Ngbr();
  virtual OC_BOOL Init();

  // Optional interface for conjugate-gradient evolver.
  virtual int IncrementPreconditioner(PreconditionerData& pcd);
};


#endif // _YY_LLBEXCHANGE6NGBR
