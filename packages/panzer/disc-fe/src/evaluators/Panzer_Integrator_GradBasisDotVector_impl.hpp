// @HEADER
// ***********************************************************************
//
//           Panzer: A partial differential equation assembly
//       engine for strongly coupled complex multiphysics systems
//                 Copyright (2011) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Roger P. Pawlowski (rppawlo@sandia.gov) and
// Eric C. Cyr (eccyr@sandia.gov)
// ***********************************************************************
// @HEADER

#ifndef   __Panzer_Integrator_GradBasisDotVector_impl_hpp__
#define   __Panzer_Integrator_GradBasisDotVector_impl_hpp__

///////////////////////////////////////////////////////////////////////////////
//
//  Include Files
//
///////////////////////////////////////////////////////////////////////////////

// Panzer
#include "Panzer_BasisIRLayout.hpp"
#include "Panzer_IntegrationRule.hpp"
#include "Panzer_Workset_Utilities.hpp"
#include "Panzer_HierarchicParallelism.hpp"

namespace panzer
{
  /////////////////////////////////////////////////////////////////////////////
  //
  //  Main Constructor
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename Traits>
  Integrator_GradBasisDotVector<EvalT, Traits>::
  Integrator_GradBasisDotVector(
    const panzer::EvaluatorStyle&        evalStyle,
    const std::string&                   resName,
    const std::string&                   fluxName,
    const panzer::BasisIRLayout&         basis,
    const panzer::IntegrationRule&       ir,
    const double&                        multiplier, /* = 1 */
    const std::vector<std::string>&      fmNames,    /* =
      std::vector<std::string>() */
    const Teuchos::RCP<PHX::DataLayout>& vecDL       /* = Teuchos::null */)
    :
    evalStyle_(evalStyle),
    multiplier_(multiplier),
    basisName_(basis.name())
  {
    using PHX::View;
    using panzer::BASIS;
    using panzer::Cell;
    using panzer::EvaluatorStyle;
    using panzer::IP;
    using PHX::DataLayout;
    using PHX::MDField;
    using std::invalid_argument;
    using std::logic_error;
    using std::string;
    using Teuchos::RCP;

    // Ensure the input makes sense.
    TEUCHOS_TEST_FOR_EXCEPTION(resName == "", invalid_argument, "Error:  "   \
      "Integrator_GradBasisDotVector called with an empty residual name.")
    TEUCHOS_TEST_FOR_EXCEPTION(fluxName == "", invalid_argument, "Error:  "   \
      "Integrator_GradBasisDotVector called with an empty flux name.")
    RCP<const PureBasis> tmpBasis = basis.getBasis();
    TEUCHOS_TEST_FOR_EXCEPTION(not tmpBasis->supportsGrad(), logic_error,
      "Error:  Integrator_GradBasisDotVector:  Basis of type \""
      << tmpBasis->name() << "\" does not support the gradient operator.")
    RCP<DataLayout> tmpVecDL = ir.dl_vector;
    if (not vecDL.is_null())
    {
      tmpVecDL = vecDL;
      TEUCHOS_TEST_FOR_EXCEPTION(
        tmpVecDL->extent(2) < ir.dl_vector->extent(2), logic_error,
        "Integrator_GradBasisDotVector:  Dimension of space exceeds "         \
        "dimension of Vector Data Layout.");
    } // end if (not vecDL.is_null())

    // Create the field for the vector-valued function we're integrating.
    vector_ = MDField<const ScalarT, Cell, IP, Dim>(fluxName, tmpVecDL);
    this->addDependentField(vector_);

    // Create the field that we're either contributing to or evaluating
    // (storing).
    field_ = MDField<ScalarT, Cell, BASIS>(resName, basis.functional);
    if (evalStyle_ == EvaluatorStyle::CONTRIBUTES)
      this->addContributedField(field_);
    else // if (evalStyle_ == EvaluatorStyle::EVALUATES)
      this->addEvaluatedField(field_);

    // Add the dependent field multipliers, if there are any.
    int i(0);
    fieldMults_.resize(fmNames.size());
    kokkosFieldMults_ = View<View<const ScalarT**>*>("GradBasisDotVector::KokkosFieldMultipliers",
      fmNames.size());
    for (const auto& name : fmNames)
    {
      fieldMults_[i++] = MDField<const ScalarT, Cell, IP>(name, ir.dl_scalar);
      this->addDependentField(fieldMults_[i - 1]);
    } // end loop over the field multipliers
    
    // Set the name of this object.
    string n("Integrator_GradBasisDotVector (");
    if (evalStyle_ == EvaluatorStyle::CONTRIBUTES)
      n += "CONTRIBUTES";
    else // if (evalStyle_ == EvaluatorStyle::EVALUATES)
      n += "EVALUATES";
    n += "):  " + field_.fieldTag().name();
    this->setName(n);
  } // end of Main Constructor

  /////////////////////////////////////////////////////////////////////////////
  //
  //  ParameterList Constructor
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename Traits>
  Integrator_GradBasisDotVector<EvalT, Traits>::
  Integrator_GradBasisDotVector(
    const Teuchos::ParameterList& p)
    :
    Integrator_GradBasisDotVector(
      panzer::EvaluatorStyle::EVALUATES,
      p.get<std::string>("Residual Name"),
      p.get<std::string>("Flux Name"),
      (*p.get<Teuchos::RCP<panzer::BasisIRLayout>>("Basis")),
      (*p.get<Teuchos::RCP<panzer::IntegrationRule>>("IR")),
      p.get<double>("Multiplier"),
      p.isType<Teuchos::RCP<const std::vector<std::string>>>
        ("Field Multipliers") ?
        (*p.get<Teuchos::RCP<const std::vector<std::string>>>
        ("Field Multipliers")) : std::vector<std::string>(),
      p.isType<Teuchos::RCP<PHX::DataLayout>>("Vector Data Layout") ?
        p.get<Teuchos::RCP<PHX::DataLayout>>("Vector Data Layout") :
        Teuchos::null)
  {
    using Teuchos::ParameterList;
    using Teuchos::RCP;

    // Ensure that the input ParameterList didn't contain any bogus entries.
    RCP<ParameterList> validParams = this->getValidParameters();
    p.validateParameters(*validParams);
  } // end of ParameterList Constructor

  /////////////////////////////////////////////////////////////////////////////
  //
  //  postRegistrationSetup()
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename Traits>
  void
  Integrator_GradBasisDotVector<EvalT, Traits>::
  postRegistrationSetup(
    typename Traits::SetupData sd,
    PHX::FieldManager<Traits>& /* fm */)
  {
    using panzer::getBasisIndex;
    using std::size_t;

    // Get the PHX::Views of the field multipliers.
    for (size_t i(0); i < fieldMults_.size(); ++i)
      kokkosFieldMults_(i) = fieldMults_[i].get_static_view();

    // Determine the index in the Workset bases for our particular basis name.
    basisIndex_ = getBasisIndex(basisName_, (*sd.worksets_)[0], this->wda);

    // Allocate temporary if not using shared memory
    bool use_shared_memory = panzer::HP::inst().useSharedMemory<ScalarT>();
    if (!use_shared_memory) {
      if (Sacado::IsADType<ScalarT>::value) {
	const auto fadSize = Kokkos::dimension_scalar(field_.get_view());
	tmp_ = PHX::View<ScalarT*>("GradBasisDotVector::tmp_",field_.extent(0),fadSize);
      } else {
	tmp_ = PHX::View<ScalarT*>("GradBasisDotVector::tmp_",field_.extent(0));
      }
    }
  } // end of postRegistrationSetup()

  /////////////////////////////////////////////////////////////////////////////
  //
  //  operator()() NO shared memory
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename Traits>
  template<int NUM_FIELD_MULT>
  KOKKOS_INLINE_FUNCTION
  void
  Integrator_GradBasisDotVector<EvalT, Traits>::
  operator()(
    const FieldMultTag<NUM_FIELD_MULT>& /* tag */,
    const Kokkos::TeamPolicy<PHX::exec_space>::member_type& team) const
  {
    using panzer::EvaluatorStyle;
    const int cell = team.league_rank();

    // Initialize the evaluated field.
    const int numQP(vector_.extent(1)), numDim(vector_.extent(2)),
              numBases(basis_.extent(1));
    if (evalStyle_ == EvaluatorStyle::EVALUATES)
      Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
        field_(cell, basis) = 0.0;
      });

    // The following if-block is for the sake of optimization depending on the
    // number of field multipliers.
    if (NUM_FIELD_MULT == 0)
    {
      // Loop over the quadrature points and dimensions of our vector fields,
      // scale the integrand by the multiplier, and then perform the
      // integration, looping over the bases.
      for (int qp(0); qp < numQP; ++qp)
      {
        for (int dim(0); dim < numDim; ++dim)
        {
	  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
	    field_(cell, basis) += basis_(cell, basis, qp, dim) * multiplier_ * vector_(cell, qp, dim);
	  });
        } // end loop over the dimensions of the vector field
      } // end loop over the quadrature points
    }
    else if (NUM_FIELD_MULT == 1)
    {
      // Loop over the quadrature points and dimensions of our vector fields,
      // scale the integrand by the multiplier and the single field multiplier,
      // and then perform the actual integration, looping over the bases.
      for (int qp(0); qp < numQP; ++qp)
      {
        for (int dim(0); dim < numDim; ++dim)
        {
	  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
	    field_(cell, basis) += basis_(cell, basis, qp, dim) * multiplier_ * vector_(cell, qp, dim) * kokkosFieldMults_(0)(cell, qp);
	  });
        } // end loop over the dimensions of the vector field
      } // end loop over the quadrature points
    }
    else
    {
      // Loop over the quadrature points and pre-multiply all the field
      // multipliers together.  Then loop over the dimensions of our vector
      // fields, scale the integrand by the multiplier and the combination of
      // the field multipliers, and then perform the actual integration,
      // looping over the bases.
      const int numFieldMults(kokkosFieldMults_.extent(0));
      for (int qp(0); qp < numQP; ++qp)
      {
	team.team_barrier();
	tmp_(cell) = 1.0;
        for (int fm(0); fm < numFieldMults; ++fm)
          tmp_(cell) *= kokkosFieldMults_(fm)(cell, qp);
        for (int dim(0); dim < numDim; ++dim)
        {
	  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
	    field_(cell, basis) += basis_(cell, basis, qp, dim) *  multiplier_ * vector_(cell, qp, dim) * tmp_(cell);
	  });
        } // end loop over the dimensions of the vector field
      } // end loop over the quadrature points
    } // end if (NUM_FIELD_MULT == something)
  } // end of operator()()

  /////////////////////////////////////////////////////////////////////////////
  //
  //  operator()() With shared memory
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename Traits>
  template<int NUM_FIELD_MULT>
  KOKKOS_INLINE_FUNCTION
  void
  Integrator_GradBasisDotVector<EvalT, Traits>::
  operator()(
    const SharedFieldMultTag<NUM_FIELD_MULT>& /* tag */,
    const Kokkos::TeamPolicy<PHX::exec_space>::member_type& team) const
  {
    using panzer::EvaluatorStyle;
    const int cell = team.league_rank();
    const int numQP = vector_.extent(1);
    const int numDim = vector_.extent(2);
    const int numBases = basis_.extent(1);
    const int fadSize = Kokkos::dimension_scalar(field_.get_view());

    scratch_view tmp;
    scratch_view tmp_field;
    if (Sacado::IsADType<ScalarT>::value) {
      tmp = scratch_view(team.team_shmem(),1,fadSize);
      tmp_field = scratch_view(team.team_shmem(),numBases,fadSize);
    }
    else {
      tmp = scratch_view(team.team_shmem(),1);
      tmp_field = scratch_view(team.team_shmem(),numBases);
    }

    // Initialize the evaluated field.
    Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
      tmp_field(basis) = 0.0;
    });

    // The following if-block is for the sake of optimization depending on the
    // number of field multipliers.
    if (NUM_FIELD_MULT == 0)
    {
      // Loop over the quadrature points and dimensions of our vector fields,
      // scale the integrand by the multiplier, and then perform the
      // integration, looping over the bases.
      for (int qp(0); qp < numQP; ++qp)
      {
        for (int dim(0); dim < numDim; ++dim)
        {
	  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
	    tmp_field(basis) += basis_(cell, basis, qp, dim) * multiplier_ * vector_(cell, qp, dim);
	  });
        } // end loop over the dimensions of the vector field
      } // end loop over the quadrature points
    }
    else if (NUM_FIELD_MULT == 1)
    {
      // Loop over the quadrature points and dimensions of our vector fields,
      // scale the integrand by the multiplier and the single field multiplier,
      // and then perform the actual integration, looping over the bases.
      for (int qp(0); qp < numQP; ++qp)
      {
        for (int dim(0); dim < numDim; ++dim)
        {
	  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
	    tmp_field(basis) += basis_(cell, basis, qp, dim) * multiplier_ * vector_(cell, qp, dim) * kokkosFieldMults_(0)(cell, qp);
	  });
        } // end loop over the dimensions of the vector field
      } // end loop over the quadrature points
    }
    else
    {
      // Loop over the quadrature points and pre-multiply all the field
      // multipliers together.  Then loop over the dimensions of our vector
      // fields, scale the integrand by the multiplier and the combination of
      // the field multipliers, and then perform the actual integration,
      // looping over the bases.
      const int numFieldMults(kokkosFieldMults_.extent(0));
      for (int qp(0); qp < numQP; ++qp)
      {
        ScalarT fieldMultsTotal(1); // need shared mem here
        for (int fm(0); fm < numFieldMults; ++fm)
          fieldMultsTotal *= kokkosFieldMults_(fm)(cell, qp);
        for (int dim(0); dim < numDim; ++dim)
        {
	  team.team_barrier();
          tmp(0) = multiplier_ * vector_(cell, qp, dim) * fieldMultsTotal;
	  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases), [&] (const int basis) {
	    tmp_field(basis) += basis_(cell, basis, qp, dim) * tmp(0);
	  });
        } // end loop over the dimensions of the vector field
      } // end loop over the quadrature points
    } // end if (NUM_FIELD_MULT == something)

    // Put final values into target field
    if (evalStyle_ == EvaluatorStyle::EVALUATES) {
      Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases),[&] (const int basis) {
	field_(cell,basis) = tmp_field(basis);
      });
    }
    else { // Contributed
      Kokkos::parallel_for(Kokkos::TeamThreadRange(team,0,numBases),[&] (const int basis) {
	field_(cell,basis) += tmp_field(basis);
      });
    }

  } // end of operator()()

  /////////////////////////////////////////////////////////////////////////////
  //
  //  evaluateFields()
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename Traits>
  void
  Integrator_GradBasisDotVector<EvalT, Traits>::
  evaluateFields(
    typename Traits::EvalData workset)
  {
    using Kokkos::parallel_for;
    using Kokkos::TeamPolicy;

    // Grab the basis information.
    basis_ = this->wda(workset).bases[basisIndex_]->weighted_grad_basis;

    bool use_shared_memory = panzer::HP::inst().useSharedMemory<ScalarT>();
    if (use_shared_memory) {
      int bytes;
      if (Sacado::IsADType<ScalarT>::value) {
	const int fadSize = Kokkos::dimension_scalar(field_.get_view());
	bytes = scratch_view::shmem_size(1,fadSize) + scratch_view::shmem_size(basis_.extent(1),fadSize);
      }
      else
	bytes = scratch_view::shmem_size(1) + scratch_view::shmem_size(basis_.extent(1));

      // The following if-block is for the sake of optimization depending on the
      // number of field multipliers.  The parallel_fors will loop over the cells
      // in the Workset and execute operator()() above.
      if (fieldMults_.size() == 0) {
	auto policy = panzer::HP::inst().teamPolicy<ScalarT,SharedFieldMultTag<0>,PHX::Device>(workset.num_cells).set_scratch_size(0,Kokkos::PerTeam(bytes));
        parallel_for(this->getName(), policy, *this);
      } else if (fieldMults_.size() == 1) {
	auto policy = panzer::HP::inst().teamPolicy<ScalarT,SharedFieldMultTag<1>,PHX::Device>(workset.num_cells).set_scratch_size(0,Kokkos::PerTeam(bytes));
        parallel_for(this->getName(), policy, *this);
      } else {
	auto policy = panzer::HP::inst().teamPolicy<ScalarT,SharedFieldMultTag<-1>,PHX::Device>(workset.num_cells).set_scratch_size(0,Kokkos::PerTeam(bytes));
        parallel_for(this->getName(), policy, *this);
      }
    }
    else {
      // The following if-block is for the sake of optimization depending on the
      // number of field multipliers.  The parallel_fors will loop over the cells
      // in the Workset and execute operator()() above.
      if (fieldMults_.size() == 0) {
	auto policy = panzer::HP::inst().teamPolicy<ScalarT,FieldMultTag<0>,PHX::Device>(workset.num_cells);
        parallel_for(this->getName(), policy, *this);
      } else if (fieldMults_.size() == 1) {
	auto policy = panzer::HP::inst().teamPolicy<ScalarT,FieldMultTag<1>,PHX::Device>(workset.num_cells);
        parallel_for(this->getName(), policy, *this);
      } else {
	auto policy = panzer::HP::inst().teamPolicy<ScalarT,FieldMultTag<-1>,PHX::Device>(workset.num_cells);
        parallel_for(this->getName(), policy, *this);
      }
    }
  } // end of evaluateFields()

  /////////////////////////////////////////////////////////////////////////////
  //
  //  getValidParameters()
  //
  /////////////////////////////////////////////////////////////////////////////
  template<typename EvalT, typename TRAITS>
  Teuchos::RCP<Teuchos::ParameterList>
  Integrator_GradBasisDotVector<EvalT, TRAITS>::
  getValidParameters() const
  {
    using panzer::BasisIRLayout;
    using panzer::IntegrationRule;
    using PHX::DataLayout;
    using std::string;
    using std::vector;
    using Teuchos::ParameterList;
    using Teuchos::RCP;
    using Teuchos::rcp;

    // Create a ParameterList with all the valid keys we support.
    RCP<ParameterList> p = rcp(new ParameterList);
    p->set<string>("Residual Name", "?");
    p->set<string>("Flux Name", "?");
    RCP<BasisIRLayout> basis;
    p->set("Basis", basis);
    RCP<IntegrationRule> ir;
    p->set("IR", ir);
    p->set<double>("Multiplier", 1.0);
    RCP<const vector<string>> fms;
    p->set("Field Multipliers", fms);
    RCP<DataLayout> vecDL;
    p->set("Vector Data Layout", vecDL);
    return p;
  } // end of getValidParameters()

} // end of namespace panzer

#endif // __Panzer_Integrator_GradBasisDotVector_impl_hpp__
