#include "continuum_dynamics_inner.hpp"
namespace SPH
{
//=================================================================================================//
namespace continuum_dynamics
{
ContinuumInitialCondition::ContinuumInitialCondition(SPHBody &sph_body)
    : LocalDynamics(sph_body), PlasticContinuumDataSimple(sph_body),
      pos_(particles_->pos_), vel_(particles_->vel_), stress_tensor_3D_(particles_->stress_tensor_3D_) {}
//=================================================================================================//
BaseRelaxation::BaseRelaxation(BaseInnerRelation &inner_relation)
    : LocalDynamics(inner_relation.getSPHBody()), ContinuumDataInner(inner_relation),
      continuum_(particles_->continuum_), rho_(particles_->rho_),
      p_(*particles_->getVariableByName<Real>("Pressure")), drho_dt_(*particles_->getVariableByName<Real>("DensityChangeRate")),
      pos_(particles_->pos_), vel_(particles_->vel_),
      force_(particles_->force_), force_prior_(particles_->force_prior_) {}
//=================================================================================================//
ShearAccelerationRelaxation::ShearAccelerationRelaxation(BaseInnerRelation &inner_relation)
    : BaseRelaxation(inner_relation),
      G_(continuum_.getShearModulus(continuum_.getYoungsModulus(), continuum_.getPoissonRatio())),
      smoothing_length_(sph_body_.sph_adaptation_->ReferenceSmoothingLength()), shear_stress_(particles_->shear_stress_),
      acc_shear_(particles_->acc_shear_) {}
//=================================================================================================//
void ShearAccelerationRelaxation::interaction(size_t index_i, Real dt)
{
    Real rho_i = rho_[index_i];
    Vecd acceleration = Vecd::Zero();
    Neighborhood &inner_neighborhood = inner_configuration_[index_i];
    for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
    {
        size_t index_j = inner_neighborhood.j_[n];
        Real r_ij = inner_neighborhood.r_ij_[n];
        Real dW_ijV_j = inner_neighborhood.dW_ijV_j_[n];
        Vecd &e_ij = inner_neighborhood.e_ij_[n];
        Real eta_ij = 2 * (0.7 * (Real)Dimensions + 2.1) * (vel_[index_i] - vel_[index_j]).dot(e_ij) / (r_ij + TinyReal);
        acceleration += eta_ij * dW_ijV_j * e_ij;
    }
    acc_shear_[index_i] += G_ * acceleration * dt / rho_i;
}
//=================================================================================================//
ShearStressRelaxation ::
    ShearStressRelaxation(BaseInnerRelation &inner_relation)
    : BaseRelaxation(inner_relation),
      shear_stress_(particles_->shear_stress_), shear_stress_rate_(particles_->shear_stress_rate_),
      velocity_gradient_(particles_->velocity_gradient_), strain_tensor_(particles_->strain_tensor_),
      strain_tensor_rate_(particles_->strain_tensor_rate_), von_mises_stress_(particles_->von_mises_stress_),
      von_mises_strain_(particles_->von_mises_strain_), Vol_(particles_->Vol_),
      B_(*particles_->getVariableByName<Matd>("KernelCorrectionMatrix")) {}
void ShearStressRelaxation::initialization(size_t index_i, Real dt)
{
    strain_tensor_[index_i] += strain_tensor_rate_[index_i] * 0.5 * dt;
    shear_stress_[index_i] += shear_stress_rate_[index_i] * 0.5 * dt;
}
void ShearStressRelaxation::interaction(size_t index_i, Real dt)
{
    Matd velocity_gradient = Matd::Zero();
    Neighborhood &inner_neighborhood = inner_configuration_[index_i];
    for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
    {
        size_t index_j = inner_neighborhood.j_[n];
        Real dW_ijV_j_ = inner_neighborhood.dW_ijV_j_[n];
        Vecd& e_ij = inner_neighborhood.e_ij_[n];
        Vecd v_ij = vel_[index_i] - vel_[index_j];
        velocity_gradient -= v_ij * (B_[index_i] * e_ij * dW_ijV_j_).transpose();
    }
    velocity_gradient_[index_i] = velocity_gradient;
    // calculate strain
    Matd strain_rate = 0.5 * (velocity_gradient + velocity_gradient.transpose());
    strain_tensor_rate_[index_i] = strain_rate;
    strain_tensor_[index_i] += strain_tensor_rate_[index_i] * 0.5 * dt;
    Matd strain_i = strain_tensor_[index_i];
    von_mises_strain_[index_i] = getVonMisesStressFromMatrix(strain_i);
}
void ShearStressRelaxation::update(size_t index_i, Real dt)
{
    shear_stress_rate_[index_i] = continuum_.ConstitutiveRelationShearStress(velocity_gradient_[index_i], shear_stress_[index_i]);
    shear_stress_[index_i] += shear_stress_rate_[index_i] * dt * 0.5;
    Matd stress_tensor_i = shear_stress_[index_i] - p_[index_i] * Matd::Identity();
    von_mises_stress_[index_i] = getVonMisesStressFromMatrix(stress_tensor_i);
}
//=================================================================================================//
FixedInAxisDirection::FixedInAxisDirection(BodyPartByParticle &body_part, Vecd constrained_axises)
    : BaseMotionConstraint<BodyPartByParticle>(body_part), constrain_matrix_(Matd::Identity())
{
    for (int k = 0; k != Dimensions; ++k)
        constrain_matrix_(k, k) = constrained_axises[k];
};
//=================================================================================================//
void FixedInAxisDirection::update(size_t index_i, Real dt)
{
    vel_[index_i] = constrain_matrix_ * vel_[index_i];
};
//=================================================================================================//
ConstrainSolidBodyMassCenter::
    ConstrainSolidBodyMassCenter(SPHBody &sph_body, Vecd constrain_direction)
    : LocalDynamics(sph_body), ContinuumDataSimple(sph_body),
      correction_matrix_(Matd::Identity()), vel_(particles_->vel_),
      compute_total_momentum_(sph_body, "Velocity")
{
    for (int i = 0; i != Dimensions; ++i)
        correction_matrix_(i, i) = constrain_direction[i];
    ReduceDynamics<QuantitySummation<Real>> compute_total_mass_(sph_body, "Mass");
    total_mass_ = compute_total_mass_.exec();
}
//=================================================================================================//
void ConstrainSolidBodyMassCenter::setupDynamics(Real dt)
{
    velocity_correction_ =
        correction_matrix_ * compute_total_momentum_.exec(dt) / total_mass_;
}
//=================================================================================================//
void ConstrainSolidBodyMassCenter::update(size_t index_i, Real dt)
{
    vel_[index_i] -= velocity_correction_;
}
//=================================================================================================//
BaseRelaxationPlastic::BaseRelaxationPlastic(BaseInnerRelation &inner_relation)
    : LocalDynamics(inner_relation.getSPHBody()), PlasticContinuumDataInner(inner_relation),
      plastic_continuum_(particles_->plastic_continuum_), rho_(particles_->rho_), mass_(particles_->mass_),
      p_(*particles_->getVariableByName<Real>("Pressure")), drho_dt_(*particles_->registerSharedVariable<Real>("DensityChangeRate")), pos_(particles_->pos_),
      vel_(particles_->vel_), force_(particles_->force_), force_prior_(particles_->force_prior_),
      stress_tensor_3D_(particles_->stress_tensor_3D_), strain_tensor_3D_(particles_->strain_tensor_3D_),
      stress_rate_3D_(particles_->stress_rate_3D_), strain_rate_3D_(particles_->strain_rate_3D_),
      elastic_strain_tensor_3D_(particles_->elastic_strain_tensor_3D_), elastic_strain_rate_3D_(particles_->elastic_strain_rate_3D_) {}
Matd BaseRelaxationPlastic::reduceTensor(Mat3d tensor_3d)
{
    Matd tensor_2d;
    for (int i = 0; i < (Real)Dimensions; i++)
    {
        for (int j = 0; j < (Real)Dimensions; j++)
        {
            tensor_2d(i, j) = tensor_3d(i, j);
        }
    }
    return tensor_2d;
}
Mat3d BaseRelaxationPlastic::increaseTensor(Matd tensor_2d)
{
    Mat3d tensor_3d = Mat3d::Zero();
    for (int i = 0; i < (Real)Dimensions; i++)
    {
        for (int j = 0; j < (Real)Dimensions; j++)
        {
            tensor_3d(i, j) = tensor_2d(i, j);
        }
    }
    return tensor_3d;
}
//====================================================================================//
StressDiffusion::StressDiffusion(BaseInnerRelation &inner_relation)
    : BaseRelaxationPlastic(inner_relation), fai_(DynamicCast<PlasticContinuum>(this, plastic_continuum_).getFrictionAngle()), smoothing_length_(sph_body_.sph_adaptation_->ReferenceSmoothingLength()),
      sound_speed_(plastic_continuum_.ReferenceSoundSpeed()) {}
void StressDiffusion::interaction(size_t index_i, Real dt)
{
    Vecd acc_prior_i = force_prior_[index_i] / mass_[index_i];
    Real gravity = abs(acc_prior_i(1, 0));
    Real density = plastic_continuum_.getDensity();
    Mat3d diffusion_stress_rate_ = Mat3d::Zero();
    Mat3d diffusion_stress_ = Mat3d::Zero();
    Neighborhood &inner_neighborhood = inner_configuration_[index_i];
    for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
    {
        size_t index_j = inner_neighborhood.j_[n];
        Real r_ij = inner_neighborhood.r_ij_[n];
        Real dW_ijV_j = inner_neighborhood.dW_ijV_j_[n];
        Real y_ij = pos_[index_i](1, 0) - pos_[index_j](1, 0);
        diffusion_stress_ = stress_tensor_3D_[index_i] - stress_tensor_3D_[index_j];
        diffusion_stress_(0, 0) = diffusion_stress_(0, 0) - (1 - sin(fai_)) * density * gravity * y_ij;
        diffusion_stress_(1, 1) = diffusion_stress_(1, 1) - density * gravity * y_ij;
        diffusion_stress_(2, 2) = diffusion_stress_(2, 2) - (1 - sin(fai_)) * density * gravity * y_ij;
        diffusion_stress_rate_ += 2 * zeta_ * smoothing_length_ * sound_speed_ * diffusion_stress_ * r_ij * dW_ijV_j / (r_ij * r_ij + 0.01 * smoothing_length_);
    }
    stress_rate_3D_[index_i] = diffusion_stress_rate_;
}
} // namespace continuum_dynamics
} // namespace SPH
