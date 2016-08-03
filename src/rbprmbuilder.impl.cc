// Copyright (c) 2012 CNRS
// Author: Florent Lamiraux, Joseph Mirabel
//
// This file is part of hpp-manipulation-corba.
// hpp-manipulation-corba is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-manipulation-corba is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-manipulation-corba.  If not, see
// <http://www.gnu.org/licenses/>.

//#include <hpp/fcl/math/transform.h>
#include <hpp/util/debug.hh>
#include "hpp/corbaserver/rbprm/rbprmbuilder.hh"
#include "rbprmbuilder.impl.hh"
#include "hpp/rbprm/rbprm-device.hh"
#include "hpp/rbprm/rbprm-validation.hh"
#include "hpp/rbprm/interpolation/rbprm-path-interpolation.hh"
#include "hpp/rbprm/interpolation/limb-rrt-helper.hh"
#include "hpp/rbprm/interpolation/limb-rrt-path.hh"
#include "hpp/rbprm/stability/stability.hh"
#include "hpp/rbprm/sampling/sample-db.hh"
#include "hpp/model/urdf/util.hh"
#include <fstream>



namespace hpp {
  namespace rbprm {
    namespace impl {

    RbprmBuilder::RbprmBuilder ()
    : POA_hpp::corbaserver::rbprm::RbprmBuilder()
    , romLoaded_(false)
    , fullBodyLoaded_(false)
    , bindShooter_()
    , analysisFactory_(0)
    {
        // NOTHING
    }

    void RbprmBuilder::loadRobotRomModel(const char* robotName,
         const char* rootJointType,
         const char* packageName,
         const char* modelName,
         const char* urdfSuffix,
         const char* srdfSuffix) throw (hpp::Error)
    {
        try
        {
            hpp::model::DevicePtr_t romDevice = model::Device::create (robotName);
            romDevices_.insert(std::make_pair(robotName, romDevice));
            hpp::model::urdf::loadRobotModel (romDevice,
                    std::string (rootJointType),
                    std::string (packageName),
                    std::string (modelName),
                    std::string (urdfSuffix),
                    std::string (srdfSuffix));
        }
        catch (const std::exception& exc)
        {
            hppDout (error, exc.what ());
            throw hpp::Error (exc.what ());
        }
        romLoaded_ = true;
    }

    void RbprmBuilder::loadRobotCompleteModel(const char* robotName,
         const char* rootJointType,
         const char* packageName,
         const char* modelName,
         const char* urdfSuffix,
         const char* srdfSuffix) throw (hpp::Error)
    {
        if(!romLoaded_)
        {
            std::string err("Rom must be loaded before loading complete model") ;
            hppDout (error, err );
            throw hpp::Error(err.c_str());
        }
        try
        {
            hpp::model::RbPrmDevicePtr_t device = hpp::model::RbPrmDevice::create (robotName, romDevices_);
            hpp::model::urdf::loadRobotModel (device,
                    std::string (rootJointType),
                    std::string (packageName),
                    std::string (modelName),
                    std::string (urdfSuffix),
                    std::string (srdfSuffix));
            // Add device to the planner
            problemSolver_->robot (device);
            problemSolver_->robot ()->controlComputation
            (model::Device::JOINT_POSITION);
        }
        catch (const std::exception& exc)
        {
            hppDout (error, exc.what ());
            throw hpp::Error (exc.what ());
        }
    }

    void RbprmBuilder::loadFullBodyRobot(const char* robotName,
         const char* rootJointType,
         const char* packageName,
         const char* modelName,
         const char* urdfSuffix,
         const char* srdfSuffix) throw (hpp::Error)
    {
        try
        {
            model::DevicePtr_t device = model::Device::create (robotName);
            hpp::model::urdf::loadRobotModel (device,
                    std::string (rootJointType),
                    std::string (packageName),
                    std::string (modelName),
                    std::string (urdfSuffix),
                    std::string (srdfSuffix));
            fullBody_ = rbprm::RbPrmFullBody::create(device);
            problemSolver_->pathValidationType ("Discretized",0.05); // reset to avoid conflict with rbprm path
            problemSolver_->robot (fullBody_->device_);
            problemSolver_->resetProblem();
            problemSolver_->robot ()->controlComputation
            (model::Device::JOINT_POSITION);
        }
        catch (const std::exception& exc)
        {
            hppDout (error, exc.what ());
            throw hpp::Error (exc.what ());
        }
        fullBodyLoaded_ = true;
        analysisFactory_ = new sampling::AnalysisFactory(fullBody_);
    }

    hpp::floatSeq* RbprmBuilder::getSampleConfig(const char* limb, unsigned short sampleId) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        const T_Limb& limbs = fullBody_->GetLimbs();
        T_Limb::const_iterator lit = limbs.find(std::string(limb));
        if(lit == limbs.end())
        {
            std::string err("No limb " + std::string(limb) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        const RbPrmLimbPtr_t& limbPtr = lit->second;
        hpp::floatSeq *dofArray;
        Eigen::VectorXd config = fullBody_->device_->currentConfiguration ();
        if(sampleId > limbPtr->sampleContainer_.samples_.size())
        {
            std::string err("Limb " + std::string(limb) + "does not have samples.");
            throw Error (err.c_str());
        }
        const sampling::Sample& sample = limbPtr->sampleContainer_.samples_[sampleId];
        config.segment(sample.startRank_, sample.length_) = sample.configuration_;
        dofArray = new hpp::floatSeq();
        dofArray->length(_CORBA_ULong(config.rows()));
        for(std::size_t i=0; i< _CORBA_ULong(config.rows()); i++)
          (*dofArray)[(_CORBA_ULong)i] = config [i];
        return dofArray;
    }


    hpp::floatSeq* RbprmBuilder::getSamplePosition(const char* limb, unsigned short sampleId) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        const T_Limb& limbs = fullBody_->GetLimbs();
        T_Limb::const_iterator lit = limbs.find(std::string(limb));
        if(lit == limbs.end())
        {
            std::string err("No limb " + std::string(limb) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        const RbPrmLimbPtr_t& limbPtr = lit->second;
        hpp::floatSeq *dofArray;
        if(sampleId > limbPtr->sampleContainer_.samples_.size())
        {
            std::string err("Limb " + std::string(limb) + "does not have samples.");
            throw Error (err.c_str());
        }
        const sampling::Sample& sample = limbPtr->sampleContainer_.samples_[sampleId];
        const fcl::Vec3f& position = sample.effectorPosition_;
        dofArray = new hpp::floatSeq();
        dofArray->length(_CORBA_ULong(3));
        for(std::size_t i=0; i< 3; i++)
          (*dofArray)[(_CORBA_ULong)i] = position [i];
        return dofArray;
    }


    CORBA::UShort RbprmBuilder::getNumSamples(const char* limb) throw (hpp::Error)
    {
        const T_Limb& limbs = fullBody_->GetLimbs();
        T_Limb::const_iterator lit = limbs.find(std::string(limb));
        if(lit == limbs.end())
        {
            std::string err("No limb " + std::string(limb) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        return (CORBA::UShort)(lit->second->sampleContainer_.samples_.size());
    }

    floatSeq *RbprmBuilder::getOctreeNodeIds(const char* limb) throw (hpp::Error)
    {
        const T_Limb& limbs = fullBody_->GetLimbs();
        T_Limb::const_iterator lit = limbs.find(std::string(limb));
        if(lit == limbs.end())
        {
            std::string err("No limb " + std::string(limb) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        const sampling::T_VoxelSampleId& ids =  lit->second->sampleContainer_.samplesInVoxels_;
        hpp::floatSeq* dofArray = new hpp::floatSeq();
        dofArray->length((_CORBA_ULong)ids.size());
        sampling::T_VoxelSampleId::const_iterator it = ids.begin();
        for(std::size_t i=0; i< _CORBA_ULong(ids.size()); ++i, ++it)
        {
          (*dofArray)[(_CORBA_ULong)i] = (double)it->first;
        }
        return dofArray;
    }

    double RbprmBuilder::getSampleValue(const char* limb, const char* valueName, unsigned short sampleId) throw (hpp::Error)
    {
        const T_Limb& limbs = fullBody_->GetLimbs();
        T_Limb::const_iterator lit = limbs.find(std::string(limb));
        if(lit == limbs.end())
        {
            std::string err("No limb " + std::string(limb) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        const sampling::SampleDB& database = lit->second->sampleContainer_;
        if (database.samples_.size() <= sampleId)
        {
            std::string err("unexisting sample id " + sampleId);
            throw Error (err.c_str());
        }
        sampling::T_Values::const_iterator cit = database.values_.find(std::string(valueName));
        if(cit == database.values_.end())
        {
            std::string err("value not existing in database " + std::string(valueName));
            throw Error (err.c_str());
        }
        return cit->second[sampleId];
    }

    model::Configuration_t dofArrayToConfig (const model::DevicePtr_t& robot,
      const hpp::floatSeq& dofArray)
    {
        std::size_t configDim = (std::size_t)dofArray.length();
        // Get robot
        if (!robot) {
            throw hpp::Error ("No robot in problem solver.");
        }
        std::size_t deviceDim = robot->configSize ();
        // Fill dof vector with dof array.
        model::Configuration_t config; config.resize (configDim);
        for (std::size_t iDof = 0; iDof < configDim; iDof++) {
            config [iDof] = (double)dofArray[(_CORBA_ULong)iDof];
        }
        // fill the vector by zero
        hppDout (info, "config dimension: " <<configDim
           <<",  deviceDim "<<deviceDim);
        if(configDim != deviceDim){
            throw hpp::Error ("dofVector Does not match");
        }
        return config;
    }

    std::vector<model::Configuration_t> doubleDofArrayToConfig (const model::DevicePtr_t& robot,
      const hpp::floatSeqSeq& doubleDofArray)
    {
        std::size_t configsDim = (std::size_t)doubleDofArray.length();
        std::vector<model::Configuration_t> res;
        for (_CORBA_ULong iConfig = 0; iConfig < configsDim; iConfig++)
        {
            res.push_back(dofArrayToConfig(robot, doubleDofArray[iConfig]));
        }
        return res;
    }

    std::vector<std::string> stringConversion(const hpp::Names_t& dofArray)
    {
        std::vector<std::string> res;
        std::size_t dim = (std::size_t)dofArray.length();
        for (_CORBA_ULong iDof = 0; iDof < dim; iDof++)
        {
            res.push_back(std::string(dofArray[iDof]));
        }
        return res;
    }

    std::vector<double> doubleConversion(const hpp::floatSeq& dofArray)
    {
        std::vector<double> res;
        std::size_t dim = (std::size_t)dofArray.length();
        for (_CORBA_ULong iDof = 0; iDof < dim; iDof++)
        {
            res.push_back(dofArray[iDof]);
        }
        return res;
    }


    void RbprmBuilder::setFilter(const hpp::Names_t& roms) throw (hpp::Error)
    {
        bindShooter_.romFilter_ = stringConversion(roms);
    }


    void RbprmBuilder::setAffordanceFilter(const char* romName, const hpp::Names_t& affordances) throw (hpp::Error)
    {
        std::string name (romName);
				std::vector<std::string> affNames = stringConversion(affordances);
        bindShooter_.affFilter_.erase(name);
        bindShooter_.affFilter_.insert(std::make_pair(name,affNames));
    }

    void RbprmBuilder::boundSO3(const hpp::floatSeq& limitszyx) throw (hpp::Error)
    {
        std::vector<double> limits = doubleConversion(limitszyx);
        if(limits.size() !=6)
        {
            throw Error ("Can not bound SO3, array of 6 double required");
        }
        bindShooter_.so3Bounds_ = limits;

    }

    hpp::floatSeq* RbprmBuilder::generateContacts(const hpp::floatSeq& configuration,
			const hpp::floatSeq& direction) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        try
        {
            fcl::Vec3f dir;
            for(std::size_t i =0; i <3; ++i)
            {
                dir[i] = direction[(_CORBA_ULong)i];
            }
						const affMap_t &affMap = problemSolver_->map
							<std::vector<boost::shared_ptr<model::CollisionObject> > > ();
		        if (affMap.empty ()) {
    	        throw hpp::Error ("No affordances found. Unable to generate Contacts.");
      		  }
            model::Configuration_t config = dofArrayToConfig (fullBody_->device_, configuration);
            rbprm::State state = rbprm::ComputeContacts(fullBody_,config,
							affMap, bindShooter_.affFilter_, dir);
            hpp::floatSeq* dofArray = new hpp::floatSeq();
            dofArray->length(_CORBA_ULong(state.configuration_.rows()));
            for(std::size_t i=0; i< _CORBA_ULong(config.rows()); i++)
              (*dofArray)[(_CORBA_ULong)i] = state.configuration_ [i];
            return dofArray;
        } catch (const std::exception& exc) {
        throw hpp::Error (exc.what ());
        }
    }

    hpp::floatSeq* RbprmBuilder::getContactSamplesIds(const char* limbname,
                                        const hpp::floatSeq& configuration,
                                        const hpp::floatSeq& direction) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        try
        {
            fcl::Vec3f dir;
            for(std::size_t i =0; i <3; ++i)
            {
                dir[i] = direction[(_CORBA_ULong)i];
            }
            model::Configuration_t config = dofArrayToConfig (fullBody_->device_, configuration);
            model::Configuration_t save = fullBody_->device_->currentConfiguration();
            fullBody_->device_->currentConfiguration(config);

            sampling::T_OctreeReport finalSet;
            rbprm::T_Limb::const_iterator lit = fullBody_->GetLimbs().find(std::string(limbname));
            if(lit == fullBody_->GetLimbs().end())
            {
                throw std::runtime_error ("Impossible to find limb for joint "
                                          + std::string(limbname) + " to robot; limb not defined.");
            }
            const RbPrmLimbPtr_t& limb = lit->second;
            fcl::Transform3f transform = limb->limb_->robot()->rootJoint()->childJoint(0)->currentTransformation (); // get root transform from configuration
						// TODO fix as in rbprm-fullbody.cc!!
            std::vector<sampling::T_OctreeReport> reports(problemSolver_->collisionObstacles().size());
            std::size_t i (0);
            //#pragma omp parallel for
            for(model::ObjectVector_t::const_iterator oit = problemSolver_->collisionObstacles().begin();
                oit != problemSolver_->collisionObstacles().end(); ++oit, ++i)
            {
                sampling::GetCandidates(limb->sampleContainer_, transform, *oit, dir, reports[i]);
            }
            for(std::vector<sampling::T_OctreeReport>::const_iterator cit = reports.begin();
                cit != reports.end(); ++cit)
            {
                finalSet.insert(cit->begin(), cit->end());
            }
            hpp::floatSeq* dofArray = new hpp::floatSeq();
            dofArray->length((_CORBA_ULong)finalSet.size());
            sampling::T_OctreeReport::const_iterator candCit = finalSet.begin();
            for(std::size_t i=0; i< _CORBA_ULong(finalSet.size()); ++i, ++candCit)
            {
              (*dofArray)[(_CORBA_ULong)i] = (double)candCit->sample_->id_;
            }
            fullBody_->device_->currentConfiguration(save);
            return dofArray;
        } catch (const std::exception& exc) {
        throw hpp::Error (exc.what ());
        }
    }

    hpp::floatSeq* RbprmBuilder::getSamplesIdsInOctreeNode(const char* limb,
                                                           double octreeNodeId) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        try
        {
            long ocId ((long)octreeNodeId);
            const T_Limb& limbs = fullBody_->GetLimbs();
            T_Limb::const_iterator lit = limbs.find(std::string(limb));
            if(lit == limbs.end())
            {
                std::string err("No limb " + std::string(limb) + "was defined for robot" + fullBody_->device_->name());
                throw Error (err.c_str());
            }
            const sampling::T_VoxelSampleId& sampleIds =  lit->second->sampleContainer_.samplesInVoxels_;
            sampling::T_VoxelSampleId::const_iterator cit = sampleIds.find(ocId);
            if(cit == sampleIds.end())
            {
                std::stringstream ss; ss << ocId;
                std::string err("No octree node with id " + ss.str() + "was defined for robot" + fullBody_->device_->name());
                throw Error (err.c_str());
            }
            const sampling::VoxelSampleId& ids = cit->second;
            hpp::floatSeq* dofArray = new hpp::floatSeq();
            dofArray->length((_CORBA_ULong)ids.second);
            std::size_t sampleId = ids.first;
            for(std::size_t i=0; i< _CORBA_ULong(ids.second); ++i, ++sampleId)
            {
              (*dofArray)[(_CORBA_ULong)i] = (double)sampleId;
            }
            return dofArray;
        } catch (const std::exception& exc) {
        throw hpp::Error (exc.what ());
        }
    }

    void RbprmBuilder::addLimb(const char* id, const char* limb, const char* effector, const hpp::floatSeq& offset, const hpp::floatSeq& normal, double x, double y,
                               unsigned short samples, const char* heuristicName, double resolution, const char *contactType, double disableEffectorCollision) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        try
        {
            fcl::Vec3f off, norm;
            for(std::size_t i =0; i <3; ++i)
            {
                off[i] = offset[(_CORBA_ULong)i];
                norm[i] = normal[(_CORBA_ULong)i];
            }
            ContactType cType = hpp::rbprm::_6_DOF;
            if(std::string(contactType) == "_3_DOF")
            {
                cType = hpp::rbprm::_3_DOF;
            }
            fullBody_->AddLimb(std::string(id), std::string(limb), std::string(effector), off, norm, x, y,
                               problemSolver_->collisionObstacles(), samples,heuristicName,resolution,cType,disableEffectorCollision > 0.5);
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }


    void RbprmBuilder::addLimbDatabase(const char* databasePath, const char* id, const char* heuristicName, double loadValues, double disableEffectorCollision) throw (hpp::Error)
    {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        try
        {
            std::string fileName(databasePath);
            fullBody_->AddLimb(fileName, std::string(id), problemSolver_->collisionObstacles(), heuristicName, loadValues > 0.5,
                               disableEffectorCollision > 0.5);
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    void SetPositionAndNormal(rbprm::State& state,
			hpp::rbprm::RbPrmFullBodyPtr_t fullBody, const hpp::floatSeq& configuration,
			const hpp::Names_t& contactLimbs)
    {
        core::Configuration_t old = fullBody->device_->currentConfiguration();
        model::Configuration_t config = dofArrayToConfig (fullBody->device_, configuration);
        fullBody->device_->currentConfiguration(config);
        fullBody->device_->computeForwardKinematics();
        std::vector<std::string> names = stringConversion(contactLimbs);
        for(std::vector<std::string>::const_iterator cit = names.begin(); cit != names.end();++cit)
        {
            rbprm::T_Limb::const_iterator lit = fullBody->GetLimbs().find(*cit);
            if(lit == fullBody->GetLimbs().end())
            {
                throw std::runtime_error ("Impossible to find limb for joint "
                                          + (*cit) + " to robot; limb not defined");
            }
            const core::JointPtr_t joint = fullBody->device_->getJointByName(lit->second->effector_->name());
            const fcl::Transform3f& transform =  joint->currentTransformation ();
            const fcl::Matrix3f& rot = transform.getRotation();
            state.contactPositions_[*cit] = transform.getTranslation();
            state.contactRotation_[*cit] = rot;
            state.contactNormals_[*cit] = fcl::Vec3f(rot(0,2),rot(1,2), rot(2,2));
            state.contacts_[*cit] = true;
            state.contactOrder_.push(*cit);
        }        
        state.nbContacts = state.contactNormals_.size() ;
        state.configuration_ = config;
        state.robustness =  stability::IsStable(fullBody,state);
        state.stable = state.robustness >= 0;
        fullBody->device_->currentConfiguration(old);
    }

    void RbprmBuilder::setStartState(const hpp::floatSeq& configuration, const hpp::Names_t& contactLimbs) throw (hpp::Error)
    {
        try
        {
            SetPositionAndNormal(startState_,fullBody_, configuration, contactLimbs);
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    void RbprmBuilder::setEndState(const hpp::floatSeq& configuration, const hpp::Names_t& contactLimbs) throw (hpp::Error)
    {
        try
        {
            SetPositionAndNormal(endState_,fullBody_, configuration, contactLimbs);
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    std::vector<State> TimeStatesToStates(const T_StateFrame& ref)
    {
        std::vector<State> res;
        for(CIT_StateFrame cit = ref.begin(); cit != ref.end(); ++cit)
        {
            res.push_back(cit->second);
        }
        return res;
    }

    floatSeqSeq* RbprmBuilder::interpolateConfigs(const hpp::floatSeqSeq& configs, double robustnessTreshold) throw (hpp::Error)
    {
        try
        {
            if(startState_.configuration_.rows() == 0)
            {
                throw std::runtime_error ("Start state not initialized, can not interpolate ");
            }
            if(endState_.configuration_.rows() == 0)
            {
                throw std::runtime_error ("End state not initialized, can not interpolate ");
            }
            std::vector<model::Configuration_t> configurations = doubleDofArrayToConfig(fullBody_->device_, configs);
            const affMap_t &affMap = problemSolver_->map
                        <std::vector<boost::shared_ptr<model::CollisionObject> > > ();
            if (affMap.empty ())
            {
                throw hpp::Error ("No affordances found. Unable to interpolate.");
            }
            hpp::rbprm::interpolation::RbPrmInterpolationPtr_t interpolator = rbprm::interpolation::RbPrmInterpolation::create(fullBody_,startState_,endState_);
            lastStatesComputedTime_ = interpolator->Interpolate(affMap, bindShooter_.affFilter_,configurations,robustnessTreshold);
            lastStatesComputed_ = TimeStatesToStates(lastStatesComputedTime_);
            hpp::floatSeqSeq *res;
            res = new hpp::floatSeqSeq ();

            res->length ((_CORBA_ULong)lastStatesComputed_.size ());
            std::size_t i=0;
            std::size_t id = 0;
            for(std::vector<State>::const_iterator cit = lastStatesComputed_.begin(); cit != lastStatesComputed_.end(); ++cit, ++id)
            {
                std::cout << "ID " << id;
                cit->print();
                const core::Configuration_t config = cit->configuration_;
                _CORBA_ULong size = (_CORBA_ULong) config.size ();
                double* dofArray = hpp::floatSeq::allocbuf(size);
                hpp::floatSeq floats (size, size, dofArray, true);
                //convert the config in dofseq
                for (model::size_type j=0 ; j < config.size() ; ++j) {
                  dofArray[j] = config [j];
                }
                (*res) [(_CORBA_ULong)i] = floats;
                ++i;
            }
            return res;
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }


    typedef Eigen::Matrix <value_type, 4, 3, Eigen::RowMajor> Matrix43;
    typedef Eigen::Matrix <value_type, 4, 3, Eigen::RowMajor> Rotation;
    typedef Eigen::Ref<Matrix43> Ref_matrix43;

    std::vector<fcl::Vec3f> computeRectangleContact(const rbprm::RbPrmFullBodyPtr_t device,
                                                    const rbprm::State& state)
    {
        std::vector<fcl::Vec3f> res;
        const rbprm::T_Limb& limbs = device->GetLimbs();
        rbprm::RbPrmLimbPtr_t limb;
        Matrix43 p; Eigen::Matrix3d R;
        for(std::map<std::string, fcl::Vec3f>::const_iterator cit = state.contactPositions_.begin();
            cit != state.contactPositions_.end(); ++cit)
        {
            const std::string& name = cit->first;
            const fcl::Vec3f& position = cit->second;
            limb = limbs.at(name);
            const double& lx = limb->x_, ly = limb->y_;
            p << lx,  ly, 0,
                 lx, -ly, 0,
                -lx, -ly, 0,
                -lx,  ly, 0;
            const fcl::Matrix3f& fclRotation = state.contactRotation_.at(name);
            for(int i =0; i< 3; ++i)
                for(int j =0; j<3;++j)
                    R(i,j) = fclRotation(i,j);
            for(std::size_t i =0; i<4; ++i)
            {
                res.push_back(position + (R*p.row(i).transpose()));
                res.push_back(state.contactNormals_.at(name));
            }
        }
        return res;
    }

    floatSeqSeq* RbprmBuilder::computeContactPoints(unsigned short cId) throw (hpp::Error)
    {
        if(lastStatesComputed_.size() <= cId + 1)
        {
            throw std::runtime_error ("Unexisting state " + std::string(""+(cId+1)));
        }
        const State& firstState = lastStatesComputed_[cId], thirdState = lastStatesComputed_[cId+1];
        std::vector<std::vector<fcl::Vec3f> > allStates;
        allStates.push_back(computeRectangleContact(fullBody_, firstState));
        std::vector<std::string> variations =thirdState.contactVariations(firstState);
        if(variations.size() >1)
        {
            throw std::runtime_error ("too many state variation between states" + std::string(""+cId) +
                                      "and " + std::string(""+(cId + 1)));
        }
        if(!variations.empty())
        {
std::cout << "variation " << variations[0] << std::endl;
            State intermediary(firstState);
            intermediary.RemoveContact(*variations.begin());
            allStates.push_back(computeRectangleContact(fullBody_, intermediary));
        }
        allStates.push_back(computeRectangleContact(fullBody_, thirdState));

        hpp::floatSeqSeq *res;
        res = new hpp::floatSeqSeq ();

        // compute array of contact positions

        res->length ((_CORBA_ULong)allStates.size());
        std::size_t i=0;
        for(std::vector<std::vector<fcl::Vec3f> >::const_iterator cit = allStates.begin();
                    cit != allStates.end(); ++cit, ++i)
        {
            const std::vector<fcl::Vec3f>& positions = *cit;
            _CORBA_ULong size = (_CORBA_ULong) positions.size () * 3;
            double* dofArray = hpp::floatSeq::allocbuf(size);
            hpp::floatSeq floats (size, size, dofArray, true);
            //convert the config in dofseq
            for(std::size_t h = 0; h<positions.size(); ++h)
            {
                for(std::size_t k =0; k<3; ++k)
                {
                    model::size_type j (h*3 + k);
                    dofArray[j] = positions[h][k];
                }
            }
            (*res) [(_CORBA_ULong)i] = floats;
        }
        return res;
    }

    floatSeqSeq* RbprmBuilder::interpolate(double timestep, double path, double robustnessTreshold) throw (hpp::Error)
    {
        try
        {
        unsigned int pathId = int(path);
        if(startState_.configuration_.rows() == 0)
        {
            throw std::runtime_error ("Start state not initialized, cannot interpolate ");
        }
        if(endState_.configuration_.rows() == 0)
        {
            throw std::runtime_error ("End state not initialized, cannot interpolate ");
        }

        if(problemSolver_->paths().size() <= pathId)
        {
            throw std::runtime_error ("No path computed, cannot interpolate ");
        }
        const affMap_t &affMap = problemSolver_->map
					<std::vector<boost::shared_ptr<model::CollisionObject> > > ();
        if (affMap.empty ())
        {
            throw hpp::Error ("No affordances found. Unable to interpolate.");
        }

        hpp::rbprm::interpolation::RbPrmInterpolationPtr_t interpolator = 
					rbprm::interpolation::RbPrmInterpolation::create(fullBody_,startState_,endState_,problemSolver_->paths()[pathId]);
        lastStatesComputedTime_ = interpolator->Interpolate(affMap, bindShooter_.affFilter_,
					timestep,robustnessTreshold);
		lastStatesComputed_ = TimeStatesToStates(lastStatesComputedTime_);

        hpp::floatSeqSeq *res;
        res = new hpp::floatSeqSeq ();

        res->length ((_CORBA_ULong)lastStatesComputed_.size ());
        std::size_t i=0;
        std::size_t id = 0;
        for(std::vector<State>::const_iterator cit = lastStatesComputed_.begin();
					cit != lastStatesComputed_.end(); ++cit, ++id)
        {
            std::cout << "ID " << id;
            cit->print();
            const core::Configuration_t config = cit->configuration_;
            _CORBA_ULong size = (_CORBA_ULong) config.size ();
            double* dofArray = hpp::floatSeq::allocbuf(size);
            hpp::floatSeq floats (size, size, dofArray, true);
            //convert the config in dofseq
            for (model::size_type j=0 ; j < config.size() ; ++j) {
              dofArray[j] = config [j];
            }
            (*res) [(_CORBA_ULong)i] = floats;
            ++i;
        }
        return res;
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    void AddPath(core::PathPtr_t path, core::ProblemSolverPtr_t ps)
    {
        core::PathVectorPtr_t resPath = core::PathVector::create(path->outputSize(), path->outputDerivativeSize());
        resPath->appendPath(path);
        ps->addPath(resPath);
    }

    void RbprmBuilder::interpolateBetweenStates(double state1, double state2, unsigned short numOptimizations) throw (hpp::Error)
    {
        try
        {
            std::size_t s1((std::size_t)state1), s2((std::size_t)state2);
            if(lastStatesComputed_.size () < s1 || lastStatesComputed_.size () < s2 )
            {
                throw std::runtime_error ("did not find a states at indicated indices: " + std::string(""+s1) + ", " + std::string(""+s2));
            }
            //create helper
//            /interpolation::LimbRRTHelper helper(fullBody_, problemSolver_->problem());
            core::PathPtr_t path = interpolation::interpolateStates<rbprm::interpolation::LimbRRTPath, CIT_State>(fullBody_,problemSolver_->problem(),
                                                                          lastStatesComputed_.begin()+s1,lastStatesComputed_.begin()+s2, numOptimizations);
            AddPath(path,problemSolver_);
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    void RbprmBuilder::interpolateBetweenStatesFromPath(double state1, double state2, unsigned short path, unsigned short numOptimizations) throw (hpp::Error)
    {
        try
        {
            std::size_t s1((std::size_t)state1), s2((std::size_t)state2);
            if(lastStatesComputed_.size () < s1 || lastStatesComputed_.size () < s2 )
            {
                throw std::runtime_error ("did not find a states at indicated indices: " + std::string(""+s1) + ", " + std::string(""+s2));
            }
            unsigned int pathId = (unsigned int)(path);
            if(problemSolver_->paths().size() <= pathId)
            {
                throw std::runtime_error ("No path computed, cannot interpolate ");
            }
            core::PathPtr_t path = interpolation::interpolateStates<rbprm::interpolation::LimbRRTPath>(fullBody_,problemSolver_->problem(), problemSolver_->paths()[pathId],
                                                                          lastStatesComputedTime_.begin()+s1,lastStatesComputedTime_.begin()+s2, numOptimizations);
            AddPath(path,problemSolver_);
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    void RbprmBuilder::saveComputedStates(const char* outfilename) throw (hpp::Error)
    {
        std::stringstream ss;
        ss << lastStatesComputed_.size()-2 << "\n";
        std::vector<rbprm::State>::iterator cit = lastStatesComputed_.begin()+1;
        int i = 0;
        ss << i++ << " ";
        cit->print(ss);
        for(std::vector<rbprm::State>::iterator cit2 = lastStatesComputed_.begin()+2;
            cit2 != lastStatesComputed_.end()-1; ++cit2, ++cit, ++i)
        {
            cit2->robustness = stability::IsStable(this->fullBody_, *cit2);
            ss << i<< " ";
            cit2->print(ss,*cit);
        }
        std::ofstream outfile;
        outfile.open(outfilename);
        if (outfile.is_open())
        {
            outfile << ss.rdbuf();
            outfile.close();
        }
        else
        {
            std::string error("Cannot open outfile " + std::string(outfilename));
            throw Error(error.c_str());
        }
    }

    void RbprmBuilder::saveLimbDatabase(const char* limbname,const char* filepath) throw (hpp::Error)
    {
        try
        {
        std::string limbName(limbname);
        std::ofstream fout;
        fout.open(filepath, std::fstream::out | std::fstream::app);
        rbprm::saveLimbInfoAndDatabase(fullBody_->GetLimbs().at(limbName),fout);
        //sampling::saveLimbDatabase(fullBody_->GetLimbs().at(limbName)->sampleContainer_,fout);
        fout.close();
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    hpp::floatSeqSeq* RbprmBuilder::getOctreeBoxes(const char* limbName, const hpp::floatSeq& configuration) throw (hpp::Error)
    {
        try
        {
        model::Configuration_t config = dofArrayToConfig (fullBody_->device_, configuration);
        model::Configuration_t save = fullBody_->device_->currentConfiguration();
        fullBody_->device_->currentConfiguration(config);
        fullBody_->device_->computeForwardKinematics();
        const std::map<std::size_t, fcl::CollisionObject*>& boxes =
                fullBody_->GetLimbs().at(std::string(limbName))->sampleContainer_.boxes_;
        const double resolution = fullBody_->GetLimbs().at(std::string(limbName))->sampleContainer_.resolution_;
        std::size_t i =0;
        hpp::floatSeqSeq *res;
        res = new hpp::floatSeqSeq ();
        res->length ((_CORBA_ULong)boxes.size ());
        for(std::map<std::size_t, fcl::CollisionObject*>::const_iterator cit = boxes.begin();
            cit != boxes.end();++cit,++i)
        {
            fcl::Vec3f position = (*cit->second).getTranslation();
            _CORBA_ULong size = (_CORBA_ULong) 4;
            double* dofArray = hpp::floatSeq::allocbuf(size);
            hpp::floatSeq floats (size, size, dofArray, true);
            //convert the config in dofseq
            for (model::size_type j=0 ; j < 3 ; ++j) {
                dofArray[j] = position[j];
            }
            dofArray[3] = resolution;
            (*res) [(_CORBA_ULong)i] = floats;
        }
        fullBody_->device_->currentConfiguration(save);
        fullBody_->device_->computeForwardKinematics();
        return res;
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    hpp::floatSeq* RbprmBuilder::getOctreeBox(const char* limbName, double octreeNodeId) throw (hpp::Error)
    {
        try
        {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        long ocId ((long)octreeNodeId);
        const T_Limb& limbs = fullBody_->GetLimbs();
        T_Limb::const_iterator lit = limbs.find(std::string(limbName));
        if(lit == limbs.end())
        {
            std::string err("No limb " + std::string(limbName) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        const std::map<std::size_t, fcl::CollisionObject*>& boxes =
                fullBody_->GetLimbs().at(std::string(limbName))->sampleContainer_.boxes_;
        std::map<std::size_t, fcl::CollisionObject*>::const_iterator cit = boxes.find(ocId);
        if(cit == boxes.end())
        {
            std::stringstream ss; ss << ocId;
            std::string err("No octree node with id " + ss.str() + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        const fcl::CollisionObject* box = cit->second;
        const fcl::Vec3f& pos = box->getTransform().getTranslation();
        hpp::floatSeq* dofArray = new hpp::floatSeq();
        dofArray->length(4);
        for(std::size_t i=0; i< 3; ++i)
        {
          (*dofArray)[(_CORBA_ULong)i] = pos[i];
        }
        (*dofArray)[(_CORBA_ULong)3] = fullBody_->GetLimbs().at(std::string(limbName))->sampleContainer_.resolution_;
        return dofArray;
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    hpp::floatSeq* RbprmBuilder::getOctreeTransform(const char* limbName, const hpp::floatSeq& configuration) throw (hpp::Error)
    {
        try{
        model::Configuration_t config = dofArrayToConfig (fullBody_->device_, configuration);
        model::Configuration_t save = fullBody_->device_->currentConfiguration();
        fullBody_->device_->currentConfiguration(config);
        fullBody_->device_->computeForwardKinematics();
        const hpp::rbprm::RbPrmLimbPtr_t limb =fullBody_->GetLimbs().at(std::string(limbName));
        const fcl::Transform3f transform = limb->octreeRoot();
        const fcl::Quaternion3f& quat = transform.getQuatRotation();
        const fcl::Vec3f& position = transform.getTranslation();
        hpp::floatSeq *dofArray;
        dofArray = new hpp::floatSeq();
        dofArray->length(_CORBA_ULong(7));
        for(std::size_t i=0; i< 3; i++)
          (*dofArray)[(_CORBA_ULong)i] = position [i];
        for(std::size_t i=0; i< 4; i++)
          (*dofArray)[(_CORBA_ULong)i+3] = quat [i];
        fullBody_->device_->currentConfiguration(save);
        fullBody_->device_->computeForwardKinematics();
        return dofArray;
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    CORBA::Short RbprmBuilder::isConfigBalanced(const hpp::floatSeq& configuration, const hpp::Names_t& contactLimbs, double robustnessTreshold) throw (hpp::Error)
    {
        try{
        rbprm::State testedState;
        model::Configuration_t config = dofArrayToConfig (fullBody_->device_, configuration);
        model::Configuration_t save = fullBody_->device_->currentConfiguration();
        std::vector<std::string> names = stringConversion(contactLimbs);
        for(std::vector<std::string>::const_iterator cit = names.begin(); cit != names.end();++cit)
        {
            std::cout << "name " << * cit << std::endl;
            const hpp::rbprm::RbPrmLimbPtr_t limb =fullBody_->GetLimbs().at(std::string(*cit));
            testedState.contacts_[*cit] = true;
            testedState.contactPositions_[*cit] = limb->effector_->currentTransformation().getTranslation();
            testedState.contactRotation_[*cit] = limb->effector_->currentTransformation().getRotation();
            // normal given by effector normal
            const fcl::Vec3f normal = limb->effector_->currentTransformation().getRotation() * limb->normal_;
            testedState.contactNormals_[*cit] = normal;
            testedState.configuration_ = config;
            ++testedState.nbContacts;
        }
        fullBody_->device_->currentConfiguration(save);
        fullBody_->device_->computeForwardKinematics();
        if (stability::IsStable(fullBody_, testedState) >= robustnessTreshold)
        {
            return (CORBA::Short)(1);
        }
        else
        {
            return (CORBA::Short)(0);
        }
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }


    void RbprmBuilder::runSampleAnalysis(const char* analysis, double isstatic) throw (hpp::Error)
    {
        try
        {
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        std::string eval(analysis);
        if (eval == "all")
        {
            for(sampling::T_evaluate::const_iterator analysisit = analysisFactory_->evaluate_.begin();
                analysisit != analysisFactory_->evaluate_.end(); ++ analysisit)
            {
                for(T_Limb::const_iterator cit = fullBody_->GetLimbs().begin(); cit !=fullBody_->GetLimbs().end();++cit)
                {
                    sampling::SampleDB & sampleDB =const_cast<sampling::SampleDB &> (cit->second->sampleContainer_);
                    sampling::addValue(sampleDB, analysisit->first, analysisit->second, isstatic > 0.5, isstatic > 0.5);
                }
            }
        }
        else
        {
            sampling::T_evaluate::const_iterator analysisit = analysisFactory_->evaluate_.find(std::string(eval));
            if(analysisit == analysisFactory_->evaluate_.end())
            {
                std::string err("No analysis named  " + eval + "was defined for analyzing database sample");
                throw Error (err.c_str());
            }
            for(T_Limb::const_iterator cit = fullBody_->GetLimbs().begin(); cit !=fullBody_->GetLimbs().end();++cit)
            {
                sampling::SampleDB & sampleDB =const_cast<sampling::SampleDB &> (cit->second->sampleContainer_);
                sampling::addValue(sampleDB, analysisit->first, analysisit->second, isstatic > 0.5, isstatic > 0.5);
            }
        }
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    hpp::floatSeq* RbprmBuilder::runLimbSampleAnalysis(const char* limbname, const char* analysis, double isstatic) throw (hpp::Error)
    {
        try
        {
        rbprm::sampling::ValueBound bounds;
        if(!fullBodyLoaded_)
            throw Error ("No full body robot was loaded");
        T_Limb::const_iterator lit = fullBody_->GetLimbs().find(std::string(limbname));
        if(lit == fullBody_->GetLimbs().end())
        {
            std::string err("No limb " + std::string(limbname) + "was defined for robot" + fullBody_->device_->name());
            throw Error (err.c_str());
        }
        std::string eval(analysis);
        if (eval == "all")
        {
            for(sampling::T_evaluate::const_iterator analysisit = analysisFactory_->evaluate_.begin();
                analysisit != analysisFactory_->evaluate_.end(); ++ analysisit)
            {
                sampling::SampleDB & sampleDB =const_cast<sampling::SampleDB &> (lit->second->sampleContainer_);
                sampling::addValue(sampleDB, analysisit->first, analysisit->second, isstatic > 0.5, isstatic > 0.5);
                bounds = sampleDB.valueBounds_[analysisit->first];
            }
        }
        else
        {
            sampling::T_evaluate::const_iterator analysisit = analysisFactory_->evaluate_.find(std::string(eval));
            if(analysisit == analysisFactory_->evaluate_.end())
            {
                std::string err("No analysis named  " + eval + "was defined for analyzing database sample");
                throw Error (err.c_str());
            }
            sampling::SampleDB & sampleDB =const_cast<sampling::SampleDB &> (lit->second->sampleContainer_);
            sampling::addValue(sampleDB, analysisit->first, analysisit->second, isstatic > 0.5, isstatic > 0.5);
            bounds = sampleDB.valueBounds_[analysisit->first];
        }
        hpp::floatSeq* dofArray = new hpp::floatSeq();
        dofArray->length(2);
        (*dofArray)[0] = bounds.first;
        (*dofArray)[1] = bounds.second;
        return dofArray;
        }
        catch(std::runtime_error& e)
        {
            throw Error(e.what());
        }
    }

    void RbprmBuilder::SetProblemSolver (hpp::core::ProblemSolverPtr_t problemSolver)
    {
        problemSolver_ = problemSolver;
        bindShooter_.problemSolver_ = problemSolver;
        //bind shooter creator to hide problem as a parameter and respect signature

        // add rbprmshooter
        problemSolver->add<core::ConfigurationShooterBuilder_t>("RbprmShooter",
                                                   boost::bind(&BindShooter::create, boost::ref(bindShooter_), _1));
        problemSolver->add<core::PathValidationBuilder_t>("RbprmPathValidation",
                                                   boost::bind(&BindShooter::createPathValidation, boost::ref(bindShooter_), _1, _2));
    }

    } // namespace impl
  } // namespace rbprm
} // namespace hpp
