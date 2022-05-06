/* -------------------------------------------------------------------------*
*								SPHinXsys									*
* --------------------------------------------------------------------------*
* SPHinXsys (pronunciation: s'finksis) is an acronym from Smoothed Particle	*
* Hydrodynamics for industrial compleX systems. It provides C++ APIs for	*
* physical accurate simulation and aims to model coupled industrial dynamic *
* systems including fluid, solid, multi-body dynamics and beyond with SPH	*
* (smoothed particle hydrodynamics), a meshless computational method using	*
* particle discretization.													*
*																			*
* SPHinXsys is partially funded by German Research Foundation				*
* (Deutsche Forschungsgemeinschaft) DFG HU1527/6-1, HU1527/10-1				*
* and HU1527/12-1.															*
*                                                                           *
* Portions copyright (c) 2017-2020 Technical University of Munich and		*
* the authors' affiliations.												*
*                                                                           *
* Licensed under the Apache License, Version 2.0 (the "License"); you may   *
* not use this file except in compliance with the License. You may obtain a *
* copy of the License at http://www.apache.org/licenses/LICENSE-2.0.        *
*                                                                           *
* --------------------------------------------------------------------------*/
/**
 * @file 	adaptation.h
 * @brief 	Adaptation is constructed before SPH body, but initialized in SPHBody constructor.
 * 			It defines the parameters for single or multi-resolution computations.
 * @author	Xiangyu Hu and Chi Zhang
 */

#ifndef PARTICLE_ADAPTATION_H
#define PARTICLE_ADAPTATION_H

#include "base_data_package.h"
#include "sph_data_containers.h"
#include "cell_linked_list.h"
#include "level_set.h"

namespace SPH
{

	class SPHBody;
	class Shape;
	class Kernel;
	class BaseParticles;
	class BaseLevelSet;

	/**
	 * @class SPHAdaptation
	 * @brief Base class for adaptation.
	 * The base class defines essential global parameteres. It is also used for single-resolution SPH method. 
	 * In the constructor parameter, system_resolution_ratio defines the relation between present resolution to the system reference resolution.
	 * The derived class will be used if further adaptation is introduced.
	 */
	class SPHAdaptation
	{
	private:
		UniquePtrKeeper<Kernel> kernel_ptr_keeper_;

	protected:
		SPHBody *sph_body_;
		Real h_spacing_ratio_;		   /**< ratio of reference kernel smoothing length to particle spacing */
		Real system_resolution_ratio_; /**< ratio of body resolution to system resolution, set to 1.0 by default */
		int local_refinement_level_;   /**< refinement level respect to reference particle spacing */
		Real spacing_ref_;			   /**< reference particle spacing used to determine local particle spacing */
		Real h_ref_;				   /**< reference particle spacing used to determine local particle smoothing length */
		Kernel *kernel_;
		Real spacing_min_;			   /**< minimum particle spacing determined by local refinement level */
		Real spacing_ratio_min_;
		Real h_ratio_max_;
		Real number_density_min_;
		Real number_density_max_;
		BoundingBox system_domain_bounds_;

	public:
		explicit SPHAdaptation(SPHBody *sph_body, Real h_spacing_ratio = 1.3, Real system_resolution_ratio = 1.0);
		virtual ~SPHAdaptation(){};

		int LocalRefinementLevel() { return local_refinement_level_; };
		Real ReferenceSpacing() { return spacing_ref_; };
		Real ReferenceSmoothingLength() { return h_ref_; };
		Kernel *getKernel() { return kernel_; };
		void resetAdapationRatios(Real h_spacing_ratio, Real system_resolution_ratio = 1.0);
		template <class KernelType, typename... ConstructorArgs>
		void resetKernel(ConstructorArgs &&...args)
		{
			kernel_ = kernel_ptr_keeper_.createPtr<KernelType>(h_ref_, std::forward<ConstructorArgs>(args)...);
		};
		Real MinimumSpacing() { return spacing_min_; };
		Real MinimumSpacingRatio() { return spacing_ratio_min_; };
		Real computeReferenceNumberDensity(Vec2d zero, Real h_ratio);
		Real computeReferenceNumberDensity(Vec3d zero, Real h_ratio);
		Real ReferenceNumberDensity();
		virtual Real SmoothingLengthRatio(size_t particle_index_i) { return 1.0; };

		virtual UniquePtr<BaseCellLinkedList> createCellLinkedList();
		virtual UniquePtr<BaseLevelSet> createLevelSet(Shape &shape, Real refinement_ratio);

	protected:
		Real RefinedSpacing(Real coarse_particle_spacing, int refinement_level);
	};

	/**
	 * @class ParticleWithLocalRefinement
	 * @brief Base class for particle with refinement.
	 */
	class ParticleWithLocalRefinement : public SPHAdaptation
	{
	public:
		StdLargeVec<Real> h_ratio_; /**< the ratio between reference smoothing length to variable smoothing length */

		ParticleWithLocalRefinement(SPHBody *sph_body, Real h_spacing_ratio_,
									Real system_resolution_ratio,
									int local_refinement_level);
		virtual ~ParticleWithLocalRefinement(){};

		size_t getCellLinkedListTotalLevel();
		size_t getLevelSetTotalLevel();
		virtual Real SmoothingLengthRatio(size_t particle_index_i) override
		{
			return h_ratio_[particle_index_i];
		};

		StdLargeVec<Real> &registerSmoothingLengthRatio(BaseParticles *base_particles);
		virtual UniquePtr<BaseCellLinkedList> createCellLinkedList() override;
		virtual UniquePtr<BaseLevelSet> createLevelSet(Shape &shape, Real refinement_ratio) override;
	};

	/**
	 * @class ParticleSpacingByBodyShape
	 * @brief Adaptive resolutions within a SPH body according to the distance to the body surface.
	 */
	class ParticleSpacingByBodyShape : public ParticleWithLocalRefinement
	{
	public:
		ParticleSpacingByBodyShape(SPHBody *sph_body, Real smoothing_length_ratio,
								   Real system_resolution_ratio,
								   int local_refinement_level);
		virtual ~ParticleSpacingByBodyShape(){};

		Real getLocalSpacing(Shape &shape, const Vecd &position);
	};
}
#endif //PARTICLE_ADAPTATION_H