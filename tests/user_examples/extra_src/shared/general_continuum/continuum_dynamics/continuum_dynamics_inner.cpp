#include "continuum_dynamics_inner.hpp"
namespace SPH
{
namespace continuum_dynamics
{
//=================================================================================================//
ContinuumInitialCondition::ContinuumInitialCondition(SPHBody &sph_body)
    : LocalDynamics(sph_body), PlasticContinuumDataSimple(sph_body),
      pos_(particles_->pos_), vel_(particles_->vel_), stress_tensor_3D_(particles_->stress_tensor_3D_) {}
//=================================================================================================//
ContinuumAcousticTimeStepSize::ContinuumAcousticTimeStepSize(SPHBody &sph_body, Real acousticCFL)
    : fluid_dynamics::AcousticTimeStepSize(sph_body, acousticCFL) {}
//=================================================================================================//
Real ContinuumAcousticTimeStepSize::reduce(size_t index_i, Real dt)
{
    return fluid_.getSoundSpeed(p_[index_i], rho_[index_i]) + vel_[index_i].norm();
}
//=================================================================================================//
Real ContinuumAcousticTimeStepSize::outputResult(Real reduced_value)
{
    return acousticCFL_ * smoothing_length_min_ / (fluid_.ReferenceSoundSpeed() + TinyReal);
}
//=================================================================================================//
ArtificialStressAcceleration::
    ArtificialStressAcceleration(BaseInnerRelation &inner_relation, Real epsilon, Real exponent)
    : LocalDynamics(inner_relation.getSPHBody()), ContinuumDataInner(inner_relation),
      smoothing_length_(sph_body_.sph_adaptation_->ReferenceSmoothingLength()),
      reference_spacing_(sph_body_.sph_adaptation_->ReferenceSpacing()),
      epsilon_(epsilon), exponent_(exponent),
      shear_stress_(*particles_->getVariableByName<Matd>("ShearStress")),
      p_(*particles_->getVariableByName<Real>("Pressure")),
      rho_(particles_->rho_), acc_prior_(particles_->acc_prior_)
{
    particles_->registerVariable(artificial_stress_, "ArtificialStress");
}
//=================================================================================================//
void ArtificialStressAcceleration::initialization(size_t index_i, Real dt)
{
    Matd full_stress = shear_stress_[index_i] - p_[index_i] * Matd::Identity();
    artificial_stress_[index_i] = getArtificialStress(full_stress, rho_[index_i]);
}
//=================================================================================================//
Matd ArtificialStressAcceleration::getArtificialStress(const Matd &stress_tensor_i, const Real &rho_i)
{
    Real sigma_xx = stress_tensor_i(0, 0);
    Real sigma_xy = stress_tensor_i(0, 1);
    Real sigma_yy = stress_tensor_i(1, 1);
    Real tiny_real(0);
    sigma_xx - sigma_yy > 0 ? tiny_real = TinyReal : tiny_real = -TinyReal;
    Real tan_sita_2 = 2 * sigma_xy / (sigma_xx - sigma_yy + tiny_real);
    Real sita_2 = atan(tan_sita_2);
    Real sita = sita_2 / 2.0;
    Real sigma_xx_dot = cos(sita) * cos(sita) * sigma_xx + 2 * cos(sita) * sin(sita) * sigma_xy + sin(sita) * sin(sita) * sigma_yy;
    Real sigma_yy_dot = sin(sita) * sin(sita) * sigma_xx + 2 * cos(sita) * sin(sita) * sigma_xy + cos(sita) * cos(sita) * sigma_yy;
    Real R_xx_dot = 0;
    Real R_yy_dot = 0;
    if (sigma_xx_dot > 0)
    {
        R_xx_dot = -epsilon_ * sigma_xx_dot / (rho_i * rho_i);
    }
    if (sigma_yy_dot > 0)
    {
        R_yy_dot = -epsilon_ * sigma_yy_dot / (rho_i * rho_i);
    }
    Matd R = Matd::Zero();
    R(0, 0) = R_xx_dot * cos(sita) * cos(sita) + R_yy_dot * sin(sita) * sin(sita);
    R(1, 1) = R_xx_dot * sin(sita) * sin(sita) + R_yy_dot * cos(sita) * cos(sita);
    R(0, 1) = (R_xx_dot - R_yy_dot) * cos(sita) * sin(sita);
    R(1, 0) = R(0, 1);
    return R;
}
//=================================================================================================//
void ArtificialStressAcceleration::interaction(size_t index_i, Real dt)
{
    Vecd acceleration = Vecd::Zero();
    Real rho_i = rho_[index_i];
    Matd stress_i = shear_stress_[index_i] - p_[index_i] * Matd::Identity();
    Neighborhood &inner_neighborhood = inner_configuration_[index_i];
    Real W_ini = sph_body_.sph_adaptation_->getKernel()->W_2D(reference_spacing_ / smoothing_length_);
    Matd R_i = getArtificialStress(stress_i, rho_i);
    for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
    {
        size_t index_j = inner_neighborhood.j_[n];
        Real r_ij = inner_neighborhood.r_ij_[n];
        Vecd nablaW_ijV_j = inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.e_ij_[n];

        Real W_ij = sph_body_.sph_adaptation_->getKernel()->W_2D(r_ij / smoothing_length_);
        Real f_ij = W_ij / W_ini;
        Matd stress_j = shear_stress_[index_j] - p_[index_j] * Matd::Identity();
        Matd R_j = getArtificialStress(stress_j, rho_[index_j]);
        Matd repulsive_force = pow(f_ij, exponent_) * (R_i + R_j);

        acceleration += rho_[index_j] * repulsive_force * nablaW_ijV_j;
    }
    acc_prior_[index_i] += acceleration;
}
//=================================================================================================//
ShearStressIntegration::ShearStressIntegration(BaseInnerRelation &inner_relation)
    : BaseShearStressIntegration<ContinuumDataInner>(inner_relation),
      continuum_(particles_->continuum_){};
//=================================================================================================//
void ShearStressIntegration::update(size_t index_i, Real dt)
{
    Matd shear_stress_rate = continuum_.ConstitutiveRelationShearStress(velocity_gradient_[index_i], shear_stress_[index_i]);
    shear_stress_[index_i] += shear_stress_rate * dt;
    Matd full_stress = shear_stress_[index_i] + p_[index_i] * Matd::Identity();
    von_mises_stress_[index_i] = getVonMisesStressFromMatrix(full_stress);
}
//=================================================================================================//
PlasticShearStressIntegration::PlasticShearStressIntegration(BaseInnerRelation &inner_relation)
    : BaseShearStressIntegration<PlasticContinuumDataInner>(inner_relation),
      plastic_continuum_(particles_->plastic_continuum_),
      stress_tensor_3D_(particles_->stress_tensor_3D_), strain_tensor_3D_(particles_->strain_tensor_3D_),
      stress_rate_3D_(particles_->stress_rate_3D_), strain_rate_3D_(particles_->strain_rate_3D_),
      elastic_strain_tensor_3D_(particles_->elastic_strain_tensor_3D_),
      elastic_strain_rate_3D_(particles_->elastic_strain_rate_3D_),
      E_(plastic_continuum_.getYoungsModulus()), nu_(plastic_continuum_.getPoissonRatio()),
      shear_stress_(*particles_->getVariableByName<Matd>("ShearStress")) {}
//=================================================================================================//
void PlasticShearStressIntegration::update(size_t index_i, Real dt)
{
    Mat3d velocity_gradient = upgradeToMat3d(velocity_gradient_[index_i]);
    stress_rate_3D_[index_i] += plastic_continuum_.ConstitutiveRelation(velocity_gradient, stress_tensor_3D_[index_i]);
    stress_tensor_3D_[index_i] += stress_rate_3D_[index_i] * dt;
    stress_tensor_3D_[index_i] = plastic_continuum_.ReturnMapping(stress_tensor_3D_[index_i]);
    strain_rate_3D_[index_i] = 0.5 * (velocity_gradient + velocity_gradient.transpose());
    strain_tensor_3D_[index_i] += strain_rate_3D_[index_i] * dt * 0.5;
    shear_stress_[index_i] = degradeToMatd(strain_tensor_3D_[index_i]);
    // calculate elastic strain for output visualization
    Mat3d deviatoric_stress = stress_tensor_3D_[index_i] - (1 / 3) * stress_tensor_3D_[index_i].trace() * Mat3d::Identity();
    Real hydrostatic_pressure = (1 / 3) * stress_tensor_3D_[index_i].trace();
    elastic_strain_tensor_3D_[index_i] = deviatoric_stress / (2 * plastic_continuum_.getShearModulus(E_, nu_)) +
                                         hydrostatic_pressure * Mat3d::Identity() / (9 * plastic_continuum_.getBulkModulus(E_, nu_));
}
//=================================================================================================//
ShearStressAcceleration::ShearStressAcceleration(BaseInnerRelation &inner_relation)
    : LocalDynamics(inner_relation.getSPHBody()), ContinuumDataInner(inner_relation),
      shear_stress_(*particles_->getVariableByName<Matd>("ShearStress")),
      rho_(particles_->rho_), acc_prior_(particles_->acc_prior_) {}
//=================================================================================================//
void ShearStressAcceleration::interaction(size_t index_i, Real dt)
{
    Real rho_i = rho_[index_i];
    Matd shear_stress_i = shear_stress_[index_i];
    Vecd acceleration = Vecd::Zero();
    Neighborhood &inner_neighborhood = inner_configuration_[index_i];
    for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
    {
        size_t index_j = inner_neighborhood.j_[n];
        Vecd nablaW_ijV_j = inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.e_ij_[n];
        acceleration += rho_[index_j] * (shear_stress_i / (rho_i * rho_i) + shear_stress_[index_j] / (rho_[index_j] * rho_[index_j])) * nablaW_ijV_j;
    }
    acc_prior_[index_i] += acceleration;
}
//=================================================================================================//
ShearAccelerationIntegration::ShearAccelerationIntegration(BaseInnerRelation &inner_relation)
    : LocalDynamics(inner_relation.getSPHBody()), ContinuumDataInner(inner_relation),
      continuum_(particles_->continuum_),
      G_(continuum_.getShearModulus(continuum_.getYoungsModulus(), continuum_.getPoissonRatio())),
      smoothing_length_(sph_body_.sph_adaptation_->ReferenceSmoothingLength()),
      vel_(particles_->vel_), acc_prior_(particles_->acc_prior_), rho_(particles_->rho_)
{
    particles_->registerVariable(acc_shear_, "AccumulatedShearAcceleration");
}
//=================================================================================================//
void ShearAccelerationIntegration::interaction(size_t index_i, Real dt)
{
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
    acc_shear_[index_i] += G_ * acceleration * dt / rho_[index_i];
    acc_prior_[index_i] += acc_shear_[index_i];
}
//=================================================================================================//
FixedInAxisDirection::FixedInAxisDirection(BodyPartByParticle &body_part, Vecd constrained_axises)
    : BaseMotionConstraint<BodyPartByParticle>(body_part), constrain_matrix_(Matd::Identity())
{
    for (int k = 0; k != Dimensions; ++k)
        constrain_matrix_(k, k) = constrained_axises[k];
}
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
    ReduceDynamics<QuantitySummation<Real>> compute_total_mass_(sph_body, "MassiveMeasure");
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
      plastic_continuum_(particles_->plastic_continuum_), rho_(particles_->rho_),
      p_(*particles_->getVariableByName<Real>("Pressure")),
      drho_dt_(*particles_->registerSharedVariable<Real>("rho0_ChangeRate")),
      pos_(particles_->pos_), vel_(particles_->vel_),
      acc_(particles_->acc_), acc_prior_(particles_->acc_prior_),
      stress_tensor_3D_(particles_->stress_tensor_3D_), strain_tensor_3D_(particles_->strain_tensor_3D_),
      stress_rate_3D_(particles_->stress_rate_3D_), strain_rate_3D_(particles_->strain_rate_3D_),
      elastic_strain_tensor_3D_(particles_->elastic_strain_tensor_3D_),
      elastic_strain_rate_3D_(particles_->elastic_strain_rate_3D_) {}
//=================================================================================================//
StressDiffusion::
    StressDiffusion(BaseInnerRelation &inner_relation, SharedPtr<Gravity> gravity__ptr, int axis)
    : BaseRelaxationPlastic(inner_relation), axis_(axis),
      rho0_(plastic_continuum_.ReferenceDensity()),
      gravity_(gravity__ptr->InducedAcceleration()[axis]),
      smoothing_length_(sph_body_.sph_adaptation_->ReferenceSmoothingLength()),
      phi_(plastic_continuum_.getFrictionAngle()),
      diffusion_coeff_(zeta_ * smoothing_length_ * plastic_continuum_.ReferenceSoundSpeed()) {}
//=================================================================================================//
void StressDiffusion::interaction(size_t index_i, Real dt)
{
    Mat3d diffusion_stress_rate_ = Mat3d::Zero();
    Neighborhood &inner_neighborhood = inner_configuration_[index_i];
    for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
    {
        size_t index_j = inner_neighborhood.j_[n];
        Real r_ij = inner_neighborhood.r_ij_[n];
        Real dW_ijV_j = inner_neighborhood.dW_ijV_j_[n];
        Real y_ij = (pos_[index_i] - pos_[index_j])[axis_];
        Mat3d difference = stress_tensor_3D_[index_i] - stress_tensor_3D_[index_j];
        difference(0, 0) -= (1 - sin(phi_)) * rho0_ * gravity_ * y_ij;
        difference(1, 1) -= rho0_ * gravity_ * y_ij;
        difference(2, 2) -= (1 - sin(phi_)) * rho0_ * gravity_ * y_ij;
        diffusion_stress_rate_ += 2.0 * diffusion_coeff_ * difference * dW_ijV_j /
                                  (r_ij + 0.01 * smoothing_length_);
    }
    stress_rate_3D_[index_i] = diffusion_stress_rate_;
}
//=================================================================================================//
} // namespace continuum_dynamics
} // namespace SPH
